//
// msx_bridge.h
//
// Emulator-specific API bridge between BMX common code (menu/UI/kernel) and
// the MSX core (vendored Z80 + machines/msx.c + BMX hardware module). Same
// role as vice_api.c for VICE machines.
//

#ifndef msx_bridge_h
#define msx_bridge_h

struct menu_item;

namespace bmx {
namespace msx {

// Menu item anchors (kept to re-sync rows after settings load).
extern struct menu_item *s_model_item;
extern struct menu_item *s_volume_item;
extern struct menu_item *s_cart_status;

void sync_all_menu_items(void);
void sync_cart_item(void);

// Staged warp settings (menu toggles).
extern int g_warp;
extern int g_autostart_warp;

}  // namespace msx
}  // namespace bmx

#endif
