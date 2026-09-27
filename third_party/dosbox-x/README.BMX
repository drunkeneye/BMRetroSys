# DOSBox-X vendored subset — BMX notes

Upstream: DOSBox-X 2026.08.31 (read-only at `/Users/aydin/tmp/dosbox-x`).
Phase-1 KEEP files (14 files; the "13 KEEP files" task list actually
names 14 — all 14 are covered here):

- src/dosbox.cpp, src/hardware/vga_draw.cpp, src/hardware/mixer.cpp
- src/ints/bios.cpp, src/ints/bios_keyboard.cpp, src/ints/int10_modes.cpp
- src/ints/mouse.cpp
- src/dos/dos_devices.cpp, src/dos/dos_misc.cpp, src/dos/dos_programs.cpp
- src/misc/programs.cpp, src/misc/support.cpp
- src/shell/shell.cpp, src/shell/shell_cmds.cpp

BMX-only files in this tree (everything else is 1:1 upstream):

- src/config.h — feature select (mirrors third_party/atari800/src/config.h
  style). Every macro is justified by an actual #ifdef in the subset.
- src/bmx_sdl_stubs.h — SDL stub wall (C-compatible, assert-free, no real
  SDL headers).
- src/SDL.h, src/SDL_syswm.h — shims redirecting `#include "SDL.h"` /
  `<SDL.h>` / `#include "SDL_syswm.h"` (used by mixer.cpp, mouse.cpp,
  bios_keyboard.cpp, dos_misc.cpp, support.cpp, shell.h, sdlmain.h) to
  the stub wall.

## SDL identifier table (upstream grep `SDL_[A-Za-z0-9]*`, sort -u)

| File | SDL identifiers used | Resolution |
|---|---|---|
| dosbox.cpp | SDL_Delay, SDL_GetTicks (+Uint32), SDL_DOSBOX_X_SPECIAL, SDL_DOSBOX_X_IME, SDL_SetIMValues, SDL_IM_ENABLE, SDL_EnableUNICODE, SDL_SetHint, SDL_HINT_IME_INTERNAL_EDITING, SDL_StartTextInput, SDL_StopTextInput | Delay/GetTicks: stub. IME block: config-off (needs WIN32/MACOSX/LINUX+C_X11, none defined; SDL_DOSBOX_X_SPECIAL undefined) |
| vga_draw.cpp | SDL_BYTEORDER, SDL_LIL_ENDIAN, SDL_MapRGB, SDL_font | stub (BYTEORDER=LIL_ENDIAN). SDL_font: USE_TTF-gated, config-off, opaque typedef for coverage |
| mixer.cpp | SDL_AudioSpec, SDL_AudioDeviceID, SDL_InitSubSystem, SDL_INIT_AUDIO, SDL_GetError, SDL_OpenAudio(Device), SDL_CloseAudio(Device), SDL_PauseAudio(Device), SDL_Lock/UnlockAudio(Device), SDL_AUDIO_ALLOW_FREQUENCY_CHANGE/SAMPLES_CHANGE, AUDIO_S16SYS (+Uint8/Uint16, SDLCALL) | stub. SDL1 entry points live (C_SDL2 off); SDL2 device entry points declared for coverage |
| bios.cpp | SDL_DOSBOX_X_SPECIAL (+SDL_STRING in ID string) | config-off (C_SDL2/SDL_DOSBOX_X_SPECIAL undefined); SDL_STRING via SDL_VERSION_ATLEAST |
| bios_keyboard.cpp | SDL_VERSION_ATLEAST, SDL_Set/GetIMValues, SDL_IM_ONOFF | VERSION_ATLEAST->0 in stub (CAN_USE_LOCK off); IME: config-off (WIN32/MACOSX-gated) |
| int10_modes.cpp | SDL_SetIMValues, SDL_IM_FONT_SIZE, SDL_font | config-off (WIN32/MACOSX/USE_TTF-gated); declared in stub |
| mouse.cpp | SDL_ShowCursor, SDL_ENABLE/DISABLE | stub (live, unconditional use) |
| dos_devices.cpp | SDL_SetClipboardText | config-off (C_SDL2-gated); declared in stub |
| dos_misc.cpp | SDL_SetClipboardText, SDL_HasClipboardText | config-off (WIN32/C_SDL2-gated); declared in stub |
| dos_programs.cpp | SDL_CDNumDrives, SDL_CDName, SDL_SwapBE16, SDL_NET (as C_SDL_NET/C_SDL2_NET) | CD/Swap: stub (live). C_SDL_NET/C_SDL2_NET: config-off |
| programs.cpp | SDL_Window, SDL_DisplayMode, SDL_Rect, SDL_GetWindowSize, SDL_GetDesktopDisplayMode, SDL_GetNumVideoDisplays, SDL_GetDisplayBounds, SDL_SetWindowPosition/Title | config-off (all C_SDL2-gated); declared in stub |
| support.cpp | SDL_Quit | stub no-op (live in E_Exit) |
| shell.cpp/shell_cmds.cpp | SDL_STRING | shell.h: SDL_VERSION_ATLEAST(2,0,0)->0 gives "SDL1" |

