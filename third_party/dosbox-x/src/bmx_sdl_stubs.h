/* bmx_sdl_stubs.h - BMX stub wall for the DOSBox-X Phase-1 subset.
 *
 * Provides C-compatible declarations for exactly the SDL identifiers used
 * by the 13 KEEP files (see README.BMX for the per-file table). No real
 * SDL headers are pulled in; SDL.h in this directory is a shim that
 * includes this file.
 *
 * Semantics: timing/audio fns are static-inline no-ops for now (ticks read
 * 0, audio open fails so the mixer takes its nosound+timer path). The
 * later .cpp implementation (ticks->CTimer, audio->circle_sound_write)
 * will replace the no-op bodies. Assert-free.
 */
#ifndef BMX_SDL_STUBS_H_
#define BMX_SDL_STUBS_H_

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* SDL integer types (SDL_stdinc.h replacements). */
typedef uint8_t Uint8;
typedef uint16_t Uint16;
typedef uint32_t Uint32;
typedef uint64_t Uint64;
typedef int8_t Sint8;
typedef int16_t Sint16;
typedef int32_t Sint32;
typedef int64_t Sint64;

#define SDLCALL

/* Version: report "not SDL2" so shell.h picks SDL_STRING "SDL1". */
typedef struct SDL_version { Uint8 major, minor, patch; } SDL_version;
#define SDL_VERSION_ATLEAST(X, Y, Z) (0)
#define SDL_COMPILEDVERSION \
    ((Uint32)1 * 100 * 100 + (Uint32)2 * 100 + (Uint32)14)

/* Endian (ARM LE; agrees with config.h WORDS_BIGENDIAN-off). */
#define SDL_LIL_ENDIAN 1234
#define SDL_BIG_ENDIAN 4321
#define SDL_BYTEORDER SDL_LIL_ENDIAN

static inline Uint16 SDL_SwapBE16(Uint16 x)
{
    return (Uint16)((Uint16)(x << 8) | (Uint16)(x >> 8));
}
static inline Uint32 SDL_SwapBE32(Uint32 x)
{
    return ((x & 0x000000FFu) << 24) | ((x & 0x0000FF00u) << 8) |
           ((x & 0x00FF0000u) >> 8) | ((x & 0xFF000000u) >> 24);
}
static inline Uint64 SDL_SwapBE64(Uint64 x)
{
    return ((x & 0x00000000000000FFull) << 56) |
           ((x & 0x000000000000FF00ull) << 40) |
           ((x & 0x0000000000FF0000ull) << 24) |
           ((x & 0x00000000FF000000ull) << 8) |
           ((x & 0x000000FF00000000ull) >> 8) |
           ((x & 0x0000FF0000000000ull) >> 24) |
           ((x & 0x00FF000000000000ull) >> 40) |
           ((x & 0xFF00000000000000ull) >> 56);
}
static inline Uint16 SDL_SwapLE16(Uint16 x) { return x; }
static inline Uint32 SDL_SwapLE32(Uint32 x) { return x; }
static inline Uint64 SDL_SwapLE64(Uint64 x) { return x; }

/* Cursor / enable constants (mouse.cpp). */
#define SDL_DISABLE 0
#define SDL_ENABLE 1

/* Timer (dosbox.cpp Normal_Loop + opt_print_ticks). Wall-clock backed:
 * SDL_GetTicks reads CTimer microseconds via bmx_host_ticks_ms (defined in
 * src/dosboxx/dosboxx_debug.cpp); SDL_Delay really sleeps so increaseticks
 * throttles instead of busy-spinning. Without this ticksRemain stays 0 and
 * the CPU never executes. */
Uint32 bmx_host_ticks_ms(void);
void bmx_host_delay_ms(Uint32 ms);
static inline Uint32 SDL_GetTicks(void) { return bmx_host_ticks_ms(); }
static inline void SDL_Delay(Uint32 ms) { bmx_host_delay_ms(ms); }

/* Audio (mixer.cpp). SDL_OpenAudio fails so MIXER_Init takes its
 * documented nosound path (TIMER_AddTickHandler) until circle_sound_write
 * lands. SDL1 entry points are live (C_SDL2 is off); SDL2 device entry
 * points are declared for coverage only. */
