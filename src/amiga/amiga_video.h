//
// amiga_video.h
//
// Amiga video presentation for BMX bare-metal.
//

#ifndef amiga_video_h
#define amiga_video_h

namespace bmx {
namespace amiga {

void video_init(void);
void video_render(void);
void video_refresh_preview(void);

void video_set_color_adjustments(int brightness, int contrast, int gamma,
                                 int tint, int saturation);
void video_get_color_adjustments(int *brightness, int *contrast, int *gamma,
                                 int *tint, int *saturation);

// CRT monitor emulation (Color/B&W/Green/Amber): luma-driven phosphor
// mapping applied to every presented pixel. Color is the identity.
void video_set_monitor(int mode);
int video_get_monitor(void);

bool video_save_screenshot(const char *path);

void video_present_stats(unsigned long *presents, unsigned long *ensure_us,
                         unsigned long *present_us);

}  // namespace amiga
}  // namespace bmx

#endif
