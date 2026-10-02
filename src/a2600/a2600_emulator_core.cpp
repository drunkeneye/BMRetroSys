//
// a2600_emulator_core.cpp
//
// Atari 2600 emulator core integration: publishes the machine class,
// builds the menu, runs the Stella frame loop.
//

#include "a2600_emulator_core.h"

#include <circle/devicenameservice.h>
#include <circle/timer.h>

#include <assert.h>
#include <stdio.h>
#include <string.h>

extern "C" {
#include "third_party/common/circle.h"

// Defined in a2600_bridge.cpp (emux_api.h cannot be included here: it
// clashes with viceoptions.h's RS232 enums in C++).
void emu_machine_init(int raster_skip_enabled, int raster_skip2_enabled);
void ui_init_menu(void);
void ui_handle_toggle_or_quick_func(void);
void circle_boot_complete(void);
}

extern "C" {
extern void circle_kernel_core_init_complete(int core);
}

#include "third_party/common/ui.h"

#include "a2600_video.h"
#include "a2600_sound.h"
#include "a2600_input.h"
#include "a2600_config.h"
#include "a2600_debug.h"
#include "a2600_cart.h"
#include "a2600_timer.h"
#include "a2600_bridge.h"

A2600EmulatorCore::A2600EmulatorCore(CMemorySystem *pMemorySystem,
                                     int cyclesPerSecond)
    :
#ifdef BMC64_USE_EMU_MULTICORE
      CMultiCoreSupport(pMemorySystem),
#endif
      launch_(false),
      cyclesPerSecond_(cyclesPerSecond) {
}

A2600EmulatorCore::~A2600EmulatorCore(void) {}

void A2600EmulatorCore::WaitForLaunch() {
  bool waiting = true;
  while (waiting) {
    m_Lock.Acquire();
    if (launch_) {
      waiting = false;
    }
    m_Lock.Release();
  }
}

void A2600EmulatorCore::RunMainA2600(bool wait) {
  if (wait) {
    WaitForLaunch();
  }

  bmx::a2600::debug_init();
  bmx::a2600::debug_log("Starting A2600 main loop, tv_mode=%s",
                        timing_option_);
#ifdef BMX_BUILD_STAMP
#define A2600_BMX_STR_(x) #x
#define A2600_BMX_STR(x) A2600_BMX_STR_(x)
  bmx::a2600::debug_log("A2600 build stamp %s", A2600_BMX_STR(BMX_BUILD_STAMP));
#else
  bmx::a2600::debug_log("A2600 build stamp unknown");
#endif

  emu_machine_init(0, 0);
  ui_init_menu();
  bmx::a2600::debug_log("A2600: machine class published, menu built");

  bmx::a2600::Config config;
  config.tv_mode = timing_option_;

  if (!bmx::a2600::config_init(config)) {
    bmx::a2600::debug_log("A2600 config init FAILED");
    return;
  }

  RunFrameLoop();
}

