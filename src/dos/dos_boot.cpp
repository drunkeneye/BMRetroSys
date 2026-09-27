//
// dos_boot.cpp
//
// Phase 1.2: DOSBox-X guest bring-up. Mirrors the upstream boot order from
// src/gui/sdlmain.cpp (restart path), restricted to vendored modules.
// Deleted-backend Inits (VOODOO/GLIDE/MPU401/SBLASTER/GUS/IDE/SERIAL/NE2K,
// FDC, RENDER/CAPTURE...) are skipped: their globals are inert stubs and
// Phase 2+ owns the real devices.
//
// Every step is logged to bmretrosys-dos.log; the first failing step tells us
// which module needs work next.
//

#include "dos_boot.h"
#include "dos_build_tag.h"
#include "dos_debug.h"
#include "dos_perf.h"
#include "dos_sound.h"

#include "control.h"
#include "programs.h"

#include <cstdio>
#include <exception>

// Vendored init entry points (all void(void) in the subset). Order mirrors
// upstream src/gui/sdlmain.cpp (restart path): Init_AddressLimitAndGateMask
// + Init_MemoryAccessArray + Init_A20_Gate + Init_PS2_Port_92h must precede
// Init_RAM (page-handler table + address mask), MEM_A20_Enable(true) after
// CALLBACK, Init_VGABIOS after ROMBIOS (needs MemBase), KEYBOARD before BIOS.
void DOSBOX_SetupConfigSections(void);
void DOSBOX_InitTickLoop(void);
void DOSBOX_RealInit(void);
void IO_Init(void);
void HARDWARE_Init(void);
void CPU_PreInit(void);
void Init_AddressLimitAndGateMask(void);
void Init_MemoryAccessArray(void);
void Init_A20_Gate(void);
void Init_PS2_Port_92h(void);
void Init_RAM(void);
void MEM_A20_Enable(bool enabled);
void Init_DMA(void);
void Init_PIC(void);
void TIMER_Init(void);
void PAGING_Init(void);
void CMOS_Init(void);
void ROMBIOS_Init(void);
void CALLBACK_Init(void);
void Init_VGABIOS(void);
void CPU_Init(void);
void FPU_Init(void);
void VGA_Init(void);
void KEYBOARD_Init(void);
void BIOS_Init(void);
void INT10_Init(void);
void DOS_Init(void);
void DRIVES_Init(void);
void MOUSE_Init(void);
void XMS_Init(void);
void EMS_Init(void);
void MIXER_Init(void);
void PCSPEAKER_Init(void);
void SBLASTER_Init(void);
void PROGRAMS_Init(void);
// VM event dispatcher (vendored setup.cpp). sdlmain calls VM_PowerOn()
// after all inits (POWERON, RESET, RESET_END); RESET runs CPU_OnReset which
// sets the F000:FFF0 reset vector state. Without this the CPU starts
// zeroed and executes garbage.
void DispatchVMEvent(enum vm_event event);
// DOS kernel + shell startup (deleted sdlmain.cpp VM_Boot_DOSBox_Kernel,
// called by the guest BIOS POST when it is time to boot DOS). All callees
// are vendored except the never-started sound/CD/layout helpers (no-op
// stubs in dos_deleted_owner_stubs.cpp). SHELL_Run changes CS:IP into
// the shell and returns; the frame loop keeps executing the guest.
void RemoveEMSPageFrame(void);
void RemoveUMBBlock(void);
void DisableINT33(void);
void DOS_GetMemory_unmap(void);
void VFILE_Shutdown(void);
void PROGRAMS_Shutdown(void);
void DOS_UninstallMisc(void);
void SBLASTER_DOS_Shutdown(void);
void GUS_DOS_Shutdown(void);
void EMS_DoShutDown(void);
void XMS_DoShutDown(void);
void DOS_DoShutDown(void);
void Init_MemHandles(void);
void DOS_Startup(Section *sec);
void DRIVES_Startup(Section *sec);
void DOS_InitClock(void);
void DOS_KeyboardLayout_Startup(Section *sec);
void XMS_Startup(Section *sec);
void EMS_Startup(Section *sec);
void SHELL_MessagesInit(void);
void CONFIGSHELL_Init(void);
void CONFIGSHELL_Run(void);
void SHELL_Init(void);
void AUTOEXEC_Startup(Section *sec);
void MSCDEX_Startup(Section *sec);
void MOUSE_Startup(Section *sec);
void SHELL_Run(void);
// dos_kernel_disabled lives in vendored dos.cpp (dos_inc.h); global scope
// here (NOT inside bmx::dos) so it binds to ::dos_kernel_disabled.
extern bool dos_kernel_disabled;
// enable_share_exe lives in vendored dos.cpp; forced false below (see
// boot_init) so it stays off even if a future upstream sync changes the
// config default back.
extern bool enable_share_exe;

