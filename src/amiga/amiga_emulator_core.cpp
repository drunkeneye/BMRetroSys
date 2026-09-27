//
// amiga_emulator_core.cpp
//
// Amiga emulator core integration for BMX bare-metal.
//
// One BMX frame == one upstream Amiga::computeFrame() (PAL field). The
// upstream emulator thread is never launched; the single Emulator instance
// owned by amiga_config is driven synchronously on core 1.
//

#include "amiga_emulator_core.h"

#include <circle/timer.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include "third_party/common/circle.h"
#include "third_party/common/ui.h"
}

extern "C" {
extern void circle_kernel_core_init_complete(int core);
}

// Defined in amiga_bridge.cpp.
void emu_machine_init(int raster_skip_enabled, int raster_skip2_enabled);

#include "amiga_video.h"
#include "amiga_sound.h"
#include "amiga_input.h"
#include "amiga_config.h"
#include "amiga_debug.h"

// Frame stepping goes through the config module (vamiga-only TU), so
// this TU stays circle-only and never mixes the two type worlds.

namespace bmx {
namespace amiga {

AmigaEmulatorCore::AmigaEmulatorCore(CMemorySystem *pMemorySystem,
                                    int cyclesPerSecond) :
#ifdef BMC64_USE_EMU_MULTICORE
       CMultiCoreSupport(pMemorySystem),
#endif
       launch_(false), cyclesPerSecond_(cyclesPerSecond) {
  (void)cyclesPerSecond_;
}

AmigaEmulatorCore::~AmigaEmulatorCore(void) {}

void AmigaEmulatorCore::WaitForLaunch() {
  bool waiting = true;
  while (waiting) {
    m_Lock.Acquire();
    if (launch_) {
      waiting = false;
    }
    m_Lock.Release();
  }
}

void AmigaEmulatorCore::RunMainAmiga(bool wait) {
  if (wait) {
    WaitForLaunch();
  }

  bmx::amiga::debug_init();
  bmx::amiga::debug_log("Starting Amiga main loop, tv_mode=%s",
                        timing_option_);

  emu_machine_init(0, 0);
  ui_init_menu();
  bmx::amiga::debug_log("Amiga: machine class published, menu built");

  bmx::amiga::Config config;
  config.tv_mode = timing_option_;

  if (!bmx::amiga::config_init(config)) {
    bmx::amiga::debug_log("Amiga config init FAILED");
    bmx::amiga::debug_dump_core_log();
    return;
  }
  bmx::amiga::debug_log("Amiga config init OK");
  bmx::amiga::debug_dump_core_log();

  RunFrameLoop();
}

void AmigaEmulatorCore::RunFrameLoop(void) {
  bmx::amiga::debug_log("RunFrameLoop: enter (computeFrame per field)");

  bmx::amiga::video_init();
  bmx::amiga::sound_init();
  bmx::amiga::debug_dump_core_log();

  // A500 PAL: 50 Hz -> 20000 us per frame.
  const unsigned long frame_ticks = 20000;
  unsigned long next_frame_us = CTimer::GetClockTicks();
  bool first_frame = true;

  while (true) {
    // Warp (menu Warp Mode toggle): run several frames per tick with no
    // pacing and present only the last one. Intermediate frames skip the
    // blit/present; their audio stays in the upstream stream (overflow
    // path realigns it) so only the last frame's 882 samples are fed.
    const int warp = bmx::amiga::config_warp();
    const int subframes = warp ? 4 : 1;
    bool stepped = true;
    for (int s = 0; s < subframes; s++) {
      if (!bmx::amiga::config_step_frame()) {
        stepped = false;
        break;
      }
    }
    if (!stepped) {
      break;
    }

    bmx::amiga::poll_input(NULL);

    // F12/menu pump slot (same as VICE videoarch pump).
    ui_handle_toggle_or_quick_func();

    bmx::amiga::video_render();
    // Drive clicks land in the Paula stream before the pull so they play
    // in the same tick.
    bmx::amiga::config_poll_drive_sounds();
    bmx::amiga::sound_play();

    if (first_frame) {
      first_frame = false;
      circle_boot_complete();
    }

    // Precise 50 Hz pacing: bulk-sleep whole milliseconds, then spin the
    // remainder (SimpleMsDelay truncates and drifts, which starves the
    // audio sink unevenly -> jitter). Skipped entirely while warping.
    if (!warp) {
      unsigned long now_us = CTimer::GetClockTicks();
      next_frame_us += frame_ticks;
      if ((long)(now_us - next_frame_us) > (long)(frame_ticks * 5)) {
        next_frame_us = now_us + frame_ticks;
      } else if ((long)(next_frame_us - now_us) > 0) {
        unsigned long wait_us = next_frame_us - now_us;
        if (wait_us > 2000) {
          CTimer::SimpleMsDelay((wait_us - 1000) / 1000);
        }
        while ((long)(next_frame_us - CTimer::GetClockTicks()) > 0) {
        }
      }
    } else {
      // Stay anchored so leaving warp resumes paced without a jump.
      next_frame_us = CTimer::GetClockTicks() + frame_ticks;
    }
  }
}

void AmigaEmulatorCore::Run(unsigned nCore) {
  assert(nCore > 0);
  switch (nCore) {
  case 1:
    printf("multicore: core 1 role=amiga\r\n");
    RunMainAmiga(true);
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

bool AmigaEmulatorCore::Init(ViceOptions* options) {
  m_options = options;
  return Initialize();
}

void AmigaEmulatorCore::LaunchEmulator(char *timing_option) {
  snprintf(timing_option_, sizeof timing_option_, "%s", timing_option);
#ifdef BMC64_USE_EMU_MULTICORE
  m_Lock.Acquire();
  launch_ = true;
  m_Lock.Release();
#else
  RunMainAmiga(false);
#endif
}

}  // namespace amiga
}  // namespace bmx
