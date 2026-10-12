//
// bbc_video.cpp
//
// See bbc_video.h. CLK delivers Red1Green1Blue1 scanlines through the
// ScanTarget interface. Coordinate contract (verified in
// Outputs/CRT/CRT.cpp): scan endpoints are in output_scale units (large,
// flywheel-derived), while visible_area is NORMALISED 0..1. The driver
// therefore maps every scan pixel to normalised space first
// (x / scale.x), crops to the modal visible_area, and plots into an
// 800x600 RGB565 shadow that video_render() blits to the Circle
// framebuffer (monitor LUT applied).
//

#include "bbc_video.h"
#include "bbc_debug.h"
#include "src/monitor_phosphor.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// CLK core (vendored subset under third_party/bbc).
#include "Outputs/ScanTarget.hpp"

extern "C" {
#include "third_party/common/circle.h"
#include "third_party/common/emux_api.h"
}

namespace bmx {
namespace bbc {

namespace {

// Framebuffer and shadow: the native BBC frame. Full CRTC line at the
// 16 MHz pixel clock is 1024 units wide; all modes (640/320/160/teletext
// 480) fit, coarser modes pixel-double via the horizontal span fill.
// Height is the 312-line PAL field; scanlines are counted per field, so
// both fields land on the same rows (no interlace bounce by
// construction). Upscaling to HDMI is the GPU pipeline's job
// ([scaling bbc] in machines.ini), exactly like the CPC (1024x312) and
// Apple II (704x462) kernels: no float scaling in glue, only integer
// positions from stable counters.
const int kFbWidth = 640;
const int kFbHeight = 312;

// Scan data assembly.
//
// The CRT deposits pixel runs through begin_data/end_data, but it also
// makes stray 1-byte allocations (colour sampling, CRT.hpp) that may or
// may not be closed with end_data. A single length variable gets wiped by
// those strays (every scan then reads len 0 = black screen). Instead:
// a 4-slot ring gives every allocation its own storage, a tiny FIFO
// pairs each end_data with its begin_data, and only runs of >= 16 bytes
// (real pixel runs are 480) become the raster source. Scans index their
// run via data_offset, relative to that run's length.
const size_t kSlotSize = 16384;
const int kRingSlots = 4;
const size_t kMinRun = 16;

uint8_t s_ring[kRingSlots][kSlotSize];
int s_ring_next = 0;
int s_open[8];
int s_open_head = 0;
int s_open_tail = 0;
int s_run_slot = -1;
size_t s_run_len = 0;

uint8_t *s_pixels = nullptr;
int s_pitch = 0;
unsigned long s_presents = 0;

// Double-buffered native frame: rasterise always paints the back
// buffer; a completed even-parity field swaps it to the front, which is
// what video_render presents. The front therefore always holds exactly
// one complete field — never a fragment, never a mix.
uint16_t s_shadow_a[kFbWidth * kFbHeight];
uint16_t s_shadow_b[kFbWidth * kFbHeight];
uint16_t *s_front = s_shadow_a;
uint16_t *s_back = s_shadow_b;
// One rasterised scanline (span-filled, then copied over its row span).
uint16_t s_line[kFbWidth];
int s_prev_dy = -1;
// Per-field scanline counter: row = index, counted, never derived from
// flywheel coordinates (integers don't wobble).
int s_line_in_field = 0;
// Field framing + parity (bob deinterlace): a present always shows one
// complete even-parity field. Odd fields still run (timing/sound) but
// never paint, so alternating field content can't bounce.
bool s_field_ready = false;
unsigned long s_field_parity = 0;
// Per-field vertical anchor: consecutive PAL fields can sit ~half a
// scanline apart (or hunt slightly), which shows as a 1px vertical bounce
// on a progressive framebuffer. Each field's first scan sets the field
// base; rows render relative to the reference (first) field, cancelling
// any per-field offset. Reset on geometry change.
bool s_new_field = true;
bool s_have_ref = false;
float s_ref_base = 0.0f;
float s_field_base = 0.0f;

int s_col_bri = 1000;
int s_col_con = 1000;
int s_col_gam = 1000;
int s_col_tin = 1000;
int s_col_sat = 1000;

int s_monitor = BMX_MONITOR_COLOR;

uint16_t s_fast_lut[65536];
bool s_fast_lut_valid = false;

// Last modals from the core.
Outputs::Display::ScanTarget::Modals s_modals;
bool s_modals_seen = false;
unsigned long s_modals_count = 0;
Outputs::Display::InputDataType s_data_type =
    Outputs::Display::InputDataType::Red1Green1Blue1;

// Current scan assembly.
Outputs::Display::ScanTarget::Scan s_scan;
bool s_scan_open = false;

unsigned long s_scans_total = 0;
unsigned long s_scans_plotted = 0;
unsigned long s_scans_skipped = 0;
// Skip reasons (definitive diagnosis in one boot).
unsigned long s_skip_dtype = 0;
unsigned long s_skip_scale = 0;
unsigned long s_skip_ydiag = 0;
unsigned long s_skip_doff = 0;
unsigned long s_skip_w = 0;
unsigned long s_skip_dy = 0;
unsigned long s_skip_none = 0;
unsigned long s_skip_vw = 0;
unsigned long s_skip_odd = 0;
unsigned long s_begin_calls = 0;
unsigned long s_end_calls = 0;
unsigned long s_end_zero = 0;
unsigned long s_end_nonzero = 0;
unsigned s_end_last = 0;
unsigned long s_frames_announced = 0;
unsigned long s_submits = 0;
unsigned long s_renders = 0;
size_t s_first_data_len = 0;

// R1G1B1 byte -> RGB565 (raw; the monitor map applies at blit time).
inline uint16_t r1g1b1_to_565(uint8_t v) {
  const unsigned r = (v & 4u) ? 31u : 0u;
  const unsigned g = (v & 2u) ? 63u : 0u;
  const unsigned b = (v & 1u) ? 31u : 0u;
  return (uint16_t)((r << 11) | (g << 5) | b);
}

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

const char *data_type_name(Outputs::Display::InputDataType t) {
  return Outputs::Display::name(t);
}

bool modals_equal(const Outputs::Display::ScanTarget::Modals &a,
                  const Outputs::Display::ScanTarget::Modals &b) {
  return a.input_data_type == b.input_data_type &&
         a.display_type == b.display_type &&
         a.cycles_per_line == b.cycles_per_line &&
         a.colour_cycle_numerator == b.colour_cycle_numerator &&
         a.colour_cycle_denominator == b.colour_cycle_denominator &&
         a.expected_vertical_lines == b.expected_vertical_lines &&
         a.visible_area.origin.x == b.visible_area.origin.x &&
         a.visible_area.origin.y == b.visible_area.origin.y &&
         a.visible_area.size.width == b.visible_area.size.width &&
         a.visible_area.size.height == b.visible_area.size.height &&
         a.output_scale.x == b.output_scale.x &&
         a.output_scale.y == b.output_scale.y;
}

class BbcScanTarget : public Outputs::Display::ScanTarget {
 public:
  void set_modals(Modals modals) override {
    s_modals_count++;
    if (!s_modals_seen || !modals_equal(s_modals, modals)) {
      // Geometry moved (flywheel lock, mode change): stale shadow pixels
      // would smear over the new frame, so start clean.
      if (s_modals_seen) {
        memset(s_shadow_a, 0, sizeof(s_shadow_a));
        memset(s_shadow_b, 0, sizeof(s_shadow_b));
        s_prev_dy = -1;
        s_have_ref = false;
        s_new_field = true;
        debug_log("video: geometry changed, shadow cleared");
      }
      s_modals_seen = true;
      debug_log("video: modals #%lu data=%s display=%d cpl=%d gcd=%d "
                "ccyc=%d/%d vlines=%d vis=(%.3f,%.3f %.3fx%.3f) "
                "scale=%ux%u aspect=%.3f",
                s_modals_count, data_type_name(modals.input_data_type),
                (int)modals.display_type, modals.cycles_per_line,
                modals.clocks_per_pixel_greatest_common_divisor,
                modals.colour_cycle_numerator,
                modals.colour_cycle_denominator,
                modals.expected_vertical_lines,
                (double)modals.visible_area.origin.x,
                (double)modals.visible_area.origin.y,
                (double)modals.visible_area.size.width,
                (double)modals.visible_area.size.height,
                (unsigned)modals.output_scale.x,
                (unsigned)modals.output_scale.y, modals.aspect_ratio);
    }
    s_modals = modals;
    s_data_type = modals.input_data_type;
  }

