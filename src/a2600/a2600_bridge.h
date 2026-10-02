//
// a2600_bridge.h
//
// BMX emux bridge for the Atari 2600 kernel (Stella core). Cartridge
// slot only: no drives, tapes, REU or userport. The console Select/Reset
// switches get user-mappable keyboard shortcuts (Keyboard menu).
//

#ifndef a2600_bridge_h
#define a2600_bridge_h

namespace bmx {
namespace a2600 {

void sync_all_menu_items(void);
void sync_cart_item(void);
void sync_key_items(void);
void cancel_key_capture(void);

}  // namespace a2600
}  // namespace bmx

#endif
