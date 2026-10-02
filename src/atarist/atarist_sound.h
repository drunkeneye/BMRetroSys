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

// Master volume in percent (0..300, 100 = unity). C linkage so the plain-C
// audio backend (atarist_bmx_audio.c) can read it in the drain path.
extern "C" int atarist_volume_pct;

void sound_init(void);
void sound_play(void);
void sound_set_volume(int pct);
int sound_get_volume(void);

}  // namespace atarist
}  // namespace bmx

#endif
