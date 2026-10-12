//
// apple2_input.cpp
//
// See apple2_input.h. ADB codes are the standard Apple ADB set (same as
// the GSplus SDL driver's scancode table); USB HID arrives through
// pending_emu_key with SDL-compatible usage IDs (see keycodes.h).
//
// Joystick: BMX 5-bit latch (0x01 up, 0x02 down, 0x04 left, 0x08 right,
// 0x10 fire1, 0x20 fire2) drives the paddle positions (0..65535,
// center 32767) via joystick_callback_update, like a real analog stick
// pinned to center/rails.
//

#include "apple2_input.h"
#include "apple2_debug.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

extern "C" {
#include "third_party/common/emux_api.h"
#include "third_party/common/keycodes.h"
#include "third_party/common/ui.h"
#include "third_party/common/circle.h"
}

// KEGS core globals (global-scope externs; see the header).
#include "apple2_kegs_globals.h"

extern "C" void joystick_callback_update(word32 buttons, int paddle_x,
                                        int paddle_y);

namespace bmx {
namespace apple2 {

// True if a physical USB key arrived since the last read (consumes).
// The autostart typer sends ADB events directly, never through here.
bool s_user_key_seen = false;

namespace {

// HID usage -> ADB keycode. -1 = no equivalent.
int hid_to_adb(long hid) {
  switch (hid) {
    case KEYCODE_a: return 0x00;
    case KEYCODE_b: return 0x0b;
    case KEYCODE_c: return 0x08;
    case KEYCODE_d: return 0x02;
    case KEYCODE_e: return 0x0e;
    case KEYCODE_f: return 0x03;
    case KEYCODE_g: return 0x05;
    case KEYCODE_h: return 0x04;
    case KEYCODE_i: return 0x22;
    case KEYCODE_j: return 0x26;
    case KEYCODE_k: return 0x28;
    case KEYCODE_l: return 0x25;
    case KEYCODE_m: return 0x2e;
    case KEYCODE_n: return 0x2d;
    case KEYCODE_o: return 0x1f;
    case KEYCODE_p: return 0x23;
    case KEYCODE_q: return 0x0c;
    case KEYCODE_r: return 0x0f;
    case KEYCODE_s: return 0x01;
    case KEYCODE_t: return 0x11;
    case KEYCODE_u: return 0x20;
    case KEYCODE_v: return 0x09;
    case KEYCODE_w: return 0x0d;
    case KEYCODE_x: return 0x07;
    case KEYCODE_y: return 0x10;
    case KEYCODE_z: return 0x06;
    case KEYCODE_1: return 0x12;
    case KEYCODE_2: return 0x13;
    case KEYCODE_3: return 0x14;
    case KEYCODE_4: return 0x15;
    case KEYCODE_5: return 0x17;
    case KEYCODE_6: return 0x16;
    case KEYCODE_7: return 0x1a;
    case KEYCODE_8: return 0x1c;
    case KEYCODE_9: return 0x19;
    case KEYCODE_0: return 0x1d;
    case KEYCODE_Dash: return 0x1b;
    case KEYCODE_Equals: return 0x18;
    case KEYCODE_LeftBracket: return 0x21;
    case KEYCODE_RightBracket: return 0x1e;
    case KEYCODE_BackSlash: return 0x2a;
    case KEYCODE_SemiColon: return 0x29;
    case KEYCODE_SingleQuote: return 0x27;
    case KEYCODE_BackQuote:
      return 0x32;
    case KEYCODE_Comma: return 0x2b;
    case KEYCODE_Period: return 0x2f;
    case KEYCODE_Slash: return 0x2c;
    case KEYCODE_Space: return 0x31;
    case KEYCODE_Return: return 0x24;
    case KEYCODE_KP_Enter: return 0x24;
    case KEYCODE_Tab: return 0x30;
    case KEYCODE_Escape: return 0x35;
    case KEYCODE_Backspace: return 0x33;
    case KEYCODE_Delete: return 0x75;
    case KEYCODE_LeftShift: return 0x38;
    case KEYCODE_RightShift: return 0x38;
    case KEYCODE_LeftControl: return 0x36;
    case KEYCODE_RightControl: return 0x36;
    case KEYCODE_LeftAlt: return 0x3a;    // Option
    case KEYCODE_RightAlt: return 0x3a;   // Option
    case KEYCODE_LeftSuper: return 0x37;   // Open-Apple
    case KEYCODE_RightSuper: return 0x37;  // Open-Apple
    case KEYCODE_CapsLock: return 0x39;
    case KEYCODE_Up: return 0x3e;
    case KEYCODE_Down: return 0x3d;
    case KEYCODE_Left: return 0x3b;
    case KEYCODE_Right: return 0x3c;
    case KEYCODE_Home: return 0x73;
    case KEYCODE_End: return 0x77;
    case KEYCODE_PageUp: return 0x74;
    case KEYCODE_PageDown: return 0x79;
    case KEYCODE_Insert: return 0x72;
    case KEYCODE_F1: return 0x7a;
    case KEYCODE_F2: return 0x78;
    case KEYCODE_F3: return 0x63;
    case KEYCODE_F4: return 0x76;
    default: return -1;
  }
}

void drain_keys(void) {
  while (pending_emu_key.head != pending_emu_key.tail) {
    int i = pending_emu_key.head & 0x0f;
    long hid = pending_emu_key.key[i];
    int pressed = pending_emu_key.pressed[i];
    pending_emu_key.head++;
    s_user_key_seen = true;
    int adb = hid_to_adb(hid);
    if (adb < 0) {
      continue;
    }
    {
      static bool logged = false;
      if (!logged) {
        logged = true;
        debug_log("input: first key hid=%ld adb=%02x %s", hid, adb,
                  pressed ? "down" : "up");
      }
    }
    adb_key_event(adb, pressed ? 0 : 1);
  }
}

int s_joystick_mode = 1;  // joystick 0 default (most games)
int s_joy_latch = 0;

void drain_joy(void) {
  while (pending_emu_joy.head != pending_emu_joy.tail) {
    int i = pending_emu_joy.head & 0x7f;
    int value = pending_emu_joy.value[i];
    int type = pending_emu_joy.type[i];
    int port = pending_emu_joy.port[i];
    pending_emu_joy.head++;
    if (port != 1 && port != 2) {
      continue;
    }
    if (s_joystick_mode == 1 && port != 1) {
      continue;
    }
    if (s_joystick_mode == 2 && port != 2) {
      continue;
    }
    int bits = value & 0x3f;
    switch (type) {
      case PENDING_EMU_JOY_TYPE_ABSOLUTE:
        s_joy_latch = bits;
        break;
      case PENDING_EMU_JOY_TYPE_AND:
        s_joy_latch &= bits;
        break;
      case PENDING_EMU_JOY_TYPE_OR:
        s_joy_latch |= bits;
        break;
      default:
        break;
    }
  }
}

void push_paddles(void) {
  if (s_joystick_mode == 0) {
    return;
  }
  const int latch = s_joy_latch;
  int px = 32767;
  int py = 32767;
  if (latch & 0x08) {
    px = 65535;  // right
  } else if (latch & 0x04) {
    px = 0;  // left
  }
  if (latch & 0x02) {
    py = 65535;  // down
  } else if (latch & 0x01) {
    py = 0;  // up
  }
  word32 buttons = 0;
  if (latch & 0x10) {
    buttons |= 1;  // fire 1 -> button 0
  }
  if (latch & 0x20) {
    buttons |= 2;  // fire 2 -> button 1
  }
  {
    static bool logged = false;
    if (!logged) {
      logged = true;
      debug_log("input: first paddle buttons=%lu x=%d y=%d mode=%d",
                (unsigned long)buttons, px, py, s_joystick_mode);
    }
  }
  joystick_callback_update(buttons, px, py);
}

// USB mouse accumulation (fed by the emu_mouse_* seams below).
int s_mouse_x = 352;
int s_mouse_y = 231;
int s_mouse_buttons = 0;
bool s_mouse_moved = false;

void push_mouse(void) {
  if (!s_mouse_moved) {
    return;
  }
  s_mouse_moved = false;
  if (s_mouse_x < 0) {
    s_mouse_x = 0;
  }
  if (s_mouse_x > 703) {
    s_mouse_x = 703;
  }
  if (s_mouse_y < 0) {
    s_mouse_y = 0;
  }
  if (s_mouse_y > 461) {
    s_mouse_y = 461;
  }
  {
    static bool logged = false;
    if (!logged) {
      logged = true;
      debug_log("input: first mouse x=%d y=%d buttons=%d", s_mouse_x,
                s_mouse_y, s_mouse_buttons);
    }
  }
  adb_update_mouse(&g_mainwin_kimage, s_mouse_x, s_mouse_y, s_mouse_buttons,
                   1);
}

}  // namespace

bool apple2_user_key_seen(void) {
  bool v = s_user_key_seen;
  s_user_key_seen = false;
  return v;
}

void apple2_user_key_reset(void) { s_user_key_seen = false; }

void refresh_keyboard_matrix(void) {}

void input_set_joystick(int mode) {
  if (mode < 0 || mode > 2) {
    mode = 0;
  }
  s_joystick_mode = mode;
  debug_log("input: joystick mode %d", mode);
}

int input_get_joystick(void) { return s_joystick_mode; }

// ---- Scripted typer (ADB press/release events, frame-paced) -------------
struct TypeEvent {
  int adb;
  bool pressed;
};
TypeEvent s_type[256];
unsigned s_type_head = 0;
unsigned s_type_tail = 0;
unsigned s_type_wait = 0;

bool type_emit(int adb, bool pressed) {
  unsigned next = (s_type_tail + 1) & 0xff;
  if (next == (s_type_head & 0xff)) {
    return false;
  }
  s_type[s_type_tail & 0xff].adb = adb;
  s_type[s_type_tail & 0xff].pressed = pressed;
  s_type_tail++;
  return true;
}

void type_wait_frames(unsigned n) {
  for (unsigned i = 0; i < n; i++) {
    type_emit(-1, false);
  }
}

void type_code(int adb) {
  type_emit(adb, true);
  type_wait_frames(3);
  type_emit(adb, false);
  type_wait_frames(3);
}

// Printable ASCII -> ADB + shift. Returns false for untypable chars.
bool type_char(char c, bool *shift) {
  *shift = false;
  if (c >= 'a' && c <= 'z') {
    c = (char)(c - 'a' + 'A');
  }
  int adb = -1;
  switch (c) {
    case 'A': adb = 0x00; break;
    case 'B': adb = 0x0b; break;
    case 'C': adb = 0x08; break;
    case 'D': adb = 0x02; break;
    case 'E': adb = 0x0e; break;
    case 'F': adb = 0x03; break;
    case 'G': adb = 0x05; break;
    case 'H': adb = 0x04; break;
    case 'I': adb = 0x22; break;
    case 'J': adb = 0x26; break;
    case 'K': adb = 0x28; break;
    case 'L': adb = 0x25; break;
    case 'M': adb = 0x2e; break;
    case 'N': adb = 0x2d; break;
    case 'O': adb = 0x1f; break;
    case 'P': adb = 0x23; break;
    case 'Q': adb = 0x0c; break;
    case 'R': adb = 0x0f; break;
    case 'S': adb = 0x01; break;
    case 'T': adb = 0x11; break;
    case 'U': adb = 0x20; break;
    case 'V': adb = 0x09; break;
    case 'W': adb = 0x0d; break;
    case 'X': adb = 0x07; break;
    case 'Y': adb = 0x10; break;
    case 'Z': adb = 0x06; break;
    case '0': adb = 0x1d; break;
    case '1': adb = 0x12; break;
    case '2': adb = 0x13; break;
    case '3': adb = 0x14; break;
    case '4': adb = 0x15; break;
    case '5': adb = 0x17; break;
    case '6': adb = 0x16; break;
    case '7': adb = 0x1a; break;
    case '8': adb = 0x1c; break;
    case '9': adb = 0x19; break;
    case ' ': adb = 0x31; break;
    case '\n':
    case '\r': adb = 0x24; break;
    case '#': adb = 0x14; *shift = true; break;  // shift+3
    case '$': adb = 0x15; *shift = true; break;  // shift+4
    case '(': adb = 0x19; *shift = true; break;  // shift+9
    case ')': adb = 0x1d; *shift = true; break;  // shift+0
    case '+': adb = 0x18; *shift = true; break;  // shift+=
    case '-': adb = 0x1b; break;
    case '=': adb = 0x18; break;
    case ',': adb = 0x2b; break;
    case '.': adb = 0x2f; break;
    case '/': adb = 0x2c; break;
    default: return false;
  }
  if (*shift) {
    type_emit(0x38, true);
    type_wait_frames(2);
  }
  type_code(adb);
  if (*shift) {
    type_emit(0x38, false);
    type_wait_frames(2);
  }
  return true;
}

void input_type_text(const char *text) {
  s_type_head = s_type_tail = 0;
  s_type_wait = 0;
  if (text == nullptr) {
    return;
  }
  for (const char *p = text; *p != '\0'; p++) {
    bool s = false;
    type_char(*p, &s);
  }
  debug_log("input: typer queued '%s'", text);
}

bool input_typer_busy(void) { return s_type_head != s_type_tail; }

void input_pump_typer(void) {
  if (s_type_wait > 0) {
    s_type_wait--;
    return;
  }
  if (s_type_head == s_type_tail) {
    return;
  }
  // One event per frame keeps the 60 Hz ADB firmware debounce happy.
  TypeEvent ev = s_type[s_type_head & 0xff];
  s_type_head++;
  if (ev.adb < 0) {
    s_type_wait = 2;
    return;
  }
  adb_key_event(ev.adb, ev.pressed ? 0 : 1);
}

void poll_input(void) {
  drain_keys();
  drain_joy();
  push_paddles();
  push_mouse();
  input_pump_typer();
}

}  // namespace apple2
}  // namespace bmx

