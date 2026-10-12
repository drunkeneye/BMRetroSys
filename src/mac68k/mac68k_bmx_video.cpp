//
// mac68k_bmx_video.cpp
//
// Basilisk II video backend for BMX bare-metal.
//
// Implements the platform side of third_party/basilisk2/include/video.h:
// VideoInit/VideoExit/VideoRefresh/VideoInterrupt/VideoQuitFullScreen plus
// a monitor_desc subclass (BMX_monitor_desc) that owns the guest-visible
// framebuffer. The menu/bridge presentation API lives in mac68k_video.cpp
// (bmx::mac68k::video_*); this TU owns the guest framebuffer, the Circle
// FBL blit, and the palette LUTs.
//
// Addressing model: memory_init() maps the framebuffer according to
// TwentyFourBitAddressing (see uae_cpu/memory.cpp). With 24-bit addressing
// the frame24_bank at MacFrameBaseMac24Bit stores guest words in
// big-endian Mac order, so the blit must byteswap 16-bit pixels (and
// 555->565 expand). With 32-bit addressing (all 0x067c ROMs, i.e. every
// supported model) the frame_host_565_bank at MacFrameBaseMac already
// stores host-order RGB565, so the blit must NOT byteswap/convert again
// (upstream SDL reads the_buffer as host pixels). MacFrameLayout is set
// SDL-style (HOST_565/DIRECT); blit_band_16 branches on it (see below).
//
// Modes: 512x342 (classic) / 512x384 / 640x480 / 800x600, each at 8-bit
// indexed and 16-bit (Mac 555). Both depths are present in every
// resolution, which satisfies the Basilisk mode rules (lowest depth
// everywhere; no missing lower depths). Row bytes use TrivialBytesPerRow
// from include/video.h. The Circle FBL is fixed 800x600 RGB565 (largest
// mode); smaller modes are centered with a cleared border.
//

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

#include "sysdeps.h"
#include "cpu_emulation.h"
#include "main.h"
#include "prefs.h"
#include "video.h"

#include "mac68k_debug.h"
#include "mac68k_video.h"
#include "src/monitor_phosphor.h"

extern "C" {
#include "third_party/common/circle.h"
#include "third_party/common/emux_api.h"
}

// Upstream video.cpp owns VideoMonitors + monitor_desc base bits; the
// platform screen size globals live here (upstream defines them per
// platform backend, none of which is in the BMX build).
uint32 MacScreenWidth = 0;
uint32 MacScreenHeight = 0;

namespace {

// Fixed Circle FBL geometry: fits the largest advertised mode.
const int kFblW = 800;
const int kFblH = 600;

// Dirty-band height for the guest->FBL diff (SDL keeps a full copy in
// the_buffer_copy; we do the same in s_copy and diff per band).
const int kBandH = 16;

// Spare bytes past the guest framebuffer (bank mapping rounds up to
// whole 64 KiB banks; guest row 0 stays safely inside anyway).
const size_t kGuestSpare = 64;

uint8_t *s_guest = nullptr;      // Guest framebuffer (Mac BE order)
uint8_t *s_copy = nullptr;       // Last-blitted copy (dirty diff)
size_t s_guest_size = 0;
uint8_t *s_fbl = nullptr;        // Circle RGB565 FBL pixels
int s_fbl_pitch = 0;
bool s_inited = false;
bool s_full_repaint = true;
unsigned long s_presents = 0;
// Single-shot file-log diagnostics (one boot answers everything).
unsigned long s_refresh_calls = 0;
unsigned long s_refresh_early = 0;
unsigned long s_refresh_presented = 0;

// Last menu color state consumed by the blit (change => LUT rebuild +
// full repaint). Read live via the mac68k_video.h getters so the menu
// owns the values and this backend owns the derived tables.
int s_last_bri = 1000;
int s_last_con = 1000;
int s_last_gam = 1000;
int s_last_tin = 1000;
int s_last_sat = 1000;
int s_last_monitor = 0;

// Per-channel LUT for brightness/contrast/gamma (neutral = identity).
// Same math as the other BMX kernels (atarist_video.cpp).
uint8_t s_chan_lut[256];
bool s_chan_lut_ready = false;
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
  const float c = (float)s_last_con / 1000.0f;
  const float b = (float)(s_last_bri - 1000) / 1000.0f;
  float g = (float)s_last_gam / 1000.0f;
  if (g < 0.1f) {
    g = 0.1f;
  }
  if (g > 4.0f) {
    g = 4.0f;
  }
  for (int i = 0; i < 256; i++) {
    const float x = clamp01((float)i / 255.0f * c + b);
    s_chan_lut[i] = (uint8_t)(powf(x, g) * 255.0f + 0.5f);
  }
  const float ang = (float)(s_last_tin - 1000) / 1000.0f * 3.14159265f;
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
  s_chan_lut_ready = true;
}

