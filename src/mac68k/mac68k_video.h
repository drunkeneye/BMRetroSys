//
// mac68k_video.h
//
// Basilisk II video presentation for BMX bare-metal (Phase 0 placeholder)
//

#ifndef mac68k_video_h
#define mac68k_video_h

namespace bmx {
namespace mac68k {

void video_init(void);
void video_render(void);

// Boot-stage splash colors (0 = splash, 1-8 = boot steps, 9 = guest
// starting, 10 = FAILED). See mac68k_video.cpp for the color table.
void video_boot_stage(int stage);

// Dumps the current emulator frame as uncompressed 24-bit BMP.
// Phase 0: no framebuffer yet, always returns false.
bool video_save_screenshot(const char *path);

// Color adjustments (menu sliders: 0..2000 brightness/contrast/tint/
// saturation, 0..4000 gamma; 1000 = neutral). Stored here, applied in the
// present path. Neutral settings take the zero-overhead fast path.
void video_set_color_adjustments(int brightness, int contrast, int gamma,
                                 int tint, int saturation);
void video_get_color_adjustments(int *brightness, int *contrast, int *gamma,
                                 int *tint, int *saturation);

// CRT monitor emulation (Color/B&W/Green/Amber): luma-driven phosphor
// mapping applied to every converted pixel. Color mode is the identity
// (default look unchanged).
void video_set_monitor(int mode);
int video_get_monitor(void);

// Present-path diagnostics for the heartbeat line (Phase 0: all zeros).
void video_present_stats(unsigned long *presents, unsigned long *ensure_us,
                          unsigned long *present_us);

}  // namespace mac68k
}  // namespace bmx

#endif