#define AUDIO_S16SYS 0x8010u
#define SDL_INIT_AUDIO 0x00000010u
#define SDL_AUDIO_ALLOW_FREQUENCY_CHANGE 0x00000001u
#define SDL_AUDIO_ALLOW_SAMPLES_CHANGE 0x00000002u

typedef uint32_t SDL_AudioDeviceID;

typedef struct SDL_AudioSpec {
    int freq;
    Uint16 format;
    Uint8 channels;
    Uint8 silence;
    Uint16 samples;
    Uint32 size;
    void (*callback)(void *userdata, Uint8 *stream, int len);
    void *userdata;
} SDL_AudioSpec;

static inline int SDL_InitSubSystem(Uint32 flags)
{
    (void)flags;
    return -1;
}
static inline const char *SDL_GetError(void) { return "BMX no SDL"; }
static inline void SDL_LockAudio(void) {}
static inline void SDL_UnlockAudio(void) {}
static inline int SDL_OpenAudio(SDL_AudioSpec *desired, SDL_AudioSpec *obtained)
{
    (void)desired;
    (void)obtained;
    return -1;
}
static inline void SDL_CloseAudio(void) {}
static inline void SDL_PauseAudio(int pause_on) { (void)pause_on; }
static inline SDL_AudioDeviceID SDL_OpenAudioDevice(const char *device, int iscapture,
    SDL_AudioSpec *desired, SDL_AudioSpec *obtained, Uint32 allowed_changes)
{
    (void)device;
    (void)iscapture;
    (void)desired;
    (void)obtained;
    (void)allowed_changes;
    return 0;
}
static inline void SDL_CloseAudioDevice(SDL_AudioDeviceID dev) { (void)dev; }
static inline void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause_on)
{
    (void)dev;
    (void)pause_on;
}
static inline void SDL_LockAudioDevice(SDL_AudioDeviceID dev) { (void)dev; }
static inline void SDL_UnlockAudioDevice(SDL_AudioDeviceID dev) { (void)dev; }

/* Video surface/pixel (vga_draw.cpp via sdlmain.h; programs.cpp window
 * fns are C_SDL2-gated but declared here for coverage). */
typedef struct SDL_PixelFormat { int unused; } SDL_PixelFormat;
typedef struct SDL_Surface {
    SDL_PixelFormat *format;
    int w, h;
} SDL_Surface;
typedef struct SDL_Rect {
    int x, y, w, h;
} SDL_Rect;
typedef struct SDL_Window { int unused; } SDL_Window;
typedef struct SDL_Renderer { int unused; } SDL_Renderer;
typedef struct SDL_Texture { int unused; } SDL_Texture;
/* SDL 1.x CD-ROM handle (cdrom.h CDROM_Interface_SDL holds a pointer only;
 * CD-audio backends are Phase-1 non-goals, so opaque is enough). */
typedef struct SDL_CD { int unused; } SDL_CD;
/* Threading primitives (cdrom.h imagePlayer holds an SDL_mutex pointer only;
 * Circle uses cooperative tasks + spinlocks: no SDL threads on BMX). */
typedef struct SDL_mutex { int unused; } SDL_mutex;
typedef struct SDL_Thread { int unused; } SDL_Thread;
typedef struct SDL_cond { int unused; } SDL_cond;
typedef struct SDL_DisplayMode {
    Uint32 format;
    int w, h, refresh_rate;
    void *driverdata;
} SDL_DisplayMode;

static inline Uint32 SDL_MapRGB(SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b)
{
    (void)format;
    (void)r;
    (void)g;
    (void)b;
    return 0;
}
static inline int SDL_ShowCursor(int toggle)
{
    (void)toggle;
    return 0;
}
static inline void SDL_GetWindowSize(SDL_Window *window, int *w, int *h)
{
    (void)window;
    if (w) *w = 640;
    if (h) *h = 480;
}
static inline int SDL_GetDesktopDisplayMode(int displayIndex, SDL_DisplayMode *mode)
{
    (void)displayIndex;
    if (mode) {
        mode->format = 0;
        mode->w = 640;
        mode->h = 480;
        mode->refresh_rate = 60;
        mode->driverdata = 0;
    }
    return 0;
}
static inline int SDL_GetNumVideoDisplays(void) { return 1; }
static inline int SDL_GetDisplayBounds(int displayIndex, SDL_Rect *rect)
{
    (void)displayIndex;
    if (rect) {
        rect->x = 0;
        rect->y = 0;
        rect->w = 640;
        rect->h = 480;
    }
    return 0;
}
static inline void SDL_SetWindowPosition(SDL_Window *window, int x, int y)
{
    (void)window;
    (void)x;
    (void)y;
}
static inline void SDL_SetWindowTitle(SDL_Window *window, const char *title)
{
    (void)window;
    (void)title;
}