inline bool colors_neutral(void) {
  return s_last_bri == 1000 && s_last_con == 1000 && s_last_gam == 1000 &&
         s_last_tin == 1000 && s_last_sat == 1000;
}

// Full color pipeline for one 8-bit-channel triple: LUT, saturation,
// hue rotation, quantize to RGB565, phosphor map. Mirrors the
// atarist/atarist_video.cpp adjusted path; neutral settings must use the
// fast paths below instead (zero overhead by construction).
uint16_t convert_adjusted888(int r8, int g8, int b8) {
  if (!s_chan_lut_ready) {
    rebuild_color_tables();
  }
  float fr = (float)s_chan_lut[r8] / 255.0f;
  float fg = (float)s_chan_lut[g8] / 255.0f;
  float fb = (float)s_chan_lut[b8] / 255.0f;
  const float s = (float)s_last_sat / 1000.0f;
  if (s != 1.0f) {
    const float luma = 0.299f * fr + 0.587f * fg + 0.114f * fb;
    fr = luma + (fr - luma) * s;
    fg = luma + (fg - luma) * s;
    fb = luma + (fb - luma) * s;
  }
  if (s_last_tin != 1000) {
    const float ir = fr;
    const float ig = fg;
    const float ib = fb;
    fr = s_hue_mat[0] * ir + s_hue_mat[1] * ig + s_hue_mat[2] * ib;
    fg = s_hue_mat[3] * ir + s_hue_mat[4] * ig + s_hue_mat[5] * ib;
    fb = s_hue_mat[6] * ir + s_hue_mat[7] * ig + s_hue_mat[8] * ib;
  }
  const int ri = (int)(clamp01(fr) * 31.0f + 0.5f);
  const int gi = (int)(clamp01(fg) * 63.0f + 0.5f);
  const int bi = (int)(clamp01(fb) * 31.0f + 0.5f);
  return bmx_monitor_map_565((uint16_t)((ri << 11) | (gi << 5) | bi),
                             s_last_monitor);
}

// Mac 555 (BE word, top bit unused) -> host RGB565, neutral colors.
inline uint16_t be555_to_565_fast(uint16_t be) {
  const uint16_t w = (uint16_t)((be >> 8) | (be << 8));
  const int r = (w >> 10) & 31;
  const int g = (w >> 5) & 31;
  const int b = w & 31;
  return (uint16_t)((r << 11) | (((g << 1) | (g >> 4)) << 5) | b);
}

}  // namespace

// --- monitor_desc base bits: owned by upstream video.cpp ------------------

class BMX_monitor_desc : public monitor_desc {
 public:
  BMX_monitor_desc(const vector<video_mode> &available_modes,
                   video_depth default_depth, uint32 default_id)
      : monitor_desc(available_modes, default_depth, default_id),
        lut_dirty(true) {
    memset(pal888, 0, sizeof(pal888));
    // Power-on palette: gray ramp (driver_open paints gray via
    // set_gray_palette before the guest sets a real palette).
    for (int i = 0; i < 256; i++) {
      lut565[i] = 0;
    }
  }

  void switch_to_current_mode(void) override;
  void set_palette(uint8 *pal, int num) override;

  void rebuild_lut(void);

  uint8 pal888[256 * 3];
  uint16_t lut565[256];
  bool lut_dirty;
};

void BMX_monitor_desc::rebuild_lut(void) {
  const bool neutral = colors_neutral();
  for (int i = 0; i < 256; i++) {
    const int r = pal888[i * 3 + 0];
    const int g = pal888[i * 3 + 1];
    const int b = pal888[i * 3 + 2];
    if (neutral) {
      lut565[i] = bmx_monitor_map_565(
          (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)),
          s_last_monitor);
    } else {
      lut565[i] = convert_adjusted888(r, g, b);
    }
  }
  lut_dirty = false;
}

void BMX_monitor_desc::set_palette(uint8 *pal, int num) {
  const video_mode &mode = get_current_mode();
  if (IsDirectMode(mode.depth)) {
    // Gamma table for direct modes; BMX applies color adjustments in the
    // blit instead, so there is nothing to store.
    bmx::mac68k::debug_log("bmx_video: set_palette: direct-mode gamma ignored "
                           "(depth=%d num=%d)",
                           (int)mode.depth, num);
    return;
  }
  if (pal == nullptr || num <= 0) {
    return;
  }
  if (num > 256) {
    num = 256;
  }
  memcpy(pal888, pal, (size_t)num * 3);
  lut_dirty = true;
  s_full_repaint = true;
  bmx::mac68k::debug_log("bmx_video: set_palette: stored %d entries", num);
}

