//
// mac128k_input.cpp
//
// Mac 128K input (classic keyboard/mouse) for BMX bare-metal.
//
// USB keyboard events arrive through the standard pending_emu_key queue
// (HID usage IDs) and are translated to Mac key codes (MKC_*, same
// numbering as ADB) with the in-tree table below, then injected via the
// Mini vMac backend entry points. USB mouse motion/buttons arrive via
// emu_mouse_* (accumulators in this TU) and are forwarded as absolute
// Mac coordinates (the backend tracks the Mac cursor position).
//
// F12 is the BMX menu key: it is never forwarded to the guest, it raises
// the menu-request flag drained by the backend tick instead (same filter
// rule as src/atarist/atarist_input.cpp).
//

#include "mac128k_input.h"
#include "mac128k_debug.h"

#include "mac128k_bmx_backend.h"

#include <stdio.h>
#include <string.h>

extern "C" {
#include <stdint.h>
#include "third_party/common/emux_api.h"
#include "third_party/common/keycodes.h"
}

namespace bmx {
namespace mac128k {

namespace {

int s_reset_request = 0;

// F12 menu-request flag (set by the key drain, taken by the tick pump).
bool s_menu_request = false;

// Rate-limited logging: first forwarded key/mouse, each unknown HID once.
bool s_first_key_logged = false;
bool s_first_mouse_logged = false;
long s_seen_unknown[32];
int s_seen_unknown_count = 0;

// ASCII drip buffer for mac128k_type_text (one char per drain call).
char s_type_buf[256];
size_t s_type_len = 0;
size_t s_type_pos = 0;

// USB HID usage -> Mac key code (MKC numbering = ADB numbering).
// the main block, SDL/video_sdl2.cpp SDLK_* switch for modifiers, arrows,
// keypad, function and navigation keys (both agree; e.g. Ctrl 0x36,
// Cmd 0x37, Shift 0x38, Caps 0x39, Opt 0x3a, Left 0x3b, Right 0x3c,
// Down 0x3d, Up 0x3e). Returns -1 for keys with no Mac equivalent.
int hid_to_adb(long hid) {
  switch (hid) {
  case KEYCODE_a: return 0;
  case KEYCODE_b: return 11;
  case KEYCODE_c: return 8;
  case KEYCODE_d: return 2;
  case KEYCODE_e: return 14;
  case KEYCODE_f: return 3;
  case KEYCODE_g: return 5;
  case KEYCODE_h: return 4;
  case KEYCODE_i: return 34;
  case KEYCODE_j: return 38;
  case KEYCODE_k: return 40;
  case KEYCODE_l: return 37;
  case KEYCODE_m: return 46;
  case KEYCODE_n: return 45;
  case KEYCODE_o: return 31;
  case KEYCODE_p: return 35;
  case KEYCODE_q: return 12;
  case KEYCODE_r: return 15;
  case KEYCODE_s: return 1;
  case KEYCODE_t: return 17;
  case KEYCODE_u: return 32;
  case KEYCODE_v: return 9;
  case KEYCODE_w: return 13;
  case KEYCODE_x: return 7;
  case KEYCODE_y: return 16;
  case KEYCODE_z: return 6;
  case KEYCODE_1: return 18;
  case KEYCODE_2: return 19;
  case KEYCODE_3: return 20;
  case KEYCODE_4: return 21;
  case KEYCODE_5: return 23;
  case KEYCODE_6: return 22;
  case KEYCODE_7: return 26;
  case KEYCODE_8: return 28;
  case KEYCODE_9: return 25;
  case KEYCODE_0: return 29;
  case KEYCODE_Return: return 36;
  case KEYCODE_Escape: return 53;
  case KEYCODE_Backspace: return 51;
  case KEYCODE_Tab: return 48;
  case KEYCODE_Space: return 49;
  case KEYCODE_Dash: return 27;
  case KEYCODE_Equals: return 24;
  case KEYCODE_LeftBracket: return 33;
  case KEYCODE_RightBracket: return 30;
  case KEYCODE_BackSlash: return 42;
  case KEYCODE_SemiColon: return 41;
  case KEYCODE_SingleQuote: return 39;
  case KEYCODE_BackQuote: return 50;
  case KEYCODE_Comma: return 43;
  case KEYCODE_Period: return 47;
  case KEYCODE_Slash: return 44;
  case KEYCODE_CapsLock: return 57;
  case KEYCODE_F1: return 122;
  case KEYCODE_F2: return 120;
  case KEYCODE_F3: return 99;
  case KEYCODE_F4: return 118;
  case KEYCODE_F5: return 96;
  case KEYCODE_F6: return 97;
  case KEYCODE_F7: return 98;
  case KEYCODE_F8: return 100;
  case KEYCODE_F9: return 101;
  case KEYCODE_F10: return 109;
  case KEYCODE_F11: return 103;
  case KEYCODE_ScrollLock: return 107;
  case KEYCODE_Pause: return 113;
  case KEYCODE_Insert: return 114;
  case KEYCODE_Home: return 115;
  case KEYCODE_PageUp: return 116;
  case KEYCODE_Delete: return 117;
  case KEYCODE_End: return 119;
  case KEYCODE_PageDown: return 121;
  case KEYCODE_Right: return 60;
  case KEYCODE_Left: return 59;
  case KEYCODE_Down: return 61;
  case KEYCODE_Up: return 62;
  case KEYCODE_NumLock: return 71;
  case KEYCODE_KP_Divide: return 75;
  case KEYCODE_KP_Multiply: return 67;
  case KEYCODE_KP_Subtract: return 78;
  case KEYCODE_KP_Add: return 69;
  case KEYCODE_KP_Enter: return 76;
  case KEYCODE_KP1: return 83;
  case KEYCODE_KP2: return 84;
  case KEYCODE_KP3: return 85;
  case KEYCODE_KP4: return 86;
  case KEYCODE_KP5: return 87;
  case KEYCODE_KP6: return 88;
  case KEYCODE_KP7: return 89;
  case KEYCODE_KP8: return 91;
  case KEYCODE_KP9: return 92;
  case KEYCODE_KP0: return 82;
  case KEYCODE_KP_Decimal: return 65;
  case KEYCODE_LeftShift:
  case KEYCODE_RightShift:
    return 56;
  case KEYCODE_LeftControl:
  case KEYCODE_RightControl:
    return 54;
  case KEYCODE_LeftAlt:
  case KEYCODE_RightAlt:
    return 55;  // Command (non-Apple host mapping, per video_sdl2.cpp)
  case KEYCODE_LeftSuper:
  case KEYCODE_RightSuper:
    return 58;  // Option (non-Apple host mapping, per video_sdl2.cpp)
  default:
    return -1;
  }
}

void log_unknown_hid_once(long hid) {
  for (int i = 0; i < s_seen_unknown_count; i++) {
    if (s_seen_unknown[i] == hid) {
      return;
    }
  }
  debug_log("mac128k: input: unknown HID 0x%lx, skipping", (unsigned long)hid);
  if (s_seen_unknown_count <
      (int)(sizeof(s_seen_unknown) / sizeof(s_seen_unknown[0]))) {
    s_seen_unknown[s_seen_unknown_count++] = hid;
  }
}

// Printable ASCII -> HID usage (+ shift flag). Minimal set: a-z, A-Z
// (via shift), 0-9, space, '.', ',', Enter. Returns 0 when unmapped.
long char_to_hid(char c, int *shift) {
  if (shift != NULL) {
    *shift = 0;
  }
  if (c >= 'a' && c <= 'z') {
    return (long)(KEYCODE_a + (c - 'a'));
  }
  if (c >= 'A' && c <= 'Z') {
    if (shift != NULL) {
      *shift = 1;
    }
    return (long)(KEYCODE_a + (c - 'A'));
  }
  if (c >= '1' && c <= '9') {
    return (long)(KEYCODE_1 + (c - '1'));
  }
  switch (c) {
  case '0':
    return KEYCODE_0;
  case ' ':
    return KEYCODE_Space;
  case '.':
    return KEYCODE_Period;
  case ',':
    return KEYCODE_Comma;
  case '\n':
  case '\r':
    return KEYCODE_Return;
  default:
    return 0;
  }
}

void drip_type_char(void) {
  if (s_type_pos >= s_type_len) {
    s_type_len = 0;
    s_type_pos = 0;
    return;
  }
  char c = s_type_buf[s_type_pos++];
  int shift = 0;
  long hid = char_to_hid(c, &shift);
  if (hid == 0) {
    return;
  }
  int adb = hid_to_adb(hid);
  if (adb < 0) {
    return;
  }
  if (shift) {
    bmx_mac128k_backend_key(56, 1);
  }
  bmx_mac128k_backend_key(adb, 1);
  bmx_mac128k_backend_key(adb, 0);
  if (shift) {
    bmx_mac128k_backend_key(56, 0);
  }
}

}  // namespace

// Mouse accumulators shared with the extern "C" producers below.
// Named-namespace scope (not anonymous) so the C-linkage definitions can
// reach them, mirroring src/atarist/atarist_input.cpp.
int s_mouse_dx = 0;
int s_mouse_dy = 0;
int s_mouse_buttons = 0;

namespace {

int s_mouse_sent_buttons = 0;

}  // namespace

void poll_input(mac128k_input_t *input) {
  if (input != NULL) {
    input->unused = 0;
  }
  // Same drain path as the standalone entry below (the frame loop calls
  // both; each is idempotent when the queue is empty).
  drain_guest_keys();
}

void drain_guest_keys(void) {
  // Drain the USB keyboard queue into the Mac keyboard. Same
  // monotonic-counter rule as atarist: never wrap head itself.
  while (pending_emu_key.head != pending_emu_key.tail) {
    int i = pending_emu_key.head & 0xf;
    long key = pending_emu_key.key[i];
    int pressed = pending_emu_key.pressed[i];
    pending_emu_key.head++;

    // F12 is the BMX menu key (handled by the tick pump); never forward
    // it to the guest.
    if (key == KEYCODE_F12) {
      // debug_log("mac128k: input: F12 menu request");  // log quiet
      s_menu_request = true;
      continue;
    }
    int adb = hid_to_adb(key);
    if (adb < 0) {
      log_unknown_hid_once(key);
      continue;
    }
    if (!s_first_key_logged) {
      s_first_key_logged = true;
      debug_log("mac128k: input: first key HID 0x%lx -> MKC %d",
                (unsigned long)key, adb);
    }
    if (pressed) {
      bmx_mac128k_backend_key(adb, 1);
    } else {
      bmx_mac128k_backend_key(adb, 0);
    }
  }

  // Drip one queued autostart char per drain.
  if (s_type_pos < s_type_len) {
    drip_type_char();
  }
}

void drain_guest_mouse(void) {
  int dx = s_mouse_dx;
  int dy = s_mouse_dy;
  s_mouse_dx = 0;
  s_mouse_dy = 0;
  if (dx != 0 || dy != 0) {
    if (!s_first_mouse_logged) {
      s_first_mouse_logged = true;
      debug_log("mac128k: input: first mouse move dx=%d dy=%d", dx, dy);
    }
    bmx_mac128k_backend_mouse_delta(dx, dy);
  }
  // The 128K Mac has a single mouse button: only the left button state
  // is forwarded (right/middle are tracked but ignored).
  int cur = (s_mouse_buttons & 0x01) ? 1 : 0;
  if (cur != s_mouse_sent_buttons) {
    s_mouse_sent_buttons = cur;
    bmx_mac128k_backend_mouse_button(cur);
  }
}

void mac128k_request_reset(int code) {
  s_reset_request = code;
}

int mac128k_take_reset_request(void) {
  int code = s_reset_request;
  s_reset_request = 0;
  return code;
}

void mac128k_type_text(const char *text) {
  if (text == NULL || text[0] == '\0') {
    return;
  }
  size_t n = strlen(text);
  if (n >= sizeof(s_type_buf)) {
    n = sizeof(s_type_buf) - 1;
  }
  memcpy(s_type_buf, text, n);
  s_type_buf[n] = '\0';
  s_type_len = n;
  s_type_pos = 0;
  debug_log("mac128k: input: type_text queued %lu chars", (unsigned long)n);
}

bool mac128k_take_menu_request(void) {
  bool pending = s_menu_request;
  s_menu_request = false;
  return pending;
}

}  // namespace mac128k
}  // namespace bmx

