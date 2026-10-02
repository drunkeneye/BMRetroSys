//
// atarist_video.cpp
//
// Hatari video presentation for BMX bare-metal.
//
// The Shifter conversion (conv_st.c, driven by Screen_Draw inside the VBL
// handler) renders the ST frame into a 32-bit XRGB buffer owned by the
// atarist_bmx_screen backend. This module blits that buffer to the Circle
// RGB565 FBL (640x400, Phase 1: overscan off) and presents it.
//
// ST modes: low with overscan (up to 832x552, doubled by ConvST),
// medium, and mono (640x400). FBL fits the overscan maximum.
//

#include "atarist_video.h"
#include "atarist_debug.h"
#include "src/monitor_phosphor.h"

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
namespace atarist {

namespace {

// FBL owned by this module (VIC layer, direct-color RGB565).
uint8_t *s_fb = NULL;
int s_fb_pitch = 0;

const int kWidth = 832;
const int kHeight = 576;

unsigned long s_present_count = 0;

// Active CRT monitor type (staged via settings before video_init; the
// per-pixel mapping below reads it live, so menu changes apply instantly).
int s_monitor = BMX_MONITOR_COLOR;

uint16_t xrgb_to_565(uint32_t px) {
  uint32_t r = (px >> 16) & 0xff;
  uint32_t g = (px >> 8) & 0xff;
  uint32_t b = px & 0xff;
  return bmx_monitor_map_565(
      (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)), s_monitor);
}

// ---- Menu color adjustments (brightness/contrast/gamma/tint/saturation)
// Sliders: 0..2000 (gamma 0..4000), 1000 = neutral. Same math as the
// DOSBox-X kernel: brightness/contrast/gamma fold into a 256-entry
// per-channel LUT rebuilt on every set; saturation (gray-axis mix) and
// tint (hue rotation about the gray axis) run per pixel. Neutral settings
// take the direct fast path above.
int s_col_bri = 1000;
int s_col_con = 1000;
int s_col_gam = 1000;
int s_col_tin = 1000;
int s_col_sat = 1000;
uint8_t s_chan_lut[256] = {0};
// Hue-rotation matrix (row-major 3x3), identity when tint is neutral.
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

uint16_t xrgb32_to_rgb565_adjusted(uint32_t p) {
  float r = (float)s_chan_lut[(p >> 16) & 0xff] / 255.0f;
  float g = (float)s_chan_lut[(p >> 8) & 0xff] / 255.0f;
  float b = (float)s_chan_lut[p & 0xff] / 255.0f;
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
  return bmx_monitor_map_565((uint16_t)((ri << 11) | (gi << 5) | bi),
                             s_monitor);
}

}  // namespace

// 32-bit conversion buffer from the screen backend (atarist_bmx_screen.c,
// C linkage).
extern "C" uint32_t *atarist_bmx_screen_pixels(int *w, int *h);

void video_init(void) {
  uint8_t *pixels = NULL;
  int pitch = 0;

  circle_alloc_fbl(FB_LAYER_VIC, 1, &pixels, kWidth, kHeight, &pitch);
  debug_log("Hatari video_init: fbl pixels=%p pitch=%d (%dx%d)",
            (const void *)pixels, pitch, kWidth, kHeight);
  if (pixels == NULL || pitch == 0) {
    debug_log("Hatari video_init FAILED");
    return;
  }
  {
    int dpx = 0, dpy = 0, fbw = 0, fbh = 0, sw = 0, sh = 0, dw = 0, dh = 0;
    circle_get_fbl_dimensions(FB_LAYER_VIC, &dpx, &dpy, &fbw, &fbh, &sw, &sh,
                              &dw, &dh);
    debug_log("Hatari video: display=%dx%d", dpx, dpy);
  }
  emux_geometry_changed(FB_LAYER_VIC);

  canvas_state[VIC_INDEX].fb_width = kWidth;
  canvas_state[VIC_INDEX].fb_height = kHeight;
  canvas_state[VIC_INDEX].gfx_w = 832;
  canvas_state[VIC_INDEX].gfx_h = 576;
  canvas_state[VIC_INDEX].max_border_w = 0;
  canvas_state[VIC_INDEX].max_border_h = 0;
  canvas_state[VIC_INDEX].max_padding_w = 0;
  canvas_state[VIC_INDEX].max_padding_h = 0;
  canvas_state[VIC_INDEX].border_w = 0;
  canvas_state[VIC_INDEX].border_h = 0;

  s_fb = pixels;
  s_fb_pitch = pitch;
  rebuild_color_tables();

  circle_clear_fbl(FB_LAYER_VIC);
  circle_show_fbl(FB_LAYER_VIC);
  circle_present_fbl(FB_LAYER_MASK(FB_LAYER_VIC), 1);
}

