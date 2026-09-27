//
// atarist_video.h
//
// Hatari video presentation for BMX bare-metal (Phase 0 placeholder).
//

#ifndef atarist_video_h
#define atarist_video_h

namespace bmx {
namespace atarist {

void video_init(void);
void video_render(void);

// Repaints the canvas from the last converted frame (menu preview while
// the guest is stalled).
void video_refresh_preview(void);

// Color adjustments (menu sliders: 0..2000 brightness/contrast/tint/
// saturation, 0..4000 gamma; 1000 = neutral). Stored here, applied in the
// XRGB32->RGB565 blit.
void video_set_color_adjustments(int brightness, int contrast, int gamma,
                                 int tint, int saturation);
void video_get_color_adjustments(int *brightness, int *contrast, int *gamma,
                                 int *tint, int *saturation);

// Dumps the current emulator frame as uncompressed 24-bit BMP.
// Phase 0: no frame yet, always returns false.
bool video_save_screenshot(const char *path);

// Present-path diagnostics for the heartbeat line (Phase 0: all zeros).
void video_present_stats(unsigned long *presents, unsigned long *ensure_us,
                         unsigned long *present_us);

}  // namespace atarist
}  // namespace bmx

#endif
