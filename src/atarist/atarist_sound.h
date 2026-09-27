//
// atarist_sound.h
//
// Hatari audio output for BMX bare-metal (Phase 0 placeholder).
//

#ifndef atarist_sound_h
#define atarist_sound_h

namespace bmx {
namespace atarist {

// ST audio sink rate (shared ViceSound pattern; YM2149 + STE DMA land here
// in Phase 3).
const int kHatariSoundRate = 44100;

void sound_init(void);
void sound_play(void);

}  // namespace atarist
}  // namespace bmx

#endif
