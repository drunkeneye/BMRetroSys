//
// cpc_bridge.h
//
// Emulator-specific API bridge between BMX common code (menu/UI/kernel) and
// the CPC core (vendored Z80 + BMX hardware module). Same role as vice_api.c
// for VICE machines.
//

#ifndef cpc_bridge_h
#define cpc_bridge_h

struct menu_item;

namespace bmx {
namespace cpc {

// Menu item anchors (kept to re-sync rows after settings load).
extern struct menu_item *s_model_item;
extern struct menu_item *s_joystick_item;
extern struct menu_item *s_volume_item;
extern struct menu_item *s_drive_status[2];

void sync_all_menu_items(void);
void sync_drive_items(void);

// Autostart warp flight flag (frame loop polls + clears it).
extern bool s_autostart_warp_on;
bool autostart_warp_active(void);
void autostart_warp_clear(void);

}  // namespace cpc
}  // namespace bmx

#endif