SDL_ticks_last/SDL_ticks_next are file-static vars defined in dosbox.cpp
itself, not SDL API — no stub needed.

Coverage: `grep -rho "SDL_[A-Za-z_]*" <14 files> | sort -u` = 58
identifiers; 56 resolve to stub declarations or config-off branches,
2 (SDL_ticks_last/SDL_ticks_next) are dosbox.cpp file-statics, not SDL
API — zero uncovered. Non-underscored SDL-adjacent tokens used (Uint8/Uint16/
Uint32, SDLCALL, AUDIO_S16SYS) are also declared in the stub wall.

## Makefile wiring needed (vendor task owns Makefile object wiring)

Do NOT edit vendored files; inject the wall with a forced include on
dosbox-x object rules only. Exact snippet for the DOSBOXX pattern rules:

    DOSBOXX_STUB := $(DOSBOXX_SRC_DIR)/../third_party/dosbox-x/src/bmx_sdl_stubs.h
    # append to the dosbox-x compile commands:
    -include $(DOSBOXX_STUB) -I third_party/dosbox-x/src

`-I third_party/dosbox-x/src` serves config.h (via dosbox.h/vga_draw.cpp
`#include "config.h"`), the SDL.h/SDL_syswm.h shims, and keeps the
`-include` path absolute from $(CURDIR). Keep the Atari rule ordering
note: dosbox-x `-I` dirs must precede VICE dirs (both ship config.h).

The .cpp implementation (ticks->CTimer, audio->circle_sound_write) lands
later and replaces the static-inline no-op bodies.

## Residual risks

1. SDL_Delay no-op breaks Normal_Loop pacing calibration (wrap_delay) and
   SDL_GetTicks()->0 freezes opt_print_ticks deltas — acceptable for
   compile-unblock only; the CTimer mapping is required before boot.
2. SDL_OpenAudio()->-1 forces the mixer's nosound+TIMER path — intended
   Phase-1 default (PC speaker via timer tick, D7 order).
3. vga_draw.cpp includes sdlmain.h, which includes
   output/output_ttf.h and output/output_gamelink.h — neither is vendored
   (USE_TTF/C_GAMELINK are off, but the #includes are unconditional).
   Vendor task must stub or vendor those headers; the SDL wall alone does
   not fix that include chain.
4. bios.cpp DB_HAVE_CLOCK_GETTIME-off selects the ftime()/sys/timeb.h
   fallback — verify circle-stdlib newlib provides it, else define
   DB_HAVE_CLOCK_GETTIME (Circle clock_gettime) instead.
5. C_FPU-off (D8 fpu=false): FPU_Init decl/calls compile out upstream for
   --disable-fpu builds; first compile spike must confirm the subset's
   FPU call sites all sit behind C_FPU.
