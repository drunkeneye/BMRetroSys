//
// dos_video.cpp
//
// DOSBox-X video presentation for BMX bare-metal (Phase 0 placeholder +
// verbose bring-up tracing into bmretrosys-dos.log).
//

#include "dos_video.h"
#include "dos_debug.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <circle/timer.h>

#include <math.h>

// RENDER backend (replaces deleted src/gui/render.cpp): the PIC-driven
// VGA draw pipeline (VGA_VerticalTimer -> per-line DRAWLINE -> TempLine
// XRGB32 -> RENDER_DrawLine) lands here; we letterbox into the fixed
// 720x400 RGB565 VIC framebuffer. Needs DOS_INCLUDE_DIRS (see Makefile
// dos_video.o rule); no SDL stub wall. dosbox.h first: it defines Bitu
// (via config.h) which video.h needs.
#include "dosbox.h"
#include "render.h"
#include "vga.h"

extern "C" {
#include "third_party/common/circle.h"
#include "third_party/common/emux_api.h"
void emux_geometry_changed(int layer);
uint32_t bmx_host_ticks_ms(void);
}

// memio_complexity_optimization gates chained-vs-planar write handlers
// (defined in vendored vga.cpp).
extern bool memio_complexity_optimization;

namespace bmx {
namespace dos {

// Forward: one-shot source sampler/report (defined below the line sink).
void video_sample_source_line(const uint32_t *line);
void video_report_source_sample(void);
// Forward: menu-time live preview repaint (defined with the setters).
void video_refresh_preview(void);

namespace {

// VGA text mode 80x25 at 720x400 (matches the dos-ntsc-native HDMI mode).
const int kVicWidth = 720;
const int kVicHeight = 400;

unsigned long s_presents = 0;
unsigned long s_frames = 0;
uint8_t *s_pixels = nullptr;
int s_pitch = 0;

// Quiesced screenshot buffer: EndUpdate copies the finished frame here on
// request so the BMP writer (menu thread) never reads a half-painted canvas.
uint16_t s_shot[720 * 400] = {0};
volatile int s_shot_req = 0;
volatile int s_shot_done = 0;

// RENDER backend state: source geometry from RENDER_SetSize, destination is
// the fixed 720x400 fb. Lines arrive top-to-bottom via s_render_line_sink;
// s_line_y resets in RENDER_StartUpdate.
Bitu s_src_w = 0;
Bitu s_src_h = 0;
unsigned s_line_y = 0;
unsigned long s_guest_frames = 0;

// Shadow copy of the last frame at SOURCE resolution (XRGB32): the menu
// pauses the guest, so slider changes would otherwise sit invisible until
// the menu closes and frames flow again. video_refresh_preview() repaints
// the canvas from this shadow with current settings for live preview.
// Max guest source is 640x480 (1.2MB, BSS).
uint32_t s_shadow[640 * 480] = {0};
Bitu s_shadow_w = 0;
Bitu s_shadow_h = 0;

inline uint16_t xrgb32_to_rgb565(uint32_t p) {
  return (uint16_t)(((p >> 19) & 0x1f) << 11 | ((p >> 10) & 0x3f) << 5 |
                    ((p >> 3) & 0x1f));
}

// ---- Menu color adjustments (brightness/contrast/gamma/tint/saturation)
// Sliders: 0..2000 (gamma 0..4000), 1000 = neutral. Brightness/contrast/
// gamma fold into a 256-entry per-channel LUT rebuilt on every set;
// saturation (gray-axis mix) and tint (hue rotation about the gray axis)
// run per pixel. Neutral settings take the direct fast path above.
int s_col_bri = 1000;
int s_col_con = 1000;
int s_col_gam = 1000;
int s_col_tin = 1000;
int s_col_sat = 1000;
uint8_t s_chan_lut[256] = {0};
// Hue-rotation matrix (row-major 3x3), identity when tint is neutral.
float s_hue_mat[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

inline float clamp01(float x) {
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

inline bool colors_neutral(void) {
  return s_col_bri == 1000 && s_col_con == 1000 && s_col_gam == 1000 &&
         s_col_tin == 1000 && s_col_sat == 1000;
}

inline uint16_t xrgb32_to_rgb565_adjusted(uint32_t p) {
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
  return (uint16_t)((ri << 11) | (gi << 5) | bi);
}

// ScalerLineHandler_t: TempLine holds s_src_w XRGB32 pixels for row
// s_line_y. Nearest-neighbor letterbox into 720x400 RGB565.
//
// Column map LUT: sx=dx*src_w/dst_w needs a u64 divide per pixel (the sink's
// dominant cost on ARM). The map only depends on (src_w,dst_w), which change
// on mode switches alone — rebuild lazily, then it's one load per pixel.
uint32_t s_colmap[720] = {0};
unsigned s_colmap_sw = 0;
unsigned s_colmap_dw = 0;

// Perf snapshot inputs (see video_sink_stats): accumulated host time and
// converted source rows. Same thread throughout, no locking.
unsigned long long s_sink_us = 0;
unsigned long s_sink_rows = 0;
unsigned long s_abort_frames = 0;
unsigned long long s_present_us = 0;
unsigned long s_present_n = 0;
double s_last_fps = 0.0;

inline void rebuild_colmap(unsigned src_w, unsigned dst_w) {
  for (unsigned dx = 0; dx < dst_w && dx < 720; dx++) {
    uint32_t sx = (uint32_t)((uint64_t)dx * src_w / dst_w);
    if (sx >= src_w) {
      sx = src_w - 1;
    }
    s_colmap[dx] = sx;
  }
  s_colmap_sw = src_w;
  s_colmap_dw = dst_w;
}

void s_render_line_sink(const void *src) {
  if (src == nullptr || s_pixels == nullptr || s_src_w == 0 || s_src_h == 0) {
    return;
  }
  if (s_line_y >= s_src_h) {
    return;
  }
  uint32_t t0 = CTimer::GetClockTicks();
  const uint32_t *line = static_cast<const uint32_t *>(src);
  video_sample_source_line(line);
  // Shadow the source row for menu-time live preview repaints (the guest
  // stalls while the menu is open, so the sink is the only fresh source).
  if (s_src_w <= 640 && s_line_y < 480) {
    memcpy(&s_shadow[(size_t)s_line_y * 640], line, (size_t)s_src_w * 4);
  }
  // Scale to FIT inside 720x400 (letterbox): scale =
  // min(720/src_w, 400/src_h). Width-limited (dst spans full width) iff
  // 720/src_w <= 400/src_h, i.e. 720*src_h <= 400*src_w.
  uint32_t dst_w, dst_h;
  if (720u * s_src_h <= 400u * s_src_w) {
    dst_w = 720u;
    dst_h = (uint32_t)((uint64_t)s_src_h * 720u / s_src_w);
  } else {
    dst_h = 400u;
    dst_w = (uint32_t)((uint64_t)s_src_w * 400u / s_src_h);
  }
  if (dst_w == 0 || dst_w > 720u || dst_h == 0 || dst_h > 400u) {
    return;
  }
  if (s_src_w != s_colmap_sw || dst_w != s_colmap_dw) {
    rebuild_colmap((unsigned)s_src_w, dst_w);
  }
  const bool neutral = colors_neutral();
  uint32_t off_x = (720u - dst_w) / 2;
  uint32_t off_y = (400u - dst_h) / 2;
  // Which dst rows does this src row cover?
  uint32_t y0 = (uint32_t)((uint64_t)s_line_y * dst_h / s_src_h);
  uint32_t y1 = (uint32_t)((uint64_t)(s_line_y + 1) * dst_h / s_src_h);
  if (y0 >= dst_h) {
    s_line_y++;
    s_sink_us += (uint32_t)(CTimer::GetClockTicks() - t0);
    s_sink_rows++;
    return;
  }
  if (y1 > dst_h) {
    y1 = dst_h;
  }
  for (uint32_t dy = y0; dy < y1; dy++) {
    uint32_t fy = dy + off_y;
    if (fy >= 400u) {
      continue;
    }
    uint16_t *row = reinterpret_cast<uint16_t *>(
        s_pixels + (size_t)fy * (size_t)s_pitch);
    for (uint32_t dx = 0; dx < dst_w; dx++) {
      uint32_t fx = dx + off_x;
      if (fx >= 720u) {
        continue;
      }
      const uint32_t sx = s_colmap[dx];
      row[fx] = neutral ? xrgb32_to_rgb565(line[sx])
                        : xrgb32_to_rgb565_adjusted(line[sx]);
    }
  }
  s_line_y++;
  s_sink_us += (uint32_t)(CTimer::GetClockTicks() - t0);
  s_sink_rows++;
}

}  // namespace

// One-shot TempLine content sampler: captures 3 source lines of the first
// 320-wide frame so the log shows whether the draw procs deliver tiled
// content (e.g. planar quarters) vs one chunky image.
static uint32_t s_sample[3][320];
static unsigned s_sample_lineno[3] = {0, 0, 0};
static unsigned s_sampled_lines = 0;
static bool s_sample_reported = false;

void video_sample_source_line(const uint32_t *line) {
  if (s_sample_reported || s_src_w != 320 || s_sampled_lines >= 3) {
    return;
  }
  // Commit only non-blank lines PAST the top overscan border: border lines
  // (uniform overscan color) arrive first every window and would mask the
  // active video forever. s_line_y>=40 skips them.
  if (s_line_y < 40) {
    return;
  }
  unsigned nonzero = 0;
  for (unsigned i = 0; i < 320; i += 4) {
    if (line[i] != 0) {
      nonzero++;
      if (nonzero >= 2) {
        break;
      }
    }
  }
  if (nonzero < 2) {
    return;
  }
  for (unsigned i = 0; i < 320; i++) {
    s_sample[s_sampled_lines][i] = line[i];
  }
  s_sample_lineno[s_sampled_lines] = s_line_y;
  s_sampled_lines++;
}

// Re-arm the sampler so each 600-frame summary carries a fresh content
// verdict (gameplay frames, not just the fade-in).
void video_sampler_rearm(void) {
  s_sampled_lines = 0;
  s_sample_reported = false;
}

void video_report_source_sample(void) {
  if (s_sample_reported || s_sampled_lines < 3) {
    return;
  }
  s_sample_reported = true;
  for (unsigned l = 0; l < 3; l++) {
    unsigned eq01 = 0, eq02 = 0, eq03 = 0;
    for (unsigned i = 0; i < 80; i++) {
      if (s_sample[l][i] == s_sample[l][80 + i]) eq01++;
      if (s_sample[l][i] == s_sample[l][160 + i]) eq02++;
      if (s_sample[l][i] == s_sample[l][240 + i]) eq03++;
    }
    if (DOS_VGA_TRACE) {
      debug_log("RENDER: sample src-line%u q0=%08x q1=%08x q2=%08x q3=%08x eq=%u/%u/%u",
                s_sample_lineno[l], s_sample[l][0], s_sample[l][80],
                s_sample[l][160], s_sample[l][240], eq01, eq02, eq03);
    }
  }
}

// Clears the whole canvas to black. Called each frame before the guest
// rect is painted (see RENDER_StartUpdate below).
void video_clear_canvas(void) {
  if (s_pixels != nullptr && s_pitch != 0) {
    memset(s_pixels, 0, (size_t)kVicHeight * (size_t)s_pitch);
  }
}

void video_init(void) {
  debug_log("video_init: enter (target %dx%d RGB565)", kVicWidth, kVicHeight);
  debug_log("video_init: calling circle_alloc_fbl(VIC)...");
  // Mailbox presents: the 70fps guest outruns the 50Hz display; without
  // this every present vblank-waits (~15ms) and the guest video caps at
  // 50fps. Mailbox overwrites the pending back buffer instead of waiting,
  // so the guest never blocks and the display shows the latest frame.
  circle_set_mailbox_fbl(FB_LAYER_VIC, 1);
  circle_alloc_fbl(FB_LAYER_VIC, 1, &s_pixels, kVicWidth, kVicHeight,
                   &s_pitch);
  debug_log("video_init: alloc done pixels=%p pitch=%d", s_pixels, s_pitch);
  if (s_pixels == nullptr || s_pitch == 0) {
    debug_log("video_init FAILED: no framebuffer");
    return;
  }
  {
    int dpx = 0, dpy = 0, fbw = 0, fbh = 0, sw = 0, sh = 0, dw = 0, dh = 0;
    debug_log("video_init: calling circle_get_fbl_dimensions(VIC)...");
    circle_get_fbl_dimensions(FB_LAYER_VIC, &dpx, &dpy, &fbw, &fbh, &sw, &sh,
                              &dw, &dh);
    debug_log("video_init: display=%dx%d fb=%dx%d src=%dx%d dst=%dx%d", dpx,
              dpy, fbw, fbh, sw, sh, dw, dh);
  }
  debug_log("video_init: calling emux_geometry_changed(VIC)...");
  emux_geometry_changed(FB_LAYER_VIC);
  debug_log("video_init: geometry published");
  // Describe our canvas to the shared video-settings machinery. Without
  // this, do_video_settings (stretch/border/center) divides against zeros
  // and programs a degenerate src rect -> black screen after any change.
  // VGA text is full-frame: no borders, gfx == fb.
  canvas_state[VIC_INDEX].fb_width = kVicWidth;
  canvas_state[VIC_INDEX].fb_height = kVicHeight;
  canvas_state[VIC_INDEX].gfx_w = kVicWidth;
  canvas_state[VIC_INDEX].gfx_h = kVicHeight;
  canvas_state[VIC_INDEX].max_border_w = 0;
  canvas_state[VIC_INDEX].max_border_h = 0;
  canvas_state[VIC_INDEX].max_padding_w = 0;
  canvas_state[VIC_INDEX].max_padding_h = 0;
  canvas_state[VIC_INDEX].border_w = 0;
  canvas_state[VIC_INDEX].border_h = 0;
  debug_log("video_init: canvas_state published (720x400 full-frame)");
  rebuild_color_tables();
  // Sign of life: fill black (the letterbox color). Earlier this was blue,
  // which leaked through as permanent "blue borders" around any guest mode
  // smaller than 720x400, because the line sink only paints the guest rect.
  debug_log("video_init: filling background + show + present...");
  memset(s_pixels, 0, (size_t)kVicHeight * (size_t)s_pitch);
  circle_show_fbl(FB_LAYER_VIC);
  circle_present_fbl(FB_LAYER_MASK(FB_LAYER_VIC), 0);
  s_presents++;
  debug_log("video_init: OK (black screen presented)");
}

void video_set_color_adjustments(int brightness, int contrast, int gamma,
                                 int tint, int saturation) {
  s_col_bri = brightness;
  s_col_con = contrast;
  s_col_gam = gamma;
  s_col_tin = tint;
  s_col_sat = saturation;
  rebuild_color_tables();
  // Repaint immediately: the guest stalls while the menu is open, so without
  // this the slider would appear dead until the menu closes (atari800 does
  // the same via video_refresh_palette).
  video_refresh_preview();
  debug_log("video_set_color_adjustments: bri=%d con=%d gam=%d tin=%d sat=%d",
            brightness, contrast, gamma, tint, saturation);
}

void video_get_color_adjustments(int *brightness, int *contrast, int *gamma,
                                 int *tint, int *saturation) {
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

// Repaints the canvas from the source shadow with current color settings.
// Called on every slider change (menu thread) so the preview behind the
// menu tracks live even though the paused guest paints nothing. Mirrors the
// line-sink mapping exactly; out-of-range shadow content (mode change with
// no fresh frame yet) is skipped, never fabricated.
void video_refresh_preview(void) {
  if (s_pixels == nullptr || s_pitch == 0 || s_shadow_w == 0 ||
      s_shadow_h == 0 || s_shadow_w > 640 || s_shadow_h > 480) {
    return;
  }
  uint32_t dst_w, dst_h;
  if (720u * (uint32_t)s_shadow_h <= 400u * (uint32_t)s_shadow_w) {
    dst_w = 720u;
    dst_h = (uint32_t)((uint64_t)s_shadow_h * 720u / s_shadow_w);
  } else {
    dst_h = 400u;
    dst_w = (uint32_t)((uint64_t)s_shadow_w * 400u / s_shadow_h);
  }
  if (dst_w == 0 || dst_w > 720u || dst_h == 0 || dst_h > 400u) {
    return;
  }
  video_clear_canvas();
  uint32_t off_x = (720u - dst_w) / 2;
  uint32_t off_y = (400u - dst_h) / 2;
  bool neutral = colors_neutral();
  for (uint32_t dy = 0; dy < dst_h; dy++) {
    uint32_t fy = dy + off_y;
    if (fy >= 400u) {
      continue;
    }
    uint32_t sy = (uint32_t)((uint64_t)dy * s_shadow_h / dst_h);
    if (sy >= (uint32_t)s_shadow_h) {
      continue;
    }
    const uint32_t *srow = &s_shadow[(size_t)sy * 640];
    uint16_t *row = reinterpret_cast<uint16_t *>(
        s_pixels + (size_t)fy * (size_t)s_pitch);
    for (uint32_t dx = 0; dx < dst_w; dx++) {
      uint32_t fx = dx + off_x;
      if (fx >= 720u) {
        continue;
      }
      uint32_t sx = (uint32_t)((uint64_t)dx * s_shadow_w / dst_w);
      if (sx >= (uint32_t)s_shadow_w) {
        sx = (uint32_t)s_shadow_w - 1;
      }
      uint32_t p = srow[sx];
      row[fx] = neutral ? xrgb32_to_rgb565(p) : xrgb32_to_rgb565_adjusted(p);
    }
  }
  circle_present_fbl(FB_LAYER_MASK(FB_LAYER_VIC), 0);
  s_presents++;
}

void video_render(void) {
  s_frames++;
  if (s_frames == 1) {
    debug_log("video_render: first frame (pixels=%p)", s_pixels);
  }
  if (s_pixels != nullptr) {
    circle_present_fbl(FB_LAYER_MASK(FB_LAYER_VIC), 0);
    s_presents++;
  }
  if ((s_frames % 200) == 0 && DOS_VGA_TRACE) {
    debug_log("video_render: heartbeat frames=%lu presents=%lu", s_frames,
              s_presents);
  }
}

bool video_save_screenshot(const char *path) {
  debug_log("video_save_screenshot: path=%s", path != nullptr ? path : "(null)");
  if (path == nullptr || s_pixels == nullptr || s_pitch == 0) {
    return false;
  }
  // Ask the guest thread for a quiesced copy (see RENDER_EndUpdate); fall
  // back to the live canvas after 1s so a stalled guest can't wedge the menu.
  s_shot_done = 0;
  s_shot_req = 1;
  uint32_t t0 = bmx_host_ticks_ms();
  while (!s_shot_done && bmx_host_ticks_ms() - t0 < 1000) {
  }
  const uint16_t *frame = s_shot_done ? s_shot : nullptr;
  const int frame_pitch = s_shot_done ? (kVicWidth * 2) : s_pitch;
  if (frame == nullptr) {
    debug_log("video_save_screenshot: quiesce timeout, reading live canvas");
    frame = reinterpret_cast<const uint16_t *>(s_pixels);
  }
  // Uncompressed 24-bit BMP, bottom-up. 720x400 RGB565 canvas; torn frames
  // possible (core1 keeps drawing) but fine for debugging.
  FILE *fp = fopen(path, "wb");
  if (fp == nullptr) {
    debug_log("video_save_screenshot: open failed");
    return false;
  }
  const uint32_t row_bytes = (uint32_t)kVicWidth * 3u;
  const uint32_t img_bytes = row_bytes * (uint32_t)kVicHeight;
  uint8_t hdr[54] = {0};
  hdr[0] = 'B';
  hdr[1] = 'M';
  uint32_t fsize = 54 + img_bytes;
  hdr[2] = (uint8_t)(fsize & 0xff);
  hdr[3] = (uint8_t)((fsize >> 8) & 0xff);
  hdr[4] = (uint8_t)((fsize >> 16) & 0xff);
  hdr[5] = (uint8_t)((fsize >> 24) & 0xff);
  hdr[10] = 54;
  hdr[14] = 40;
  hdr[18] = (uint8_t)(kVicWidth & 0xff);
  hdr[19] = (uint8_t)((kVicWidth >> 8) & 0xff);
  hdr[22] = (uint8_t)(kVicHeight & 0xff);
  hdr[23] = (uint8_t)((kVicHeight >> 8) & 0xff);
  hdr[26] = 1;
  hdr[28] = 24;
  hdr[34] = (uint8_t)(img_bytes & 0xff);
  hdr[35] = (uint8_t)((img_bytes >> 8) & 0xff);
  hdr[36] = (uint8_t)((img_bytes >> 16) & 0xff);
  hdr[37] = (uint8_t)((img_bytes >> 24) & 0xff);
  bool ok = fwrite(hdr, 1, sizeof(hdr), fp) == sizeof(hdr);
  // Reuse a single row buffer (2160 bytes, stack-safe margin).
  static uint8_t row[720 * 3];
  for (int y = kVicHeight - 1; ok && y >= 0; y--) {
    const uint16_t *src = reinterpret_cast<const uint16_t *>(
        reinterpret_cast<const uint8_t *>(frame) + (size_t)y * (size_t)frame_pitch);
    for (int x = 0; x < kVicWidth; x++) {
      uint16_t p = src[x];
      row[x * 3 + 0] = (uint8_t)((p & 0x1f) << 3);          // B
      row[x * 3 + 1] = (uint8_t)(((p >> 5) & 0x3f) << 2);   // G
      row[x * 3 + 2] = (uint8_t)(((p >> 11) & 0x1f) << 3);  // R
    }
    ok = fwrite(row, 1, sizeof(row), fp) == sizeof(row);
  }
  ok = (fclose(fp) == 0) && ok;
  debug_log("video_save_screenshot: %s", ok ? "OK" : "FAILED");
  return ok;
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

unsigned long video_guest_frames(void) {
  return (unsigned long)s_guest_frames;
}

unsigned long video_abort_frames(void) {
  return s_abort_frames;
}

double video_emu_fps(void) {
  return s_last_fps;
}

void video_present_cost(unsigned long long *present_us,
                           unsigned long *present_n) {
  if (present_us != nullptr) {
    *present_us = s_present_us;
  }
  if (present_n != nullptr) {
    *present_n = s_present_n;
  }
}

void video_sink_stats(unsigned long long *sink_us, unsigned long *rows) {
  if (sink_us != nullptr) {
    *sink_us = s_sink_us;
  }
  if (rows != nullptr) {
    *rows = s_sink_rows;
  }
}

}  // namespace dos
}  // namespace bmx

// ---- RENDER backend (deleted src/gui/render.cpp) ----
// The PIC-driven VGA pipeline calls these; the line sink above (inside
// bmx::dos) does the conversion. RENDER_DrawLine is the pointer the
// pipeline invokes per line; render.src mirrors the source geometry for
// any vendored readers.
void RENDER_SetSize(Bitu width, Bitu height, Bitu bpp, float fps,
                    double scrn_ratio) {
  (void)scrn_ratio;
  bmx::dos::s_src_w = width;
  bmx::dos::s_src_h = height;
  bmx::dos::s_shadow_w = width;
  bmx::dos::s_shadow_h = height;
  bmx::dos::s_line_y = 0;
  RENDER_DrawLine = bmx::dos::s_render_line_sink;
  render.src.width = width;
  render.src.height = height;
  render.src.bpp = bpp;
  render.src.fps = fps;
  // Upstream render.cpp does this at the end of RENDER_SetSize: the
  // DRAWLINE procs write into TempLine, which nobody else allocates.
  // Without it the first draw event faults on a NULL line buffer.
  if (width != 0 && height != 0) {
    TempLineAlloc((unsigned int)width);
  }
  static Bitu s_last_w = 0, s_last_h = 0;
  bmx::dos::s_last_fps = (double)fps;
  if (width != s_last_w || height != s_last_h) {
    s_last_w = width;
    s_last_h = height;
    if (DOS_VGA_TRACE) {
      bmx::dos::debug_log("RENDER_SetSize: src=%lux%lu bpp=%lu fps=%.1f",
                              (unsigned long)width, (unsigned long)height,
                              (unsigned long)bpp, (double)fps);
    }
  }
}

bool RENDER_StartUpdate(void) {
  bmx::dos::s_line_y = 0;
  return true;
}

void RENDER_EndUpdate(bool abort) {
  if (abort) {
    bmx::dos::s_abort_frames++;
    return;
  }
  // The line sink only paints the guest rect: clear the canvas to black
  // once per source-geometry change (mode switch) so stale pixels never
  // linger as borders. Deliberately NOT every frame: a full 576KB clear
  // at 60fps wastes CPU, and rewriting the borders under the menu
  // compositor flickers at the boundary.
  static Bitu s_cleared_w = 0, s_cleared_h = 0;
  if (bmx::dos::s_src_w != s_cleared_w ||
      bmx::dos::s_src_h != s_cleared_h) {
    bmx::dos::video_clear_canvas();
    s_cleared_w = bmx::dos::s_src_w;
    s_cleared_h = bmx::dos::s_src_h;
  }
  bmx::dos::s_guest_frames++;
  // One-shot TempLine content report (first 320-wide frame only).
  bmx::dos::video_report_source_sample();
  // Quiesced screenshot capture: the menu (core0) sets s_shot_req; the next
  // EndUpdate copies the finished frame so the BMP writer never races the
  // line sink mid-frame (torn grabs look like tiling artifacts).
  if (bmx::dos::s_shot_req && bmx::dos::s_pixels != nullptr &&
      bmx::dos::s_pitch != 0) {
    memcpy(bmx::dos::s_shot, bmx::dos::s_pixels,
           (size_t)400 * (size_t)bmx::dos::s_pitch);
    bmx::dos::s_shot_req = 0;
    bmx::dos::s_shot_done = 1;
  }
  // Geometry diagnostics: lines actually delivered vs SetSize, plus the VGA
  // draw struct the procs wrote. A persistent mismatch here (e.g. 400
  // doubled lines for src_h 200, or a draw width != src width) explains
  // tiling/squish artifacts directly.
  {
    unsigned lines = bmx::dos::s_line_y;
    bool mismatch = (lines != (unsigned)bmx::dos::s_src_h) ||
                    (vga.draw.width != bmx::dos::s_src_w);
    static unsigned s_mismatch_logged = 0;
    if (mismatch && s_mismatch_logged < 8 && DOS_VGA_TRACE) {
      s_mismatch_logged++;
      bmx::dos::debug_log(
          "RENDER: geometry src=%lux%lu lines=%u draw=%lux%lu mode=%d "
          "llen=%lu blocks=%lu add=%lu step=%lu/%lu pan=%lu bpp=%lu",
          (unsigned long)bmx::dos::s_src_w,
          (unsigned long)bmx::dos::s_src_h, lines,
          (unsigned long)vga.draw.width, (unsigned long)vga.draw.height,
          (int)vga.mode, (unsigned long)vga.draw.line_length,
          (unsigned long)vga.draw.blocks, (unsigned long)vga.draw.address_add,
          (unsigned long)vga.draw.render_step,
          (unsigned long)vga.draw.render_max,
          (unsigned long)vga.draw.panning, (unsigned long)vga.draw.bpp);
    }
  }
  if (bmx::dos::s_pixels != nullptr) {
    // Submit throttle: the 70fps guest outruns 50/60Hz displays. Submits
    // closer than ~20ms apart risk overwriting the actively-scanning
    // buffer (heavy flicker with unlike portions per frame). Skipped
    // frames are NOT dropped work — the canvas keeps updating and the
    // next submit shows the latest; the guest itself is never throttled.
    // The mailbox flag covers transients; the fence covers lifecycle ops.
    static uint32_t s_last_submit_us = 0;
    uint32_t now_us = CTimer::GetClockTicks();
    if (s_last_submit_us == 0 ||
        (uint32_t)(now_us - s_last_submit_us) >= 20000u) {
      uint32_t pt0 = CTimer::GetClockTicks();
      circle_present_fbl(FB_LAYER_MASK(FB_LAYER_VIC), 0);
      bmx::dos::s_present_us += (uint32_t)(CTimer::GetClockTicks() - pt0);
      bmx::dos::s_present_n++;
      s_last_submit_us = now_us;
    }
    bmx::dos::s_presents++;
  }
  if ((bmx::dos::s_guest_frames % 600) == 0) {
    if (DOS_VGA_TRACE) {
      bmx::dos::debug_log(
          "RENDER: guest frames=%lu src=%lux%lu lines=%u draw=%lux%lu mode=%d "
          "llen=%lu blocks=%lu lbase=%lx ashift=%lu scanlen=%lu add=%lu split=%lu total=%lu "
          "chained=%d compat4=%d complex=%lu memopt=%d",
        bmx::dos::s_guest_frames,
        (unsigned long)bmx::dos::s_src_w,
        (unsigned long)bmx::dos::s_src_h, bmx::dos::s_line_y,
        (unsigned long)vga.draw.width, (unsigned long)vga.draw.height,
        (int)vga.mode, (unsigned long)vga.draw.line_length,
        (unsigned long)vga.draw.blocks,
        (unsigned long)vga.draw.linear_base,
        (unsigned long)vga.config.addr_shift,
        (unsigned long)vga.config.scan_len,
        (unsigned long)vga.draw.address_add,
        (unsigned long)vga.draw.split_line,
        (unsigned long)vga.draw.lines_total,
        (int)vga.config.chained, (int)vga.config.compatible_chain4,
        (unsigned long)vga.complexity.flags, (int)memio_complexity_optimization);
    }
    // Fresh content verdict for the next window (see sampler above).
    bmx::dos::video_sampler_rearm();
  }
}
