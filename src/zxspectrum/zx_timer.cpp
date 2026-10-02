//
// zx_timer.cpp
//
// See zx_timer.h. Replicates the desktop 50 Hz timer contract against the
// Circle wall clock (microseconds) with free-running 20 ms tick slots,
// exactly like the hardware timer thread on desktop:
//   - timer_check_interrupt() (called per opcode) raises
//     interrupcion_timer_generada + interrupcion_fifty_generada once per
//     tick slot.
//   - timer_pause_waiting_end_frame() (called once per emulated frame when
//     the core finished early) spins until the next tick slot starts.
// Warp mode raises the flags on every call and never waits.
//

#include "zx_timer.h"

#include <circle/timer.h>
#include <sys/time.h>

extern "C" {
#include "zx_zesarux.h"
}

namespace bmx {
namespace zxspectrum {

namespace {
unsigned long s_last_tick = 0;
bool s_warp = false;
bool s_anchored = false;
unsigned long s_pause_calls = 0;
unsigned long s_pause_spins = 0;
}  // namespace

void timer_set_warp(bool warp) { s_warp = warp; }
bool timer_warp(void) { return s_warp; }
bool timer_anchored(void) { return s_anchored; }
unsigned long timer_tick(void) { return s_last_tick; }
void timer_advance(unsigned long slot) { s_last_tick = slot; }
void timer_pause_stats(unsigned long *calls, unsigned long *spins) {
  if (calls != nullptr) {
    *calls = s_pause_calls;
  }
  if (spins != nullptr) {
    *spins = s_pause_spins;
  }
}

// 50 Hz frame pacing, anchored to the frame-loop iteration start (see
// header): wait out the rest of the 20 ms budget, skip the wait on overrun.
void timer_pace_frame_end(unsigned long frame_start_us) {
  if (s_warp) {
    return;
  }
  const unsigned long now = CTimer::GetClockTicks();
  const unsigned long elapsed_us = now - frame_start_us;
  if (elapsed_us >= 20000UL) {
    return;  // overran: never catch up by waiting, just run flat out
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
      break;  // wall clock stalled; never wedge the machine
    }
  }
}

void timer_reset_frame_clock(void) {
  s_last_tick = CTimer::GetClockTicks() / 20000UL;
  s_anchored = true;
  interrupcion_timer_generada.v = 0;
  interrupcion_fifty_generada.v = 0;
}

}  // namespace zxspectrum
}  // namespace bmx

extern "C" {

void timer_check_interrupt(void) {
  if (bmx::zxspectrum::timer_warp()) {
    interrupcion_timer_generada.v = 1;
    interrupcion_fifty_generada.v = 1;
    return;
  }
  // The wall clock is MMIO-backed (~microseconds per read on Pi4), so
  // sampling it on every opcode alone costs tens of ms per frame. Sample
  // at most every 64 opcodes: ~70us granularity is plenty for 20ms slots.
  static unsigned divider = 0;
  if ((++divider & 63U) != 0U) {
    return;
  }
  const unsigned long slot = CTimer::GetClockTicks() / 20000UL;
  if (!bmx::zxspectrum::timer_anchored() || slot != bmx::zxspectrum::timer_tick()) {
    bmx::zxspectrum::timer_advance(slot);
    interrupcion_timer_generada.v = 1;
    interrupcion_fifty_generada.v = 1;
  }
}

// 50 Hz frame pacing without polling the wall clock in a tight loop (one
// MMIO tick read costs microseconds on Pi4; spinning on it burned seconds
// per frame). Sleep out whole milliseconds, then spin at most a short
// tail. Anchored to tick slots, so sub-ms errors never accumulate; over
// budget frames skip the wait instead of burst-catching-up.
//
// NOTE: frame pacing now lives in timer_pace_frame_end() (called from the
// frame loop after video+sound); this in-core entry point is deliberately a
// no-op. Waiting here is what quantized slightly-late frames to 40 ms: at
// frame end the timer flag is usually already consumed mid-frame, so this
// always observed "early in the current slot" and waited for the next slot
// boundary on top of already-late emulation (plus the post-wait sound dump
// and a possible second wait on the next opcode's fresh slot sample).
void timer_pause_waiting_end_frame(void) {
  return;
}

int timer_condicion_top_speed(void) { return 0; }

void timer_get_elapsed_core_frame_pre(void) {}
void timer_get_elapsed_core_frame_post(void) {}

void timer_stats_current_time(struct timeval *tiempo) {
  if (tiempo == NULL) {
    return;
  }
  const unsigned long us = CTimer::GetClockTicks();
  tiempo->tv_sec = (long)(us / 1000000UL);
  tiempo->tv_usec = (long)(us % 1000000UL);
}

long timer_stats_diference_time(struct timeval *antes,
                                struct timeval *despues) {
  if (antes == NULL || despues == NULL) {
    return 0;
  }
  return (long)((despues->tv_sec - antes->tv_sec) * 1000000L +
                (despues->tv_usec - antes->tv_usec));
}

void init_timer(void) { bmx::zxspectrum::timer_reset_frame_clock(); }
void timer_restart(void) { bmx::zxspectrum::timer_reset_frame_clock(); }
void timer_reset(void) { bmx::zxspectrum::timer_reset_frame_clock(); }
void timer_sleep(int milisec) {
  if (milisec > 0) {
    CTimer::SimpleMsDelay((unsigned)milisec);
  }
}
int timer_sleep_machine = 0;
int original_timer_sleep_machine = 0;
void timer_add_timer_usleep_to_bottom(void) {}

}  // extern "C"
