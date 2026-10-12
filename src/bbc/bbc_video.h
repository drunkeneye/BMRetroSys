//
// bbc_video.h
//
// ScanTarget implementation: CLK feeds Red1Green1Blue1 scanlines, the
// driver rasterises them into a full-scale RGB565 buffer and blits the
// visible area to the 800x600 Circle framebuffer (monitor LUT applied).
//

#ifndef BMX_BBC_VIDEO_H
#define BMX_BBC_VIDEO_H

#include "src/monitor_phosphor.h"

namespace bmx {
namespace bbc {

void video_init(void);
void video_render(void);
// Field framing: true once a complete field (retrace-to-retrace) has
// been rasterised and is ready to present. The core loop runs the
// machine in slices until ready, presents, then acknowledges.
bool video_field_ready(void);
void video_field_ack(void);
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
bool video_save_screenshot(const char *path);

}  // namespace bbc
}  // namespace bmx

#endif  // BMX_BBC_VIDEO_H
