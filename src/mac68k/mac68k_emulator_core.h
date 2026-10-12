//
// mac68k_emulator_core.h
//
// Basilisk II (Macintosh 68k) emulator core integration for BMX bare-metal
// (Phase 0 placeholder: interpreter-only, core scope glue, no upstream
// Basilisk includes yet).
//

#ifndef mac68k_emulator_core_h
#define mac68k_emulator_core_h

#include <circle/memory.h>
#include <circle/multicore.h>
#include <circle/spinlock.h>
#include "emulatorcore.h"

extern "C" {
#include "third_party/common/circle.h"
}

#ifndef BMX_EMU_MULTICORE
#define BMX_EMU_MULTICORE 0
#endif

#if defined(ARM_ALLOW_MULTI_CORE) && BMX_EMU_MULTICORE
#define BMC64_USE_EMU_MULTICORE 1
#endif

namespace bmx {
namespace mac68k {

class Mac68kEmulatorCore
 : public ::EmulatorCore
#ifdef BMC64_USE_EMU_MULTICORE
   ,public CMultiCoreSupport
#endif
{
public:
  Mac68kEmulatorCore(CMemorySystem *pMemorySystem, int cyclesPerSecond);
  ~Mac68kEmulatorCore(void);

  void Run(unsigned nCore)
#ifdef BMC64_USE_EMU_MULTICORE
      override
#endif
  ;

#ifndef BMC64_USE_EMU_MULTICORE
  bool Initialize() { return true; }
#endif

  bool Init(ViceOptions* options) override;
  void LaunchEmulator(char *timing_option) override;

private:
  bool launch_;
  int cyclesPerSecond_;
  char timing_option_[8];
  CSpinLock m_Lock;
  ViceOptions *m_options;

  void WaitForLaunch();
  void RunMainMac68k(bool wait);
  void RunFrameLoop();
};

}  // namespace mac68k
}  // namespace bmx

#endif
