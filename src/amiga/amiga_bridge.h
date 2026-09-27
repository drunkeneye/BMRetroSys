//
// amiga_bridge.h
//
// Declares the emulator-specific API bridge between BMX's common code
// (menu/UI/kernel) and the vAmiga (A500 OCS) emulator core.
//

#ifndef amiga_bridge_h
#define amiga_bridge_h

namespace bmx {
namespace amiga {

// The EMUX bridge needs no explicit install: emu_machine_init() publishes
// BMC64_MACHINE_CLASS_AMIGA. Drive status rows are refreshed via
// sync_drive_items() on every menu open.
void sync_drive_items(void);

}  // namespace amiga
}  // namespace bmx

#endif
