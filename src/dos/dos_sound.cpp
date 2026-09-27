//
// dos_sound.cpp
//
// DOSBox-X audio output for BMX bare-metal.
//
// Signal path:
//   guest (SB DSP / OPL / PC speaker)
//     -> DOSBox-X mixer (mixes per emulated ms via TIMER tick into work buf)
//     -> MIXER_BMX_Pull (vendored mixer.cpp, __circle__ export: runs the REAL
//        SDL callback body, so underflow/prebuffer/mute semantics are
//        bit-identical to desktop DOSBox-X)
//     -> stereo-to-mono downmix
//     -> circle_sound_write (same PCM sink the C64/Atari kernels use)
//
// sound_play() runs once per frame-loop iteration. Pull size is derived
// from elapsed wall-clock time so the drain stays locked to real time:
//   - emulation slower than real time -> mixer underproduces -> callback
//     pads with the last DAC value (brief hold, no drift)
//   - emulation faster -> mixer overproduces -> callback's drop logic trims
// Either way the work buffer can't grow without bound.
//
// While the BMX menu is open the guest (and mixer) stall, so nothing is
// pulled and the sink drains into silence. Correct: paused means quiet.
//

#include "dos_sound.h"
#include "dos_debug.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// Live DOSBox-X section access for the BMX sound menu (control.h/setup.h
// need the vendored include roots; see Makefile dos_sound.o rule).
#include "control.h"
#include "setup.h"

extern "C" {
#include "third_party/common/circle.h"
uint32_t bmx_host_ticks_ms(void);
}

// Vendored mixer export (hardware/mixer.cpp, __circle__-gated). The TU uses
// Bitu, which is Bit64u == `unsigned long long` (config.h); declaring the
// same type here keeps mangling identical (`unsigned long` would mangle
// differently despite also being 64-bit LP64). (circle_sound_* come from
// circle.h above; do NOT redeclare them here — linkage mismatch.)
extern unsigned long long MIXER_BMX_Pull(int16_t *stereo_out,
                                         unsigned long long frames);
extern unsigned long long MIXER_BMX_Rate(void);

