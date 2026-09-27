//
// amiga_input.cpp
//
// Amiga input for BMX bare-metal.
//
// USB keyboard events arrive through pending_emu_key (HID usage IDs, same
// numeric values as SDL scancodes) and are translated to raw Amiga keycodes
// (u8, 0x00-0x7F) via keyboard.press()/release(). USB mouse deltas accumulate
// in s_mouse_* and are forwarded to controlPort1.mouse every frame (port 1
// is the Amiga mouse port). USB joysticks latch per BMX port and drive
// controlPort2.joystick (port 2 = Amiga games port) with port 1 as primary.
//

#include "amiga_input.h"
#include "amiga_config.h"
#include "amiga_debug.h"

#include <stdio.h>

extern "C" {
#include <stdint.h>
#include "third_party/common/emux_api.h"
#include "third_party/common/keycodes.h"
}

#include "vaconfig.h"
#include "Emulator.h"
#include "Amiga.h"
#include "VAmiga.h"
#include "JoystickTypes.h"
#include "Keyboard.h"
#include "Joystick.h"
#include "Mouse.h"

namespace bmx {
namespace amiga {

int s_mouse_dx = 0;
int s_mouse_dy = 0;
int s_mouse_buttons = 0;

// BMX 5-bit latch: 0x01 up, 0x02 down, 0x04 left, 0x08 right, 0x10 fire.
int s_joy_latch[2] = {0, 0};

void drain_joy(void) {
  while (pending_emu_joy.head != pending_emu_joy.tail) {
    int i = pending_emu_joy.head & 0x7f;
    int value = pending_emu_joy.value[i];
    int type = pending_emu_joy.type[i];
    int port = pending_emu_joy.port[i];
    pending_emu_joy.head++;

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

}  // namespace amiga
}  // namespace bmx

namespace bmx {
namespace amiga {

// HID usage -> raw Amiga keycode (0x00-0x7F). -1 = no equivalent.
int hid_to_amiga(long hid) {
  switch (hid) {
  case KEYCODE_a: return 0x20;
  case KEYCODE_b: return 0x35;
  case KEYCODE_c: return 0x33;
  case KEYCODE_d: return 0x22;
  case KEYCODE_e: return 0x12;
  case KEYCODE_f: return 0x23;
  case KEYCODE_g: return 0x24;
  case KEYCODE_h: return 0x25;
  case KEYCODE_i: return 0x17;
  case KEYCODE_j: return 0x26;
  case KEYCODE_k: return 0x27;
  case KEYCODE_l: return 0x28;
  case KEYCODE_m: return 0x37;
  case KEYCODE_n: return 0x36;
  case KEYCODE_o: return 0x18;
  case KEYCODE_p: return 0x19;
  case KEYCODE_q: return 0x10;
  case KEYCODE_r: return 0x13;
  case KEYCODE_s: return 0x21;
  case KEYCODE_t: return 0x14;
  case KEYCODE_u: return 0x16;
  case KEYCODE_v: return 0x34;
  case KEYCODE_w: return 0x11;
  case KEYCODE_x: return 0x32;
  case KEYCODE_y: return 0x15;
  case KEYCODE_z: return 0x31;
  case KEYCODE_1: return 0x01;
  case KEYCODE_2: return 0x02;
  case KEYCODE_3: return 0x03;
  case KEYCODE_4: return 0x04;
  case KEYCODE_5: return 0x05;
  case KEYCODE_6: return 0x06;
  case KEYCODE_7: return 0x07;
  case KEYCODE_8: return 0x08;
  case KEYCODE_9: return 0x09;
  case KEYCODE_0: return 0x0A;
  case KEYCODE_Return: return 0x44;
  case KEYCODE_Escape: return 0x45;
  case KEYCODE_Backspace: return 0x41;
  case KEYCODE_Tab: return 0x42;
  case KEYCODE_Space: return 0x40;
  case KEYCODE_Dash: return 0x0B;
  case KEYCODE_Equals: return 0x0C;
  case KEYCODE_LeftBracket: return 0x1A;
  case KEYCODE_RightBracket: return 0x1B;
  case KEYCODE_BackSlash: return 0x0D;
  case KEYCODE_SemiColon: return 0x29;
  case KEYCODE_SingleQuote: return 0x2A;
  case KEYCODE_BackQuote: return 0x00;
  case KEYCODE_Comma: return 0x38;
  case KEYCODE_Period: return 0x39;
  case KEYCODE_Slash: return 0x3A;
  case KEYCODE_CapsLock: return 0x62;
  case KEYCODE_F1: return 0x50;
  case KEYCODE_F2: return 0x51;
  case KEYCODE_F3: return 0x52;
  case KEYCODE_F4: return 0x53;
  case KEYCODE_F5: return 0x54;
  case KEYCODE_F6: return 0x55;
  case KEYCODE_F7: return 0x56;
  case KEYCODE_F8: return 0x57;
  case KEYCODE_F9: return 0x58;
  case KEYCODE_F10: return 0x59;
  case KEYCODE_Delete: return 0x46;
  case KEYCODE_Right: return 0x4E;
  case KEYCODE_Left: return 0x4F;
  case KEYCODE_Down: return 0x4D;
  case KEYCODE_Up: return 0x4C;
  case KEYCODE_KP_Divide: return 0x5C;
  case KEYCODE_KP_Multiply: return 0x5D;
  case KEYCODE_KP_Subtract: return 0x4A;
  case KEYCODE_KP_Add: return 0x5E;
  case KEYCODE_KP_Enter: return 0x43;
  case KEYCODE_KP1: return 0x1D;
  case KEYCODE_KP2: return 0x1E;
  case KEYCODE_KP3: return 0x1F;
  case KEYCODE_KP4: return 0x2D;
  case KEYCODE_KP5: return 0x2E;
  case KEYCODE_KP6: return 0x2F;
  case KEYCODE_KP7: return 0x3D;
  case KEYCODE_KP8: return 0x3E;
  case KEYCODE_KP9: return 0x3F;
  case KEYCODE_KP0: return 0x0F;
  case KEYCODE_KP_Decimal: return 0x3C;
  default: return -1;
  }
}

void apply_joy_latch(vamiga::Joystick &joy, int latch, int prev) {
  int changed = latch ^ prev;
  if (changed & 0x01) {
    joy.trigger((latch & 0x01) ? vamiga::GamePadAction::PULL_UP
                               : vamiga::GamePadAction::RELEASE_Y);
  }
  if (changed & 0x02) {
    joy.trigger((latch & 0x02) ? vamiga::GamePadAction::PULL_DOWN
                               : vamiga::GamePadAction::RELEASE_Y);
  }
  if (changed & 0x04) {
    joy.trigger((latch & 0x04) ? vamiga::GamePadAction::PULL_LEFT
                               : vamiga::GamePadAction::RELEASE_X);
  }
  if (changed & 0x08) {
    joy.trigger((latch & 0x08) ? vamiga::GamePadAction::PULL_RIGHT
                               : vamiga::GamePadAction::RELEASE_X);
  }
  if (changed & 0x10) {
    joy.trigger((latch & 0x10) ? vamiga::GamePadAction::PRESS_FIRE
                               : vamiga::GamePadAction::RELEASE_FIRE);
  }
}

void poll_input(bmx::amiga::amiga_input_t *input) {
  if (input != NULL) {
    input->unused = 0;
  }
  vamiga::VAmiga *v = (vamiga::VAmiga *)config_emu();
  if (v == NULL || v->keyboard.keyboard == NULL) {
    return;
  }
  vamiga::Keyboard *kb = v->keyboard.keyboard;
  vamiga::Joystick *joy2 = v->controlPort2.joystick.joystick;
  vamiga::Joystick *joy1 = v->controlPort1.joystick.joystick;
  vamiga::Mouse *mouse1 = v->controlPort1.mouse.mouse;

  bmx::amiga::drain_joy();

  // Keyboard queue -> Amiga raw codes (F12 reserved for the BMX menu).
  while (pending_emu_key.head != pending_emu_key.tail) {
    int i = pending_emu_key.head & 0xf;
    long key = pending_emu_key.key[i];
    int pressed = pending_emu_key.pressed[i];
    pending_emu_key.head++;

    if (key == KEYCODE_F12) {
      continue;
    }
    if (key == KEYCODE_LeftShift) {
      pressed ? kb->press(0x60) : kb->release(0x60);
      continue;
    }
    if (key == KEYCODE_RightShift) {
      pressed ? kb->press(0x61) : kb->release(0x61);
      continue;
    }
    if (key == KEYCODE_LeftControl || key == KEYCODE_RightControl) {
      pressed ? kb->press(0x63) : kb->release(0x63);
      continue;
    }
    if (key == KEYCODE_LeftAlt) {
      pressed ? kb->press(0x64) : kb->release(0x64);
      continue;
    }
    if (key == KEYCODE_RightAlt) {
      pressed ? kb->press(0x65) : kb->release(0x65);
      continue;
    }
    if (key == KEYCODE_LeftSuper) {
      pressed ? kb->press(0x66) : kb->release(0x66);
      continue;
    }
    if (key == KEYCODE_RightSuper) {
      pressed ? kb->press(0x67) : kb->release(0x67);
      continue;
    }
    int ak = hid_to_amiga(key);
    if (ak >= 0) {
      try {
        pressed ? kb->press((vamiga::KeyCode)ak)
                : kb->release((vamiga::KeyCode)ak);
      } catch (...) {
      }
    }
  }

  // Joystick latches -> port 2 joystick (primary games port). Edge-trigger
  // on change only; static state keeps the polling cheap.
  {
    static int prev0 = 0, prev1 = 0;
    try {
      if (joy2 != NULL) {
        apply_joy_latch(*joy2, bmx::amiga::s_joy_latch[0], prev0);
        prev0 = bmx::amiga::s_joy_latch[0];
      }
      if (joy1 != NULL) {
        apply_joy_latch(*joy1, bmx::amiga::s_joy_latch[1], prev1);
        prev1 = bmx::amiga::s_joy_latch[1];
      }
    } catch (...) {
    }
  }

  // Mouse deltas -> port 1 mouse; buttons level-mapped.
  try {
    if (mouse1 == NULL) {
      return;
    }
    if (bmx::amiga::s_mouse_dx != 0 || bmx::amiga::s_mouse_dy != 0) {
      mouse1->setDxDy((double)bmx::amiga::s_mouse_dx,
                      (double)bmx::amiga::s_mouse_dy);
      bmx::amiga::s_mouse_dx = 0;
      bmx::amiga::s_mouse_dy = 0;
    }
    static int prev_buttons = 0;
    int cur = bmx::amiga::s_mouse_buttons;
    if ((cur ^ prev_buttons) & 0x01) {
      mouse1->trigger(
          (cur & 0x01) ? vamiga::GamePadAction::PRESS_LEFT
                       : vamiga::GamePadAction::RELEASE_LEFT);
    }
    if ((cur ^ prev_buttons) & 0x02) {
      mouse1->trigger(
          (cur & 0x02) ? vamiga::GamePadAction::PRESS_RIGHT
                       : vamiga::GamePadAction::RELEASE_RIGHT);
    }
    if ((cur ^ prev_buttons) & 0x04) {
      mouse1->trigger(
          (cur & 0x04) ? vamiga::GamePadAction::PRESS_MIDDLE
                       : vamiga::GamePadAction::RELEASE_MIDDLE);
    }
    prev_buttons = cur;
  } catch (...) {
  }
}

}  // namespace amiga
}  // namespace bmx

// Mouse driver seam (mirrors atarist_input.cpp): VICE mousedrv.c is not
// linked for non-VICE kernels, so these live here with C linkage.
extern "C" {

void emu_mouse_move(int x, int y) {
  bmx::amiga::s_mouse_dx += x;
  bmx::amiga::s_mouse_dy += y;
}

void emu_mouse_button_left(int pressed) {
  if (pressed)
    bmx::amiga::s_mouse_buttons |= 0x01;
  else
    bmx::amiga::s_mouse_buttons &= ~0x01;
}

void emu_mouse_button_right(int pressed) {
  if (pressed)
    bmx::amiga::s_mouse_buttons |= 0x02;
  else
    bmx::amiga::s_mouse_buttons &= ~0x02;
}

void emu_mouse_button_middle(int pressed) {
  if (pressed)
    bmx::amiga::s_mouse_buttons |= 0x04;
  else
    bmx::amiga::s_mouse_buttons &= ~0x04;
}

void emu_mouse_wheel_up(int pressed) {
  (void)pressed;
}

void emu_mouse_wheel_down(int pressed) {
  (void)pressed;
}

}  // extern "C"
