//
// a2600_input.cpp
//
// See a2600_input.h.
//

#include "a2600_input.h"
#include "a2600_config.h"

#include "bspf.hxx"
#include "OSystem.hxx"
#include "EventHandler.hxx"
#include "Event.hxx"
#include "Console.hxx"
#include "Switches.hxx"
#include "M6532.hxx"

// KEYCODE_* macros only (no keycode functions are called from this TU,
// so the C-linkage functions declared here are never referenced).
#include "third_party/common/keycodes.h"

extern "C" {
#include "third_party/common/emux_api.h"
}

namespace bmx {
namespace a2600 {

namespace {

// VICE-style 5-bit stick state per player.
int s_pad[2] = {0, 0};
int s_select_pulse = 0;
int s_reset_pulse = 0;
// User-mappable console-switch shortcuts (Keyboard menu).
long s_select_key = kDefaultSelectKey;
long s_reset_key = kDefaultResetKey;

void send_stick(EventHandler &events, bool left, int v) {
  if (left) {
    events.handleEvent(Event::LeftJoystickUp, (v & 0x01) ? 1 : 0);
    events.handleEvent(Event::LeftJoystickDown, (v & 0x02) ? 1 : 0);
    events.handleEvent(Event::LeftJoystickLeft, (v & 0x04) ? 1 : 0);
    events.handleEvent(Event::LeftJoystickRight, (v & 0x08) ? 1 : 0);
    events.handleEvent(Event::LeftJoystickFire, (v & 0x10) ? 1 : 0);
  } else {
    events.handleEvent(Event::RightJoystickUp, (v & 0x01) ? 1 : 0);
    events.handleEvent(Event::RightJoystickDown, (v & 0x02) ? 1 : 0);
    events.handleEvent(Event::RightJoystickLeft, (v & 0x04) ? 1 : 0);
    events.handleEvent(Event::RightJoystickRight, (v & 0x08) ? 1 : 0);
    events.handleEvent(Event::RightJoystickFire, (v & 0x10) ? 1 : 0);
  }
}

}  // namespace

void input_press_select(void) { s_select_pulse = 4; }
void input_press_reset(void) { s_reset_pulse = 4; }

long input_get_select_key(void) { return s_select_key; }
long input_get_reset_key(void) { return s_reset_key; }
void input_set_select_key(long keycode) { s_select_key = keycode; }
void input_set_reset_key(long keycode) { s_reset_key = keycode; }

void input_queue_depths(int *key_depth, int *joy_depth) {
  if (key_depth != nullptr) {
    *key_depth = pending_emu_key.tail - pending_emu_key.head;
  }
  if (joy_depth != nullptr) {
    *joy_depth = pending_emu_joy.tail - pending_emu_joy.head;
  }
}

namespace {

unsigned long s_key_events = 0;
long s_last_key = 0;

}  // namespace

void input_key_stats(unsigned long *total, long *last_key) {
  if (total != nullptr) {
    *total = s_key_events;
  }
  if (last_key != nullptr) {
    *last_key = s_last_key;
  }
}

void input_pump_frame(void *osystem_ptr) {
  // The 2600 has no keyboard matrix: consume the framework key queue every
  // frame (even with no cartridge) so it can never fill up and so stale
  // presses never linger. BMX console/menu keys (F1-F12) are owned by the
  // common menu layer, not the core; F12 in particular only ever toggles
  // the menu, so it must never inject a console pulse.
  while (pending_emu_key.head != pending_emu_key.tail) {
    int i = pending_emu_key.head & 0xf;
    long key = pending_emu_key.key[i];
    int pressed = pending_emu_key.pressed[i];
    pending_emu_key.head++;
    s_last_key = key;
    s_key_events++;
    if (pressed && key != KEYCODE_F12) {  // menu toggle is not a switch
      if (key == s_select_key) {
        s_select_pulse = 4;
      } else if (key == s_reset_key) {
        s_reset_pulse = 4;
      }
    }
  }
  // Latch sticks even with no cartridge so the joy queue can never fill
  // up either; the latched state applies once a console exists.
  while (pending_emu_joy.head != pending_emu_joy.tail) {
    int i = pending_emu_joy.head & 0x7f;
    int port = pending_emu_joy.port[i];
    int value = pending_emu_joy.value[i] & 0x1f;
    int type = pending_emu_joy.type[i];
    pending_emu_joy.head++;
    int player = (port <= 1) ? 0 : 1;
    if (type == PENDING_EMU_JOY_TYPE_ABSOLUTE) {
      s_pad[player] = value;
    } else if (type == PENDING_EMU_JOY_TYPE_AND) {
      s_pad[player] &= value;
    } else {
      s_pad[player] |= value;
    }
  }
  OSystem *osystem = static_cast<OSystem *>(osystem_ptr);
  if (osystem == nullptr || !osystem->hasConsole()) {
    return;
  }
  EventHandler &events = osystem->eventHandler();
  send_stick(events, true, s_pad[0]);
  send_stick(events, false, s_pad[1]);
  events.handleEvent(Event::ConsoleSelect, s_select_pulse > 0 ? 1 : 0);
  events.handleEvent(Event::ConsoleReset, s_reset_pulse > 0 ? 1 : 0);
  if (s_select_pulse > 0) {
    s_select_pulse--;
  }
  if (s_reset_pulse > 0) {
    s_reset_pulse--;
  }
  Console &console = osystem->console();
  console.switches().setLeftDifficultyA(config_get_left_difficulty_a());
  console.switches().setRightDifficultyA(config_get_right_difficulty_a());
  events.pollInput();
  console.riot().update();
}

}  // namespace a2600
}  // namespace bmx

extern "C" {

// No mouse on a 2600: satisfy the common mouse_input.o link.
void emu_mouse_move(int x, int y) {
  (void)x;
  (void)y;
}

void emu_mouse_button_left(int pressed) { (void)pressed; }
void emu_mouse_button_right(int pressed) { (void)pressed; }
void emu_mouse_button_middle(int pressed) { (void)pressed; }
void emu_mouse_wheel_up(int pressed) { (void)pressed; }
void emu_mouse_wheel_down(int pressed) { (void)pressed; }

}  // extern "C"
