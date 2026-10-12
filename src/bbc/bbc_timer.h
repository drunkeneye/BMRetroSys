//
// bbc_timer.h
//
// 50 Hz PAL frame pacing (BBC Model B is a 50 Hz machine). Mirrors
// apple2_timer (20 ms budget here instead of 16.65 ms).
//

#ifndef BMX_BBC_TIMER_H
#define BMX_BBC_TIMER_H

namespace bmx {
namespace bbc {

void timer_set_warp(bool warp);
bool timer_warp(void);
void timer_pause_stats(unsigned long *calls, unsigned long *spins);
void timer_pace_frame_end(unsigned long frame_start_us);

}  // namespace bbc
}  // namespace bmx

#endif  // BMX_BBC_TIMER_H
