//
// apple2_input.h
//
// USB keyboard -> ADB key events, USB joystick -> paddle positions,
// USB mouse -> ADB mouse. Queues (pending_emu_key/joy) are drained once
// per frame in poll_input().
//

#ifndef apple2_input_h
#define apple2_input_h

namespace bmx {
namespace apple2 {

void refresh_keyboard_matrix(void);
void input_set_joystick(int mode);
int input_get_joystick(void);
// Queue ADB typing (frame-paced, e.g. the PR#6 boot command). A Return is
// appended by the caller when wanted (pass it explicitly).
void input_type_text(const char *text);
// True while typer events remain queued.
bool input_typer_busy(void);
// Physical-key activity probe for autostart (consumes the flag).
bool apple2_user_key_seen(void);
void apple2_user_key_reset(void);
void poll_input(void);

}  // namespace apple2
}  // namespace bmx

#endif
