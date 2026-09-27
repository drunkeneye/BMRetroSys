//
// vamiga_prefix.h -- force-included prefix for vendored vAmiga C++ objects.
//
// Same pattern as hatari_prefix.h / DOSBOXX_FORCE_INCLUDE: idempotent
// system headers first so every upstream TU is self-sufficient under the
// bare-metal newlib headers, plus the BMX_BARE_METAL marker for any
// #ifdef-gated BMX adaptations inside src/amiga glue (upstream files are
// never edited).
//

#ifndef BMX_VAMIGA_PREFIX_H
#define BMX_VAMIGA_PREFIX_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#ifndef BMX_BARE_METAL
#define BMX_BARE_METAL 1
#endif

// Bare metal defines neither _WIN32 nor __unix__, so utl's generic branch
// picks MSVC localtime_s/gmtime_s. Map them onto POSIX (same semantics,
// swapped arguments).
#define localtime_s(l, t) localtime_r((t), (l))
#define gmtime_s(l, t) gmtime_r((t), (l))

// newlib (Circle) ships no IPv6 bits; cpp-httplib references a few.
// Parse-only: the HTTP servers are never started on BMX.
#ifndef IPV6_V6ONLY
#define IPV6_V6ONLY 26
#endif
#ifndef INET6_ADDRSTRLEN
#define INET6_ADDRSTRLEN 46
#endif
#ifndef NI_NUMERICHOST
#define NI_NUMERICHOST 1
#endif
#ifndef IN6_IS_ADDR_LINKLOCAL
#define IN6_IS_ADDR_LINKLOCAL(a) \
  ((a)->s6_addr[0] == 0xfe && (((a)->s6_addr[1] & 0xc0) == 0x80))
#endif

#endif  // BMX_VAMIGA_PREFIX_H
