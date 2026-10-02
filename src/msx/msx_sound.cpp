//
// msx_sound.cpp
//
// See msx_sound.h. The core appends one stereo pair per scanline (312 per
// 50 Hz frame, 15600 Hz); the drain below resamples to 44100 Hz.
//
// Same design as the CPC kernel:
// - signed char input (ARM C++ plain char is unsigned; the core emits
//   signed bytes; misreading wraps silence into garbage).
// - DC is blocked with a running one-pole Q9 high-pass per channel, never
//   a per-frame mean (which steps at 20 ms boundaries = 50 Hz flutter).
// - the resampler keeps fractional phase across drains so the long-term
//   rate is exactly 44100 Hz (a per-drain rounding would drift into
//   chronic under/overrun).
//

#include "msx_sound.h"
#include "msx_debug.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern "C" {
#include "third_party/common/circle.h"
}

extern "C" {
#include "audio/audio.h"
// AY PSG register file (vendored ay38912.c) for per-window volume tracking.
extern unsigned char ay_3_8912_registros[][16];
}

namespace bmx {
namespace msx {

namespace {

// Core production rate (312 pairs per 50 Hz frame) and sink rate.
const int kCoreRate = 15600;
const int kSinkRate = 44100;
// Fixed input->output step in 16.16 (input samples per output sample).
const long long kStep = ((long long)kCoreRate << 16) / kSinkRate;
// Effective step, trimmed by the sink-fill servo below. Starts exact.
long long s_step_eff = kStep;
// Sink capacity: FRAG_SIZE 256 * NUM_FRAGS 16 (src/vicesound.h). The servo
// and the sndspace heartbeat readout use this.
const int kSinkTotal = 256 * 16;
// Minimum sink free-space seen over the heartbeat window: proves sink
// starvation that drop/starve counters cannot see (they only track
// producer-empty and sink-full, never sink-empty).
int s_space_min = kSinkTotal;

int16_t s_out[6144 * 2];
// (6144 frames of headroom: the rate servo speeds +5% => ~927/frame for a
// nominal 312-pair drain, and catch-up drains of up to 2048 pairs need
// ~6080. Truncating at 882 drove s_pos negative without bound, see below.)
unsigned long s_frames_total = 0;
unsigned long s_drop_frames = 0;
unsigned long s_starve_calls = 0;
int s_last_in_samples = 0;
unsigned s_calls = 0;
int s_out_min = 32767;
int s_out_max = -32768;
// Raw producer input range over the heartbeat window (signed 8-bit).
int s_in_min = 127;
int s_in_max = -128;
// Max AY channel-A volume nibble programmed over the window (R8 & 15)
// plus whether the envelope bit (R8 bit 4) ever appeared: tells a quiet
// BIOS program apart from a mangled register write.
int s_ay8_max = 0;
int s_ay8_env = 0;

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
      // One-pole DC blocker (subtract slow moving average). Tau is 128
      // input samples (~8 ms): long enough to pass the lowest PLAY tones
      // untouched, short enough that note/click attacks don't swell into
      // a 30 ms "wah" (the old >>9 tau smeared every transient).
      const int mix = (int)in[i * 2 + ch] * 256;
      s_dc_lp[ch] += (long)mix - (s_dc_lp[ch] >> 7);
      s_filt[ch][i] = mix - (int)(s_dc_lp[ch] >> 7);
    }
  }
  int n = 0;
  const long long limit = (long long)in_pairs << 16;
  while (s_pos < limit && n < out_capacity) {
    // s_pos can lag up to one input behind (servo phase debt after a
    // truncated catch-up drain). A negative index would read before
    // s_filt and rail the output full-scale; pin to the first input.
    int idx = (int)(s_pos >> 16);
    if (idx < 0) {
      idx = 0;
    }
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
    s_pos += s_step_eff;
  }
  s_pos -= limit;
  // Bound phase debt to one input sample: deeper debt is dropped input,
  // not debt (prevents runaway after truncated catch-up drains).
  if (s_pos < -s_step_eff) {
    s_pos = -s_step_eff;
  }
  return n;
}

