//
// zx_video.h
//
// ZX Spectrum video presentation for BMX bare-metal: the ZEsarUX ULA
// renders into its rainbow_buffer (indexed), the Circle refresh hook
// converts to the RGB565 FBL and presents.
//

#ifndef zx_video_h
#define zx_video_h

namespace bmx {
namespace zxspectrum {

void video_init(void);
void video_render(void);
// Uncompressed 24-bit BMP dump of the RGB565 framebuffer.
bool video_save_screenshot(const char *path);
void video_present_stats(unsigned long *presents, unsigned long *ensure_us,
                         unsigned long *present_us);

// Menu color sliders (0..2000, neutral 1000; gamma 0..4000).
void video_set_colors(int brightness, int contrast, int gamma, int tint,
                      int saturation);
void video_get_colors(int *brightness, int *contrast, int *gamma, int *tint,
                      int *saturation);

// CRT monitor emulation: luma-driven phosphor mapping applied to every
// presented pixel. Color mode is the identity (default look unchanged).
void video_set_monitor(int mode);
int video_get_monitor(void);

}  // namespace zxspectrum
}  // namespace bmx

#endif
