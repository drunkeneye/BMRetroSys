//
// mac68k_emulator_core.cpp
//
// Basilisk II (Macintosh 68k) emulator core integration for BMX bare-metal:
// frame loop with boot, blocking m68k_execute, post-quit idle and
// menu-driven reboot. 60Hz guest services run in the cpu tick hook
// (mac68k_bmx_tick.cpp).
//

#include "mac68k_emulator_core.h"

#include <assert.h>
#include <circle/timer.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mac68k_boot.h"
#include "mac68k_bmx_glue.h"
#include "mac68k_config.h"
#include "mac68k_debug.h"
#include "mac68k_input.h"
#include "mac68k_menu.h"
#include "mac68k_sound.h"
#include "mac68k_video.h"

// UI/menu entry points (C linkage; declared manually like
// src/a2600/a2600_emulator_core.cpp does).
extern "C" {
void emu_machine_init(int raster_skip_enabled, int raster_skip2_enabled);
void ui_init_menu(void);
}

// UAE CPU entry points (declared manually — including uae_cpu/newcpu.h here
// would pull sysdeps.h into a TU that already includes Circle headers, and
// Circle's circle/types.h clashes with the sysdeps uintptr/intptr
// typedefs; see mac68k_basilisk_globals.cpp).
extern void m68k_reset(void);
extern void m68k_execute(void);
extern bool quit_program;
// Basilisk bank/host globals (basilisk_glue.cpp + mac68k_bmx_video.cpp).
// uint32/uint8/int32 are sysdeps.h spellings: unsigned int/unsigned char/int
// here (SIZEOF_INT == 4 on ARM newlib), declared manually like m68k_reset
// above so this Circle TU stays sysdeps-free.
extern unsigned int RAMBaseMac;
extern unsigned int ROMBaseMac;
extern unsigned char *ROMBaseHost;
extern unsigned int ROMSize;
extern unsigned char *MacFrameBaseHost;
extern unsigned int MacFrameSize;
extern int MacFrameLayout;
extern int emulated_ticks;
// Single-shot diag, sysdeps-free spellings (see above).
extern unsigned short ROMVersion;
extern bool TwentyFourBitAddressing;
extern unsigned int MacScreenWidth;
extern unsigned int MacScreenHeight;
// Opcode dispatch table (newcpu.h: cpuop_func with empty REGPARAM on ARM).
typedef void (*bmx_cpuop_handler_t)(unsigned int);
extern bmx_cpuop_handler_t cpufunctbl[65536];
// Memory bank probe (memory.h addrbank layout: 6 access funcs + xlate;
// SAVE_MEMORY_BANKS is force-enabled in banked mode, so mem_banks is an
// array of POINTERS to addrbank).
struct bmx_bank_mirror {
  void *lget;
  void *wget;
  void *bget;
  void *lput;
  void *wput;
  void *bput;
  void *xlate;
};
extern struct bmx_bank_mirror *mem_banks[65536];
extern struct bmx_bank_mirror rom_bank;
extern struct bmx_bank_mirror dummy_bank;
// Basilisk teardown (third_party/basilisk2/main.cpp): exits drivers,
// video, audio, ADB, timers. Prefs and the RAM area survive it; boot_init()
// frees/re-mallocs the area and runs InitAll fresh. Exit680x0 (like
// QuitEmulator in Unix/main_unix.cpp) tears down the UAE CPU tables before
// ExitAll so a reboot re-runs init_m68k cleanly.
extern void ExitAll(void);
extern void Exit680x0(void);

extern "C" {
void circle_boot_complete(void);
void circle_kernel_core_init_complete(int core);
}

