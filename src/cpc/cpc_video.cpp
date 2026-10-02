//
// cpc_video.cpp
//
// See cpc_video.h. Framebuffer is 1024x312 (64 CRTC chars x 312 lines at
// 16 Mode-2 pixels per char). The C hooks below are driven by cpc_core.c;
// address formation follows the CPC VMA table (A15-14 = MA13-12,
// A13-11 = RA2-0, A10-1 = MA9-0, A0 = CCLK) with the MA-advances-by-R1
// model (firmware-manual 80-byte row pitch, Caprice32/WinAPE behaviour).
// Pixel decodes are the standard interleave:
//   mode 2: bit(7-i) -> pixel i (pens 0/1)
//   mode 1: pen bit1 <- {b7,b6,b5,b4}, bit0 <- {b3,b2,b1,b0} per pixel
//   mode 0: pen bits <- {b7,b5,b3,b1} / {b6,b4,b2,b0}
//   mode 3: treated as mode 1.
//

#include "cpc_video.h"
#include "cpc_video_iface.h"
#include "cpc_debug.h"
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

// Provided by cpc_hw.c (see cpc_video_* getters there).
extern "C" unsigned cpc_video_ma(void);
extern "C" unsigned cpc_video_raster(void);
extern "C" unsigned cpc_video_row(void);
extern "C" unsigned cpc_video_mode(void);
extern "C" unsigned cpc_video_r1(void);
extern "C" unsigned cpc_video_r6(void);
extern "C" const uint8_t *cpc_video_pens(void);
extern "C" const uint8_t *cpc_video_ram(void);

