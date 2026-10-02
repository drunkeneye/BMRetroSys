//
// cpc_input.cpp
//
// See cpc_input.h. Matrix model: cpc_key_set(row 0..9, bit 0..7) in
// cpc_hw.c; the PPI/AY scan path reads it live, so no refresh hook does
// real work (the firmware scans rows itself every frame).
//
// Matrix map is the MAME CPC driver table (kbrow.0-9, 6128 variant),
// verified bit-for-bit: (row = PPI Port C 0..9, bit, 0 = pressed).
// Numpad legends (464) share cells with the 6128 F-keys, so one table
// serves all models; both HID codes drive the same cell.
//

#include "cpc_input.h"
#include "cpc_debug.h"

#include "third_party/common/emux_api.h"
#include "third_party/common/keycodes.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

extern "C" {
#include "third_party/common/ui.h"
#include "third_party/common/circle.h"
}

// Implemented in cpc_hw.c.
extern "C" void cpc_key_set(int row, int bit, int pressed);
extern "C" void cpc_key_clear_all(void);

namespace bmx {
namespace cpc {

namespace {

bool s_keys[10][8] = {{false}};

struct KeyMap {
  long hid;
  int row;
  int bit;
};
// HID (USB usage, see keycodes.h) -> CPC matrix cell. Letters/digits map
// to their unshifted cells; the firmware applies shift itself.
const KeyMap kKeyMap[] = {
    // Row 8: 1 2 Esc Q Tab A Caps Z
    {KEYCODE_1, 8, 0}, {KEYCODE_2, 8, 1}, {KEYCODE_q, 8, 3},
    {KEYCODE_a, 8, 5}, {KEYCODE_z, 8, 7}, {KEYCODE_Escape, 8, 2},
    {KEYCODE_Tab, 8, 4}, {KEYCODE_CapsLock, 8, 6},
    // Row 7: 4 3 E W S D C X
    {KEYCODE_4, 7, 0}, {KEYCODE_3, 7, 1}, {KEYCODE_e, 7, 2},
    {KEYCODE_w, 7, 3}, {KEYCODE_s, 7, 4}, {KEYCODE_d, 7, 5},
    {KEYCODE_c, 7, 6}, {KEYCODE_x, 7, 7},
    // Row 6: 6 5 R T G F B V
    {KEYCODE_6, 6, 0}, {KEYCODE_5, 6, 1}, {KEYCODE_r, 6, 2},
    {KEYCODE_t, 6, 3}, {KEYCODE_g, 6, 4}, {KEYCODE_f, 6, 5},
    {KEYCODE_b, 6, 6}, {KEYCODE_v, 6, 7},
    // Row 5: 8 7 U Y H J N Space
    {KEYCODE_8, 5, 0}, {KEYCODE_7, 5, 1}, {KEYCODE_u, 5, 2},
    {KEYCODE_y, 5, 3}, {KEYCODE_h, 5, 4}, {KEYCODE_j, 5, 5},
    {KEYCODE_n, 5, 6}, {KEYCODE_Space, 5, 7},
    // Row 4: 0 9 O I L K M ,
    {KEYCODE_0, 4, 0}, {KEYCODE_9, 4, 1}, {KEYCODE_o, 4, 2},
    {KEYCODE_i, 4, 3}, {KEYCODE_l, 4, 4}, {KEYCODE_k, 4, 5},
    {KEYCODE_m, 4, 6}, {KEYCODE_Comma, 4, 7},
    // Row 3: ^ - @ P ; : / .
    {KEYCODE_Equals, 3, 0},  // ^ (UK position)
    {KEYCODE_Dash, 3, 1}, {KEYCODE_LeftBracket, 3, 2},  // @
    {KEYCODE_p, 3, 3}, {KEYCODE_SingleQuote, 3, 4},  // ;
    {KEYCODE_SemiColon, 3, 5},  // : (UK position)
    {KEYCODE_Slash, 3, 6}, {KEYCODE_Period, 3, 7},
    // Row 2: Clr [ Return ] F4 Shift \ Ctrl
    {KEYCODE_Home, 2, 0}, {KEYCODE_RightBracket, 2, 1},
    {KEYCODE_Return, 2, 2}, {KEYCODE_BackSlash, 2, 3},
    {KEYCODE_LeftShift, 2, 5}, {KEYCODE_RightShift, 2, 5},
    {KEYCODE_LeftControl, 2, 7}, {KEYCODE_RightControl, 2, 7},
    // Row 1: Left Copy F7 F8 F5 F1 F2 F0
    {KEYCODE_Left, 1, 0}, {KEYCODE_End, 1, 1},
    {KEYCODE_F7, 1, 2}, {KEYCODE_F8, 1, 3}, {KEYCODE_F5, 1, 4},
    {KEYCODE_F1, 1, 5}, {KEYCODE_F2, 1, 6}, {KEYCODE_F10, 1, 7},
    // Row 0: Up Right Down F9 F6 F3 Enter Dot
    {KEYCODE_Up, 0, 0}, {KEYCODE_Right, 0, 1}, {KEYCODE_Down, 0, 2},
    {KEYCODE_F9, 0, 3}, {KEYCODE_F6, 0, 4}, {KEYCODE_F3, 0, 5},
    {KEYCODE_KP_Enter, 0, 6},
    // Row 9: joystick + Del
    {KEYCODE_Backspace, 9, 7}, {KEYCODE_Delete, 9, 7},
    // 464 numpad (same cells as the 6128 F-keys above).
    {KEYCODE_KP9, 0, 3}, {KEYCODE_KP6, 0, 4}, {KEYCODE_KP3, 0, 5},
    {KEYCODE_KP_Decimal, 0, 7}, {KEYCODE_KP7, 1, 2}, {KEYCODE_KP8, 1, 3},
    {KEYCODE_KP5, 1, 4}, {KEYCODE_KP1, 1, 5}, {KEYCODE_KP2, 1, 6},
    {KEYCODE_KP0, 1, 7}, {KEYCODE_KP4, 2, 4},
    {KEYCODE_F4, 2, 4},
};

bool lookup_key(long hid, int *row, int *bit) {
  for (size_t i = 0; i < sizeof(kKeyMap) / sizeof(kKeyMap[0]); i++) {
    if (kKeyMap[i].hid == hid) {
      *row = kKeyMap[i].row;
      *bit = kKeyMap[i].bit;
      return true;
    }
  }
  return false;
}

int s_joystick_mode = 1;  // joystick 0 default (most games)

// BMX absolute 5-bit layouts (bit0 right, bit1 left, bit2 down, bit3 up,
// bit4 fire, mirroring the Kempston convention) folded onto a CPC matrix
// line like real joystick hardware.
void apply_joy_value(int port, int value) {
  (void)port;
  const int line = (s_joystick_mode == 2) ? 6 : 9;
  s_keys[line][0] = (value & 0x08) != 0;  // up
  s_keys[line][1] = (value & 0x04) != 0;  // down
  s_keys[line][2] = (value & 0x02) != 0;  // left
  s_keys[line][3] = (value & 0x01) != 0;  // right
  s_keys[line][4] = (value & 0x10) != 0;  // fire 1
  s_keys[line][5] = (value & 0x20) != 0;  // fire 2
}

// ---- Autostart typer (matrix press/release events, frame-paced).
struct TypeEvent {
  int row;
  int bit;
  bool pressed;
};
TypeEvent s_type[512];
unsigned s_type_head = 0;
unsigned s_type_tail = 0;
unsigned s_type_wait = 0;
bool s_type_shift = false;

bool type_emit(int row, int bit, bool pressed) {
  unsigned next = (s_type_tail + 1) & 0x1ff;
  if (next == (s_type_head & 0x1ff)) {
    return false;
  }
  s_type[s_type_tail & 0x1ff].row = row;
  s_type[s_type_tail & 0x1ff].bit = bit;
  s_type[s_type_tail & 0x1ff].pressed = pressed;
  s_type_tail++;
  return true;
}

void type_wait(int frames) { type_emit(-2, frames, false); }

void type_key(int row, int bit) {
  type_emit(row, bit, true);
  type_wait(3);
  type_emit(row, bit, false);
  type_wait(2);
}

void type_shift(bool shift) {
  if (shift != s_type_shift) {
    type_emit(2, 5, shift);
    s_type_shift = shift;
  }
}

// CPC cell map for the typer (unshifted legends; shifted via type_shift).
bool type_cell(char c, int *row, int *bit, bool *shift) {
  *shift = false;
  if (c >= 'a' && c <= 'z') {
    c = (char)(c - 'a' + 'A');
  }
  switch (c) {
    case 'A': *row = 8; *bit = 5; return true;
    case 'B': *row = 6; *bit = 6; return true;
    case 'C': *row = 7; *bit = 6; return true;
    case 'D': *row = 7; *bit = 5; return true;
    case 'E': *row = 7; *bit = 2; return true;
    case 'F': *row = 6; *bit = 5; return true;
    case 'G': *row = 6; *bit = 4; return true;
    case 'H': *row = 5; *bit = 4; return true;
    case 'I': *row = 4; *bit = 3; return true;
    case 'J': *row = 5; *bit = 5; return true;
    case 'K': *row = 4; *bit = 5; return true;
    case 'L': *row = 4; *bit = 4; return true;
    case 'M': *row = 4; *bit = 6; return true;
    case 'N': *row = 5; *bit = 6; return true;
    case 'O': *row = 4; *bit = 2; return true;
    case 'P': *row = 3; *bit = 3; return true;
    case 'Q': *row = 8; *bit = 3; return true;
    case 'R': *row = 6; *bit = 2; return true;
    case 'S': *row = 7; *bit = 4; return true;
    case 'T': *row = 6; *bit = 3; return true;
    case 'U': *row = 5; *bit = 2; return true;
    case 'V': *row = 6; *bit = 7; return true;
    case 'W': *row = 7; *bit = 3; return true;
    case 'X': *row = 7; *bit = 7; return true;
    case 'Y': *row = 5; *bit = 3; return true;
    case 'Z': *row = 8; *bit = 7; return true;
    case '0': *row = 4; *bit = 0; return true;
    case '1': *row = 8; *bit = 0; return true;
    case '2': *row = 8; *bit = 1; return true;
    case '3': *row = 7; *bit = 1; return true;
    case '4': *row = 7; *bit = 0; return true;
    case '5': *row = 6; *bit = 1; return true;
    case '6': *row = 6; *bit = 0; return true;
    case '7': *row = 5; *bit = 1; return true;
    case '8': *row = 5; *bit = 0; return true;
    case '9': *row = 4; *bit = 1; return true;
    case ' ': *row = 5; *bit = 7; return true;
    case '\n': *row = 2; *bit = 2; return true;  // big RETURN
    case '"': *row = 8; *bit = 1; *shift = true; return true;
    case ':': *row = 3; *bit = 5; return true;
    case '|': *row = 3; *bit = 2; *shift = true; return true;  // shift+@
    case ';': *row = 3; *bit = 4; return true;
    case '.': *row = 3; *bit = 7; return true;
    case ',': *row = 4; *bit = 7; return true;
    case '-': *row = 3; *bit = 1; return true;
    case '/': *row = 3; *bit = 6; return true;
    default: return false;
  }
}

void type_char(char c) {
  int row = -1, bit = 0;
  bool shift = false;
  if (!type_cell(c, &row, &bit, &shift)) {
    return;
  }
  type_shift(shift);
  type_key(row, bit);
}

}  // namespace

void refresh_keyboard_matrix(void) {
  for (int r = 0; r < 10; r++) {
    for (int b = 0; b < 8; b++) {
      cpc_key_set(r, b, s_keys[r][b] ? 1 : 0);
    }
  }
}

void input_set_joystick(int mode) {
  if (mode < 0 || mode > 2) {
    mode = 0;
  }
  s_joystick_mode = mode;
  debug_log("input: joystick mode %d", mode);
}

int input_get_joystick(void) { return s_joystick_mode; }

void input_type_text_delayed(const char *text, unsigned wait_frames) {
  s_type_head = s_type_tail = 0;
  s_type_wait = wait_frames;
  s_type_shift = false;
  if (text == nullptr) {
    return;
  }
  for (const char *p = text; *p != '\0'; p++) {
    type_char(*p);
  }
  type_shift(false);
  type_wait(2);
  unsigned pending = s_type_tail - s_type_head;
  debug_log("input: typer queued %u events (wait %u)", pending, wait_frames);
}

void input_type_text(const char *text) {
  input_type_text_delayed(text, 100);
}

void input_type_autostart_tape(bool tape_cmd_first) {
  s_type_head = s_type_tail = 0;
  s_type_shift = false;
  // Boot to BASIC Ready (~3s covers the 6128 RAM test).
  s_type_wait = 150;
  if (tape_cmd_first) {
    for (const char *p = "|TAPE\n"; *p != '\0'; p++) {
      type_char(*p);
    }
    // Let the drive switch complete and the Ready prompt return.
    type_wait(90);
  }
  for (const char *p = "RUN\"\n"; *p != '\0'; p++) {
    type_char(*p);
  }
  type_shift(false);
  type_wait(2);
  unsigned pending = s_type_tail - s_type_head;
  debug_log("input: autostart tape script queued %u events (tape_cmd=%d)",
            pending, tape_cmd_first ? 1 : 0);
}

void input_type_autostart_disc_name(const char *name) {
  s_type_head = s_type_tail = 0;
  s_type_shift = false;
  // Boot to BASIC Ready (~3s covers the 6128 RAM test), then boot the
  // disc with the detected file (DISC.BAS on standard bootables, else
  // the first suitable file). The 464 has no disc system; typing is
  // harmless there (BASIC reports unknown command).
  s_type_wait = 150;
  type_char('R');
  type_char('U');
  type_char('N');
  type_char('"');
  if (name != nullptr) {
    for (const char *p = name; *p != '\0'; p++) {
      type_char(*p);
    }
  }
  type_char('\n');
  type_shift(false);
  type_wait(2);
  unsigned pending = s_type_tail - s_type_head;
  debug_log("input: autostart disc script queued %u events (%s)", pending,
            name != nullptr ? name : "?");
}

void input_type_autostart_disc(void) { input_type_autostart_disc_name("DISC"); }

void input_pump_typer(void) {
  if (s_type_head == s_type_tail) {
    return;
  }
  if (s_type_wait > 0) {
    s_type_wait--;
    return;
  }
  unsigned i = s_type_head & 0x1ff;
  if (s_type[i].row == -2) {
    s_type_wait = (unsigned)s_type[i].bit;
  } else {
    s_keys[s_type[i].row][s_type[i].bit] = s_type[i].pressed;
    refresh_keyboard_matrix();
  }
  s_type_head++;
}

void poll_input(void) {
  while (pending_emu_key.head != pending_emu_key.tail) {
    int i = pending_emu_key.head & 0xf;
    long key = pending_emu_key.key[i];
    int pressed = pending_emu_key.pressed[i];
    pending_emu_key.head++;
    // Unlike the ZX kernel, F1..F10 are CPC machine keys (BASIC function
    // keys, games) and must reach the matrix. Only F12 (menu toggle) is
    // reserved. If a user assigns quick functions to F-keys, the menu
    // layer consumes those first.
    if (key == KEYCODE_F12) {
      continue;
    }
    int row = -1, bit = 0;
    if (lookup_key(key, &row, &bit)) {
      s_keys[row][bit] = (pressed != 0);
      // Temporary key trace (one line per USB event): proves which HID
      // code landed on which matrix cell. Remove once the map is
      // device-verified.
      debug_log("input: hid=%ld %s -> (%d,%d)", key,
                pressed ? "down" : "up", row, bit);
    } else {
      debug_log("input: hid=%ld %s unmapped", key,
                pressed ? "down" : "up");
    }
  }
  refresh_keyboard_matrix();
  while (pending_emu_joy.head != pending_emu_joy.tail) {
    int i = pending_emu_joy.head & 0x7f;
    int port = pending_emu_joy.port[i];
    int value = pending_emu_joy.value[i];
    int type = pending_emu_joy.type[i];
    pending_emu_joy.head++;
    if (type == PENDING_EMU_JOY_TYPE_ABSOLUTE) {
      apply_joy_value(port, value);
    }
  }
  input_pump_typer();
  ui_handle_toggle_or_quick_func();
}

}  // namespace cpc
}  // namespace bmx

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
