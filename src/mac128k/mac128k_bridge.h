//
// mac128k_bridge.h
//
// Declares the emulator-specific API bridge between BMX's common code
// (menu/UI/kernel) and the Mini vMac (Macintosh 128K) emulator core.
//

#ifndef mac128k_bridge_h
#define mac128k_bridge_h

namespace bmx {
namespace mac128k {

// The EMUX bridge needs no explicit install: emu_machine_init() (called by
// the common menu/kernel startup) publishes the Mac128k machine class.
//

}  // namespace mac128k
}  // namespace bmx

#endif
