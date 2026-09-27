//
// dos_input.cpp
//
// DOSBox-X input for BMX bare-metal (Phase 0 placeholder)
//

#include "dos_input.h"

#include <stddef.h>

//
// dos_input.cpp
//
// DOSBox-X input for BMX bare-metal: drains the BMX key queue
// (pending_emu_key, fed by USB via emu_key_pressed) and injects oskeys into
// the guest via KEYBOARD_AddKey (scanset translation is internal to the
// guest keyboard controller emulation). F12 stays with the BMX menu.
//

#include "dos_input.h"
#include "dos_debug.h"
#include "dos_perf.h"
#include "dos_sound.h"

#include "third_party/common/emux_api.h"
#include "third_party/common/keycodes.h"
#include "third_party/dosbox-x/include/keyboard.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

extern "C" {
#include "third_party/common/ui.h"
#include "third_party/common/circle.h"
}

// Wall clock for typer pacing (defined by the platform layer; ms).
extern "C" uint32_t bmx_host_ticks_ms(void);

namespace bmx {
namespace dos {

namespace {

// USB HID usage -> DOSBox-X logical key. KBD_KEYS mirrors the HID layout
// for the main block, so this is mostly 1:1 by name.
KBD_KEYS hid_to_kbd(long hid) {
  switch (hid) {
  case KEYCODE_a: return KBD_a;
  case KEYCODE_b: return KBD_b;
  case KEYCODE_c: return KBD_c;
  case KEYCODE_d: return KBD_d;
  case KEYCODE_e: return KBD_e;
  case KEYCODE_f: return KBD_f;
  case KEYCODE_g: return KBD_g;
  case KEYCODE_h: return KBD_h;
  case KEYCODE_i: return KBD_i;
  case KEYCODE_j: return KBD_j;
  case KEYCODE_k: return KBD_k;
  case KEYCODE_l: return KBD_l;
  case KEYCODE_m: return KBD_m;
  case KEYCODE_n: return KBD_n;
  case KEYCODE_o: return KBD_o;
  case KEYCODE_p: return KBD_p;
  case KEYCODE_q: return KBD_q;
  case KEYCODE_r: return KBD_r;
  case KEYCODE_s: return KBD_s;
  case KEYCODE_t: return KBD_t;
  case KEYCODE_u: return KBD_u;
  case KEYCODE_v: return KBD_v;
  case KEYCODE_w: return KBD_w;
  case KEYCODE_x: return KBD_x;
  case KEYCODE_y: return KBD_y;
  case KEYCODE_z: return KBD_z;
  case KEYCODE_1: return KBD_1;
  case KEYCODE_2: return KBD_2;
  case KEYCODE_3: return KBD_3;
  case KEYCODE_4: return KBD_4;
  case KEYCODE_5: return KBD_5;
  case KEYCODE_6: return KBD_6;
  case KEYCODE_7: return KBD_7;
  case KEYCODE_8: return KBD_8;
  case KEYCODE_9: return KBD_9;
  case KEYCODE_0: return KBD_0;
  case KEYCODE_Return: return KBD_enter;
  case KEYCODE_Escape: return KBD_esc;
  case KEYCODE_Backspace: return KBD_backspace;
  case KEYCODE_Tab: return KBD_tab;
  case KEYCODE_Space: return KBD_space;
  case KEYCODE_Dash: return KBD_minus;
  case KEYCODE_Equals: return KBD_equals;
  case KEYCODE_LeftBracket: return KBD_leftbracket;
  case KEYCODE_RightBracket: return KBD_rightbracket;
  case KEYCODE_BackSlash: return KBD_backslash;
  case KEYCODE_Pound: return KBD_extra_lt_gt;
  case KEYCODE_SemiColon: return KBD_semicolon;
  case KEYCODE_SingleQuote: return KBD_quote;
  case KEYCODE_BackQuote: return KBD_grave;
  case KEYCODE_Comma: return KBD_comma;
  case KEYCODE_Period: return KBD_period;
  case KEYCODE_Slash: return KBD_slash;
  case KEYCODE_CapsLock: return KBD_capslock;
  case KEYCODE_F1: return KBD_f1;
  case KEYCODE_F2: return KBD_f2;
  case KEYCODE_F3: return KBD_f3;
  case KEYCODE_F4: return KBD_f4;
  case KEYCODE_F5: return KBD_f5;
  case KEYCODE_F6: return KBD_f6;
  case KEYCODE_F7: return KBD_f7;
  case KEYCODE_F8: return KBD_f8;
  case KEYCODE_F9: return KBD_f9;
  case KEYCODE_F10: return KBD_f10;
  case KEYCODE_F11: return KBD_f11;
  case KEYCODE_ScrollLock: return KBD_scrolllock;
  case KEYCODE_Pause: return KBD_pause;
  case KEYCODE_Insert: return KBD_insert;
  case KEYCODE_Home: return KBD_home;
  case KEYCODE_PageUp: return KBD_pageup;
  case KEYCODE_Delete: return KBD_delete;
  case KEYCODE_End: return KBD_end;
  case KEYCODE_PageDown: return KBD_pagedown;
  case KEYCODE_Right: return KBD_right;
  case KEYCODE_Left: return KBD_left;
  case KEYCODE_Down: return KBD_down;
  case KEYCODE_Up: return KBD_up;
  case KEYCODE_NumLock: return KBD_numlock;
  case KEYCODE_KP_Divide: return KBD_kpdivide;
  case KEYCODE_KP_Multiply: return KBD_kpmultiply;
  case KEYCODE_KP_Subtract: return KBD_kpminus;
  case KEYCODE_KP_Add: return KBD_kpplus;
  case KEYCODE_KP_Enter: return KBD_kpenter;
  case KEYCODE_KP1: return KBD_kp1;
  case KEYCODE_KP2: return KBD_kp2;
  case KEYCODE_KP3: return KBD_kp3;
  case KEYCODE_KP4: return KBD_kp4;
  case KEYCODE_KP5: return KBD_kp5;
  case KEYCODE_KP6: return KBD_kp6;
  case KEYCODE_KP7: return KBD_kp7;
  case KEYCODE_KP8: return KBD_kp8;
  case KEYCODE_KP9: return KBD_kp9;
  case KEYCODE_KP0: return KBD_kp0;
  case KEYCODE_KP_Decimal: return KBD_kpperiod;
  case KEYCODE_Application: return KBD_rwinmenu;
  case KEYCODE_LeftShift: return KBD_leftshift;
  case KEYCODE_RightShift: return KBD_rightshift;
  case KEYCODE_LeftControl: return KBD_leftctrl;
  case KEYCODE_RightControl: return KBD_rightctrl;
  case KEYCODE_LeftAlt: return KBD_leftalt;
  case KEYCODE_RightAlt: return KBD_rightalt;
  case KEYCODE_LeftSuper: return KBD_lwindows;
  case KEYCODE_RightSuper: return KBD_rwindows;
  default: return KBD_NONE;
  }
}

}  // namespace

// Drain the BMX key queue into the guest keyboard controller. F12 is
// owned by the BMX menu layer (ui_toggle_pending) and never forwarded.
void drain_guest_keys(void) {
  static bool s_first_key_logged = false;
  while (pending_emu_key.head != pending_emu_key.tail) {
    int i = pending_emu_key.head & 0xf;
    long key = pending_emu_key.key[i];
    int pressed = pending_emu_key.pressed[i];
    pending_emu_key.head++;
    if (key == KEYCODE_F12) {
      continue;
    }
    KBD_KEYS kbd = hid_to_kbd(key);
    if (!s_first_key_logged) {
      s_first_key_logged = true;
      debug_log("dos input: first key hid=%ld kbd=%d pressed=%d", key,
                (int)kbd, pressed);
    }
    if (kbd != KBD_NONE) {
      KEYBOARD_AddKey(kbd, pressed != 0);
    }
  }
}

// Phase 0 mouse accumulator. Drained by the real poll_input in Phase 3;
// until then the values just accumulate so the menu mouse layer links.
static int s_mouse_dx = 0;
static int s_mouse_dy = 0;
static int s_mouse_buttons = 0;

void poll_input(dos_input_t *input) {
  if (input != NULL) {
    input->unused = 0;
  }
  drain_guest_keys();
}

// ---- Menu-initiated reboot ------------------------------------------------
// Single writer (core0 menu) / single reader (core1 guest thread via
// GFX_Events); plain volatile int is sufficient, no locking.
static volatile int s_reset_request = 0;

void dos_request_reset(int code) {
  s_reset_request = (code == 3) ? 3 : 6;
  debug_log("dos input: reset requested (%s)", code == 3 ? "hard" : "soft");
}

int dos_take_reset_request(void) {
  int code = s_reset_request;
  s_reset_request = 0;
  return code;
}

// ---- Autostart typer ------------------------------------------------------
// Staging buffer of HID press/release events, filled by core0 (menu) and
// dripped into pending_emu_key by the guest thread. 512 events cover a
// ~250-char command script; longer scripts are truncated with a log.
static long s_type_key[512] = {0};
static int s_type_pressed[512] = {0};
static unsigned s_type_head = 0;
static unsigned s_type_tail = 0;
static uint32_t s_type_start_ms = 0;
static uint32_t s_type_last_ms = 0;

void dos_type_emit(long hid, int pressed) {
  unsigned next = (s_type_tail + 1) & 0x1ff;
  if (next == (s_type_head & 0x1ff)) {
    debug_log("dos input: typer buffer full, dropping input");
    return;
  }
  s_type_key[s_type_tail & 0x1ff] = hid;
  s_type_pressed[s_type_tail & 0x1ff] = pressed;
  s_type_tail++;
}

// Appends one press+release pair, expanding Shift combinations.
void dos_type_char_events(char c) {
  if (c == '\n') {
    dos_type_emit(KEYCODE_Return, 1);
    dos_type_emit(KEYCODE_Return, 0);
    return;
  }
  long hid = -1;
  bool shift = false;
  if (c >= 'a' && c <= 'z') {
    hid = KEYCODE_a + (c - 'a');
  } else if (c >= 'A' && c <= 'Z') {
    hid = KEYCODE_a + (c - 'A');
  } else if (c >= '0' && c <= '9') {
    hid = (c == '0') ? KEYCODE_0 : KEYCODE_1 + (c - '1');
  } else {
    switch (c) {
      case ' ': hid = KEYCODE_Space; break;
      case '.': hid = KEYCODE_Period; break;
      case '/': hid = KEYCODE_Slash; break;
      case '-': hid = KEYCODE_Dash; break;
      case ':': hid = KEYCODE_SemiColon; shift = true; break;
      case '_': hid = KEYCODE_Dash; shift = true; break;
      case '"': hid = KEYCODE_SingleQuote; shift = true; break;
      default: break;
    }
  }
  if (hid < 0) {
    return;
  }
  if (shift) {
    dos_type_emit(KEYCODE_LeftShift, 1);
  }
  dos_type_emit(hid, 1);
  dos_type_emit(hid, 0);
  if (shift) {
    dos_type_emit(KEYCODE_LeftShift, 0);
  }
}

void dos_type_text(const char *text) {
  if (text == NULL) {
    return;
  }
  // A new script replaces any still-pending keystrokes (re-selecting in the
  // menu restarts cleanly instead of concatenating onto a half-typed line)
  // and restarts the initial delay.
  s_type_head = s_type_tail = 0;
  for (const char *p = text; *p != '\0'; p++) {
    dos_type_char_events(*p);
  }
  uint32_t now = bmx_host_ticks_ms();
  // Brief pause for menu close + shell prompt before the first keystroke
  // lands. The menu already shows "Starting...", so keep this short: a long
  // silence reads as "nothing happens" and invites a double select.
  s_type_start_ms = now + 1500;
  s_type_last_ms = 0;
  debug_log("dos input: autostart script queued (%u events)",
            s_type_tail - s_type_head);
}

// Moves staged typer events into the live key queue: at most one event per
// call, at most every 20ms, only while the live queue has room. Called from
// GFX_Events (guest thread), so typeahead never overflows the guest side.
void pump_autostart_typer(void) {
  if (s_type_head == s_type_tail) {
    return;
  }
  uint32_t now = bmx_host_ticks_ms();
  if (now < s_type_start_ms) {
    return;
  }
  if (s_type_last_ms != 0 && now - s_type_last_ms < 20) {
    return;
  }
  unsigned used = pending_emu_key.tail - pending_emu_key.head;
  if (used >= 12) {
    return;
  }
  unsigned i = s_type_head & 0x1ff;
  int t = pending_emu_key.tail & 0xf;
  pending_emu_key.key[t] = s_type_key[i];
  pending_emu_key.mod[t] = 0;
  pending_emu_key.pressed[t] = s_type_pressed[i];
  pending_emu_key.tail++;
  s_type_head++;
  s_type_last_ms = now;
  if (s_type_head == s_type_tail) {
    s_type_start_ms = 0;
    debug_log("dos input: autostart script fully typed");
  }
}

}  // namespace dos
}  // namespace bmx

