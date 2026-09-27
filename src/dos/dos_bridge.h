//
// dos_bridge.h
//
// Declares the emulator-specific API bridge between BMX's common code
// (menu/UI/kernel) and the DOSBox-X emulator core, mirroring the role that
// vice_api.c plays for the VICE machines.
//

#ifndef dos_bridge_h
#define dos_bridge_h

namespace bmx {
namespace dos {

// The EMUX bridge needs no explicit install: emu_machine_init() (called by
// the common menu/kernel startup) publishes the DOS machine class.
// Kept as documentation anchor for the bridge module.

}  // namespace dos
}  // namespace bmx

#endif
