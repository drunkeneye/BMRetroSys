/*
	mac128k_bmx_backend.h

	C boundary between the Mini vMac backend
	(src/mac128k/mac128k_bmx_backend.c, pure C, Mini vMac types) and the
	BMX C++ wrapper (video/audio/input/menu/debug).

	Included from both sides; __cplusplus guards keep the C++ TU's view
	at C linkage so the linker matches the C TU's symbols.
*/

#ifndef mac128k_bmx_backend_h
#define mac128k_bmx_backend_h

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* poll bits returned by bmx_mac128k_poll_input */
#define BMX_MAC128K_EV_NONE 0
#define BMX_MAC128K_EV_MENU (1 << 0)
#define BMX_MAC128K_EV_RESET_SOFT (1 << 1)

/* Wrapper -> backend (implemented in mac128k_bmx_backend.c) */
void bmx_mac128k_backend_set_rom_path(const char *path);
void bmx_mac128k_backend_set_disk(int drive, const char *path,
	int read_only);
void bmx_mac128k_backend_key(int mkc, int down);
void bmx_mac128k_backend_mouse_delta(int dh, int dv);
void bmx_mac128k_backend_mouse_button(int down);
int bmx_mac128k_backend_init(void);
void bmx_mac128k_backend_run(void);
void bmx_mac128k_backend_shutdown(void);
void bmx_mac128k_backend_request_reboot(int code);
int bmx_mac128k_backend_take_reboot(void);
int bmx_mac128k_backend_insert_disk(const char *path, int read_only);
int bmx_mac128k_backend_eject_disk(int drive);
const char *bmx_mac128k_backend_disk_path(int drive);

/* Backend -> wrapper (implemented in the C++ wrapper) */
void bmx_mac128k_debug_log(const char *s);
void bmx_mac128k_video_clear(void);
void bmx_mac128k_video_blit_rows(const unsigned char *macbits,
	int top, int bottom);
void bmx_mac128k_video_present(void);
void bmx_mac128k_audio_write(const unsigned char *samp, unsigned int n);
unsigned int bmx_mac128k_poll_input(void);
void bmx_mac128k_open_menu(void);

#ifdef __cplusplus
}
#endif

/* circle_get_ticks64 is provided by src/kernel.cpp (C linkage, same
   declaration as third_party/common/circle.h). */
#ifdef __cplusplus
extern "C" {
#endif
extern uint64_t circle_get_ticks64(void);
#ifdef __cplusplus
}
#endif

#endif
