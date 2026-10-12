//
// bbc_debug.h
//
// See bbc_debug.cpp (RAM bootrail ring + bmretrosys-bbc.log).
//

#ifndef BMX_BBC_DEBUG_H
#define BMX_BBC_DEBUG_H

namespace bmx {
namespace bbc {

void debug_init(void);
void debug_log(const char *fmt, ...);
void bootrail_blink(int n);
void debug_dump_core_log(void);
void debug_close(void);

}  // namespace bbc
}  // namespace bmx

#ifdef __cplusplus
extern "C" {
#endif
void bbc_debug_log(const char *fmt, ...);
#ifdef __cplusplus
}
#endif

#endif  // BMX_BBC_DEBUG_H
