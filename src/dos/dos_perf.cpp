//
// dos_perf.cpp
//
// See dos_perf.h. Section access needs the vendored include roots
// (Makefile dos_perf.o rule, no SDL stub wall).
//

#include "dos_perf.h"
#include "dos_debug.h"
#include "dos_input.h"
#include "dos_sound.h"
#include "dos_video.h"

#include <circle/timer.h>

#include <stdio.h>
#include <string.h>

#include "control.h"
#include "setup.h"

extern "C" {
uint32_t bmx_host_ticks_ms(void);
}

// Vendored Normal_Loop split (dosbox.cpp, __circle__-gated). Same thread.
extern unsigned long long bmx_perf_cpu_us;
extern unsigned long long bmx_perf_other_us;
extern unsigned long bmx_perf_cpu_n;
extern unsigned long bmx_perf_other_n;

namespace bmx {
namespace dos {

namespace {

const char *const kCores[] = {"auto", "normal", "simple", "full"};
const char *const kCycles[] = {"auto",       "fixed 300",  "fixed 3000",
                               "fixed 7500", "fixed 15000", "fixed 30000",
                               "fixed 40000", "fixed 60000", "max"};

// Machine-accurate CPU presets: cputype + community cycle counts.
// 8086/286 force their authentic normal sub-decoders (upstream); 386+
// leave the selected core (simple) alone. No _prefetch variants (they
// E_Exit unless core is normal) and no pentium_mmx (FPU build dep).
struct CpuPreset {
  const char *name;
  const char *cputype;
  const char *cycles;
};
const CpuPreset kPresets[] = {
    {"8088 4.77MHz", "8086", "fixed 300"},
    {"80286 12MHz", "286", "fixed 3000"},
    {"80386DX 33MHz", "386", "fixed 15000"},
    {"80486DX2 66MHz", "486", "fixed 40000"},
    {"Pentium 100MHz", "pentium", "fixed 60000"},
};
const char *kCustomName = "Custom";

// Fast-with-fallback defaults: 386DX-33MHz preset + simple core.
int s_preset_idx = 2;
int s_core_idx = 2;
int s_cycles_idx = 4;
// Staged cputype switch (menu -> soft reset -> reset handler). See
// perf_set_cpu_preset / perf_apply_pending_reset.
char s_pending_cputype[32] = {0};
bool s_pending_cputype_valid = false;

Section_prop *cpu_section(void) {
  if (control == nullptr) {
    return nullptr;
  }
  return static_cast<Section_prop *>(control->GetSection("cpu"));
}

bool cpu_set(const char *key, const char *val) {
  Section_prop *s = cpu_section();
  if (s == nullptr || key == nullptr || val == nullptr) {
    return false;
  }
  char line[64];
  snprintf(line, sizeof(line), "%s=%s", key, val);
  return s->HandleInputline(line);
}

}  // namespace

int perf_core_count(void) {
  return (int)(sizeof(kCores) / sizeof(kCores[0]));
}
const char *perf_core_name(int i) {
  if (i < 0 || i >= perf_core_count()) {
    return "";
  }
  return kCores[i];
}
int perf_core_current(void) {
  // Stored index is the source of truth: boot apply + setters keep the
  // section in sync, and nothing else on BMX touches these props.
  // (Reading back needs care — cycles is a multiremain, not a plain prop.)
  return s_core_idx;
}
void perf_set_core(int i) {
  if (i < 0 || i >= perf_core_count()) {
    return;
  }
  s_core_idx = i;
  // Immediate: CPU onpropchange -> Change_Config swaps cpudecoder live.
  cpu_set("core", kCores[i]);
  debug_log("perf: core=%s (live)", kCores[i]);
}

int perf_cycles_count(void) {
  return (int)(sizeof(kCycles) / sizeof(kCycles[0]));
}
const char *perf_cycles_name(int i) {
  if (i < 0 || i >= perf_cycles_count()) {
    return "";
  }
  return kCycles[i];
}
int perf_cycles_current(void) {
  // See perf_core_current: stored index, section kept in sync.
  return s_cycles_idx;
}
void perf_set_cycles(int i) {
  if (i < 0 || i >= perf_cycles_count()) {
    return;
  }
  s_cycles_idx = i;
  // Immediate: Change_Config rewrites CPU_CycleMax live (fixed also drops
  // the auto-guess bit, so the value sticks).
  cpu_set("cycles", kCycles[i]);
  debug_log("perf: cycles=%s (live)", kCycles[i]);
}

void perf_apply_boot(void) {
  // Sections exist (post-SetupConfigSections), CPU not yet constructed:
  // these become the construction values, no reboot needed.
  // Order: preset (cputype+cycles), then manual core/cycles overrides win.
  const CpuPreset &p = kPresets[s_preset_idx];
  cpu_set("cputype", p.cputype);
  cpu_set("cycles", p.cycles);
  cpu_set("core", kCores[s_core_idx]);
  if (strcmp(kCycles[s_cycles_idx], p.cycles) != 0) {
    cpu_set("cycles", kCycles[s_cycles_idx]);
  }
  debug_log("perf: boot cpu=%s core=%s cycles=%s", p.name,
            kCores[s_core_idx], kCycles[s_cycles_idx]);
}

int perf_cpu_preset_count(void) {
  // Presets + trailing "Custom" (section matches nothing: manual tweaks).
  return (int)(sizeof(kPresets) / sizeof(kPresets[0])) + 1;
}
const char *perf_cpu_preset_name(int i) {
  if (i < 0 || i >= perf_cpu_preset_count()) {
    return "";
  }
  if (i == perf_cpu_preset_count() - 1) {
    return kCustomName;
  }
  return kPresets[i].name;
}
int perf_cpu_preset_current(void) {
  Section_prop *s = cpu_section();
  if (s != nullptr) {
    const char *ct = s->Get_string("cputype");
    // cycles is multiremain; compare via the stored index path instead:
    // match cputype first, then require the cycles value to match too.
    for (int i = 0; i < perf_cpu_preset_count() - 1; i++) {
      if (ct != nullptr && !strcmp(kPresets[i].cputype, ct) &&
          !strcmp(kPresets[i].cycles, kCycles[s_cycles_idx])) {
        return i;
      }
    }
  }
  // Manual tweaks (or pre-boot): stored preset unless cycles drifted.
  if (strcmp(kPresets[s_preset_idx].cycles, kCycles[s_cycles_idx]) != 0) {
    return perf_cpu_preset_count() - 1;
  }
  return s_preset_idx;
}
bool perf_set_cpu_preset(int i) {
  if (i < 0 || i >= perf_cpu_preset_count() - 1) {
    return false;  // "Custom" is display-only
  }
  // Cycles are throw-free: apply live right here (Change_Config rewrites
  // CycleMax synchronously, no reboot).
  const CpuPreset &p = kPresets[i];
  cpu_set("cycles", p.cycles);
  for (int c = 0; c < perf_cycles_count(); c++) {
    if (!strcmp(kCycles[c], p.cycles)) {
      s_cycles_idx = c;
      break;
    }
  }
  // cputype is NOT touched here: across families Change_Config throws
  // int(3) to reboot, which through menu (C) frames terminates the core
  // (the observed crash). Instead stage it and go through the standard
  // soft-reset path; the reset handler applies it where int throws are
  // caught and swallowed (config already assigned before the throw).
  Section_prop *s = cpu_section();
  const char *cur = (s != nullptr) ? s->Get_string("cputype") : nullptr;
  s_preset_idx = i;
  if (cur != nullptr && !strcmp(cur, p.cputype)) {
    debug_log("perf: cpu=%s (already active)", p.name);
    return false;
  }
  snprintf(s_pending_cputype, sizeof(s_pending_cputype), "%s", p.cputype);
  s_pending_cputype_valid = true;
  dos_request_reset(6);  // soft reboot; reset applies the cputype
  debug_log("perf: cpu=%s staged (rebooting DOS)", p.name);
  return true;
}

// Runs in run_guest_slice's catch (core1, outside any CPU batch), before
// RESET/RESET_END are dispatched. Applies a staged cputype; the reboot
// throw (if any) is swallowed because a reset is already in progress and
// Change_Config assigns everything before throwing.
void perf_apply_pending_reset(void) {
  if (!s_pending_cputype_valid) {
    return;
  }
  s_pending_cputype_valid = false;
  Section_prop *s = cpu_section();
  if (s == nullptr) {
    return;
  }
  char line[64];
  snprintf(line, sizeof(line), "cputype=%s", s_pending_cputype);
  try {
    s->HandleInputline(line);
  } catch (int code) {
    debug_log("perf: cputype switch threw int=%d (swallowed, resetting)",
              code);
  } catch (...) {
    debug_log("perf: cputype switch threw unknown (swallowed, resetting)");
  }
  debug_log("perf: cputype=%s active after reset", s_pending_cputype);
}

// -- live pump + snapshot ---------------------------------------------------

namespace {

unsigned long s_pumps = 0;
unsigned long long s_snd_us = 0;
uint32_t s_snap_ms = 0;
unsigned long long s_snap_audio = 0;
unsigned long s_snap_guest_frames = 0;
unsigned long s_snap_abort_frames = 0;
unsigned long long s_snap_sink_us = 0;
unsigned long s_snap_sink_frames = 0;
unsigned long long s_snap_present_us = 0;
unsigned long s_snap_present_n = 0;

}  // namespace

void perf_audio_tick(void) {
  s_pumps++;
  uint32_t t0 = CTimer::GetClockTicks();
  sound_play();
  s_snd_us += (uint32_t)(CTimer::GetClockTicks() - t0);
}

void perf_snapshot_tick(void) {
  uint32_t now_ms = bmx_host_ticks_ms();
  if (s_snap_ms == 0) {
    s_snap_ms = now_ms;
    s_snap_audio = sound_frames_total();
    s_snap_guest_frames = video_guest_frames();
    s_snap_abort_frames = video_abort_frames();
    unsigned long long sink_us;
    unsigned long sink_frames;
    video_sink_stats(&sink_us, &sink_frames);
    s_snap_sink_us = sink_us;
    s_snap_sink_frames = sink_frames;
    unsigned long long present_us;
    unsigned long present_n;
    video_present_cost(&present_us, &present_n);
    s_snap_present_us = present_us;
    s_snap_present_n = present_n;
    return;
  }
  uint32_t elapsed = (uint32_t)(now_ms - s_snap_ms);
  if (elapsed < 5000u) {
    return;
  }
  if (!DOS_RUNTIME_TRACE) {
    // Stay silent but keep the baseline moving so re-enabling mid-run
    // reports sane deltas instead of a 5-minute window.
    s_snap_ms = now_ms;
    s_pumps = 0;
    s_snd_us = 0;
    s_snap_audio = sound_frames_total();
    s_snap_guest_frames = video_guest_frames();
    s_snap_abort_frames = video_abort_frames();
    unsigned long long sink_us;
    unsigned long sink_frames;
    video_sink_stats(&sink_us, &sink_frames);
    s_snap_sink_us = sink_us;
    s_snap_sink_frames = sink_frames;
    unsigned long long present_us;
    unsigned long present_n;
    video_present_cost(&present_us, &present_n);
    s_snap_present_us = present_us;
    s_snap_present_n = present_n;
    bmx_perf_cpu_us = 0;
    bmx_perf_other_us = 0;
    bmx_perf_cpu_n = 0;
    bmx_perf_other_n = 0;
    return;
  }
  double dt = (double)elapsed / 1000.0;
  unsigned long long audio_now = sound_frames_total();
  double audio_fps = (double)(audio_now - s_snap_audio) / dt;
  unsigned long guest_now = video_guest_frames();
  double guest_fps = (double)(guest_now - s_snap_guest_frames) / dt;
  unsigned long abort_now = video_abort_frames();
  double abort_fps = (double)(abort_now - s_snap_abort_frames) / dt;
  double emu_fps = video_emu_fps();
  unsigned long long sink_us;
  unsigned long sink_frames;
  video_sink_stats(&sink_us, &sink_frames);
  double sink_ms =
      (sink_frames > s_snap_sink_frames)
          ? (double)(sink_us - s_snap_sink_us) / 1000.0 /
                (double)(sink_frames - s_snap_sink_frames)
          : 0.0;
  unsigned long long present_us;
  unsigned long present_n;
  video_present_cost(&present_us, &present_n);
  double pres_ms = (present_n > s_snap_present_n)
                       ? (double)(present_us - s_snap_present_us) / 1000.0 /
                             (double)(present_n - s_snap_present_n)
                       : 0.0;
  unsigned long long cpu_us = bmx_perf_cpu_us;
  unsigned long long other_us = bmx_perf_other_us;
  unsigned long cpu_n = bmx_perf_cpu_n;
  unsigned long other_n = bmx_perf_other_n;
  bmx_perf_cpu_us = 0;
  bmx_perf_other_us = 0;
  bmx_perf_cpu_n = 0;
  bmx_perf_other_n = 0;
  debug_log("perf snap: pump=%.0f/s cpu=%.2fms/%lu other=%.2fms/%lu "
            "snd=%.2fms audio=%.0ffps guest=%.1ffps abort=%.1ffps "
            "emufps=%.1f vid=%.2fms pres=%.2fms",
            (double)s_pumps / dt, (double)cpu_us / 1000.0 / (double)s_pumps,
            cpu_n, (double)other_us / 1000.0 / (double)s_pumps, other_n,
            (double)s_snd_us / 1000.0 / (double)s_pumps, audio_fps, guest_fps,
            abort_fps, emu_fps, sink_ms, pres_ms);
  s_snap_ms = now_ms;
  s_pumps = 0;
  s_snd_us = 0;
  s_snap_audio = audio_now;
  s_snap_guest_frames = guest_now;
  s_snap_abort_frames = abort_now;
  s_snap_sink_us = sink_us;
  s_snap_sink_frames = sink_frames;
  s_snap_present_us = present_us;
  s_snap_present_n = present_n;
}

}  // namespace dos
}  // namespace bmx
