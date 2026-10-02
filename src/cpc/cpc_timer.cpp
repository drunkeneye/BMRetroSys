//
// cpc_timer.cpp
//
// See cpc_timer.h.
//

#include "cpc_timer.h"

#include <circle/timer.h>

namespace bmx {
namespace cpc {

namespace {
bool s_warp = false;
unsigned long s_pause_calls = 0;
unsigned long s_pause_spins = 0;
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

void timer_pace_frame_end(unsigned long frame_start_us) {  if (s_warp) {
    return;
  }
  const unsigned long now = CTimer::GetClockTicks();
  const unsigned long elapsed_us = now - frame_start_us;
  if (elapsed_us >= 20000UL) {
    return;
  }
  ++s_pause_calls;
  const unsigned long remain_us = 20000UL - elapsed_us;
  if (remain_us > 1500UL) {
    CTimer::SimpleMsDelay((unsigned)((remain_us - 500UL) / 1000UL));
  }
  unsigned long spins = 0;
  while (CTimer::GetClockTicks() - frame_start_us < 20000UL) {
    ++s_pause_spins;
    if (++spins > 1000000UL) {
      break;
    }
  }
}

}  // namespace cpc
}  // namespace bmx

// Upstream timer.c compatibility (referenced by linked cpu.c paths; the
// CPC kernel paces outside the core, so these are inert).
extern "C" {
int timer_sleep_machine = 0;
int original_timer_sleep_machine = 0;

void timer_restart(void) {}
}
