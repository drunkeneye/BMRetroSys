//
// msx_video.cpp
//
// See msx_video.h. TMS9918A scanline renderer. Table math mirrors the
// vendored fullscreen renderer (video_chips/vdp_9918a.c) exactly:
//   mode = ((R1>>2)&6) | ((R0>>1)&1): 0 = Graphics I, 4 = Text,
//          2 = Multicolor, 1 = Screen 2 bitmap layout.
//   name table   = (R2&15)*0x400
//   pattern base = (R4&7)*0x800 (bitmap thirds add 2048*tercio, masked
//                  to bank 0/8192 upstream; replicated here)
//   color base   = R3*0x40 (Graphics I uses +char/8)
//   multicolor pattern base masked to 1K, address = base + char*8 +
//   (y&3)*2, high nibble first square then low
//   sprites: attribute = R5*0x80&~127, pattern = R6*0x800, Y+1 with 208
//   terminator, EC shifts X by -32, transparent color 0, drawn last =
//   highest priority. No 4-per-line limit or 5S/C flags in phase 1.
// Palette is the upstream vdp9918_colortable_original.
//

#include "msx_video.h"
#include "msx_video_iface.h"
#include "msx_debug.h"
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
#include "third_party/common/ui.h"
extern struct menu_item *h_border_item[];
extern struct menu_item *v_border_item[];
}

// Owned by machines/msx.c (VRAM) and video_chips/vdp_9918a.c (registers).
extern "C" {
extern unsigned char *msx_vram_memory;
extern unsigned char vdp_9918a_registers[];
}