namespace bmx {
namespace dos {

namespace {

void boot_fatal_hook(const char *msg) {
  debug_log("E_Exit: %s", msg != nullptr ? msg : "(null)");
}

void boot_terminate_hook(void) {
  // Unwrap the in-flight exception for its message (set_terminate cannot
  // take it directly, but current_exception can).
  try {
    std::exception_ptr eptr = std::current_exception();
    if (eptr) {
      try {
        std::rethrow_exception(eptr);
      } catch (const std::exception &ex) {
        debug_log("TERMINATE: %s", ex.what());
      } catch (...) {
        debug_log("TERMINATE: non-std exception");
      }
    } else {
      debug_log("TERMINATE: no active exception (abort?)");
    }
  } catch (...) {
    debug_log("TERMINATE: failed to unwrap");
  }
  // Spin (do not return: terminate must not return).
  while (true) {
  }
}

}  // namespace

bool boot_init(void) {
  // NOTE: the boot-code version is DOS_BUILD_TAG (rewritten by the
  // build script on every kernel build); __DATE__/__TIME__ would go stale
  // whenever this TU is not recompiled.
  debug_log("boot_init: enter (DOSBox-X guest bring-up, bootcode "
            DOS_BUILD_TAG ")");
  bmx_dosboxx_fatal_hook = boot_fatal_hook;
  std::set_terminate(boot_terminate_hook);

  // The upstream main() builds control from argv; we boot with an empty
  // command line so every section keeps compiled-in defaults (Phase 4
  // loads dosbox-x.conf overrides from SD).
  debug_log("boot_init: creating control (empty cmdline)...");
  static const char *argv[] = {"dosbox-x", nullptr};
  static CommandLine cmd(1, argv);
  control = new Config(&cmd);
  if (control == nullptr) {
    debug_log("boot_init FAILED: control alloc");
    return false;
  }
  debug_log("boot_init: control ready, SetupConfigSections...");
  DOSBOX_SetupConfigSections();
  debug_log("boot_init: sections ready");

  debug_log("boot_init: InitTickLoop + RealInit...");
  DOSBOX_InitTickLoop();
  DOSBOX_RealInit();
  debug_log("boot_init: RealInit done");
  // Fast-with-fallback performance defaults (simple core + fixed 15000):
  // applied here so CPU_PreInit constructs the CPU from them. Persisted
  // menu choices (if any) are already loaded into the perf store.
  bmx::dos::perf_apply_boot();
  // Share-mode file opens (open/share/fdopen) can never work here:
  // circle-newlib's fdopen() needs fcntl(F_GETFL) and Circle answers
  // ENOSYS, so every share-mode open of a mounted-drive file fails. A
  // single DOS session has no concurrent processes to share with, so force
  // plain fopen() unconditionally (also the vendored config default).
  ::enable_share_exe = false;

  debug_log("boot_init: machine (IO/HARDWARE/CPUpre/gatemask/accessarray/A20/92/RAM/DMA/PIC)...");
  IO_Init();
  HARDWARE_Init();
  CPU_PreInit();
  Init_AddressLimitAndGateMask();
  Init_MemoryAccessArray();
  Init_A20_Gate();
  Init_PS2_Port_92h();
  Init_RAM();
  Init_DMA();
  Init_PIC();
  debug_log("boot_init: machine done");

  debug_log("boot_init: timer/paging/cmos/rombios/callback/vgabios/A20...");
  TIMER_Init();
  PAGING_Init();
  CMOS_Init();
  ROMBIOS_Init();
  CALLBACK_Init();
  Init_VGABIOS();
  MEM_A20_Enable(true);
  debug_log("boot_init: platform done");

  debug_log("boot_init: cpu/fpu/vga/keyboard/bios/int10...");
  CPU_Init();
  FPU_Init();
  VGA_Init();
  KEYBOARD_Init();
  BIOS_Init();
  INT10_Init();
  debug_log("boot_init: cpu/bios done");

  debug_log("boot_init: dos/drives/mouse/xms/ems...");
  DOS_Init();
  DRIVES_Init();
  MOUSE_Init();
  XMS_Init();
  EMS_Init();
  debug_log("boot_init: dos done");

  debug_log("boot_init: mixer/sblaster/pcspeaker/programs...");
  // The BMX PCM sink runs at kDosboxxSoundRate; force the mixer to match so
  // the drain path never resamples (desktop default is 48000). Same
  // mechanism a dosbox-x.conf line uses; applied before MIXER_Init reads it.
  // oplrate joins it (OPL channel resamples to the mixer rate otherwise).
  {
    Section *mixsec = control->GetSection("mixer");
    if (mixsec != nullptr) {
      char rate_line[32];
      snprintf(rate_line, sizeof(rate_line), "rate=%d",
               bmx::dos::kDosboxxSoundRate);
      if (!mixsec->HandleInputline(rate_line)) {
        debug_log("boot_init: mixer rate override rejected");
      }
    } else {
      debug_log("boot_init: no mixer section (rate override skipped)");
    }
    // OPL at the sink rate: no mixer-side resample stage (Reset-gated for
    // later menu changes; here it lands before the SBLASTER ctor reads it).
    Section *sbsec = control->GetSection("sblaster");
    if (sbsec != nullptr) {
      char opl_line[32];
      snprintf(opl_line, sizeof(opl_line), "oplrate=%d",
               bmx::dos::kDosboxxSoundRate);
      if (!sbsec->HandleInputline(opl_line)) {
        debug_log("boot_init: oplrate override rejected");
      }
    }
  }
  MIXER_Init();
  SBLASTER_Init();
  PCSPEAKER_Init();
  PROGRAMS_Init();
  debug_log("boot_init: power-on (POWERON/RESET/RESET_END)... ");
  DispatchVMEvent(VM_EVENT_POWERON);
  DispatchVMEvent(VM_EVENT_RESET);
  DispatchVMEvent(VM_EVENT_RESET_END);
  debug_log("boot_init: ALL OK (guest BIOS+DOS initialized)");

  return true;
}

}  // namespace dos
}  // namespace bmx

