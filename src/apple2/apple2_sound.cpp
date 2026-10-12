//
// apple2_sound.cpp
//
// See apple2_sound.h. Resampler/DC-blocker design mirrors cpc_sound.cpp:
// persistent 16.16 phase (exact long-term rate), per-channel one-pole
// high-pass (no per-frame mean stepping), volume scaling by the shared
// audiovolume menu value.
//

#include "apple2_sound.h"
#include "apple2_debug.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include "third_party/common/circle.h"
}

// KEGS core globals (global-scope externs; see the header).
#include "apple2_kegs_globals.h"
extern "C" {
#include "sound.h"
}

// Shared volume (0-100, driven by the menu). ZEsarUX audio.c owns this
// for the ZX/CPC/MSX kernels; the Apple II kernel defines its own.
int audiovolume = 100;

namespace bmx {
namespace apple2 {

namespace {

const int kCoreRate = 48000;
const int kSinkRate = 44100;
const long long kStep = ((long long)kCoreRate << 16) / kSinkRate;

// Core -> drain FIFO (stereo pairs).
const int kFifoPairs = 16384;
int16_t s_fifo[kFifoPairs * 2];
int s_fifo_head = 0;
int s_fifo_tail = 0;
int s_fifo_count = 0;

int16_t s_out[1024 * 2];
unsigned long s_frames_total = 0;
unsigned long s_drop_frames = 0;
unsigned long s_starve_calls = 0;
int s_last_in_samples = 0;
int s_out_min = 32767;
int s_out_max = -32768;

long long s_pos = 0;
long s_dc_lp[2] = {0, 0};

void fifo_push(int16_t left, int16_t right) {
  if (s_fifo_count >= kFifoPairs) {
    return;  // overrun: drop oldest implicitly by refusing new
  }
  s_fifo[s_fifo_head * 2] = left;
  s_fifo[s_fifo_head * 2 + 1] = right;
  s_fifo_head = (s_fifo_head + 1) % kFifoPairs;
  s_fifo_count++;
}

int resample_drain(int16_t *out, int out_capacity) {
  if (out == nullptr || out_capacity <= 0 || s_fifo_count <= 0) {
    return 0;
  }
  static int s_filt[2][2048];
  int in_pairs = s_fifo_count;
  if (in_pairs > 2048) {
    in_pairs = 2048;
  }
  const int vol = audiovolume;
  for (int ch = 0; ch < 2; ch++) {
    for (int i = 0; i < in_pairs; i++) {
      int idx = (s_fifo_tail + i) % kFifoPairs;
      const int mix = (int)s_fifo[idx * 2 + ch] * 256;
      s_dc_lp[ch] += (long)mix - (s_dc_lp[ch] >> 9);
      s_filt[ch][i] = mix - (int)(s_dc_lp[ch] >> 9);
    }
  }
  int n = 0;
  const long long limit = (long long)in_pairs << 16;
  while (s_pos < limit && n < out_capacity) {
    const int idx = (int)(s_pos >> 16);
    const int frac = (int)(s_pos & 0xffff);
    const int i0 = idx < in_pairs ? idx : in_pairs - 1;
    const int i1 = (idx + 1) < in_pairs ? idx + 1 : in_pairs - 1;
    for (int ch = 0; ch < 2; ch++) {
      const int s0 = s_filt[ch][i0];
      const int s1 = s_filt[ch][i1];
      int v = s0 + (int)(((long long)(s1 - s0) * frac) >> 16);
      v = v * vol / 100;
      if (v > 32767) {
        v = 32767;
      }
      if (v < -32768) {
        v = -32768;
      }
      out[n * 2 + ch] = (int16_t)v;
    }
    n++;
    s_pos += kStep;
  }
  // Consume only what the phase advanced past, keeping input and
  // phase aligned when the output buffer fills first.
  int consumed = (int)(s_pos >> 16);
  if (consumed > in_pairs) {
    consumed = in_pairs;
  }
  if (consumed < 0) {
    consumed = 0;
  }
  s_pos -= (long long)consumed << 16;
  s_fifo_tail = (s_fifo_tail + consumed) % kFifoPairs;
  s_fifo_count -= consumed;
  return n;
}

void drain_produced(void) {
  const int n =
      resample_drain(s_out, (int)(sizeof(s_out) / sizeof(s_out[0])) / 2);
  if (n <= 0) {
    s_starve_calls++;
    return;
  }
  s_last_in_samples = n;
  for (int i = 0; i < n * 2; i++) {
    if (s_out[i] < s_out_min) {
      s_out_min = s_out[i];
    }
    if (s_out[i] > s_out_max) {
      s_out_max = s_out[i];
    }
  }
  int space = circle_sound_bufferspace();
  if (space < 0) {
    space = 0;
  }
  int want = n;
  if (want > space) {
    want = space;
  }
  if (want <= 0) {
    s_drop_frames++;
    return;
  }
  circle_sound_write(s_out, (size_t)want);
  s_frames_total += (unsigned long)want;
}

}  // namespace

void sound_init(void) {
  g_preferred_rate = 48000;
  if (g_sound_shm_addr == nullptr) {
    g_sound_shm_addr = (word32 *)calloc(SOUND_SHM_SAMP_SIZE, 4);
  }
  sound_set_audio_rate(g_preferred_rate);
  g_audio_enable = -1;
  debug_log("sound: shm=%p rate=%d enable=%d fifocap=%d pairs",
            (void *)g_sound_shm_addr, g_preferred_rate, g_audio_enable,
            kFifoPairs);
  s_pos = 0;
  s_dc_lp[0] = s_dc_lp[1] = 0;
  s_fifo_head = s_fifo_tail = s_fifo_count = 0;
  debug_log("sound_init: BMX drain (48000->%dHz)", kSinkRate);
}

void sound_play(void) { drain_produced(); }

unsigned long sound_frames_total(void) { return s_frames_total; }
unsigned long sound_drop_frames(void) { return s_drop_frames; }
unsigned long sound_starve_calls(void) { return s_starve_calls; }
int sound_last_in_samples(void) { return s_last_in_samples; }
int sound_out_min(void) { return s_out_min; }
int sound_out_max(void) { return s_out_max; }
void sound_window_reset(void) {
  s_out_min = 32767;
  s_out_max = -32768;
}

}  // namespace apple2
}  // namespace bmx

