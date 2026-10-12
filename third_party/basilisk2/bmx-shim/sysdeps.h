//
// sysdeps.h -- BMX system-dependent definitions for Basilisk II.
//
// Shadows Unix/sysdeps.h (bmx-shim/ is first in BASILISK2_INCLUDE_DIRS).
// Same role, but no autotools: sizes are hardcoded for ARM, no X11/GTK/
// pthreads/TAP assumptions. Interpreter-only banks build:
// REAL_ADDRESSING=0, DIRECT_ADDRESSING=0, EMULATED_68K=1, no VOSF.

#ifndef SYSDEPS_H
#define SYSDEPS_H

#ifndef __STDC__
#error "Your compiler is not ANSI. Get a real one."
#endif

#include <sys/types.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* getpagesize(): newlib declares it in <sys/unistd.h> but never defines
 * it (definition in mac68k_bmx_sys.cpp, ARM pages are 4 KiB). Declared
 * here so TUs that don't include <unistd.h> (e.g. CrossPlatform/
 * vm_alloc.cpp) can call it. NOT a macro (that would break the libc
 * header declaration in TUs that do include <unistd.h>). */
#ifdef __cplusplus
extern "C" {
#endif
#if !defined(__GLIBC__)
extern int getpagesize(void);
#endif
#ifdef __cplusplus
}
#endif

/* Fixed type sizes on ARM (both AArch32 and AArch64 newlib). */
#define SIZEOF_SHORT 2
#define SIZEOF_INT 4
#define SIZEOF_LONG_LONG 8
#if defined(__LP64__) || defined(__aarch64__)
#define SIZEOF_LONG 8
#define SIZEOF_VOID_P 8
#else
#define SIZEOF_LONG 4
#define SIZEOF_VOID_P 4
#endif

/* Mac and host address space are distinct */
#ifndef REAL_ADDRESSING
#define REAL_ADDRESSING 0
#endif
#ifndef DIRECT_ADDRESSING
#define DIRECT_ADDRESSING 0
#endif

/* Using 68k emulator */
#define EMULATED_68K 1

/* The m68k emulator uses a prefetch buffer ? */
#define USE_PREFETCH_BUFFER 0

/* Mac ROM is write protected when banked memory is used (upstream banked
 * parity: REAL_ADDRESSING=0, DIRECT_ADDRESSING=0 => ROM writes via the
 * guest bank stay no-ops; all ROM patches apply via host-side image
 * writes). USE_SCRATCHMEM_SUBTERFUGE intentionally left undefined: that
 * path is a REAL_ADDRESSING-only upstream feature, and the WriteMac-based
 * decoder rewrite is a no-op under banked memory anyway. */
#define ROM_IS_WRITE_PROTECTED 1

/* ExtFS / UDP tunnel not supported on BMX (safe stubs in mac68k glue). */
#define SUPPORTS_EXTFS 0
#define SUPPORTS_UDP_TUNNEL 0

/* Data types */
typedef unsigned char uint8;
typedef signed char int8;
typedef unsigned short uint16;
typedef short int16;
#if SIZEOF_INT == 4
typedef unsigned int uint32;
typedef int int32;
#else
typedef unsigned long uint32;
typedef long int32;
#endif
typedef unsigned long long uint64;
typedef long long int64;
#define VAL64(a) (a ## LL)
#define UVAL64(a) (a ## uLL)
#if SIZEOF_VOID_P == 4
typedef uint32 uintptr;
typedef int32 intptr;
#else
typedef uint64 uintptr;
typedef int64 intptr;
#endif

typedef off_t loff_t;
typedef char * caddr_t;

/* Time data type for Time Manager emulation */
#include <sys/time.h>
typedef struct timeval tm_time_t;

/* Float format codes (IEEE only, as upstream). */
#define UNKNOWN_FLOAT_FORMAT 0
#define IEEE_FLOAT_FORMAT 1
#define VAX_FLOAT_FORMAT 2
#define IBM_FLOAT_FORMAT 3
#define C4X_FLOAT_FORMAT 4

/* UAE CPU data types */
#define uae_s8 int8
#define uae_u8 uint8
#define uae_s16 int16
#define uae_u16 uint16
#define uae_s32 int32
#define uae_u32 uint32
#define uae_s64 int64
#define uae_u64 uint64
typedef uae_u32 uaecptr;

/* ARM allows unaligned access. */
#define CPU_CAN_ACCESS_UNALIGNED 1

/* Timing functions (provided by mac68k glue: circle ticks). */
extern uint64 GetTicks_usec(void);
extern void Delay_usec(uint32 usec);

/* No test-and-set: single-emulator-thread model, BMX spinlocks instead. */
typedef volatile int spinlock_t;
static const spinlock_t SPIN_LOCK_UNLOCKED = 0;
static inline void spin_lock(spinlock_t *lock)
{
  (void)lock;
}
static inline void spin_unlock(spinlock_t *lock)
{
  (void)lock;
}
static inline int spin_trylock(spinlock_t *lock)
{
  (void)lock;
  return 1;
}
#define XDisplayLock()
#define XDisplayUnlock()

/* UAE CPU memory accessors: little-endian host, unaligned OK
 * (mirrors the Unix sysdeps "Other little-endian CPUs" branch). */
static inline uae_u32 do_get_mem_long(uae_u32 *a) {uint32 x = *a; return (x >> 24) | (x >> 8) & 0xff00 | (x << 8) & 0xff0000 | (x << 24);}
static inline uae_u32 do_get_mem_word(uae_u16 *a) {uint16 x = *a; return ((x >> 8) & 0xffu) | (((uae_u32)x << 8) & 0xff00u);}
static inline void do_put_mem_long(uae_u32 *a, uae_u32 v) {*a = (v >> 24) | (v >> 8) & 0xff00 | (v << 8) & 0xff0000 | (v << 24);}
static inline void do_put_mem_word(uae_u16 *a, uae_u32 v) {*a = (v >> 8) | (v << 8);}

static inline uae_u32 do_byteswap_32(uae_u32 v)
  { return (((v >> 24) & 0xff) | ((v >> 8) & 0xff00) | ((v & 0xff) << 24) | ((v & 0xff00) << 8)); }
static inline uae_u32 do_byteswap_16(uae_u32 v)
  { return (((v >> 8) & 0xff) | ((v & 0xff) << 8)); }

#define do_get_mem_byte(a) ((uae_u32)*((uae_u8 *)(a)))
#define do_put_mem_byte(a, v) (*(uae_u8 *)(a) = (v))

#define call_mem_get_func(func, addr) ((*func)(addr))
#define call_mem_put_func(func, addr, v) ((*func)(addr, v))
#define __inline__ inline
#define CPU_EMU_SIZE 0
#undef NO_INLINE_MEMORY_ACCESS
#undef MD_HAVE_MEM_1_FUNCS
#define ENUMDECL typedef enum
#define ENUMNAME(name) name
#define write_log printf

#define ASM_SYM(a)

#ifndef REGPARAM
# define REGPARAM
#endif
#define REGPARAM2

#endif  /* SYSDEPS_H */
