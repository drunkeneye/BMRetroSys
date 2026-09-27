//
// atarist_input.cpp
//
// Hatari input for BMX bare-metal.
//
// USB keyboard events arrive through the standard pending_emu_key queue
// (HID usage IDs, identical numeric values to SDL scancodes) and are
// translated to Atari ST scancodes with Hatari's own table from
// sdl/keymap.c, then injected via IKBD_PressSTKey(). USB mouse motion and
// buttons arrive via emu_mouse_* (same TU accumulators) and are forwarded
// into the IKBD mouse state every frame, which is what GEM needs.
//

#include "atarist_input.h"
#include "atarist_debug.h"

#include <stdio.h>

extern "C" {
#include <stdint.h>
#include "ikbd.h"
#include "joy.h"
#include "joy_ui.h"
#include "third_party/common/circle.h"
#include "third_party/common/emux_api.h"
#include "third_party/common/keycodes.h"
}

namespace bmx {
namespace atarist {

// Mouse deltas accumulate on core 0 (USB) and are consumed per-frame on
// core 1. Buttons are level state (bit0=left, bit1=right, bit2=middle).
int s_mouse_dx = 0;
int s_mouse_dy = 0;
int s_mouse_buttons = 0;

// Joystick latches (BMX 5-bit form: 0x01 up, 0x02 down, 0x04 left,
// 0x08 right, 0x10 fire). Index 0 = BMX port 1, 1 = BMX port 2.
int s_joy_latch[2] = {0, 0};

void drain_joy(void) {
  // Same monotonic-counter rule as the key queue: never wrap head itself.
  while (pending_emu_joy.head != pending_emu_joy.tail) {
    int i = pending_emu_joy.head & 0x7f;
    int value = pending_emu_joy.value[i];
    int type = pending_emu_joy.type[i];
    int port = pending_emu_joy.port[i];
    pending_emu_joy.head++;

    // Queue ports are 1-based. BMX port 1 is the primary ST stick.
    int idx = -1;
    if (port == 1) {
      idx = 0;
    } else if (port == 2) {
      idx = 1;
    }
    if (idx < 0) {
      continue;
    }
    int bits = value & 0x1f;
    switch (type) {
    case PENDING_EMU_JOY_TYPE_ABSOLUTE:
      s_joy_latch[idx] = bits;
      break;
    case PENDING_EMU_JOY_TYPE_AND:
      s_joy_latch[idx] &= bits;
      break;
    case PENDING_EMU_JOY_TYPE_OR:
      s_joy_latch[idx] |= bits;
      break;
    default:
      break;
    }
  }
}

}  // namespace atarist
}  // namespace bmx

// C-linkage joystick state reader for the C JoyUI backend.
extern "C" void atarist_joy_state(int st_id, int *x, int *y, int *buttons) {
  // BMX port 1 -> ST stick 1 (JOYID_JOYSTICK1), port 2 -> ST stick 0.
  int latch = (st_id == 1) ? bmx::atarist::s_joy_latch[0] : bmx::atarist::s_joy_latch[1];
  int ix = 0, iy = 0, ib = 0;
  if (latch & 0x04) {
    ix = -20000;
  } else if (latch & 0x08) {
    ix = 20000;
  }
  if (latch & 0x01) {
    iy = -20000;
  } else if (latch & 0x02) {
    iy = 20000;
  }
  if (latch & 0x10) {
    ib = JOYREADING_BUTTON1;
  }
  if (x != NULL) {
    *x = ix;
  }
  if (y != NULL) {
    *y = iy;
  }
  if (buttons != NULL) {
    *buttons = ib;
  }
}

