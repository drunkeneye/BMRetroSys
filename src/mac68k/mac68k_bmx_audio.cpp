//
// mac68k_bmx_audio.cpp
//
// Basilisk II audio backend for BMX bare-metal.
//
// Implements the platform side of third_party/basilisk2/include/audio.h:
// AudioInit/AudioExit/AudioReset/AudioInterrupt/audio_enter_stream/
// audio_exit_stream/audio_set_sample_rate-size-channels and the
// get/set mute/volume controls. Mirrors dummy/audio_dummy.cpp for the
// advertised format (44100 Hz, 16-bit, stereo, stereo|16bit component
// flags) and SDL/audio_sdl.cpp for the AudioInterrupt adat thunk
// (Execute68k(audio_data + adatGetSourceData)).
//
// Data flow: the guest Apple Mixer renders big-endian s16 frames into
// the scd_buffer described by adatStreamInfo; mac68k_audio_service()
// (called from the ~60 Hz emulator tick once that workstream wires it)
// runs AudioInterrupt() to fill adat, then drains the buffer: byteswap
// to s16LE, downmix stereo->mono (the Circle sink is mono in every BMX
// kernel), apply the master volume, and hand ≤2048-frame chunks to
// circle_sound_write(). When the guest is idle the tick still writes one
// tick's worth of silence so the sink stays paced to the audio clock,
// exactly like the SDL callback's silence path.
//
// NOTE: PrefsFindBool("nosound") has no provider in the BMX link yet
// (upstream prefs.cpp is not built), so sound defaults to on; the hook
// is marked TODO(prefs) below. Likewise the Makefile owner must add
// mac68k/mac68k_bmx_audio.o to the RASPI_MAC68K object list. No Circle
// C++ headers are included here (sysdeps.h uintptr clash); the sink is
// declared via third_party/common/circle.h inside extern "C", the same
// pattern the other kernels use.
//

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <vector>

#include "sysdeps.h"
#include "cpu_emulation.h"
#include "main.h"
#include "audio.h"
#include "audio_defs.h"

#include "mac68k_debug.h"

extern "C" {
#include "third_party/common/circle.h"
}

// Upstream audio.cpp owns the audio globals + AudioReset.

namespace {

// Only one format is advertised (matches dummy + kMac68kSoundRate, so no
// resampling is ever needed in the drain path).
const uint32 kRateFixed = 44100u << 16;
const int kFramesPerBlock = 4096;  // Guest-visible, SDL-identical.
const int kChunkFrames = 2048;     // Max frames per circle_sound_write.
const int kIdleFramesPerTick = 44100 / 60;  // Silence pacing when idle.

int s_rate_idx = 0;
int s_size_idx = 0;
int s_chan_idx = 0;
bool s_mute = false;
int s_volume_pct = 100;  // Master volume, percent (0..300, 100 = neutral).

bool s_logged_first_audio = false;
bool s_had_stream = false;
unsigned long s_underruns = 0;

int16_t s_mono[kChunkFrames];

inline int16_t swap_s16(int16_t v) {
  uint16_t u = (uint16_t)v;
  u = (uint16_t)((u >> 8) | (u << 8));
  return (int16_t)u;
}

void push_frames(const int16_t *mono, unsigned nframes) {
  if (mono == nullptr || nframes == 0) {
    return;
  }
  unsigned done = 0;
  while (done < nframes) {
    unsigned chunk = nframes - done;
    if (chunk > (unsigned)kChunkFrames) {
      chunk = (unsigned)kChunkFrames;
    }
    circle_sound_write(const_cast<int16_t *>(mono + done), (size_t)chunk);
    done += chunk;
  }
}

void push_silence(unsigned nframes) {
  static int16_t s_zero[kChunkFrames];
  memset(s_zero, 0, sizeof(s_zero));
  unsigned done = 0;
  while (done < nframes) {
    unsigned chunk = nframes - done;
    if (chunk > (unsigned)kChunkFrames) {
      chunk = (unsigned)kChunkFrames;
    }
    circle_sound_write(s_zero, (size_t)chunk);
    done += chunk;
  }
}

}  // namespace

