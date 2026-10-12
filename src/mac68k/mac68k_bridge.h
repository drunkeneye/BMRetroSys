//
// mac68k_bridge.h
//
// Declares the emulator-specific API bridge between BMX's common code
// (menu/UI/kernel) and the Basilisk II (Macintosh 68k) emulator core,
// mirroring the role that vice_api.c plays for the VICE machines.
//

#ifndef mac68k_bridge_h
#define mac68k_bridge_h

namespace bmx {
namespace mac68k {

// The EMUX bridge needs no explicit install: emu_machine_init() (called by
// the common menu/kernel startup) publishes the Mac68k machine class once
// BMC64_MACHINE_CLASS_MAC68K exists (added by another workstream).
// Kept as documentation anchor for the bridge module.

}  // namespace mac68k
}  // namespace bmx

#endif
