//
// apple2_bmx_shim.c
//
// BMX driver-provided symbols for the vendored KEGS core that have no
// bare-metal implementation: real joystick hardware polling (BMX drives
// joystick_callback_update() directly from the USB latch instead),
// serial/modem backends (unconnected in v1), and the audio rate default
// (owned by sound_driver.c upstream, which is excluded).
//
// Signatures mirror protos_base.h exactly (validated by the host harness
// link in /tmp/opencode/a2lab).
//

#include "defc.h"
#include "defcomm.h"

/* Preferred mixer rate (sound_driver.c upstream); the BMX drain resamples
 * 48 kHz -> 44.1 kHz. */
int g_preferred_rate = 48000;

/* Joystick callback state (joystick_driver.c upstream). BMX feeds these
 * from poll_input(); paddles.c consumes them. Range 0..65535, center 32767.
 * Buttons: bit 0 = button 0, bit 1 = button 1. */
int g_joystick_callback_buttons = 0;
int g_joystick_callback_x = 32767;
int g_joystick_callback_y = 32767;

void joystick_callback_update(word32 buttons, int paddle_x, int paddle_y) {
  extern int g_paddle_buttons;
  g_joystick_callback_buttons = (g_paddle_buttons & (~3)) | (buttons & 3);
  g_joystick_callback_x = paddle_x;
  g_joystick_callback_y = paddle_y;
}

/* Real-hardware joystick polling: nothing to poll on bare metal. */
void joystick_init(void) {}
void joystick_update(dword64 dfcyc) { (void)dfcyc; }
void joystick_update_buttons(void) {}

/* Serial/modem backends: unconnected in v1 (Super Serial Card absent). */
void scc_serial_unix_open(int port) { (void)port; }
void scc_serial_unix_close(int port) { (void)port; }
void scc_serial_unix_change_params(int port) { (void)port; }
void scc_serial_unix_fill_readbuf(dword64 dfcyc, int port, int space_left) {
  (void)dfcyc;
  (void)port;
  (void)space_left;
}
void scc_serial_unix_empty_writebuf(int port) { (void)port; }
void scc_socket_open(dword64 dfcyc, int port, int cfg) {
  (void)dfcyc;
  (void)port;
  (void)cfg;
}
void scc_socket_close(int port) { (void)port; }
void scc_socket_fill_readbuf(dword64 dfcyc, int port, int space_left) {
  (void)dfcyc;
  (void)port;
  (void)space_left;
}
void scc_socket_empty_writebuf(dword64 dfcyc, int port) {
  (void)dfcyc;
  (void)port;
}

/* Process plumbing: unavailable on bare metal. cfg_decompress_file()
 * (gunzip for .gz disks) treats every failure as "cannot decompress" and
 * aborts the insert cleanly, so failing here is correct behavior.
 * (Compressed images stay unmounted; feed the kernel plain .dsk/.po/
 * .nib/.woz/.2mg/.hdv.) */
int fork(void) { return -1; }
int pipe(int fd[2]) {
  (void)fd;
  return -1;
}
int dup2(int oldfd, int newfd) {
  (void)oldfd;
  (void)newfd;
  return -1;
}
int waitpid(int pid, int *status, int opts) {
  (void)pid;
  (void)status;
  (void)opts;
  return -1;
}
int system(const char *cmd) {
  (void)cmd;
  return -1;
}

/* File truncation: unavailable on bare metal (FATFS grows on write).
 * Only used by WOZ write-through; failing degrades to a non-shrinking
 * image, which still reads/writes correctly. */
int ftruncate(int fd, long length) {
  (void)fd;
  (void)length;
  return -1;
}
