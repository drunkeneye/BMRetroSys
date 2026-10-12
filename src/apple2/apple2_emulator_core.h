//
// apple2_emulator_core.h
//
// Apple II emulator core integration for BMX bare-metal.
//

#ifndef apple2_emulator_core_h
#define apple2_emulator_core_h

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
namespace apple2 {

class Apple2EmulatorCore
 : public ::EmulatorCore
#ifdef BMC64_USE_EMU_MULTICORE
   ,public ::CMultiCoreSupport
#endif
{
public:
  Apple2EmulatorCore(CMemorySystem *pMemorySystem, int cyclesPerSecond);
  ~Apple2EmulatorCore(void);

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
  char timing_option_[64];
  CSpinLock m_Lock;
  ViceOptions *m_options;

  void WaitForLaunch();
  void RunMainAPPLE2(bool wait);
  void RunFrameLoop();
};

}  // namespace apple2
}  // namespace bmx

#endif
