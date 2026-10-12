//
// bbc_bridge.h
//
// Bridge contract shared with bbc_emulator_core.cpp (same role as
// apple2_bridge.h re vice_api.c).
//

#ifndef BMX_BBC_BRIDGE_H
#define BMX_BBC_BRIDGE_H

namespace bmx {
namespace bbc {

void bbc_autostart_poll(void);
void sync_all_menu_items(void);
void sync_drive_items(void);

}  // namespace bbc
}  // namespace bmx

#endif  // BMX_BBC_BRIDGE_H
