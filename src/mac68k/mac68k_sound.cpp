//
// mac68k_sound.cpp
//
// Menu/bridge side for the BMX Mac68k audio backend.
//
// The Basilisk II platform backend (AudioInit/AudioInterrupt, Apple Mixer
// drain into the Circle sink) lives in mac68k_bmx_audio.cpp, which also
// owns the master volume/mute state. This TU keeps the bmx::mac68k::sound_*
// menu API (signatures unchanged) and routes volume through it.
//

#include "mac68k_sound.h"

#include <stdint.h>

#include "mac68k_debug.h"

// Backend glue (defined in mac68k_bmx_audio.cpp; declared here to avoid
// a new shared header).
extern void bmx_mac68k_audio_set_master_pct(int pct);
extern int bmx_mac68k_audio_get_master_pct(void);
extern void bmx_mac68k_audio_submit(const int16_t *frames, unsigned nframes);

namespace bmx {
namespace mac68k {

void sound_init(void) {
  // Real initialization happens in the Basilisk AudioInit() backend
  // (called by emulator boot); the menu side only owns routing here.
  debug_log("sound_init: menu side ready (backend inits in AudioInit)");
}

void sound_shutdown(void) {
  debug_log("sound_shutdown");
}

void sound_write_chunk(const int16_t *frames, unsigned nframes) {
  bmx_mac68k_audio_submit(frames, nframes);
}

int sound_get_volume(void) {
  return bmx_mac68k_audio_get_master_pct();
}

void sound_set_volume(int pct) {
  if (pct < 0) {
    pct = 0;
  }
  if (pct > 300) {
    pct = 300;
  }
  bmx_mac68k_audio_set_master_pct(pct);
}

}  // namespace mac68k
}  // namespace bmx
