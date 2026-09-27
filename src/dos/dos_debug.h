//
// dos_debug.h
//
// SD-card debug log for the BMX DOSBox-X machine (Phase 0 placeholder).
//

#ifndef dos_debug_h
#define dos_debug_h

namespace bmx {
namespace dos {

// VGA/RENDER trace chatter (per-frame geometry, sampler, slice/PC dumps):
// 0 = off (normal runs; flip to 1 for display debugging). Boot, sound,
// menu, error and one-shot lines are unaffected.
#define DOS_VGA_TRACE 0

// Runtime heartbeat chatter (sound_play heartbeat, perf snap): 0 = off
// (normal runs), 1 = on (audio/perf debugging).
#define DOS_RUNTIME_TRACE 0

// Opens the log sink (serial + bmretrosys-dos.log on SD, per-line fsync).
void debug_init(void);

// After the guest is up, guest-side (dosbox-x:) chatter is dropped — every
// line costs an SD fsync and gameplay is verified working. Error markers
// (E_Exit/TERMINATE/ERROR/failed) always pass through.
void debug_set_runtime_quiet(bool quiet);
bool debug_guest_pass(const char *body);

// printf-style line sink with power-on timestamp (serial + log file).
void debug_log(const char *fmt, ...);

// Appends the core's buffered log. Phase 0: no core log yet, no-op.
void debug_dump_core_log(void);

// ACT LED blink diagnostics (no FS/UART/video needed; 1.2s pause between
// codes). Core codes: 1=core constructed, 2=Init, 3=LaunchEmulator,
// 4=core1 Run. Shared core-0 path (viceapp, DOS only): 5=emmc ready,
// 6=sd mounts ready, 7=network+remote+netcheck done. Core-1 codes:
// 8=menu built, 9=video up. 10=multicore Initialize() returned (single
// LONG ~1.2s blink, unmistakable).
// Heartbeat = double-blink from frame loop.
// Bring-up beacon: 3x (1s on, 1s off). If this is not seen, dos code
// never runs at all.
void debug_blink_boot(void);
void debug_blink(unsigned stage);
void debug_heartbeat(void);

void debug_close(void);

}  // namespace dos
}  // namespace bmx

#endif
