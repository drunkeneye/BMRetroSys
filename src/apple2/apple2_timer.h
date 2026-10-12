//
// apple2_timer.h
//
// Frame pacing for the 60.05 Hz Apple II video (16.65 ms budget).
// Mirrors cpc_timer (20 ms PAL budget) with the NTSC budget.
//

#ifndef apple2_timer_h
#define apple2_timer_h

namespace bmx {
namespace apple2 {

void timer_set_warp(bool warp);
bool timer_warp(void);
void timer_pace_frame_end(unsigned long frame_start_us);
void timer_pause_stats(unsigned long *calls, unsigned long *spins);

}  // namespace apple2
}  // namespace bmx

#endif
