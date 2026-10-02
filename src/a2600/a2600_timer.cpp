//
// a2600_timer.cpp
//
// See a2600_timer.h.
//

#include "a2600_timer.h"

#include <circle/timer.h>

namespace bmx {
namespace a2600 {

namespace {

bool s_warp = false;

}  // namespace

void timer_set_warp(bool warp) { s_warp = warp; }

void timer_pace_frame_end(int refresh_rate, unsigned long frame_start_us) {
  if (s_warp) {
    return;
  }
  if (refresh_rate != 60) {
    refresh_rate = 50;
  }
  const unsigned long frame_us = 1000000UL / (unsigned long)refresh_rate;
  const unsigned long now = CTimer::GetClockTicks();
  unsigned long elapsed = (now >= frame_start_us)
                              ? (now - frame_start_us)
                              : 0;
  if (elapsed < frame_us) {
    CTimer::SimpleusDelay(frame_us - elapsed);
  }
}

}  // namespace a2600
}  // namespace bmx
