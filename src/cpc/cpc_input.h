//
// cpc_input.h
//
// CPC keyboard matrix (10 lines x 8 bits, 0 = pressed), USB HID mapping,
// joystick injection (Joy0 = line 9, Joy1 = line 6, like real hardware)
// and the autostart typer.
//

#ifndef cpc_input_h
#define cpc_input_h

namespace bmx {
namespace cpc {

// Refresh hook point (firmware scans via PPI; nothing periodic needed, but
// kept for symmetry with the other kernels).
void refresh_keyboard_matrix(void);
// Joystick mode: 0 none, 1 joystick 0 (line 9), 2 joystick 1 (line 6).
void input_set_joystick(int mode);
int input_get_joystick(void);
// Type text through the matrix (autostart RUN").
void input_type_text(const char *text);
void input_type_text_delayed(const char *text, unsigned wait_frames);
void input_pump_typer(void);
// Full tape autostart script: optional |TAPE drive switch (664/6128 boot
// with DISC selected) then RUN". Hands-off: the firmware needs its Ready
// prompt between the two commands.
void input_type_autostart_tape(bool tape_cmd_first);
// Disc autostart: boot to BASIC Ready, then RUN"DISC (standard CPC boot
// file). Discs without a DISC.BAS loader report file-not-found and the
// user runs CAT + RUN"file manually.
void input_type_autostart_disc(void);
// Same, but RUN" a detected boot file (no quotes in name).
void input_type_autostart_disc_name(const char *name);
// Per-frame: drain USB queues, pump typer, handle F12 menu.
void poll_input(void);

}  // namespace cpc
}  // namespace bmx

#endif
