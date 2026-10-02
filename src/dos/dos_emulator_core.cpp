//
// dos_emulator_core.cpp
//
// DOSBox-X emulator core integration for BMX bare-metal (Phase 0 placeholder:
// no emulation yet, frame loop only pumps input/menu/video)
//

#include "dos_emulator_core.h"

#include <circle/timer.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <exception>

extern "C" {
#include "third_party/common/circle.h"
#include "third_party/common/ui.h"
// zip_wipe_machine_caches lives in menu.c; menu.h itself cannot be
// included here (it pulls emux_api.h, whose RS232 enums clash with
// viceoptions.h in C++, same reason as above).
void zip_wipe_machine_caches(void);

// Defined in dos_bridge.cpp (emux_api.h cannot be included here: it
// clashes with viceoptions.h's RS232 enums in C++).
void emu_machine_init(int raster_skip_enabled, int raster_skip2_enabled);
}

extern "C" {
extern void circle_kernel_core_init_complete(int core);
extern uint32_t bmx_host_ticks_ms(void);
}

#include "dos_video.h"
#include "dos_sound.h"
#include "dos_input.h"
#include "dos_config.h"
#include "dos_debug.h"
#include "dos_boot.h"
#include "dos_perf.h"

// Guest slice driver: DOS_RunMachine never returns, so the frame loop
// calls the loop handler (Normal_Loop) once per frame instead. Needs the
// real Bitu (64-bit on AArch64) via vendored dosbox.h (Makefile rule adds
// DOS_INCLUDE_DIRS for this TU).
#include "dosbox.h"
LoopHandler *DOSBOX_GetLoop(void);
// setup.h for enum vm_event (reboot recovery dispatch below).
#include "setup.h"
// bmx_sdl_stubs.h for le16toh/htole16 (regs.h->mem.h needs them; this TU
// has no FORCE_INCLUDE, so include normally AFTER the C++ headers above).
#include "bmx_sdl_stubs.h"
// regs.h for the throw-site diagnostics below.
#include "regs.h"
#include "pic.h"
#include "mem.h"