namespace {

// (Re)allocates the guest framebuffer for the monitor's current mode,
// publishes it to the UAE bank mapping, and clears the FBL letterbox.
// Called from VideoInit and from BMX_monitor_desc::switch_to_current_mode
// (the core calls the latter with current_mode already updated).
void setup_current_mode(BMX_monitor_desc *mon) {
  const video_mode &mode = mon->get_current_mode();
  const uint32 w = mode.x;
  const uint32 h = mode.y;
  const uint32 rb = mode.bytes_per_row;
  const size_t need = (size_t)rb * (size_t)h + kGuestSpare;

  if (need != s_guest_size) {
    free(s_guest);
    free(s_copy);
    s_guest = (uint8_t *)malloc(need);
    s_copy = (uint8_t *)calloc(1, need);
    s_guest_size = (s_guest != nullptr && s_copy != nullptr) ? need : 0;
    if (s_guest_size == 0) {
      free(s_guest);
      free(s_copy);
      s_guest = nullptr;
      s_copy = nullptr;
    } else {
      memset(s_guest, 0, need);
    }
  }

  // SDL set_mac_frame_buffer equivalent: layout follows the guest depth
  // (HOST_565 for 16-bit, DIRECT for indexed), base follows the
  // addressing mode, then the banks are remapped onto the new buffer.
  if (mode.depth == VDEPTH_16BIT) {
    MacFrameLayout = FLAYOUT_HOST_565;
  } else {
    MacFrameLayout = FLAYOUT_DIRECT;
  }
  if (TwentyFourBitAddressing) {
    mon->set_mac_frame_base(MacFrameBaseMac24Bit);
  } else {
    mon->set_mac_frame_base(MacFrameBaseMac);
  }
  MacFrameBaseHost = s_guest;
  MacFrameSize = rb * h;
  MacScreenWidth = w;
  MacScreenHeight = h;
  InitFrameBufferMapping();

  if (s_fbl != nullptr && s_fbl_pitch != 0) {
    memset(s_fbl, 0, (size_t)s_fbl_pitch * (size_t)kFblH);
  }
  mon->lut_dirty = true;
  s_full_repaint = true;

  bmx::mac68k::debug_log(
      "bmx_video: mode %lux%lu depth=%d rowbytes=%lu base=0x%08lx fbsize=%lu "
      "layout=%d",
      (unsigned long)w, (unsigned long)h, (int)mode.depth,
      (unsigned long)rb, (unsigned long)mon->get_mac_frame_base(),
      (unsigned long)MacFrameSize, MacFrameLayout);
  // Single-shot: addressing + host pointers + canvas in the same line so one
  // boot distinguishes 24-bit vs 32-bit mapping and vis/gfx mismatch.
  bmx::mac68k::debug_log(
      "bmx_video: setup 24bit=%d baseMac=0x%08lx base24=0x%08lx host=%p size=%lu "
      "guest=%p copy=%p need=%lu fbl=%p pitch=%d canvas fb=%dx%d gfx=%dx%d "
      "vis=%dx%d left=%d top=%d border=%dx%d",
      TwentyFourBitAddressing ? 1 : 0, (unsigned long)MacFrameBaseMac,
      (unsigned long)MacFrameBaseMac24Bit, (void *)MacFrameBaseHost,
      (unsigned long)MacFrameSize, (void *)s_guest, (void *)s_copy,
      (unsigned long)need, (void *)s_fbl, s_fbl_pitch,
      canvas_state[VIC_INDEX].fb_width, canvas_state[VIC_INDEX].fb_height,
      canvas_state[VIC_INDEX].gfx_w, canvas_state[VIC_INDEX].gfx_h,
      canvas_state[VIC_INDEX].vis_w, canvas_state[VIC_INDEX].vis_h,
      canvas_state[VIC_INDEX].left, canvas_state[VIC_INDEX].top,
      canvas_state[VIC_INDEX].border_w, canvas_state[VIC_INDEX].border_h);
}

void blit_band_16(const video_mode &mode, uint32 y0, uint32 y1) {
  const uint32 w = mode.x;
  const uint32 rb = mode.bytes_per_row;
  const int dst_x0 = (kFblW - (int)w) / 2;
  const int dst_y0 = (kFblH - (int)mode.y) / 2;
  const int stride = s_fbl_pitch / 2;
  uint16_t *dst_base = reinterpret_cast<uint16_t *>(s_fbl);
  const bool neutral = colors_neutral();
  const int phosphor = s_last_monitor;
  // 32-bit addressing (frame_host_565_bank): s_guest already holds
  // host-order RGB565 (bank did the 555->565 conversion on write, cf.
  // uae_cpu/memory.cpp frame_host_565_wput). Copy directly; do NOT
  // byteswap or re-expand. 24-bit addressing (frame24_bank): s_guest
  // holds big-endian Mac 555, byteswap + 555->565 expand required.
  const bool host_pixels = !TwentyFourBitAddressing;

  for (uint32 y = y0; y < y1; y++) {
    const uint8_t *src =
        s_guest + (size_t)y * (size_t)rb;
    uint16_t *dst = dst_base + (size_t)(y + dst_y0) * (size_t)stride + dst_x0;
    if (host_pixels) {
      if (neutral && phosphor == BMX_MONITOR_COLOR) {
        for (uint32 x = 0; x < w; x++) {
          uint16_t v;
          memcpy(&v, src + x * 2, 2);
          dst[x] = v;
        }
      } else if (neutral) {
        for (uint32 x = 0; x < w; x++) {
          uint16_t v;
          memcpy(&v, src + x * 2, 2);
          dst[x] = bmx_monitor_map_565(v, phosphor);
        }
      } else {
        if (!s_chan_lut_ready) {
          rebuild_color_tables();
        }
        for (uint32 x = 0; x < w; x++) {
          uint16_t v;
          memcpy(&v, src + x * 2, 2);
          const int r5 = (v >> 11) & 31;
          const int g6 = (v >> 5) & 63;
          const int b5 = v & 31;
          dst[x] = convert_adjusted888(r5 * 255 / 31, g6 * 255 / 63,
                                       b5 * 255 / 31);
        }
      }
    } else {
      if (neutral && phosphor == BMX_MONITOR_COLOR) {
        for (uint32 x = 0; x < w; x++) {
          uint16_t be;
          memcpy(&be, src + x * 2, 2);
          dst[x] = be555_to_565_fast(be);
        }
      } else if (neutral) {
        for (uint32 x = 0; x < w; x++) {
          uint16_t be;
          memcpy(&be, src + x * 2, 2);
          dst[x] = bmx_monitor_map_565(be555_to_565_fast(be), phosphor);
        }
      } else {
        if (!s_chan_lut_ready) {
          rebuild_color_tables();
        }
        for (uint32 x = 0; x < w; x++) {
          uint16_t be;
          memcpy(&be, src + x * 2, 2);
          const uint16_t m = (uint16_t)((be >> 8) | (be << 8));
          const int r5 = (m >> 10) & 31;
          const int g5 = (m >> 5) & 31;
          const int b5 = m & 31;
          dst[x] = convert_adjusted888(r5 * 255 / 31, g5 * 255 / 31,
                                       b5 * 255 / 31);
        }
      }
    }
  }
}

void blit_band_8(BMX_monitor_desc *mon, const video_mode &mode, uint32 y0,
                 uint32 y1) {
  const uint32 w = mode.x;
  const uint32 rb = mode.bytes_per_row;
  const int dst_x0 = (kFblW - (int)w) / 2;
  const int dst_y0 = (kFblH - (int)mode.y) / 2;
  const int stride = s_fbl_pitch / 2;
  uint16_t *dst_base = reinterpret_cast<uint16_t *>(s_fbl);
  if (mon->lut_dirty) {
    mon->rebuild_lut();
  }
  for (uint32 y = y0; y < y1; y++) {
    const uint8_t *src = s_guest + (size_t)y * (size_t)rb;
    uint16_t *dst = dst_base + (size_t)(y + dst_y0) * (size_t)stride + dst_x0;
    for (uint32 x = 0; x < w; x++) {
      dst[x] = mon->lut565[src[x]];
    }
  }
}

// Pulls the live menu color/monitor state; returns true when anything
// changed (caller rebuilds LUTs and forces a full repaint).
bool sync_menu_state(void) {
  int bri = 1000, con = 1000, gam = 1000, tin = 1000, sat = 1000;
  bmx::mac68k::video_get_color_adjustments(&bri, &con, &gam, &tin, &sat);
  const int mon = bmx::mac68k::video_get_monitor();
  if (bri == s_last_bri && con == s_last_con && gam == s_last_gam &&
      tin == s_last_tin && sat == s_last_sat && mon == s_last_monitor) {
    return false;
  }
  s_last_bri = bri;
  s_last_con = con;
  s_last_gam = gam;
  s_last_tin = tin;
  s_last_sat = sat;
  s_last_monitor = mon;
  s_chan_lut_ready = false;
  return true;
}

// Parses "win/W/H" or "dga/W/H"; NULL selects the default below
// (classic 512x342 like SDL, else 640x480).
void parse_screen_pref(const char *mode_str, bool classic, int *dw, int *dh) {
  if (classic) {
    *dw = 512;
    *dh = 342;
  } else {
    *dw = 640;
    *dh = 480;
  }
  if (mode_str != nullptr) {
    int w = 0, h = 0;
    if (sscanf(mode_str, "win/%d/%d", &w, &h) == 2 ||
        sscanf(mode_str, "dga/%d/%d", &w, &h) == 2) {
      if (w > 0 && h > 0) {
        *dw = w;
        *dh = h;
      }
    }
  }
}

}  // namespace

