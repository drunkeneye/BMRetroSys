//
// zx_video.cpp
//
// See zx_video.h. Framebuffer is 352x304 (256x192 + full border), the same
// geometry the core renders; BMX scaling/stretch handles the output size.
//

#include "zx_video.h"
#include "zx_debug.h"

#include "src/monitor_phosphor.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

#include <circle/timer.h>

extern "C" {
#include "third_party/common/circle.h"
#include "third_party/common/emux_api.h"
}

extern "C" {
#include "zx_zesarux.h"
}

namespace bmx {
namespace zxspectrum {

namespace {

const int kFbWidth = 352;
const int kFbHeight = 304;

uint8_t *s_pixels = nullptr;
int s_pitch = 0;
unsigned long s_presents = 0;
unsigned long s_frames = 0;

int s_col_bri = 1000;
int s_col_con = 1000;
int s_col_gam = 1000;
int s_col_tin = 1000;
int s_col_sat = 1000;

int s_monitor = BMX_MONITOR_COLOR;

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

inline uint16_t rgb888_to_rgb565(uint32_t rgb) {
  const int r = (rgb >> 16) & 0xff;
  const int g = (rgb >> 8) & 0xff;
  const int b = rgb & 0xff;
  if (colors_neutral()) {
    return bmx_monitor_map_565(
        (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)), s_monitor);
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
  // Tint: reuse the gray-axis hue rotation from the DOS kernel path.
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
  return bmx_monitor_map_565((uint16_t)((ri << 11) | (gi << 5) | bi),
                             s_monitor);
}

// Circle refresh hook: ZEsarUX calls (*scr_refresca_pantalla)() at the end
// of every emulated frame. Convert the indexed rainbow buffer to RGB565.
void blit_rainbow(void) {
  if (s_pixels == nullptr || rainbow_buffer == nullptr ||
      spectrum_colortable == nullptr) {
    return;
  }
  const int w = get_total_ancho_rainbow_cached;
  const int h = get_total_alto_rainbow_cached;
  if (w <= 0 || h <= 0 || w > kFbWidth || h > kFbHeight) {
    return;
  }
  const int off_x = (kFbWidth - w) / 2;
  const int off_y = (kFbHeight - h) / 2;
  for (int y = 0; y < h; y++) {
    uint16_t *row =
        reinterpret_cast<uint16_t *>(s_pixels + (size_t)(y + off_y) * (size_t)s_pitch);
    const z80_int *src = &rainbow_buffer[(size_t)y * (size_t)w];
    for (int x = 0; x < w; x++) {
      row[x + off_x] = rgb888_to_rgb565((uint32_t)spectrum_colortable[src[x] & 0xff]);
    }
  }
}

}  // namespace

void video_blit_rainbow(void) { blit_rainbow(); }

extern "C" void zx_circle_refresh_hook(void) {
  bmx::zxspectrum::video_blit_rainbow();
}

// Installed in video_init() below; defined in zx_input.cpp.
extern "C" void zx_keyboard_refresh_hook(void);

void video_init(void) {
  rebuild_color_tables();
  uint8_t *pixels = nullptr;
  int pitch = 0;
  // layer FB_LAYER_VIC, pixelmode 1 = RGB565 direct (atari800 pattern).
  if (circle_alloc_fbl(FB_LAYER_VIC, 1, &pixels, kFbWidth, kFbHeight, &pitch) != 0 ||
      pixels == nullptr) {
    debug_log("video_init: circle_alloc_fbl FAILED");
    return;
  }
  s_pixels = pixels;
  s_pitch = pitch;
  emux_geometry_changed(FB_LAYER_VIC);
  canvas_state[VIC_INDEX].fb_width = kFbWidth;
  canvas_state[VIC_INDEX].fb_height = kFbHeight;
  // GFX area: 256x192 playfield; the rest is border.
  canvas_state[VIC_INDEX].gfx_w = 256;
  canvas_state[VIC_INDEX].gfx_h = 192;
  canvas_state[VIC_INDEX].border_w = (kFbWidth - 256) / 2;
  canvas_state[VIC_INDEX].border_h = (kFbHeight - 192) / 2;
  canvas_state[VIC_INDEX].max_border_w = canvas_state[VIC_INDEX].border_w;
  canvas_state[VIC_INDEX].max_border_h = canvas_state[VIC_INDEX].border_h;
  circle_clear_fbl(FB_LAYER_VIC);
  circle_show_fbl(FB_LAYER_VIC);
  circle_present_fbl(1u << FB_LAYER_VIC, 0);
  // Take over the ZEsarUX refresh entry point.
  scr_refresca_pantalla = zx_circle_refresh_hook;
  // Take over the 50 Hz keyboard-refresh entry point. This must not stay
  // NULL: the core calls it on every tick (a null call crashes before the
  // first frame completes).
  scr_actualiza_tablas_teclado = zx_keyboard_refresh_hook;
  debug_log("video_init: fbl %dx%d pitch=%d rainbow=%dx%d", kFbWidth,
            kFbHeight, pitch, get_total_ancho_rainbow_cached,
            get_total_alto_rainbow_cached);
}

void video_render(void) {
  emux_ensure_video();
  s_frames++;
  circle_present_fbl(1u << FB_LAYER_VIC, 0);
  s_presents++;
}

void video_present_stats(unsigned long *presents, unsigned long *ensure_us,
                         unsigned long *present_us) {
  if (presents != nullptr) *presents = s_presents;
  if (ensure_us != nullptr) *ensure_us = 0;
  if (present_us != nullptr) *present_us = 0;
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
  if (!bmx_monitor_valid(mode)) {
    return;
  }
  s_monitor = mode;
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
  // Rows bottom-up BGR, padded to 4 bytes.
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

}  // namespace zxspectrum
}  // namespace bmx