void A2600EmulatorCore::RunFrameLoop(void) {
  bmx::a2600::video_init();
  bmx::a2600::sound_init();
  bmx::a2600::debug_log("A2600 frame loop starting");

  unsigned long frame_count = 0;
  unsigned long heartbeat_us = CTimer::GetClockTicks();
  // Scratch for one frame of Stella stereo audio (~31 kHz).
  static int16_t s_stereo[2048];

  while (true) {
    const unsigned long frame_start_us = CTimer::GetClockTicks();
    // Input pump (USB pads -> Stella events) + F12 menu. The menu runs
    // on this thread while open; Stella stays paused meanwhile.
    bmx::a2600::cart_pump_input();
    ui_handle_toggle_or_quick_func();
    if (bmx::a2600::cart_has_console()) {
      const uint32_t *pixels = nullptr;
      int pitch = 0, w = 0, h = 0, refresh = 50;
      if (bmx::a2600::cart_run_frame(pixels, pitch, w, h, refresh)) {
        bmx::a2600::video_blit(pixels, w, h, pitch);
      }
      int stella_rate = 0;
      unsigned int pairs = bmx::a2600::cart_drain_audio(
          s_stereo, sizeof(s_stereo) / (2 * sizeof(s_stereo[0])),
          stella_rate);
      if (pairs > 0) {
        bmx::a2600::sound_drain(s_stereo, pairs, stella_rate);
      }
      bmx::a2600::timer_pace_frame_end(refresh, frame_start_us);
    } else {
      // No cartridge: baked-in splash (not black) + idle pace, menu
      // stays usable (attach via Drives).
      bmx::a2600::video_show_splash();
      CTimer::SimpleMsDelay(20);
    }
    bmx::a2600::video_render();
    frame_count++;
    if (frame_count == 1) {
      circle_boot_complete();
      bmx::a2600::debug_log("A2600: boot complete signalled");
    }

    unsigned long now_us = CTimer::GetClockTicks();
    if (now_us - heartbeat_us >= 5000000UL) {
      unsigned long presents = 0, ensure_us = 0, present_us = 0;
      bmx::a2600::video_present_stats(&presents, &ensure_us, &present_us);
      int key_depth = 0, joy_depth = 0;
      bmx::a2600::input_queue_depths(&key_depth, &joy_depth);
      unsigned long key_total = 0;
      long last_key = 0;
      bmx::a2600::input_key_stats(&key_total, &last_key);
      // Raw Circle device presence (bit0=ukbd1 bit1=ukbd2 bit2=mouse1
      // bit3=upad1): tells USB enumeration apart from framework
      // registration. Lock-protected GetDevice, safe from this core.
      unsigned usb_mask = 0;
      CDeviceNameService *svc = CDeviceNameService::Get();
      if (svc != nullptr) {
        if (svc->GetDevice("ukbd1", FALSE) != nullptr) {
          usb_mask |= 0x01;
        }
        if (svc->GetDevice("ukbd2", FALSE) != nullptr) {
          usb_mask |= 0x02;
        }
        if (svc->GetDevice("mouse1", FALSE) != nullptr) {
          usb_mask |= 0x04;
        }
        if (svc->GetDevice("upad1", FALSE) != nullptr) {
          usb_mask |= 0x08;
        }
      }
      bmx::a2600::debug_log(
          "A2600 heartbeat frames=%lu presents=%lu cart=%s ui=%d toggle=%d "
          "keyq=%d joyq=%d kbd=%d keys=%lu lastkey=%ld usb=0x%x core0=%lu "
          "boot=%lu",
          frame_count, presents, bmx::a2600::cart_path(), (int)ui_enabled,
          (int)ui_toggle_pending, key_depth, joy_depth,
          emu_get_keyboard_count(), key_total, last_key, usb_mask,
          bmx_core0_tick(), bmx_get_boot_stage());
      heartbeat_us = now_us;
    }
  }
}

void A2600EmulatorCore::Run(unsigned nCore) {
  assert(nCore > 0);
  switch (nCore) {
  case 1:
    printf("multicore: core 1 role=a2600\r\n");
    RunMainA2600(true);
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

bool A2600EmulatorCore::Init(ViceOptions* options) {
  m_options = options;
  return Initialize();
}

void A2600EmulatorCore::LaunchEmulator(char *timing_option) {
  snprintf(timing_option_, sizeof(timing_option_), "%s",
           timing_option != NULL ? timing_option : "pal");
#ifdef BMC64_USE_EMU_MULTICORE
  // Multicore: core 1 runs the loop (released via the flag below). This
  // function MUST return so core 0 can finish boot (USB, scheduler).
  // Running the loop here wedges core 0 in Init: no USB, no input, ever.
  m_Lock.Acquire();
  launch_ = true;
  m_Lock.Release();
#else
  RunMainA2600(false);
#endif
}
