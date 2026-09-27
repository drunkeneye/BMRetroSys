/* config.h - Hand-written for BMX bare-metal build, DOSBox-X 2026.08.31.
 *
 * Mirrors what upstream's own configure would produce for a no-SDL,
 * no-network, interpreter-only target, minus everything the bare-metal
 * port does not use.
 *
 * Rule: this file plus bmx_sdl_stubs.h (+ SDL.h/SDL_syswm.h shims) are the
 * ONLY BMX-specific files inside third_party/dosbox-x. Everything else
 * stays 1:1 with upstream.
 *
 * Deliberately NOT defined: C_SDL2/C_SDL1 (no SDL backend; BMX provides
 * the stub wall), SDL_DOSBOX_X_SPECIAL (patched-SDL marker, no backend
 * tests it on BMX), C_X11/C_OPENGL/C_DIRECT3D/C_METAL (no GPU backend),
 * C_DEBUG/C_HEAVY_DEBUG (no curses debugger), C_DOSBOX_AGENT,
 * C_DYNREC/C_DYNAMIC_X86/C_HAVE_MPROTECT (interpreter only, no JIT),
 * C_FPU_X86 (generic FPU path; C_FPU itself is ON so fpu.cpp compiles,
 * runtime fpu=false keeps it dormant), C_AVCODEC/C_SSHOT (no capture),
 * C_AVCODEC (no FFmpeg capture), C_SSHOT/C_LIBPNG (BMX screenshots),
 * C_PCAP/C_SLIRP/C_SDL_NET/C_SDL2_NET/C_MODEM/C_IPX (no networking),
 * C_FLUIDSYNTH/C_MT32 (non-goals, D7), HAVE_ALSA (no ALSA MIDI),
 * C_PRINTER/C_XBRZ/C_GAMELINK/C_FREETYPE (non-goals),
 * C_EMSCRIPTEN/C_OSFREE (DOS kernel enabled), C_ICONV (WIN32-only
 * self-define; #ifdef fallbacks used), DB_HAVE_CLOCK_GETTIME (ftime
 * fallback used), WORDS_BIGENDIAN (ARM is little-endian).
 */
#ifndef DOSBOXX_CONFIG_H_
#define DOSBOXX_CONFIG_H_

#define PACKAGE_NAME "dosbox-x"
#define PACKAGE "dosbox-x"
#define PACKAGE_VERSION "2026.08.31"
#define PACKAGE_STRING "dosbox-x 2026.08.31"
#define VERSION "2026.08.31"

/* CPU: interpreter-only 286 target (D8). C_TARGETCPU is not tested by any
 * Phase-1 KEEP file, so it is deliberately left undefined here. */

/* Unaligned access is permitted on ARM (vga_draw.cpp:1014 tests this). */
#define C_UNALIGNED_MEMORY 1

/* Bare-metal runtime support (Circle newlib provides these headers). */
#define HAVE_STDINT_H 1
#define HAVE_INTTYPES_H 1
#define HAVE_STRING_H 1
#define HAVE_STDLIB_H 1
#define HAVE_SYS_STAT_H 1

/* No Unix-specific features (mirrors atari800 config.h policy: present
 * in newlib, but must not gate Unix behaviour on). */
/* #undef HAVE_UNISTD_H */
/* #undef HAVE_SYS_TYPES_H */
/* #undef HAVE_STRINGS_H */
/* #undef HAVE_MEMORY_H */

/* ARM is little-endian (SDL_BYTEORDER in the stub wall agrees). */
/* #undef WORDS_BIGENDIAN */

/* Resource directory fallback (cross.cpp GetResourceDir). No /usr/share on
 * bare metal: point at the DOS system dir so resource probes hit the SD. */
#define RESDIR "/dos"

/* No SDL backend (C_SDL2/C_SDL1): every C_SDL2 branch in the subset
 * compiles out; SDL1 fallbacks resolve to bmx_sdl_stubs.h. */
/* #undef C_SDL2 */
/* #undef C_SDL1 */

/* No GPU/windowing backends (D8 non-goals). */
/* #undef C_X11 */
/* #undef C_OPENGL */
/* #undef C_DIRECT3D */
/* #undef C_METAL */

