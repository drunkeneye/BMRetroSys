//
// zx_emulator_core.cpp
//
// ZX Spectrum emulator core integration: publishes the machine class,
// builds the menu, inits the model, then runs the reduced-core frame loop
// (one cpu_core_loop_reduced_spectrum() call per opcode; a frame ends when
// frames_video_total_infinito advances).
//

#include "zx_emulator_core.h"

#include <circle/timer.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include "third_party/common/circle.h"
#include "third_party/common/ui.h"

// Defined in zx_bridge.cpp (emux_api.h cannot be included here: it
// clashes with viceoptions.h's RS232 enums in C++).
void emu_machine_init(int raster_skip_enabled, int raster_skip2_enabled);
}

extern "C" {
extern void circle_kernel_core_init_complete(int core);
}

// ZEsarUX core entry points (C linkage, vendored subset).
extern "C" {
#include "zx_zesarux.h"
void cpu_core_loop_reduced_spectrum(void);
}

#include "zx_video.h"
#include "zx_sound.h"
#include "zx_input.h"
#include "zx_config.h"
#include "zx_debug.h"
#include "zx_machine.h"
#include "zx_timer.h"
#include "zx_timer.h"

ZXSpectrumEmulatorCore::ZXSpectrumEmulatorCore(CMemorySystem *pMemorySystem,
                                               int cyclesPerSecond) :
#ifdef BMC64_USE_EMU_MULTICORE
       CMultiCoreSupport(pMemorySystem),
#endif
       launch_(false), cyclesPerSecond_(cyclesPerSecond) {
}

ZXSpectrumEmulatorCore::~ZXSpectrumEmulatorCore(void) {}

void ZXSpectrumEmulatorCore::WaitForLaunch() {
  bool waiting = true;
  while (waiting) {
    m_Lock.Acquire();
    if (launch_) {
      waiting = false;
    }
    m_Lock.Release();
  }
}

void ZXSpectrumEmulatorCore::RunMainZXSpectrum(bool wait) {
  if (wait) {
    WaitForLaunch();
  }

  bmx::zxspectrum::debug_init();
  bmx::zxspectrum::debug_log("Starting ZXSpectrum main loop, tv_mode=%s",
                             timing_option_);

  emu_machine_init(0, 0);
  ui_init_menu();
  bmx::zxspectrum::debug_log("ZXSpectrum: machine class published, menu built");

  bmx::zxspectrum::Config config;
  config.tv_mode = timing_option_;

  if (!bmx::zxspectrum::config_init(config)) {
    bmx::zxspectrum::debug_log("ZXSpectrum config init FAILED");
    return;
  }

  // Boot model 0 (Spectrum 48k); the Machine menu switches live.
  if (!bmx::zxspectrum::machine_init_model(0)) {
    bmx::zxspectrum::debug_log("ZXSpectrum: model 0 init FAILED, halting");
    return;
  }

  RunFrameLoop();
}