// Backend tick entry: drains USB queues into the guest and reports
// menu/reset requests. Runs on the emulation thread (called from the
// backend WaitForNextTick).
extern "C" unsigned int bmx_mac128k_poll_input(void) {
  using namespace bmx::mac128k;
  drain_guest_keys();
  drain_guest_mouse();
  unsigned int ev = BMX_MAC128K_EV_NONE;
  if (mac128k_take_menu_request()) {
    // debug_log("mac128k: input: menu request -> pump");  // log quiet
    ev |= BMX_MAC128K_EV_MENU;
  }
  int reset_code = mac128k_take_reset_request();
  if (reset_code == 6) {
    ev |= BMX_MAC128K_EV_RESET_SOFT;
  } else if (reset_code != 0) {
    // Hard reset (or anything else): unwind the guest now; the frame
    // loop picks up the code via bmx_mac128k_backend_take_reboot() and
    // runs a full reboot.
    bmx_mac128k_backend_request_reboot(reset_code);
  }
  return ev;
}

// BMX mouse driver producers (src/mouse_input.cpp expects these with C
// linkage; the accumulators and the drain share one TU, mirroring
// src/atarist/atarist_input.cpp).
extern "C" {

void emu_mouse_move(int x, int y) {
  bmx::mac128k::s_mouse_dx += x;
  bmx::mac128k::s_mouse_dy += y;
}

void emu_mouse_button_left(int pressed) {
  if (pressed) {
    bmx::mac128k::s_mouse_buttons |= 0x01;
  } else {
    bmx::mac128k::s_mouse_buttons &= ~0x01;
  }
}

void emu_mouse_button_right(int pressed) {
  if (pressed) {
    bmx::mac128k::s_mouse_buttons |= 0x02;
  } else {
    bmx::mac128k::s_mouse_buttons &= ~0x02;
  }
}

void emu_mouse_button_middle(int pressed) {
  if (pressed) {
    bmx::mac128k::s_mouse_buttons |= 0x04;
  } else {
    bmx::mac128k::s_mouse_buttons &= ~0x04;
  }
}

void emu_mouse_wheel_up(int pressed) {
  (void)pressed;
}

void emu_mouse_wheel_down(int pressed) {
  (void)pressed;
}

}  // extern "C"
