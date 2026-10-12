//
// apple2_video.cpp
//
// See apple2_video.h. The KEGS core renders into its Kimage (32-bit ARGB,
// 704 wide, up to 530 rows with status lines); the driver pulls changed
// rectangles via video_out_data() and converts them to the Circle RGB565
// framebuffer, applying the monitor (color/B&W/green/amber) map.
//
// Driver contract notes (verified against the SDL driver + host harness):
// - video_set_*_mask/alpha/palette/active BEFORE first frame. Without
//   video_set_palette() the text/HGR palette stays all zeros and every
//   text/HGR pixel renders black.
// - 1:1 output needs kimage x_ == a2_ (video_update_scale() preserves
//   aspect against the window size and never yields 1:1 here), so the
//   driver forces x_ = a2_ directly and re-syncs on aspect change.
// - a2 dims are stable per video mode (704x462 with the status bar off,
//   704x528 with it on); the FBL + shadow cover the max.
//

#include "apple2_video.h"
#include "apple2_debug.h"
#include "src/monitor_phosphor.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

// KEGS core globals (global-scope externs; see the header).
#include "apple2_kegs_globals.h"
extern "C" {
#include "defcomm.h"
}

extern "C" {
#include "third_party/common/circle.h"
#include "third_party/common/emux_api.h"
}