namespace bmx {
namespace atarist {

// HID usage -> ST scancode (mirrors Hatari sdl/keymap.c Keymap_GetKeySTScanCode;
// SDL scancodes are USB HID usage IDs, same as BMX KEYCODE_*).
// Returns -1 for keys with no ST equivalent.
int hid_to_st(long hid) {
  switch (hid) {
  case KEYCODE_a: return 0x1e;
  case KEYCODE_b: return 0x30;
  case KEYCODE_c: return 0x2e;
  case KEYCODE_d: return 0x20;
  case KEYCODE_e: return 0x12;
  case KEYCODE_f: return 0x21;
  case KEYCODE_g: return 0x22;
  case KEYCODE_h: return 0x23;
  case KEYCODE_i: return 0x17;
  case KEYCODE_j: return 0x24;
  case KEYCODE_k: return 0x25;
  case KEYCODE_l: return 0x26;
  case KEYCODE_m: return 0x32;
  case KEYCODE_n: return 0x31;
  case KEYCODE_o: return 0x18;
  case KEYCODE_p: return 0x19;
  case KEYCODE_q: return 0x10;
  case KEYCODE_r: return 0x13;
  case KEYCODE_s: return 0x1f;
  case KEYCODE_t: return 0x14;
  case KEYCODE_u: return 0x16;
  case KEYCODE_v: return 0x2f;
  case KEYCODE_w: return 0x11;
  case KEYCODE_x: return 0x2d;
  case KEYCODE_y: return 0x15;
  case KEYCODE_z: return 0x2c;
  case KEYCODE_1: return 0x02;
  case KEYCODE_2: return 0x03;
  case KEYCODE_3: return 0x04;
  case KEYCODE_4: return 0x05;
  case KEYCODE_5: return 0x06;
  case KEYCODE_6: return 0x07;
  case KEYCODE_7: return 0x08;
  case KEYCODE_8: return 0x09;
  case KEYCODE_9: return 0x0a;
  case KEYCODE_0: return 0x0b;
  case KEYCODE_Return: return 0x1c;
  case KEYCODE_Escape: return 0x01;
  case KEYCODE_Backspace: return 0x0e;
  case KEYCODE_Tab: return 0x0f;
  case KEYCODE_Space: return 0x39;
  case KEYCODE_Dash: return 0x0c;
  case KEYCODE_Equals: return 0x0d;
  case KEYCODE_LeftBracket: return 0x1a;
  case KEYCODE_RightBracket: return 0x1b;
  case KEYCODE_BackSlash: return 0x29;
  case KEYCODE_Pound: return 0x2b;
  case KEYCODE_SemiColon: return 0x27;
  case KEYCODE_SingleQuote: return 0x28;
  case KEYCODE_BackQuote: return 0x2b;
  case KEYCODE_Comma: return 0x33;
  case KEYCODE_Period: return 0x34;
  case KEYCODE_Slash: return 0x35;
  case KEYCODE_CapsLock: return 0x3a;
  case KEYCODE_F1: return 0x3b;
  case KEYCODE_F2: return 0x3c;
  case KEYCODE_F3: return 0x3d;
  case KEYCODE_F4: return 0x3e;
  case KEYCODE_F5: return 0x3f;
  case KEYCODE_F6: return 0x40;
  case KEYCODE_F7: return 0x41;
  case KEYCODE_F8: return 0x42;
  case KEYCODE_F9: return 0x43;
  case KEYCODE_F10: return 0x44;
  case KEYCODE_Insert: return 0x52;
  case KEYCODE_Home: return 0x47;
  case KEYCODE_PageUp: return 0x63;
  case KEYCODE_Delete: return 0x53;
  case KEYCODE_End: return 0x2b;
  case KEYCODE_PageDown: return 0x64;
  case KEYCODE_Right: return 0x4d;
  case KEYCODE_Left: return 0x4b;
  case KEYCODE_Down: return 0x50;
  case KEYCODE_Up: return 0x48;
  case KEYCODE_KP_Divide: return 0x65;
  case KEYCODE_KP_Multiply: return 0x66;
  case KEYCODE_KP_Subtract: return 0x4a;
  case KEYCODE_KP_Add: return 0x4e;
  case KEYCODE_KP_Enter: return 0x72;
  case KEYCODE_KP1: return 0x6d;
  case KEYCODE_KP2: return 0x6e;
  case KEYCODE_KP3: return 0x6f;
  case KEYCODE_KP4: return 0x6a;
  case KEYCODE_KP5: return 0x6b;
  case KEYCODE_KP6: return 0x6c;
  case KEYCODE_KP7: return 0x67;
  case KEYCODE_KP8: return 0x68;
  case KEYCODE_KP9: return 0x69;
  case KEYCODE_KP0: return 0x70;
  case KEYCODE_KP_Decimal: return 0x71;
  default: return -1;
  }
}

void poll_input(bmx::atarist::atarist_input_t *input) {
  if (input != NULL) {
    input->unused = 0;
  }

  // Drain the USB joystick queue into the port latches (consumed
  // asynchronously by JoyUI_ReadJoystick via Joy_GetStickData).
  bmx::atarist::drain_joy();

  // Drain the USB keyboard queue into the IKBD processor.
  while (pending_emu_key.head != pending_emu_key.tail) {
    int i = pending_emu_key.head & 0xf;
    long key = pending_emu_key.key[i];
    int pressed = pending_emu_key.pressed[i];
    pending_emu_key.head++;

    // F12 is the BMX menu key (handled by the frame-loop pump); never
    // forward it to the ST (Hatari maps it to Undo).
    if (key == KEYCODE_F12) {
      continue;
    }
    // Modifier keys (shift/ctrl/alt both sides).
    if (key == KEYCODE_LeftShift) {
      IKBD_PressSTKey(0x2a, pressed ? true : false);
      continue;
    }
    if (key == KEYCODE_RightShift) {
      IKBD_PressSTKey(0x36, pressed ? true : false);
      continue;
    }
    if (key == KEYCODE_LeftControl || key == KEYCODE_RightControl) {
      IKBD_PressSTKey(0x1d, pressed ? true : false);
      continue;
    }
    if (key == KEYCODE_LeftAlt || key == KEYCODE_RightAlt) {
      IKBD_PressSTKey(0x38, pressed ? true : false);
      continue;
    }
    int st = hid_to_st(key);
    if (st >= 0) {
      IKBD_PressSTKey((uint8_t)st, pressed ? true : false);
    }
  }

  // Forward accumulated USB mouse motion/buttons (emu_mouse_* producers run
  // on the USB path; consume here on the emulator thread).
  if (bmx::atarist::s_mouse_dx != 0 || bmx::atarist::s_mouse_dy != 0) {
    KeyboardProcessor.Mouse.dx += bmx::atarist::s_mouse_dx;
    KeyboardProcessor.Mouse.dy += bmx::atarist::s_mouse_dy;
    bmx::atarist::s_mouse_dx = 0;
    bmx::atarist::s_mouse_dy = 0;
  }
  if (bmx::atarist::s_mouse_buttons & 0x01) {
    Keyboard.bLButtonDown |= BUTTON_MOUSE;
  } else {
    Keyboard.bLButtonDown &= ~BUTTON_MOUSE;
  }
  if (bmx::atarist::s_mouse_buttons & 0x02) {
    Keyboard.bRButtonDown |= BUTTON_MOUSE;
  } else {
    Keyboard.bRButtonDown &= ~BUTTON_MOUSE;
  }
}

}  // namespace atarist
}  // namespace bmx

