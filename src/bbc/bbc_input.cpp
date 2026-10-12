//
// bbc_input.cpp
//
// See bbc_input.h. USB HID arrives through pending_emu_key with
// SDL-compatible usage IDs (see keycodes.h) and is forwarded to the CLK
// keyboard (MappedKeyboardMachine maps to the BBC matrix). The BMX 5-bit
// joystick latch drives the BBC analogue sticks (float 0..1 rails).
//

#include "bbc_input.h"
#include "bbc_debug.h"
#include "bbc_machine.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>

extern "C" {
#include "third_party/common/emux_api.h"
#include "third_party/common/keycodes.h"
#include "third_party/common/ui.h"
#include "third_party/common/circle.h"
}

// CLK core (vendored subset under third_party/bbc).
#include "Inputs/Joystick.hpp"
#include "Inputs/Keyboard.hpp"
#include "Machines/Acorn/BBCMicro/BBCMicro.hpp"
#include "Machines/JoystickMachine.hpp"
#include "Machines/KeyboardMachine.hpp"

namespace bmx {
namespace bbc {

namespace clk {
BBCMicro::Machine *machine_ptr(void);
}  // namespace clk

// True if a physical USB key arrived since the last read (consumes).
// The autostart typer sends key events directly, never through here.
bool s_user_key_seen = false;

namespace {

Inputs::Keyboard *clk_keyboard(void) {
  BBCMicro::Machine *m = clk::machine_ptr();
  if (m == nullptr) {
    return nullptr;
  }
  return &dynamic_cast<MachineTypes::MappedKeyboardMachine *>(m)->keyboard();
}

// HID usage -> CLK key. KEY_UNKNOWN = no equivalent.
Inputs::Keyboard::Key hid_to_clkkey(long hid) {
  using K = Inputs::Keyboard::Key;
  switch (hid) {
    case KEYCODE_a: return K::A;
    case KEYCODE_b: return K::B;
    case KEYCODE_c: return K::C;
    case KEYCODE_d: return K::D;
    case KEYCODE_e: return K::E;
    case KEYCODE_f: return K::F;
    case KEYCODE_g: return K::G;
    case KEYCODE_h: return K::H;
    case KEYCODE_i: return K::I;
    case KEYCODE_j: return K::J;
    case KEYCODE_k: return K::K;
    case KEYCODE_l: return K::L;
    case KEYCODE_m: return K::M;
    case KEYCODE_n: return K::N;
    case KEYCODE_o: return K::O;
    case KEYCODE_p: return K::P;
    case KEYCODE_q: return K::Q;
    case KEYCODE_r: return K::R;
    case KEYCODE_s: return K::S;
    case KEYCODE_t: return K::T;
    case KEYCODE_u: return K::U;
    case KEYCODE_v: return K::V;
    case KEYCODE_w: return K::W;
    case KEYCODE_x: return K::X;
    case KEYCODE_y: return K::Y;
    case KEYCODE_z: return K::Z;
    case KEYCODE_1: return K::k1;
    case KEYCODE_2: return K::k2;
    case KEYCODE_3: return K::k3;
    case KEYCODE_4: return K::k4;
    case KEYCODE_5: return K::k5;
    case KEYCODE_6: return K::k6;
    case KEYCODE_7: return K::k7;
    case KEYCODE_8: return K::k8;
    case KEYCODE_9: return K::k9;
    case KEYCODE_0: return K::k0;
    case KEYCODE_Dash: return K::Hyphen;
    case KEYCODE_Equals: return K::Equals;
    case KEYCODE_LeftBracket: return K::OpenSquareBracket;
    case KEYCODE_RightBracket: return K::CloseSquareBracket;
    case KEYCODE_BackSlash: return K::Backslash;
    case KEYCODE_SemiColon: return K::Semicolon;
    case KEYCODE_SingleQuote: return K::Quote;
    case KEYCODE_BackQuote: return K::BackTick;
    case KEYCODE_Comma: return K::Comma;
    case KEYCODE_Period: return K::FullStop;
    case KEYCODE_Slash: return K::ForwardSlash;
    case KEYCODE_Space: return K::Space;
    case KEYCODE_Return: return K::Enter;
    case KEYCODE_KP_Enter: return K::KeypadEnter;
    case KEYCODE_Tab: return K::Tab;
    case KEYCODE_Escape: return K::Escape;
    case KEYCODE_Backspace: return K::Backspace;
    case KEYCODE_Delete: return K::Delete;
    case KEYCODE_LeftShift: return K::LeftShift;
    case KEYCODE_RightShift: return K::RightShift;
    case KEYCODE_LeftControl: return K::LeftControl;
    case KEYCODE_RightControl: return K::RightControl;
    case KEYCODE_CapsLock: return K::CapsLock;
    case KEYCODE_Up: return K::Up;
    case KEYCODE_Down: return K::Down;
    case KEYCODE_Left: return K::Left;
    case KEYCODE_Right: return K::Right;
    case KEYCODE_Home: return K::Home;
    case KEYCODE_End: return K::End;
    case KEYCODE_PageUp: return K::PageUp;
    case KEYCODE_PageDown: return K::PageDown;
    case KEYCODE_Insert: return K::Insert;
    // BBC red function keys F0-F9 on USB F1-F10.
    case KEYCODE_F1: return K::F1;
    case KEYCODE_F2: return K::F2;
    case KEYCODE_F3: return K::F3;
    case KEYCODE_F4: return K::F4;
    case KEYCODE_F5: return K::F5;
    case KEYCODE_F6: return K::F6;
    case KEYCODE_F7: return K::F7;
    case KEYCODE_F8: return K::F8;
    case KEYCODE_F9: return K::F9;
    case KEYCODE_F10: return K::F10;
    default: return K::Max;
  }
}

void drain_keys(void) {
  Inputs::Keyboard *kbd = clk_keyboard();
  while (pending_emu_key.head != pending_emu_key.tail) {
    int i = pending_emu_key.head & 0x0f;
    long hid = pending_emu_key.key[i];
    int pressed = pending_emu_key.pressed[i];
    pending_emu_key.head++;
    s_user_key_seen = true;
    // Arrival log: first 64 distinct HIDs with their CLK mapping, so a
    // dead key's arrival value is visible in one boot.
    {
      static long seen[64];
      static int nseen = 0;
      bool known = false;
      for (int k = 0; k < nseen; k++) {
        if (seen[k] == hid) {
          known = true;
          break;
        }
      }
      if (!known && nseen < 64) {
        seen[nseen++] = hid;
        Inputs::Keyboard::Key probe = hid_to_clkkey(hid);
        debug_log("input: hid=%ld -> clk=%d %s", hid, (int)probe,
                  pressed ? "down" : "up");
      }
    }
    if (kbd == nullptr) {
      continue;
    }
    Inputs::Keyboard::Key key = hid_to_clkkey(hid);
    if (key == Inputs::Keyboard::Key::Max) {
      continue;
    }
    {
      static bool logged = false;
      if (!logged) {
        logged = true;
        debug_log("input: first key hid=%ld clk=%d %s", hid, (int)key,
                  pressed ? "down" : "up");
      }
    }
    kbd->set_key_pressed(key, 0, pressed != 0, false);
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

void push_sticks(void) {
  if (s_joystick_mode == 0) {
    return;
  }
  BBCMicro::Machine *m = clk::machine_ptr();
  if (m == nullptr) {
    return;
  }
  const auto &sticks =
      dynamic_cast<MachineTypes::JoystickMachine *>(m)->get_joysticks();
  const size_t idx = (size_t)(s_joystick_mode - 1);
  if (idx >= sticks.size() || sticks[idx] == nullptr) {
    return;
  }
  const int latch = s_joy_latch;
  float hx = 0.5f, vy = 0.5f;
  if (latch & 0x08) {
    hx = 1.0f;  // right
  } else if (latch & 0x04) {
    hx = 0.0f;  // left
  }
  if (latch & 0x02) {
    vy = 1.0f;  // down
  } else if (latch & 0x01) {
    vy = 0.0f;  // up
  }
  {
    static bool logged = false;
    if (!logged) {
      logged = true;
      debug_log("input: first stick latch=%02x hx=%.1f vy=%.1f mode=%d",
                latch, (double)hx, (double)vy, s_joystick_mode);
    }
  }
  sticks[idx]->set_input(Inputs::Joystick::Input(
                             Inputs::Joystick::Input::Horizontal),
                         hx);
  sticks[idx]->set_input(Inputs::Joystick::Input(
                             Inputs::Joystick::Input::Vertical),
                         vy);
  sticks[idx]->set_input(
      Inputs::Joystick::Input(Inputs::Joystick::Input::Fire),
      (latch & 0x10) != 0);
}

// ---- Scripted typer (CLK key press/release, frame-paced) ------------------
struct TypeEvent {
  Inputs::Keyboard::Key key;
  bool pressed;
  bool wait;  // padding event (debounce gap)
};
TypeEvent s_type[256];
unsigned s_type_head = 0;
unsigned s_type_tail = 0;
unsigned s_type_wait = 0;

bool type_emit(Inputs::Keyboard::Key key, bool pressed) {
  unsigned next = (s_type_tail + 1) & 0xff;
  if (next == (s_type_head & 0xff)) {
    return false;
  }
  s_type[s_type_tail & 0xff].key = key;
  s_type[s_type_tail & 0xff].pressed = pressed;
  s_type[s_type_tail & 0xff].wait = false;
  s_type_tail++;
  return true;
}

void type_wait_frames(unsigned n) {
  for (unsigned i = 0; i < n; i++) {
    unsigned next = (s_type_tail + 1) & 0xff;
    if (next == (s_type_head & 0xff)) {
      return;
    }
    s_type[s_type_tail & 0xff].wait = true;
    s_type_tail++;
  }
}

void type_code(Inputs::Keyboard::Key key) {
  type_emit(key, true);
  type_wait_frames(3);
  type_emit(key, false);
  type_wait_frames(3);
}

// Printable ASCII -> CLK key + shift. Returns false for untypable chars.
bool type_char(char c, bool *shift) {
  using K = Inputs::Keyboard::Key;
  *shift = false;
  if (c >= 'a' && c <= 'z') {
    c = (char)(c - 'a' + 'A');
  }
  K key = K::Max;
  switch (c) {
    case 'A': key = K::A; break;
    case 'B': key = K::B; break;
    case 'C': key = K::C; break;
    case 'D': key = K::D; break;
    case 'E': key = K::E; break;
    case 'F': key = K::F; break;
    case 'G': key = K::G; break;
    case 'H': key = K::H; break;
    case 'I': key = K::I; break;
    case 'J': key = K::J; break;
    case 'K': key = K::K; break;
    case 'L': key = K::L; break;
    case 'M': key = K::M; break;
    case 'N': key = K::N; break;
    case 'O': key = K::O; break;
    case 'P': key = K::P; break;
    case 'Q': key = K::Q; break;
    case 'R': key = K::R; break;
    case 'S': key = K::S; break;
    case 'T': key = K::T; break;
    case 'U': key = K::U; break;
    case 'V': key = K::V; break;
    case 'W': key = K::W; break;
    case 'X': key = K::X; break;
    case 'Y': key = K::Y; break;
    case 'Z': key = K::Z; break;
    case '0': key = K::k0; break;
    case '1': key = K::k1; break;
    case '2': key = K::k2; break;
    case '3': key = K::k3; break;
    case '4': key = K::k4; break;
    case '5': key = K::k5; break;
    case '6': key = K::k6; break;
    case '7': key = K::k7; break;
    case '8': key = K::k8; break;
    case '9': key = K::k9; break;
    case ' ': key = K::Space; break;
    case '\n':
    case '\r': key = K::Enter; break;
    case '*': key = K::k8; *shift = true; break;  // BBC * commands
    case '.': key = K::FullStop; break;
    case '!': key = K::k1; *shift = true; break;
    case '"': key = K::k2; *shift = true; break;
    case '-': key = K::Hyphen; break;
    case '=': key = K::Equals; break;
    case ',': key = K::Comma; break;
    case '/': key = K::ForwardSlash; break;
    case ':': key = K::Semicolon; *shift = true; break;
    default: return false;
  }
  if (*shift) {
    type_emit(K::LeftShift, true);
    type_wait_frames(2);
  }
  type_code(key);
  if (*shift) {
    type_emit(K::LeftShift, false);
    type_wait_frames(2);
  }
  return true;
}

}  // namespace

bool bbc_user_key_seen(void) {
  bool v = s_user_key_seen;
  s_user_key_seen = false;
  return v;
}

void bbc_user_key_reset(void) { s_user_key_seen = false; }

void refresh_keyboard_matrix(void) {}

void input_set_joystick(int mode) {
  if (mode < 0 || mode > 2) {
    mode = 0;
  }
  s_joystick_mode = mode;
  debug_log("input: joystick mode %d", mode);
}

int input_get_joystick(void) { return s_joystick_mode; }

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
  // One event per frame keeps the 50 Hz keyboard scan happy.
  TypeEvent ev = s_type[s_type_head & 0xff];
  s_type_head++;
  if (ev.wait) {
    s_type_wait = 2;
    return;
  }
  Inputs::Keyboard *kbd = clk_keyboard();
  if (kbd == nullptr) {
    return;
  }
  kbd->set_key_pressed(ev.key, 0, ev.pressed, false);
}

void poll_input(void) {
  drain_keys();
  drain_joy();
  push_sticks();
  input_pump_typer();
}

}  // namespace bbc
}  // namespace bmx

// The BBC has no mouse; the common USB mouse code still needs these seams.
extern "C" {

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
