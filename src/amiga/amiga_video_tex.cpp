//
// amiga_video_tex.cpp
//
// Vamiga-only TU: exposes the stable texture to the circle-only blit.
// Never includes Circle headers.
//

#include "amiga_video_tex.h"
#include "amiga_config.h"
#include "amiga_debug.h"

#include <stddef.h>
#include <stdint.h>

#include "vaconfig.h"
#include "VAmiga.h"
#include "Amiga.h"
#include "Constants.h"
#include "Texture.h"
#include "HostTypes.h"

namespace bmx {
namespace amiga {

const uint64_t *video_lock_pixels(int *w, int *h) {
  vamiga::VAmiga *v = (vamiga::VAmiga *)config_emu();
  if (v == NULL || v->amiga.amiga == NULL) {
    return NULL;
  }
  try {
    // NOTE: not VideoPort::getTexture() -- that returns animated white
    // noise unless the (unlaunched) emulator thread reports RUNNING.
    // The stable Denise buffer is what the thread frontend displays.
    const vamiga::Texture &tex =
        v->amiga.amiga->denise.pixelEngine.getStableBuffer(0);
    if (tex.pixels.ptr == NULL) {
      return NULL;
    }
    if (w != NULL) {
      *w = (int)vamiga::HPIXELS;
    }
    if (h != NULL) {
      *h = (int)vamiga::VPIXELS;
    }
    return (const uint64_t *)tex.pixels.ptr;
  } catch (...) {
    return NULL;
  }
}

int video_tex_format(void) {
  vamiga::VAmiga *v = (vamiga::VAmiga *)config_emu();
  if (v == NULL || v->amiga.amiga == NULL) {
    return (int)vamiga::TexFormat::RGBA;
  }
  try {
    vamiga::i64 fmt =
        v->amiga.amiga->host.getOption(vamiga::Opt::HOST_TEX_FORMAT);
    if (fmt < 0 || fmt > 2) {
      return (int)vamiga::TexFormat::RGBA;
    }
    return (int)fmt;
  } catch (...) {
    return (int)vamiga::TexFormat::RGBA;
  }
}

bool video_content_box(int *x0, int *y0, int *w, int *h) {
  // Fixed PAL visible frame: full line minus horizontal/vertical blanking
  // (same "large box" VideoPort::findInnerArea starts from, but WITHOUT
  // the border shrink -- the border color is part of the picture).
  // PAL-only kernel, so no NTSC branch.
  const int bx0 = 4 * (int)vamiga::HBLANK_CNT;              // 72
  const int bx1 = 4 * (int)vamiga::PAL::HPOS_MAX;           // 904
  const int by0 = (int)vamiga::PAL::VBLANK_CNT;             // 26
  const int by1 = (int)vamiga::PAL::VPOS_CNT_SF;            // 312
  if (x0 != NULL) {
    *x0 = bx0;
  }
  if (y0 != NULL) {
    *y0 = by0;
  }
  if (w != NULL) {
    *w = bx1 - bx0;
  }
  if (h != NULL) {
    *h = by1 - by0;
  }
  return true;
}

}  // namespace amiga
}  // namespace bmx
