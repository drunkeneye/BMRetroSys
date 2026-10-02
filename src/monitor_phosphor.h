//
// monitor_phosphor.h
//
// Shared CRT monitor emulation for the non-VICE kernels (VICE machines use
// real .vpl palettes instead, selected from the Palette menu).
//
// Luma-driven phosphor mapping applied to the final RGB565 pixel, after all
// color adjustments, so the sliders cannot re-tint the phosphor. Color mode
// is the identity, i.e. the default look is bit-identical with or without
// this header in the path.
//
// C-compatible on purpose: C and C++ translation units share it
// (a2600 keeps its own identical copy for now and is intentionally
// untouched).
//

#ifndef BMX_MONITOR_PHOSPHOR_H
#define BMX_MONITOR_PHOSPHOR_H

#include <stdint.h>

enum BmxMonitorMode {
  BMX_MONITOR_COLOR = 0,
  BMX_MONITOR_BW = 1,
  BMX_MONITOR_GREEN = 2,
  BMX_MONITOR_AMBER = 3
};

static inline int bmx_monitor_valid(int mode) {
  return mode >= BMX_MONITOR_COLOR && mode <= BMX_MONITOR_AMBER;
}

static inline uint16_t bmx_monitor_map_565(uint16_t p, int mode) {
  if (mode == BMX_MONITOR_COLOR) {
    return p;
  }
  const int r = ((p >> 11) & 31) * 255 / 31;
  const int g = ((p >> 5) & 63) * 255 / 63;
  const int b = (p & 31) * 255 / 31;
  const int luma = (299 * r + 587 * g + 114 * b) / 1000;
  int ro = luma, go = luma, bo = luma;
  if (mode == BMX_MONITOR_GREEN) {
    ro = 0;
    bo = 0;
  } else if (mode == BMX_MONITOR_AMBER) {
    /* Amber ~ #FFB000: matches the VICE palette_C64_amber white point. */
    go = (luma * 169) / 255;
    bo = 0;
  }
  return (uint16_t)(((ro >> 3) << 11) | ((go >> 2) << 5) | (bo >> 3));
}

#endif
