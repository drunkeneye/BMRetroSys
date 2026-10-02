//
// dos_video.h
//
// DOSBox-X video presentation for BMX bare-metal (Phase 0 placeholder)
//

#ifndef dos_video_h
#define dos_video_h

namespace bmx {
namespace dos {

void video_init(void);
void video_render(void);

// Dumps the current emulator frame as uncompressed 24-bit BMP.
// Phase 0: no framebuffer yet, always returns false.
bool video_save_screenshot(const char *path);

// Color adjustments (menu sliders: 0..2000 brightness/contrast/tint/
// saturation, 0..4000 gamma; 1000 = neutral). Stored here, applied in the
// XRGB32->RGB565 line sink. Neutral settings take the zero-overhead fast
// path; anything else uses a 256-entry brightness/contrast/gamma LUT plus
// a saturation/hue matrix.
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

// Perf snapshot inputs: guest video frames (RENDER_EndUpdate count) and
// line-sink cost (accumulated host µs + source rows converted).
unsigned long video_guest_frames(void);
void video_sink_stats(unsigned long long *sink_us, unsigned long *rows);
// Aborted (dropped) frames, emulated refresh rate, present-path cost.
unsigned long video_abort_frames(void);
double video_emu_fps(void);
void video_present_cost(unsigned long long *present_us,
                           unsigned long *present_n);

}  // namespace dos
}  // namespace bmx

#endif