void BMX_monitor_desc::switch_to_current_mode(void) {
  setup_current_mode(this);
  bmx::mac68k::debug_log("bmx_video: switch_to_current_mode done");
}

// --- Glue for mac68k_video.cpp (menu side) ------------------------------
// Declared extern there; defined here to avoid a new shared header.
uint8_t *bmx_mac68k_video_fbl(int *pitch, int *w, int *h) {
  if (pitch != nullptr) {
    *pitch = s_fbl_pitch;
  }
  if (w != nullptr) {
    *w = kFblW;
  }
  if (h != nullptr) {
    *h = kFblH;
  }
  return s_fbl;
}

void bmx_mac68k_video_invalidate(void) {
  s_full_repaint = true;
}

unsigned long bmx_mac68k_video_presents(void) {
  return s_presents;
}

// Early splash + boot-stage colors, usable before VideoInit() (no guest
// framebuffer needed). Allocates the fixed 800x600 RGB565 FBL on first
// use, fills it solid, shows and presents it. Called from menu-side
// video_init()/video_boot_stage() so the screen is never black during
// boot; VideoInit() below reuses the FBL when present.
void bmx_mac68k_video_stage_fill(uint16_t rgb565) {
  using bmx::mac68k::debug_log;
  if (s_fbl == nullptr) {
    uint8_t *pixels = nullptr;
    int pitch = 0;
    if (circle_alloc_fbl(FB_LAYER_VIC, 1, &pixels, kFblW, kFblH, &pitch) !=
            0 ||
        pixels == nullptr || pitch == 0) {
      debug_log("bmx_video: stage fill: circle_alloc_fbl FAILED");
      return;
    }
    s_fbl = pixels;
    s_fbl_pitch = pitch;
    canvas_state[VIC_INDEX].fb_width = kFblW;
    canvas_state[VIC_INDEX].fb_height = kFblH;
    canvas_state[VIC_INDEX].vis_w = kFblW;
    canvas_state[VIC_INDEX].vis_h = kFblH;
    emux_geometry_changed(FB_LAYER_VIC);
    circle_show_fbl(FB_LAYER_VIC);
    debug_log("bmx_video: stage fill: early FBL allocated pitch=%d", pitch);
  }
  for (int y = 0; y < kFblH; y++) {
    uint16_t *row = reinterpret_cast<uint16_t *>(s_fbl + (size_t)y *
                                                 (size_t)s_fbl_pitch);
    for (int x = 0; x < kFblW; x++) {
      row[x] = rgb565;
    }
  }
  circle_present_fbl(FB_LAYER_MASK(FB_LAYER_VIC), 0);
  emux_frame_buffer_changed(FB_LAYER_VIC);
  s_presents++;
  debug_log("bmx_video: stage fill 0x%04x presented (presents=%lu)",
            (unsigned)rgb565, s_presents);
}

