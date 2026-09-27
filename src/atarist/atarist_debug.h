//
// atarist_debug.h
//
// SD-card debug log for the BMX Hatari machine.
//

#ifndef atarist_debug_h
#define atarist_debug_h

namespace bmx {
namespace atarist {

// Opens the log file. Safe to call once at emulator startup; falls back to
// printf-only when no volume is writable.
void debug_init(void);

// printf-style line sink: serial (via printf) plus the log file (flushed).
void debug_log(const char *fmt, ...);

// Appends the core's buffered log to the file and clears it. Phase 0: the
// vendored core is not linked yet, so this is a no-op kept for API parity.
void debug_dump_core_log(void);

void debug_close(void);

}  // namespace atarist
}  // namespace bmx

#endif