/* No debugger/agent. */
/* #undef C_DEBUG */
/* #undef C_HEAVY_DEBUG */
/* #undef C_DOSBOX_AGENT */

/* No JIT (interpreter only). FPU code COMPILES in (C_FPU) but stays off at
 * runtime via fpu=false (dosbox-x README QBasic guidance, D8): with C_FPU
 * undefined fpu.cpp does not compile (its savestate tail is unconditional),
 * while C_FPU=1 costs one 32K TU and keeps the cpu ESC handlers linked. */
/* #undef C_DYNREC */
/* #undef C_DYNAMIC_X86 */
/* #undef C_HAVE_MPROTECT */
#define C_FPU 1
/* #undef C_FPU_X86 */

/* No capture/screenshots (BMX owns screenshots), no audio synth goals. */
/* #undef C_AVCODEC */
/* #undef C_SSHOT */
/* C_LIBPNG stays ON so bios.cpp's `#if C_LIBPNG` include/use twins stay
 * consistent (logo arrays + <png.h>); the header resolves to the BMX shim
 * whose failing stubs make the logo path skip safely. No real libpng. */
#define C_LIBPNG 1
/* #undef C_FLUIDSYNTH */
/* #undef C_MT32 */
/* #undef HAVE_ALSA */

/* No networking of any kind. */
/* #undef C_PCAP */
/* #undef C_SLIRP */
/* #undef C_SDL_NET */
/* #undef C_SDL2_NET */
/* #undef C_MODEM */
/* #undef C_IPX */

/* Misc non-goals / wrong-target guards. */
/* #undef C_PRINTER */
/* #undef C_XBRZ */
/* #undef C_GAMELINK */
/* #undef C_EMSCRIPTEN */
/* #undef C_OSFREE */
/* #undef C_ICONV */
/* #undef DB_HAVE_CLOCK_GETTIME */

/* Compiler attributes (ARM GCC/Clang support all of these). */
#define C_HAS_ATTRIBUTE 1
#define C_ATTRIBUTE_ALWAYS_INLINE 1
/* #undef C_ATTRIBUTE_FASTCALL */
#define C_HAS_BUILTIN_EXPECT 1

#if C_ATTRIBUTE_ALWAYS_INLINE
#define INLINE inline __attribute__((always_inline))
#else
#define INLINE inline
#endif

#ifdef C_ATTRIBUTE_FASTCALL
#define DB_FASTCALL __attribute__((fastcall))
#else
#define DB_FASTCALL
#endif

#if C_HAS_ATTRIBUTE
#define GCC_ATTRIBUTE(x) __attribute__((x))
#else
#define GCC_ATTRIBUTE(x) /* attribute not supported */
#endif

#if C_HAS_BUILTIN_EXPECT
#define GCC_UNLIKELY(x) __builtin_expect((x), 0)
#define GCC_LIKELY(x) __builtin_expect((x), 1)
#else
#define GCC_UNLIKELY(x) (x)
#define GCC_LIKELY(x) (x)
#endif

/* Base integer types (adapted from upstream vs/config.h for ARM).
 * dosbox.h:112-189 needs GCC_ATTRIBUTE + Bits/Bitu from THIS file. */
typedef double Real64;

#define SIZEOF_UNSIGNED_CHAR 1
#define SIZEOF_UNSIGNED_SHORT 2
#define SIZEOF_UNSIGNED_INT 4
#if defined(__aarch64__)
#define SIZEOF_UNSIGNED_LONG 8
#define SIZEOF_UNSIGNED_LONG_LONG 8
#define SIZEOF_INT_P 8
#else /* AArch32 ILP32 */
#define SIZEOF_UNSIGNED_LONG 4
#define SIZEOF_UNSIGNED_LONG_LONG 8
#define SIZEOF_INT_P 4
#endif

typedef unsigned char Bit8u;
typedef signed char Bit8s;
typedef unsigned short Bit16u;
typedef signed short Bit16s;
typedef unsigned int Bit32u;
typedef signed int Bit32s;
typedef unsigned long long Bit64u;
typedef signed long long Bit64s;

#if SIZEOF_INT_P == 4
typedef Bit32u Bitu;
typedef Bit32s Bits;
#else
typedef Bit64u Bitu;
typedef Bit64s Bits;
#endif

#endif /* DOSBOXX_CONFIG_H_ */
