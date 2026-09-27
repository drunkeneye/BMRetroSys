//
// amiga_input.h
//
// Amiga input (keyboard/mouse/joystick) for BMX bare-metal.
//

#ifndef amiga_input_h
#define amiga_input_h

namespace bmx {
namespace amiga {

struct amiga_input_t {
  int unused;
};

void poll_input(amiga_input_t *input);

// C-linkage mouse accumulators feed these (mirrors atarist_input.h).
extern int s_mouse_dx;
extern int s_mouse_dy;
extern int s_mouse_buttons;

}  // namespace amiga
}  // namespace bmx

#endif
