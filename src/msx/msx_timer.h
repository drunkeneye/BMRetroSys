//
// msx_timer.h
//
// 50 Hz wall-clock frame pacing for the MSX kernel (the MSX1 frame is
// 312 lines at ~50 Hz; 20 ms slots are within 0.2%). Same design as the
// CPC kernel: pacing happens OUTSIDE the core, after video+sound.
//

#ifndef msx_timer_h
#define msx_timer_h

namespace bmx {
namespace msx {

// Warp: skip frame pacing so cassette loads run flat out.
void timer_set_warp(bool warp);
bool timer_warp(void);
// Frame pacing, called once per iteration AFTER frame work. Waits out the
// remainder of the 20 ms budget; overran frames return immediately.
void timer_pace_frame_end(unsigned long frame_start_us);
// Pause stats for the heartbeat log.
void timer_pause_stats(unsigned long *calls, unsigned long *spins);

}  // namespace msx
}  // namespace bmx

#endif
