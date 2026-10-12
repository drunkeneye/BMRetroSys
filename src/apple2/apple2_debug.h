//
// apple2_debug.h
//
// SD-card log for the BMX Apple II machine. No UART chatter: routine log
// lines go to the file only (same policy as the other kernels).
//

#ifndef apple2_debug_h
#define apple2_debug_h

namespace bmx {
namespace apple2 {

void debug_init(void);
void debug_log(const char *fmt, ...);
void debug_dump_core_log(void);
void debug_close(void);

// Bootrail: debug_log() always records into a 16KB RAM ring (sequence
// numbered) and also writes the log file once debug_init() opens it, so a
// single boot captures every step even across the pre-file phase: the ring
// is replayed into the file at init. Blink milestones via the ACT LED for
// the pre-file blind spot (file logging is impossible before the volumes
// mount); short blinks so normal boots barely notice.
void bootrail_blink(int n);

}  // namespace apple2
}  // namespace bmx

// C linkage for the KEGS core (C files).
extern "C" void apple2_debug_log(const char *fmt, ...);

#endif
