//
// apple2_video.h
//
// See apple2_video.cpp.
//

#ifndef apple2_video_h
#define apple2_video_h

namespace bmx {
namespace apple2 {

void video_init(void);
void video_render(void);
bool video_save_screenshot(const char *path);
void video_present_stats(unsigned long *presents, unsigned long *ensure_us,
                         unsigned long *present_us);
void video_frame_stats(unsigned *mode, unsigned *lines);
void video_src_stats(int *left, int *top, int *w, int *h);
void video_set_colors(int brightness, int contrast, int gamma, int tint,
                      int saturation);
void video_get_colors(int *brightness, int *contrast, int *gamma, int *tint,
                      int *saturation);
void video_set_monitor(int mode);
int video_get_monitor(void);

}  // namespace apple2
}  // namespace bmx

#endif
