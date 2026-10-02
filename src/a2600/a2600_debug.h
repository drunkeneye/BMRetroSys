//
// a2600_debug.h
//
// SD-card debug log for the BMX Atari 2600 machine.
//

#ifndef a2600_debug_h
#define a2600_debug_h

namespace bmx {
namespace a2600 {

void debug_log(const char *fmt, ...);
void debug_init(void);
void debug_dump_core_log(void);

}  // namespace a2600
}  // namespace bmx

#endif
