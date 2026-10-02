//
// msx_debug.h
//
// SD-card log for the BMX MSX machine. No UART chatter: routine log lines
// go to the file only (same policy as the other kernels).
//

#ifndef msx_debug_h
#define msx_debug_h

namespace bmx {
namespace msx {

void debug_init(void);
void debug_log(const char *fmt, ...);
void debug_dump_core_log(void);
void debug_close(void);

}  // namespace msx
}  // namespace bmx

// C linkage for the hardware module + core loop (C files).
extern "C" void msx_debug_log(const char *fmt, ...);

#endif