// --- Glue for mac68k_sound.cpp (menu side) ------------------------------
// Declared extern there; defined here to avoid a new shared header.
void bmx_mac68k_audio_set_master_pct(int pct) {
  if (pct < 0) {
    pct = 0;
  }
  if (pct > 300) {
    pct = 300;
  }
  s_volume_pct = pct;
}

int bmx_mac68k_audio_get_master_pct(void) {
  return s_volume_pct;
}

void bmx_mac68k_audio_submit(const int16_t *frames, unsigned nframes) {
  if (frames == nullptr || nframes == 0 || !audio_open) {
    return;
  }
  // Stereo-interleaved s16 input -> mono, volume-scaled.
  unsigned done = 0;
  while (done < nframes) {
    unsigned chunk = nframes - done;
    if (chunk > (unsigned)kChunkFrames) {
      chunk = (unsigned)kChunkFrames;
    }
    const int vol = s_mute ? 0 : s_volume_pct;
    for (unsigned i = 0; i < chunk; i++) {
      int32_t mixed = ((int32_t)frames[(done + i) * 2] +
                       (int32_t)frames[(done + i) * 2 + 1]) / 2;
      mixed = (mixed * vol) / 100;
      if (mixed > 32767) {
        mixed = 32767;
      }
      if (mixed < -32768) {
        mixed = -32768;
      }
      s_mono[i] = (int16_t)mixed;
    }
    circle_sound_write(s_mono, (size_t)chunk);
    done += chunk;
  }
}

// --- Basilisk platform entry points --------------------------------------

void AudioInit(void) {
  using bmx::mac68k::debug_log;
  AudioStatus.sample_rate = kRateFixed;
  AudioStatus.sample_size = 16;
  AudioStatus.channels = 2;
  AudioStatus.mixer = 0;
  AudioStatus.num_sources = 0;
  audio_component_flags = cmpWantsRegisterMessage | kStereoOut | k16BitOut;

  audio_sample_rates.clear();
  audio_sample_sizes.clear();
  audio_channel_counts.clear();
  audio_sample_rates.push_back(kRateFixed);
  audio_sample_sizes.push_back(16);
  audio_channel_counts.push_back(2);
  audio_frames_per_block = kFramesPerBlock;

  // TODO(prefs): honor PrefsFindBool("nosound") once prefs.cpp is linked.
  const bool nosound = false;
  debug_log("bmx_audio: AudioInit: nosound pref=%s rate=44100 size=16 "
            "channels=2",
            nosound ? "true" : "false(unavailable, default on)");
  if (nosound) {
    audio_open = false;
    AudioAvailable = false;
    return;
  }

  // The Circle sink needs no open call (other kernels drain into it
  // directly); from the guest's point of view hardware is present.
  audio_open = true;
  AudioAvailable = true;
  debug_log("bmx_audio: AudioInit: open, frames_per_block=%d",
            audio_frames_per_block);
}

void AudioExit(void) {
  audio_open = false;
  AudioAvailable = false;
  bmx::mac68k::debug_log("bmx_audio: AudioExit done");
}

// AudioReset owned by upstream audio.cpp.

void audio_enter_stream(void) {
  bmx::mac68k::debug_log("bmx_audio: enter_stream");
}

void audio_exit_stream(void) {
  bmx::mac68k::debug_log("bmx_audio: exit_stream");
}

void AudioInterrupt(void) {
  if (!audio_open) {
    return;
  }
  if (audio_data == 0) {
    return;
  }
  // Get data from Apple Mixer (mirrors SDL/audio_sdl.cpp exactly).
  if (AudioStatus.mixer) {
    M68kRegisters r;
    memset(&r, 0, sizeof(r));
    r.a[0] = audio_data + adatStreamInfo;
    r.a[1] = AudioStatus.mixer;
    Execute68k(audio_data + adatGetSourceData, &r);
  } else {
    WriteMacInt32(audio_data + adatStreamInfo, 0);
  }
}

