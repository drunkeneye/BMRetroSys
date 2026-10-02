//
// cpc_video.h
//
// CPC video for BMX bare-metal: 1024x312 RGB565 framebuffer holding the
// full CRTC frame (64 chars x 312 lines at Mode-2 pixel granularity).
// The visible block is the first R1 chars / R6 rows (top-left); the rest
// is border ink. Per-scanline rendering keeps raster bars, splits and
// mid-frame palette changes working.
//

#ifndef cpc_video_h
#define cpc_video_h

namespace bmx {
namespace cpc {

void video_init(void);
// Present the framebuffer (called once per frame-loop iteration).
void video_render(void);
// Uncompressed 24-bit BMP dump of the RGB565 framebuffer.
bool video_save_screenshot(const char *path);
void video_present_stats(unsigned long *presents, unsigned long *ensure_us,
                         unsigned long *present_us);
// Frame diagnostics for the heartbeat log: current GA video mode and the
// number of scanlines rendered into the framebuffer last frame (0 while
// the core is stuck before first render; <200 hints at a wedged frame).
void video_frame_stats(unsigned *mode, unsigned *lines);
// CRTC/palette snapshot for video debugging (R1/R6/MA/border pen).
void video_crtc_stats(unsigned *r1, unsigned *r6, unsigned *ma,
                      unsigned *border);
// Canvas source rect (common scaler state) for video debugging.
void video_src_stats(int *left, int *top, int *w, int *h);
// Colour sliders (menu): 1000 = neutral.
void video_set_colors(int brightness, int contrast, int gamma, int tint,
                      int saturation);
void video_get_colors(int *brightness, int *contrast, int *gamma, int *tint,
                      int *saturation);
// CRT monitor emulation (Color/B&W/Green/Amber), baked into the pen LUT.
void video_set_monitor(int mode);
int video_get_monitor(void);

}  // namespace cpc
}  // namespace bmx

#endif
