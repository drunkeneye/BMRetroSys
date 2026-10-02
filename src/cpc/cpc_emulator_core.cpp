//
// cpc_emulator_core.cpp
//
// CPC emulator core integration: publishes the machine class, builds the
// menu, inits the model, then runs the CPC frame loop (one
// cpu_core_loop_cpc() call per opcode; a frame ends when the CRTC line
// count wraps and frames_video_total_infinito advances).
//

#include "cpc_emulator_core.h"

#include <circle/timer.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include "third_party/common/circle.h"
#include "third_party/common/ui.h"

// Defined in cpc_bridge.cpp (emux_api.h cannot be included here: it
// clashes with viceoptions.h's RS232 enums in C++).
void emu_machine_init(int raster_skip_enabled, int raster_skip2_enabled);
}

extern "C" {
extern void circle_kernel_core_init_complete(int core);
}

// CPC core entry points (C linkage, vendored subset + src/cpc).
extern "C" {
#include "cpc_zesarux.h"
}

extern "C" {
extern unsigned int frames_video_total_infinito;
}

#include "cpc_video.h"
#include "cpc_sound.h"
#include "cpc_input.h"
#include "cpc_config.h"
#include "cpc_debug.h"
#include "cpc_machine.h"
#include "cpc_timer.h"
#include "cpc_tape.h"
#include "cpc_bridge.h"

CPCEmulatorCore::CPCEmulatorCore(CMemorySystem *pMemorySystem,
                                               int cyclesPerSecond) :
#ifdef BMC64_USE_EMU_MULTICORE
       CMultiCoreSupport(pMemorySystem),
#endif
       launch_(false), cyclesPerSecond_(cyclesPerSecond) {
}

CPCEmulatorCore::~CPCEmulatorCore(void) {}

void CPCEmulatorCore::WaitForLaunch() {
  bool waiting = true;
  while (waiting) {
    m_Lock.Acquire();
    if (launch_) {
      waiting = false;
    }
    m_Lock.Release();
  }
}

void CPCEmulatorCore::RunMainCPC(bool wait) {
  if (wait) {
    WaitForLaunch();
  }

  bmx::cpc::debug_init();
  bmx::cpc::debug_log("Starting CPC main loop, tv_mode=%s",
                             timing_option_);

  emu_machine_init(0, 0);
  ui_init_menu();
  bmx::cpc::debug_log("CPC: machine class published, menu built");

  bmx::cpc::Config config;
  config.tv_mode = timing_option_;

  if (!bmx::cpc::config_init(config)) {
    bmx::cpc::debug_log("CPC config init FAILED");
    return;
  }

  // Boot the 6128 (most software targets 64K, which runs fine on 128K);
  // the Machine menu switches live.
  if (!bmx::cpc::machine_init_model(2)) {
    bmx::cpc::debug_log("CPC: model 2 init FAILED, halting");
    return;
  }

  RunFrameLoop();
}

