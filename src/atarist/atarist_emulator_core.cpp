//
// atarist_emulator_core.cpp
//
// Hatari emulator core integration for BMX bare-metal.
//
// One BMX frame == one Hatari VBL: the CPU free-runs inside m68k_run() and
// returns at the end of every frame because our Timing_WaitOnVbl() sets
// SPCFLAG_BRK + quit_program (same mechanism as the libretro port).
// The loop then pumps F12, presents video, drains audio and paces to the
// ST's 50 Hz before re-entering the core.
//

#include "atarist_emulator_core.h"

#include <circle/timer.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
// NOTE: <string> first -- Hatari cpu/sysdeps.h includes <string> under
// __cplusplus, which is only legal outside an extern "C" region.
#include <string>

extern "C" {
#include "third_party/common/circle.h"
#include "third_party/common/ui.h"
#include "configuration.h"
#include "main.h"
#include "m68000.h"
#include "newcpu.h"
#include "hatari-glue.h"
#include "reset.h"
#include "video.h"
#include "sound.h"
#include "log.h"
#include "stMemory.h"
#include "conv_st.h"
#include "ioMem.h"
#include "memory.h"

// Defined in atarist_bridge.cpp.
void emu_machine_init(int raster_skip_enabled, int raster_skip2_enabled);
// Defined in atarist_bmx_platform.c.
void atarist_log_exc_stats(void);
// Mixer output high-water mark (atarist_bmx_audio.c, C linkage).
extern "C" int atarist_audio_level_max;
// Draw count from the screen backend (atarist_bmx_screen.c, C linkage).
unsigned long atarist_bmx_draws(void);
// 32-bit conversion buffer from the screen backend (C linkage).
extern "C" uint32_t *atarist_bmx_screen_pixels(int *w, int *h);
}

extern "C" {
extern void circle_kernel_core_init_complete(int core);
}

#include "atarist_video.h"
#include "atarist_sound.h"
#include "atarist_input.h"
#include "atarist_config.h"
#include "atarist_debug.h"

