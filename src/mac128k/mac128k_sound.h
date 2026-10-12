//
// mac128k_sound.h
//
// Mac 128K audio output for BMX bare-metal.
//

#ifndef mac128k_sound_h
#define mac128k_sound_h

namespace bmx {
namespace mac128k {

// Mini vMac classic sound: ~22254Hz 8-bit mono (370 samples/tick).
const int kMac128kSoundRate = 22254;

void sound_init(void);
void sound_shutdown(void);

// Master volume, percent (0..300, 100 = neutral).
int sound_get_volume(void);
void sound_set_volume(int pct);

}  // namespace mac128k
}  // namespace bmx

#endif