// ---- KEGS sound-driver seam (replaces sound_driver.c) ---------------------
//
// The core mixes stereo S16 pairs into the shm ring and reports them via
// snddrv_send_sound(real, size): `size` newest pairs ending at
// g_sound_shm_pos (wrapping). Silence batches (real=0) are skipped — the
// Circle sink idles on its own. Word layout (little-endian core):
// low 16 = left (val0), high 16 = right (val1).
extern "C" {

void snddrv_init(void) { ::bmx::apple2::sound_init(); }

void snddrv_shutdown(void) {}

int child_send_samples(byte *ptr, int size) {
  (void)ptr;
  return size;
}

void snddrv_send_sound(int real_samps, int size) {
  using namespace ::bmx::apple2;
  if (!real_samps || size <= 0) {
    return;
  }
  {
    static bool logged = false;
    if (!logged) {
      logged = true;
      ::bmx::apple2::debug_log("sound: first packet real=%d size=%d pos=%d",
                               real_samps, size, (int)g_sound_shm_pos);
    }
  }
  if (g_sound_shm_addr == nullptr) {
    return;
  }
  if (size > SOUND_SHM_SAMP_SIZE) {
    size = SOUND_SHM_SAMP_SIZE;
  }
  for (int i = 0; i < size; i++) {
    int pos = (int)g_sound_shm_pos - size + i;
    while (pos < 0) {
      pos += SOUND_SHM_SAMP_SIZE;
    }
    pos %= SOUND_SHM_SAMP_SIZE;
    const word32 w = g_sound_shm_addr[pos];
    fifo_push((int16_t)(w & 0xffff), (int16_t)((w >> 16) & 0xffff));
  }
}

}  // extern "C"
