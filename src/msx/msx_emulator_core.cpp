//
// msx_emulator_core.cpp
//
// MSX emulator core integration: publishes the machine class, builds the
// menu, inits the model, then runs the MSX frame loop (one
// cpu_core_loop_msx() call per opcode; a frame ends after 312 scanlines
// and frames_video_total_infinito advances).
//

#include "msx_emulator_core.h"

#include <circle/timer.h>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include "third_party/common/circle.h"
#include "third_party/common/ui.h"

// Defined in msx_bridge.cpp (emux_api.h cannot be included here: it
// clashes with viceoptions.h's RS232 enums in C++).
void emu_machine_init(int raster_skip_enabled, int raster_skip2_enabled);
}

extern "C" {
extern void circle_kernel_core_init_complete(int core);
}

// MSX core entry points (C linkage, vendored subset + src/msx).
extern "C" {
#include "msx_zesarux.h"
}

extern "C" {
void cpu_core_loop_msx(void);
extern unsigned int frames_video_total_infinito;
// Display-pipeline state (emux_api.c / overlay.c globals; emux_api.h
// cannot be included here, see above).
extern int vic_showing;
extern int vic_enabled;
extern int vdc_showing;
extern int vdc_enabled;
extern int statusbar_showing;
extern int statusbar_enabled;
extern int vkbd_showing;
extern int vkbd_enabled;
extern int diagnostics_showing;
// Present-path DIAG (fbl_native_kms.cpp): cumulative counters per scanout
// path + KMS-active flag + actual per-layer Showing() state.
void circle_diag_present_stats(unsigned long *direct, unsigned long *v3d,
                               unsigned long *soft, int *kms_active,
                               int *show0, int *show1, int *show2, int *show3);
// AY PSG register file (vendored ay38912.c): R0-R5 tone periods, R6 noise,
// R7 mixer, R8-R10 volumes (+16 = envelope), R11-R13 envelope.
extern unsigned char ay_3_8912_registros[][16];
}

#include "msx_video.h"
#include "msx_sound.h"
#include "msx_input.h"
#include "msx_config.h"
#include "msx_debug.h"
#include "msx_machine.h"
#include "msx_timer.h"
#include "msx_tape.h"
#include "msx_cart.h"
#include "msx_bridge.h"

MSXEmulatorCore::MSXEmulatorCore(CMemorySystem *pMemorySystem,
                                               int cyclesPerSecond) :
#ifdef BMC64_USE_EMU_MULTICORE
       CMultiCoreSupport(pMemorySystem),
#endif
       launch_(false), cyclesPerSecond_(cyclesPerSecond) {
}

MSXEmulatorCore::~MSXEmulatorCore(void) {}

void MSXEmulatorCore::WaitForLaunch() {
  bool waiting = true;
  while (waiting) {
    m_Lock.Acquire();
    if (launch_) {
      waiting = false;
    }
    m_Lock.Release();
  }
}

void MSXEmulatorCore::RunMainMSX(bool wait) {
  if (wait) {
    WaitForLaunch();
  }

  bmx::msx::debug_init();
  bmx::msx::debug_log("Starting MSX main loop, tv_mode=%s",
                             timing_option_);
#ifdef BMX_BUILD_STAMP
#define MSX_BMX_STR_(x) #x
#define MSX_BMX_STR(x) MSX_BMX_STR_(x)
  bmx::msx::debug_log("MSX build stamp %s", MSX_BMX_STR(BMX_BUILD_STAMP));
#else
  bmx::msx::debug_log("MSX build stamp unknown");
#endif

  emu_machine_init(0, 0);
  ui_init_menu();
  bmx::msx::debug_log("MSX: machine class published, menu built");

  bmx::msx::Config config;
  config.tv_mode = timing_option_;

  if (!bmx::msx::config_init(config)) {
    bmx::msx::debug_log("MSX config init FAILED");
    return;
  }

  // Boot MSX1; the Machine menu switches live.
  if (!bmx::msx::machine_init_model(0)) {
    bmx::msx::debug_log("MSX: model 0 init FAILED, halting");
    return;
  }

  RunFrameLoop();
}