// 60 Hz tick helper: fills adat via AudioInterrupt() (same-thread nested
// Execute68k, safe because the emulator thread is the only 68k runner),
// then drains the mixer buffer into the Circle sink. Safe to call at
// ~60 Hz from the emulator thread; blocks to pace when the sink is full.
void mac68k_audio_service(void) {
  using bmx::mac68k::debug_log;
  if (!audio_open) {
    return;
  }
  AudioInterrupt();
  if (audio_data == 0) {
    push_silence(kIdleFramesPerTick);
    return;
  }
  const uint32 info = ReadMacInt32(audio_data + adatStreamInfo);
  if (info == 0) {
    if (s_had_stream) {
      s_had_stream = false;
      debug_log("bmx_audio: stream idle");
    }
    push_silence(kIdleFramesPerTick);
    return;
  }
  uint32 count = ReadMacInt32(info + scd_sampleCount);
  const uint32 buf = ReadMacInt32(info + scd_buffer);
  if (count == 0 || buf == 0) {
    s_underruns++;
    if (s_underruns == 1) {
      debug_log("bmx_audio: underrun (empty stream buffer)");
    }
    push_silence(kIdleFramesPerTick);
    return;
  }
  if (count > (uint32)kFramesPerBlock) {
    count = (uint32)kFramesPerBlock;
  }
  if (!s_had_stream) {
    s_had_stream = true;
    debug_log("bmx_audio: stream active: %lu frames channels=%lu",
              (unsigned long)count, (unsigned long)AudioStatus.channels);
  }
  if (!s_logged_first_audio) {
    s_logged_first_audio = true;
    debug_log("bmx_audio: first audio: %lu frames", (unsigned long)count);
  }

  // Mac samples are big-endian s16 ("twos"); the sink takes s16LE mono.
  const int16_t *src = reinterpret_cast<const int16_t *>(Mac2HostAddr(buf));
  const bool stereo = (AudioStatus.channels == 2);
  const int vol = s_mute ? 0 : s_volume_pct;
  uint32 done = 0;
  while (done < count) {
    uint32 chunk = count - done;
    if (chunk > (uint32)kChunkFrames) {
      chunk = (uint32)kChunkFrames;
    }
    for (uint32 i = 0; i < chunk; i++) {
      int32_t s;
      if (stereo) {
        const int32_t l = swap_s16(src[(done + i) * 2]);
        const int32_t r = swap_s16(src[(done + i) * 2 + 1]);
        s = (l + r) / 2;
      } else {
        s = swap_s16(src[done + i]);
      }
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

bool audio_set_sample_rate(int index) {
  if (index != 0) {
    return false;
  }
  s_rate_idx = index;
  return true;
}

bool audio_set_sample_size(int index) {
  if (index != 0) {
    return false;
  }
  s_size_idx = index;
  return true;
}

bool audio_set_channels(int index) {
  if (index != 0) {
    return false;
  }
  s_chan_idx = index;
  return true;
}

// Volume values carry left-channel volume in the upper 16 bits and right
// in the lower 16 bits; both are 8.8 fixed point, 0x0100 = maximum.
namespace {

uint32 pack_volume(int pct) {
  int v = pct;
  if (v < 0) {
    v = 0;
  }
  if (v > 100) {
    v = 100;  // Extra gain above 100 % is applied in the drain path.
  }
  const uint32 chan = (uint32)(v * 0x0100 / 100);
  return (chan << 16) | chan;
}

}  // namespace

bool audio_get_main_mute(void) {
  return s_mute;
}

uint32 audio_get_main_volume(void) {
  return pack_volume(s_volume_pct);
}

bool audio_get_speaker_mute(void) {
  return s_mute;
}

uint32 audio_get_speaker_volume(void) {
  return audio_get_main_volume();
}

void audio_set_main_mute(bool mute) {
  s_mute = mute;
}

void audio_set_main_volume(uint32 vol) {
  const uint32 avg = (((vol >> 16) & 0xffff) + (vol & 0xffff)) / 2;
  int pct = (int)(avg * 100 / 0x0100);
  if (pct > 300) {
    pct = 300;
  }
  s_volume_pct = pct;
}

void audio_set_speaker_mute(bool mute) {
  (void)mute;
}

void audio_set_speaker_volume(uint32 vol) {
  (void)vol;
}
