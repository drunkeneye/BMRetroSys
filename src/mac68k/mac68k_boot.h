//
// mac68k_boot.h
//
// Mac68k guest bring-up (config + module init chain, Phase 0 placeholder).
//

#ifndef mac68k_boot_h
#define mac68k_boot_h

namespace bmx {
namespace mac68k {

// Runs the Mac68k init chain (config, video, audio, ADB, timer; ether,
// serial, scsi, cdrom, extfs stay stubs). Returns true when every step
// completes; each step is logged. Phase 0: no core yet, always true.
bool boot_init(void);

}  // namespace mac68k
}  // namespace bmx

#endif
