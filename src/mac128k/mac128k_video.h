//
// mac128k_video.h
//
// Mac 128K video presentation for BMX bare-metal.
//

#ifndef mac128k_video_h
#define mac128k_video_h

namespace bmx {
namespace mac128k {

void video_init(void);
void video_render(void);

// Boot-stage splash colors (0 = splash, 1-3 = boot steps,
// 8 = boot complete, 10 = FAILED). See mac128k_video.cpp.
void video_boot_stage(int stage);

// Dumps the current frame as uncompressed 24-bit BMP.
bool video_save_screenshot(const char *path);

// CRT monitor emulation (Color/B&W/Green/Amber). Applies to the 1-bit
// guest pixels at blit time; Color is the identity.
void video_set_monitor(int mode);
int video_get_monitor(void);

void video_present_stats(unsigned long *presents, unsigned long *ensure_us,
                          unsigned long *present_us);

}  // namespace mac128k
}  // namespace bmx

#endif