// --- Basilisk platform entry points --------------------------------------

bool VideoInit(bool classic) {
  using bmx::mac68k::debug_log;
  if (s_inited) {
    debug_log("bmx_video: VideoInit: already initialized");
    return true;
  }

  const char *mode_str = PrefsFindString("screen");
  int def_w = 0, def_h = 0;
  parse_screen_pref(mode_str, classic, &def_w, &def_h);
  debug_log("bmx_video: VideoInit: classic=%d screen pref=%s default=%dx%d",
            classic ? 1 : 0, mode_str != nullptr ? mode_str : "(none)",
            def_w, def_h);

  // Build the VIDEO_MODE-equivalent list: 512x342 classic (0x80),
  // 640x480 (0x81), 800x600 (0x82), each at 8-bit indexed and 16-bit.
  // Row bytes use TrivialBytesPerRow (include/video.h). Upstream SDL
  // classic is 512x342 1-bit; we offer 512x342 at 8/16-bit so the mode
  // rules (lowest depth everywhere) still hold on a fixed FBL.
  static const struct {
    uint32 w, h, id;
  } kRes[] = {
      {512, 342, 0x80},
      {640, 480, 0x81},
      {800, 600, 0x82},
  };
  vector<video_mode> modes;
  for (size_t r = 0; r < sizeof(kRes) / sizeof(kRes[0]); r++) {
    video_mode m8;
    m8.x = kRes[r].w;
    m8.y = kRes[r].h;
    m8.resolution_id = kRes[r].id;
    m8.depth = VDEPTH_8BIT;
    m8.bytes_per_row = TrivialBytesPerRow(kRes[r].w, VDEPTH_8BIT);
    m8.user_data = 0;
    modes.push_back(m8);
    video_mode m16 = m8;
    m16.depth = VDEPTH_16BIT;
    m16.bytes_per_row = TrivialBytesPerRow(kRes[r].w, VDEPTH_16BIT);
    modes.push_back(m16);
  }

  // Honor displaycolordepth pref when valid (15/16 -> 16-bit, 8 -> 8-bit),
  // else classic ? 8-bit : 16-bit (matches SDL default_depth logic).
  video_depth def_depth = classic ? VDEPTH_8BIT : VDEPTH_16BIT;
  const int32 disp_depth = PrefsFindInt32("displaycolordepth");
  if (disp_depth == 8) {
    def_depth = VDEPTH_8BIT;
  } else if (disp_depth == 15 || disp_depth == 16) {
    def_depth = VDEPTH_16BIT;
  }
  uint32 def_id = 0x81;
  bool found = false;
  for (size_t r = 0; r < sizeof(kRes) / sizeof(kRes[0]); r++) {
    if (kRes[r].w == (uint32)def_w && kRes[r].h == (uint32)def_h) {
      def_id = kRes[r].id;
      found = true;
      break;
    }
  }
  if (!found) {
    // Clamp unknown pref sizes onto a known resolution.
    if (classic) {
      def_id = 0x80;
    } else {
      def_id = 0x81;
    }
    debug_log("bmx_video: VideoInit: pref size %dx%d not offered, using "
              "id=0x%02lx",
              def_w, def_h, (unsigned long)def_id);
  }

  BMX_monitor_desc *mon =
      new (std::nothrow) BMX_monitor_desc(modes, def_depth, def_id);
  if (mon == nullptr) {
    debug_log("bmx_video: VideoInit: monitor alloc FAILED");
    return false;
  }
  VideoMonitors.push_back(mon);
  setup_current_mode(mon);

  // Fixed Circle FBL (RGB565; circle_alloc_fbl takes (layer, pixelmode)
  // despite the swapped parameter names in circle.h). Reuses the
  // early-splash FBL when menu-side video_init() already allocated it.
  if (s_fbl == nullptr) {
    uint8_t *pixels = nullptr;
    int pitch = 0;
    if (circle_alloc_fbl(FB_LAYER_VIC, 1, &pixels, kFblW, kFblH, &pitch) !=
            0 ||
        pixels == nullptr || pitch == 0) {
      debug_log("bmx_video: VideoInit: circle_alloc_fbl FAILED");
      free(s_guest);
      free(s_copy);
      s_guest = nullptr;
      s_copy = nullptr;
      s_guest_size = 0;
      delete mon;
      VideoMonitors.clear();
      return false;
    }
    s_fbl = pixels;
    s_fbl_pitch = pitch;
  } else {
    debug_log("bmx_video: VideoInit: reusing early-splash FBL pitch=%d",
              s_fbl_pitch);
  }
  memset(s_fbl, 0, (size_t)s_fbl_pitch * (size_t)kFblH);

  canvas_state[VIC_INDEX].fb_width = kFblW;
  canvas_state[VIC_INDEX].fb_height = kFblH;
  canvas_state[VIC_INDEX].gfx_w = (int)MacScreenWidth;
  canvas_state[VIC_INDEX].gfx_h = (int)MacScreenHeight;
  // Centered-in-FBL layout (same lesson as the mac128k kernel): the
  // scaler recomputes the displayed cutout as vis=gfx+2*border, so the
  // borders must be the real centering values or the cutout collapses
  // to the upper-left gfx rect. max==border pins the default cutout to
  // the full FBL.
  canvas_state[VIC_INDEX].border_w = (kFblW - (int)MacScreenWidth) / 2;
  canvas_state[VIC_INDEX].border_h = (kFblH - (int)MacScreenHeight) / 2;
  canvas_state[VIC_INDEX].max_border_w = canvas_state[VIC_INDEX].border_w;
  canvas_state[VIC_INDEX].max_border_h = canvas_state[VIC_INDEX].border_h;
  canvas_state[VIC_INDEX].left = 0;
  canvas_state[VIC_INDEX].top = 0;
  canvas_state[VIC_INDEX].vis_w = kFblW;
  canvas_state[VIC_INDEX].vis_h = kFblH;
  circle_set_src_rect_fbl(FB_LAYER_VIC, 0, 0, kFblW, kFblH);
  emux_geometry_changed(FB_LAYER_VIC);

  circle_clear_fbl(FB_LAYER_VIC);
  circle_show_fbl(FB_LAYER_VIC);
  circle_present_fbl(FB_LAYER_MASK(FB_LAYER_VIC), 0);
  emux_frame_buffer_changed(FB_LAYER_VIC);

  sync_menu_state();
  s_inited = true;
  debug_log("bmx_video: VideoInit ok: guest %lux%lu fbl %dx%d pitch=%d",
            (unsigned long)MacScreenWidth, (unsigned long)MacScreenHeight,
            kFblW, kFblH, s_fbl_pitch);
  debug_log(
      "bmx_video: VideoInit canvas fb=%dx%d gfx=%dx%d vis=%dx%d left=%d top=%d "
      "border=%dx%d guest=%p copy=%p fbsize=%lu layout=%d 24bit=%d "
      "monitors=%lu",
      canvas_state[VIC_INDEX].fb_width, canvas_state[VIC_INDEX].fb_height,
      canvas_state[VIC_INDEX].gfx_w, canvas_state[VIC_INDEX].gfx_h,
      canvas_state[VIC_INDEX].vis_w, canvas_state[VIC_INDEX].vis_h,
      canvas_state[VIC_INDEX].left, canvas_state[VIC_INDEX].top,
      canvas_state[VIC_INDEX].border_w, canvas_state[VIC_INDEX].border_h,
      (void *)s_guest, (void *)s_copy, (unsigned long)MacFrameSize,
      MacFrameLayout, TwentyFourBitAddressing ? 1 : 0,
      (unsigned long)VideoMonitors.size());
  return true;
}

