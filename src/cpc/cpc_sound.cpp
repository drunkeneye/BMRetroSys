//
// cpc_sound.cpp
//
// See cpc_sound.h. The core appends one stereo pair per scanline (312 per
// 50 Hz frame, ~15625 Hz); the drain below resamples to 44100 Hz.
//
// Two lessons from the ZX kernel are built in from the start:
// - signed char input (ARM C++ plain char is unsigned; the core emits
//   signed bytes; misreading wraps silence into garbage).
// - DC is blocked with a running one-pole Q9 high-pass per channel, never
//   a per-frame mean (which steps at 20 ms boundaries = 50 Hz flutter).
// - the resampler keeps fractional phase across drains so the long-term
//   rate is exactly 44100 Hz (a per-drain rounding would drift into
//   chronic under/overrun).
//

#include "cpc_sound.h"
#include "cpc_debug.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern "C" {
#include "third_party/common/circle.h"
}

extern "C" {
#include "audio/audio.h"
}

namespace bmx {
namespace cpc {

namespace {

// Core production rate (312 pairs per 19.968 ms frame) and sink rate.
const int kCoreRate = 15625;
const int kSinkRate = 44100;
// Fixed input->output step in 16.16 (input samples per output sample).
const long long kStep = ((long long)kCoreRate << 16) / kSinkRate;

int16_t s_out[882 * 2];
unsigned long s_frames_total = 0;
unsigned long s_drop_frames = 0;
unsigned long s_starve_calls = 0;
int s_last_in_samples = 0;
unsigned s_calls = 0;
int s_out_min = 32767;
int s_out_max = -32768;

// Persistent resampler phase (input-sample position, 16.16).
long long s_pos = 0;
// Per-channel DC blocker state (Q9).
long s_dc_lp[2] = {0, 0};

int resample_drain(const signed char *in, int in_pairs, int16_t *out,
                   int out_capacity) {
  if (in == nullptr || out == nullptr || in_pairs <= 0 ||
      out_capacity <= 0) {
    return 0;
  }
  static int s_filt[2][2048];
  if (in_pairs > 2048) {
    in_pairs = 2048;
  }
  for (int ch = 0; ch < 2; ch++) {
    for (int i = 0; i < in_pairs; i++) {
      const int mix = (int)in[i * 2 + ch] * 256;
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
  s_pos -= limit;
  return n;
}

// ---- DIAG waveform capture (buffered; per-frame FATFS writes cost ms).
namespace {
FILE *s_dump = nullptr;
unsigned long s_dump_frames = 0;
const unsigned long kDumpMaxFrames = 30 * 44100;
int16_t s_dump_buf[32768];
unsigned s_dump_used = 0;
unsigned s_dump_calls = 0;
}  // namespace

void sound_dump_flush(void) {
  if (s_dump == nullptr || s_dump_used == 0) {
    return;
  }
  fwrite(s_dump_buf, sizeof(int16_t), (size_t)s_dump_used, s_dump);
  s_dump_used = 0;
}

void sound_dump_write(const int16_t *data, int nframes) {
  if (s_dump_frames >= kDumpMaxFrames || data == nullptr || nframes <= 0) {
    return;
  }
  if (s_dump == nullptr) {
    s_dump = fopen("SYS:/CPCAUDIO.RAW", "wb");
    if (s_dump == nullptr) {
      s_dump_frames = kDumpMaxFrames;
      debug_log("DIAG audio dump: fopen SYS:/CPCAUDIO.RAW FAILED");
      return;
    }
    s_dump_used = 0;
    s_dump_calls = 0;
    debug_log("DIAG audio dump start");
  }
  int words = nframes * 2;
  while (words > 0) {
    unsigned room =
        (unsigned)(sizeof(s_dump_buf) / sizeof(s_dump_buf[0])) - s_dump_used;
    unsigned take = (unsigned)words < room ? (unsigned)words : room;
    memcpy(&s_dump_buf[s_dump_used], data, (size_t)take * sizeof(int16_t));
    s_dump_used += take;
    data += take;
    words -= (int)take;
    if (s_dump_used ==
        (unsigned)(sizeof(s_dump_buf) / sizeof(s_dump_buf[0]))) {
      sound_dump_flush();
    }
  }
  s_dump_frames += (unsigned long)nframes;
  if (++s_dump_calls >= 50) {
    s_dump_calls = 0;
    sound_dump_flush();
    fflush(s_dump);
  }
  if (s_dump_frames >= kDumpMaxFrames) {
    sound_dump_flush();
    fflush(s_dump);
    fclose(s_dump);
    s_dump = nullptr;
    debug_log("DIAG audio dump done (%lu frames)", s_dump_frames);
  }
}

void drain_produced(void) {
  if (audio_buffer == nullptr) {
    return;
  }
  int produced = audio_buffer_indice;
  if (produced <= 0) {
    s_starve_calls++;
    return;
  }
  if (produced & 1) {
    produced--;
  }
  const int in_pairs = produced / 2;
  audio_buffer_indice = 0;
  if (in_pairs <= 0) {
    s_starve_calls++;
    return;
  }
  s_last_in_samples = in_pairs;
  static signed char s_copy[4096];
  int copy_bytes = produced;
  if (copy_bytes > (int)sizeof(s_copy)) {
    copy_bytes = (int)sizeof(s_copy);
  }
  memcpy(s_copy, audio_buffer, (size_t)copy_bytes);
  const int pairs = copy_bytes / 2;
  // One drain resamples to at most ceil(pairs * 44100/15625) frames;
  // s_out fits 882 (312 pairs -> ~881). Persistent phase keeps the
  // long-term rate exact.
  const int n =
      resample_drain(s_copy, pairs, s_out, (int)(sizeof(s_out) / sizeof(s_out[0])) / 2);
  if (n <= 0) {
    return;
  }
  sound_dump_write(s_out, n);
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

void flush_hook(char *buffer) {
  (void)buffer;
  drain_produced();
}

}  // namespace

void sound_dump_restart(void) {
  if (s_dump != nullptr) {
    sound_dump_flush();
    fflush(s_dump);
    fclose(s_dump);
    s_dump = nullptr;
  }
  s_dump_used = 0;
  s_dump_calls = 0;
  s_dump_frames = 0;
}

void sound_init(void) {
  audio_buffer_switch.v = 0;
  audio_playing.v = 1;
  audio_buffer_one = audio_buffer_one_assigned;
  audio_buffer_two = audio_buffer_two_assigned;
  set_active_audio_buffer();
  audio_empty_buffer();
  set_audiodriver_null();
  audio_init();
  audio_send_frame = flush_hook;
  s_pos = 0;
  s_dc_lp[0] = s_dc_lp[1] = 0;
  debug_log("sound_init: null driver + BMX drain (15625->%dHz)", kSinkRate);
}

void sound_play(void) {
  s_calls++;
  drain_produced();
  if (s_calls == 1) {
    debug_log("sound_play: first drain, indice=%d", audio_buffer_indice);
  }
}

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

}  // namespace cpc
}  // namespace bmx
