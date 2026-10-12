//
// mac128k_bmx_video.cpp
//
// Mac 128K video backend for BMX bare-metal.
//
// The Mini vMac core keeps the 512x342 1-bit guest framebuffer in
// emulated RAM; the backend hook HaveChangedScreenBuff hands dirty rows
// to bmx_mac128k_video_blit_rows() below, which expands them into the
// fixed 800x600 RGB565 Circle FBL (centered, cleared border).
// bmx_mac128k_video_present() presents the FBL.
//

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "mac128k_bmx_glue.h"
#include "mac128k_debug.h"
#include "mac128k_video.h"
#include "src/monitor_phosphor.h"

extern "C" {
#include "third_party/common/circle.h"
#include "third_party/common/emux_api.h"
}

namespace {

const int kFblW = 800;
const int kFblH = 600;
// Guest 512x342 centered in the FBL.
const int kDstX0 = (kFblW - bmx::mac128k::kMac128kWidth) / 2;
const int kDstY0 = (kFblH - bmx::mac128k::kMac128kHeight) / 2;

uint8_t *s_fbl = nullptr;
int s_fbl_pitch = 0;
unsigned long s_presents = 0;
unsigned long s_blits = 0;
int s_monitor = 0;  // mirrors menu-side video_get_monitor()

inline uint16_t mono_to_565(int black) {
  // Mac 1-bit: 1 = black, 0 = white. Phosphor-map both ends so Green /
  // Amber modes tint the white; Color/B&W are identity-ish.
  uint16_t v = black ? 0x0000 : 0xFFFF;
  return bmx_monitor_map_565(v, s_monitor);
}

void ensure_fbl(void) {
  if (s_fbl != nullptr) {
    return;
  }
  uint8_t *pixels = nullptr;
  int pitch = 0;
  if (circle_alloc_fbl(FB_LAYER_VIC, 1, &pixels, kFblW, kFblH, &pitch) != 0 ||
      pixels == nullptr || pitch == 0) {
    bmx::mac128k::debug_log("mac128k: video: circle_alloc_fbl FAILED");
    return;
  }
  s_fbl = pixels;
  s_fbl_pitch = pitch;
  // Centered-in-FBL layout: the guest rect sits at (144,129) with a real
  // black border around it. The common scaler recomputes the displayed
  // source rect as vis=gfx+2*border at (left,top) on every video-settings
  // change ("Cut out is defined by top,left,vis_w,vis_h"), so the borders
  // must be the centering values (else the cutout collapses to the
  // upper-left gfx rect and the centered desktop is cropped away).
  // max_border == border pins the cutout to the full FBL by default.
  canvas_state[VIC_INDEX].fb_width = kFblW;
  canvas_state[VIC_INDEX].fb_height = kFblH;
  canvas_state[VIC_INDEX].gfx_w = bmx::mac128k::kMac128kWidth;
  canvas_state[VIC_INDEX].gfx_h = bmx::mac128k::kMac128kHeight;
  canvas_state[VIC_INDEX].border_w = kDstX0;
  canvas_state[VIC_INDEX].border_h = kDstY0;
  canvas_state[VIC_INDEX].max_border_w = kDstX0;
  canvas_state[VIC_INDEX].max_border_h = kDstY0;
  canvas_state[VIC_INDEX].left = 0;
  canvas_state[VIC_INDEX].top = 0;
  canvas_state[VIC_INDEX].vis_w = kFblW;
  canvas_state[VIC_INDEX].vis_h = kFblH;
  // Pin the displayed source rect to the full FBL: the common scaler
  // recomputes it as (left,top,vis) on video-settings changes, and stale
  // per-machine settings (or a zero border) would otherwise collapse it
  // to the upper-left gfx rect and crop the centered desktop away.
  circle_set_src_rect_fbl(FB_LAYER_VIC, 0, 0, kFblW, kFblH);
  emux_geometry_changed(FB_LAYER_VIC);
  // Apply geometry once (int-scaling silent path + initial video settings
  // application). Must NOT run per-present: it hides the VIC layer and
  // strobes visibility at present rate.
  emux_frame_buffer_changed(FB_LAYER_VIC);
  circle_show_fbl(FB_LAYER_VIC);
  memset(s_fbl, 0, (size_t)s_fbl_pitch * (size_t)kFblH);
  bmx::mac128k::debug_log("mac128k: video: FBL allocated pitch=%d", pitch);
}

}  // namespace

// --- Glue for mac128k_video.cpp (menu side) ---
uint8_t *bmx_mac128k_video_fbl(int *pitch, int *w, int *h) {
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

unsigned long bmx_mac128k_video_presents(void) {
  return s_presents;
}

// Clears the whole FBL to black (called once when emulation starts so no
// boot-stage fill color survives in the letterbox around the guest rect).
extern "C" void bmx_mac128k_video_clear(void) {
  ensure_fbl();
  if (s_fbl == nullptr) {
    return;
  }
  memset(s_fbl, 0, (size_t)s_fbl_pitch * (size_t)kFblH);
}

void bmx_mac128k_video_set_monitor(int mode) {
  s_monitor = mode;
}

void bmx_mac128k_video_stage_fill(uint16_t rgb565) {
  using bmx::mac128k::debug_log;
  ensure_fbl();
  if (s_fbl == nullptr) {
    return;
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
  debug_log("mac128k: video: stage fill 0x%04x presented (presents=%lu)",
            (unsigned)rgb565, s_presents);
}

// --- Mini vMac backend hooks (C linkage, called from backend .c) ---
extern "C" {

void bmx_mac128k_video_blit_rows(const unsigned char *macbits, int top,
                                 int bottom) {
  using bmx::mac128k::kMac128kRowBytes;
  using bmx::mac128k::kMac128kWidth;
  ensure_fbl();
  if (s_fbl == nullptr || macbits == nullptr) {
    return;
  }
  if (top < 0) {
    top = 0;
  }
  if (bottom > bmx::mac128k::kMac128kHeight) {
    bottom = bmx::mac128k::kMac128kHeight;
  }
  if (bottom <= top) {
    return;
  }
  s_monitor = bmx::mac128k::video_get_monitor();
  const int stride = s_fbl_pitch / 2;
  uint16_t *dst_base = reinterpret_cast<uint16_t *>(s_fbl);
  for (int y = top; y < bottom; y++) {
    const unsigned char *src = macbits + (size_t)y * kMac128kRowBytes;
    uint16_t *dst = dst_base + (size_t)(y + kDstY0) * (size_t)stride + kDstX0;
    for (int x = 0; x < kMac128kWidth; x++) {
      int black = (src[x >> 3] >> (7 - (x & 7))) & 1;
      dst[x] = mono_to_565(black);
    }
  }
  s_blits++;
}

void bmx_mac128k_video_present(void) {
  ensure_fbl();
  if (s_fbl == nullptr) {
    return;
  }
  emux_ensure_video();
  // Async present, exactly like the working mac68k kernel (and the boot
  // stage fills): the display flips the current buffer contents without
  // stalling the emulation thread. NOTE: no emux_frame_buffer_changed()
  // here — it hides the VIC layer per call (do_video_settings) and would
  // strobe visibility at present rate. Geometry is applied once at init.
  circle_present_fbl(FB_LAYER_MASK(FB_LAYER_VIC), 0);
  s_presents++;
}

}  // extern "C"
