//
// amiga_video.cpp
//
// Amiga video presentation for BMX bare-metal.
//
// Upstream renders each PAL frame into a 912x313 super-hires Texture
// (one u64 Texel per hires pixel). This module blits the full visible
// PAL frame (blanking removed, border kept: 832x286) 1:1 at the ORIGIN
// of a fixed 832x576 RGB565 FBL. Origin (not centered!) matters: the
// display scaler samples src_rect (canvas gfx 832x286), so letterbox
// bars must not be part of the sampled area -- otherwise stretch
// factors scale black bars and clamp before the content grows.
// Presented on FB_LAYER_VIC.
//

#include "amiga_video.h"
#include "amiga_video_tex.h"
#include "amiga_config.h"
#include "amiga_debug.h"

extern "C" {
#include "third_party/common/circle.h"
#include "third_party/common/emux_api.h"
}

#include <circle/timer.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace bmx {
namespace amiga {

namespace {

// Fixed FBL: fits the full PAL visible frame (832x286) with room to spare.
const int kFbW = 832;
const int kFbH = 576;

// Active texel packing, same values as vamiga::TexFormat (0=ABGR,
// 1=ARGB, 2=RGBA). Read once at video_init; nothing changes it later.
int s_tex_fmt = 2;

// Visible content box in texture pixels (refreshed periodically).
int s_box_x0 = 0;
int s_box_y0 = 0;
int s_box_w = 912;
int s_box_h = 313;

uint8_t *s_fb = NULL;
int s_fb_pitch = 0;
unsigned long s_present_count = 0;

int s_col_bri = 1000;
int s_col_con = 1000;
int s_col_gam = 1000;
int s_col_tin = 1000;
int s_col_sat = 1000;
uint8_t s_chan_lut[256] = {0};
float s_hue_mat[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

float clamp01(float x) {
  if (x < 0.0f) {
    return 0.0f;
  }
  if (x > 1.0f) {
    return 1.0f;
  }
  return x;
}

void rebuild_color_tables(void) {
  const float c = (float)s_col_con / 1000.0f;
  const float b = (float)(s_col_bri - 1000) / 1000.0f;
  float g = (float)s_col_gam / 1000.0f;
  if (g < 0.1f) {
    g = 0.1f;
  }
  if (g > 4.0f) {
    g = 4.0f;
  }
  for (int i = 0; i < 256; i++) {
    float x = clamp01((float)i / 255.0f * c + b);
    s_chan_lut[i] = (uint8_t)(powf(x, g) * 255.0f + 0.5f);
  }
  const float ang = (float)(s_col_tin - 1000) / 1000.0f * 3.14159265f;
  const float co = cosf(ang);
  const float si = sinf(ang);
  s_hue_mat[0] = (float)(0.299 + 0.701 * co + 0.168 * si);
  s_hue_mat[1] = (float)(0.587 - 0.587 * co + 0.330 * si);
  s_hue_mat[2] = (float)(0.114 - 0.114 * co - 0.497 * si);
  s_hue_mat[3] = (float)(0.299 - 0.299 * co - 0.328 * si);
  s_hue_mat[4] = (float)(0.587 + 0.413 * co + 0.035 * si);
  s_hue_mat[5] = (float)(0.114 - 0.114 * co + 0.292 * si);
  s_hue_mat[6] = (float)(0.299 - 0.300 * co + 1.250 * si);
  s_hue_mat[7] = (float)(0.587 - 0.588 * co - 1.050 * si);
  s_hue_mat[8] = (float)(0.114 + 0.886 * co - 0.203 * si);
}

bool colors_neutral(void) {
  return s_col_bri == 1000 && s_col_con == 1000 && s_col_gam == 1000 &&
         s_col_tin == 1000 && s_col_sat == 1000;
}

inline void split_channels(uint32_t v, int *r, int *g, int *b) {
  // ABGR packs 0xFFBBGGRR, ARGB 0xFFRRGGBB, RGBA 0xRRGGBBFF.
  if (s_tex_fmt == 0) {
    *r = (int)(v & 0xff);
    *g = (int)((v >> 8) & 0xff);
    *b = (int)((v >> 16) & 0xff);
  } else if (s_tex_fmt == 1) {
    *r = (int)((v >> 16) & 0xff);
    *g = (int)((v >> 8) & 0xff);
    *b = (int)(v & 0xff);
  } else {
    *r = (int)((v >> 24) & 0xff);
    *g = (int)((v >> 16) & 0xff);
    *b = (int)((v >> 8) & 0xff);
  }
}

inline uint16_t rgba32_to_565(uint32_t v) {
  int r = 0, g = 0, b = 0;
  split_channels(v, &r, &g, &b);
  return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
}

uint16_t rgba32_to_565_adjusted(uint32_t v) {
  int ri0 = 0, gi0 = 0, bi0 = 0;
  split_channels(v, &ri0, &gi0, &bi0);
  float r = (float)s_chan_lut[ri0] / 255.0f;
  float g = (float)s_chan_lut[gi0] / 255.0f;
  float b = (float)s_chan_lut[bi0] / 255.0f;
  const float s = (float)s_col_sat / 1000.0f;
  if (s != 1.0f) {
    const float luma = 0.299f * r + 0.587f * g + 0.114f * b;
    r = luma + (r - luma) * s;
    g = luma + (g - luma) * s;
    b = luma + (b - luma) * s;
  }
  if (s_col_tin != 1000) {
    const float ir = r;
    const float ig = g;
    const float ib = b;
    r = s_hue_mat[0] * ir + s_hue_mat[1] * ig + s_hue_mat[2] * ib;
    g = s_hue_mat[3] * ir + s_hue_mat[4] * ig + s_hue_mat[5] * ib;
    b = s_hue_mat[6] * ir + s_hue_mat[7] * ig + s_hue_mat[8] * ib;
  }
  const int ri = (int)(clamp01(r) * 31.0f + 0.5f);
  const int gi = (int)(clamp01(g) * 63.0f + 0.5f);
  const int bi = (int)(clamp01(b) * 31.0f + 0.5f);
  return (uint16_t)((ri << 11) | (gi << 5) | bi);
}

}  // namespace

void video_init(void) {
  uint8_t *pixels = NULL;
  int pitch = 0;

  circle_alloc_fbl(FB_LAYER_VIC, 1, &pixels, kFbW, kFbH, &pitch);
  debug_log("Amiga video_init: fbl pixels=%p pitch=%d (%dx%d)",
            (const void *)pixels, pitch, kFbW, kFbH);
  if (pixels == NULL || pitch == 0) {
    debug_log("Amiga video_init FAILED");
    return;
  }

  canvas_state[VIC_INDEX].fb_width = kFbW;
  canvas_state[VIC_INDEX].fb_height = kFbH;
  // Sampled content size: must match the blit origin/size below so the
  // scaler (and h/v-stretch) operate on content, not letterbox.
  canvas_state[VIC_INDEX].gfx_w = 832;
  canvas_state[VIC_INDEX].gfx_h = 286;
  canvas_state[VIC_INDEX].max_border_w = 0;
  canvas_state[VIC_INDEX].max_border_h = 0;
  canvas_state[VIC_INDEX].max_padding_w = 0;
  canvas_state[VIC_INDEX].max_padding_h = 0;
  canvas_state[VIC_INDEX].border_w = 0;
  canvas_state[VIC_INDEX].border_h = 0;

  s_fb = pixels;
  s_fb_pitch = pitch;
  rebuild_color_tables();

  // Program stretch/src_rect NOW with the real gfx size. ui_init_menu()
  // already ran do_video_settings() while gfx was still 0x0, and
  // circle_alloc_fbl() above reset src to the full 832x576 FBL. Without
  // this, the scaler samples content (832x286 at origin) + bottom-half
  // letterbox as one frame -> picture squeezed into the upper half until
  // the user nudges h/v-stretch (which re-runs do_video_settings).
  // Same pattern as VICE draw_buffer_alloc -> emux_frame_buffer_changed.
  emux_frame_buffer_changed(FB_LAYER_VIC);

  s_tex_fmt = video_tex_format();
  debug_log("Amiga video_init: tex_format=%d (0=ABGR,1=ARGB,2=RGBA)", s_tex_fmt);
  {
    int bx0 = 0, by0 = 0, bw = 0, bh = 0;
    if (video_content_box(&bx0, &by0, &bw, &bh) && bw <= kFbW && bh <= kFbH) {
      s_box_x0 = bx0;
      s_box_y0 = by0;
      s_box_w = bw;
      s_box_h = bh;
    }
    debug_log("Amiga video_init: content box %dx%d+%d+%d", s_box_w, s_box_h,
              s_box_x0, s_box_y0);
  }

  circle_clear_fbl(FB_LAYER_VIC);
  circle_show_fbl(FB_LAYER_VIC);
  circle_present_fbl(FB_LAYER_MASK(FB_LAYER_VIC), 1);
}

void blit_frame(void) {
  if (s_fb == NULL) {
    return;
  }
  // Stable emulator texture: one u64 Texel per pixel.
  int tw = 0, th = 0;
  const uint64_t *pixels = video_lock_pixels(&tw, &th);
  if (pixels == NULL || tw <= 0 || th <= 0) {
    return;
  }
  // Refresh the content box a few times per second (mode switches,
  // border on/off). Cheap enough to run inline.
  if ((s_present_count % 30) == 0) {
    int bx0 = 0, by0 = 0, bw = 0, bh = 0;
    if (video_content_box(&bx0, &by0, &bw, &bh) && bw <= kFbW && bh <= kFbH) {
      if (bw != s_box_w || bh != s_box_h || bx0 != s_box_x0 ||
          by0 != s_box_y0) {
        debug_log("video: content box %dx%d+%d+%d (frame %lu)", bw, bh, bx0,
                  by0, s_present_count);
      }
      s_box_x0 = bx0;
      s_box_y0 = by0;
      s_box_w = bw;
      s_box_h = bh;
    }
  }
  // Content at FBL origin (see header comment): the scaler samples
  // canvas gfx (832x286) from (0,0), display-side centering applies.
  int copy_w = s_box_w < kFbW ? s_box_w : kFbW;
  int copy_h = s_box_h < kFbH ? s_box_h : kFbH;
  if (copy_w > 832) {
    copy_w = 832;
  }
  if (copy_h > 286) {
    copy_h = 286;
  }

  uint16_t *dst = reinterpret_cast<uint16_t *>(s_fb);
  int stride = s_fb_pitch / 2;
  // Clear the whole FBL, then blit the box 1:1 at the origin.
  memset(dst, 0, (size_t)kFbH * (size_t)stride * sizeof(uint16_t));
  const bool neutral = colors_neutral();
  for (int y = 0; y < copy_h; y++) {
    int sy = s_box_y0 + y;
    if (sy < 0 || sy >= th) {
      continue;
    }
    uint16_t *drow = dst + (size_t)y * (size_t)stride;
    for (int x = 0; x < copy_w; x++) {
      int sx = s_box_x0 + x;
      if (sx < 0 || sx >= tw) {
        continue;
      }
      uint64_t t = pixels[(size_t)sy * (size_t)tw + (size_t)sx];
      uint32_t rgba = (uint32_t)(t & 0xffffffffu);
      drow[x] = neutral ? rgba32_to_565(rgba) : rgba32_to_565_adjusted(rgba);
    }
  }
}

void video_render(void) {
  if (s_fb == NULL) {
    return;
  }
  emux_ensure_video();
  blit_frame();
  circle_present_fbl(FB_LAYER_MASK(FB_LAYER_VIC), 0);
  s_present_count++;
}

void video_refresh_preview(void) {
  if (s_fb == NULL) {
    return;
  }
  emux_ensure_video();
  blit_frame();
  circle_present_fbl(FB_LAYER_MASK(FB_LAYER_VIC), 0);
}

void video_set_color_adjustments(int brightness, int contrast, int gamma,
                                 int tint, int saturation) {
  s_col_bri = brightness;
  s_col_con = contrast;
  s_col_gam = gamma;
  s_col_tin = tint;
  s_col_sat = saturation;
  rebuild_color_tables();
  video_refresh_preview();
}

void video_get_color_adjustments(int *brightness, int *contrast, int *gamma,
                                 int *tint, int *saturation) {
  if (brightness != NULL) {
    *brightness = s_col_bri;
  }
  if (contrast != NULL) {
    *contrast = s_col_con;
  }
  if (gamma != NULL) {
    *gamma = s_col_gam;
  }
  if (tint != NULL) {
    *tint = s_col_tin;
  }
  if (saturation != NULL) {
    *saturation = s_col_sat;
  }
}

bool video_save_screenshot(const char *path) {
  if (s_fb == NULL || path == NULL) {
    return false;
  }
  FILE *fp = fopen(path, "wb");
  if (fp == NULL) {
    return false;
  }
  const int row_bytes = kFbW * 3;
  const int pad = (4 - (row_bytes % 4)) % 4;
  const uint32_t img_size = (uint32_t)(row_bytes + pad) * (uint32_t)kFbH;
  uint8_t hdr[54];
  memset(hdr, 0, sizeof(hdr));
  hdr[0] = 'B';
  hdr[1] = 'M';
  uint32_t file_size = img_size + 54;
  memcpy(hdr + 2, &file_size, 4);
  hdr[10] = 54;
  hdr[14] = 40;
  memcpy(hdr + 18, &kFbW, 4);
  int height = kFbH;
  memcpy(hdr + 22, &height, 4);
  hdr[26] = 1;
  hdr[28] = 24;
  memcpy(hdr + 34, &img_size, 4);
  bool ok = fwrite(hdr, 1, sizeof(hdr), fp) == sizeof(hdr);
  uint16_t *fb = reinterpret_cast<uint16_t *>(s_fb);
  int stride = s_fb_pitch / 2;
  uint8_t pad_bytes[3] = {0, 0, 0};
  for (int y = kFbH - 1; ok && y >= 0; y--) {
    for (int x = 0; x < kFbW; x++) {
      uint16_t px = fb[(size_t)y * (size_t)stride + (size_t)x];
      uint8_t bgr[3];
      bgr[0] = (uint8_t)(((px) & 0x1f) << 3);
      bgr[1] = (uint8_t)(((px >> 5) & 0x3f) << 2);
      bgr[2] = (uint8_t)(((px >> 11) & 0x1f) << 3);
      if (fwrite(bgr, 1, 3, fp) != 3) {
        ok = false;
        break;
      }
    }
    if (ok && pad > 0 && fwrite(pad_bytes, 1, (size_t)pad, fp) != (size_t)pad) {
      ok = false;
    }
  }
  fclose(fp);
  return ok;
}

void video_present_stats(unsigned long *presents, unsigned long *ensure_us,
                         unsigned long *present_us) {
  if (presents != NULL) {
    *presents = s_present_count;
  }
  if (ensure_us != NULL) {
    *ensure_us = 0;
  }
  if (present_us != NULL) {
    *present_us = 0;
  }
}

}  // namespace amiga
}  // namespace bmx
