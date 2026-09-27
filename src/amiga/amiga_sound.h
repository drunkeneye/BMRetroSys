//
// amiga_sound.h
//
// Amiga audio output for BMX bare-metal (Paula 4x8bit -> 44.1kHz sink).
//

#ifndef amiga_sound_h
#define amiga_sound_h

namespace bmx {
namespace amiga {

const int kAmigaSoundRate = 44100;

void sound_init(void);
void sound_play(void);

}  // namespace amiga
}  // namespace bmx

#endif
