//
// msx_input.cpp
//
// See msx_input.h. The guest reads msx_keyboard_table[] live through the
// PPI (port A9h, row in PPI-C low nibble), so this module only maintains
// the table (active-low, init 0xFF) plus the typer queue.
//
// Matrix map is the openMSX standard MSX layout (rows 0-8 + numpad rows
// 9-10; row 11 special, unused here):
//   row 0: 0 1 2 3 4 5 6 7            (bit0..bit7)
//   row 1: 8 9 - = \ [ ] ;
//   row 2: ' ` , . / Acc A B
//   row 3: C D E F G H I J
//   row 4: K L M N O P Q R
//   row 5: S T U V W X Y Z
//   row 6: shift ctrl graph caps code F1 F2 F3
//   row 7: F4 F5 esc tab stop bs select ret
//   row 8: space home ins del left up down right
//

#include "msx_input.h"
#include "msx_debug.h"

#include "third_party/common/emux_api.h"
#include "third_party/common/keycodes.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

extern "C" {
#include "third_party/common/ui.h"
#include "third_party/common/circle.h"
}

// Owned by machines/msx.c (matrix) and msx_shim.cpp (joystick state).
// Types must match joystick.h (z80_byte) exactly.
extern "C" {
extern unsigned char msx_keyboard_table[];
extern unsigned char puerto_especial_joystick;
}

