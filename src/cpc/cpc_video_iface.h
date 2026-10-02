// cpc_video_iface.h
//
// C-safe hooks between the CPC core/hardware (C) and the video renderer
// (cpc_video.cpp, C++).

#ifndef cpc_video_iface_h
#define cpc_video_iface_h

#ifdef __cplusplus
extern "C" {
#endif

// Render one scanline into the framebuffer (called once per emulated line,
// after cpc_hw_end_of_line; reads CRTC/GA state via the cpc_video_* hw
// getters). Lines past the framebuffer height are skipped.
void cpc_video_scanline(void);
// Reset the framebuffer line counter (called at CRTC frame end).
void cpc_video_frame_start(void);
// Mark the hardware-colour -> RGB LUT stale (called on INKR writes).
void cpc_video_palette_dirty(void);

#ifdef __cplusplus
}
#endif

#endif
