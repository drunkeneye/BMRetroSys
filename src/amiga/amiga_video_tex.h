//
// amiga_video_tex.h
//
// Vamiga-only texture accessor (no Circle headers here, so the two type
// worlds never mix in one TU). Backs the FBL blit in amiga_video.cpp.
//

#ifndef amiga_video_tex_h
#define amiga_video_tex_h

#include <stdint.h>

namespace bmx {
namespace amiga {

// Returns the stable Denise texture as one u64 Texel per pixel
// (912x313 PAL; color packing per video_tex_format()), valid until the
// next computeFrame(). Dimensions via w/h. NULL when not ready.
const uint64_t *video_lock_pixels(int *w, int *h);

// Active host texel format, same values as vamiga::TexFormat
// (0=ABGR, 1=ARGB, 2=RGBA). The blit adapts its channel shifts to it.
int video_tex_format(void);

// Visible content box in texture pixels (Denise inner area: border
// cropped). Returns false when undetermined (caller keeps the previous
// box or falls back to full frame).
bool video_content_box(int *x0, int *y0, int *w, int *h);

}  // namespace amiga
}  // namespace bmx

#endif
