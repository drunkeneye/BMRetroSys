//
// mac128k_video.cpp
//
// Menu/bridge presentation side for the Mac 128K video backend.
//

#include "mac128k_video.h"

#include <stdint.h>
#include <stdio.h>

#include "mac128k_debug.h"
#include "src/monitor_phosphor.h"

// Backend glue (defined in mac128k_bmx_video.cpp).
extern uint8_t *bmx_mac128k_video_fbl(int *pitch, int *w, int *h);
extern unsigned long bmx_mac128k_video_presents(void);
extern void bmx_mac128k_video_set_monitor(int mode);
extern void bmx_mac128k_video_stage_fill(uint16_t rgb565);

namespace bmx {
namespace mac128k {

namespace {

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
  debug_log("video_init: early splash");
  bmx_mac128k_video_stage_fill(0xFFFF);  // white
  debug_blink(2);
}

void video_boot_stage(int stage) {
  static const uint16_t kColors[] = {
      0xFFFF,  // 0 splash white
      0x001F,  // 1 debug blue
      0x07E0,  // 2 ROM green
      0xFFE0,  // 3 disks yellow
      0x07E0,  // 4 (unused, green)
      0x07E0,  // 5 (unused)
      0x07E0,  // 6 (unused)
      0x07E0,  // 7 (unused)
      0xFFFF,  // 8 boot complete white
      0xFFFF,  // 9 guest starting white
      0xF800,  // 10 FAILED red
  };
  const int n = (int)(sizeof(kColors) / sizeof(kColors[0]));
  if (stage < 0 || stage >= n) {
    return;
  }
  debug_log("video: boot stage %d", stage);
  bmx_mac128k_video_stage_fill(kColors[stage]);
}

void video_render(void) {
  // Refreshes happen in the backend tick (DoneWithDrawingForTick); the
  // idle pump has no guest, nothing to do.
}

bool video_save_screenshot(const char *path) {
  int pitch = 0, w = 0, h = 0;
  uint8_t *pixels = bmx_mac128k_video_fbl(&pitch, &w, &h);
  if (path == nullptr || pixels == nullptr || pitch == 0 || w <= 0 ||
      h <= 0) {
    return false;
  }
  FILE *fp = fopen(path, "wb");
  if (fp == nullptr) {
    return false;
  }
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
  bool ok = true;
  for (int y = h - 1; y >= 0; y--) {
    const uint16_t *src =
        reinterpret_cast<const uint16_t *>(pixels + (size_t)y * (size_t)pitch);
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

void video_set_monitor(int mode) {
  if (!bmx_monitor_valid(mode)) {
    return;
  }
  s_monitor = mode;
  bmx_mac128k_video_set_monitor(mode);
}

int video_get_monitor(void) {
  return s_monitor;
}

void video_present_stats(unsigned long *presents, unsigned long *ensure_us,
                         unsigned long *present_us) {
  if (presents != nullptr) {
    *presents = bmx_mac128k_video_presents();
  }
  if (ensure_us != nullptr) {
    *ensure_us = 0;
  }
  if (present_us != nullptr) {
    *present_us = 0;
  }
}

}  // namespace mac128k
}  // namespace bmx