namespace bmx {
namespace atarist {

AtariSTEmulatorCore::AtariSTEmulatorCore(CMemorySystem *pMemorySystem,
                                       int cyclesPerSecond) :
#ifdef BMC64_USE_EMU_MULTICORE
       CMultiCoreSupport(pMemorySystem),
#endif
       launch_(false), cyclesPerSecond_(cyclesPerSecond) {
  (void)cyclesPerSecond_;
}

AtariSTEmulatorCore::~AtariSTEmulatorCore(void) {}

void AtariSTEmulatorCore::WaitForLaunch() {
  bool waiting = true;
  while (waiting) {
    m_Lock.Acquire();
    if (launch_) {
      waiting = false;
    }
    m_Lock.Release();
  }
}

void AtariSTEmulatorCore::RunMainAtariST(bool wait) {
  if (wait) {
    WaitForLaunch();
  }

  bmx::atarist::debug_init();
  bmx::atarist::debug_log("Starting Hatari main loop, tv_mode=%s",
                         timing_option_);

  // Publish our machine class (VICE does this in RunMainProgram, which has
  // no Hatari equivalent) and build the BMX menu tree.
  emu_machine_init(0, 0);
  ui_init_menu();
  bmx::atarist::debug_log("AtariST: machine class published, menu built");

  bmx::atarist::Config config;
  config.tv_mode = timing_option_;

  if (!bmx::atarist::config_init(config)) {
    bmx::atarist::debug_log("Hatari config init FAILED");
    bmx::atarist::debug_dump_core_log();
    return;
  }
  bmx::atarist::debug_log("Hatari config init OK");
  bmx::atarist::debug_dump_core_log();

  // Unpause (libretro pattern): enables audio + marks emulation active.
  Main_UnPauseEmulation();

  RunFrameLoop();
}

void AtariSTEmulatorCore::RunFrameLoop(void) {
  bmx::atarist::debug_log("RunFrameLoop: enter (m68k_run per VBL)");

  bmx::atarist::video_init();
  bmx::atarist::sound_init();
  bmx::atarist::debug_dump_core_log();

  // ST PAL: 50 Hz -> 20000 us per frame. The core generates samples for the
  // sink every VBL; pacing here keeps video + audio in lockstep.
  const unsigned long frame_ticks = 20000;
  unsigned long next_frame_us = CTimer::GetClockTicks();
  unsigned long frame_count = 0;
  bool first_frame = true;
  bool first_run = true;

  while (true) {
    // A reset (menu reboot, autostart boot) sets quit_program to
    // UAE_RESET/UAE_RESET_HARD plus SPCFLAG_MODE_CHANGE. Only m68k_go()
    // performs the full restart sequence (prefs/table rebuild,
    // m68k_reset2 vector fetch, memory clear) -- a bare m68k_run() would
    // execute garbage at a stale PC (the broken post-reset state seen
    // with autostarted disks). Route resets through m68k_go exactly like
    // SDL builds; normal frames use the light m68k_run() path.
    // NOTE: do NOT clear MODE_CHANGE or quit_program here -- m68k_go owns
    // both. (An earlier revision cleared MODE_CHANGE manually, which
    // skipped the vector fetch and wedged every reset.)
    if (first_run) {
      first_run = false;
      UAE_Set_Quit_Reset(false);
      m68k_go(true);
    } else if (quit_program == UAE_RESET_HARD || quit_program == UAE_RESET ||
               (regs.spcflags & SPCFLAG_MODE_CHANGE)) {
      bmx::atarist::debug_log("frame loop: reset requested (%d), via m68k_go",
                             quit_program);
      m68k_go(true);
    } else {
      M68000_UnsetSpecial(SPCFLAG_BRK);
      quit_program = 0;
      m68k_run();
    }
    frame_count++;

    if (frame_count <= 3) {
      bmx::atarist::debug_log("RunFrameLoop: frame %lu done (VBLs=%lu)",
                             frame_count, (unsigned long)nVBLs);
    }

    bmx::atarist::poll_input(NULL);

    // Same slot as VICE's videoarch pump: consumes ui_toggle_pending (F12)
    // and hotkey quick functions. Without this the menu never opens.
    ui_handle_toggle_or_quick_func();

    bmx::atarist::video_render();
    bmx::atarist::sound_play();

    if (first_frame) {
      first_frame = false;
      circle_boot_complete();
      bmx::atarist::debug_log("AtariST: boot complete signalled");
    }
    if ((frame_count % 300) == 0) {
      unsigned long presents = 0, ensure_us = 0, present_us = 0;
      bmx::atarist::video_present_stats(&presents, &ensure_us, &present_us);
      // PC tells whether the 68k executes TOS (0xE00000+) or is stuck.
      // stopped/intmask/sr/pending diagnose a STOP-state wait (VBLs advance
      // on the video side regardless of CPU progress). nGeneratedSamples
      // shows the mixer output level.
      bmx::atarist::debug_log("Hatari heartbeat frames=%lu vbls=%lu "
                             "presents=%lu pc=0x%06x stopped=%d intmask=%d "
                             "sr=0x%04x pending=0x%x audgen=%d draws=%lu "
                             "skip=%d",
                             frame_count, (unsigned long)nVBLs, presents,
                             (unsigned)m68k_getpc(), (int)regs.stopped,
                             (int)regs.intmask, (unsigned)regs.sr,
                             (unsigned)pendingInterrupts, nGeneratedSamples,
                             atarist_bmx_draws(), nFrameSkips);
      // Exact exception census for the window (vector:count).
      atarist_log_exc_stats();
      bmx::atarist::debug_log("Hatari audio: genmax=%d/frame",
                             atarist_audio_level_max);
      atarist_audio_level_max = 0;
      // Video state: base address, mode, Shifter colors vs conversion
      // palette, video-RAM checksum, plus a checksum of the 32-bit
      // conversion output (pxsum!=0 proves the converters emit pixels).
      {
        unsigned long vramsum = 0;
        if (STRam != NULL && VideoBase < 0x100000) {
          for (unsigned i = 0; i < 32768; i += 257) {
            vramsum += STRam[VideoBase + i];
          }
        }
        unsigned long pxsum = 0;
        {
          int pw = 0, ph = 0;
          uint32_t *px = atarist_bmx_screen_pixels(&pw, &ph);
          if (px != NULL && pw > 0 && ph > 0) {
            size_t n = (size_t)pw * (size_t)ph;
            for (size_t i = 0; i < n; i += 4097) {
              pxsum += px[i];
            }
          }
        }
        bmx::atarist::debug_log("Hatari video vbase=0x%06x res=%d hires=%d "
                               "pal=%08x,%08x,%08x vramsum=%lu "
                               "pxsum=%lu",
                               (unsigned)VideoBase, STRes,
                               bUseHighRes ? 1 : 0,
                               (unsigned)STRGBPalette[0],
                               (unsigned)STRGBPalette[1],
                               (unsigned)STRGBPalette[15], vramsum, pxsum);
      }
      bmx::atarist::debug_dump_core_log();
    }

    // Pace to 50 Hz wall-clock (the core has no vsync wait of its own;
    // retro relies on the frontend, we rely on this).
    unsigned long now_us = CTimer::GetClockTicks();
    next_frame_us += frame_ticks;
    if ((long)(now_us - next_frame_us) > (long)(frame_ticks * 5)) {
      next_frame_us = now_us + frame_ticks;
    } else if ((long)(next_frame_us - now_us) > 0) {
      unsigned long wait_us = next_frame_us - now_us;
      CTimer::SimpleMsDelay(wait_us / 1000);
    }
  }
}

void AtariSTEmulatorCore::Run(unsigned nCore) {
  assert(nCore > 0);
  switch (nCore) {
  case 1:
    printf("multicore: core 1 role=atarist\r\n");
    RunMainAtariST(true);
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

bool AtariSTEmulatorCore::Init(ViceOptions* options) {
  m_options = options;
  return Initialize();
}

void AtariSTEmulatorCore::LaunchEmulator(char *timing_option) {
  snprintf(timing_option_, sizeof timing_option_, "%s", timing_option);
#ifdef BMC64_USE_EMU_MULTICORE
  m_Lock.Acquire();
  launch_ = true;
  m_Lock.Release();
#else
  RunMainAtariST(false);
#endif
}

}  // namespace atarist
}  // namespace bmx
