//
// apple2_sound.h
//
// 48 kHz stereo S16 from the KEGS shm ring -> 44100 Hz Circle audio.
// The core pushes via snddrv_send_sound(); sound_play() drains once per
// frame (skipped in warp, like the other kernels).
//

#ifndef apple2_sound_h
#define apple2_sound_h

namespace bmx {
namespace apple2 {

void sound_init(void);
void sound_play(void);
unsigned long sound_frames_total(void);
int sound_out_min(void);
int sound_out_max(void);
void sound_window_reset(void);
unsigned long sound_drop_frames(void);
unsigned long sound_starve_calls(void);
int sound_last_in_samples(void);

}  // namespace apple2
}  // namespace bmx

#endif