void MSXEmulatorCore::RunFrameLoop(void) {
  bmx::msx::video_init();
  bmx::msx::sound_init();
  bmx::msx::debug_log("MSX frame loop starting");

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
    bmx::msx::poll_input();
    unsigned long t_ops0 = CTimer::GetClockTicks();
    stage_poll += t_ops0 - t_poll0;

    // Run opcodes until the emulated frame completes (50 Hz) or a
    // safety bound trips (60k opcodes ~= 7 frames at ~8k/frame).
    unsigned int guard = 0;
    while (frames_video_total_infinito == last_video_frame && guard < 60000) {
      cpu_core_loop_msx();
      guard++;
      op_count++;
    }
    unsigned long t_emu1 = CTimer::GetClockTicks();
    stage_emu += t_emu1 - t_ops0;
    if (frames_video_total_infinito == last_video_frame) {
      if (++stuck_frames == 1) {
        bmx::msx::debug_log(
            "MSX: frame did not complete after %u opcodes "
            "(pc=%04x t=%d)",
            guard, reg_pc, t_estados);
      }
    } else {
      stuck_frames = 0;
      last_video_frame = frames_video_total_infinito;
    }
    frame_count++;

    if (frame_count <= 3) {
      bmx::msx::debug_log("MSX: pump done frame=%lu guard=%u video=%u",
                          frame_count, guard, last_video_frame);
    }
    unsigned long t_vid0 = CTimer::GetClockTicks();
    if (frame_count == 1) {
      bmx::msx::debug_log("MSX: before first video_render");
    }
    bmx::msx::video_render();
    if (frame_count == 1) {
      bmx::msx::debug_log("MSX: after first video_render");
    }
    unsigned long t_snd0 = CTimer::GetClockTicks();
    stage_vid += t_snd0 - t_vid0;
    if (!bmx::msx::timer_warp()) {
      bmx::msx::sound_play();
    }
    unsigned long t_end = CTimer::GetClockTicks();
    stage_snd += t_end - t_snd0;

    // Frame pacing AFTER all frame work (emulation + video + sound), so
    // post-wait work can never push the frame over budget again. Overran
    // frames and warp mode return immediately (no catch-up waits).
    bmx::msx::timer_pace_frame_end(t_poll0);
    unsigned long t_pace = CTimer::GetClockTicks();
    stage_pace += t_pace - t_end;

    if (frame_count == 1) {
      circle_boot_complete();
      bmx::msx::debug_log("MSX: boot complete signalled");
    }
    if (frame_count > 0 && (frame_count % 300) == 0) {
      unsigned long presents = 0, ensure_us = 0, present_us = 0;
      bmx::msx::video_present_stats(&presents, &ensure_us, &present_us);
      unsigned long now_us = CTimer::GetClockTicks();
      // NOTE: unsigned long is 32-bit here, so the x100 scale needs 64-bit.
      unsigned long long delta_us =
          (now_us > heartbeat_us) ? (unsigned long long)(now_us - heartbeat_us)
                                : 0ULL;
      unsigned long realfps_x100 =
          delta_us != 0ULL
              ? (unsigned long)((300ULL * 1000000ULL * 100ULL) / delta_us)
              : 0UL;
      heartbeat_us = now_us;
      unsigned long pause_calls = 0, pause_spins = 0;
      bmx::msx::timer_pause_stats(&pause_calls, &pause_spins);
      unsigned vid_mode = 0, vid_lines = 0;
      bmx::msx::video_frame_stats(&vid_mode, &vid_lines);
      int vid_left = 0, vid_top = 0, vid_w = 0, vid_h = 0;
      bmx::msx::video_src_stats(&vid_left, &vid_top, &vid_w, &vid_h);
      // VDP state + VRAM checksum: the photo shows thin black dashes in
      // the text area (FBL content vs scaler path still undecided).
      unsigned vram_sum = 0;
      if (msx_vram_memory != nullptr) {
        for (int vi = 0; vi < 16384; vi++) {
          vram_sum += msx_vram_memory[vi];
        }
      }
      bmx::msx::debug_log(
          "MSX vdp R0-7=%02x %02x %02x %02x %02x %02x %02x %02x "
          "st=%02x vram_sum=%05x",
          vdp_9918a_registers[0], vdp_9918a_registers[1],
          vdp_9918a_registers[2], vdp_9918a_registers[3],
          vdp_9918a_registers[4], vdp_9918a_registers[5],
          vdp_9918a_registers[6], vdp_9918a_registers[7],
          vdp_9918a_status_register, vram_sum & 0xfffff);
      {
        // Display-pipeline state: which planes composite, and the live
        // scaler geometry (FBL clean in BMPs, artifact downstream).
        int dpw = 0, dph = 0, fbw = 0, fbh = 0, sw = 0, sh = 0, dw = 0,
            dh = 0;
        circle_get_fbl_dimensions(FB_LAYER_VIC, &dpw, &dph, &fbw, &fbh, &sw,
                                 &sh, &dw, &dh);
        bmx::msx::debug_log(
            "MSX layers vic=%d/%d vdc=%d/%d stb=%d/%d vk=%d/%d diag=%d "
            "ui=%d/%d geom disp=%dx%d fb=%dx%d src=%dx%d dst=%dx%d",
            vic_showing, vic_enabled, vdc_showing, vdc_enabled,
            statusbar_showing, statusbar_enabled, vkbd_showing, vkbd_enabled,
            diagnostics_showing, ui_showing, ui_enabled, dpw, dph, fbw, fbh,
            sw, sh, dw, dh);
      }
      {
        // Scanout ground truth: which present path serves frames (direct
        // hwscale planes / V3D / CPU composition) and which layers are
        // really in the plane list (vs. stale menu flags).
        unsigned long pp_direct = 0, pp_v3d = 0, pp_soft = 0;
        int kms = 0, sh0 = 0, sh1 = 0, sh2 = 0, sh3 = 0;
#if RASPPI != 3
        // Present-path counters live in fbl_native_kms.cpp, which Pi3
        // (firmware dispmanx path, fbl.o) does not link.
        circle_diag_present_stats(&pp_direct, &pp_v3d, &pp_soft, &kms,
                                  &sh0, &sh1, &sh2, &sh3);
#endif
        bmx::msx::debug_log(
            "MSX scan direct=%lu v3d=%lu soft=%lu kms=%d actual=%d%d%d%d",
            pp_direct, pp_v3d, pp_soft, kms, sh0, sh1, sh2, sh3);
      }
      bmx::msx::debug_log(
          "MSX heartbeat frame=%lu videoframes=%u pc=%04x "
          "presents=%lu realfps=%lu.%02lu sndframes=%lu sndminmax=%d/%d "
          "ayin=%d/%d snddrop=%lu sndstarve=%lu sndin=%d sndspace=%d "
          "ay8=%d/%d "
          "ops=%lu pausecalls=%lu pausespins=%lu "
          "stageus=%lu/%lu/%lu/%lu paceus=%lu vidmode=%u vidlines=%u "
          "vidsrc=%d,%d,%d,%d",
          frame_count, last_video_frame, reg_pc, presents,
          realfps_x100 / 100, realfps_x100 % 100,
          bmx::msx::sound_frames_total(),
          bmx::msx::sound_out_min(),
          bmx::msx::sound_out_max(),
          bmx::msx::sound_in_min(),
          bmx::msx::sound_in_max(),
          bmx::msx::sound_drop_frames(),
          bmx::msx::sound_starve_calls(),
          bmx::msx::sound_last_in_samples(),
          bmx::msx::sound_sink_min_space(),
          bmx::msx::sound_ay8_max(), bmx::msx::sound_ay8_env(),
          op_count, pause_calls,
          pause_spins, stage_poll, stage_emu, stage_vid, stage_snd,
          stage_pace, vid_mode, vid_lines, vid_left, vid_top, vid_w, vid_h);
      bmx::msx::debug_log(
          "MSX ay R0-13=%02x %02x %02x %02x %02x %02x %02x %02x "
          "%02x %02x %02x %02x %02x %02x",
          ay_3_8912_registros[0][0], ay_3_8912_registros[0][1],
          ay_3_8912_registros[0][2], ay_3_8912_registros[0][3],
          ay_3_8912_registros[0][4], ay_3_8912_registros[0][5],
          ay_3_8912_registros[0][6], ay_3_8912_registros[0][7],
          ay_3_8912_registros[0][8], ay_3_8912_registros[0][9],
          ay_3_8912_registros[0][10], ay_3_8912_registros[0][11],
          ay_3_8912_registros[0][12], ay_3_8912_registros[0][13]);
      bmx::msx::sound_window_reset();
      stage_poll = stage_emu = stage_vid = stage_snd = stage_pace = 0;
      circle_boot_complete();
    }
  }
}

void MSXEmulatorCore::Run(unsigned nCore) {
  assert(nCore > 0);
  switch (nCore) {
  case 1:
    printf("multicore: core 1 role=msx\r\n");
    RunMainMSX(true);
    break;
  case 2:
  case 3:
#ifdef BMC64_USE_EMU_MULTICORE
    bmx::msx::debug_log("Run: core %u signalling init complete", nCore);
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
      "1: wfi\n\t");
#endif
#endif
}

bool MSXEmulatorCore::Init(ViceOptions* options) {
  m_options = options;
  return Initialize();
}

void MSXEmulatorCore::LaunchEmulator(char *timing_option) {
  snprintf(timing_option_, sizeof timing_option_, "%s", timing_option);
#ifdef BMC64_USE_EMU_MULTICORE
  m_Lock.Acquire();
  launch_ = true;
  m_Lock.Release();
#else
  RunMainMSX(false);
#endif
}