// ---- DIAG waveform capture (buffered; per-frame FATFS writes cost ms).
// One continuous MONO recording from boot, split into 5 s chunk files
// (~441 KB each: SYS:/MSXA0001.RAW, ...). Small closed files transfer
// intact through size-limited copy paths and stay valid if the card is
// pulled (each chunk's directory entry is final on close). Recording
// stops LOUDLY on write error (log), never silently.
namespace {
FILE *s_dump = nullptr;
unsigned long s_dump_frames = 0;
// Frames per chunk file: 5 s mono.
const unsigned long kDumpChunkFrames = 5 * 44100;
unsigned long s_dump_chunk = 0;
unsigned long s_dump_chunk_frames = 0;
int16_t s_dump_buf[32768];
unsigned s_dump_used = 0;
unsigned s_dump_calls = 0;
bool s_dump_dead = false;
unsigned long s_dump_errs = 0;
}  // namespace

void sound_dump_flush(void) {
  if (s_dump == nullptr || s_dump_used == 0) {
    return;
  }
  if (s_dump_dead) {
    s_dump_used = 0;
    return;
  }
  const size_t want = (size_t)s_dump_used;
  const size_t wrote =
      fwrite(s_dump_buf, sizeof(int16_t), want, s_dump);
  s_dump_used = 0;
  if (wrote != want) {
    s_dump_errs++;
    if (s_dump != nullptr) {
      fclose(s_dump);
      s_dump = nullptr;
    }
    if (!s_dump_dead) {
      s_dump_dead = true;
      debug_log("DIAG audio dump WRITE FAILED (%lu) - disabled, "
                "check free space on SYS",
                s_dump_errs);
    }
  }
}

void sound_dump_open_chunk(void) {
  char name[64];
  snprintf(name, sizeof(name), "SYS:/MSXA%04lu.RAW", s_dump_chunk);
  s_dump = fopen(name, "wb");
  s_dump_chunk_frames = 0;
  if (s_dump == nullptr) {
    debug_log("DIAG audio dump: fopen %s FAILED", name);
    return;
  }
  s_dump_used = 0;
  s_dump_calls = 0;
  debug_log("DIAG audio dump chunk %lu start", s_dump_chunk);
}