  Scan *begin_scan() override {
    s_scan_open = true;
    return &s_scan;
  }

  void end_scan() override {
    if (!s_scan_open) {
      return;
    }
    s_scan_open = false;
    s_scans_total++;
    rasterise();
  }

  uint8_t *begin_data(size_t required_length,
                      size_t required_alignment = 1) override {
    (void)required_alignment;
    s_begin_calls++;
    if (s_first_data_len == 0) {
      s_first_data_len = required_length;
      debug_log("video: first begin_data %u bytes",
                (unsigned)required_length);
    }
    if (required_length > kSlotSize) {
      debug_log("video: begin_data %u bytes exceeds %u cap",
                (unsigned)required_length, (unsigned)kSlotSize);
      return nullptr;
    }
    const int slot = s_ring_next;
    s_ring_next = (s_ring_next + 1) % kRingSlots;
    // Track opens FIFO so each end_data pairs with its own begin_data.
    const int next_tail = (s_open_tail + 1) % 8;
    if (next_tail != s_open_head) {
      s_open[s_open_tail] = slot;
      s_open_tail = next_tail;
    }
    return s_ring[slot];
  }

  void end_data(size_t actual_length) override {
    s_end_calls++;
    if (actual_length == 0) {
      s_end_zero++;
    } else {
      s_end_nonzero++;
      s_end_last = (unsigned)actual_length;
    }
    // Pair with the oldest open allocation; only real pixel runs (>= 16
    // bytes; colour samples are 1) become the raster source.
    int slot = -1;
    if (s_open_head != s_open_tail) {
      slot = s_open[s_open_head];
      s_open_head = (s_open_head + 1) % 8;
    }
    if (slot >= 0 && actual_length >= kMinRun &&
        actual_length <= kSlotSize) {
      s_run_slot = slot;
      s_run_len = actual_length;
    }
  }