void ZXSpectrumEmulatorCore::RunFrameLoop(void) {
  bmx::zxspectrum::video_init();
  bmx::zxspectrum::sound_init();
  bmx::zxspectrum::debug_log("ZXSpectrum frame loop starting");

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
    bmx::zxspectrum::poll_input();
    unsigned long t_ops0 = CTimer::GetClockTicks();
    stage_poll += t_ops0 - t_poll0;

    // The vendored core idles opcodes while esperando_tiempo_final_t_estados
    // is set (its old frame pacer: set at early frame end, cleared on the
    // next wall slot). Pacing now lives outside the core, so the flag is
    // only ever a stall source: a phase-misaligned frame end parks 60k
    // guard iterations with pc/t frozen (watchdog "did not complete" with
    // t~0, videoframes stall, drains starve, audio gaps). Clear it every
    // iteration; frames then always complete by T-burn and the pace below
    // still governs wall speed. The fifty/timer interrupt blocks run
    // unconditionally in the core and are unaffected.
    esperando_tiempo_final_t_estados.v = 0;

    // Run opcodes until the emulated frame completes (50 Hz) or a
    // safety bound trips (60k opcodes ~= 3.5 frames at ~17k/frame).
    unsigned int guard = 0;
    while (frames_video_total_infinito == last_video_frame && guard < 60000) {
      cpu_core_loop_reduced_spectrum();
      guard++;
      op_count++;
    }
    unsigned long t_emu1 = CTimer::GetClockTicks();
    stage_emu += t_emu1 - t_ops0;
    if (frames_video_total_infinito == last_video_frame) {
      if (++stuck_frames == 1) {
        bmx::zxspectrum::debug_log(
            "ZXSpectrum: frame did not complete after %u opcodes "
            "(pc=%04x t=%d)",
            guard, reg_pc, t_estados);
      }
    } else {
      stuck_frames = 0;
      last_video_frame = frames_video_total_infinito;
    }
    frame_count++;

    unsigned long t_vid0 = CTimer::GetClockTicks();
    bmx::zxspectrum::video_render();
    unsigned long t_snd0 = CTimer::GetClockTicks();
    stage_vid += t_snd0 - t_vid0;
    if (!bmx::zxspectrum::timer_warp()) {
      bmx::zxspectrum::sound_play();
    }
    unsigned long t_end = CTimer::GetClockTicks();
    stage_snd += t_end - t_snd0;

    // Frame pacing AFTER all frame work (emulation + video + sound), so
    // post-wait work can never push the frame over budget again. Overran
    // frames and warp mode return immediately (no catch-up waits).
    bmx::zxspectrum::timer_pace_frame_end(t_poll0);
    unsigned long t_pace = CTimer::GetClockTicks();
    stage_pace += t_pace - t_end;

    if (frame_count == 1) {
      circle_boot_complete();
      bmx::zxspectrum::debug_log("ZXSpectrum: boot complete signalled");
    }
    if (frame_count > 0 && (frame_count % 300) == 0) {
      unsigned long presents = 0, ensure_us = 0, present_us = 0;
      bmx::zxspectrum::video_present_stats(&presents, &ensure_us, &present_us);
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
      bmx::zxspectrum::timer_pause_stats(&pause_calls, &pause_spins);
      bmx::zxspectrum::debug_log(
          "ZXSpectrum heartbeat frame=%lu videoframes=%u pc=%04x "
          "presents=%lu realfps=%lu.%02lu sndframes=%lu sndminmax=%d/%d "
          "snddrop=%lu sndstarve=%lu sndin=%d "
          "ops=%lu pausecalls=%lu pausespins=%lu "
          "stageus=%lu/%lu/%lu/%lu paceus=%lu",
          frame_count, last_video_frame, reg_pc, presents,
          realfps_x100 / 100, realfps_x100 % 100,
          bmx::zxspectrum::sound_frames_total(),
          bmx::zxspectrum::sound_out_min(),
          bmx::zxspectrum::sound_out_max(),
          bmx::zxspectrum::sound_drop_frames(),
          bmx::zxspectrum::sound_starve_calls(),
          bmx::zxspectrum::sound_last_in_samples(), op_count, pause_calls,
          pause_spins, stage_poll, stage_emu, stage_vid, stage_snd,
          stage_pace);
      bmx::zxspectrum::sound_window_reset();
      stage_poll = stage_emu = stage_vid = stage_snd = stage_pace = 0;
      // Periodic sound recovery driver: a device dropped for wedging (see
      // ViceSound stall policy) gets recreated here; no-op when healthy.
      circle_boot_complete();
    }
  }
}

void ZXSpectrumEmulatorCore::Run(unsigned nCore) {
  assert(nCore > 0);
  switch (nCore) {
  case 1:
    printf("multicore: core 1 role=zxspectrum\r\n");
    RunMainZXSpectrum(true);
    break;
  case 2:
  case 3:
#ifdef BMC64_USE_EMU_MULTICORE
    bmx::zxspectrum::debug_log("Run: core %u signalling init complete", nCore);
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

bool ZXSpectrumEmulatorCore::Init(ViceOptions* options) {
  m_options = options;
  return Initialize();
}

void ZXSpectrumEmulatorCore::LaunchEmulator(char *timing_option) {
  snprintf(timing_option_, sizeof timing_option_, "%s", timing_option);
#ifdef BMC64_USE_EMU_MULTICORE
  m_Lock.Acquire();
  launch_ = true;
  m_Lock.Release();
#else
  RunMainZXSpectrum(false);
#endif
}