void VideoExit(void) {
  using bmx::mac68k::debug_log;
  for (size_t i = 0; i < VideoMonitors.size(); i++) {
    delete VideoMonitors[i];
  }
  VideoMonitors.clear();
  free(s_guest);
  free(s_copy);
  s_guest = nullptr;
  s_copy = nullptr;
  s_guest_size = 0;
  s_inited = false;
  // The Circle FBL stays allocated/shown so the menu keeps a display.
  debug_log("bmx_video: VideoExit done calls=%lu early=%lu presents=%lu",
            s_refresh_calls, s_refresh_early, s_presents);
}

void VideoQuitFullScreen(void) {
  // No fullscreen concept on BMX; the guest driver calls this around
  // error alerts, which are always safe here.
  bmx::mac68k::debug_log("bmx_video: VideoQuitFullScreen (no-op)");
}

void VideoRefresh(void) {
  s_refresh_calls++;
  // Honor frameskip pref like SDL (0 = every frame, N = every N+1th).
  // Upstream gates every refresh path on tick_counter >= frame_skip.
  const int32 frame_skip = PrefsFindInt32("frameskip");
  if (frame_skip > 0) {
    static unsigned s_skip_tick = 0;
    if (++s_skip_tick <= (unsigned)frame_skip) {
      return;
    }
    s_skip_tick = 0;
  }
  if (!s_inited || s_fbl == nullptr || s_guest == nullptr ||
      s_copy == nullptr || VideoMonitors.empty()) {
    s_refresh_early++;
    return;
  }
  emux_ensure_video();
  BMX_monitor_desc *mon = static_cast<BMX_monitor_desc *>(VideoMonitors[0]);
  const video_mode &mode = mon->get_current_mode();
  const uint32 w = mode.x;
  const uint32 h = mode.y;
  const uint32 rb = mode.bytes_per_row;
  if (w == 0 || h == 0 || w > (uint32)kFblW || h > (uint32)kFblH) {
    s_refresh_early++;
    return;
  }

  if (sync_menu_state()) {
    mon->lut_dirty = true;
    s_full_repaint = true;
  }

  const uint32 nbands = (h + (kBandH - 1)) / kBandH;
  if (s_full_repaint) {
    // The guest rect is centered in the fixed FBL; anything outside it
    // keeps whatever the boot-stage fills painted last (e.g. stage-8
    // white shows as a bright border around black content). Clear once
    // per full repaint so the border is black until the guest draws.
    memset(s_fbl, 0, (size_t)s_fbl_pitch * (size_t)kFblH);
  }
  for (uint32 b = 0; b < nbands; b++) {
    const uint32 y0 = b * kBandH;
    uint32 y1 = y0 + kBandH;
    if (y1 > h) {
      y1 = h;
    }
    const size_t off = (size_t)y0 * (size_t)rb;
    const size_t len = (size_t)(y1 - y0) * (size_t)rb;
    if (!s_full_repaint && memcmp(s_guest + off, s_copy + off, len) == 0) {
      continue;
    }
    if (mode.depth == VDEPTH_16BIT) {
      blit_band_16(mode, y0, y1);
    } else {
      blit_band_8(mon, mode, y0, y1);
    }
    memcpy(s_copy + off, s_guest + off, len);
  }
  s_full_repaint = false;

  circle_present_fbl(FB_LAYER_MASK(FB_LAYER_VIC), 0);
  s_presents++;
  s_refresh_presented++;
}

