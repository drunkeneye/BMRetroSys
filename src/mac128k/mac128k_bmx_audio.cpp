//
// mac128k_bmx_audio.cpp
//
// Mac 128K audio backend: drains Mini vMac 8-bit 22254Hz mono blocks
// into the Circle s16LE mono sink (blocking, paced to the audio clock).
// Master volume applies here (menu side in mac128k_sound.cpp).
//

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mac128k_debug.h"

extern "C" {
#include "third_party/common/circle.h"
}

namespace {

int s_volume_pct = 100;
unsigned long s_total_samples = 0;
// Circle sink chunking (mac68k pattern: <=2048 frames per write).
const unsigned kChunkFrames = 2048;
int16_t s_mono[kChunkFrames];

}  // namespace

extern "C" unsigned long bmx_mac128k_audio_total(void) {
  return s_total_samples;
}

void bmx_mac128k_audio_set_master_pct(int pct) {
  if (pct < 0) {
    pct = 0;
  }
  if (pct > 300) {
    pct = 300;
  }
  s_volume_pct = pct;
}

int bmx_mac128k_audio_get_master_pct(void) {
  return s_volume_pct;
}

extern "C" {

void bmx_mac128k_audio_write(const unsigned char *samp, unsigned int n) {
  if (samp == nullptr || n == 0) {
    return;
  }
  // First-drain line commented out (log quiet).
  // if (!s_first_logged) {
  //   s_first_logged = true;
  //   bmx::mac128k::debug_log("mac128k: audio: first drain, %u samples", n);
  // }
  const int vol = s_volume_pct;
  s_total_samples += n;
  unsigned done = 0;
  while (done < n) {
    unsigned chunk = n - done;
    if (chunk > kChunkFrames) {
      chunk = kChunkFrames;
    }
    for (unsigned i = 0; i < chunk; i++) {
      int32_t s = ((int32_t)samp[done + i] - 0x80) * 256;
      s = (s * vol) / 100;
      if (s > 32767) {
        s = 32767;
      }
      if (s < -32768) {
        s = -32768;
      }
      s_mono[i] = (int16_t)s;
    }
    circle_sound_write(s_mono, (size_t)chunk);
    done += chunk;
  }
}

}  // extern "C"
