//
// cpc_debug.h
//
// SD-card log for the BMX CPC machine. No UART chatter: routine log lines
// go to the file only (same policy as the other kernels).
//

#ifndef cpc_debug_h
#define cpc_debug_h

namespace bmx {
namespace cpc {

void debug_init(void);
void debug_log(const char *fmt, ...);
void debug_dump_core_log(void);
void debug_close(void);

}  // namespace cpc
}  // namespace bmx

// C linkage for the hardware module + core loop (C files).
extern "C" void cpc_debug_log(const char *fmt, ...);

#endif
