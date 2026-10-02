//
// zx_input.cpp
//
// See zx_input.h. Matrix rows (bit0 = leftmost key, 0 = pressed):
//   65278: SHIFT Z X C V   65022: A S D F G   64510: Q W E R T
//   63486: 1 2 3 4 5       61438: 0 9 8 7 6   57342: P O I U Y
//   49150: ENTER L K J H   32766: SPACE SYM M N B
//

#include "zx_input.h"
#include "zx_debug.h"

#include "third_party/common/emux_api.h"
#include "third_party/common/keycodes.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

extern "C" {
#include "third_party/common/ui.h"
#include "third_party/common/circle.h"
}

#include "zx_zesarux.h"

namespace bmx {
namespace zxspectrum {

namespace {

// Latched matrix state: true = pressed.
bool s_keys[8][5] = {{false}};
// (row, bit) for HID codes; row -1 = unmapped.
struct KeyMap {
  long hid;
  int row;
  int bit;
};
const KeyMap kKeyMap[] = {
    {KEYCODE_a, 1, 0}, {KEYCODE_b, 7, 4}, {KEYCODE_c, 0, 3},
    {KEYCODE_d, 1, 2}, {KEYCODE_e, 2, 2}, {KEYCODE_f, 1, 3},
    {KEYCODE_g, 1, 4}, {KEYCODE_h, 6, 4}, {KEYCODE_i, 5, 2},
    {KEYCODE_j, 6, 3}, {KEYCODE_k, 6, 2}, {KEYCODE_l, 6, 1},
    {KEYCODE_m, 7, 2}, {KEYCODE_n, 7, 3}, {KEYCODE_o, 5, 1},
    {KEYCODE_p, 5, 0}, {KEYCODE_q, 2, 0}, {KEYCODE_r, 2, 3},
    {KEYCODE_s, 1, 1}, {KEYCODE_t, 2, 4}, {KEYCODE_u, 5, 3},
    {KEYCODE_v, 0, 4}, {KEYCODE_w, 2, 1}, {KEYCODE_x, 0, 2},
    {KEYCODE_y, 5, 4}, {KEYCODE_z, 0, 1},
    {KEYCODE_1, 3, 0}, {KEYCODE_2, 3, 1}, {KEYCODE_3, 3, 2},
    {KEYCODE_4, 3, 3}, {KEYCODE_5, 3, 4}, {KEYCODE_6, 4, 4},
    {KEYCODE_7, 4, 3}, {KEYCODE_8, 4, 2}, {KEYCODE_9, 4, 1},
    {KEYCODE_0, 4, 0},
    {KEYCODE_Return, 6, 0}, {KEYCODE_KP_Enter, 6, 0},
    {KEYCODE_Space, 7, 0}, {KEYCODE_Tab, 0, 0},  // Tab as CAPS
    {KEYCODE_LeftShift, 0, 0}, {KEYCODE_RightShift, 7, 1},
    {KEYCODE_LeftControl, 7, 1},  // no CTRL on ZX: offer SYM instead
    {KEYCODE_SemiColon, 6, 1},    // L layout neighbours, shifted by user
    {KEYCODE_Comma, 7, 3}, {KEYCODE_Period, 7, 2},
    {KEYCODE_Slash, 7, 4}, {KEYCODE_Dash, 6, 3},
    {KEYCODE_Equals, 6, 1}, {KEYCODE_LeftBracket, 5, 3},
    {KEYCODE_RightBracket, 5, 1}, {KEYCODE_SingleQuote, 5, 0},
    {KEYCODE_BackQuote, 5, 0}, {KEYCODE_BackSlash, 5, 0},
    // Cursor arrows -> Cursor-joystick keys (5 left, 6 down, 7 up, 8 right).
    {KEYCODE_Left, 3, 4}, {KEYCODE_Down, 4, 4}, {KEYCODE_Up, 4, 3},
    {KEYCODE_Right, 4, 2},
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

int s_joystick_mode = 1;  // Kempston default
uint8_t s_kempston = 0;

void apply_joy_value(int port, int value) {
  // BMX joy layer sends port 1/2 absolute 5-bit + fire bit4 layouts;
  // fold every port into the single Kempston byte (machine has one port).
  (void)port;
  s_kempston = (uint8_t)(value & 0x1f);
  puerto_especial_joystick = s_kempston;
}

// ---- Autostart typer ------------------------------------------------------
// Matrix-level press/release events with frame-paced holds and gaps,
// driven one step per frame by input_pump_typer. Modifiers (SYM/CAPS) are
// held across runs of shifted chars like human typing: this avoids both
// debounce merges of repeated keys and E-mode toggle churn from lone
// SYM taps (which ate the second quote of autostart's "").

struct TypeEvent {
  int row;    // matrix row, or -2 for a wait event
  int bit;    // matrix bit, or wait frames for a wait event
  bool pressed;
};
TypeEvent s_type[512];
unsigned s_type_head = 0;
unsigned s_type_tail = 0;
unsigned s_type_wait = 0;  // frames to hold before next event
// Modifier state held by the emitted stream.
bool s_type_sym = false;
bool s_type_caps = false;

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

void type_wait(int frames) {
  type_emit(-2, frames, false);
}

// Press+release of one matrix key: 3-frame hold, 2-frame gap so the ROM's
// debounce sees press and release as distinct.
void type_key(int row, int bit) {
  type_emit(row, bit, true);
  type_wait(3);
  type_emit(row, bit, false);
  type_wait(2);
}

// Set the emitted modifier state, only emitting transitions.
void type_shift(bool sym, bool caps) {
  if (sym != s_type_sym) {
    type_emit(7, 1, sym);
    s_type_sym = sym;
  }
  if (caps != s_type_caps) {
    type_emit(0, 0, caps);
    s_type_caps = caps;
  }
}

void type_char(char c) {
  if (c >= 'a' && c <= 'z') {
    int row = -1, bit = 0;
    // letter rows mirror the HID map above.
    static const int8_t rows[26] = {1, 7, 0, 1, 2, 1, 1, 6, 5, 6,
                                    6, 6, 7, 7, 5, 5, 2, 2, 1, 2,
                                    5, 0, 2, 0, 5, 0};
    static const int8_t bits[26] = {0, 4, 3, 2, 2, 3, 4, 4, 2, 3,
                                    2, 1, 2, 3, 1, 0, 0, 3, 1, 4,
                                    3, 4, 1, 2, 4, 1};
    row = rows[c - 'a'];
    bit = bits[c - 'a'];
    type_shift(false, false);
    type_key(row, bit);
  } else if (c >= 'A' && c <= 'Z') {
    type_shift(s_type_sym, true);  // CAPS, held across runs
    type_char((char)(c - 'A' + 'a'));
  } else if (c >= '0' && c <= '9') {
    static const int8_t bits09[10] = {0, 0, 1, 2, 3, 4, 4, 3, 2, 1};
    static const int8_t rows09[10] = {4, 3, 3, 3, 3, 3, 4, 4, 4, 4};
    type_shift(false, false);
    type_key(rows09[c - '0'], bits09[c - '0']);
  } else {
    switch (c) {
      case ' ':
        type_shift(false, false);
        type_key(7, 0);
        break;
      case '\n':
        type_shift(false, false);
        type_key(6, 0);
        break;
      case '"':  // SYM SHIFT + P, SYM held across runs
        type_shift(true, false);
        type_key(5, 0);
        break;
      case ':':  // SYM SHIFT + Z
        type_shift(true, false);
        type_key(0, 1);
        break;
      case '.':
        type_shift(true, false);
        type_key(7, 2);
        break;
      default:
        break;
    }
  }
}

}  // namespace

void refresh_keyboard_matrix(void) {
  z80_byte *rows[8] = {&puerto_65278, &puerto_65022, &puerto_64510,
                       &puerto_63486, &puerto_61438, &puerto_57342,
                       &puerto_49150, &puerto_32766};
  for (int r = 0; r < 8; r++) {
    z80_byte v = 255;
    for (int b = 0; b < 5; b++) {
      if (s_keys[r][b]) {
        v = (z80_byte)(v & ~(1 << b));
      }
    }
    *rows[r] = v;
  }
}

void input_set_joystick(int mode) {
  if (mode < 0 || mode > 4) {
    mode = 0;
  }
  s_joystick_mode = mode;
  static const int kEmu[5] = {JOYSTICK_NONE, JOYSTICK_KEMPSTON,
                              JOYSTICK_SINCLAIR_1, JOYSTICK_SINCLAIR_2,
                              JOYSTICK_CURSOR};
  joystick_emulation = kEmu[mode];
  debug_log("input: joystick mode %d", mode);
}

int input_get_joystick(void) { return s_joystick_mode; }

void input_type_text_delayed(const char *text, unsigned wait_frames) {
  s_type_head = s_type_tail = 0;
  s_type_wait = wait_frames;
  // A fresh script starts with no modifiers held; clear any stuck state
  // from a previous run so keys never stick.
  s_type_sym = false;
  s_type_caps = false;
  s_keys[0][0] = false;
  s_keys[7][1] = false;
  refresh_keyboard_matrix();
  if (text == nullptr) {
    return;
  }
  for (const char *p = text; *p != '\0'; p++) {
    type_char(*p);
  }
  // Release everything at the end of the script.
  type_shift(false, false);
  type_wait(2);
  debug_log("input: typer queued %u events (wait %u)", s_type_tail - s_type_head,
            wait_frames);
}

void input_type_text(const char *text) {
  input_type_text_delayed(text, 100);  // ~2s for menu close + BASIC prompt
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
    // Wait event: hold the current matrix state.
    s_type_wait = (unsigned)s_type[i].bit;
  } else {
    s_keys[s_type[i].row][s_type[i].bit] = s_type[i].pressed;
    refresh_keyboard_matrix();
  }
  s_type_head++;
}

void poll_input(void) {
  // USB keyboard queue -> matrix latch.
  while (pending_emu_key.head != pending_emu_key.tail) {
    int i = pending_emu_key.head & 0xf;
    long key = pending_emu_key.key[i];
    int pressed = pending_emu_key.pressed[i];
    pending_emu_key.head++;
    if (key == KEYCODE_F12 || (key >= KEYCODE_F1 && key <= KEYCODE_F11)) {
      continue;  // BMX console/menu keys
    }
    int row = -1, bit = 0;
    if (lookup_key(key, &row, &bit)) {
      s_keys[row][bit] = (pressed != 0);
    }
  }
  refresh_keyboard_matrix();
  // USB sticks queue -> Kempston byte.
  while (pending_emu_joy.head != pending_emu_joy.tail) {
    int i = pending_emu_joy.head & 0x7f;
    int port = pending_emu_joy.port[i];
    int value = pending_emu_joy.value[i];
    int type = pending_emu_joy.type[i];
    pending_emu_joy.head++;
    if (type == PENDING_EMU_JOY_TYPE_ABSOLUTE) {
      apply_joy_value(port, value);
    } else if (type == PENDING_EMU_JOY_TYPE_OR) {
      s_kempston = (uint8_t)(s_kempston | (value & 0x1f));
      puerto_especial_joystick = s_kempston;
    } else if (type == PENDING_EMU_JOY_TYPE_AND) {
      s_kempston = (uint8_t)(s_kempston & (value & 0x1f));
      puerto_especial_joystick = s_kempston;
    }
  }
  input_pump_typer();
  // Menu toggle / quick functions (F12), same slot as the other kernels.
  ui_handle_toggle_or_quick_func();
}

}  // namespace zxspectrum
}  // namespace bmx

// The ZEsarUX core calls (*scr_actualiza_tablas_teclado)() on every 50 Hz
// interrupt; point it at the BMX matrix refresh (installed at boot).
extern "C" void zx_keyboard_refresh_hook(void) {
  bmx::zxspectrum::refresh_keyboard_matrix();
}

// VICE provides these in mousedrv.c; non-VICE kernels define them for
// mouse_input.cpp (atari800/dos pattern). No mouse on Spectrum: ignore.
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
