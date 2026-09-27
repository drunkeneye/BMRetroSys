//
// config.h
//
// Hatari feature selection for BMX bare-metal (Circle + newlib, no SDL,
// no X, no host OS services). Hand-written, mirroring the BMX pattern used
// for atari800 (third_party/atari800/src/config.h) and dosbox-x
// (third_party/dosbox-x/src/config.h): the file is found as "config.h"
// because third_party/hatari/src is leftmost on the Hatari include path.
//
// Convention (upstream CMake): a macro that is *absent* means the feature
// is off (most tests are `#if MACRO`, which treats undefined as 0).
// Only macros that are positively true on BMX are defined below.
//

#ifndef HATARI_BMX_CONFIG_H
#define HATARI_BMX_CONFIG_H

// --- Target identification ------------------------------------------------
// ARM little-endian (WORDS_BIGENDIAN stays undefined = little endian).

// --- DSP (Falcon) ----------------------------------------------------------
// No DSP56001 emulation on BMX (non-goal, ATARIST.md D1).
#define ENABLE_DSP_EMU 0

// --- Tracing ---------------------------------------------------------------
/* #undef ENABLE_TRACING */

// --- Libraries --------------------------------------------------------------
/* #undef HAVE_LIBPNG */      // screenshots stay BMP (SCREEN_SNAPSHOT_BMP)
/* #undef HAVE_ZLIB_H */
/* #undef HAVE_LIBZ */        // no .zip/.gz support: TOS/floppies are raw
/* #undef HAVE_LIBARCHIVE */
/* #undef HAVE_CAPSIMAGE */   // no IPF/CAPS (proprietary lib, never)
/* #undef HAVE_PORTMIDI */    // no MIDI (non-goal)

// --- Host OS services -------------------------------------------------------
/* #undef HAVE_UNIX_DOMAIN_SOCKETS */  // no control socket (control.c)
// newlib does provide these libc headers:
#define HAVE_UNISTD_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_STRINGS_H 1
// <utime.h> comes from the BMX shim (bmx-shim/utime.h): struct utimbuf for
// GEMDOS date stamping; utime() itself declines (see hatari_bmx_platform.c).
#define HAVE_UTIME_H 1
/* #undef HAVE_SYS_TIME_H */   // timeval pacing unused (Circle CTimer instead)
/* #undef HAVE_TERMIOS_H */
/* #undef HAVE_SYS_IOCTL_H */
/* #undef HAVE_FLOCK */
/* #undef HAVE_SELECT */
/* #undef HAVE_UTIME_H */
/* #undef HAVE_SYS_UTIME_H */
/* #undef HAVE_STATVFS */
/* #undef HAVE_SCANDIR */
/* #undef HAVE_ALPHASORT */
/* #undef HAVE_FSEEKO */
/* #undef HAVE_FTELLO */
/* #undef HAVE_BSWAP_16 */
/* #undef HAVE_BSWAP_32 */
/* #undef HAVE_BYTESWAP_H */
/* #undef HAVE_UDEV */
/* #undef HAVE_SYS_NDIR_H */
/* #undef HAVE_LOCALE_CHARSET */
/* #undef USE_LOCALE_CHARSET */

// --- SDL ---------------------------------------------------------------------
/* #undef ENABLE_SDL3 */      // sdlgui.h takes the SDL2-shaped branch; the
                              // opaque SDL_Surface/SDL_Event decls come from
                              // the BMX SDL shim (SDL.h) on the include path.

#endif  // HATARI_BMX_CONFIG_H