void sound_dump_write(const int16_t *data, int nframes) {
  if (data == nullptr || nframes <= 0 || s_dump_dead) {
    return;
  }
  if (s_dump == nullptr) {
    sound_dump_open_chunk();
    if (s_dump == nullptr) {
      return;
    }
  }
  // Record left channel only (AY output is mono; halves file size).
  unsigned frames = (unsigned)nframes;
  const int16_t *stereo = data;
  while (frames > 0) {
    unsigned room =
        (unsigned)(sizeof(s_dump_buf) / sizeof(s_dump_buf[0])) - s_dump_used;
    unsigned take = frames < room ? frames : room;
    for (unsigned i = 0; i < take; i++) {
      s_dump_buf[s_dump_used++] = stereo[i * 2];
    }
    stereo += take * 2;
    frames -= take;
    if (s_dump_dead) {
      return;
    }
    if (s_dump_used ==
        (unsigned)(sizeof(s_dump_buf) / sizeof(s_dump_buf[0]))) {
      sound_dump_flush();
    }
  }
  s_dump_frames += (unsigned long)nframes;
  s_dump_chunk_frames += (unsigned long)nframes;
  if (++s_dump_calls >= 50) {
    s_dump_calls = 0;
    sound_dump_flush();
    if (s_dump != nullptr) {
      fflush(s_dump);
    }
  }
  // Chunk boundary: close the finished 5 s file (directory entry final,
  // survives card pulls) and continue in the next one. Continuous audio,
  // no gaps: the next write reopens first.
  if (!s_dump_dead && s_dump_chunk_frames >= kDumpChunkFrames) {
    sound_dump_flush();
    if (s_dump != nullptr) {
      fflush(s_dump);
      fclose(s_dump);
      s_dump = nullptr;
    }
    s_dump_chunk++;
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
  // No-drop bound: the core buffer holds at most AUDIO_BUFFER_SIZE*2 =
  // 3120 bytes (312*5 pairs stereo), s_copy fits 4096, so the cap below
  // never binds and audio_buffer_indice is only cleared after every byte
  // was copied. s_filt fits 2048 pairs (1560 max here); s_out fits 6144
  // frames (1560 pairs need ~4542 at max servo rate). Nothing truncates.
  static signed char s_copy[4096];
  int copy_bytes = produced;
  if (copy_bytes > (int)sizeof(s_copy)) {
    copy_bytes = (int)sizeof(s_copy);
  }
  memcpy(s_copy, audio_buffer, (size_t)copy_bytes);
  const int pairs = copy_bytes / 2;
  // AY program census for this drain (cheap, runs at most ~50x/s).
  {
    const int r8 = (int)ay_3_8912_registros[0][8];
    if ((r8 & 15) > s_ay8_max) {
      s_ay8_max = r8 & 15;
    }
    if (r8 & 16) {
      s_ay8_env = 1;
    }
  }
  for (int i = 0; i < copy_bytes; i++) {
    const int v = (int)s_copy[i];
    if (v < s_in_min) {
      s_in_min = v;
    }
    if (v > s_in_max) {
      s_in_max = v;
    }
  }
  // One drain resamples to at most ceil(pairs * 44100/15600 * 1.05)
  // frames (servo headroom); s_out fits 6144 (a full 2048-pair s_copy
  // drain needs ~6080). Persistent phase keeps the long-term rate exact.
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
  if (space > kSinkTotal) {
    space = kSinkTotal;
  }
  if (space < s_space_min) {
    s_space_min = space;
  }
  // Clock-domain servo: the core runs at realfps<50 (log showed 48.44),
  // so AY production (~15.1 kHz) trails the 44.1 kHz sink and the DMA runs
  // dry a few percent of the time -> choppy/ringy tones at the right
  // pitch. err>0 means sink dry -> shrink the step (more output per
  // input). Slew is ~0.0015%/drain, authority clamped to +/-3%, so locked
  // pitch error is inaudible and correction is a slow glide, not vibrato.
  const long err = (long)(kSinkTotal / 2) - (long)(kSinkTotal - space);
  // Very slow integral (4x calmer than the first attempt): the fill target
  // is only a drift integrator, and any hunting shows up directly as
  // pitch wobble on sustained PLAY tones. Converges ~1% in a few seconds.
  s_step_eff -= (kStep * err) / ((long long)kSinkTotal * 4096LL);
  const long long step_lo = kStep * 97 / 100;
  const long long step_hi = kStep * 103 / 100;
  if (s_step_eff < step_lo) {
    s_step_eff = step_lo;
  }
  if (s_step_eff > step_hi) {
    s_step_eff = step_hi;
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
  s_dump_chunk = 0;
  s_dump_chunk_frames = 0;
  s_dump_dead = false;
  s_dump_errs = 0;
}

void sound_init(void) {
  // TEMP DIAG drain self-test: 4 drains of synthetic 440 Hz square
  // (+-24 chars stereo, 312 pairs each) straight through resample_drain.
  // Working drain gives min~-6144, max~+6144 with dozens of flips.
  {
    static signed char t_in[312 * 2];
    static int16_t t_out[882 * 2];
    for (int i = 0; i < 312; i++) {
      // 440 Hz at 15600 in-rate: period ~35.45 inputs.
      const int ph = i % 35;
      const signed char v = ph < 18 ? 24 : -24;
      t_in[i * 2] = v;
      t_in[i * 2 + 1] = v;
    }
    int mn = 32767, mx = -32768, flips = 0, last = 0, total = 0;
    for (int d = 0; d < 4; d++) {
      const int n = resample_drain(t_in, 312, t_out, 882);
      for (int i = 0; i < n * 2; i++) {
        const int v = t_out[i];
        if (v < mn) {
          mn = v;
        }
        if (v > mx) {
          mx = v;
        }
        if (total > 0 && ((last < 0) != (v < 0))) {
          flips++;
        }
        last = v;
        total++;
      }
    }
    debug_log("SNDTEST drain min=%d max=%d flips=%d n=%d", mn, mx, flips,
              total);
  }
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
int sound_in_min(void) { return s_in_min; }
int sound_in_max(void) { return s_in_max; }
unsigned long sound_drop_frames(void) { return s_drop_frames; }
unsigned long sound_starve_calls(void) { return s_starve_calls; }
int sound_last_in_samples(void) { return s_last_in_samples; }
int sound_sink_min_space(void) { return s_space_min; }
int sound_ay8_max(void) { return s_ay8_max; }
int sound_ay8_env(void) { return s_ay8_env; }
int sound_out_min(void) { return s_out_min; }
int sound_out_max(void) { return s_out_max; }
void sound_window_reset(void) {
  s_out_min = 32767;
  s_out_max = -32768;
  s_in_min = 127;
  s_in_max = -128;
  s_space_min = kSinkTotal;
  s_ay8_max = 0;
  s_ay8_env = 0;
}

}  // namespace msx
}  // namespace bmx
