//
// zx_input.h
//
// ZX Spectrum input for BMX bare-metal: USB keyboard (HID) -> ZX matrix,
// USB sticks/pads -> Kempston/Sinclair/Cursor via puerto_especial_joystick,
// autostart typer, and the C mouse hooks mouse_input.cpp expects.
//

#ifndef zx_input_h
#define zx_input_h

namespace bmx {
namespace zxspectrum {

// Drain pending_emu_key/joy queues into the matrix/joystick state.
void poll_input(void);
// Refresh the ZEsarUX matrix ports from the latched state (also installed
// as scr_actualiza_tablas_teclado).
void refresh_keyboard_matrix(void);

// Joystick mode: 0=None, 1=Kempston, 2=Sinclair 1, 3=Sinclair 2, 4=Cursor.
void input_set_joystick(int mode);
int input_get_joystick(void);

// Autostart typer: type a ZX text (supports '"', ':', Enter=\n).
// The delayed form holds `wait_frames` before the first key: autostart
// after a reset needs ~3s for the ROM (esp. the 128k RAM test) to reach
// BASIC; the plain form keeps the ~2s menu-close lead-in.
void input_type_text(const char *text);
void input_type_text_delayed(const char *text, unsigned wait_frames);
void input_pump_typer(void);

}  // namespace zxspectrum
}  // namespace bmx

#endif
