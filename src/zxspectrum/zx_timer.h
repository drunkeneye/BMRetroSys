//
// zx_timer.h
//
// BMX replacement for the ZEsarUX timer.c subset used by the reduced
// Spectrum core: free-running 50 Hz tick slots on the Circle wall clock
// plus the statistics stubs screen.c expects.
//

#ifndef zx_timer_h
#define zx_timer_h

namespace bmx {
namespace zxspectrum {

// Called when a new model starts / after a reset.
void timer_reset_frame_clock(void);
// Warp: skip frame pacing so frames run flat out.
void timer_set_warp(bool warp);
bool timer_warp(void);
// Current tick-slot bookkeeping (20 ms slots).
bool timer_anchored(void);
unsigned long timer_tick(void);
void timer_advance(unsigned long slot);
// Pause stats (clock-free counters): calls and spin iterations since boot.
// A spin count pinned at the watchdog limit means the wall clock never
// advances past the slot (broken timer source, not slow emulation).
void timer_pause_stats(unsigned long *calls, unsigned long *spins);
// Frame pacing, called once per frame-loop iteration AFTER the frame's work
// (emulation + video + sound). Waits out the remainder of the 20 ms slot
// and returns; overran frames (and warp mode) return immediately instead of
// waiting for the next slot boundary. Waiting inside the vendored core
// cannot do this: by frame end the timer flag is usually already consumed,
// so the core always sees "early in current slot" and quantizes every
// slightly-late frame to a full extra slot (25 fps for ~21 ms of work).
void timer_pace_frame_end(unsigned long frame_start_us);

}  // namespace zxspectrum
}  // namespace bmx

#endif
