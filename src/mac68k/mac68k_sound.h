//
// mac68k_sound.h
//
// Basilisk II audio output for BMX bare-metal (Phase 0 placeholder).
//

#ifndef mac68k_sound_h
#define mac68k_sound_h

#include <stdint.h>

namespace bmx {
namespace mac68k {

// Hardware PCM sink rate (shared with the other BMX kernels). The Mac
// audio path is configured to this rate at boot so no resampling is ever
// needed in the drain path.
const int kMac68kSoundRate = 44100;

void sound_init(void);
void sound_shutdown(void);

// Hands one chunk of interleaved stereo S16 frames to the sink.
// Phase 0: dropped (no audio sink yet).
void sound_write_chunk(const int16_t *frames, unsigned nframes);

// Master volume, percent (0..300, 100 = neutral).
int sound_get_volume(void);
void sound_set_volume(int pct);

}  // namespace mac68k
}  // namespace bmx

#endif