namespace bmx {
namespace msx {

namespace {

struct KeyMap {
  long hid;
  int row;
  int bit;
};
// HID (USB usage, see keycodes.h) -> MSX matrix cell. Letters/digits map
// to their unshifted cells; the firmware applies shift itself.
const KeyMap kKeyMap[] = {
    // Row 0: digits.
    {KEYCODE_0, 0, 0}, {KEYCODE_1, 0, 1}, {KEYCODE_2, 0, 2},
    {KEYCODE_3, 0, 3}, {KEYCODE_4, 0, 4}, {KEYCODE_5, 0, 5},
    {KEYCODE_6, 0, 6}, {KEYCODE_7, 0, 7},
    // Row 1: digits + symbols.
    {KEYCODE_8, 1, 0}, {KEYCODE_9, 1, 1}, {KEYCODE_Dash, 1, 2},
    {KEYCODE_Equals, 1, 3}, {KEYCODE_BackSlash, 1, 4},
    {KEYCODE_LeftBracket, 1, 5}, {KEYCODE_RightBracket, 1, 6},
    {KEYCODE_SemiColon, 1, 7},
    // Row 2: symbols + A/B.
    {KEYCODE_SingleQuote, 2, 0}, {KEYCODE_Comma, 2, 2},
    {KEYCODE_Period, 2, 3}, {KEYCODE_Slash, 2, 4},
    {KEYCODE_a, 2, 6}, {KEYCODE_b, 2, 7},
    // Rows 3-5: letters.
    {KEYCODE_c, 3, 0}, {KEYCODE_d, 3, 1}, {KEYCODE_e, 3, 2},
    {KEYCODE_f, 3, 3}, {KEYCODE_g, 3, 4}, {KEYCODE_h, 3, 5},
    {KEYCODE_i, 3, 6}, {KEYCODE_j, 3, 7}, {KEYCODE_k, 4, 0},
    {KEYCODE_l, 4, 1}, {KEYCODE_m, 4, 2}, {KEYCODE_n, 4, 3},
    {KEYCODE_o, 4, 4}, {KEYCODE_p, 4, 5}, {KEYCODE_q, 4, 6},
    {KEYCODE_r, 4, 7}, {KEYCODE_s, 5, 0}, {KEYCODE_t, 5, 1},
    {KEYCODE_u, 5, 2}, {KEYCODE_v, 5, 3}, {KEYCODE_w, 5, 4},
    {KEYCODE_x, 5, 5}, {KEYCODE_y, 5, 6}, {KEYCODE_z, 5, 7},
    // Row 6: modifiers + F1-F3.
    {KEYCODE_LeftShift, 6, 0}, {KEYCODE_RightShift, 6, 0},
    {KEYCODE_LeftControl, 6, 1}, {KEYCODE_RightControl, 6, 1},
    {KEYCODE_CapsLock, 6, 3}, {KEYCODE_F1, 6, 5}, {KEYCODE_F2, 6, 6},
    {KEYCODE_F3, 6, 7},
    // Row 7: F4/F5/esc/tab/stop/bs/select/ret.
    {KEYCODE_F4, 7, 0}, {KEYCODE_F5, 7, 1}, {KEYCODE_Escape, 7, 2},
    {KEYCODE_Tab, 7, 3}, {KEYCODE_F8, 7, 4},  // STOP
    {KEYCODE_Backspace, 7, 5}, {KEYCODE_Delete, 7, 5},
    {KEYCODE_F7, 7, 6},  // SELECT
    {KEYCODE_Return, 7, 7},
    // Row 8: space/home/ins/del/cursors.
    {KEYCODE_Space, 8, 0}, {KEYCODE_Home, 8, 1},
    {KEYCODE_Left, 8, 4}, {KEYCODE_Up, 8, 5}, {KEYCODE_Down, 8, 6},
    {KEYCODE_Right, 8, 7},
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

// Local mirror of the table (active-high pressed); flushed to the
// vendored active-low table on refresh.
bool s_keys[16][8] = {{false}};

int s_joy_value = 0;

void apply_joy_value(int value) {
  s_joy_value = value;
  // Upstream encoding (machines/msx.c R14 path): bits 8/4/2/1/16 clear
  // R14 bits 0/1/2/3/4 (up/down/left/right/fire1), active-low.
  int v = 0;
  if (value & 0x08) {
    v |= 8;  // up
  }
  if (value & 0x04) {
    v |= 4;  // down
  }
  if (value & 0x02) {
    v |= 2;  // left
  }
  if (value & 0x01) {
    v |= 1;  // right
  }
  if (value & 0x10) {
    v |= 16;  // fire 1
  }
  puerto_especial_joystick = v;
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
    type_emit(6, 0, shift);
    s_type_shift = shift;
  }
}

// MSX cell map for the typer (unshifted legends; shifted via type_shift).
// " is Shift+2, : is Shift+; on the MSX layout.
bool type_cell(char c, int *row, int *bit, bool *shift) {
  *shift = false;
  if (c >= 'a' && c <= 'z') {
    c = (char)(c - 'a' + 'A');
  }
  switch (c) {
    case 'A': *row = 2; *bit = 6; return true;
    case 'B': *row = 2; *bit = 7; return true;
    case 'C': *row = 3; *bit = 0; return true;
    case 'D': *row = 3; *bit = 1; return true;
    case 'E': *row = 3; *bit = 2; return true;
    case 'F': *row = 3; *bit = 3; return true;
    case 'G': *row = 3; *bit = 4; return true;
    case 'H': *row = 3; *bit = 5; return true;
    case 'I': *row = 3; *bit = 6; return true;
    case 'J': *row = 3; *bit = 7; return true;
    case 'K': *row = 4; *bit = 0; return true;
    case 'L': *row = 4; *bit = 1; return true;
    case 'M': *row = 4; *bit = 2; return true;
    case 'N': *row = 4; *bit = 3; return true;
    case 'O': *row = 4; *bit = 4; return true;
    case 'P': *row = 4; *bit = 5; return true;
    case 'Q': *row = 4; *bit = 6; return true;
    case 'R': *row = 4; *bit = 7; return true;
    case 'S': *row = 5; *bit = 0; return true;
    case 'T': *row = 5; *bit = 1; return true;
    case 'U': *row = 5; *bit = 2; return true;
    case 'V': *row = 5; *bit = 3; return true;
    case 'W': *row = 5; *bit = 4; return true;
    case 'X': *row = 5; *bit = 5; return true;
    case 'Y': *row = 5; *bit = 6; return true;
    case 'Z': *row = 5; *bit = 7; return true;
    case '0': *row = 0; *bit = 0; return true;
    case '1': *row = 0; *bit = 1; return true;
    case '2': *row = 0; *bit = 2; return true;
    case '3': *row = 0; *bit = 3; return true;
    case '4': *row = 0; *bit = 4; return true;
    case '5': *row = 0; *bit = 5; return true;
    case '6': *row = 0; *bit = 6; return true;
    case '7': *row = 0; *bit = 7; return true;
    case '8': *row = 1; *bit = 0; return true;
    case '9': *row = 1; *bit = 1; return true;
    case ' ': *row = 8; *bit = 0; return true;
    case '\n': *row = 7; *bit = 7; return true;  // RET
    case '"': *row = 0; *bit = 2; *shift = true; return true;
    case ':': *row = 1; *bit = 7; *shift = true; return true;
    case '.': *row = 2; *bit = 3; return true;
    case ',': *row = 2; *bit = 2; return true;
    case '-': *row = 1; *bit = 2; return true;
    case '/': *row = 2; *bit = 4; return true;
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
  for (int r = 0; r < 16; r++) {
    uint8_t v = 0xFF;
    for (int b = 0; b < 8; b++) {
      if (s_keys[r][b]) {
        v &= (uint8_t) ~(1u << b);
      }
    }
    msx_keyboard_table[r] = v;
  }
}

void input_set_joystick_value(int value) { apply_joy_value(value); }

int input_get_joystick_value(void) { return s_joy_value; }

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

void input_type_autostart_cas(void) {
  s_type_head = s_type_tail = 0;
  s_type_shift = false;
  // Boot to BASIC Ready, then load the cassette the standard way.
  s_type_wait = 150;
  for (const char *p = "RUN\"CAS:\n"; *p != '\0'; p++) {
    type_char(*p);
  }
  type_shift(false);
  type_wait(2);
  unsigned pending = s_type_tail - s_type_head;
  debug_log("input: autostart cas script queued %u events", pending);
}

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
    // F12 (menu toggle) is reserved; F1..F8 are MSX machine keys
    // (function row, STOP/SELECT) and must reach the matrix.
    if (key == KEYCODE_F12) {
      continue;
    }
    int row = -1, bit = 0;
    if (lookup_key(key, &row, &bit)) {
      s_keys[row][bit] = (pressed != 0);
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
      (void)port;
      apply_joy_value(value);
    }
  }
  input_pump_typer();
  ui_handle_toggle_or_quick_func();
}

}  // namespace msx
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
