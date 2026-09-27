//
// atarist_bridge.h
//
// Declares the emulator-specific API bridge between BMX's common code
// (menu/UI/kernel) and the AtariST (Atari ST/STE/MegaSTE) emulator core,
// mirroring the role that vice_api.c plays for the VICE machines.
//

#ifndef atarist_bridge_h
#define atarist_bridge_h

namespace bmx {
namespace atarist {

// The EMUX bridge needs no explicit install: emu_machine_init() (called by
// the common menu/kernel startup) publishes BMC64_MACHINE_CLASS_ATARIST.
// Kept as documentation anchor for the bridge module.

}  // namespace atarist
}  // namespace bmx

#endif
