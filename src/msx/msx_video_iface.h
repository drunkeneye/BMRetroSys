//
// msx_video_iface.h
//
// C hooks driven by msx_core.c (per scanline + frame start).
//

#ifndef msx_video_iface_h
#define msx_video_iface_h

#ifdef __cplusplus
extern "C" {
#endif

void msx_video_scanline(void);
void msx_video_frame_start(void);

#ifdef __cplusplus
}
#endif

#endif
