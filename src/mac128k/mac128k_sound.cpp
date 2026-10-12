//
// mac128k_sound.cpp
//
// Menu/bridge side for the Mac 128K audio backend.
//

#include "mac128k_sound.h"

#include "mac128k_debug.h"

void bmx_mac128k_audio_set_master_pct(int pct);
int bmx_mac128k_audio_get_master_pct(void);

namespace bmx {
namespace mac128k {

void sound_init(void) {
  debug_log("sound_init: menu side ready (backend drains in tick)");
}

void sound_shutdown(void) {
  debug_log("sound_shutdown");
}

int sound_get_volume(void) {
  return bmx_mac128k_audio_get_master_pct();
}

void sound_set_volume(int pct) {
  if (pct < 0) {
    pct = 0;
  }
  if (pct > 300) {
    pct = 300;
  }
  bmx_mac128k_audio_set_master_pct(pct);
}

}  // namespace mac128k
}  // namespace bmx