void CPCEmulatorCore::RunFrameLoop(void) {
  bmx::cpc::video_init();
  bmx::cpc::sound_init();
  bmx::cpc::debug_log("CPC frame loop starting");

  unsigned long frame_count = 0;
  unsigned int last_video_frame = frames_video_total_infinito;
  unsigned long op_count = 0;
  // Watchdog: if the video frame counter never advances, the core is
  // stuck (bad ROM/paging) — log once, keep pumping the menu.
  unsigned long stuck_frames = 0;
  // Wall clock of the last heartbeat (real frame rate + pacing check).
  unsigned long heartbeat_us = CTimer::GetClockTicks();
  // Per-stage tick accumulators (clock-free ratios reveal a wedged stage
  // regardless of tick rate).
  unsigned long stage_poll = 0, stage_emu = 0, stage_vid = 0, stage_snd = 0;
  unsigned long stage_pace = 0;

  while (true) {
    // Input + menu pump (F12), same slot as the other kernels.
    unsigned long t_poll0 = CTimer::GetClockTicks();
    bmx::cpc::poll_input();
    unsigned long t_ops0 = CTimer::GetClockTicks();
    stage_poll += t_ops0 - t_poll0;

    // Run opcodes until the emulated frame completes (50 Hz) or a
    // safety bound trips (60k opcodes ~= 7 frames at ~8k/frame).
    unsigned int guard = 0;
    while (frames_video_total_infinito == last_video_frame && guard < 60000) {
      cpu_core_loop_cpc();
      guard++;
      op_count++;
    }
    unsigned long t_emu1 = CTimer::GetClockTicks();
    stage_emu += t_emu1 - t_ops0;
    if (frames_video_total_infinito == last_video_frame) {
      if (++stuck_frames == 1) {
        bmx::cpc::debug_log(
            "CPC: frame did not complete after %u opcodes "
            "(pc=%04x t=%d)",
            guard, reg_pc, t_estados);
      }
    } else {
      stuck_frames = 0;
      last_video_frame = frames_video_total_infinito;
    }
    frame_count++;

    unsigned long t_vid0 = CTimer::GetClockTicks();
    bmx::cpc::video_render();
    unsigned long t_snd0 = CTimer::GetClockTicks();
    stage_vid += t_snd0 - t_vid0;
    if (!bmx::cpc::timer_warp()) {
      bmx::cpc::sound_play();
    }
    unsigned long t_end = CTimer::GetClockTicks();
    stage_snd += t_end - t_snd0;

    // Frame pacing AFTER all frame work (emulation + video + sound), so
    // post-wait work can never push the frame over budget again. Overran
    // frames and warp mode return immediately (no catch-up waits).
    bmx::cpc::timer_pace_frame_end(t_poll0);
    unsigned long t_pace = CTimer::GetClockTicks();
    stage_pace += t_pace - t_end;

    if (frame_count == 1) {
      circle_boot_complete();
      bmx::cpc::debug_log("CPC: boot complete signalled");
    }
    if (frame_count > 0 && (frame_count % 300) == 0) {
      unsigned long presents = 0, ensure_us = 0, present_us = 0;
      bmx::cpc::video_present_stats(&presents, &ensure_us, &present_us);
      unsigned long now_us = CTimer::GetClockTicks();
      // NOTE: unsigned long is 32-bit here, so the x100 scale needs 64-bit
      // (300*1e8 overflows 32-bit). Was 300*1e6 (missing x100): it printed
      // 0.24 for a true 24 fps.
      unsigned long long delta_us =
          (now_us > heartbeat_us) ? (unsigned long long)(now_us - heartbeat_us)
                                  : 0ULL;
      unsigned long realfps_x100 =
          delta_us != 0ULL
              ? (unsigned long)((300ULL * 1000000ULL * 100ULL) / delta_us)
              : 0UL;
      heartbeat_us = now_us;
      unsigned long pause_calls = 0, pause_spins = 0;
      bmx::cpc::timer_pause_stats(&pause_calls, &pause_spins);
      unsigned vid_mode = 0, vid_lines = 0;
      bmx::cpc::video_frame_stats(&vid_mode, &vid_lines);
      unsigned vid_r1 = 0, vid_r6 = 0, vid_ma = 0, vid_border = 0;
      bmx::cpc::video_crtc_stats(&vid_r1, &vid_r6, &vid_ma, &vid_border);
      int vid_left = 0, vid_top = 0, vid_w = 0, vid_h = 0;
      bmx::cpc::video_src_stats(&vid_left, &vid_top, &vid_w, &vid_h);
      bmx::cpc::debug_log(
          "CPC heartbeat frame=%lu videoframes=%u pc=%04x "
          "presents=%lu realfps=%lu.%02lu sndframes=%lu sndminmax=%d/%d "
          "snddrop=%lu sndstarve=%lu sndin=%d "
          "ops=%lu pausecalls=%lu pausespins=%lu "
          "stageus=%lu/%lu/%lu/%lu paceus=%lu vidmode=%u vidlines=%u "
          "vidcrtc=%u/%u/%04x vidborder=%u vidsrc=%d,%d,%d,%d",
          frame_count, last_video_frame, reg_pc, presents,
          realfps_x100 / 100, realfps_x100 % 100,
          bmx::cpc::sound_frames_total(),
          bmx::cpc::sound_out_min(),
          bmx::cpc::sound_out_max(),
          bmx::cpc::sound_drop_frames(),
          bmx::cpc::sound_starve_calls(),
          bmx::cpc::sound_last_in_samples(), op_count, pause_calls,
          pause_spins, stage_poll, stage_emu, stage_vid, stage_snd,
          stage_pace, vid_mode, vid_lines, vid_r1, vid_r6, vid_ma,
          vid_border, vid_left, vid_top, vid_w, vid_h);
      bmx::cpc::sound_window_reset();
      stage_poll = stage_emu = stage_vid = stage_snd = stage_pace = 0;
      // Autostart warp ends when the tape stream plays out (the load is
      // done); a no-op for normal warp use. Periodic boot_complete keeps
      // the sound device recovery driver primed (see vicesound policy).
      if (bmx::cpc::autostart_warp_active() && bmx::cpc::tape_finished()) {
        bmx::cpc::timer_set_warp(false);
        bmx::cpc::autostart_warp_clear();
        bmx::cpc::debug_log("CPC: autostart warp off (tape finished)");
      }
      circle_boot_complete();
    }
  }
}

void CPCEmulatorCore::Run(unsigned nCore) {
  assert(nCore > 0);
  switch (nCore) {
  case 1:
    printf("multicore: core 1 role=cpc\r\n");
    RunMainCPC(true);
    break;
  case 2:
  case 3:
#ifdef BMC64_USE_EMU_MULTICORE
    bmx::cpc::debug_log("Run: core %u signalling init complete", nCore);
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

bool CPCEmulatorCore::Init(ViceOptions* options) {
  m_options = options;
  return Initialize();
}

void CPCEmulatorCore::LaunchEmulator(char *timing_option) {
  snprintf(timing_option_, sizeof timing_option_, "%s", timing_option);
#ifdef BMC64_USE_EMU_MULTICORE
  m_Lock.Acquire();
  launch_ = true;
  m_Lock.Release();
#else
  RunMainCPC(false);
#endif
}
