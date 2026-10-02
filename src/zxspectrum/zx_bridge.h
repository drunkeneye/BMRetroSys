//
// zx_bridge.h
//
// Emulator-specific API bridge between BMX common code (menu/UI/kernel) and
// the ZEsarUX spectrum core. Same role as vice_api.c for VICE machines.
//

#ifndef zx_bridge_h
#define zx_bridge_h

struct menu_item;

namespace bmx {
namespace zxspectrum {

// Menu item anchors (kept to re-sync rows after settings load).
extern struct menu_item *s_model_item;
extern struct menu_item *s_joystick_item;
extern struct menu_item *s_volume_item;

void sync_all_menu_items(void);

}  // namespace zxspectrum
}  // namespace bmx

#endif