namespace bmx {
namespace dos {

namespace {

// Max stereo frames per pull: 100ms @ 44.1kHz. One frame-loop iteration is
// ~50ms; this cap absorbs jitter without ever overflowing the 16x256-frame
// circle buffer. Static BSS: 4410 * 2ch * 2B = ~17KB, fine.
const unsigned long long kMaxPullFrames = 4410;
int16_t s_stereo[kMaxPullFrames * 2];
int16_t s_mono[kMaxPullFrames];

unsigned long s_calls = 0;
unsigned long s_pulls = 0;
unsigned long long s_frames_total = 0;
unsigned long long s_dropped_frames = 0;
unsigned int s_peak = 0;
uint32_t s_last_ms = 0;

}  // namespace

void sound_init(void) {
  s_last_ms = bmx_host_ticks_ms();
  debug_log("sound_init: BMX drain ready (sink=%dHz, pull<=%llu frames)",
            kDosboxxSoundRate, kMaxPullFrames);
}

unsigned long long sound_frames_total(void) {
  return s_frames_total;
}

void sound_play(void) {
  s_calls++;
  const uint32_t now_ms = bmx_host_ticks_ms();
  uint32_t elapsed = now_ms - s_last_ms;
  if (s_calls == 1) {
    // Unconditional: proves the pump reaches the drain (the 09:44 kernel
    // never got here — sound_play sat behind the parked frame loop).
    debug_log("sound_play: first call, mixer rate=%lluHz (expect %d)",
              MIXER_BMX_Rate(), kDosboxxSoundRate);
  }
  if (DOS_RUNTIME_TRACE && (s_calls % 600) == 0) {
    // Heartbeat regardless of pulls: calls advance even when gated.
    debug_log("sound_play: calls=%lu pulls=%lu frames=%llu dropped=%llu "
              "peak=%u space=%d",
              s_calls, s_pulls, s_frames_total, s_dropped_frames, s_peak,
              circle_sound_bufferspace());
    s_peak = 0;
  }
  // Gate: below ~10ms is pump noise (GFX_Events fires on every idle slice).
  // s_last_ms only advances on a real pull, so gated time accumulates into
  // the next pull instead of being lost.
  if (elapsed < 10) {
    return;
  }
  s_last_ms = now_ms;
  if (elapsed > 100) {
    // Stalled (menu was open, mixer didn't advance either): don't overpull,
    // the callback pads the gap with the last DAC value.
    elapsed = 100;
  }
  unsigned long long want =
      ((unsigned long long)elapsed * (unsigned long long)kDosboxxSoundRate) /
      1000ull;
  if (want > kMaxPullFrames) {
    want = kMaxPullFrames;
  }
  if (want == 0) {
    return;
  }

  MIXER_BMX_Pull(s_stereo, want);
  s_pulls++;

  // Never-block write: AddChunk spin-waits (50us slices) while the device
  // queue is full, so an overlong write stalls the guest thread and the
  // next pull arrives even later (cascade). Cap the write at the free
  // space instead; dropped post-stall padding is a ms glitch, a guest
  // stall is audible stutter. Units match (mono frames).
  int space = circle_sound_bufferspace();
  if (space < 0) {
    space = 0;
  }
  if ((unsigned long long)space < want) {
    s_dropped_frames += want - (unsigned long long)space;
    want = (unsigned long long)space;
  }
  if (want == 0) {
    return;
  }

  // Stereo -> mono (average) + peak tracking for the periodic log line.
  for (unsigned long long i = 0; i < want; i++) {
    int32_t l = (int32_t)s_stereo[i * 2];
    int32_t r = (int32_t)s_stereo[i * 2 + 1];
    int32_t m = (l + r) / 2;
    s_mono[i] = (int16_t)m;
    unsigned int a = (unsigned int)(m < 0 ? -m : m);
    if (a > s_peak) {
      s_peak = a;
    }
  }
  s_frames_total += want;
  circle_sound_write(s_mono, (size_t)want);
}

// -- BMX sound menu backing store -------------------------------------------
// The Sound > Emulation folder is built once per menu open; these helpers
// read the live DOSBox-X sections so it always shows current values, and
// write them back via the same HandleInputline path a dosbox-x.conf line
// uses. SB/OPL hardware is (re)constructed on the RESET event, so changes
// apply after the menu Reset (soft or hard) — the setter says so.

namespace {

Section_prop *sound_section(const char *name) {
  if (control == nullptr) {
    return nullptr;
  }
  return static_cast<Section_prop *>(control->GetSection(name));
}

int index_of(const char *const *names, int count, const char *current,
             int fallback) {
  if (current != nullptr) {
    for (int i = 0; i < count; i++) {
      if (!strcmp(names[i], current)) {
        return i;
      }
    }
  }
  return fallback;
}

bool section_set(Section_prop *prop, const char *key, const char *val) {
  if (prop == nullptr || key == nullptr || val == nullptr) {
    return false;
  }
  char line[64];
  snprintf(line, sizeof(line), "%s=%s", key, val);
  return prop->HandleInputline(line);
}

const char *const kSbTypes[] = {"none",  "sb1",   "sb2",
                                "sbpro1", "sbpro2", "sb16"};
const char *const kOplModes[] = {"auto",     "opl2",  "dualopl2",
                                 "opl3",     "opl3gold", "none"};
const char *const kOplEmus[] = {"default", "compat", "fast",  "nuked",
                                "mame",    "cqm",    "esfmu"};
// NOTE: hardware-serial backends (opl2board/opl3duoboard/retrowave_opl3)
// are deliberately NOT offered: no serial/SPI hardware on bare metal.

}  // namespace

int dos_sound_sbtype_count(void) {
  return (int)(sizeof(kSbTypes) / sizeof(kSbTypes[0]));
}
const char *dos_sound_sbtype_name(int i) {
  if (i < 0 || i >= dos_sound_sbtype_count()) {
    return "";
  }
  return kSbTypes[i];
}
int dos_sound_sbtype_current(void) {
  Section_prop *s = sound_section("sblaster");
  const char *cur = (s != nullptr) ? s->Get_string("sbtype") : nullptr;
  return index_of(kSbTypes, dos_sound_sbtype_count(), cur, 5);
}
void dos_sound_set_sbtype(int i) {
  if (i < 0 || i >= dos_sound_sbtype_count()) {
    return;
  }
  section_set(sound_section("sblaster"), "sbtype", kSbTypes[i]);
  debug_log("sound menu: sbtype=%s (applies after Reset)", kSbTypes[i]);
}

int dos_sound_oplmode_count(void) {
  return (int)(sizeof(kOplModes) / sizeof(kOplModes[0]));
}
const char *dos_sound_oplmode_name(int i) {
  if (i < 0 || i >= dos_sound_oplmode_count()) {
    return "";
  }
  return kOplModes[i];
}
int dos_sound_oplmode_current(void) {
  Section_prop *s = sound_section("sblaster");
  const char *cur = (s != nullptr) ? s->Get_string("oplmode") : nullptr;
  return index_of(kOplModes, dos_sound_oplmode_count(), cur, 0);
}
void dos_sound_set_oplmode(int i) {
  if (i < 0 || i >= dos_sound_oplmode_count()) {
    return;
  }
  section_set(sound_section("sblaster"), "oplmode", kOplModes[i]);
  debug_log("sound menu: oplmode=%s (applies after Reset)", kOplModes[i]);
}

int dos_sound_oplemu_count(void) {
  return (int)(sizeof(kOplEmus) / sizeof(kOplEmus[0]));
}
const char *dos_sound_oplemu_name(int i) {
  if (i < 0 || i >= dos_sound_oplemu_count()) {
    return "";
  }
  return kOplEmus[i];
}
int dos_sound_oplemu_current(void) {
  Section_prop *s = sound_section("sblaster");
  const char *cur = (s != nullptr) ? s->Get_string("oplemu") : nullptr;
  return index_of(kOplEmus, dos_sound_oplemu_count(), cur, 0);
}
void dos_sound_set_oplemu(int i) {
  if (i < 0 || i >= dos_sound_oplemu_count()) {
    return;
  }
  section_set(sound_section("sblaster"), "oplemu", kOplEmus[i]);
  debug_log("sound menu: oplemu=%s (applies after Reset)", kOplEmus[i]);
}

}  // namespace dos
}  // namespace bmx
