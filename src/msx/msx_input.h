//
// msx_input.h
//
// MSX keyboard matrix (11 rows x 8 bits, 0 = pressed) in
// msx_keyboard_table[], USB HID mapping, joystick state (read by the PSG
// R14/R15 path in machines/msx.c via puerto_especial_joystick) and the
// autostart typer. Matrix layout is the openMSX standard map.
//

#ifndef msx_input_h
#define msx_input_h

namespace bmx {
namespace msx {

// Host HID -> msx_keyboard_table cell (called from poll_input).
void refresh_keyboard_matrix(void);
// Joystick: BMX 5-bit absolute value (bit0 right, bit1 left, bit2 down,
// bit3 up, bit4 fire), folded into puerto_especial_joystick like real
// joystick hardware on PSG R14.
void input_set_joystick_value(int value);
int input_get_joystick_value(void);
// Type text through the matrix (autostart RUN"CAS:).
void input_type_text(const char *text);
void input_type_text_delayed(const char *text, unsigned wait_frames);
void input_pump_typer(void);
// Full cassette autostart script: boot to BASIC Ready, then RUN"CAS:.
void input_type_autostart_cas(void);
// Per-frame: drain USB queues, pump typer, handle F12 menu.
void poll_input(void);

}  // namespace msx
}  // namespace bmx

#endif
