//
// dos_perf.h
//
// BMX performance controls for the DOSBox-X machine (fast-with-fallback).
//
// Two tiers, matching what upstream supports:
//
//   LIVE (immediate, no reboot): cpu core + cycles. DOSBox-X re-runs
//   CPU::Change_Config synchronously on section HandleInputline via its
//   onpropchange hook (cpu.cpp) — cpudecoder/CycleMax swap mid-game.
//
//   CODE-FIXED (no menu): frame pacing (adaptive slice sleep, ticks clamp),
//   video map LUT, OPL rate. Either correctness fixes or identical-output
//   wins; a menu would add nothing.
//
// Persisted indices live here (defaults = fast); the bridge saves/loads
// them to settings-dos.txt, boot applies them after SetupConfigSections
// (before CPU_PreInit constructs the CPU from the section).
//

#ifndef dos_perf_h
#define dos_perf_h

namespace bmx {
namespace dos {

// CPU core choices. Default = simple (fastest real-mode interpreter;
// upstream auto-falls-back to normal on paging/ROM edges).
int perf_core_count(void);
const char *perf_core_name(int i);
int perf_core_current(void);
void perf_set_core(int i);

// Cycles choices (HandleInputline value strings). Default = fixed 15000
// (smooth MI1/MI2 era speed; fixed disables the auto-guess).
int perf_cycles_count(void);
const char *perf_cycles_name(int i);
int perf_cycles_current(void);
void perf_set_cycles(int i);

// CPU presets (machine-accurate cputype + community cycle counts).
// Changing preset writes cputype (crossing an architecture family makes
// upstream reboot the guest via the existing reset machinery) and cycles.
// Returns true from perf_set_cpu_preset when the guest will reboot, so the
// caller can warn once. "Custom" (last) means the section doesn't match any
// preset (manual tweaks); selecting it is a no-op.
int perf_cpu_preset_count(void);
const char *perf_cpu_preset_name(int i);
int perf_cpu_preset_current(void);
bool perf_set_cpu_preset(int i);

// Applies a staged cputype switch. Called from run_guest_slice's
// exception handler (core1, outside any CPU batch) before RESET/RESET_END
// are dispatched; no-op when nothing is staged.
void perf_apply_pending_reset(void);

// Writes stored (or default) preset+core+cycles into the cpu section. Call
// after SetupConfigSections, before CPU_PreInit.
void perf_apply_boot(void);

// Live-pump hooks (called from GFX_Events, the thread that never parks):
// perf_audio_tick runs the audio drain timed; perf_snapshot_tick emits the
// 5s `perf snap` line (pump rate, vendored cpu/other split, snd time,
// audio fps = guest speed, guest video fps, video sink time).
void perf_audio_tick(void);
void perf_snapshot_tick(void);

}  // namespace dos
}  // namespace bmx

#endif
