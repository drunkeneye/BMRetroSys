//
// bbc_emulator_core.cpp
//
// BBC Micro emulator core integration: publishes the machine class, builds
// the menu, creates the CLK machine for the saved model, then runs the
// 50 Hz frame loop (one machine_run_frame() per frame).
//

#include "bbc_emulator_core.h"

#include <circle/timer.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include "third_party/common/circle.h"
#include "third_party/common/ui.h"

// Defined in bbc_bridge.cpp (emux_api.h cannot be included here: it
// clashes with viceoptions.h's RS232 enums in C++).
void emu_machine_init(int raster_skip_enabled, int raster_skip2_enabled);
}

extern "C" {
extern void circle_kernel_core_init_complete(int core);
}

#include "bbc_video.h"
#include "bbc_sound.h"
#include "bbc_input.h"
#include "bbc_config.h"
#include "bbc_debug.h"
#include "bbc_machine.h"
#include "bbc_timer.h"
#include "bbc_bridge.h"

namespace bmx {
namespace bbc {

BbcEmulatorCore::BbcEmulatorCore(CMemorySystem *pMemorySystem,
                                int cyclesPerSecond) :
#ifdef BMC64_USE_EMU_MULTICORE
        CMultiCoreSupport(pMemorySystem),
#endif
        launch_(false), cyclesPerSecond_(cyclesPerSecond) {
  bmx::bbc::debug_log("S0 core constructed cps=%d", cyclesPerSecond_);
}

BbcEmulatorCore::~BbcEmulatorCore(void) {}

void BbcEmulatorCore::WaitForLaunch() {
  bool waiting = true;
  while (waiting) {
    m_Lock.Acquire();
    if (launch_) {
      waiting = false;
    }
    m_Lock.Release();
  }
}

void BbcEmulatorCore::RunMainBBC(bool wait) {
  if (wait) {
    WaitForLaunch();
  }

  bmx::bbc::debug_log("S1 RunMain enter tv_mode=%s", timing_option_);
  bmx::bbc::debug_init();
  bmx::bbc::debug_log("S2 debug_init done (rail replayed above, if any)");

  emu_machine_init(0, 0);
  bmx::bbc::debug_log("S3 emu_machine_init done");
  bmx::bbc::debug_log("S4 ui_init_menu enter");
  ui_init_menu();
  bmx::bbc::debug_log("S5 ui_init_menu done, menu built");

  bmx::bbc::Config config;
  config.tv_mode = timing_option_;

  bmx::bbc::debug_log("S6 config_init enter");
  if (!bmx::bbc::config_init(config)) {
    bmx::bbc::debug_log("S6 config init FAILED, halting");
    return;
  }
  bmx::bbc::debug_log("S6 config ok ntsc=%d pool_roms=%d",
                      bmx::bbc::config_is_ntsc(),
                      bmx::bbc::config_rom_count());

  // Boot the saved model first (settings load in ui_init_menu above),
  // falling back across the table so a partial pool still boots.
  const int nmodels = bmx::bbc::machine_model_count();
  const int start = bmx::bbc::machine_current_index();
  bmx::bbc::debug_log("S7 model probe enter, saved=%d",
                      bmx::bbc::machine_current_index());
  int booted = -1;
  for (int k = 0; k < nmodels; k++) {
    int m = (start + k) % nmodels;
    if (bmx::bbc::machine_init_model(m)) {
      booted = m;
      break;
    }
  }
  if (booted < 0) {
    bmx::bbc::debug_log("S7 HALT: no usable ROM in %s",
                        bmx::bbc::machine_rom_dir());
    return;
  }
  bmx::bbc::debug_log("S7 model %d '%s' selected", booted,
                      bmx::bbc::machine_model(booted)->name);

  // The CLK machine is created after video/sound endpoints exist.
  bmx::bbc::debug_log("S8 machine_create enter");
  bmx::bbc::video_init();
  bmx::bbc::debug_log("S8 video_init done");
  bmx::bbc::sound_init();
  bmx::bbc::debug_log("S8 sound_init done");
  if (!bmx::bbc::machine_create_initial()) {
    bmx::bbc::debug_log("S8 HALT: machine_create FAILED");
    return;
  }
  bmx::bbc::debug_log("S8 machine created ok");
  bmx::bbc::machine_note_core_up();

  RunFrameLoop();
}

void BbcEmulatorCore::RunFrameLoop(void) {
  bmx::bbc::debug_log("S9/S10 entering 50 Hz frame loop");

  unsigned long frame_count = 0;
  unsigned long heartbeat_us = CTimer::GetClockTicks();
  unsigned long stage_poll = 0, stage_emu = 0, stage_vid = 0, stage_snd = 0;
  unsigned long stage_pace = 0;

  while (true) {
    unsigned long t_poll0 = CTimer::GetClockTicks();
    bmx::bbc::poll_input();
    // Disc autostart countdown (attach + shift+break).
    bmx::bbc::bbc_autostart_poll();
    // Same slot as VICE's videoarch pump: consumes ui_toggle_pending (F12)
    // and hotkey quick functions. Without this the menu never opens.
    ui_handle_toggle_or_quick_func();
    unsigned long t_ops0 = CTimer::GetClockTicks();
    stage_poll += t_ops0 - t_poll0;

    // Run the machine in slices until a complete field is ready: a
    // present then always shows exactly one field (bob-selected), never
    // a fragment, so counted rows land identically every time.
    unsigned slices = 0;
    while (!bmx::bbc::video_field_ready() && slices < 15) {
      bmx::bbc::machine_run_slice(0.002);
      slices++;
    }
    bmx::bbc::video_field_ack();
    if (frame_count < 12) {
      bmx::bbc::debug_log("F%lu emu done slices=%u", frame_count, slices);
    }
    unsigned long t_emu1 = CTimer::GetClockTicks();
    stage_emu += t_emu1 - t_ops0;
    frame_count++;

    unsigned long t_vid0 = CTimer::GetClockTicks();
    bmx::bbc::video_render();
    if (frame_count < 12) {
      bmx::bbc::debug_log("F%lu video done", frame_count);
    }
    unsigned long t_snd0 = CTimer::GetClockTicks();
    stage_vid += t_snd0 - t_vid0;
    if (!bmx::bbc::timer_warp()) {
      bmx::bbc::sound_play();
    }
    if (frame_count < 12) {
      bmx::bbc::debug_log("F%lu sound done", frame_count);
    }
    unsigned long t_end = CTimer::GetClockTicks();
    stage_snd += t_end - t_snd0;

    bmx::bbc::timer_pace_frame_end(t_poll0);
    unsigned long t_pace = CTimer::GetClockTicks();
    stage_pace += t_pace - t_end;

    if (frame_count == 1) {
      circle_boot_complete();
      bmx::bbc::debug_log("BBC: boot complete signalled (frame 1)");
    }
    if (frame_count > 0 && (frame_count % 250) == 0) {
      unsigned long presents = 0, ensure_us = 0, present_us = 0;
      bmx::bbc::video_present_stats(&presents, &ensure_us, &present_us);
      unsigned long now_us = CTimer::GetClockTicks();
      unsigned long long delta_us =
          (now_us > heartbeat_us) ? (unsigned long long)(now_us - heartbeat_us)
                                  : 0ULL;
      unsigned long realfps_x100 =
          delta_us != 0ULL
              ? (unsigned long)((250ULL * 1000000ULL * 100ULL) / delta_us)
              : 0UL;
      heartbeat_us = now_us;
      unsigned long pause_calls = 0, pause_spins = 0;
      bmx::bbc::timer_pause_stats(&pause_calls, &pause_spins);
      unsigned vid_mode = 0, vid_lines = 0;
      bmx::bbc::video_frame_stats(&vid_mode, &vid_lines);
      int vid_left = 0, vid_top = 0, vid_w = 0, vid_h = 0;
      bmx::bbc::video_src_stats(&vid_left, &vid_top, &vid_w, &vid_h);
      bmx::bbc::debug_log(
          "BBC heartbeat frame=%lu presents=%lu realfps=%lu.%02lu "
          "sndframes=%lu sndminmax=%d/%d snddrop=%lu sndstarve=%lu sndin=%d "
          "pausecalls=%lu pausespins=%lu "
          "stageus=%lu/%lu/%lu/%lu paceus=%lu vidlines=%u "
          "vidsrc=%d,%d,%d,%d",
          frame_count, presents, realfps_x100 / 100, realfps_x100 % 100,
          bmx::bbc::sound_frames_total(), bmx::bbc::sound_out_min(),
          bmx::bbc::sound_out_max(), bmx::bbc::sound_drop_frames(),
          bmx::bbc::sound_starve_calls(), bmx::bbc::sound_last_in_samples(),
          pause_calls, pause_spins, stage_poll, stage_emu, stage_vid,
          stage_snd, stage_pace, vid_mode, vid_lines, vid_left, vid_top,
          vid_w, vid_h);
      bmx::bbc::sound_window_reset();
      stage_poll = stage_emu = stage_vid = stage_snd = stage_pace = 0;
      circle_boot_complete();
      bmx::bbc::bootrail_blink(1);
    }
  }
}

void BbcEmulatorCore::Run(unsigned nCore) {
  assert(nCore > 0);
  switch (nCore) {
  case 1:
    printf("multicore: core 1 role=bbc\r\n");
    bmx::bbc::debug_log("core1: Run enter");
    RunMainBBC(true);
    break;
  case 2:
  case 3:
#ifdef BMC64_USE_EMU_MULTICORE
    bmx::bbc::debug_log("Run: core %u signalling init complete", nCore);
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

bool BbcEmulatorCore::Init(ViceOptions* options) {
  m_options = options;
  return Initialize();
}

void BbcEmulatorCore::LaunchEmulator(char *timing_option) {
  snprintf(timing_option_, sizeof timing_option_, "%s", timing_option);
#ifdef BMC64_USE_EMU_MULTICORE
  m_Lock.Acquire();
  launch_ = true;
  m_Lock.Release();
#else
  RunMainBBC(false);
#endif
}

}  // namespace bbc
}  // namespace bmx
