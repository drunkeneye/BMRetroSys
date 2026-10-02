//
// a2600_video.cpp
//
// See a2600_video.h. Framebuffer is 352x304 (same geometry as the
// Spectrum kernel); the Stella TIA frame (up to 320 wide, height varies
// by ROM/region) is blitted centered. Same scaler story as the other
// kernels.
//

#include "a2600_video.h"
#include "a2600_debug.h"
#include "a2600_splash.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include <circle/timer.h>

extern "C" {
#include "third_party/common/circle.h"
#include "third_party/common/emux_api.h"
#include "third_party/common/ui.h"
extern struct menu_item *h_border_item[];
extern struct menu_item *v_border_item[];
}

namespace bmx {
namespace a2600 {

namespace {

const int kFbWidth = 352;
const int kFbHeight = 304;
const int kGfxW = 320;
const int kGfxH = 240;

uint8_t *s_pixels = nullptr;
int s_pitch = 0;
unsigned long s_presents = 0;
unsigned long s_frames = 0;

int s_col_bri = 1000;
int s_col_con = 1000;
int s_col_gam = 1000;
int s_col_tin = 1000;
int s_col_sat = 1000;

// Active CRT monitor type (staged via settings before video_init; the
// per-pixel mapping below reads it live, so menu changes apply instantly).
int s_monitor = A2600_MONITOR_COLOR;

// Per-channel LUT for brightness/contrast/gamma (neutral = identity).
uint8_t s_chan_lut[256];
bool s_lut_ready = false;

inline float clamp01(float x) {
  if (x < 0.0f) return 0.0f;
  if (x > 1.0f) return 1.0f;
  return x;
}

void rebuild_color_tables(void) {
  const float c = (float)s_col_con / 1000.0f;
  const float b = (float)(s_col_bri - 1000) / 1000.0f;
  float g = (float)s_col_gam / 1000.0f;
  if (g < 0.1f) g = 0.1f;
  if (g > 4.0f) g = 4.0f;
  for (int i = 0; i < 256; i++) {
    float x = clamp01((float)i / 255.0f * c + b);
    // Integer power approximation: x^g via exp2(g*log2(x)).
    float y = (x <= 0.0f) ? 0.0f : exp2f(g * log2f(x));
    s_chan_lut[i] = (uint8_t)(y * 255.0f + 0.5f);
  }
  s_lut_ready = true;
}

inline bool colors_neutral(void) {
  return s_col_bri == 1000 && s_col_con == 1000 && s_col_gam == 1000 &&
         s_col_tin == 1000 && s_col_sat == 1000;
}

// Luma-driven phosphor mapping (Atari800 pattern): real green/amber
// monitors showed luma only. Applied last, so the color sliders cannot
// re-tint the phosphor; Color mode is the identity (default look unchanged).
inline uint16_t monitor_map_565(uint16_t p) {
  if (s_monitor == A2600_MONITOR_COLOR) {
    return p;
  }
  const int r = ((p >> 11) & 31) * 255 / 31;
  const int g = ((p >> 5) & 63) * 255 / 63;
  const int b = (p & 31) * 255 / 31;
  const int luma = (299 * r + 587 * g + 114 * b) / 1000;
  int ro = luma, go = luma, bo = luma;
  if (s_monitor == A2600_MONITOR_GREEN) {
    ro = 0;
    bo = 0;
  } else if (s_monitor == A2600_MONITOR_AMBER) {
    // Amber ~ #FFB000: G/R = 176/255.
    go = (luma * 176) / 255;
    bo = 0;
  }
  return (uint16_t)(((ro >> 3) << 11) | ((go >> 2) << 5) | (bo >> 3));
}

inline uint16_t rgb888_to_rgb565(uint32_t rgb) {
  const int r = (rgb >> 16) & 0xff;
  const int g = (rgb >> 8) & 0xff;
  const int b = rgb & 0xff;
  if (colors_neutral()) {
    return monitor_map_565(
        (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)));
  }
  if (!s_lut_ready) rebuild_color_tables();
  float fr = (float)s_chan_lut[r] / 255.0f;
  float fg = (float)s_chan_lut[g] / 255.0f;
  float fb = (float)s_chan_lut[b] / 255.0f;
  const float s = (float)s_col_sat / 1000.0f;
  if (s != 1.0f) {
    const float luma = 0.299f * fr + 0.587f * fg + 0.114f * fb;
    fr = luma + (fr - luma) * s;
    fg = luma + (fg - luma) * s;
    fb = luma + (fb - luma) * s;
  }
  if (s_col_tin != 1000) {
    const float ang = (float)(s_col_tin - 1000) / 1000.0f * 3.14159265f;
    const float co = cosf(ang);
    const float si = sinf(ang);
    const float ir = fr, ig = fg, ib = fb;
    fr = (0.299f + 0.701f * co + 0.168f * si) * ir +
         (0.587f - 0.587f * co + 0.330f * si) * ig +
         (0.114f - 0.114f * co - 0.497f * si) * ib;
    fg = (0.299f - 0.299f * co - 0.328f * si) * ir +
         (0.587f + 0.413f * co + 0.035f * si) * ig +
         (0.114f - 0.114f * co + 0.292f * si) * ib;
    fb = (0.299f - 0.300f * co + 1.250f * si) * ir +
         (0.587f - 0.588f * co - 1.050f * si) * ig +
         (0.114f + 0.886f * co - 0.203f * si) * ib;
  }
  const int ri = (int)(clamp01(fr) * 31.0f + 0.5f);
  const int gi = (int)(clamp01(fg) * 63.0f + 0.5f);
  const int bi = (int)(clamp01(fb) * 31.0f + 0.5f);
  return monitor_map_565((uint16_t)((ri << 11) | (gi << 5) | bi));
}

void write_le16(FILE *fp, uint16_t v) {
  uint8_t b[2] = {(uint8_t)(v & 0xff), (uint8_t)((v >> 8) & 0xff)};
  fwrite(b, 1, 2, fp);
}

void write_le32(FILE *fp, uint32_t v) {
  uint8_t b[4] = {(uint8_t)(v & 0xff), (uint8_t)((v >> 8) & 0xff),
                   (uint8_t)((v >> 16) & 0xff), (uint8_t)((v >> 24) & 0xff)};
  fwrite(b, 1, 4, fp);
}

}  // namespace

void video_init(void) {
  rebuild_color_tables();
  uint8_t *pixels = nullptr;
  int pitch = 0;
  if (circle_alloc_fbl(FB_LAYER_VIC, 1, &pixels, kFbWidth, kFbHeight,
                       &pitch) != 0 ||
      pixels == nullptr) {
    debug_log("video_init: circle_alloc_fbl FAILED");
    return;
  }
  s_pixels = pixels;
  s_pitch = pitch;
  canvas_state[VIC_INDEX].fb_width = kFbWidth;
  canvas_state[VIC_INDEX].fb_height = kFbHeight;
  canvas_state[VIC_INDEX].gfx_w = kGfxW;
  canvas_state[VIC_INDEX].gfx_h = kGfxH;
  canvas_state[VIC_INDEX].border_w = (kFbWidth - kGfxW) / 2;
  canvas_state[VIC_INDEX].border_h = (kFbHeight - kGfxH) / 2;
  canvas_state[VIC_INDEX].max_border_w = canvas_state[VIC_INDEX].border_w;
  canvas_state[VIC_INDEX].max_border_h = canvas_state[VIC_INDEX].border_h;
  emux_geometry_changed(FB_LAYER_VIC);
  // Fresh installs baked border value=0/max=0 at menu-build time.
  // Restore the full-border default only when no saved settings exist.
  {
    static bool s_border_defaulted = false;
    if (!s_border_defaulted) {
      s_border_defaulted = true;
      FILE *probe = fopen("/settings/settings-a2600.txt", "rb");
      if (probe == nullptr) {
        probe = fopen("/settings-a2600.txt", "rb");
      }
      const bool have_saved = (probe != nullptr);
      if (probe != nullptr) {
        fclose(probe);
      }
      bool changed = false;
      if (!have_saved) {
        if (h_border_item[VIC_INDEX] != nullptr &&
            h_border_item[VIC_INDEX]->value == 0 &&
            h_border_item[VIC_INDEX]->max > 0) {
          h_border_item[VIC_INDEX]->value = h_border_item[VIC_INDEX]->max;
          changed = true;
        }
        if (v_border_item[VIC_INDEX] != nullptr &&
            v_border_item[VIC_INDEX]->value == 0 &&
            v_border_item[VIC_INDEX]->max > 0) {
          v_border_item[VIC_INDEX]->value = v_border_item[VIC_INDEX]->max;
          changed = true;
        }
      }
      if (changed) {
        emux_frame_buffer_changed(FB_LAYER_VIC);
      }
    }
  }
  // Source rect default: full framebuffer (border=max -> zero offsets).
  // Must never be 0,0,0,0 (empty cutout) before the menu video machinery
  // runs; otherwise the scaler shows black.
  canvas_state[VIC_INDEX].left = 0;
  canvas_state[VIC_INDEX].top = 0;
  canvas_state[VIC_INDEX].vis_w = kFbWidth;
  canvas_state[VIC_INDEX].vis_h = kFbHeight;
  circle_clear_fbl(FB_LAYER_VIC);
  circle_show_fbl(FB_LAYER_VIC);
  circle_present_fbl(1u << FB_LAYER_VIC, 0);
  emux_frame_buffer_changed(FB_LAYER_VIC);
  debug_log("video_init: fbl %dx%d pitch=%d", kFbWidth, kFbHeight, pitch);
}

void video_blit(const uint32_t *pixels, int w, int h, int pitch) {
  if (s_pixels == nullptr || pixels == nullptr) {
    return;
  }
  if (!s_lut_ready) {
    rebuild_color_tables();
  }
  if (w > kFbWidth) {
    w = kFbWidth;
  }
  if (h > kFbHeight) {
    h = kFbHeight;
  }
  if (w <= 0 || h <= 0 || pitch < w) {
    return;
  }
  const int off_x = (kFbWidth - w) / 2;
  const int off_y = (kFbHeight - h) / 2;
  // Clear the letterbox first so variable-height frames never smear.
  memset(s_pixels, 0, (size_t)s_pitch * (size_t)kFbHeight);
  for (int y = 0; y < h; y++) {
    uint16_t *row = reinterpret_cast<uint16_t *>(
        s_pixels + (size_t)(y + off_y) * (size_t)s_pitch);
    const uint32_t *src = pixels + (size_t)y * (size_t)pitch;
    for (int x = 0; x < w; x++) {
      row[x + off_x] = rgb888_to_rgb565(src[x]);
    }
  }
}

void video_show_splash(void) {
  if (s_pixels == nullptr) {
    return;
  }
  static_assert(kSplashW <= kGfxW && kSplashH <= kGfxH,
                "splash must fit the gfx area");
  memset(s_pixels, 0, (size_t)s_pitch * (size_t)kFbHeight);
  const int gfx_x = (kFbWidth - kGfxW) / 2;
  const int gfx_y = (kFbHeight - kGfxH) / 2;
  const int dst_x = gfx_x + (kGfxW - kSplashW) / 2;
  const int dst_y = gfx_y + (kGfxH - kSplashH) / 2;
  for (int y = 0; y < kSplashH; y++) {
    uint16_t *row = reinterpret_cast<uint16_t *>(
        s_pixels + (size_t)(y + dst_y) * (size_t)s_pitch);
    const uint16_t *src = kSplashPixels + (size_t)y * (size_t)kSplashW;
    for (int x = 0; x < kSplashW; x++) {
      row[x + dst_x] = monitor_map_565(src[x]);
    }
  }
}

void video_render(void) {
  emux_ensure_video();
  s_frames++;
  circle_present_fbl(1u << FB_LAYER_VIC, 0);
  s_presents++;
}

void video_present_stats(unsigned long *presents, unsigned long *ensure_us,
                         unsigned long *present_us) {
  if (presents != nullptr) {
    *presents = s_presents;
  }
  if (ensure_us != nullptr) {
    *ensure_us = 0;
  }
  if (present_us != nullptr) {
    *present_us = 0;
  }
}

bool video_save_screenshot(const char *path) {
  if (path == nullptr || s_pixels == nullptr) {
    return false;
  }
  FILE *fp = fopen(path, "wb");
  if (fp == nullptr) {
    return false;
  }
  static const int kBpp = 3;
  const int row_stride = kFbWidth * kBpp;
  const int row_pad = (4 - (row_stride % 4)) % 4;
  const uint32_t pixel_bytes =
      (uint32_t)(row_stride + row_pad) * (uint32_t)kFbHeight;
  fwrite("BM", 1, 2, fp);
  write_le32(fp, 54 + pixel_bytes);
  write_le32(fp, 0);
  write_le32(fp, 54);
  write_le32(fp, 40);
  write_le32(fp, (uint32_t)kFbWidth);
  write_le32(fp, (uint32_t)kFbHeight);
  write_le16(fp, 1);
  write_le16(fp, 24);
  write_le32(fp, 0);
  write_le32(fp, pixel_bytes);
  write_le32(fp, 0);
  write_le32(fp, 0);
  write_le32(fp, 0);
  write_le32(fp, 0);
  static const uint8_t kPad[3] = {0, 0, 0};
  bool ok = true;
  for (int y = kFbHeight - 1; y >= 0; y--) {
    const uint16_t *src = reinterpret_cast<const uint16_t *>(
        s_pixels + (size_t)y * (size_t)s_pitch);
    for (int x = 0; x < kFbWidth; x++) {
      const uint16_t p = src[x];
      fputc((p & 31u) * 255u / 31u, fp);
      fputc(((p >> 5) & 63u) * 255u / 63u, fp);
      fputc(((p >> 11) & 31u) * 255u / 31u, fp);
    }
    if (row_pad != 0 &&
        fwrite(kPad, 1, (size_t)row_pad, fp) != (size_t)row_pad) {
      ok = false;
      break;
    }
  }
  fclose(fp);
  debug_log("video: screenshot %s (%s)", path, ok ? "ok" : "FAILED");
  return ok;
}

void video_set_colors(int brightness, int contrast, int gamma, int tint,
                      int saturation) {
  s_col_bri = brightness;
  s_col_con = contrast;
  s_col_gam = gamma;
  s_col_tin = tint;
  s_col_sat = saturation;
  rebuild_color_tables();
}

void video_get_colors(int *brightness, int *contrast, int *gamma, int *tint,
                      int *saturation) {
  if (brightness != nullptr) *brightness = s_col_bri;
  if (contrast != nullptr) *contrast = s_col_con;
  if (gamma != nullptr) *gamma = s_col_gam;
  if (tint != nullptr) *tint = s_col_tin;
  if (saturation != nullptr) *saturation = s_col_sat;
}

void video_set_monitor(int mode) {
  if (mode < A2600_MONITOR_COLOR || mode > A2600_MONITOR_AMBER) {
    return;
  }
  s_monitor = mode;
}

int video_get_monitor(void) { return s_monitor; }

}  // namespace a2600
}  // namespace bmx
