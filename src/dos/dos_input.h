//
// dos_input.h
//
// DOSBox-X input (keyboard/joystick/mouse) for BMX bare-metal
// (Phase 0 placeholder)
//

#ifndef dos_input_h
#define dos_input_h

namespace bmx {
namespace dos {

// Phase 0 placeholder input state. The real per-frame keyboard/mouse state
// (USB kbd -> PC AT codes, serial mouse) lands in Phase 3.
struct dos_input_t {
  int unused;
};

// Polls BMX queued input events and fills the placeholder input struct.
void poll_input(dos_input_t *input);

// Drains the BMX key queue (pending_emu_key) into the guest keyboard
// controller. Shared by poll_input (frame loop) and GFX_Events (guest
// idle slices, including nested SHELL_Run).
void drain_guest_keys(void);

// Reboot request from the menu (core0). Codes mirror the DOSBox-X reboot
// throw convention: 6 = soft, 3 = hard. The frame loop never polls this
// directly; GFX_Events (guest thread) takes it and throws, so the request
// unwinds through nested SHELL_Run like a guest reboot instead of
// stranding the frame loop inside a second VM_Boot.
void dos_request_reset(int code);
int dos_take_reset_request(void);

// Queue keystrokes for the guest (menu autostart). Text is expanded to HID
// press/release events into a staging buffer and dripped into
// pending_emu_key from GFX_Events, so arbitrarily long commands never
// overflow the 16-slot queue. core0 returns immediately; use from the DOS
// prompt (keystrokes typed into a running game go to the game).
void dos_type_text(const char *text);

}  // namespace dos
}  // namespace bmx

#endif