namespace bmx {
namespace dos {

namespace {

// Runs one guest slice; returns the handler result (0 = tick budget spent,
// nonzero = callback/shutdown request). Guest reboot/shutdown arrives as
// throw(int) (2=boot, 3=reboot, 6=reboot dos); anything escaping here would
// terminate the core, so convert to a log + cold continue. A reboot request
// re-dispatches RESET/RESET_END (DispatchVMEvent is global via setup.h;
// sdlmain reboot path, minus menu ops) so a legit guest reboot recovers
// instead of running on post-fault state.
Bitu run_guest_slice(void) {
  try {
    return DOSBOX_GetLoop()();
  } catch (int code) {
    unsigned ip = 0;
    double pic = 0;
    ip = (unsigned)reg_eip;
    pic = PIC_FullIndex();
    debug_log("run_guest_slice: guest threw int=%d at %04x:%08x pic=%.1f",
              code, (unsigned)SegValue(cs), ip, pic);
    if (code == 3 || code == 6) {
      // Staged cputype switch (CPU preset menu) lands here: applied
      // outside any CPU batch, reboot throw (if any) swallowed inside,
      // before the reset events reconstruct the machine around it.
      bmx::dos::perf_apply_pending_reset();
      // Every reboot (menu soft/hard, guest triple-fault) invalidates the
      // previous session: mounts die with it, so drop the extracted-zip
      // caches now. The next autostart re-extracts fresh instead of
      // tripping over stale files.
      zip_wipe_machine_caches();
      DispatchVMEvent(VM_EVENT_RESET);
      DispatchVMEvent(VM_EVENT_RESET_END);
      debug_log("run_guest_slice: reset re-dispatched");
    }
  } catch (const std::exception &ex) {
    debug_log("run_guest_slice: std::exception %s", ex.what());
  } catch (...) {
    debug_log("run_guest_slice: unknown exception");
  }
  return 0;
}

}  // namespace

DosboxxEmulatorCore::DosboxxEmulatorCore(CMemorySystem *pMemorySystem,
                                         int cyclesPerSecond) :
#ifdef BMC64_USE_EMU_MULTICORE
       CMultiCoreSupport(pMemorySystem),
#endif
       launch_(false), cyclesPerSecond_(cyclesPerSecond) {
  // Very first dos instruction: unmistakable 3x 1s beacon. If this is
  // not seen, dos code never runs (wrong kernel / earlier crash).
  // Blink only after that: FS is not mounted this early, and even probing
  // fopen here risks hanging the boot, so no debug_log until mounts are up.
}

DosboxxEmulatorCore::~DosboxxEmulatorCore(void) {}

void DosboxxEmulatorCore::WaitForLaunch() {
  bmx::dos::debug_log("WaitForLaunch: core1 waiting for launch flag...");
  bool waiting = true;
  while (waiting) {
    m_Lock.Acquire();
    if (launch_) {
      waiting = false;
    }
    m_Lock.Release();
  }
  bmx::dos::debug_log("WaitForLaunch: released");
}

void DosboxxEmulatorCore::RunMainDosboxx(bool wait) {
  bmx::dos::debug_log("RunMainDosboxx: enter (wait=%d)", (int)wait);
  if (wait) {
    WaitForLaunch();
  }

  bmx::dos::debug_log("RunMainDosboxx: calling debug_init...");
  bmx::dos::debug_init();
  bmx::dos::debug_log("RunMainDosboxx: debug ready, tv_mode=%s",
                           timing_option_);

  // Publish our machine class (BMC64_MACHINE_CLASS_DOS) and build the
  // BMX menu tree.
  bmx::dos::debug_log("RunMainDosboxx: calling emu_machine_init...");
  emu_machine_init(0, 0);
  bmx::dos::debug_log("RunMainDosboxx: emu_machine_init done, "
                           "calling ui_init_menu...");
  ui_init_menu();
  bmx::dos::debug_log("RunMainDosboxx: machine class published, "
                           "menu built");

  bmx::dos::Config config;
  config.tv_mode = timing_option_;

  if (!bmx::dos::config_init(config)) {
    bmx::dos::debug_log("Dosboxx config init FAILED");
    bmx::dos::debug_dump_core_log();
    return;
  }
  bmx::dos::debug_log("Dosboxx config init OK");
  bmx::dos::debug_dump_core_log();

  RunFrameLoop();
}

void DosboxxEmulatorCore::RunFrameLoop(void) {
  dos_input_t input;
  input.unused = 0;

  bmx::dos::debug_log("RunFrameLoop: enter (placeholder: cut-down "
                           "DOSBox-X lands in Phase 1)");

  bmx::dos::debug_log("RunFrameLoop: calling video_init...");
  bmx::dos::video_init();
  bmx::dos::debug_log("RunFrameLoop: video_init done, calling "
                           "sound_init...");
  bmx::dos::sound_init();
  bmx::dos::debug_log("RunFrameLoop: sound_init done, calling "
                           "boot_init (DOSBox guest)...");
  if (!bmx::dos::boot_init()) {
    bmx::dos::debug_log("RunFrameLoop: boot_init FAILED, looping "
                             "on blue screen");
  } else {
    bmx::dos::debug_log("RunFrameLoop: boot_init OK");
  }
  bmx::dos::debug_dump_core_log();

  bool first_frame = true;
  unsigned long frames = 0;
  while (true) {
    frames++;
    if (frames == 1) {
      bmx::dos::debug_log("RunFrameLoop: first poll_input...");
    }
    bmx::dos::poll_input(&input);
    if (frames == 1) {
      bmx::dos::debug_log("RunFrameLoop: first poll done, running slice");
    }

    // Run the guest for one tick budget (Normal_Loop consumes ticksRemain,
    // which the wall-clock now feeds; returns 0 when the budget is spent).
    // First 60 slices: log the entry PC to capture the initial path
    // (F000:FFF0 reset vector vs zeroed-state sled).
    if (frames <= 60 && DOS_VGA_TRACE) {
      bmx::dos::debug_log("RunFrameLoop: slice %lu enter %04x:%08x",
                              frames, (unsigned)SegValue(cs),
                              (unsigned)reg_eip);
    }
    // Segment transition trap: the interesting event is a far jump between
    // segments (e.g. F000:xxxx -> 0000:xxxx when POST goes lost). Dump 32
    // bytes at the source (the jumping instruction) and 16 at the target.
    {
      static unsigned s_last_seg = 0;
      static unsigned s_last_eip = 0;
      static bool s_have_last = false;
      unsigned seg = (unsigned)SegValue(cs);
      if (s_have_last && seg != s_last_seg && DOS_VGA_TRACE) {
        uint32_t last_lin = (s_last_seg << 4) + (unsigned)s_last_eip;
        uint32_t lin = (seg << 4) + (unsigned)reg_eip;
        char src[100], dst[52];
        for (int b = 0; b < 32; b++) {
          snprintf(src + b * 3, 4, "%02x ",
                   mem_readb((PhysPt)(last_lin + (uint32_t)b)));
        }
        src[96] = '\0';
        for (int b = 0; b < 16; b++) {
          snprintf(dst + b * 3, 4, "%02x ",
                   mem_readb((PhysPt)(lin + (uint32_t)b)));
        }
        dst[48] = '\0';
        bmx::dos::debug_log(
            "RunFrameLoop: SEG JUMP %04x:%08x -> %04x:%08x", s_last_seg,
            (unsigned)s_last_eip, seg, (unsigned)reg_eip);
        bmx::dos::debug_log("RunFrameLoop: src @%05x: %s", last_lin, src);
        bmx::dos::debug_log("RunFrameLoop: dst @%05x: %s", lin, dst);
      }
      s_last_seg = seg;
      s_last_eip = reg_eip;
      s_have_last = true;
    }
    Bitu slice_ret = run_guest_slice();
    if (slice_ret != 0 && DOS_VGA_TRACE) {
      bmx::dos::debug_log("RunFrameLoop: slice ret=%lu (frame %lu)",
                              (unsigned long)slice_ret, frames);
    }
    // Throttled guest-PC sampler (every 40 frames ~= 2s): shows whether the
    // guest progresses (BIOS POST), spins, or halts. Cheap: two globals.
    // Includes 16 bytes at the (real-mode) linear address so the executed
    // code can be identified off-device.
    if ((frames % 40) == 0 && DOS_VGA_TRACE) {
      unsigned seg = (unsigned)SegValue(cs);
      unsigned ip = (unsigned)reg_eip;
      uint32_t lin = (seg << 4) + ip;
      char hex[64];
      for (int b = 0; b < 16; b++) {
        uint8_t byte = mem_readb((PhysPt)(lin + (uint32_t)b));
        snprintf(hex + b * 3, 4, "%02x ", byte);
      }
      hex[48] = '\0';
      bmx::dos::debug_log("RunFrameLoop: guest pc=%04x:%08x pic=%.1f",
                              seg, ip, PIC_FullIndex());
      bmx::dos::debug_log("RunFrameLoop: guest @%05x: %s", lin, hex);
    }
    if (frames == 1) {
      bmx::dos::debug_log("RunFrameLoop: first slice done, pumping menu");
    }

    // Same slot as VICE's videoarch pump: consumes ui_toggle_pending (F12)
    // and hotkey quick functions. Without this the menu never opens.
    ui_handle_toggle_or_quick_func();

    if (frames == 1) {
      bmx::dos::debug_log("RunFrameLoop: first video_render...");
    }
    bmx::dos::video_render();

    // Drain mixed guest audio (SB/OPL/PC speaker) to the hardware sink.
    // Runs even when the guest idles: silence mixes to DAC hold, so this
    // never injects noise, and it keeps the sink clocked every frame.
    // (Post-boot this line runs once: the loop parks in slice 1 and the
    // live drain moves to GFX_Events via perf_audio_tick.)
    bmx::dos::sound_play();

    if (first_frame) {
      first_frame = false;
      bmx::dos::debug_log("RunFrameLoop: signalling boot complete...");
      circle_boot_complete();
      bmx::dos::debug_log("Dosboxx: boot complete signalled");
    }
    if ((frames % 200) == 0 && DOS_VGA_TRACE) {
      unsigned long presents = 0, ensure_us = 0, present_us = 0;
      bmx::dos::video_present_stats(&presents, &ensure_us, &present_us);
      bmx::dos::debug_log("RunFrameLoop: heartbeat frames=%lu "
                               "presents=%lu", frames, presents);
    }

    // Adaptive slice pacing (fast-with-fallback): the old fixed 50ms sleep
    // capped the guest at ~20 ticks/50ms ~= 40% real speed (increaseticks
    // discards everything past the clamp). Target ~120 slices/sec: sleep
    // only the leftover of an 8ms period; a struggling host (slice > 8ms)
    // doesn't sleep at all. Single throttle — the vendored increaseticks
    // Delay(1) only fires when truly idle.
    {
      static uint32_t s_slice_start = 0;
      uint32_t now = CTimer::GetClockTicks();
      if (s_slice_start != 0) {
        uint32_t dt = now - s_slice_start;
        if (dt < 8000u) {
          uint32_t wait_ms = (8000u - dt + 500u) / 1000u;
          if (wait_ms != 0) {
            CTimer::SimpleMsDelay(wait_ms);
          }
        }
      }
      s_slice_start = CTimer::GetClockTicks();
    }
  }
}

void DosboxxEmulatorCore::Run(unsigned nCore) {
  assert(nCore > 0);
  bmx::dos::debug_log("Run: core %u entry", nCore);
  switch (nCore) {
  case 1:
    bmx::dos::debug_log("Run: core1 -> RunMainDosboxx(wait)");
    RunMainDosboxx(true);
    break;
  case 2:
  case 3:
#ifdef BMC64_USE_EMU_MULTICORE
    circle_kernel_core_init_complete(nCore);
#endif
    break;
  }

#ifdef BMC64_USE_EMU_MULTICORE
  bmx::dos::debug_log("Run: core %u idle", nCore);
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

bool DosboxxEmulatorCore::Init(ViceOptions* options) {
  m_options = options;
  bool ok = Initialize();
  bmx::dos::debug_log("DosboxxEmulatorCore::Init: multicore init "
                           "returned ok=%d", (int)ok);
  return ok;
}

void DosboxxEmulatorCore::LaunchEmulator(char *timing_option) {
  snprintf(timing_option_, sizeof timing_option_, "%s", timing_option);
  bmx::dos::debug_log("LaunchEmulator: tv_mode=%s (core0 side)",
                           timing_option_);
#ifdef BMC64_USE_EMU_MULTICORE
  m_Lock.Acquire();
  launch_ = true;
  m_Lock.Release();
  bmx::dos::debug_log("LaunchEmulator: launch flag set");
#else
  RunMainDosboxx(false);
#endif
}

}  // namespace dos
}  // namespace bmx
