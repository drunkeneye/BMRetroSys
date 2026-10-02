//
// a2600_sound.h
//
// TIA audio: Stella mixes into its AudioQueue; the frame loop drains it
// here, resamples to the 44.1 kHz Circle sink and applies the menu
// volume (0..300 %). Mono downmix, same sink as the other kernels.
//

#ifndef a2600_sound_h
#define a2600_sound_h

#include <stddef.h>
#include <stdint.h>

namespace bmx {
namespace a2600 {

const int kStellaSoundRate = 44100;

void sound_init(void);
// Drain one frame worth of queued Stella audio (stella_rate Hz stereo)
// into the Circle sink. No-op when nothing is queued.
void sound_drain(const int16_t *stereo, size_t pairs, int stella_rate);

int sound_get_volume(void);
void sound_set_volume(int pct);

}  // namespace a2600
}  // namespace bmx

#endif
