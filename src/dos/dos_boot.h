//
// dos_boot.h
//
// Phase 1.2: DOSBox-X guest bring-up (config + module init chain).
//

#ifndef dos_boot_h
#define dos_boot_h

namespace bmx {
namespace dos {

// Runs the vendored DOSBox-X init chain in upstream order
// (sdlmain.cpp restart path, minus deleted SDL/output backends):
// Config + SetupConfigSections, RealInit, machine, CPU, BIOS, DOS.
// Returns true when every step completes; each step is logged.
bool boot_init(void);

}  // namespace dos
}  // namespace bmx

#endif