  void submit() override { s_submits++; }

  void set_delegate(Delegate &delegate) override {
    // Request constant vertical position per scan: without this the CRT
    // samples the vertical flywheel at both endpoints and every scan is
    // (correctly) slightly diagonal, which a framebuffer raster can't use.
    Delegate::Preferences prefs;
    prefs.force_horizontal_scans = true;
    delegate.set(prefs);
    debug_log("video: delegate set, requested force_horizontal_scans");
  }

  void announce(Event event, bool is_visible, const Scan::EndPoint &location,
                uint8_t composite_amplitude) override {
    (void)is_visible;
    (void)location;
    (void)composite_amplitude;
    if (event == Event::EndVerticalRetrace) {
      s_frames_announced++;
      s_new_field = true;
      // New field: scanlines restart at row 0 (both fields share rows).
      s_line_in_field = 0;
      // The field that just ended is complete. Paint/present odd-parity
      // fields only (bob): the new field paints the back buffer while
      // the front keeps showing the last complete odd field; on the next
      // (even) announce the buffers swap and the frame presents.
      s_field_parity++;
      if ((s_field_parity & 1) == 0) {
        uint16_t *tmp = s_front;
        s_front = s_back;
        s_back = tmp;
        s_field_ready = true;
      }
    }
  }

