//
// amiga_debug.h
//
// SD-card debug log for the BMX Amiga machine (clone of atarist_debug.h).
// Serial logs = log files: everything also lands in bmretrosys-amiga.log.
//

#ifndef amiga_debug_h
#define amiga_debug_h

namespace bmx {
namespace amiga {

void debug_init(void);
void debug_log(const char *fmt, ...);
void debug_dump_core_log(void);
void debug_close(void);

}  // namespace amiga
}  // namespace bmx

#endif
