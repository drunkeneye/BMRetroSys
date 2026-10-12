//
// apple2_timer.cpp
//
// See apple2_timer.h. The KEGS core's own micro_sleep() is a no-op on BMX
// (see the __BMX__ branch in third_party/apple2/clock.c); pacing happens
// here after each frame's work, so post-wait work can never push the frame
// over budget again.
//

#include "apple2_timer.h"

#include <circle/timer.h>

namespace bmx {
namespace apple2 {

namespace {
bool s_warp = false;
unsigned long s_pause_calls = 0;
unsigned long s_pause_spins = 0;
// 60.05 Hz frame budget.
const unsigned long kFrameUs = 16650UL;
}  // namespace

void timer_set_warp(bool warp) { s_warp = warp; }
bool timer_warp(void) { return s_warp; }

void timer_pause_stats(unsigned long *calls, unsigned long *spins) {
  if (calls != nullptr) {
    *calls = s_pause_calls;
  }
  if (spins != nullptr) {
    *spins = s_pause_spins;
  }
}

void timer_pace_frame_end(unsigned long frame_start_us) {
  if (s_warp) {
    return;
  }
  const unsigned long now = CTimer::GetClockTicks();
  const unsigned long elapsed_us = now - frame_start_us;
  if (elapsed_us >= kFrameUs) {
    return;
  }
  ++s_pause_calls;
  const unsigned long remain_us = kFrameUs - elapsed_us;
  if (remain_us > 1500UL) {
    CTimer::SimpleMsDelay((unsigned)((remain_us - 500UL) / 1000UL));
  }
  unsigned long spins = 0;
  while (CTimer::GetClockTicks() - frame_start_us < kFrameUs) {
    ++s_pause_spins;
    if (++spins > 1000000UL) {
      break;
    }
  }
}

}  // namespace apple2
}  // namespace bmx
