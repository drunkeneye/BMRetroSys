//
// vamiga_c_prefix.h -- force-included prefix for vendored vAmiga C objects
// (utlib thirdparty: lz4.c, zip.c). C-safe: no C++ constructs.
//
// newlib declares ftruncate/symlink in sys/unistd.h and ships a utime.h
// that needs time_t first; pull them in the right order so zip.c sees
// everything without touching upstream files.
//

#ifndef BMX_VAMIGA_C_PREFIX_H
#define BMX_VAMIGA_C_PREFIX_H

#include <time.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/unistd.h>
#include <unistd.h>
#include <utime.h>

// newlib's utime.h is a dummy (struct only, no function). Declared here,
// defined as a failing stub in amiga_bmx_stubs.cpp (never called on BMX).
int utime(const char *path, const struct utimbuf *times);

#endif  // BMX_VAMIGA_C_PREFIX_H