// Single-shot file-log diagnostic: called from the 60Hz tick every 600 ticks
// (~10s) so one boot proves refresh liveness, guest writes, and FBL output
// without per-frame spam. All output goes via debug_log -> SD log file.
void bmx_mac68k_video_diag(void) {
  using bmx::mac68k::debug_log;
  uint32 w = 0, h = 0, rb = 0;
  int depth = -1;
  bool lut_dirty = false;
  if (!VideoMonitors.empty()) {
    BMX_monitor_desc *mon = static_cast<BMX_monitor_desc *>(VideoMonitors[0]);
    const video_mode &mode = mon->get_current_mode();
    w = mode.x;
    h = mode.y;
    rb = mode.bytes_per_row;
    depth = (int)mode.depth;
    lut_dirty = mon->lut_dirty;
  }
  // Sampled FNV-1a checksums: guest/copy over Mac frame, FBL over RGB565.
  // Full scan every 10s would stall the tick; every 4096th byte is enough to
  // tell all-zero vs guest-drawn.
  uint32_t guest_ck = 2166136261u, copy_ck = 2166136261u, fbl_ck = 2166136261u;
  if (s_guest != nullptr && rb != 0 && h != 0) {
    size_t n = (size_t)rb * (size_t)h;
    for (size_t i = 0; i < n; i += 4096) {
      guest_ck ^= s_guest[i];
      guest_ck *= 16777619u;
    }
    guest_ck ^= (uint32_t)(n & 0xffffffffu);
  }
  if (s_copy != nullptr && rb != 0 && h != 0) {
    size_t n = (size_t)rb * (size_t)h;
    for (size_t i = 0; i < n; i += 4096) {
      copy_ck ^= s_copy[i];
      copy_ck *= 16777619u;
    }
    copy_ck ^= (uint32_t)(n & 0xffffffffu);
  }
  if (s_fbl != nullptr && s_fbl_pitch != 0) {
    size_t n = (size_t)s_fbl_pitch * (size_t)kFblH;
    const uint8_t *p = s_fbl;
    for (size_t i = 0; i < n; i += 4096) {
      fbl_ck ^= p[i];
      fbl_ck *= 16777619u;
    }
    fbl_ck ^= (uint32_t)(n & 0xffffffffu);
  }
  debug_log(
      "bmx_video: diag calls=%lu early=%lu presents=%lu presented=%lu "
      "mode=%lux%lu d=%d rb=%lu full=%d lutDirty=%d inited=%d guestCk=%08x "
      "copyCk=%08x fblCk=%08x guest=%p fbl=%p pitch=%d",
      s_refresh_calls, s_refresh_early, s_presents, s_refresh_presented,
      (unsigned long)w, (unsigned long)h, depth, (unsigned long)rb,
      s_full_repaint ? 1 : 0, lut_dirty ? 1 : 0, s_inited ? 1 : 0,
      guest_ck, copy_ck, fbl_ck, (void *)s_guest, (void *)s_fbl, s_fbl_pitch);
}

void VideoInterrupt(void) {
  // Same-thread VBL: SDL pumps events and presents from its redraw thread;
  // on BMX the emulator thread calls this, so refresh inline.
  VideoRefresh();
}
