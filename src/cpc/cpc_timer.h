//
// cpc_timer.h
//
// 50 Hz wall-clock frame pacing for the CPC kernel (the CPC frame is
// 19.968 ms; 20 ms slots are within 0.2%). Same design as the ZX kernel:
// pacing happens OUTSIDE the core, after video+sound.
//

#ifndef cpc_timer_h
#define cpc_timer_h

namespace bmx {
namespace cpc {

// Warp: skip frame pacing so tape loads run flat out.
void timer_set_warp(bool warp);
bool timer_warp(void);
// Frame pacing, called once per iteration AFTER frame work. Waits out the
// remainder of the 20 ms budget; overran frames return immediately.
void timer_pace_frame_end(unsigned long frame_start_us);
// Pause stats for the heartbeat log.
void timer_pause_stats(unsigned long *calls, unsigned long *spins);

}  // namespace cpc
}  // namespace bmx

#endif
