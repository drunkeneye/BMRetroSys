//
// apple2_bridge.h
//
// Emulator-specific API bridge between BMX common code (menu/UI/kernel) and
// the KEGS core (vendored GSplus subset). Same role as vice_api.c for VICE
// machines.
//

#ifndef apple2_bridge_h
#define apple2_bridge_h

struct menu_item;

namespace bmx {
namespace apple2 {

// Menu item anchors (kept to re-sync rows after settings load).
extern struct menu_item *s_model_item;
extern struct menu_item *s_joystick_item;
extern struct menu_item *s_speed_item;
extern struct menu_item *s_monitor_item;
extern struct menu_item *s_volume_item;
extern struct menu_item *s_drive_status[4];

void sync_all_menu_items(void);
void sync_drive_items(void);

// Slot-6 autostart poll (attach + reset + PR#6 typer). Called once per
// frame by the core loop; no-op when idle.
void apple2_autostart_poll(void);

}  // namespace apple2
}  // namespace bmx

#endif
