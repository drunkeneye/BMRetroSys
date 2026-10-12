//
// mac128k_debug.h
//
// SD-card debug log for the BMX Mac128k machine (Phase 0 placeholder).
//

#ifndef mac128k_debug_h
#define mac128k_debug_h

namespace bmx {
namespace mac128k {

// Opens the log sink (serial + bmretrosys-mac128k.log on SD, per-line fsync).
void debug_init(void);

// printf-style line sink with power-on timestamp (serial + log file).
void debug_log(const char *fmt, ...);

// Appends the core's buffered log. Phase 0: no core log yet, no-op.
void debug_dump_core_log(void);

// ACT LED blink diagnostics (no FS/UART/video needed). Core codes:
// 1=core constructed, 2=Init, 3=LaunchEmulator, 4=core1 Run.
// Heartbeat = double-blink from frame loop.
void debug_blink_boot(void);
void debug_blink(unsigned stage);
void debug_heartbeat(void);

void debug_close(void);

}  // namespace mac128k
}  // namespace bmx

#endif