  private:
  void rasterise(void) {
    // Bob: only odd-parity fields paint (halves raster cost too).
    if ((s_field_parity & 1) == 0) {
      s_skip_odd++;
      s_scans_skipped++;
      return;
    }
    const int x0 = (int)s_scan.end_points[0].x;
    const int y0 = (int)s_scan.end_points[0].y;
    const int x1 = (int)s_scan.end_points[1].x;
    const int y1 = (int)s_scan.end_points[1].y;
    const int doff_dbg = (int)s_scan.end_points[0].data_offset;
    if (s_scans_total <= 8) {
      debug_log("video: scan#%lu (%d,%d)->(%d,%d) doff=%d run=%d/%u",
                s_scans_total, x0, y0, x1, y1, doff_dbg, s_run_slot,
                (unsigned)s_run_len);
    }
    if (s_data_type !=
        Outputs::Display::InputDataType::Red1Green1Blue1) {
      s_skip_dtype++;
      s_scans_skipped++;
      return;
    }
    const unsigned sx = s_modals.output_scale.x;
    if (sx == 0) {
      s_skip_scale++;
      s_scans_skipped++;
      return;
    }
    if (y0 != y1) {
      const int ydiff = y0 >= y1 ? (y0 - y1) : (y1 - y0);
      if (ydiff > 2) {
        // Genuinely non-horizontal (retrace/diagonal): nothing to plot.
        // (A 1-2 unit delta is flywheel sampling noise; use y0.)
        s_skip_ydiag++;
        s_scans_skipped++;
        return;
      }
    }
    // Samples come from the tracked run; data_offset indexes into it.
    if (s_run_slot < 0 || s_run_len == 0) {
      s_skip_doff++;
      s_scans_skipped++;
      return;
    }
    const int doff = (int)s_scan.end_points[0].data_offset;
    if (doff < 0 || (size_t)doff >= s_run_len) {
      s_skip_doff++;
      s_scans_skipped++;
      return;
    }
    // Samples [doff, run_len) spread across scan units [x0, x1): a ~39k
    // unit scan carries ~480 pixels, so units-per-sample, not 1:1.
    const size_t n = s_run_len - (size_t)doff;
    if (n == 0) {
      s_skip_w++;
      s_scans_skipped++;
      return;
    }
    // Horizontal: integer mapping into the 640-wide native frame.
    // ox/visw come from the modal visible_area (stable after flywheel
    // lock); all integer math, so identical input maps identically every
    // frame. Coarser modes land every 2nd/4th pixel and the span fill
    // doubles them (nearest neighbour, like CPC Mode1/0).
    int ox = 0, visw = 1;
    if (s_modals_seen) {
      ox = (int)(s_modals.visible_area.origin.x * (float)sx);
      visw = (int)(s_modals.visible_area.size.width * (float)sx);
    }
    if (visw <= 0) {
      s_skip_vw++;
      s_scans_skipped++;
      return;
    }
    const int xspan = x1 - x0;
    // Destination row: counted per field, both fields share rows.
    const int dy = s_line_in_field;
    if (s_line_in_field < kFbHeight) {
      s_line_in_field++;
    }
    if (dy < 0 || dy >= kFbHeight) {
      s_skip_dy++;
      s_scans_skipped++;
      return;
    }
    const uint8_t *src = &s_ring[s_run_slot][(size_t)doff];
    for (int x = 0; x < kFbWidth; x++) {
      s_line[x] = 0;
    }
    bool any = false;
    int last_dx = -1;
    for (size_t i = 0; i < n; i++) {
      // Sample position in scan units (multiply-first integer math).
      const int x = xspan != 0 ? (int)(x0 + (int)i * xspan / (int)n) : x0;
      const int dx = (x - ox) * kFbWidth / visw;
      const uint16_t pix = r1g1b1_to_565(src[i]);
      if (dx < 0 || dx >= kFbWidth) {
        last_dx = -1;
        continue;
      }
      if (last_dx < 0) {
        s_line[dx] = pix;
      } else if (dx >= last_dx) {
        for (int f = last_dx; f <= dx; f++) {
          s_line[f] = pix;
        }
      } else {
        for (int f = dx; f <= last_dx; f++) {
          s_line[f] = pix;
        }
      }
      last_dx = dx;
      any = true;
    }
    if (!any) {
      s_skip_none++;
      s_scans_skipped++;
      return;
    }
    memcpy(&s_back[(size_t)dy * kFbWidth], s_line, sizeof(s_line));
    s_prev_dy = dy;
    s_scans_plotted++;
  }
};

BbcScanTarget s_target;

}  // namespace

Outputs::Display::ScanTarget *bbc_scan_target(void) { return &s_target; }

bool video_field_ready(void) { return s_field_ready; }

void video_field_ack(void) { s_field_ready = false; }

void video_init(void) {
  memset(s_shadow_a, 0, sizeof(s_shadow_a));
  memset(s_shadow_b, 0, sizeof(s_shadow_b));
  s_front = s_shadow_a;
  s_back = s_shadow_b;
  s_field_ready = false;
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
  debug_log("video: fbl=%p pitch=%d canvas=%dx%d gfx=%dx%d border=%dx%d",
            (void *)pixels, pitch, kFbWidth, kFbHeight,
            canvas_state[VIC_INDEX].gfx_w, canvas_state[VIC_INDEX].gfx_h,
            canvas_state[VIC_INDEX].border_w,
            canvas_state[VIC_INDEX].border_h);
  emux_geometry_changed(FB_LAYER_VIC);
  canvas_state[VIC_INDEX].fb_width = kFbWidth;
  canvas_state[VIC_INDEX].fb_height = kFbHeight;
  canvas_state[VIC_INDEX].gfx_w = 640;
  canvas_state[VIC_INDEX].gfx_h = 256;
  canvas_state[VIC_INDEX].border_w = (kFbWidth - 640) / 2;
  canvas_state[VIC_INDEX].border_h = (kFbHeight - 256) / 2;
  canvas_state[VIC_INDEX].max_border_w = canvas_state[VIC_INDEX].border_w;
  canvas_state[VIC_INDEX].max_border_h = canvas_state[VIC_INDEX].border_h;
  circle_clear_fbl(FB_LAYER_VIC);
  circle_show_fbl(FB_LAYER_VIC);
  circle_present_fbl(1u << FB_LAYER_VIC, 0);
  rebuild_fast_lut();
  debug_log("video_init done: fbl=%dx%d shadow=%dx%d", kFbWidth, kFbHeight,
            kFbWidth, kFbHeight);
}

void video_render(void) {
  if (s_pixels == nullptr) {
    return;
  }
  if (!s_fast_lut_valid && sliders_default()) {
    rebuild_fast_lut();
  }
  // Front buffer -> FBL with the monitor map (1:1, same dimensions).
  const int stride = s_pitch / 2;
  for (int y = 0; y < kFbHeight; y++) {
    const uint16_t *src = &s_front[(size_t)y * kFbWidth];
    uint16_t *dst =
        reinterpret_cast<uint16_t *>(s_pixels + (size_t)y * (size_t)s_pitch);
    (void)stride;
    for (int x = 0; x < kFbWidth; x++) {
      dst[x] = s_fast_lut[src[x]];
    }
  }
  emux_ensure_video();
  circle_present_fbl(1u << FB_LAYER_VIC, 0);
  s_presents++;
  s_renders++;
  if (s_presents <= 3 || (s_renders % 250) == 0) {
    unsigned long long sum = 0;
    unsigned long nz = 0;
    const uint16_t *px = reinterpret_cast<const uint16_t *>(s_pixels);
    for (int y = 0; y < kFbHeight; y += 4) {
      for (int x = 0; x < kFbWidth; x += 4) {
        uint16_t v = px[(size_t)y * (size_t)stride + (size_t)x];
        sum += v;
        if (v != 0) {
          nz++;
        }
      }
    }
    debug_log("vidproof fbl#%lu scans=%lu plotted=%lu skipped=%lu "
              "frames=%lu submits=%lu datalen=%u sum=%llu nz=%lu "
              "sk(dt=%lu sc=%lu yd=%lu do=%lu w=%lu dy=%lu no=%lu vw=%lu od=%lu) "
              "beg=%lu end=%lu endz=%lu endnz=%lu endlast=%u",
              s_presents, s_scans_total, s_scans_plotted, s_scans_skipped,
              s_frames_announced, s_submits,
              (unsigned)s_first_data_len, sum, nz, s_skip_dtype,
              s_skip_scale, s_skip_ydiag, s_skip_doff, s_skip_w, s_skip_dy,
              s_skip_none, s_skip_vw, s_skip_odd, s_begin_calls, s_end_calls,
              s_end_zero, s_end_nonzero, s_end_last);
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
    *mode = s_modals_seen ? (unsigned)s_modals.expected_vertical_lines : 0u;
  }
  if (lines != nullptr) {
    *lines = (unsigned)(s_scans_total & 0xffffffffu);
  }
}

void video_src_stats(int *left, int *top, int *w, int *h) {
  if (s_modals_seen) {
    if (left != nullptr) {
      *left = (int)(s_modals.visible_area.origin.x * 1000.0f);
    }
    if (top != nullptr) {
      *top = (int)(s_modals.visible_area.origin.y * 1000.0f);
    }
    if (w != nullptr) {
      *w = (int)(s_modals.visible_area.size.width * 1000.0f);
    }
    if (h != nullptr) {
      *h = (int)(s_modals.visible_area.size.height * 1000.0f);
    }
    return;
  }
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

}  // namespace bbc
}  // namespace bmx
