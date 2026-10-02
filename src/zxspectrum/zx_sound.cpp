//
// zx_sound.cpp
//
// See zx_sound.h. Consumption is per frame (the core appends ~312 stereo
// samples per 50 Hz frame); the 5-frame envio_audio path is never used.
//

#include "zx_sound.h"
#include "zx_debug.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern "C" {
#include "third_party/common/circle.h"
}

extern "C" {
#include "zx_zesarux.h"
}

namespace bmx {
namespace zxspectrum {

namespace {

// Sink rate shared with the Atari/DOS kernels (src/defs.h SAMPLE_RATE).
const int kSinkRate = 44100;
const int kCoreRate = 15600;

// Resample staging: one frame is 312 core samples -> 882 sink frames.
int16_t s_out[1024];
unsigned long s_frames_total = 0;
unsigned long s_drop_frames = 0;   // drain had audio but sink had no space
unsigned long s_starve_calls = 0;  // drain found nothing produced
int s_last_in_samples = 0;         // core samples in the last drain
unsigned s_calls = 0;
// Output range over the current heartbeat window (reset per window).
int s_out_min = 32767;
int s_out_max = -32768;

// Linear-interpolating 15600 -> 44100 mono resampler over signed 8-bit
// stereo input (L/R averaged) with a running one-pole DC blocker.
// NOTE: plain `char` is UNSIGNED on ARM C++, while the ZEsarUX core (built
// as C with -fsigned-char) emits SIGNED bytes. Using `signed char` here is
// mandatory: with unsigned reads, an idle beeper level of -48 becomes +208
// and the whole stream turns into loud wrapped garbage.
//
// DC is blocked with a slow high-pass (tau ~512 input samples ~= 33 ms,
// ~5 Hz corner), NOT a per-frame mean: subtracting each frame's own mean
// forces every 20 ms block to zero mean, so loud low-frequency content
// steps at block boundaries (a 50 Hz flutter on music). The running filter
// kills the parked-beeper DC (the boot drone) and slow drift while leaving
// frame-to-frame continuity intact. Returns sink frames written to out.
//
// The low-pass estimate is kept in Q9 (stored x512): a plain integer
// update `lp += (mix-lp)>>9` stalls with up to +511 residual (a positive
// error below 512 shifts to 0 and never converges; observed as a constant
// 511 output on silence), while the Q9 form converges to within 1 LSB.
static long s_dc_lp = 0;  // Q9 low-pass estimate of the (L+R)*128 mix
static int s_filt[2048];  // DC-blocked input mix, in input order
int resample_frame(const signed char *in, int in_samples,
                   int16_t *out, int out_capacity) {
  if (in == nullptr || out == nullptr || in_samples <= 0 ||
      out_capacity <= 0) {
    return 0;
  }
  if (in_samples > (int)(sizeof(s_filt) / sizeof(s_filt[0]))) {
    in_samples = (int)(sizeof(s_filt) / sizeof(s_filt[0]));
  }
  // Filter in input order first: the interpolator below visits inputs
  // out of order (repeats/skips), so the filter state must be advanced
  // linearly here, never per output sample.
  for (int i = 0; i < in_samples; i++) {
    // Average stereo pair, scale 8-bit signed to 16-bit. A parked beeper
    // sits on the output as a large constant without the blocker below
    // (real Spectrum audio is AC; some amplifiers turn the DC into
    // motorboating).
    const int mix = ((int)in[i * 2] + (int)in[i * 2 + 1]) * 128;
    s_dc_lp += (long)mix - (s_dc_lp >> 9);
    s_filt[i] = mix - (int)(s_dc_lp >> 9);
  }
  // Fixed-point step: in_samples/out_samples per output frame, where
  // out_samples = in_samples * 44100/15600.
  const int out_samples =
      (int)((long long)in_samples * kSinkRate / kCoreRate);
  int n = out_samples < out_capacity ? out_samples : out_capacity;
  long long pos = 0;  // 16.16 position in input samples
  const long long step = ((long long)in_samples << 16) / (n > 0 ? n : 1);
  for (int i = 0; i < n; i++) {
    const int idx = (int)(pos >> 16);
    const int frac = (int)(pos & 0xffff);
    const int i0 = idx < in_samples ? idx : in_samples - 1;
    const int i1 = (idx + 1) < in_samples ? idx + 1 : in_samples - 1;
    const int s0 = s_filt[i0];
    const int s1 = s_filt[i1];
    int v = s0 + (int)(((long long)(s1 - s0) * frac) >> 16);
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    out[i] = (int16_t)v;
    pos += step;
  }
  return n;
}

// ---- Temporary audio diagnostic (DIAG): capture produced audio to
// SYS:/ZXAUDIO.RAW for off-device waveform analysis. 30 s from each
// re-arm (tape attach / autostart-warp end): trap loading is silent, so
// a short window only ever catches boot silence; 30 s reaches game audio.
// Removed once the device audio issue is diagnosed.
namespace {
FILE *s_dump = nullptr;
unsigned long s_dump_frames = 0;
const unsigned long kDumpMaxFrames = 30 * 44100;
// RAM staging for the dump: a per-frame FATFS fwrite (~1.7 KB) costs
// milliseconds and sits after the frame pace point, so it alone can push
// borderline frames over the 20 ms budget (every such frame then waits out
// a whole extra tick slot). Buffer ~37 frames and flush rarely instead;
// one ~10 ms hitch every ~50 frames is inaudible (production is unaffected,
// only the trace timing), unlike a hitch on every frame.
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

void sound_dump_write(const int16_t *data, int n) {
  if (s_dump_frames >= kDumpMaxFrames || data == nullptr || n <= 0) {
    return;
  }
  if (s_dump == nullptr) {
    s_dump = fopen("SYS:/ZXAUDIO.RAW", "wb");
    if (s_dump == nullptr) {
      s_dump_frames = kDumpMaxFrames;  // don't retry
      debug_log("DIAG audio dump: fopen SYS:/ZXAUDIO.RAW FAILED");
      return;
    }
    s_dump_used = 0;
    s_dump_calls = 0;
    debug_log("DIAG audio dump start");
  }
  const int total = n;
  while (n > 0) {
    unsigned room =
        (unsigned)(sizeof(s_dump_buf) / sizeof(s_dump_buf[0])) - s_dump_used;
    unsigned take = (unsigned)n < room ? (unsigned)n : room;
    memcpy(&s_dump_buf[s_dump_used], data, (size_t)take * sizeof(int16_t));
    s_dump_used += take;
    data += take;
    n -= (int)take;
    if (s_dump_used ==
        (unsigned)(sizeof(s_dump_buf) / sizeof(s_dump_buf[0]))) {
      sound_dump_flush();
    }
  }
  s_dump_frames += (unsigned long)total;
  // Flush the partial buffer about once a second as well: a mid-run pull
  // of the file then misses at most ~1 s of tail instead of ~37 frames
  // sitting in RAM. Costs one FATFS hitch per 50 frames, not per frame.
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
  // Consume-and-clear: the desktop envio_audio path (which resets the
  // index after each 5-frame send) never runs on BMX, so reset here or
  // the index saturates at the buffer limit after ~5 frames.
  int produced = audio_buffer_indice;
  if (produced <= 0) {
    s_starve_calls++;
    return;
  }
  // indice counts bytes; stereo pairs.
  if (produced & 1) {
    produced--;
  }
  const int in_samples = produced / 2;
  audio_buffer_indice = 0;
  if (in_samples <= 0) {
    s_starve_calls++;
    return;
  }
  s_last_in_samples = in_samples;
  // Copy out first: single-threaded, but keeps the logic obvious.
  // signed char: see resample_frame note (core emits signed bytes).
  static signed char s_copy[4096];
  int copy_bytes = produced;
  if (copy_bytes > (int)sizeof(s_copy)) {
    copy_bytes = (int)sizeof(s_copy);
  }
  memcpy(s_copy, audio_buffer, (size_t)copy_bytes);
  const int n = resample_frame(s_copy, copy_bytes / 2, s_out,
                               (int)(sizeof(s_out) / sizeof(s_out[0])));
  if (n <= 0) {
    return;
  }
  // DIAG waveform capture (see sound_dump_restart triggers): what we hand
  // the sink, whether or not it fits this frame.
  sound_dump_write(s_out, n);
  for (int i = 0; i < n; i++) {
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
    // Sink full while we hold a full frame: overrun, this frame is lost.
    s_drop_frames++;
    return;
  }
  // NOTE: the sink returns 0 unconditionally (see ViceSound::AddChunk), so
  // no accepted/dropped accounting is possible here; count attempted.
  circle_sound_write(s_out, (size_t)want);
  s_frames_total += (unsigned long)want;
}

// Safety net: anything calling the desktop flush path lands here instead
// of the null driver (envio_audio is never invoked on BMX).
void flush_hook(char *buffer) {
  (void)buffer;
  drain_produced();
}

}  // namespace

// Re-arm the DIAG waveform capture (next 30 s of produced audio go to
// SYS:/ZXAUDIO.RAW, truncating any previous capture).
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
  // Replicates start.c main_init_audio for the null driver.
  audio_buffer_switch.v = 0;
  audio_playing.v = 1;
  audio_buffer_one = audio_buffer_one_assigned;
  audio_buffer_two = audio_buffer_two_assigned;
  set_active_audio_buffer();
  audio_empty_buffer();
  set_audiodriver_null();
  audio_init();
  audio_send_frame = flush_hook;
  s_dc_lp = 0;  // converge the DC blocker from silence (kills boot drone)
  debug_log("sound_init: null driver + BMX drain (15600->%dHz)", kSinkRate);
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

}  // namespace zxspectrum
}  // namespace bmx