/* Clipboard (dos_devices.cpp/dos_misc.cpp; C_SDL2-gated, declared here). */
static inline int SDL_SetClipboardText(const char *text)
{
    (void)text;
    return -1;
}
static inline int SDL_HasClipboardText(void) { return 0; }

/* CD-ROM queries (dos_programs.cpp MOUNT -cd; stub reports no drives). */
static inline int SDL_CDNumDrives(void) { return 0; }
static inline const char *SDL_CDName(int drive)
{
    (void)drive;
    return 0;
}

/* IME (WIN32/MACOSX-gated DOS-V branches; declared here for coverage). */
#define SDL_IM_ONOFF 1
#define SDL_IM_ENABLE 2
#define SDL_IM_FONT_SIZE 3
#define SDL_HINT_IME_INTERNAL_EDITING "SDL_IME_INTERNAL_EDITING"
static inline char *SDL_SetIMValues(int cap, ...)
{
    (void)cap;
    return 0;
}
static inline const void *SDL_GetIMValues(int cap, ...)
{
    (void)cap;
    return 0;
}
static inline int SDL_EnableUNICODE(int enable)
{
    (void)enable;
    return 0;
}
static inline int SDL_SetHint(const char *name, const char *value)
{
    (void)name;
    (void)value;
    return 0;
}
static inline void SDL_StartTextInput(void) {}
static inline void SDL_StopTextInput(void) {}

/* TTF font handle (USE_TTF-gated; opaque typedef so ttf.SDL_font member
 * accesses in TTF-gated blocks have a complete type if ever enabled). */
typedef struct SDL_Font { int unused; } SDL_Font, SDL_font;

/* Misc. */
static inline void SDL_Quit(void) {}

/* SDL_DOSBOX_X_SPECIAL / SDL_DOSBOX_X_IME are intentionally NOT defined:
 * every use site additionally requires WIN32/MACOSX/C_SDL2, so the
 * branches stay off (config.h-off coverage). */

/* BSD-endian helpers (mem.h via byteorder.h): bare-metal Circle/newlib
 * matches none of byteorder.h's OS branches (__linux__/__APPLE__/BSD...),
 * so le16toh/htole16/... would be undefined on target. Pi ARM is
 * little-endian: LE conversions are identity, BE use __builtin_bswap.
 * __circle__-gated so host builds (glibc/macOS provide these) are
 * untouched. */
#ifdef __circle__
#ifndef htobe16
#define htobe16(x) __builtin_bswap16(x)
#define htole16(x) (x)
#define be16toh(x) __builtin_bswap16(x)
#define le16toh(x) (x)
#define htobe32(x) __builtin_bswap32(x)
#define htole32(x) (x)
#define be32toh(x) __builtin_bswap32(x)
#define le32toh(x) (x)
#define htobe64(x) __builtin_bswap64(x)
#define htole64(x) (x)
#define be64toh(x) __builtin_bswap64(x)
#define le64toh(x) (x)
#endif
#endif

/* alloca(3) + clock_gettime(2): newlib headers declare neither function
 * (struct timespec / CLOCK_REALTIME exist; the calls do not). dos_files.cpp
 * uses alloca for path scratch buffers; initRand + BIOS_HostTimeSync
 * (dead, #if 0) use clock_gettime. Real RTC/host-time sync is Phase-2+
 * work (BMX owns time via CTimer); the zero fallback keeps initRand
 * deterministic until then. __circle__-gated so host builds are untouched. */
#ifdef __circle__
#include <alloca.h>
#include <time.h>
static inline int clock_gettime(clockid_t clk_id, struct timespec *tp) {
  (void)clk_id;
  if (tp) {
    tp->tv_sec = 0;
    tp->tv_nsec = 0;
  }
  return 0;
}
#endif

#ifdef __cplusplus
}
#endif

#endif /* BMX_SDL_STUBS_H_ */
