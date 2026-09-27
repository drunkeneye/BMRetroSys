//
// utime.h -- minimal utime shim for Hatari bare-metal (BMX).
//
// newlib (Circle) has no <utime.h>: Hatari's gemdos.c includes it behind
// HAVE_UTIME_H (defined in the BMX config.h) for GEMDOS host-file date
// stamping. The struct mirrors POSIX; utime() itself is implemented in
// hatari_bmx_platform.c and intentionally declines (returns -1), so file
// dates are left alone -- cosmetic only, writes are unaffected.
//

#ifndef BMX_HATARI_UTIME_SHIM_H
#define BMX_HATARI_UTIME_SHIM_H

#include <sys/types.h>
#include <time.h>

struct utimbuf {
  time_t actime;
  time_t modtime;
};

extern int utime(const char *path, const struct utimbuf *times);

#endif  // BMX_HATARI_UTIME_SHIM_H
