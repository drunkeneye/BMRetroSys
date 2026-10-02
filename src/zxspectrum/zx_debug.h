//
// zx_debug.h
//
// SD-card log for the BMX ZX Spectrum machine. No UART chatter: routine
// log lines go to the file only (same policy as the other kernels).
//

#ifndef zx_debug_h
#define zx_debug_h

namespace bmx {
namespace zxspectrum {

void debug_init(void);
void debug_log(const char *fmt, ...);
void debug_dump_core_log(void);
void debug_close(void);

}  // namespace zxspectrum
}  // namespace bmx

#endif