void blit_frame(void) {
  if (s_fb == NULL) {
    return;
  }
  // Blit the Hatari 32-bit frame into the RGB565 FBL (top-left; clamped
  // to the overscan-sized FBL). NOTE: source rows are
  // always s_width wide (use the real pitch, not the clamped width).
  int w = 0, h = 0;
  uint32_t *src = atarist_bmx_screen_pixels(&w, &h);
  if (src != NULL && w > 0 && h > 0) {
    int src_stride = w;
    if (w > kWidth) {
      w = kWidth;
    }
    if (h > kHeight) {
      h = kHeight;
    }
    uint16_t *dst = reinterpret_cast<uint16_t *>(s_fb);
    int stride = s_fb_pitch / 2;
    const bool neutral = colors_neutral();
    for (int y = 0; y < h; y++) {
      uint32_t *srow = src + (size_t)y * (size_t)src_stride;
      uint16_t *drow = dst + (size_t)y * (size_t)stride;
      if (neutral) {
        for (int x = 0; x < w; x++) {
          drow[x] = xrgb_to_565(srow[x]);
        }
      } else {
        for (int x = 0; x < w; x++) {
          drow[x] = xrgb32_to_rgb565_adjusted(srow[x]);
        }
      }
    }
    // Clear any leftover region on shrink (e.g. mono -> color switch).
    if (w < kWidth) {
      for (int y = 0; y < h; y++) {
        uint16_t *drow = dst + (size_t)y * (size_t)stride;
        for (int x = w; x < kWidth; x++) {
          drow[x] = 0;
        }
      }
    }
    if (h < kHeight) {
      memset(dst + (size_t)h * (size_t)stride, 0,
             (size_t)(kHeight - h) * (size_t)stride * sizeof(uint16_t));
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

// Repaints the canvas from the last converted frame with current color
// settings: the guest stalls while the menu is open, so without this the
// sliders would appear dead until the menu closes.
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
  debug_log("video_set_color_adjustments: bri=%d con=%d gam=%d tin=%d sat=%d",
            brightness, contrast, gamma, tint, saturation);
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

void video_set_monitor(int mode) {
  if (!bmx_monitor_valid(mode)) {
    return;
  }
  s_monitor = mode;
}

int video_get_monitor(void) { return s_monitor; }

bool video_save_screenshot(const char *path) {
  if (s_fb == NULL || path == NULL) {
    return false;
  }
  // Uncompressed 24-bit BMP straight from the RGB565 FBL.
  FILE *fp = fopen(path, "wb");
  if (fp == NULL) {
    return false;
  }
  const int row_bytes = kWidth * 3;
  const int pad = (4 - (row_bytes % 4)) % 4;
  const uint32_t img_size = (uint32_t)(row_bytes + pad) * (uint32_t)kHeight;
  uint8_t hdr[54];
  memset(hdr, 0, sizeof(hdr));
  hdr[0] = 'B';
  hdr[1] = 'M';
  uint32_t file_size = img_size + 54;
  memcpy(hdr + 2, &file_size, 4);
  hdr[10] = 54;
  hdr[14] = 40;
  memcpy(hdr + 18, &kWidth, 4);
  int height = kHeight;
  memcpy(hdr + 22, &height, 4);
  hdr[26] = 1;
  hdr[28] = 24;
  memcpy(hdr + 34, &img_size, 4);
  bool ok = fwrite(hdr, 1, sizeof(hdr), fp) == sizeof(hdr);
  uint16_t *fb = reinterpret_cast<uint16_t *>(s_fb);
  int stride = s_fb_pitch / 2;
  uint8_t pad_bytes[3] = {0, 0, 0};
  for (int y = kHeight - 1; ok && y >= 0; y--) {
    for (int x = 0; x < kWidth; x++) {
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

}  // namespace atarist
}  // namespace bmx
