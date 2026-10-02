//
// a2600_timer.h
//
// Frame pacing for the 2600 kernel: NTSC ROMs run at 60 Hz, PAL at
// 50 Hz. The core paces itself to the emulated refresh rate.
//

#ifndef a2600_timer_h
#define a2600_timer_h

namespace bmx {
namespace a2600 {

// Call once per emulated frame with the ROM refresh rate (50 or 60).
// Sleeps the remainder of the frame; warp disables pacing.
void timer_pace_frame_end(int refresh_rate, unsigned long frame_start_us);
void timer_set_warp(bool warp);

}  // namespace a2600
}  // namespace bmx

#endif
