//
// mac128k_emulator_core.cpp
//
// Mini vMac (Macintosh 128K) emulator core integration for BMX bare-metal:
// frame loop with boot, blocking backend run (ProgramMain) and
// menu-driven reboot. The F12 menu and reset handling run inside the
// backend tick (WaitForNextTick); this loop boots, enters the guest,
// and reboots on backend return.
//

#include "mac128k_emulator_core.h"

#include <assert.h>
#include <circle/timer.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mac128k_boot.h"
#include "mac128k_bmx_backend.h"
#include "mac128k_bmx_glue.h"
#include "mac128k_config.h"
#include "mac128k_debug.h"
#include "mac128k_input.h"
#include "mac128k_menu.h"
#include "mac128k_sound.h"
#include "mac128k_video.h"

// UI/menu entry points (C linkage; declared manually like
// src/a2600/a2600_emulator_core.cpp does).
extern "C" {
void emu_machine_init(int raster_skip_enabled, int raster_skip2_enabled);
void ui_init_menu(void);
// Common menu toggle handler (defined in third_party/common/ui.c).
void ui_handle_toggle_or_quick_func(void);
}

extern "C" {
void circle_boot_complete(void);
void circle_kernel_core_init_complete(int core);
}

namespace bmx {
namespace mac128k {

Mac128kEmulatorCore::Mac128kEmulatorCore(CMemorySystem *pMemorySystem,
                                       int cyclesPerSecond) :
#ifdef BMC64_USE_EMU_MULTICORE
      CMultiCoreSupport(pMemorySystem),
#endif
      launch_(false), cyclesPerSecond_(cyclesPerSecond), m_options(nullptr) {
#ifndef BMC64_USE_EMU_MULTICORE
  (void)pMemorySystem;
#endif
  (void)cyclesPerSecond_;
  timing_option_[0] = '\0';
  // NOTE: keep this constructor free of ALL logging and file I/O.
  // Core0 constructs this object before ViceApp::Initialize runs
  // CGlueStdioInit, so printf has no backend yet, and FAT is not mounted
  // yet so fopen risks hanging the boot. Every working kernel (a2600,
  // dos, atarist, amiga) keeps its constructor empty for this reason.
}

Mac128kEmulatorCore::~Mac128kEmulatorCore(void) {}

bool Mac128kEmulatorCore::Init(ViceOptions *options) {
  m_options = options;
#ifdef BMC64_USE_EMU_MULTICORE
  // Releases the secondary cores (Circle spin-table/IPI). Without this,
  // core1 never enters Run(): black screen, no log, no blink. Every other
  // kernel (a2600/dos/atarist/amiga/msx) does exactly this here.
  bool ok = Initialize();
  debug_log("mac128k: Init on core0, multicore init returned ok=%d", (int)ok);
  return ok;
#else
  return true;
#endif
}

void Mac128kEmulatorCore::LaunchEmulator(char *timing_option) {
  snprintf(timing_option_, sizeof(timing_option_), "%s",
           timing_option != nullptr ? timing_option : "pal");
  debug_log("mac128k: LaunchEmulator on core0, timing='%s', releasing core1",
            timing_option_);
#ifdef BMC64_USE_EMU_MULTICORE
  // Multicore: core 1 runs the loop (released via the flag below). This
  // function MUST return so core 0 can finish boot (USB, scheduler).
  m_Lock.Acquire();
  launch_ = true;
  m_Lock.Release();
#else
  RunMainMac128k(false);
#endif
}

void Mac128kEmulatorCore::Run(unsigned nCore) {
  assert(nCore > 0);
  switch (nCore) {
  case 1:
    printf("multicore: core 1 role=mac128k\r\n");
    RunMainMac128k(true);
    break;
  case 2:
  case 3:
#ifdef BMC64_USE_EMU_MULTICORE
    circle_kernel_core_init_complete(nCore);
#endif
    break;
  }

#ifdef BMC64_USE_EMU_MULTICORE
  printf("Core %d idle\n", nCore);
#if AARCH == 64
  asm("dsb sy\n\t"
      "1: wfi\n\t"
      "b 1b\n\t");
#else
  asm("dsb\n\t"
      "1: wfi\n\t"
      "b 1b\n\t");
#endif
#endif
}

void Mac128kEmulatorCore::WaitForLaunch() {
  // Spinlock-only wait: no CTimer calls here. The Circle system timer is
  // owned by core0 (mTimer.Initialize) and may not exist yet when core1
  // first spins; CTimer::Get()->MsDelay() here used to null-deref before
  // any log/blink (black screen, no logs). Matches a2600/dos/atarist.
  bool waiting = true;
  while (waiting) {
    m_Lock.Acquire();
    if (launch_) {
      waiting = false;
    }
    m_Lock.Release();
  }
  debug_log("mac128k: core1 launch flag seen");
}

void Mac128kEmulatorCore::RunMainMac128k(bool wait) {
  if (wait) {
    debug_log("mac128k: core1 waiting for launch flag");
    WaitForLaunch();
  }
  // First file access: mounts are up (core0 released us post-Launch).
  // a2600/dos pattern: debug_init() first on core1.
  debug_init();
  debug_log("mac128k: core1 log open, calling emu_machine_init");
  emu_machine_init(0, 0);
  debug_log("mac128k: core1 emu_machine_init done, calling ui_init_menu");
  ui_init_menu();
  debug_log("mac128k: core1 machine class published, menu built");
  RunFrameLoop();
}

void Mac128kEmulatorCore::RunFrameLoop() {
  debug_log("mac128k: core1 RunFrameLoop entered");
  // Liveness beacon FIRST (before video): 1 blink group proves core1
  // reached mac128k code even if the screen stays black. Screen splash
  // follows; stage colors then track boot.
  debug_blink(1);
  debug_log("mac128k: core1 entry blink done, calling video_init");
  video_init();
  sound_init();
  menu_init();
  debug_init();
  Config cfg;
  cfg.tv_mode = timing_option_;
  config_init(cfg);

  // NOTE on the F12 menu: the backend tick (WaitForNextTick) drains input
  // and runs the menu pump while the guest runs; a staged hard reset
  // unwinds ProgramMain via ForceMacOff and this loop reboots below.
  debug_log("mac128k: frame loop starting, F12 menu live via backend tick");

  bool boot_complete_signalled = false;
  unsigned long idle_count = 0;
  while (launch_) {
    if (!boot_init()) {
      debug_log("mac128k: boot_init failed, idle pump (add ROM/disks, then "
                "Reset from menu)");
      // Red screen + LED code: visible without UART or log access.
      video_boot_stage(10);
      debug_blink(9);
      // Idle pump without a guest: keep the menu alive so the user can
      // stage a ROM (the F12 key is drained here and pumped below),
      // retry boot when the menu requests a reset.
      for (int i = 0; i < 50 && launch_; i++) {
        if (mac128k_take_reset_request() != 0) {
          debug_log("mac128k: reset requested, retrying boot");
          break;
        }
        mac128k_input_t input;
        poll_input(&input);
        drain_guest_keys();
        drain_guest_mouse();
        ui_handle_toggle_or_quick_func();
        if (mac128k_take_menu_request()) {
          ui_handle_toggle_or_quick_func();
        }
        video_render();
        CTimer::Get()->MsDelay(100);
      }
      idle_count++;
      if (idle_count % 6 == 0) {
        debug_log("mac128k: idle without guest, still waiting for boot");
      }
      continue;
    }
    if (!boot_complete_signalled) {
      circle_boot_complete();
      boot_complete_signalled = true;
      debug_log("mac128k: boot complete signalled");
    }

    // Enter the guest. ProgramMain blocks until the backend raises
    // ForceMacOff (menu hard reset / staged ROM+disk change).
    debug_log("mac128k: entering backend_run (blocking; F12 menu stays "
              "live via the backend tick)");
    video_boot_stage(9);
    bmx_mac128k_backend_run();
    int reboot_code = bmx_mac128k_backend_take_reboot();
    debug_log("mac128k: backend_run returned (reboot=%d)", reboot_code);
    bmx_mac128k_backend_shutdown();
    if (reboot_code == 0) {
      // No reboot was requested. Stay in the idle pump until the user
      // resets or switches machines.
      debug_log("mac128k: guest exited without reboot, entering idle pump");
      while (launch_) {
        if (mac128k_take_reset_request() != 0) {
          break;
        }
        mac128k_input_t input;
        poll_input(&input);
        drain_guest_keys();
        drain_guest_mouse();
        ui_handle_toggle_or_quick_func();
        if (mac128k_take_menu_request()) {
          ui_handle_toggle_or_quick_func();
        }
        video_render();
        CTimer::Get()->MsDelay(20);
      }
      continue;
    }
    debug_log("mac128k: reboot %d: re-running boot_init (staged ROM/disks "
              "apply)", reboot_code);
  }
  debug_log("mac128k: frame loop exiting");
}

}  // namespace mac128k
}  // namespace bmx