// VICE provides these in arch/raspi/mousedrv.c; ATARIST builds link no VICE
// archives so the BMX mouse driver (src/mouse_input.cpp) expects them here,
// mirroring src/dos/dos_input.cpp. C linkage to match
// mouse_input.cpp's extern "C" declarations.
extern "C" {

void emu_mouse_move(int x, int y) {
  bmx::atarist::s_mouse_dx += x;
  bmx::atarist::s_mouse_dy += y;
}

void emu_mouse_button_left(int pressed) {
  if (pressed)
    bmx::atarist::s_mouse_buttons |= 0x01;
  else
    bmx::atarist::s_mouse_buttons &= ~0x01;
}

void emu_mouse_button_right(int pressed) {
  if (pressed)
    bmx::atarist::s_mouse_buttons |= 0x02;
  else
    bmx::atarist::s_mouse_buttons &= ~0x02;
}

void emu_mouse_button_middle(int pressed) {
  if (pressed)
    bmx::atarist::s_mouse_buttons |= 0x04;
  else
    bmx::atarist::s_mouse_buttons &= ~0x04;
}

void emu_mouse_wheel_up(int pressed) {
  (void)pressed;
}

void emu_mouse_wheel_down(int pressed) {
  (void)pressed;
}

}  // extern "C"
