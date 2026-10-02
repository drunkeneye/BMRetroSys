//
// msx_video.h
//
// TMS9918A scanline renderer for the BMX MSX kernel. Framebuffer is
// 352x304 (256x192 display + 48/48/56/56 border, the VDP rainbow model);
// the picture is drawn CENTERED so the shared scaler (which assumes
// centered gfx) shows it correctly with V/H stretch and border trims.
// Modes: Graphics I (32x24), Text (40x24), Multicolor (64x48) and the
// Screen 2 bitmap layout (MSX2-only, same math as upstream). Sprites
// 8x8/16x16 with optional doubling, back-to-front, transparent 0.
//

#ifndef msx_video_h
#define msx_video_h

namespace bmx {
namespace msx {

void video_init(void);
void video_render(void);
void video_present_stats(unsigned long *presents, unsigned long *ensure_us,
                         unsigned long *present_us);
void video_frame_stats(unsigned *mode, unsigned *lines);
void video_src_stats(int *left, int *top, int *w, int *h);
void video_set_colors(int brightness, int contrast, int gamma, int tint,
                      int saturation);
void video_get_colors(int *brightness, int *contrast, int *gamma, int *tint,
                      int *saturation);
// CRT monitor emulation (shared phosphor map): baked into the pen LUT.
void video_set_monitor(int mode);
int video_get_monitor(void);
// BMP screenshot of the current framebuffer (RGB565 -> 24-bit BMP).
bool video_save_screenshot(const char *path);

}  // namespace msx
}  // namespace bmx

#endif
