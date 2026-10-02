//
// a2600_input.h
//
// USB gamepad -> Stella joystick events. Two players from the Joyports
// port mapping (port 1 = left, anything else = right). Console Select /
// Reset arrive as short pulses triggered from menu buttons or from the
// user-mappable keyboard shortcuts (Keyboard menu, defaults Space/Enter).
//

#ifndef a2600_input_h
#define a2600_input_h

namespace bmx {
namespace a2600 {

// Default console-switch keys (USB HID usages, see keycodes.h).
// Select = Space, Reset = Enter.
const long kDefaultSelectKey = 0x2c;
const long kDefaultResetKey = 0x28;

// Drain pending framework joystick queue + push one frame of Stella
// events. Needs the live OSystem (event handler + console switches).
void input_pump_frame(void *osystem);

// Momentary console buttons (consumed over the next few frames).
void input_press_select(void);
void input_press_reset(void);

// Keyboard shortcuts for the console switches (persisted in
// settings-a2600.txt). A press injects the same pulse as the menu buttons.
long input_get_select_key(void);
long input_get_reset_key(void);
void input_set_select_key(long keycode);
void input_set_reset_key(long keycode);

// Queue depths for the debug heartbeat (key queue is drained every frame;
// the 2600 has no keyboard, so a growing depth means the pump starved).
void input_queue_depths(int *key_depth, int *joy_depth);

// Lifetime USB key event count + most recent keycode seen by the drain.
// If this never grows while keys are pressed, USB delivery is dead.
void input_key_stats(unsigned long *total, long *last_key);

}  // namespace a2600
}  // namespace bmx

#endif
