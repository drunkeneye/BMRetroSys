//
// amiga_sound.cpp
//
// Amiga audio glue: per-frame drain of AudioPort::copyStereo() (float
// L/R at 44.1 kHz) into the Circle sink. Never blocks: capped by
// circle_sound_bufferspace(), excess samples are dropped (same rule as
// the DOSBox-X MIXER_BMX_Pull path).
//

#include "amiga_sound.h"
#include "amiga_config.h"
#include "amiga_debug.h"

extern "C" {
#include "third_party/common/circle.h"
}

#include <stddef.h>

#include <stdint.h>
#include <stdio.h>

namespace bmx {
namespace amiga {

namespace {

// PAL frame at 44.1kHz: exactly 882 stereo samples. Pull exactly one
// frame's worth per frame, never drain-all: upstream's ASR loop keeps
// the AudioStream around 50% fill by trimming production, and that
// control loop only converges when the consumer takes a steady ration.
// Draining everything every frame pinned the sink queue near-full
// (jitter + dropouts) and tripped the underflow wipe constantly.
const int kPullNominal = 882;
const int kPullMax = 1024;
float s_left[kPullMax];
float s_right[kPullMax];
int16_t s_mono[kPullMax];

}  // namespace

void sound_init(void) {
  // No-op: the ViceSound sink is created by circle_boot_complete() on the
  // first frame (same as the Hatari kernel); sound_play() drains into it.
  debug_log("sound_init: Paula drain at %dHz via ViceSound sink",
            kAmigaSoundRate);
}

void sound_play(void) {
  // One nominal frame per call; copyStereo returns fewer when Paula is
  // starved (short feed, sink underruns honestly) and leaves the rest
  // buffered so ASR sees true fill dynamics.
  int got = config_pull_audio(s_left, s_right, kPullNominal);
  if (got <= 0) {
    return;
  }
  int space = circle_sound_bufferspace();
  if (space <= 0) {
    return;
  }
  if (space < got) {
    got = space;
  }
  // Stereo -> mono average: the Circle sink (ViceSound AddChunk) takes
  // mono s16 frames (same as the DOSBox-X/Hatari kernels).
  for (int i = 0; i < got; i++) {
    float m = 0.5f * (s_left[i] + s_right[i]);
    if (m < -1.0f) {
      m = -1.0f;
    } else if (m > 1.0f) {
      m = 1.0f;
    }
    s_mono[i] = (int16_t)(m * 32767.0f);
  }
  circle_sound_write(s_mono, (size_t)got);
}

}  // namespace amiga
}  // namespace bmx
