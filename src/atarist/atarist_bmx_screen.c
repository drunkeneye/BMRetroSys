//
// atarist_bmx_screen.c
//
// BMX screen backend for the vendored Hatari core (replaces src/sdl/screen.c
// and src/retro/screen.c). Plain C: Hatari headers are C-only.
//
// Hatari's Shifter conversion (conv_st.c) renders the ST frame into a
// 32-bit XRGB buffer that THIS backend owns (via Screen_GetDimension).
// Screen_Draw() runs the conversion only; the 32->16-bit RGB565 blit into
// the Circle FBL happens in atarist_video video_render() (same thread,
// right after m68k_run returns at the VBL break), which keeps render and
// present separated like the Atari800 kernel.
//
// Overscan is off in Phase 1 (ConfigureParams.Screen.bAllowOverscan=false),
// so every ST mode fits 640x400: low (doubled), medium (Y-doubled) and mono.
//

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "main.h"
#include "configuration.h"
#include "screen.h"
#include "video.h"
#include "conv_st.h"

// File+serial log sink (atarist_debug.cpp). No printf(): no UART-only output.
void atarist_log_line(const char *line);

// 32-bit XRGB conversion target owned by this backend. Grown by
// Screen_SetVideoSize; 640x400 covers every non-overscan ST mode.
static uint32_t *s_pixels = NULL;
static int s_width = 0;
static int s_height = 0;
static int s_last_draw_w = 0;
static int s_last_draw_h = 0;
// Draw diagnostics (logged, never UART-only).
static unsigned long s_draws = 0;
static unsigned long s_changed = 0;

// Draw-loop diagnostic for the BMX heartbeat.
unsigned long atarist_bmx_draws(void) {
  return s_draws;
}

bool bGrabMouse = false;
bool bInFullScreen = false;

// BMX-side accessor for atarist_video (blit + present).
uint32_t *atarist_bmx_screen_pixels(int *w, int *h) {
  if (w != NULL) {
    *w = s_last_draw_w;
  }
  if (h != NULL) {
    *h = s_last_draw_h;
  }
  return s_pixels;
}

uint32_t Screen_MapRGB(uint8_t red, uint8_t green, uint8_t blue) {
  return ((uint32_t)red << 16) | ((uint32_t)green << 8) | (uint32_t)blue;
}

void Screen_Init(void) {
  atarist_log_line("AtariST: Screen_Init, BMX FBL backend");
  // Guarantee a conversion target from the start: ConvST sizes the buffer
  // on resolution changes, but the first VBL must never draw into NULL.
  Screen_SetVideoSize(640, 400, true);
  // Install the Shifter draw functions + RGB tables (mirrors SDL
  // Screen_Init, which ends in ConvST_ChangeResolution). Without this the
  // converters are never selected and every frame converts nothing.
  ConvST_ChangeResolution(false);
}

void Screen_UnInit(void) {
  free(s_pixels);
  s_pixels = NULL;
  s_width = 0;
  s_height = 0;
}

bool Screen_Lock(void) {
  return s_pixels != NULL;
}

void Screen_UnLock(void) {
}

void Screen_ClearScreen(void) {
  if (s_pixels != NULL) {
    memset(s_pixels, 0,
           (size_t)s_width * (size_t)s_height * sizeof(uint32_t));
  }
}

void Screen_EnterFullScreen(void) {
}

void Screen_ReturnFromFullScreen(void) {
}

bool Screen_UngrabMouse(void) {
  return true;
}

void Screen_GrabMouseIfNecessary(void) {
}

void Screen_ModeChanged(bool bForceChange) {
  // Re-resolve the conversion once up and running (mirrors SDL, whose
  // sdlscrn guard this replaces).
  if (s_pixels != NULL) {
    ConvST_ChangeResolution(bForceChange);
  }
}