namespace bmx {
namespace mac68k {

Mac68kEmulatorCore::Mac68kEmulatorCore(CMemorySystem *pMemorySystem,
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

Mac68kEmulatorCore::~Mac68kEmulatorCore(void) {}

bool Mac68kEmulatorCore::Init(ViceOptions *options) {
  m_options = options;
#ifdef BMC64_USE_EMU_MULTICORE
  // Releases the secondary cores (Circle spin-table/IPI). Without this,
  // core1 never enters Run(): black screen, no log, no blink. Every other
  // kernel (a2600/dos/atarist/amiga/msx) does exactly this here.
  bool ok = Initialize();
  debug_log("mac68k: Init on core0, multicore init returned ok=%d", (int)ok);
  return ok;
#else
  return true;
#endif
}

void Mac68kEmulatorCore::LaunchEmulator(char *timing_option) {
  snprintf(timing_option_, sizeof(timing_option_), "%s",
           timing_option != nullptr ? timing_option : "pal");
  debug_log("mac68k: LaunchEmulator on core0, timing='%s', releasing core1",
            timing_option_);
#ifdef BMC64_USE_EMU_MULTICORE
  // Multicore: core 1 runs the loop (released via the flag below). This
  // function MUST return so core 0 can finish boot (USB, scheduler).
  m_Lock.Acquire();
  launch_ = true;
  m_Lock.Release();
#else
  RunMainMac68k(false);
#endif
}

void Mac68kEmulatorCore::Run(unsigned nCore) {
  assert(nCore > 0);
  switch (nCore) {
  case 1:
    printf("multicore: core 1 role=mac68k\r\n");
    RunMainMac68k(true);
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

void Mac68kEmulatorCore::WaitForLaunch() {
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
  debug_log("mac68k: core1 launch flag seen");
}

void Mac68kEmulatorCore::RunMainMac68k(bool wait) {
  if (wait) {
    debug_log("mac68k: core1 waiting for launch flag");
    WaitForLaunch();
  }
  // First file access: mounts are up (core0 released us post-Launch).
  // a2600/dos pattern: debug_init() first on core1.
  debug_init();
  debug_log("mac68k: core1 log open, calling emu_machine_init");
  emu_machine_init(0, 0);
  debug_log("mac68k: core1 emu_machine_init done, calling ui_init_menu");
  ui_init_menu();
  debug_log("mac68k: core1 machine class published, menu built");
  RunFrameLoop();
}

void Mac68kEmulatorCore::RunFrameLoop() {
  debug_log("mac68k: core1 RunFrameLoop entered");
  // Liveness beacon FIRST (before video): 1 blink group proves core1
  // reached mac68k code even if the screen stays black. Screen splash
  // follows; stage colors then track boot.
  debug_blink(1);
  debug_log("mac68k: core1 entry blink done, calling video_init");
  video_init();
  sound_init();
  menu_init();
  debug_init();
  Config cfg;
  cfg.tv_mode = timing_option_;
  config_init(cfg);

  // NOTE on the F12 menu: the cpu tick hook (mac68k_bmx_tick.cpp) pumps
  // ui_handle_toggle_or_quick_func() whenever the input drain raises the
  // F12 menu-request flag, so the menu opens while the guest runs. 60Hz
  // guest services (video refresh, timers, audio drain) run in the same
  // hook; mac68k_request_reset() fully reboots the guest below.
  debug_log("mac68k: frame loop starting, F12 menu live via tick pump");

  bool booted = false;
  bool boot_complete_signalled = false;
  unsigned long idle_count = 0;
  while (launch_) {
    if (!booted) {
      if (!boot_init()) {
        debug_log("mac68k: boot_init failed, idle pump (add ROM/disks, then "
                  "reboot from menu)");
        // Red screen + LED code: visible without UART or log access.
        video_boot_stage(10);
        debug_blink(9);
        // Idle pump without a guest: keep video/input alive, retry boot
        // when the menu requests a reset (e.g. after attaching media).
        for (int i = 0; i < 50 && launch_; i++) {
          if (mac68k_take_reset_request() != 0) {
            debug_log("mac68k: reset requested, retrying boot");
            break;
          }
          mac68k_input_t input;
          poll_input(&input);
          drain_guest_keys();
          drain_guest_mouse();
          video_render();
          CTimer::Get()->MsDelay(100);
        }
        idle_count++;
        if (idle_count % 6 == 0) {
          debug_log("mac68k: idle without guest, still waiting for boot");
        }
        continue;
      }
      booted = true;
      if (!boot_complete_signalled) {
        circle_boot_complete();
        boot_complete_signalled = true;
        debug_log("mac68k: boot complete signalled");
      }
    }

    // (Re)enter the guest. A reboot above re-ran boot_init (fresh RAM
    // area, fresh ROM bytes, drivers re-inited); quit_program must be
    // cleared or m68k_execute would return immediately.
    mac68k_quit_requested = false;
    quit_program = false;
    m68k_reset();
    // DIAGNOSTIC (guest-black bisect, temporary): dump pre-execute state.
    // The log ends at "entering m68k_execute" with no tick ever firing, so
    // either the first fetch faults/loops or banks are mis-installed.
    {
      debug_log("mac68k: pre-exec: RAMBaseMac=0x%08x ROMBaseMac=0x%08x "
                "emulated_ticks=%d framehost=%p framesize=%u layout=%d "
                "ROM=0x%04x 24bit=%d screen=%ux%u",
                RAMBaseMac, ROMBaseMac, emulated_ticks,
                (void *)MacFrameBaseHost, MacFrameSize, MacFrameLayout,
                (unsigned)ROMVersion, TwentyFourBitAddressing ? 1 : 0,
                MacScreenWidth, MacScreenHeight);
      if (ROMBaseHost != NULL && ROMSize >= 56) {
        debug_log("mac68k: pre-exec ROM[0x00]=%02x%02x%02x%02x%02x%02x%02x%02x "
                  "ROM[0x28]=%02x%02x%02x%02x%02x%02x%02x%02x",
                  ROMBaseHost[0], ROMBaseHost[1], ROMBaseHost[2],
                  ROMBaseHost[3], ROMBaseHost[4], ROMBaseHost[5],
                  ROMBaseHost[6], ROMBaseHost[7], ROMBaseHost[0x28],
                  ROMBaseHost[0x29], ROMBaseHost[0x2a], ROMBaseHost[0x2b],
                  ROMBaseHost[0x2c], ROMBaseHost[0x2d], ROMBaseHost[0x2e],
                  ROMBaseHost[0x2f]);
        // First fetch: m68k_reset set PC=ROMBaseMac+0x2a, so the guest
        // executes ROM file offset 0x2a (big-endian first word). A NULL
        // handler here means init_m68k never built the tables.
        unsigned first_op =
            ((unsigned)ROMBaseHost[0x2a] << 8) | ROMBaseHost[0x2b];
        debug_log("mac68k: pre-exec first_op=0x%04x handler=%p",
                  first_op, (void *)cpufunctbl[first_op]);
        // Bank probe: read Mac 0x4080002a through the installed bank's
        // wget, exactly as GET_OPCODE would. Dummy bank here means the ROM
        // was never mapped (silent illegal loop, no fault, no tick).
        {
          typedef unsigned int (*bmx_wget_t)(unsigned int);
          struct bmx_bank_mirror *b = mem_banks[0x4080];
          struct bmx_bank_mirror *b0 = mem_banks[0x0000];
          debug_log("mac68k: pre-exec bank4080=%p rom=%p dummy=%p "
                    "wget=%p",
                    (void *)b, (void *)&rom_bank, (void *)&dummy_bank,
                    b != NULL ? b->wget : NULL);
          debug_log("mac68k: pre-exec bank0000=%p wget=%p",
                    (void *)b0, b0 != NULL ? b0->wget : NULL);
          if (b != NULL && b->wget != NULL) {
            unsigned w =
                ((bmx_wget_t)(b->wget))(0x4080002au);
            debug_log("mac68k: pre-exec bank word @0x4080002a=0x%04x "
                      "(expect 0x4efa)",
                      w & 0xffffu);
          }
          // Frame bank probe: 32-bit Macs map the frame at 0xa0000000,
          // 24-bit at 0x00500000. Wrong bank here = guest writes go nowhere
          // and the screen stays black even though the CPU runs.
          {
            typedef unsigned int (*bmx_wget_t)(unsigned int);
            unsigned int fbase =
                TwentyFourBitAddressing ? 0x00500000u : 0xa0000000u;
            struct bmx_bank_mirror *fb = mem_banks[(fbase >> 16) & 0xffffu];
            debug_log("mac68k: pre-exec framebase=0x%08x bank=%p wget=%p "
                      "framehost=%p framesize=%u",
                      fbase, (void *)fb, fb != NULL ? fb->wget : NULL,
                      (void *)MacFrameBaseHost, MacFrameSize);
            if (fb != NULL && fb->wget != NULL && MacFrameBaseHost != NULL) {
              unsigned fw = ((bmx_wget_t)(fb->wget))(fbase);
              debug_log("mac68k: pre-exec frame word @0x%08x=0x%04x",
                        fbase, fw & 0xffffu);
            }
          }
        }
      }
    }
    debug_log("mac68k: m68k_reset done, entering m68k_execute (blocking; "
              "F12 menu stays live via the tick pump)");
    m68k_execute();
    debug_log("mac68k: m68k_execute returned (quit_requested=%d)",
              (int)mac68k_quit_requested);

    // Post-quit idle: pump I/O so the screen stays alive until the user
    // switches machines or requests a reboot from the menu.
    while (launch_) {
      int reset_code = mac68k_take_reset_request();
      if (reset_code != 0) {
        // FULL reboot (not just m68k_reset): tear the Basilisk drivers
        // down, then boot fresh so staged disk/ROM/model/RAM choices
        // apply. boot_init() frees the old RAM area, re-mallocs, reloads
        // the ROM from disk and re-runs InitAll (XPRAM/drivers/Init680x0/
        // PatchROM on fresh bytes).
        debug_log("mac68k: reset request %d: ExitAll + full reboot",
                  reset_code);
        Exit680x0();
        ExitAll();
        debug_log("mac68k: ExitAll done, re-running boot_init");
        if (!boot_init()) {
          debug_log("mac68k: reboot boot_init FAILED, entering idle pump");
          booted = false;
          break;
        }
        debug_log("mac68k: reboot boot_init ok, re-entering guest");
        break;
      }
      mac68k_input_t input;
      poll_input(&input);
      drain_guest_keys();
      drain_guest_mouse();
      video_render();
      CTimer::Get()->MsDelay(20);
    }
  }
  debug_log("mac68k: frame loop exiting");
}

}  // namespace mac68k
}  // namespace bmx
