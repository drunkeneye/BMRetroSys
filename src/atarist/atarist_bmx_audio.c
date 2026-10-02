//
// atarist_bmx_audio.c
//
// BMX audio backend for the vendored Hatari core (replaces src/sdl/audio.c
// and src/retro/audio.c). Plain C: Hatari headers are C-only (sysdeps.h
// includes <string> under __cplusplus).
//
// Hatari's mixer (sound.c PSG + dmaSnd.c STE DMA) produces signed 16-bit
// stereo into the AudioMixBuffer ring (AudioMixBuffer_pos_write /
// nGeneratedSamples). Audio_Unlock() is Hatari's "samples ready" signal
// (called from Sound_Update, i.e. once per VBL plus STE-DMA HBL updates).
// We drain the ring here, downmix to mono, and hand it to the proven
// circle_sound_write() sink (44.1 kHz, same path as the Atari800 kernel).
// The sink blocks when full, which paces emulation naturally; Hatari-side
// overrun protection (Sound_BufferIndexNeedReset) is untouched.
//

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "main.h"
#include "configuration.h"
#include "audio.h"
#include "sound.h"
#include "circle.h"

// File+serial log sink (atarist_debug.cpp). No printf(): no UART-only output.
void atarist_log_line(const char *line);

// Hatari audio globals (declared in includes/audio.h, owned by backend).
bool bSoundWorking = false;
int SoundBufferSize = 1024;
int SdlAudioBufferSize = 0;
int pulse_swallowing_count = 0;

static bool s_playing = false;
// Master volume percent (owned by atarist_sound.cpp, menu-driven 0..300).
extern int atarist_volume_pct;
// Loudest single Unlock batch since last heartbeat (mixer output level).
// The drain zeroes nGeneratedSamples, so without this the heartbeat would
// always read 0 and silence would be indistinguishable from a dead mixer.
int atarist_audio_level_max = 0;

// Mono staging: one drain chunk (stereo frames -> mono samples).
// 2048 frames is ~46 ms at 44.1 kHz, well within sink fragment sizes.
#define kDrainFrames 2048
static int16_t s_mono[kDrainFrames];

static void drain_ring(void) {
  int available = nGeneratedSamples;
  if (available <= 0) {
    return;
  }
  if (available > AUDIOMIXBUFFER_SIZE) {
    available = AUDIOMIXBUFFER_SIZE;
  }
  int pos = AudioMixBuffer_pos_read;
  while (available > 0) {
    int chunk = available > kDrainFrames ? kDrainFrames : available;
    int vol = atarist_volume_pct;
    if (vol < 0) {
      vol = 0;
    }
    if (vol > 300) {
      vol = 300;
    }
    for (int i = 0; i < chunk; i++) {
      int idx = (pos + i) & AUDIOMIXBUFFER_SIZE_MASK;
      int32_t mixed = (int32_t)AudioMixBuffer[idx][0] +
                      (int32_t)AudioMixBuffer[idx][1];
      mixed = (mixed * vol) / 200;
      if (mixed > 32767) {
        mixed = 32767;
      }
      if (mixed < -32768) {
        mixed = -32768;
      }
      s_mono[i] = (int16_t)mixed;
    }
    circle_sound_write(s_mono, (size_t)chunk);
    pos = (pos + chunk) & AUDIOMIXBUFFER_SIZE_MASK;
    available -= chunk;
  }
  AudioMixBuffer_pos_read = pos;
  nGeneratedSamples = 0;
}

void Audio_Init(void) {
  atarist_log_line("AtariST: Audio_Init, 44.1kHz mono sink");
  bSoundWorking = true;
  Audio_EnableAudio(true);
}

void Audio_UnInit(void) {
  Audio_EnableAudio(false);
  bSoundWorking = false;
}

void Audio_Lock(void) {
  // No-op: single emulator thread, no SDL callback to serialize with.
}

void Audio_Unlock(void) {
  // "Samples ready" signal from Sound_Update: drain the Hatari ring into
  // the Circle sink when playing, otherwise just advance (drop) so the
  // mixer never stalls.
  if (nGeneratedSamples > atarist_audio_level_max) {
    atarist_audio_level_max = nGeneratedSamples;
  }
  if (s_playing && bSoundWorking && nGeneratedSamples > 0) {
    drain_ring();
  } else {
    AudioMixBuffer_pos_read = (AudioMixBuffer_pos_read + nGeneratedSamples) &
                              AUDIOMIXBUFFER_SIZE_MASK;
    nGeneratedSamples = 0;
  }
}

void Audio_FreeSoundBuffer(void) {
}

void Audio_EnableAudio(bool bEnable) {
  s_playing = bEnable ? true : false;
}