namespace bmx {
namespace cpc {

namespace {

const int kFbWidth = 1024;
const int kFbHeight = 312;
const int kCharsPerLine = 64;
const int kPxPerChar = 16;

// Hardware colour (0..31) -> 8-bit RGB, from the CPC palette reference
// (50% maps to 128). Index = INKR value.
const uint8_t kHwRgb[32][3] = {
    {128, 128, 128}, {128, 128, 128}, {0, 255, 128}, {255, 255, 128},
    {0, 0, 128},     {255, 0, 128},   {0, 128, 128}, {255, 128, 128},
    {255, 0, 128},   {255, 255, 128}, {255, 255, 0}, {255, 255, 255},
    {255, 0, 0},     {255, 0, 255},   {255, 128, 0}, {255, 128, 255},
    {0, 0, 128},     {0, 255, 128},   {0, 255, 0},   {0, 255, 255},
    {0, 0, 0},       {0, 0, 255},     {0, 128, 0},   {0, 128, 255},
    {128, 0, 128},   {128, 255, 128}, {128, 255, 0}, {128, 255, 255},
    {128, 0, 0},     {128, 0, 255},   {128, 128, 0}, {128, 128, 255},
};

uint8_t *s_pixels = nullptr;
int s_pitch = 0;
unsigned long s_presents = 0;
unsigned s_fb_line = 0;
unsigned s_last_frame_lines = 0;
// Vertical centering offset (lines): the shared scaler assumes the gfx
// window sits centered in the framebuffer (border on all sides), so the
// renderer must place it there too. Latched per frame from R6.
unsigned s_y_off = 56;

int s_col_bri = 1000;
int s_col_con = 1000;
int s_col_gam = 1000;
int s_col_tin = 1000;
int s_col_sat = 1000;

int s_monitor = BMX_MONITOR_COLOR;

// Pen (0..16, 16 = border) -> RGB565, rebuilt from INKR state + sliders.
uint16_t s_pen_lut[17];
bool s_lut_dirty = true;

inline float clamp01(float x) {
  if (x < 0.0f) {
    return 0.0f;
  }
  if (x > 1.0f) {
    return 1.0f;
  }
  return x;
}

uint16_t rgb_to_565(float fr, float fg, float fb) {
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
  return (uint16_t)((ri << 11) | (gi << 5) | bi);
}

void rebuild_lut(void) {
  const float c = (float)s_col_con / 1000.0f;
  const float b = (float)(s_col_bri - 1000) / 1000.0f;
  float g = (float)s_col_gam / 1000.0f;
  if (g < 0.1f) {
    g = 0.1f;
  }
  if (g > 4.0f) {
    g = 4.0f;
  }
  const uint8_t *pens = cpc_video_pens();
  if (pens == nullptr) {
    return;
  }
  for (int p = 0; p < 17; p++) {
    const uint8_t hw = pens[p] & 31U;
    float fr = (float)kHwRgb[hw][0] / 255.0f;
    float fg = (float)kHwRgb[hw][1] / 255.0f;
    float fb = (float)kHwRgb[hw][2] / 255.0f;
    fr = clamp01(fr * c + b);
    fg = clamp01(fg * c + b);
    fb = clamp01(fb * c + b);
    auto gamma = [g](float x) -> float {
      return (x <= 0.0f) ? 0.0f : exp2f(g * log2f(x));
    };
    s_pen_lut[p] =
        bmx_monitor_map_565(rgb_to_565(gamma(fr), gamma(fg), gamma(fb)),
                            s_monitor);
  }
  s_lut_dirty = false;
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
  emux_geometry_changed(FB_LAYER_VIC);
  canvas_state[VIC_INDEX].fb_width = kFbWidth;
  canvas_state[VIC_INDEX].fb_height = kFbHeight;
  canvas_state[VIC_INDEX].gfx_w = 640;
  canvas_state[VIC_INDEX].gfx_h = 200;
  canvas_state[VIC_INDEX].border_w = (kFbWidth - 640) / 2;
  canvas_state[VIC_INDEX].border_h = (kFbHeight - 200) / 2;
  canvas_state[VIC_INDEX].max_border_w = canvas_state[VIC_INDEX].border_w;
  canvas_state[VIC_INDEX].max_border_h = canvas_state[VIC_INDEX].border_h;
  circle_clear_fbl(FB_LAYER_VIC);
  circle_show_fbl(FB_LAYER_VIC);
  circle_present_fbl(1u << FB_LAYER_VIC, 0);
  s_lut_dirty = true;
  debug_log("video_init: fbl %dx%d pitch=%d", kFbWidth, kFbHeight, pitch);
}

void video_render(void) {
  emux_ensure_video();
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

void video_frame_stats(unsigned *mode, unsigned *lines) {
  if (mode != nullptr) {
    *mode = cpc_video_mode() & 3U;
  }
  if (lines != nullptr) {
    *lines = s_last_frame_lines;
  }
}

void video_crtc_stats(unsigned *r1, unsigned *r6, unsigned *ma,
                      unsigned *border) {
  if (r1 != nullptr) {
    *r1 = cpc_video_r1();
  }
  if (r6 != nullptr) {
    *r6 = cpc_video_r6();
  }
  if (ma != nullptr) {
    *ma = cpc_video_ma();
  }
  if (border != nullptr) {
    const uint8_t *pens = cpc_video_pens();
    *border = pens != nullptr ? (pens[16] & 31U) : 99U;
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
  s_lut_dirty = true;
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
  s_lut_dirty = true;
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

}  // namespace cpc
}  // namespace bmx

extern "C" {

void cpc_video_palette_dirty(void) { bmx::cpc::s_lut_dirty = true; }

void cpc_video_frame_start(void) {
  bmx::cpc::s_last_frame_lines = bmx::cpc::s_fb_line;
  bmx::cpc::s_fb_line = 0;
  // Latch vertical centering from the programmed R6 (standard 25 -> 56).
  // Demos reprogram R6 for overscan; one frame of lag on change is
  // invisible. Rows above the window are never drawn below, so paint
  // them with the border pen here.
  if (bmx::cpc::s_lut_dirty) {
    bmx::cpc::rebuild_lut();
  }
  unsigned r6 = cpc_video_r6();
  if (r6 > 39U) {
    r6 = 39U;
  }
  bmx::cpc::s_y_off = (312U - r6 * 8U) / 2U;
  if (bmx::cpc::s_pixels != nullptr && bmx::cpc::s_y_off != 0) {
    uint16_t *dst = reinterpret_cast<uint16_t *>(bmx::cpc::s_pixels);
    const size_t n =
        (size_t)bmx::cpc::s_y_off * (size_t)bmx::cpc::s_pitch / 2U;
    const uint16_t b = bmx::cpc::s_pen_lut[16];
    for (size_t i = 0; i < n; i++) {
      dst[i] = b;
    }
  }
}

void cpc_video_scanline(void) {
  using namespace bmx::cpc;
  if (s_pixels == nullptr) {
    return;
  }
  if (s_lut_dirty) {
    rebuild_lut();
  }
  if (s_fb_line >= (unsigned)kFbHeight) {
    return;
  }
  // Centered destination: source line s draws at s + y_off (clipped).
  const unsigned dest_line = s_fb_line + s_y_off;
  s_fb_line++;
  if (dest_line >= (unsigned)kFbHeight) {
    return;
  }
  uint16_t *dst =
      reinterpret_cast<uint16_t *>(s_pixels + (size_t)dest_line * (size_t)s_pitch);
  const unsigned ma = cpc_video_ma();
  const unsigned raster = cpc_video_raster() & 7U;
  const unsigned row = cpc_video_row();
  const unsigned mode = cpc_video_mode() & 3U;
  unsigned r1 = cpc_video_r1();
  if (r1 > (unsigned)kCharsPerLine) {
    r1 = (unsigned)kCharsPerLine;
  }
  const unsigned r6 = cpc_video_r6();
  const uint8_t *ram = cpc_video_ram();
  const unsigned page = (ma >> 12) & 3U;
  const uint32_t page_base = page << 14;
  const uint32_t raster_base = (raster & 7U) << 11;
  const int displayed_row = (int)row < (int)r6 ? 1 : 0;
  // Horizontal centering: the scaler assumes the r1-char window sits in
  // the middle of the 64-char line (standard 40 -> 12-char margin each
  // side, matching the declared 192px border). Per-line, so overscan
  // and wobble effects stay centered too.
  const unsigned x0c = ((unsigned)kCharsPerLine - r1) / 2U;

  for (unsigned c = 0; c < (unsigned)kCharsPerLine; c++) {
    uint16_t *px = dst + (size_t)c * (unsigned)kPxPerChar;
    if (!displayed_row || c < x0c || c >= x0c + r1) {
      const uint16_t b = s_pen_lut[16];
      for (int i = 0; i < kPxPerChar; i++) {
        px[i] = b;
      }
      continue;
    }
    const unsigned word = (ma + (c - x0c)) & 0x3FFFU;
    const uint32_t addr =
        page_base | raster_base | (((word & 0x3FFU) << 1) & 0x7FFU);
    const uint8_t b0 = ram[addr & 0xFFFFU];
    const uint8_t b1 = ram[(addr + 1U) & 0xFFFFU];
    if (mode == 2) {
      for (int i = 0; i < 8; i++) {
        const unsigned pen = (b0 >> (7 - i)) & 1U;
        px[i] = s_pen_lut[pen];
      }
      for (int i = 0; i < 8; i++) {
        const unsigned pen = (b1 >> (7 - i)) & 1U;
        px[8 + i] = s_pen_lut[pen];
      }
    } else if (mode == 0) {
      unsigned pens[4];
      pens[0] = ((b0 >> 7) & 1U) << 3 | ((b0 >> 5) & 1U) << 2 |
                ((b0 >> 3) & 1U) << 1 | ((b0 >> 1) & 1U);
      pens[1] = ((b0 >> 6) & 1U) << 3 | ((b0 >> 4) & 1U) << 2 |
                ((b0 >> 2) & 1U) << 1 | (b0 & 1U);
      pens[2] = ((b1 >> 7) & 1U) << 3 | ((b1 >> 5) & 1U) << 2 |
                ((b1 >> 3) & 1U) << 1 | ((b1 >> 1) & 1U);
      pens[3] = ((b1 >> 6) & 1U) << 3 | ((b1 >> 4) & 1U) << 2 |
                ((b1 >> 2) & 1U) << 1 | (b1 & 1U);
      for (int p = 0; p < 4; p++) {
        const uint16_t col = s_pen_lut[pens[p] & 15U];
        px[p * 4] = col;
        px[p * 4 + 1] = col;
        px[p * 4 + 2] = col;
        px[p * 4 + 3] = col;
      }
    } else {
      // Mode 1 (and the unofficial mode 3): 4 pixels per byte.
      const uint8_t bytes[2] = {b0, b1};
      for (int k = 0; k < 2; k++) {
        const uint8_t b = bytes[k];
        const unsigned p0 = ((b >> 7) & 1U) << 1 | ((b >> 3) & 1U);
        const unsigned p1 = ((b >> 6) & 1U) << 1 | ((b >> 2) & 1U);
        const unsigned p2 = ((b >> 5) & 1U) << 1 | ((b >> 1) & 1U);
        const unsigned p3 = ((b >> 4) & 1U) << 1 | (b & 1U);
        const unsigned ps[4] = {p0, p1, p2, p3};
        for (int p = 0; p < 4; p++) {
          const uint16_t col = s_pen_lut[ps[p] & 3U];
          px[k * 8 + p * 2] = col;
          px[k * 8 + p * 2 + 1] = col;
        }
      }
    }
  }
}

}  // extern "C"
