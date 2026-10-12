//
// apple2_emulator_core.cpp
//
// Apple II emulator core integration: publishes the machine class, builds
// the menu, inits the model, then runs the KEGS frame loop (one run_16ms()
// per 60 Hz frame).
//

#include "apple2_emulator_core.h"

#include <circle/timer.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include "third_party/common/circle.h"
#include "third_party/common/ui.h"

// Defined in apple2_bridge.cpp (emux_api.h cannot be included here: it
// clashes with viceoptions.h's RS232 enums in C++).
void emu_machine_init(int raster_skip_enabled, int raster_skip2_enabled);
}

extern "C" {
extern void circle_kernel_core_init_complete(int core);
}

// KEGS core globals (global-scope externs; see the header).
#include "apple2_kegs_globals.h"

#include "apple2_video.h"
#include "apple2_sound.h"
#include "apple2_input.h"
#include "apple2_config.h"
#include "apple2_debug.h"
#include "apple2_machine.h"
#include "apple2_timer.h"
#include "apple2_bridge.h"

namespace bmx {
namespace apple2 {

Apple2EmulatorCore::Apple2EmulatorCore(CMemorySystem *pMemorySystem,
                                       int cyclesPerSecond) :
#ifdef BMC64_USE_EMU_MULTICORE
        CMultiCoreSupport(pMemorySystem),
#endif
        launch_(false), cyclesPerSecond_(cyclesPerSecond) {
  bmx::apple2::debug_log("S0 core constructed cps=%d", cyclesPerSecond_);
}

Apple2EmulatorCore::~Apple2EmulatorCore(void) {}

void Apple2EmulatorCore::WaitForLaunch() {
  bool waiting = true;
  while (waiting) {
    m_Lock.Acquire();
    if (launch_) {
      waiting = false;
    }
    m_Lock.Release();
  }
}

void Apple2EmulatorCore::RunMainAPPLE2(bool wait) {
  if (wait) {
    WaitForLaunch();
  }

  bmx::apple2::debug_log("S1 RunMain enter tv_mode=%s", timing_option_);
  bmx::apple2::debug_init();
  bmx::apple2::debug_log("S2 debug_init done (rail replayed above, if any)");

  emu_machine_init(0, 0);
  bmx::apple2::debug_log("S3 emu_machine_init done");
  bmx::apple2::debug_log("S4 ui_init_menu enter");
  ui_init_menu();
  bmx::apple2::debug_log("S5 ui_init_menu done, menu built");

  bmx::apple2::Config config;
  config.tv_mode = timing_option_;

  bmx::apple2::debug_log("S6 config_init enter");
  if (!bmx::apple2::config_init(config)) {
    bmx::apple2::debug_log("S6 config init FAILED, halting");
    return;
  }
  bmx::apple2::debug_log("S6 config ok ntsc=%d pool_roms=%d",
                         bmx::apple2::config_is_ntsc(),
                         bmx::apple2::config_rom_count());

  // Boot the saved model first (settings load in ui_init_menu above),
  // falling back across the table so a partial pool still boots.
  const int nmodels = bmx::apple2::machine_model_count();
  const int start = bmx::apple2::machine_current_index();
  bmx::apple2::debug_log("S7 model probe enter, saved=%d",
                             bmx::apple2::machine_current_index());
  int booted = -1;
  for (int k = 0; k < nmodels; k++) {
    int m = (start + k) % nmodels;
    if (bmx::apple2::machine_init_model(m)) {
      booted = m;
      break;
    }
  }
  if (booted < 0) {
    bmx::apple2::debug_log("S7 HALT: no usable ROM in %s",
                           bmx::apple2::machine_rom_dir());
    return;
  }
  bmx::apple2::debug_log("S7 model %d booted", booted);

  // KEGS one-time init + first reset. Globals are staged before the call:
  // 1 MHz authentic speed, control panel off, no config.kegs writes,
  // status bar off.
  g_config_control_panel = 0;
  g_config_kegs_auto_update = 0;
  if (g_limit_speed < 0 || g_limit_speed > 3) {
    g_limit_speed = 1;
  }
  g_audio_enable = -1;
  g_status_enable = 0;
  g_mainwin_width = 704;
  g_mainwin_height = 462;
  bmx::apple2::debug_log("S8 kegs_init enter");
  if (kegs_init(32, 704, 462, 1) != 0) {
    bmx::apple2::debug_log("S8 HALT: kegs_init FAILED");
    return;
  }
  bmx::apple2::debug_log("S8 kegs_init ok rom_version=%d vbl=%lu",
                             g_rom_version,
                             (unsigned long)g_vbl_count);
  bmx::apple2::machine_note_core_up();

  RunFrameLoop();
}

void Apple2EmulatorCore::RunFrameLoop(void) {
  bmx::apple2::debug_log("S9 video_init enter");
  bmx::apple2::video_init();
  bmx::apple2::debug_log("S9 video_init done");
  bmx::apple2::debug_log("S10 sound_init enter");
  bmx::apple2::sound_init();
  bmx::apple2::debug_log("S10 sound_init done, entering frame loop");

  unsigned long frame_count = 0;
  unsigned long last_vbl = g_vbl_count;
  unsigned long stuck_frames = 0;
  unsigned long heartbeat_us = CTimer::GetClockTicks();
  unsigned long stage_poll = 0, stage_emu = 0, stage_vid = 0, stage_snd = 0;
  unsigned long stage_pace = 0;

  while (true) {
    unsigned long t_poll0 = CTimer::GetClockTicks();
    bmx::apple2::poll_input();
    // Slot-6 autostart countdown (attach + reset + PR#6 typer).
    bmx::apple2::apple2_autostart_poll();
    // Same slot as VICE's videoarch pump: consumes ui_toggle_pending (F12)
    // and hotkey quick functions. Without this the menu never opens.
    ui_handle_toggle_or_quick_func();
    unsigned long t_ops0 = CTimer::GetClockTicks();
    stage_poll += t_ops0 - t_poll0;

    // One KEGS timeslice = one 60 Hz frame (the core paces internally
    // only via the no-op micro_sleep; BMX holds the rate below).
    if (frame_count < 12) {
      bmx::apple2::debug_log("F%lu emu enter", frame_count);
    }
    int rc = run_16ms();
    if (frame_count < 12) {
      bmx::apple2::debug_log("F%lu emu done rc=%d vbl=%lu", frame_count, rc,
                             (unsigned long)g_vbl_count);
    }
    unsigned long t_emu1 = CTimer::GetClockTicks();
    stage_emu += t_emu1 - t_ops0;
    if (rc != 0) {
      bmx::apple2::debug_log("APPLE2: run_16ms returned %d", rc);
      break;
    }
    if (g_vbl_count == last_vbl) {
      if (++stuck_frames == 1) {
        bmx::apple2::debug_log("APPLE2: VBL did not advance");
      }
    } else {
      stuck_frames = 0;
      last_vbl = g_vbl_count;
    }
    frame_count++;

    unsigned long t_vid0 = CTimer::GetClockTicks();
    bmx::apple2::video_render();
    if (frame_count < 12) {
      bmx::apple2::debug_log("F%lu video done", frame_count);
    }
    unsigned long t_snd0 = CTimer::GetClockTicks();
    stage_vid += t_snd0 - t_vid0;
    if (!bmx::apple2::timer_warp()) {
      bmx::apple2::sound_play();
    }
    if (frame_count < 12) {
      bmx::apple2::debug_log("F%lu sound done", frame_count);
    }
    unsigned long t_end = CTimer::GetClockTicks();
    stage_snd += t_end - t_snd0;

    bmx::apple2::timer_pace_frame_end(t_poll0);
    unsigned long t_pace = CTimer::GetClockTicks();
    stage_pace += t_pace - t_end;

    if (frame_count == 1) {
      circle_boot_complete();
      bmx::apple2::debug_log("APPLE2: boot complete signalled (frame 1)");
    }
    if (frame_count > 0 && (frame_count % 300) == 0) {
      unsigned long presents = 0, ensure_us = 0, present_us = 0;
      bmx::apple2::video_present_stats(&presents, &ensure_us, &present_us);
      unsigned long now_us = CTimer::GetClockTicks();
      unsigned long long delta_us =
          (now_us > heartbeat_us) ? (unsigned long long)(now_us - heartbeat_us)
                                  : 0ULL;
      unsigned long realfps_x100 =
          delta_us != 0ULL
              ? (unsigned long)((300ULL * 1000000ULL * 100ULL) / delta_us)
              : 0UL;
      heartbeat_us = now_us;
      unsigned long pause_calls = 0, pause_spins = 0;
      bmx::apple2::timer_pause_stats(&pause_calls, &pause_spins);
      unsigned vid_mode = 0, vid_lines = 0;
      bmx::apple2::video_frame_stats(&vid_mode, &vid_lines);
      int vid_left = 0, vid_top = 0, vid_w = 0, vid_h = 0;
      bmx::apple2::video_src_stats(&vid_left, &vid_top, &vid_w, &vid_h);
      bmx::apple2::debug_log(
          "APPLE2 heartbeat frame=%lu vbl=%lu presents=%lu realfps=%lu.%02lu "
          "sndframes=%lu sndminmax=%d/%d snddrop=%lu sndstarve=%lu sndin=%d "
          "pausecalls=%lu pausespins=%lu "
          "stageus=%lu/%lu/%lu/%lu paceus=%lu vidmode=%04x vidlines=%u "
          "vidsrc=%d,%d,%d,%d",
          frame_count, last_vbl, presents, realfps_x100 / 100,
          realfps_x100 % 100, bmx::apple2::sound_frames_total(),
          bmx::apple2::sound_out_min(), bmx::apple2::sound_out_max(),
          bmx::apple2::sound_drop_frames(),
          bmx::apple2::sound_starve_calls(),
          bmx::apple2::sound_last_in_samples(), pause_calls, pause_spins,
          stage_poll, stage_emu, stage_vid, stage_snd, stage_pace, vid_mode,
          vid_lines, vid_left, vid_top, vid_w, vid_h);
      bmx::apple2::sound_window_reset();
      stage_poll = stage_emu = stage_vid = stage_snd = stage_pace = 0;
      circle_boot_complete();
      bmx::apple2::bootrail_blink(1);
    }
  }
}

void Apple2EmulatorCore::Run(unsigned nCore) {
  assert(nCore > 0);
  switch (nCore) {
  case 1:
    printf("multicore: core 1 role=apple2\r\n");
    bmx::apple2::debug_log("core1: Run enter");
    RunMainAPPLE2(true);
    break;
  case 2:
  case 3:
#ifdef BMC64_USE_EMU_MULTICORE
    bmx::apple2::debug_log("Run: core %u signalling init complete", nCore);
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

bool Apple2EmulatorCore::Init(ViceOptions* options) {
  m_options = options;
  return Initialize();
}

void Apple2EmulatorCore::LaunchEmulator(char *timing_option) {
  snprintf(timing_option_, sizeof timing_option_, "%s", timing_option);
#ifdef BMC64_USE_EMU_MULTICORE
  m_Lock.Acquire();
  launch_ = true;
  m_Lock.Release();
#else
  RunMainAPPLE2(false);
#endif
}

}  // namespace apple2
}  // namespace bmx