// Called by the guest BIOS POST (cb_bios_post) when it is time to boot DOS.
// Global scope: bios.cpp forward-declares ::VM_Boot_DOSBox_Kernel.
// Mirrors deleted sdlmain.cpp VM_Boot_DOSBox_Kernel, minus HMENU menu ops
// (not compiled in) and the PC-98 function-key row (not PC-98). Runs on the
// emulation thread during a guest slice; SHELL_Run arms CS:IP for the shell
// and returns, the frame loop keeps the guest executing.
bool VM_Boot_DOSBox_Kernel(void) {
  bmx::dos::debug_log("VM_Boot_DOSBox_Kernel: enter");
  if (!dos_kernel_disabled) {
    RemoveEMSPageFrame();
    RemoveUMBBlock();
    DisableINT33();
    DOS_GetMemory_unmap();
    VFILE_Shutdown();
    PROGRAMS_Shutdown();
    DOS_UninstallMisc();
    SBLASTER_DOS_Shutdown();
    GUS_DOS_Shutdown();
    EMS_DoShutDown();
    XMS_DoShutDown();
    DOS_DoShutDown();
    DispatchVMEvent(VM_EVENT_DOS_SURPRISE_REBOOT);
    dos_kernel_disabled = true;
  }
  if (dos_kernel_disabled) {
    Init_MemHandles();
    DispatchVMEvent(VM_EVENT_DOS_BOOT);
    dos_kernel_disabled = false;
    DOS_Startup(nullptr);
    DRIVES_Startup(nullptr);
    DispatchVMEvent(VM_EVENT_DOS_INIT_KERNEL_READY);
    DOS_InitClock();
    DOS_KeyboardLayout_Startup(nullptr);
    XMS_Startup(nullptr);
    EMS_Startup(nullptr);
    SHELL_MessagesInit();
    CONFIGSHELL_Init();
    CONFIGSHELL_Run();
    DispatchVMEvent(VM_EVENT_DOS_INIT_CONFIG_SYS_DONE);
    SHELL_Init();
    DispatchVMEvent(VM_EVENT_DOS_INIT_SHELL_READY);
    AUTOEXEC_Startup(nullptr);
    MSCDEX_Startup(nullptr);
    MOUSE_Startup(nullptr);
    DispatchVMEvent(VM_EVENT_DOS_INIT_AUTOEXEC_BAT_DONE);
    DispatchVMEvent(VM_EVENT_DOS_INIT_AT_PROMPT);
    SHELL_Run();
  }
  bmx::dos::debug_log("VM_Boot_DOSBox_Kernel: done (shell armed)");
  return true;
}
