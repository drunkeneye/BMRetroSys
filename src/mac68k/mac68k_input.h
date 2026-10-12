//
// mac68k_input.h
//
// Basilisk II input (ADB keyboard/mouse) for BMX bare-metal
// (Phase 0 placeholder)
//

#ifndef mac68k_input_h
#define mac68k_input_h

namespace bmx {
namespace mac68k {

// Phase 0 placeholder input state. The real per-frame keyboard/mouse state
// (USB kbd -> ADB codes, ADB mouse) lands in a later phase.
struct mac68k_input_t {
  int unused;
};

// Polls BMX queued input events and fills the placeholder input struct.
void poll_input(mac68k_input_t *input);

// Drains the BMX key queue into the guest ADB keyboard controller.
void drain_guest_keys(void);

// Forwards the USB mouse accumulator into the guest ADB mouse, gated on
// the BMX menu being closed. Same live pump as the key drain.
void drain_guest_mouse(void);

// Reboot request from the menu (core0). The frame loop takes it and
// reboots the guest. Codes mirror the classic convention: 6 = soft,
// 3 = hard.
void mac68k_request_reset(int code);
int mac68k_take_reset_request(void);

// Queue keystrokes for the guest (menu autostart). core0 returns
// immediately; the guest thread drips the keys in.
void mac68k_type_text(const char *text);

// F12 menu request flag, set by the key drain (F12 is filtered from the
// ADB stream). The cpu tick pump takes it and runs the common menu
// toggle handler. Returns true once per request.
bool mac68k_take_menu_request(void);

}  // namespace mac68k
}  // namespace bmx

#endif
