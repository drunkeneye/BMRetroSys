//
// atarist_sound.cpp
//
// Hatari audio glue for BMX bare-metal.
//
// The actual mixing (YM2149 + STE DMA at 44.1 kHz) and the drain into the
// Circle sink both happen inside the core: Sound_Update() mixes into
// AudioMixBuffer and our Audio_Unlock() backend (atarist_bmx_audio.cpp)
// moves it to circle_sound_write(). This module keeps the BMX-side sound
// menu/bridge state (Phase 3 expands it with YM/DMA options).
//

#include "atarist_sound.h"
#include "atarist_debug.h"

namespace bmx {
namespace atarist {

extern "C" int atarist_volume_pct = 100;

void sound_set_volume(int pct) {
  if (pct < 0) {
    pct = 0;
  }
  if (pct > 300) {
    pct = 300;
  }
  atarist_volume_pct = pct;
}

int sound_get_volume(void) { return atarist_volume_pct; }

void sound_init(void) {
  debug_log("sound_init: YM2149 + STE DMA via AudioMixBuffer drain");
}

void sound_play(void) {
  // Drained synchronously by Audio_Unlock() during emulation; nothing to
  // do here (kept for frame-loop symmetry with the other kernels).
}

}  // namespace atarist
}  // namespace bmx
