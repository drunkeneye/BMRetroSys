//
// bbc_input.h
//
// USB HID -> CLK Inputs::Keyboard::Key, BMX 5-bit latch -> BBC analogue
// joysticks, plus a frame-paced scripted typer (drives keyboard()).
//

#ifndef BMX_BBC_INPUT_H
#define BMX_BBC_INPUT_H

namespace bmx {
namespace bbc {

void poll_input(void);
void input_set_joystick(int mode);
int input_get_joystick(void);

void input_type_text(const char *text);
bool input_typer_busy(void);
void input_pump_typer(void);

bool bbc_user_key_seen(void);
void bbc_user_key_reset(void);
void refresh_keyboard_matrix(void);

}  // namespace bbc
}  // namespace bmx

#endif  // BMX_BBC_INPUT_H