// VICE provides these in arch/raspi/mousedrv.c; DOS builds link no VICE
// archives so the BMX mouse driver (src/mouse_input.cpp) expects them here,
// mirroring src/atari800/atari800_input.cpp:478-516. C linkage to match
// mouse_input.cpp's extern "C" declarations.
extern "C" {

void emu_mouse_move(int x, int y) {
  bmx::dos::s_mouse_dx += x;
  bmx::dos::s_mouse_dy += y;
}

void emu_mouse_button_left(int pressed) {
  if (pressed)
    bmx::dos::s_mouse_buttons |= 0x01;
  else
    bmx::dos::s_mouse_buttons &= ~0x01;
}

void emu_mouse_button_right(int pressed) {
  if (pressed)
    bmx::dos::s_mouse_buttons |= 0x02;
  else
    bmx::dos::s_mouse_buttons &= ~0x02;
}

void emu_mouse_button_middle(int pressed) {
  if (pressed)
    bmx::dos::s_mouse_buttons |= 0x04;
  else
    bmx::dos::s_mouse_buttons &= ~0x04;
}

void emu_mouse_wheel_up(int pressed) {
  (void)pressed;
}

void emu_mouse_wheel_down(int pressed) {
  (void)pressed;
}

}  // extern "C"

// Host-event pump for the guest loop (deleted sdlmain.cpp GFX_Events).
// Normal_Loop calls this on every idle slice, INCLUDING slices nested
// inside SHELL_Run (which blocks from the first boot slice until the shell
// exits). Without this, keyboard input and the F12 menu would die as soon
// as DOS boots. Same thread as the frame loop (core1), so no locking.
//
// A pending menu reboot request throws here so it unwinds through nested
// SHELL_Run exactly like a guest reboot (run_guest_slice catches int 3/6
// and re-dispatches RESET/RESET_END, then BIOS POST boots DOS fresh).
void GFX_Events() {
  // One-shot: the outer frame loop parks inside the first guest slice
  // forever (SHELL_Run never returns), so its circle_boot_complete() never
  // runs — and that is what creates the ViceSound device + publishes the
  // sound output. Without this there is no sink (writes vanish, bufferspace
  // stuck full, menu shows Output=None). Same timing as the outer-loop
  // first frame (~1s, BIOS POST running); idempotent if that ever runs.
  static bool s_boot_completed = false;
  static uint32_t s_boot_complete_ms = 0;
  static bool s_runtime_quiet = false;
  if (!s_boot_completed) {
    s_boot_completed = true;
    circle_boot_complete();
    s_boot_complete_ms = bmx_host_ticks_ms();
    bmx::dos::debug_log("GFX_Events: boot complete signalled");
  }
  // Runtime log quiet ~8s after boot-complete: keeps the full DOS boot
  // (XMS/COMMAND.COM/file opens) and early chatter, then drops all
  // guest-side lines (each costs an SD fsync) except error markers.
  if (!s_runtime_quiet &&
      (uint32_t)(bmx_host_ticks_ms() - s_boot_complete_ms) >= 8000u) {
    s_runtime_quiet = true;
    bmx::dos::debug_set_runtime_quiet(true);
    bmx::dos::debug_log("GFX_Events: runtime log quiet");
  }
  int reset = bmx::dos::dos_take_reset_request();
  if (reset != 0) {
    bmx::dos::debug_log("GFX_Events: menu reboot request (%s), throwing",
                            reset == 3 ? "hard" : "soft");
    throw reset;
  }
  bmx::dos::pump_autostart_typer();
  bmx::dos::drain_guest_keys();
  ui_handle_toggle_or_quick_func();
  // Audio drain lives here, not in the outer frame loop: the first guest
  // slice (SHELL_Run) never returns, so the outer loop parks forever and
  // anything behind it (video_render, sound_play) runs exactly once. This
  // pump fires on every Normal_Loop idle slice, guest running or idling.
  // sound_play is wall-clock gated internally, so high-frequency calls are
  // cheap (one timer read + compare). The perf hooks time the drain and
  // emit the 5s snapshot from this same live context (an outer-loop
  // snapshot would run once and never again, same parking bug).
  bmx::dos::perf_audio_tick();
  bmx::dos::perf_snapshot_tick();
}
