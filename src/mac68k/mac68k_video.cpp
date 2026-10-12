//
// mac68k_video.cpp
//
// Menu/bridge presentation side for the BMX Mac68k video backend.
//
// The Basilisk II platform backend (guest framebuffer, Circle FBL blit,
// palette LUTs) lives in mac68k_bmx_video.cpp. This TU keeps the
// bmx::mac68k::video_* menu API (signatures unchanged): it owns the
// color/monitor settings, forwards refreshes into VideoRefresh(), and
// implements the BMP screenshot from the backend FBL.
//

#include "mac68k_video.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "sysdeps.h"
#include "video.h"

#include "mac68k_debug.h"
#include "src/monitor_phosphor.h"

// Backend glue (defined in mac68k_bmx_video.cpp; declared here to avoid
// a new shared header).
extern uint8_t *bmx_mac68k_video_fbl(int *pitch, int *w, int *h);
extern void bmx_mac68k_video_invalidate(void);
extern unsigned long bmx_mac68k_video_presents(void);
extern void bmx_mac68k_video_stage_fill(uint16_t rgb565);

namespace bmx {
namespace mac68k {

namespace {

int s_brightness = 1000;
int s_contrast = 1000;
int s_gamma = 1000;
int s_tint = 1000;
int s_saturation = 1000;
int s_monitor = 0;  // 0 = Color (identity)

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
  // Show the splash immediately: without this the screen stays black
  // until the guest framebuffer comes up (VideoInit inside InitAll).
  // BRIGHT white: must be unmistakable vs black on any monitor.
  debug_log("video_init: early splash");
  bmx_mac68k_video_stage_fill(0xFFFF);  // white
  debug_blink(2);  // splash shown: 2 blink groups
}

// Boot-stage colors (RGB565): visible progress while boot_init() runs.
// 0 = splash navy, 1-8 = boot steps, 9 = guest starting (white),
// 10 = boot FAILED (red).
void video_boot_stage(int stage) {
  static const uint16_t kColors[] = {
      0xFFFF,  // 0 splash white (must read as "on" on any monitor)
      0x001F,  // 1 prefs blue
      0x03FF,  // 2 ROM scan cyan-ish
      0x07E0,  // 3 RAM green
      0x07FF,  // 4 ROM load cyan
      0xFFE0,  // 5 globals yellow
      0xF81F,  // 6 CheckROM magenta
      0xFD20,  // 7 InitAll orange
      0xFFFF,  // 8 boot complete white
      0xFFFF,  // 9 guest starting white
      0xF800,  // 10 FAILED red
  };
  const int n = (int)(sizeof(kColors) / sizeof(kColors[0]));
  if (stage < 0 || stage >= n) {
    return;
  }
  debug_log("video: boot stage %d", stage);
  bmx_mac68k_video_stage_fill(kColors[stage]);
}

void video_render(void) {
  VideoRefresh();
}

bool video_save_screenshot(const char *path) {
  int pitch = 0, w = 0, h = 0;
  uint8_t *pixels = bmx_mac68k_video_fbl(&pitch, &w, &h);
  if (path == nullptr || pixels == nullptr || pitch == 0 || w <= 0 ||
      h <= 0) {
    return false;
  }
  FILE *fp = fopen(path, "wb");
  if (fp == nullptr) {
    return false;
  }
  // Uncompressed 24-bit BMP straight from the RGB565 FBL (a2600 pattern).
  static const int kBpp = 3;
  const int row_stride = w * kBpp;
  const int row_pad = (4 - (row_stride % 4)) % 4;
  const uint32_t pixel_bytes =
      (uint32_t)(row_stride + row_pad) * (uint32_t)h;
  fwrite("BM", 1, 2, fp);
  write_le32(fp, 54 + pixel_bytes);
  write_le32(fp, 0);
  write_le32(fp, 54);
  write_le32(fp, 40);
  write_le32(fp, (uint32_t)w);
  write_le32(fp, (uint32_t)h);
  write_le16(fp, 1);
  write_le16(fp, 24);
  write_le32(fp, 0);
  write_le32(fp, pixel_bytes);
  write_le32(fp, 0);
  write_le32(fp, 0);
  write_le32(fp, 0);
  write_le32(fp, 0);
  static const uint8_t kPad[3] = {0, 0, 0};
  const int stride = pitch / 2;
  bool ok = true;
  for (int y = h - 1; y >= 0; y--) {
    const uint16_t *src =
        reinterpret_cast<const uint16_t *>(pixels + (size_t)y * (size_t)pitch);
    (void)stride;
    for (int x = 0; x < w; x++) {
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

void video_set_color_adjustments(int brightness, int contrast, int gamma,
                                 int tint, int saturation) {
  s_brightness = brightness;
  s_contrast = contrast;
  s_gamma = gamma;
  s_tint = tint;
  s_saturation = saturation;
  // The backend reads these live and rebuilds its LUTs; force a full
  // repaint so slider moves show even on a static guest frame.
  bmx_mac68k_video_invalidate();
}

void video_get_color_adjustments(int *brightness, int *contrast, int *gamma,
                                 int *tint, int *saturation) {
  if (brightness != nullptr) {
    *brightness = s_brightness;
  }
  if (contrast != nullptr) {
    *contrast = s_contrast;
  }
  if (gamma != nullptr) {
    *gamma = s_gamma;
  }
  if (tint != nullptr) {
    *tint = s_tint;
  }
  if (saturation != nullptr) {
    *saturation = s_saturation;
  }
}

void video_set_monitor(int mode) {
  if (!bmx_monitor_valid(mode)) {
    return;
  }
  s_monitor = mode;
  bmx_mac68k_video_invalidate();
}

int video_get_monitor(void) {
  return s_monitor;
}

void video_present_stats(unsigned long *presents, unsigned long *ensure_us,
                          unsigned long *present_us) {
  if (presents != nullptr) {
    *presents = bmx_mac68k_video_presents();
  }
  if (ensure_us != nullptr) {
    *ensure_us = 0;
  }
  if (present_us != nullptr) {
    *present_us = 0;
  }
}

}  // namespace mac68k
}  // namespace bmx