bool Screen_Draw(bool bForceFlip) {
  (void)bForceFlip;
  if (bQuitProgram) {
    return false;
  }
  // Render ST frame into s_pixels (no statusbar, no SDL blit).
  bool changed = ConvST_DrawFrame();
  s_last_draw_w = s_width;
  s_last_draw_h = s_height;
  s_draws++;
  if (changed) {
    s_changed++;
  }
  if (s_draws <= 3 || (changed && s_changed == 1)) {
    char msg[128];
    // Checksum over a sparse sample (full 256k walk each frame is wasteful).
    unsigned long sum = 0;
    if (s_pixels != NULL) {
      size_t n = (size_t)s_width * (size_t)s_height;
      for (size_t i = 0; i < n; i += 4097) {
        sum += s_pixels[i];
      }
    }
    snprintf(msg, sizeof(msg),
             "AtariST: Screen_Draw #%lu %dx%d changed=%d sum=%lu", s_draws,
             s_width, s_height, changed ? 1 : 0, sum);
    atarist_log_line(msg);
  }
  return changed;
}

void Screen_GetPixelFormat(uint32_t *rmask, uint32_t *gmask, uint32_t *bmask,
                           int *rshift, int *gshift, int *bshift) {
  if (rmask != NULL) {
    *rmask = 0x00FF0000;
  }
  if (gmask != NULL) {
    *gmask = 0x0000FF00;
  }
  if (bmask != NULL) {
    *bmask = 0x000000FF;
  }
  if (rshift != NULL) {
    *rshift = 16;
  }
  if (gshift != NULL) {
    *gshift = 8;
  }
  if (bshift != NULL) {
    *bshift = 0;
  }
}

void Screen_GetDimension(uint32_t **pixels, int *width, int *height,
                         int *pitch) {
  if (pixels != NULL) {
    *pixels = s_pixels;
  }
  if (width != NULL) {
    *width = s_width;
  }
  if (height != NULL) {
    *height = s_height;
  }
  if (pitch != NULL) {
    *pitch = s_width * (int)sizeof(uint32_t);
  }
}

void Screen_GetDesktopSize(int *width, int *height) {
  if (width != NULL) {
    *width = 720;
  }
  if (height != NULL) {
    *height = 576;
  }
}

void Screen_SetTextureScale(int width, int height, int win_width,
                            int win_height, bool bForceCreation) {
  (void)width;
  (void)height;
  (void)win_width;
  (void)win_height;
  (void)bForceCreation;
}

bool Screen_SetVideoSize(int width, int height, bool bForceChange) {
  if (width <= 0 || height <= 0) {
    return false;
  }
  if (!bForceChange && width == s_width && height == s_height &&
      s_pixels != NULL) {
    return false;
  }
  uint32_t *next = (uint32_t *)realloc(s_pixels, (size_t)width *
                                                    (size_t)height *
                                                    sizeof(uint32_t));
  if (next == NULL) {
    char msg[64];
    snprintf(msg, sizeof(msg),
             "AtariST: Screen_SetVideoSize: alloc %dx%d FAILED", width, height);
    atarist_log_line(msg);
    return false;
  }
  s_pixels = next;
  s_width = width;
  s_height = height;
  memset(next, 0, (size_t)width * (size_t)height * sizeof(uint32_t));
  {
    char msg[64];
    snprintf(msg, sizeof(msg), "AtariST: Screen_SetVideoSize: %dx%d", width,
             height);
    atarist_log_line(msg);
  }
  return true;
}

void Screen_GenConvUpdate(bool update_statusbar) {
  (void)update_statusbar;
}

uint32_t Screen_GetGenConvWidth(void) {
  return (uint32_t)s_width;
}

uint32_t Screen_GetGenConvHeight(void) {
  return (uint32_t)s_height;
}

int Screen_SaveBMP(const char *filename) {
  // Screenshots go through the BMX menu (FBL dump); not via Hatari.
  (void)filename;
  return -1;
}

void Screen_StatusbarMessage(const char *msg, uint32_t msecs) {
  (void)msg;
  (void)msecs;
}

int Screen_GetUISocket(void) {
  return -1;
}

void Screen_SetTitle(const char *title) {
  (void)title;
}

void Screen_MinimizeWindow(void) {
}

uint32_t Screen_GetMouseState(int *mx, int *my) {
  if (mx != NULL) {
    *mx = 0;
  }
  if (my != NULL) {
    *my = 0;
  }
  return 0;
}

bool Screen_ShowCursor(bool show) {
  (void)show;
  return true;
}
