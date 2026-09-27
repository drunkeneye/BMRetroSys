//
// SDL.h -- minimal SDL2 API shim for Hatari bare-metal (BMX).
//
// Hatari's core never calls SDL (all SDL use is isolated in src/sdl/ and
// src/gui-sdl/, which are excluded from the BMX build). The only file that
// must still *parse* is includes/sdlgui.h, which needs the opaque
// SDL_Surface / SDL_EventType / SDL_Event declarations. Those live here;
// no SDL function is ever declared or defined.
//

#ifndef BMX_HATARI_SDL_SHIM_H
#define BMX_HATARI_SDL_SHIM_H

#include <stdbool.h>
#include <stdint.h>

// Opaque screen surface (only ever used as SDL_Surface *).
typedef struct BMX_SDL_Surface {
  int unused;
} SDL_Surface;

// Opaque event type + event (only ever used as values/pointers in the
// SDLGui_DoDialogExt callback signature).
typedef int SDL_EventType;

typedef struct BMX_SDL_Event {
  SDL_EventType type;
  uint8_t padding[60];
} SDL_Event;

#endif  // BMX_HATARI_SDL_SHIM_H