// Mouse driver seams (mirrors amiga_input.cpp): the common USB mouse code
// calls these with deltas/buttons; poll_input() forwards them to ADB.
extern "C" {

void emu_mouse_move(int x, int y) {
  ::bmx::apple2::s_mouse_x += x;
  ::bmx::apple2::s_mouse_y += y;
  ::bmx::apple2::s_mouse_moved = true;
}

void emu_mouse_button_left(int pressed) {
  if (pressed) {
    ::bmx::apple2::s_mouse_buttons |= 0x01;
  } else {
    ::bmx::apple2::s_mouse_buttons &= ~0x01;
  }
  ::bmx::apple2::s_mouse_moved = true;
}

void emu_mouse_button_right(int pressed) {
  if (pressed) {
    ::bmx::apple2::s_mouse_buttons |= 0x02;
  } else {
    ::bmx::apple2::s_mouse_buttons &= ~0x02;
  }
  ::bmx::apple2::s_mouse_moved = true;
}

void emu_mouse_button_middle(int pressed) {
  if (pressed) {
    ::bmx::apple2::s_mouse_buttons |= 0x04;
  } else {
    ::bmx::apple2::s_mouse_buttons &= ~0x04;
  }
  ::bmx::apple2::s_mouse_moved = true;
}

void emu_mouse_wheel_up(int pressed) {
  (void)pressed;
}

void emu_mouse_wheel_down(int pressed) {
  (void)pressed;
}

}  // extern "C"
