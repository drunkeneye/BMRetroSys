//
// basilisk_prefix.h -- force-included prefix for vendored Basilisk II objects.
//
// Same pattern as hatari_prefix.h / vamiga_prefix.h: idempotent system
// headers first so every upstream TU is self-sufficient, plus BMX build
// switches (interpreter-only 68k, no JIT, no SDL, banks addressing) so no
// upstream file needs patching. Compile with -include bmx-shim/basilisk_prefix.h.

#ifndef BASILISK_BMX_PREFIX_H
#define BASILISK_BMX_PREFIX_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#ifndef __BMX__
#define __BMX__ 1
#endif

#ifndef USE_JIT
#define USE_JIT 0
#endif

#ifndef EMULATED_68K
#define EMULATED_68K 1
#endif

#ifndef REAL_ADDRESSING
#define REAL_ADDRESSING 0
#endif

#ifndef DIRECT_ADDRESSING
#define DIRECT_ADDRESSING 0
#endif

#ifndef ENABLE_VOSF
#define ENABLE_VOSF 0
#endif

#ifndef SUPPORTS_EXTFS
#define SUPPORTS_EXTFS 0
#endif

#ifndef USE_SDL_VIDEO
#define USE_SDL_VIDEO 0
#endif

#ifndef USE_SDL_AUDIO
#define USE_SDL_AUDIO 0
#endif

/* getpagesize(): newlib declares it in <sys/unistd.h> but never defines it,
 * so a real definition lives in mac68k_bmx_sys.cpp (ARM page size 4 KiB).
 * Declared here (force-included before every TU, including ones like
 * CrossPlatform/vm_alloc.cpp that never include sysdeps.h). NOT a macro:
 * a function-like macro would break the libc header declaration in TUs
 * that include <unistd.h>; a duplicate identical declaration is benign. */
#ifdef __linux__
#include <features.h>
#endif
#if defined(__GLIBC__)
/* Host glibc declares getpagesize() itself (with noexcept); pull it in so
 * TUs like CrossPlatform/vm_alloc.cpp that never include <unistd.h> see it. */
#include <unistd.h>
#else
#ifdef __cplusplus
extern "C" {
#endif
/* Newlib only declares getpagesize() in <sys/unistd.h> without defining it;
 * the real definition lives in mac68k_bmx_sys.cpp (ARM page size 4 KiB). */
extern int getpagesize(void);
#ifdef __cplusplus
}
#endif
#endif

/* No autotools config.h: fixed float sizes for ARM newlib. */
#ifndef SIZEOF_FLOAT
#define SIZEOF_FLOAT 4
#endif
#ifndef SIZEOF_DOUBLE
#define SIZEOF_DOUBLE 8
#endif
#ifndef SIZEOF_LONG_DOUBLE
#define SIZEOF_LONG_DOUBLE 8
#endif

/* arm-none-eabi newlib has no <arpa/inet.h>/<netinet/in.h>; the ROM/rsrc
 * patchers use htons/htonl unconditionally. BMX targets are little-endian. */
#ifndef htons
static inline unsigned short bmx_htons(unsigned short v) {
  return (unsigned short)__builtin_bswap16(v);
}
static inline unsigned long bmx_htonl(unsigned long v) {
#if __SIZEOF_LONG__ == 4
  return (unsigned long)__builtin_bswap32((unsigned int)v);
#else
  return (unsigned long)__builtin_bswap64((unsigned long long)v);
#endif
}
#define htons(v) bmx_htons(v)
#define htonl(v) bmx_htonl(v)
#define ntohs(v) bmx_htons(v)
#define ntohl(v) bmx_htonl(v)
#endif

#endif  // BASILISK_BMX_PREFIX_H
