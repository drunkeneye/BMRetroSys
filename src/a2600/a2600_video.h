//
// a2600_video.h
//
// 2600 video presentation for BMX bare-metal: Stella renders the TIA
// frame (160 wide, height varies by ROM/region) into its u32 surface;
// this module blits it centered into a fixed 352x304 RGB565 FBL for the
// menu scaler. Same geometry as the Spectrum kernel.
//

#ifndef a2600_video_h
#define a2600_video_h

#include <stddef.h>
#include <stdint.h>

namespace bmx {
namespace a2600 {

void video_init(void);
// Blit one Stella u32 frame (w x h, row pitch elements) centered.
void video_blit(const uint32_t *pixels, int w, int h, int pitch);
// No-cartridge splash (baked-in logo) centered in the gfx area.
void video_show_splash(void);
void video_render(void);
void video_present_stats(unsigned long *presents, unsigned long *ensure_us,
                         unsigned long *present_us);
// Uncompressed 24-bit BMP dump of the RGB565 framebuffer.
bool video_save_screenshot(const char *path);

// Menu color sliders (0..2000, neutral 1000; gamma 0..4000).
void video_set_colors(int brightness, int contrast, int gamma, int tint,
                      int saturation);
void video_get_colors(int *brightness, int *contrast, int *gamma, int *tint,
                      int *saturation);

// CRT monitor emulation (Atari800 pattern): luma-driven phosphor mapping
// applied to every presented pixel (game frame and splash alike).
enum A2600Monitor {
  A2600_MONITOR_COLOR = 0,
  A2600_MONITOR_BW = 1,
  A2600_MONITOR_GREEN = 2,
  A2600_MONITOR_AMBER = 3,
};
void video_set_monitor(int mode);
int video_get_monitor(void);

}  // namespace a2600
}  // namespace bmx

#endif