namespace bmx {
namespace msx {

namespace {

const int kFbWidth = 352;
const int kFbHeight = 304;
const int kBorderL = 48;
const int kBorderT = 56;
const int kGfxW = 256;
const int kGfxH = 192;
// Core frame lines (229 T-states each) vs framebuffer lines.
const int kCoreLines = 312;
// Source lines are centered into the framebuffer.
const int kLineOff = (kCoreLines - kFbHeight) / 2;

// TMS9918A palette (upstream vdp9918_colortable_original), 8-bit RGB.
const uint8_t kVdpRgb[16][3] = {
    {0x00, 0x00, 0x00}, {0x01, 0x01, 0x01}, {0x3e, 0xb8, 0x49},
    {0x74, 0xd0, 0x7d}, {0x59, 0x55, 0xe0}, {0x80, 0x76, 0xf1},
    {0xb9, 0x5e, 0x51}, {0x65, 0xdb, 0xef}, {0xdb, 0x65, 0x59},
    {0xff, 0x89, 0x7d}, {0xcc, 0xc3, 0x5e}, {0xde, 0xd0, 0x87},
    {0x3a, 0xa2, 0x41}, {0xb7, 0x66, 0xb5}, {0xcc, 0xcc, 0xcc},
    {0xff, 0xff, 0xff},
};

uint8_t *s_pixels = nullptr;
int s_pitch = 0;
unsigned long s_presents = 0;
unsigned s_src_line = 0;
unsigned s_last_frame_lines = 0;
unsigned s_last_mode = 0;

int s_col_bri = 1000;
int s_col_con = 1000;
int s_col_gam = 1000;
int s_col_tin = 1000;
int s_col_sat = 1000;

// Active CRT monitor type (shared phosphor map; Color is identity).
int s_monitor = BMX_MONITOR_COLOR;

// Pen (0..15) -> RGB565, rebuilt from sliders.
uint16_t s_pen_lut[16];
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
  for (int p = 0; p < 16; p++) {
    float fr = (float)kVdpRgb[p][0] / 255.0f;
    float fg = (float)kVdpRgb[p][1] / 255.0f;
    float fb = (float)kVdpRgb[p][2] / 255.0f;
    fr = clamp01(fr * c + b);
    fg = clamp01(fg * c + b);
    fb = clamp01(fb * c + b);
    auto gamma = [g](float x) -> float {
      return (x <= 0.0f) ? 0.0f : exp2f(g * log2f(x));
    };
    s_pen_lut[p] = bmx_monitor_map_565(rgb_to_565(gamma(fr), gamma(fg), gamma(fb)),
                                           s_monitor);
  }
  s_lut_dirty = false;
}

inline uint8_t vram_byte(unsigned addr) {
  return msx_vram_memory[addr & 16383];
}

inline unsigned vdp_mode(void) {
  return (unsigned)(((vdp_9918a_registers[1] >> 2) & 6) |
                    ((vdp_9918a_registers[0] >> 1) & 1));
}

// Draw one VDP line (0..191) into the framebuffer row, x centered.
void draw_display_line(uint16_t *dst, unsigned vline) {
  const unsigned mode = vdp_mode();
  const unsigned name_base = (vdp_9918a_registers[2] & 15u) * 0x400u;
  const unsigned pat_base = (vdp_9918a_registers[4] & 7u) * 0x800u;
  const unsigned col_base = (unsigned)vdp_9918a_registers[3] * 0x40u;
  const unsigned row = vline / 8u;
  const unsigned crow = vline % 8u;
  if (mode == 4) {
    // Text 40x24, 6px chars centered in the 256 window.
    const unsigned ink = (vdp_9918a_registers[7] >> 4) & 15u;
    const unsigned paper = vdp_9918a_registers[7] & 15u;
    uint16_t *px = dst + (kBorderL + 8);
    for (unsigned x = 0; x < 40u; x++) {
      const unsigned ch = vram_byte(name_base + row * 40u + x);
      uint8_t bits = vram_byte(pat_base + ch * 8u + crow);
      for (int i = 0; i < 6; i++) {
        *px++ = s_pen_lut[(bits & 0x80u) ? ink : paper];
        bits <<= 1;
      }
    }
    return;
  }
  if (mode == 2) {
    // Multicolor 64x48: each 8x8 name cell holds 2x2 squares of 4x4 px
    // (mirrors video_chips/vdp_9918a.c case 2: incremento_byte=(y&3)*2,
    // then 2 bytes = 2 square-rows, high nibble first). Per scanline only
    // one square-row applies: cell lines 0-3 read the first pattern byte,
    // lines 4-7 the second. 32 cells x 8 px = 256 px, no overflow.
    const unsigned pb = pat_base & (65536u - 1024u);
    const unsigned half = (crow >= 4u) ? 1u : 0u;
    uint16_t *px = dst + kBorderL;
    for (unsigned x = 0; x < 32u; x++) {
      const unsigned ch = vram_byte(name_base + row * 32u + x);
      const unsigned addr = pb + ch * 8u + (row & 3u) * 2u + half;
      uint8_t b = vram_byte(addr);
      for (int sq = 0; sq < 2; sq++) {
        const unsigned col = (b >> 4) & 15u;
        b <<= 4;
        const uint16_t c = s_pen_lut[col];
        for (int i = 0; i < 4; i++) {
          *px++ = c;
        }
      }
    }
    return;
  }
  // Graphics I (mode 0) and Screen 2 bitmap (mode 1): 32x8 chars.
  const int bitmap = (mode == 1) ? 1 : 0;
  const unsigned tercio = row / 8u;
  uint16_t *px = dst + kBorderL;
  for (unsigned x = 0; x < 32u; x++) {
    const unsigned ch = vram_byte(name_base + row * 32u + x);
    unsigned paddr;
    unsigned caddr;
    if (bitmap) {
      paddr = ((pat_base & 8192u) + ch * 8u + 2048u * tercio) & 16383u;
      caddr = ((col_base & 8192u) + ch * 8u + 2048u * tercio) & 16383u;
    } else {
      paddr = (pat_base + ch * 8u) & 16383u;
      caddr = (col_base + ch / 8u) & 16383u;
    }
    const uint8_t bits0 = vram_byte(paddr + crow);
    uint8_t bits = bits0;
    unsigned ink;
    unsigned paper;
    if (bitmap) {
      const uint8_t cb = vram_byte(caddr + crow);
      ink = (cb >> 4) & 15u;
      paper = cb & 15u;
    } else {
      const uint8_t cb = vram_byte(caddr);
      ink = (cb >> 4) & 15u;
      paper = cb & 15u;
    }
    const uint16_t ci = s_pen_lut[ink];
    const uint16_t cp = s_pen_lut[paper];
    for (int i = 0; i < 8; i++) {
      *px++ = (bits & 0x80u) ? ci : cp;
      bits <<= 1;
    }
  }
}

// Draw sprites covering VDP line vline (back-to-front handled by caller
// order: this draws one sprite's row if visible).
void draw_sprites_line(uint16_t *dst, unsigned vline) {
  const unsigned mode = vdp_mode();
  if (mode == 4) {
    return;  // no sprites in text mode
  }
  const int size = (vdp_9918a_registers[1] & 2) ? 16 : 8;
  const int dbl = (vdp_9918a_registers[1] & 1) ? 2 : 1;
  const unsigned pat_table = (unsigned)vdp_9918a_registers[6] * 0x800u;
  unsigned attr_table = (unsigned)vdp_9918a_registers[5] * 0x80u;
  attr_table &= (65535u - 128u);
  // Collect back-to-front: find 208 terminator first (upstream order).
  int last = 31;
  for (int i = 0; i < 32; i++) {
    if (vram_byte(attr_table + (unsigned)i * 4u) == 208) {
      last = i - 1;
      break;
    }
  }
  for (int i = last; i >= 0; i--) {
    const unsigned a = attr_table + (unsigned)i * 4u;
    int y = (int)vram_byte(a);
    int x = (int)vram_byte(a + 1u);
    const unsigned name = vram_byte(a + 2u);
    const unsigned attr = vram_byte(a + 3u);
    y++;  // 255 -> line 0
    if (y >= 256 - 32) {
      y -= 256;
    }
    if (attr & 128u) {
      x -= 32;
    }
    const int h = size * dbl;
    if ((int)vline < y || (int)vline >= y + h) {
      continue;
    }
    const unsigned color = attr & 15u;
    if (color == 0) {
      continue;  // transparent
    }
    const uint16_t c = s_pen_lut[color];
    const int line_in = ((int)vline - y) / dbl;
    const int quads = size == 16 ? 2 : 1;
    const int qy = line_in / 8;
    const int sy = line_in % 8;
    for (int qx = 0; qx < quads; qx++) {
      // 16x16 blocks are column-major (matches the vendored fullscreen
      // renderer: quad_x outer, quad_y inner).
      const unsigned paddr =
          (pat_table + name * 8u + (unsigned)(qx * quads + qy) * 8u +
           (unsigned)sy) &
          16383u;
      const uint8_t bits0 = vram_byte(paddr);
      for (int b = 0; b < 8; b++) {
        if (bits0 & (0x80u >> b)) {
          const int dx = x + (qx * 8 + b) * dbl;
          if (dx >= 0 && dx < 256) {
            // dbl==2: widen to 2 pixels.
            dst[kBorderL + (unsigned)dx] = c;
            if (dbl == 2 && dx + 1 < 256) {
              dst[kBorderL + (unsigned)dx + 1u] = c;
            }
          }
        }
      }
    }
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
  // Canvas state FIRST: emux_geometry_changed() derives the border/stretch
  // menu maxes from max_border_*. Calling it before this runs (as before)
  // baked max=0 into the menu, so boot ended with border trim 0
  // (src 256x192 cropped) while the framebuffer is designed full
  // 352x304 with border. Any later stretch touch then re-applied the
  // cropped src at a bigger scale -> "small on reboot, huge after tweak".
  canvas_state[VIC_INDEX].fb_width = kFbWidth;
  canvas_state[VIC_INDEX].fb_height = kFbHeight;
  canvas_state[VIC_INDEX].gfx_w = kGfxW;
  canvas_state[VIC_INDEX].gfx_h = kGfxH;
  canvas_state[VIC_INDEX].border_w = kBorderL;
  canvas_state[VIC_INDEX].border_h = kBorderT;
  canvas_state[VIC_INDEX].max_border_w = canvas_state[VIC_INDEX].border_w;
  canvas_state[VIC_INDEX].max_border_h = canvas_state[VIC_INDEX].border_h;
  emux_geometry_changed(FB_LAYER_VIC);
  // Fresh installs baked value=0/max=0 at menu-build time (canvas was
  // still 0x0). Now that max is real, restore the intended full-border
  // default - but ONLY when no saved settings exist yet, so a user's
  // previously saved border trim is never clobbered on boot.
  {
    static bool s_border_defaulted = false;
    if (!s_border_defaulted) {
      s_border_defaulted = true;
      FILE *probe = fopen("/settings/settings-msx.txt", "rb");
      if (probe == nullptr) {
        probe = fopen("/settings-msx.txt", "rb");
      }
      const bool have_saved = (probe != nullptr);
      if (probe != nullptr) {
        fclose(probe);
      }
      if (!have_saved) {
        if (h_border_item[VIC_INDEX] != nullptr &&
            h_border_item[VIC_INDEX]->value == 0 &&
            h_border_item[VIC_INDEX]->max > 0) {
          h_border_item[VIC_INDEX]->value = h_border_item[VIC_INDEX]->max;
        }
        if (v_border_item[VIC_INDEX] != nullptr &&
            v_border_item[VIC_INDEX]->value == 0 &&
            v_border_item[VIC_INDEX]->max > 0) {
          v_border_item[VIC_INDEX]->value = v_border_item[VIC_INDEX]->max;
        }
      }
    }
  }
  // Source rect default: full framebuffer (border=max -> zero offsets).
  // The menu's video-apply recomputes these on change, but they must
  // never be 0,0,0,0 (empty cutout) before that happens.
  canvas_state[VIC_INDEX].left = 0;
  canvas_state[VIC_INDEX].top = 0;
  canvas_state[VIC_INDEX].vis_w = kFbWidth;
  canvas_state[VIC_INDEX].vis_h = kFbHeight;
  circle_clear_fbl(FB_LAYER_VIC);
  circle_show_fbl(FB_LAYER_VIC);
  circle_present_fbl(1u << FB_LAYER_VIC, 0);
  s_lut_dirty = true;
  // Publish complete geometry to the menu video machinery (border item
  // maxes were baked as 0 at menu-build time, before this ran) and apply
  // the current settings now, so boot shows exactly what a manual touch
  // would: no 1.0-vs-saved jump. Same pattern as the Atari/Amiga kernels.
  emux_frame_buffer_changed(FB_LAYER_VIC);
  debug_log("video_init: fbl %dx%d pitch=%d", kFbWidth, kFbHeight, pitch);
}

void video_render(void) {
  emux_ensure_video();
  // sync=0 like ZX/CPC: flip immediately, never stall the 50 fps core to
  // the display's vblank. sync=1 throttled the frame loop below 50 fps
  // (log realfps=48.4), which starved the 44.1 kHz audio sink (jittery
  // PLAY tones) and shifted HVS flip cadence. Pacing stays in
  // timer_pace_frame_end; static screens don't tear.
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
    *mode = s_last_mode;
  }
  if (lines != nullptr) {
    *lines = s_last_frame_lines;
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

}  // namespace msx
}  // namespace bmx

extern "C" {

void msx_video_frame_start(void) {
  bmx::msx::s_last_frame_lines = bmx::msx::s_src_line;
  bmx::msx::s_src_line = 0;
}

void msx_video_scanline(void) {
  using namespace bmx::msx;
  if (s_pixels == nullptr || msx_vram_memory == nullptr) {
    return;
  }
  if (s_lut_dirty) {
    rebuild_lut();
  }
  if (s_src_line >= (unsigned)kCoreLines) {
    return;
  }
  const unsigned src = s_src_line++;
  if (src < (unsigned)kLineOff ||
      src >= (unsigned)(kLineOff + kFbHeight)) {
    return;  // invisible overscan lines
  }
  const unsigned dest = src - (unsigned)kLineOff;
  uint16_t *dst =
      reinterpret_cast<uint16_t *>(s_pixels + (size_t)dest * (size_t)s_pitch);
  s_last_mode = vdp_mode();
  const unsigned border = vdp_9918a_registers[7] & 15u;
  const uint16_t bc = s_pen_lut[border];
  const int blank = (vdp_9918a_registers[1] & 0x40) == 0;
  if (blank || dest < (unsigned)kBorderT ||
      dest >= (unsigned)(kBorderT + kGfxH)) {
    for (int i = 0; i < kFbWidth; i++) {
      dst[i] = bc;
    }
    return;
  }
  const unsigned vline = dest - (unsigned)kBorderT;
  // Full-line border first (text mode draws 240 of 256 window pixels),
  // then display, then sprites on top.
  for (int i = 0; i < kFbWidth; i++) {
    dst[i] = bc;
  }
  draw_display_line(dst, vline);
  draw_sprites_line(dst, vline);
}

}  // extern "C"
