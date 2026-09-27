//
// atarist_input.h
//
// AtariST input (keyboard/mouse/joystick) for BMX bare-metal
// (Phase 0 placeholder).
//

#ifndef atarist_input_h
#define atarist_input_h

namespace bmx {
namespace atarist {

// Phase 0 placeholder input state. USB kbd -> IKBD, mouse/joy land in
// Phase 3.
struct atarist_input_t {
  int unused;
};

// Polls BMX queued input events and fills the placeholder input struct.
void poll_input(atarist_input_t *input);

// Joystick state for the JoyUI backend (atarist_bmx_platform.c):
// ST stick id (0/1) -> digital X/Y (-20000/0/+20000) + button bitmask
// (bit0 = fire). BMX port 1 feeds ST stick 1, BMX port 2 feeds ST stick 0.
void atarist_joy_state(int st_id, int *x, int *y, int *buttons);

// Phase 0 mouse accumulator (menu mouse layer). Drained by the real
// poll_input in Phase 3; until then values just accumulate so the menu
// mouse layer links.
extern int s_mouse_dx;
extern int s_mouse_dy;
extern int s_mouse_buttons;

}  // namespace atarist
}  // namespace bmx

#endif
