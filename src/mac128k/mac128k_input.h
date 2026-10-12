//
// mac128k_input.h
//
// Mac 128K input (classic keyboard/mouse) for BMX bare-metal.
//

#ifndef mac128k_input_h
#define mac128k_input_h

namespace bmx {
namespace mac128k {

struct mac128k_input_t {
  int unused;
};

void poll_input(mac128k_input_t *input);
void drain_guest_keys(void);
void drain_guest_mouse(void);

// Reboot request from the menu. Codes mirror the classic convention:
// 6 = soft (Mini vMac reset, disks ejected), 3 = hard (full reboot).
void mac128k_request_reset(int code);
int mac128k_take_reset_request(void);

void mac128k_type_text(const char *text);
bool mac128k_take_menu_request(void);

}  // namespace mac128k
}  // namespace bmx

#endif
