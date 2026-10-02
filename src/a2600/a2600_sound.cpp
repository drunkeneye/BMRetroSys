//
// a2600_sound.cpp
//
// See a2600_sound.h.
//

#include "a2600_sound.h"
#include "a2600_config.h"
#include "a2600_debug.h"

#include <stdint.h>
#include <stdio.h>

extern "C" {
#include "third_party/common/circle.h"
}

namespace bmx {
namespace a2600 {

void sound_init(void) {
  debug_log("sound_init: TIA AudioQueue drain at %dHz", kStellaSoundRate);
}

int sound_get_volume(void) { return config_get_volume(); }
void sound_set_volume(int pct) { config_set_volume(pct); }

void sound_drain(const int16_t *stereo, size_t pairs, int stella_rate) {
  if (stereo == nullptr || pairs == 0 || stella_rate <= 0) {
    return;
  }
  int vol = config_get_volume();
  if (vol <= 0) {
    return;
  }
  if (vol > 300) {
    vol = 300;
  }
  // Nearest-neighbour resample Stella-rate stereo -> 44.1 kHz mono.
  static int16_t s_mono[2048];
  size_t want =
      (pairs * (size_t)kStellaSoundRate + (size_t)stella_rate - 1) /
      (size_t)stella_rate;
  if (want > sizeof(s_mono) / sizeof(s_mono[0])) {
    want = sizeof(s_mono) / sizeof(s_mono[0]);
  }
  for (size_t i = 0; i < want; i++) {
    size_t src = (i * pairs) / want;
    if (src >= pairs) {
      src = pairs - 1;
    }
    int32_t mixed = ((int32_t)stereo[src * 2] + (int32_t)stereo[src * 2 + 1]) / 2;
    mixed = (mixed * vol) / 100;
    if (mixed > 32767) {
      mixed = 32767;
    }
    if (mixed < -32768) {
      mixed = -32768;
    }
    s_mono[i] = (int16_t)mixed;
  }
  circle_sound_write(s_mono, want);
}

}  // namespace a2600
}  // namespace bmx