namespace bmx {
namespace apple2 {

namespace {

// Max Kimage extent (X_A2 704 wide; height + status lines + slack).
const int kShadowW = 704;
const int kShadowH = 530;
// Framebuffer: status bar off -> 704x462 active area.
const int kFbWidth = 704;
const int kFbHeight = 462;

uint8_t *s_pixels = nullptr;
int s_pitch = 0;
unsigned long s_presents = 0;

word32 s_shadow[kShadowW * kShadowH];

int s_col_bri = 1000;
int s_col_con = 1000;
int s_col_gam = 1000;
int s_col_tin = 1000;
int s_col_sat = 1000;

int s_monitor = BMX_MONITOR_COLOR;

// Fast path: RGB565 -> monitor-mapped RGB565 LUT, valid when all color
// sliders sit at their defaults (the common case). Rebuilt on monitor or
// slider change; the float path below covers adjusted sliders.
uint16_t s_fast_lut[65536];
bool s_fast_lut_valid = false;

bool sliders_default(void) {
  return s_col_bri == 1000 && s_col_con == 1000 && s_col_gam == 1000 &&
         s_col_tin == 1000 && s_col_sat == 1000;
}

void rebuild_fast_lut(void) {
  for (unsigned i = 0; i < 65536; i++) {
    s_fast_lut[i] = bmx_monitor_map_565((uint16_t)i, s_monitor);
  }
  s_fast_lut_valid = true;
}

inline uint16_t map_pixel_fast(uint32_t argb) {
  const unsigned ri = ((argb >> 16) & 0xff) >> 3;
  const unsigned gi = ((argb >> 8) & 0xff) >> 2;
  const unsigned bi = (argb & 0xff) >> 3;
  return s_fast_lut[(ri << 11) | (gi << 5) | bi];
}

inline float clamp01(float x) {
  if (x < 0.0f) {
    return 0.0f;
  }
  if (x > 1.0f) {
    return 1.0f;
  }
  return x;
}

// ARGB (ignoring source alpha; the core sets it via g_alpha_mask) ->
// RGB565 with the BMX color sliders + monitor map. Same math as the CPC
// pen LUT so every kernel's sliders behave identically.
uint16_t map_pixel(uint32_t argb) {
  float fr = (float)((argb >> 16) & 0xff) / 255.0f;
  float fg = (float)((argb >> 8) & 0xff) / 255.0f;
  float fb = (float)(argb & 0xff) / 255.0f;
  const float c = (float)s_col_con / 1000.0f;
  const float b = (float)(s_col_bri - 1000) / 1000.0f;
  fr = clamp01(fr * c + b);
  fg = clamp01(fg * c + b);
  fb = clamp01(fb * c + b);
  float g = (float)s_col_gam / 1000.0f;
  if (g < 0.1f) {
    g = 0.1f;
  }
  if (g > 4.0f) {
    g = 4.0f;
  }
  auto gamma = [g](float x) -> float {
    return (x <= 0.0f) ? 0.0f : exp2f(g * log2f(x));
  };
  fr = gamma(fr);
  fg = gamma(fg);
  fb = gamma(fb);
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
  const uint16_t rgb565 = (uint16_t)((ri << 11) | (gi << 5) | bi);
  return bmx_monitor_map_565(rgb565, s_monitor);
}

// Pull one change rect through the shadow buffer into the FBL.
void blit_rect(const Change_rect &rect) {
  if (s_pixels == nullptr) {
    return;
  }
  int x = rect.x < 0 ? 0 : rect.x;
  int y = rect.y < 0 ? 0 : rect.y;
  int w = rect.width;
  int h = rect.height;
  if (x + w > kFbWidth) {
    w = kFbWidth - x;
  }
  if (y + h > kFbHeight) {
    h = kFbHeight - y;
  }
  if (w <= 0 || h <= 0) {
    return;
  }
  for (int r = 0; r < h; r++) {
    const word32 *src = &s_shadow[(size_t)(y + r) * kShadowW + x];
    uint16_t *dst = reinterpret_cast<uint16_t *>(
        s_pixels + (size_t)(y + r) * (size_t)s_pitch) +
                    x;
    if (s_fast_lut_valid) {
      for (int c = 0; c < w; c++) {
        dst[c] = map_pixel_fast(src[c]);
      }
    } else {
      for (int c = 0; c < w; c++) {
        dst[c] = map_pixel(src[c]);
      }
    }
  }
}

// Force 1:1 (x_ = a2_) so video_out_data takes the direct copy path.
void sync_scale(void) {
  if (g_mainwin_kimage.x_width != g_mainwin_kimage.a2_width ||
      g_mainwin_kimage.x_height != g_mainwin_kimage.a2_height) {
    g_mainwin_kimage.x_width = g_mainwin_kimage.a2_width;
    g_mainwin_kimage.x_height = g_mainwin_kimage.a2_height;
    video_set_x_refresh_needed(&g_mainwin_kimage, 1);
    debug_log("video: aspect sync a2=%dx%d", g_mainwin_kimage.a2_width,
              g_mainwin_kimage.a2_height);
  }
}

}  // namespace

void video_init(void) {
  uint8_t *pixels = nullptr;
  int pitch = 0;
  if (circle_alloc_fbl(FB_LAYER_VIC, 1, &pixels, kFbWidth, kFbHeight, &pitch) != 0 ||
      pixels == nullptr) {
    debug_log("video_init: circle_alloc_fbl FAILED");
    return;
  }
  s_pixels = pixels;
  s_pitch = pitch;
  debug_log("video: fbl=%p pitch=%d canvas=%dx%d gfx=%dx%d border=%dx%d",
            (void *)pixels, pitch, kFbWidth, kFbHeight,
            canvas_state[VIC_INDEX].gfx_w, canvas_state[VIC_INDEX].gfx_h,
            canvas_state[VIC_INDEX].border_w,
            canvas_state[VIC_INDEX].border_h);
  emux_geometry_changed(FB_LAYER_VIC);
  canvas_state[VIC_INDEX].fb_width = kFbWidth;
  canvas_state[VIC_INDEX].fb_height = kFbHeight;
  canvas_state[VIC_INDEX].gfx_w = 640;
  canvas_state[VIC_INDEX].gfx_h = 400;
  canvas_state[VIC_INDEX].border_w = (kFbWidth - 640) / 2;
  canvas_state[VIC_INDEX].border_h = (kFbHeight - 400) / 2;
  canvas_state[VIC_INDEX].max_border_w = canvas_state[VIC_INDEX].border_w;
  canvas_state[VIC_INDEX].max_border_h = canvas_state[VIC_INDEX].border_h;
  circle_clear_fbl(FB_LAYER_VIC);
  circle_show_fbl(FB_LAYER_VIC);
  circle_present_fbl(1u << FB_LAYER_VIC, 0);

  // Driver video init (mirrors the SDL driver): pixel layout, opaque
  // alpha, palettes, active. The palette call is load-bearing: without
  // it the text/HGR palette stays all zeros and text renders black.
  video_set_red_mask(0xff0000);
  video_set_green_mask(0x00ff00);
  video_set_blue_mask(0x0000ff);
  video_set_alpha_mask(0xff000000);
  video_set_palette();
  debug_log("video: masks+palette set pal0=%08x pal15=%08x",
            (unsigned)g_a2palette_1624[0], (unsigned)g_a2palette_1624[15]);
  video_set_active(&g_mainwin_kimage, 1);
  rebuild_fast_lut();
  sync_scale();
  debug_log("video_init done: kimage a2=%dx%d x=%dx%d active=%d",
            g_mainwin_kimage.a2_width, g_mainwin_kimage.a2_height,
            g_mainwin_kimage.x_width, g_mainwin_kimage.x_height,
            video_get_active(&g_mainwin_kimage));
}

void video_render(void) {
  if (!video_get_active(&g_mainwin_kimage)) {
    debug_log("video_render: kimage inactive, skipping");
    return;
  }
  if (video_change_aspect_needed(&g_mainwin_kimage, kFbWidth, kFbHeight)) {
    debug_log("video_render: aspect change, resync");
    sync_scale();
  }
  if (!s_fast_lut_valid && sliders_default()) {
    rebuild_fast_lut();
  }
  Change_rect rect;
  int nrects = 0;
  Change_rect first = {0, 0, 0, 0};
  for (int i = 0; i < MAX_CHANGE_RECTS; i++) {
    if (!video_out_data(s_shadow, &g_mainwin_kimage, kShadowW, &rect, i)) {
      break;
    }
    if (nrects == 0) {
      first = rect;
    }
    nrects++;
    blit_rect(rect);
  }
  emux_ensure_video();
  circle_present_fbl(1u << FB_LAYER_VIC, 0);
  s_presents++;
  // Pixel proof for the first frames: nonzero checksum means the core
  // composited real pixels into the framebuffer.
  if (s_presents <= 3) {
    unsigned long long sum = 0;
    unsigned long nz = 0;
    const uint16_t *px = reinterpret_cast<const uint16_t *>(s_pixels);
    const int stride = s_pitch / 2;
    for (int y = 0; y < kFbHeight; y += 4) {
      for (int x = 0; x < kFbWidth; x += 4) {
        uint16_t v = px[(size_t)y * (size_t)stride + (size_t)x];
        sum += v;
        if (v != 0) {
          nz++;
        }
      }
    }
    debug_log("vidproof fbl#%lu rects=%d first=(%d,%d %dx%d) "
              "a2=%dx%d sum=%llu nzsamples=%lu",
              s_presents, nrects, first.x, first.y, first.width,
              first.height, g_mainwin_kimage.a2_width,
              g_mainwin_kimage.a2_height, sum, nz);
  }
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

void video_frame_stats(unsigned *mode, unsigned *lines) {
  if (mode != nullptr) {
    *mode = (unsigned)(g_cur_a2_stat & 0xffffu);
  }
  if (lines != nullptr) {
      *lines = (unsigned)g_mainwin_kimage.a2_height;
  }
}

void video_src_stats(int *left, int *top, int *w, int *h) {
  if (left != nullptr) {
    *left = canvas_state[VIC_INDEX].left;
  }
  if (top != nullptr) {
    *top = canvas_state[VIC_INDEX].top;
  }
  if (w != nullptr) {
    *w = canvas_state[VIC_INDEX].vis_w;
  }
  if (h != nullptr) {
    *h = canvas_state[VIC_INDEX].vis_h;
  }
}

void video_set_colors(int brightness, int contrast, int gamma, int tint,
                      int saturation) {
  s_col_bri = brightness;
  s_col_con = contrast;
  s_col_gam = gamma;
  s_col_tin = tint;
  s_col_sat = saturation;
  s_fast_lut_valid = false;
}

void video_get_colors(int *brightness, int *contrast, int *gamma, int *tint,
                      int *saturation) {
  if (brightness != nullptr) {
    *brightness = s_col_bri;
  }
  if (contrast != nullptr) {
    *contrast = s_col_con;
  }
  if (gamma != nullptr) {
    *gamma = s_col_gam;
  }
  if (tint != nullptr) {
    *tint = s_col_tin;
  }
  if (saturation != nullptr) {
    *saturation = s_col_sat;
  }
}

void video_set_monitor(int mode) {
  if (!bmx_monitor_valid(mode)) {
    return;
  }
  s_monitor = mode;
  s_fast_lut_valid = false;
}

int video_get_monitor(void) { return s_monitor; }

namespace {

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

bool video_save_screenshot(const char *path) {
  if (path == nullptr || s_pixels == nullptr) {
    return false;
  }
  FILE *fp = fopen(path, "wb");
  if (fp == nullptr) {
    return false;
  }
  // Uncompressed 24-bit BMP straight from the RGB565 framebuffer.
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
      const uint8_t b = (uint8_t)((p & 31u) * 255u / 31u);
      const uint8_t g = (uint8_t)(((p >> 5) & 63u) * 255u / 63u);
      const uint8_t r = (uint8_t)(((p >> 11) & 31u) * 255u / 31u);
      fputc(b, fp);
      fputc(g, fp);
      fputc(r, fp);
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

}  // namespace apple2
}  // namespace bmx
