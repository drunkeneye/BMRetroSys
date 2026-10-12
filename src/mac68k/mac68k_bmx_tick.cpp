//
// mac68k_bmx_tick.cpp
//
// BMX bare-metal CPU-emulator services hook + platform timer functions.
//
// Hook: int32 emulated_ticks / cpu_do_check_ticks(), the
// USE_CPU_EMUL_SERVICES-compatible pair declared in uae_cpu/newcpu.h. NOTE:
// the hook only fires if the build compiles the UAE core with
// -DUSE_CPU_EMUL_SERVICES (Unix sysdeps.h defines it for NetBSD only; the
// BMX bmx-shim does not). Without that flag cpu_check_ticks() is empty and
// these symbols are unreferenced but harmless. The Makefile workstream must
// add the define to activate 60Hz services.
//
// Timer platform functions (declared in timer.h, owned by no portable TU):
// TimerDateTime, Microseconds, timer_current_time/add/sub/cmp,
// timer_mac2host/host2mac. BMX has no RTC: wall time is a fixed boot epoch
// (2026-01-01 00:00 UTC) plus Circle uptime.
//

#include "sysdeps.h"

#include "mac68k_debug.h"

#include "cpu_emulation.h"
// memory.h must precede newcpu.h (m68k_setpc uses get_real_address;
// same include order as mac68k_bmx_main.cpp). No Circle headers in this
// TU so the sysdeps uintptr/intptr typedefs stay unclashed.
#include "uae_cpu/newcpu.h"
#include "main.h"
#include "macos_util.h"
#include "rom_patches.h"
#include "timer.h"
#include "video.h"
#include "sony.h"
#include "disk.h"

#include "mac68k_input.h"

#include "mac68k_bmx_glue.h"

#include <stdio.h>

// BMX menu pump (F12 toggle + quick funcs). kbd.c sets ui_toggle_pending=2
// machine-independently; like every other kernel's core1 frame loop, the
// tick hook pumps it every 60Hz service so the F12 menu opens while the
// guest runs (guest pauses inside the menu, same as Stella/Hatari).
// Declaration mirrors src/a2600/a2600_emulator_core.cpp:24 (ui.c is C).
extern "C" void ui_handle_toggle_or_quick_func(void);

// Tick access without Circle headers (same pattern as
// mac68k_basilisk_globals.cpp): circle_get_ticks64() is microsecond uptime.
extern "C" {
extern unsigned long circle_get_ticks(void);
extern uint64_t circle_get_ticks64(void);
// Common menu toggle/zip handler (defined in third_party/common/ui.c;
// declared manually here, same pattern as
// src/a2600/a2600_emulator_core.cpp:24, to avoid pulling ui.h into this
// Basilisk TU).
void ui_handle_toggle_or_quick_func(void);
}

// F12 menu-request flag, raised by the mac68k input drain (which filters
// F12 from the ADB stream) and taken here on the emulator thread.
namespace bmx {
namespace mac68k {
bool mac68k_take_menu_request(void);
}  // namespace mac68k
}  // namespace bmx

// Audio drain owned by the audio workstream (workstream B). Weak reference:
// the tick calls it when linked, skips it otherwise, so this TU links with
// or without workstream B.
extern void mac68k_audio_service(void) __attribute__((weak));

// Single-shot file-log diagnostics: video refresh liveness + checksums.
void bmx_mac68k_video_diag(void);
// EmulOp trap hits (owned by third_party/basilisk2/emul_op.cpp).
extern unsigned long bmx_emulop_count;
extern unsigned int bmx_emulop_last;
extern unsigned long bmx_emulop_hist[0x40];
// Unmapped/hardware reads (owned by uae_cpu/memory.cpp dummy bank).
extern unsigned long bmx_dummy_reads;
extern unsigned int bmx_dummy_last_addr;
extern unsigned long bmx_dummy_writes;
extern unsigned int bmx_dummy_last_waddr;
extern unsigned int bmx_dummy_first_addrs[64];
extern unsigned int bmx_dummy_first_pcs[64];
extern unsigned char bmx_dummy_first_rw[64];
extern unsigned int bmx_dummy_first_n;
// First-N ADBOp/CLKNOMEM transactions (owned by emul_op.cpp).
struct bmx_adb_rec {
  unsigned op;
  unsigned char d[9];
};
extern struct bmx_adb_rec bmx_adb_recs[48];
extern unsigned int bmx_adb_n;
struct bmx_clk_rec {
  unsigned d1;
  unsigned d2;
};
extern struct bmx_clk_rec bmx_clk_recs[32];
extern unsigned int bmx_clk_n;
// Guest RAM for motion checksum (sysdeps-free spellings).
extern unsigned char *RAMBaseHost;
extern unsigned int RAMSize;

namespace {
// Fine-grained PC trace: last 128 guest PCs at 60Hz (plus d0/a0/d7/a3).
// The 1Hz sampler aliases tight loops; this ring reveals the exact cycle
// order and any sub-Hz register drift. Dumped at the 600-tick diag only.
uint32_t s_tr_pc[128];
uint32_t s_tr_d0[128];
uint32_t s_tr_a0[128];
uint32_t s_tr_d7[128];
uint32_t s_tr_a3[128];
unsigned s_tr_idx = 0;
}  // namespace

namespace {

// 60.15Hz tick interval in microseconds (mirrors main_unix.cpp one_tick).
const uint64_t kTickIntervalUsec = 16625;

// Seconds between the Mac epoch (1904-01-01) and the Unix epoch
// (1970-01-01); same constant TimeToMacTime() uses.
const uint32 kUnixToMacDiff = 2082844800u;
// Fixed BMX boot epoch: 2026-01-01 00:00 UTC (Unix time 1767225600).
const uint32 kBootUnixSec = 1767225600u;
const uint32 kBootMacSec = kBootUnixSec + kUnixToMacDiff;

bool s_in_tick = false;
bool s_first_tick_logged = false;
bool s_no_disc_logged = false;
uint64_t s_last_tick = 0;
unsigned s_tick_count = 0;
unsigned s_second_count = 0;

}  // namespace

// Countdown consumed by the UAE core (cpu_check_ticks in newcpu.h); the
// task specifies the 20000 reload value.
int32 emulated_ticks = 20000;

uint32 TimerDateTime(void) {
  uint64_t elapsed_sec = circle_get_ticks64() / 1000000ull;
  uint32 mac_time = kBootMacSec + (uint32)elapsed_sec;
  return mac_time;
}

void Microseconds(uint32 &hi, uint32 &lo) {
  // No RTC on BMX: uptime microseconds since boot (callers use this for
  // interval timing, which uptime satisfies).
  uint64_t us = circle_get_ticks64();
  hi = (uint32)(us >> 32);
  lo = (uint32)us;
}

void timer_current_time(tm_time_t &t) {
  uint64_t us = circle_get_ticks64();
  t.tv_sec = (long)(kBootUnixSec + us / 1000000ull);
  t.tv_usec = (long)(us % 1000000ull);
}

void timer_add_time(tm_time_t &res, tm_time_t a, tm_time_t b) {
  res.tv_sec = a.tv_sec + b.tv_sec;
  res.tv_usec = a.tv_usec + b.tv_usec;
  if (res.tv_usec >= 1000000) {
    res.tv_sec++;
    res.tv_usec -= 1000000;
  }
}

void timer_sub_time(tm_time_t &res, tm_time_t a, tm_time_t b) {
  res.tv_sec = a.tv_sec - b.tv_sec;
  res.tv_usec = a.tv_usec - b.tv_usec;
  if (res.tv_usec < 0) {
    res.tv_sec--;
    res.tv_usec += 1000000;
  }
}

int timer_cmp_time(tm_time_t a, tm_time_t b) {
  if (a.tv_sec != b.tv_sec) {
    return a.tv_sec < b.tv_sec ? -1 : 1;
  }
  if (a.tv_usec != b.tv_usec) {
    return a.tv_usec < b.tv_usec ? -1 : 1;
  }
  return 0;
}

void timer_mac2host_time(tm_time_t &res, int32 mactime) {
  res.tv_sec = (long)((uint32)mactime - kUnixToMacDiff);
  res.tv_usec = 0;
}

int32 timer_host2mac_time(tm_time_t hosttime) {
  return (int32)((uint32)hosttime.tv_sec + kUnixToMacDiff);
}

void cpu_do_check_ticks(void) {
  // Reload FIRST so nested checks cannot starve the countdown.
  emulated_ticks = 20000;
  if (s_in_tick) {
    return;
  }
  s_in_tick = true;

  if (!s_first_tick_logged) {
    s_first_tick_logged = true;
    s_last_tick = circle_get_ticks64();
    bmx::mac68k::debug_log(
        "mac68k: cpu tick hook first fire, emulated_ticks reloaded to 20000");
  }

  uint64_t now = circle_get_ticks64();
  if (now - s_last_tick >= kTickIntervalUsec) {
    s_last_tick = now;
    s_tick_count++;

    // 60Hz service (mirrors main_unix.cpp one_tick/write of VideoRefresh +
    // 60Hz flag gating: Classic ROMs only interrupt once the Mac started).
    // DIAG single-shot: record gate decision so one log proves whether
    // VideoRefresh runs while the guest spins with intmask=7.
    bool will_refresh =
        (ROMVersion != ROM_VERSION_CLASSIC || HasMacStarted());
    if (will_refresh) {
      SetInterruptFlag(INTFLAG_60HZ);
      // Guest video refresh; VideoInit/Refresh are owned by the video
      // workstream (strong reference: link requires that workstream).
      VideoRefresh();
    }
    // 60Hz PC trace (cheap loads + stores, no logging here).
    {
      unsigned i = s_tr_idx % 128;
      s_tr_pc[i] = (uint32_t)m68k_getpc();
      s_tr_d0[i] = (uint32_t)m68k_dreg(regs, 0);
      s_tr_a0[i] = (uint32_t)m68k_areg(regs, 0);
      s_tr_d7[i] = (uint32_t)m68k_dreg(regs, 7);
      s_tr_a3[i] = (uint32_t)m68k_areg(regs, 3);
      s_tr_idx++;
    }
    // Stall-loop entry detector + awaited-flag change watch (60Hz, cheap).
    // The IIci ROM parks in a closed poll loop (PCs 0x408429xx-0x408430xx
    // awaiting D7/lowmem bits only an interrupt or hardware could change).
    // Logging the ENTERING edge names the caller (which JMP armed D7/A3);
    // any flag change afterwards would prove the waiter can terminate.
    {
      uint32_t pc = (uint32_t)m68k_getpc();
      bool in_loop =
          (pc >= 0x40842900u && pc < 0x40843100u);
      static uint32_t s_prev_pc = 0;
      static bool s_prev_in = false;
      static unsigned s_entries = 0;
      if (in_loop && !s_prev_in) {
        s_entries++;
        if (s_entries <= 8) {
          bmx::mac68k::debug_log(
              "mac68k: loopenter #%u from=0x%08x d0=%08x d7=%08x a3=%08x "
              "lm2=%02x",
              s_entries, (unsigned)s_prev_pc,
              (unsigned)m68k_dreg(regs, 0), (unsigned)m68k_dreg(regs, 7),
              (unsigned)m68k_areg(regs, 3),
              (unsigned)(ReadMacInt8(2) & 0xff));
        }
      }
      s_prev_pc = pc;
      s_prev_in = in_loop;
      // Flag watch (in-loop only): D7/A3/D0/lowmem[2] frozen while
      // parked => provable deadlock; any change names the releaser.
      static uint32_t s_w_d7 = 0, s_w_a3 = 0, s_w_d0 = 0;
      static unsigned s_w_lm2 = 0;
      static bool s_w_init = false;
      static unsigned s_w_changes = 0;
      if (in_loop) {
        uint32_t d7 = (uint32_t)m68k_dreg(regs, 7);
        uint32_t a3 = (uint32_t)m68k_areg(regs, 3);
        uint32_t d0 = (uint32_t)m68k_dreg(regs, 0);
        unsigned lm2 = (unsigned)(ReadMacInt8(2) & 0xff);
        if (!s_w_init) {
          s_w_d7 = d7;
          s_w_a3 = a3;
          s_w_d0 = d0;
          s_w_lm2 = lm2;
          s_w_init = true;
          bmx::mac68k::debug_log(
              "mac68k: flagwatch armed d7=%08x a3=%08x d0=%08x lm2=%02x",
              (unsigned)d7, (unsigned)a3, (unsigned)d0, lm2);
        } else if (d7 != s_w_d7 || a3 != s_w_a3 || d0 != s_w_d0 ||
                   lm2 != s_w_lm2) {
          s_w_changes++;
          if (s_w_changes <= 12) {
            bmx::mac68k::debug_log(
                "mac68k: flagchg #%u d7=%08x->%08x a3=%08x->%08x "
                "d0=%08x->%08x lm2=%02x->%02x pc=0x%08x",
                s_w_changes, (unsigned)s_w_d7, (unsigned)d7,
                (unsigned)s_w_a3, (unsigned)a3, (unsigned)s_w_d0,
                (unsigned)d0, s_w_lm2, lm2, (unsigned)pc);
          }
          s_w_d7 = d7;
          s_w_a3 = a3;
          s_w_d0 = d0;
          s_w_lm2 = lm2;
        }
      }
    }
    if (s_tick_count % 600 == 0) {
      uint32 wlsc = ReadMacInt32(0xcfc);
      uaecptr pc0 = m68k_getpc();
      bmx::mac68k::debug_log(
          "mac68k: tickdiag ticks=%u ROM=0x%04x willRefresh=%d started=%d "
          "WLSC=0x%08x INTFLAGS=0x%08x intmask=%d vbl=0x%08x time=0x%08x "
          "pc=0x%08x emulops=%lu lastOp=0x%04x",
          s_tick_count, (unsigned)ROMVersion, will_refresh ? 1 : 0,
          HasMacStarted() ? 1 : 0, (unsigned)wlsc,
          (unsigned)InterruptFlags, (int)regs.intmask,
          (unsigned)get_long(0x160), (unsigned)get_long(0x20c),
          (unsigned)pc0, bmx_emulop_count, (unsigned)(bmx_emulop_last & 0xffffu));
      // Boot-stage histogram: exact trap sequence proves how far ROM boot
      // got. Nonzero idx:count pairs only (fits one 512B log line).
      // 03=RESET 04=CLKNOMEM 05/06=XPRAM 07=BOOTGLOBS 08=BOOTSTK 09=MEMSIZE
      // 0A=INSTALL 0B=SERD 0C-0F=SONY 10-13=DISK 14-17=CDROM 18-1A=VIDEO
      // 1B-1F=SERIAL 20-22=ETHER 23=ADBOP 24-26=TIME 27=MICROSEC 28=SCSI
      // 29=IRQ 2A/2B=SCRAP 2C=CHECKLOAD 2D=AUDIO 2F=BLOCKMOVE 30-34=SOUNDIN.
      {
        char hist[512];
        int n = snprintf(hist, sizeof(hist), "mac68k: emulhist total=%lu ",
                         bmx_emulop_count);
        for (int i = 0; i < 0x40 && n > 0 && (size_t)n < sizeof(hist) - 12; i++) {
          if (bmx_emulop_hist[i] != 0) {
            n += snprintf(hist + n, sizeof(hist) - (size_t)n, "%02x:%lu ",
                          i, bmx_emulop_hist[i]);
          }
        }
        bmx::mac68k::debug_log("%s", hist);
      }
      // Hardware-poll proof: unmapped reads (VIA/SCC/SCSI live in dummy).
      bmx::mac68k::debug_log("mac68k: dummyreads=%lu last=0x%08x writes=%lu "
                             "wlast=0x%08x",
                             bmx_dummy_reads, bmx_dummy_last_addr,
                             bmx_dummy_writes, bmx_dummy_last_waddr);
      // First-seen probe addresses (steering). First 20 fit one line.
      {
        char line[512];
        int n = snprintf(line, sizeof(line), "mac68k: firstprobes n=%u ",
                         bmx_dummy_first_n);
        unsigned lim =
            bmx_dummy_first_n < 20 ? bmx_dummy_first_n : 20;
        for (unsigned j = 0; j < lim && n > 0 && (size_t)n < sizeof(line) - 10;
             j++) {
          n += snprintf(line + n, sizeof(line) - (size_t)n, "%08x ",
                        bmx_dummy_first_addrs[j]);
        }
        bmx::mac68k::debug_log("%s", line);
      }
      // PC-tagged probes: WHICH guest instruction touched hardware (R/W).
      // Proves the stall loop itself is hardware-free (registers/lowmem).
      {
        char line[512];
        int n = snprintf(line, sizeof(line), "mac68k: probeat n=%u ",
                         bmx_dummy_first_n);
        unsigned lim = bmx_dummy_first_n < 16 ? bmx_dummy_first_n : 16;
        for (unsigned j = 0; j < lim && n > 0 && (size_t)n < sizeof(line) - 22;
             j++) {
          n += snprintf(line + n, sizeof(line) - (size_t)n, "%c%08x@%08x ",
                        bmx_dummy_first_rw[j] ? 'W' : 'R',
                        bmx_dummy_first_addrs[j], bmx_dummy_first_pcs[j]);
        }
        bmx::mac68k::debug_log("%s", line);
      }
      // ADBOp transactions (op + data): WHAT the ROM probed pre-stall.
      {
        char line[512];
        int n = snprintf(line, sizeof(line), "mac68k: adbops n=%u ",
                         bmx_adb_n);
        unsigned lim = bmx_adb_n < 12 ? bmx_adb_n : 12;
        for (unsigned j = 0; j < lim && n > 0 && (size_t)n < sizeof(line) - 30;
             j++) {
          n += snprintf(line + n, sizeof(line) - (size_t)n,
                        "%02x:%02x%02x%02x ",
                        (unsigned)(bmx_adb_recs[j].op & 0xff),
                        bmx_adb_recs[j].d[0], bmx_adb_recs[j].d[1],
                        bmx_adb_recs[j].d[2]);
        }
        bmx::mac68k::debug_log("%s", line);
      }
      // CLKNOMEM transactions (op in, result out): RTC/PRAM steering.
      {
        char line[512];
        int n = snprintf(line, sizeof(line), "mac68k: clkops n=%u ",
                         bmx_clk_n);
        unsigned lim = bmx_clk_n < 16 ? bmx_clk_n : 16;
        for (unsigned j = 0; j < lim && n > 0 && (size_t)n < sizeof(line) - 20;
             j++) {
          n += snprintf(line + n, sizeof(line) - (size_t)n, "%08x->%08x ",
                        bmx_clk_recs[j].d1, bmx_clk_recs[j].d2);
        }
        bmx::mac68k::debug_log("%s", line);
      }
      // Guest RAM motion checksum (sampled FNV): any hidden writes anywhere.
      {
        uint32_t ck = 2166136261u;
        if (RAMBaseHost != NULL && RAMSize != 0) {
          size_t n = (size_t)RAMSize;
          const uint8_t *p = (const uint8_t *)RAMBaseHost;
          for (size_t i = 0; i < n; i += 4096) {
            ck ^= p[i];
            ck *= 16777619u;
          }
          ck ^= (uint32_t)(n & 0xffffffffu);
        }
        bmx::mac68k::debug_log("mac68k: ramCk=%08x size=%u", ck, RAMSize);
      }
      // PC-trace digest: unique PCs in the last ~2s (the loop body) plus
      // d0/a0/d7/a3 drift. Proves static park vs slow crawl.
      {
        uint32_t uniq[48];
        unsigned nu = 0;
        uint32_t d0min = 0xffffffffu, d0max = 0, a0min = 0xffffffffu,
                 a0max = 0, d7min = 0xffffffffu, d7max = 0, a3min = 0xffffffffu,
                 a3max = 0;
        for (unsigned k = 0; k < 128; k++) {
          uint32_t p = s_tr_pc[k], d0 = s_tr_d0[k], a0 = s_tr_a0[k],
                   d7 = s_tr_d7[k], a3 = s_tr_a3[k];
          if (d0 < d0min) d0min = d0;
          if (d0 > d0max) d0max = d0;
          if (a0 < a0min) a0min = a0;
          if (a0 > a0max) a0max = a0;
          if (d7 < d7min) d7min = d7;
          if (d7 > d7max) d7max = d7;
          if (a3 < a3min) a3min = a3;
          if (a3 > a3max) a3max = a3;
          bool seen = false;
          for (unsigned j = 0; j < nu; j++) {
            if (uniq[j] == p) {
              seen = true;
              break;
            }
          }
          if (!seen && nu < 48) uniq[nu++] = p;
        }
        char line[512];
        int n = snprintf(line, sizeof(line),
                         "mac68k: trace uniq=%u d0=%08x/%08x a0=%08x/%08x "
                         "d7=%08x/%08x a3=%08x/%08x pcs=",
                         nu, d0min, d0max, a0min, a0max, d7min, d7max, a3min,
                         a3max);
        for (unsigned j = 0; j < nu && n > 0 && (size_t)n < sizeof(line) - 10;
             j++) {
          n += snprintf(line + n, sizeof(line) - (size_t)n, "%08x ",
                        uniq[j]);
        }
        bmx::mac68k::debug_log("%s", line);
      }
      // Hot-loop RAM dump: the polled flag lives in RAM (A0/A3). Contents
      // name the waiter without another run.
      {
        uaecptr a0 = m68k_areg(regs, 0);
        uaecptr a3 = m68k_areg(regs, 3);
        bmx::mac68k::debug_log(
            "mac68k: hotram a0=0x%08x @a0=%08x %08x a3=0x%08x @a3=%08x %08x "
            "ramFC00=%08x ramFD00=%08x",
            (unsigned)a0, (unsigned)get_long(a0), (unsigned)get_long(a0 + 4),
            (unsigned)a3, (unsigned)get_long(a3), (unsigned)get_long(a3 + 4),
            (unsigned)get_long(0xfc00), (unsigned)get_long(0xfd00));
      }
      // Stall prover: ROM words around the stuck PC — identifies the VIA/ADB/
      // SCSI poll without a second run. Guarded to ROM range.
      if (pc0 >= 0x40800000u && pc0 < 0x40880000u) {
        bmx::mac68k::debug_log(
            "mac68k: stall pc-8=%04x %04x pc=%04x %04x %04x %04x %04x %04x "
            "%04x %04x",
            (unsigned)(get_word(pc0 - 8) & 0xffffu),
            (unsigned)(get_word(pc0 - 6) & 0xffffu),
            (unsigned)(get_word(pc0 - 4) & 0xffffu),
            (unsigned)(get_word(pc0 - 2) & 0xffffu),
            (unsigned)(get_word(pc0) & 0xffffu),
            (unsigned)(get_word(pc0 + 2) & 0xffffu),
            (unsigned)(get_word(pc0 + 4) & 0xffffu),
            (unsigned)(get_word(pc0 + 6) & 0xffffu),
            (unsigned)(get_word(pc0 + 8) & 0xffffu),
            (unsigned)(get_word(pc0 + 10) & 0xffffu));
      }
      bmx_mac68k_video_diag();
    }

    // NOTE on SonyInterrupt/DiskInterrupt: Unix one_tick() never calls
    // them; the guest's 60Hz EMUL_OP handler (emul_op.cpp) already drives
    // both once the Mac has started. Calling them from the host tick as
    // well would risk double-completing deferred disk I/O, and drive
    // presence cannot be queried portably here, so the host tick leaves
    // them to the guest VBL path.
    if (!s_no_disc_logged) {
      s_no_disc_logged = true;
      bmx::mac68k::debug_log(
          "mac68k: cpu tick: Sony/DiskInterrupt left to guest VBL EMUL_OP "
          "(unix one_tick parity, no double-complete)");
    }

    // Audio drain when workstream B is linked, skipped otherwise.
    if (mac68k_audio_service != NULL) {
      mac68k_audio_service();
    }

    // Guest input drains (USB queues -> ADB). These also run in the
    // boot-fail/post-quit idle pumps, but while m68k_execute() blocks core1
    // this is the ONLY path that feeds the guest keyboard/mouse.
    bmx::mac68k::drain_guest_keys();
    bmx::mac68k::drain_guest_mouse();

    // F12 menu pump (see header comment above the decl). Unconditional:
    // ui_handle_toggle_or_quick_func() is a cheap flag check when idle.
    ui_handle_toggle_or_quick_func();

    // 1Hz service (mirrors main_unix.cpp one_second, minus the XPRAM
    // watchdog which needs no host timer here).
    s_second_count++;
    if (s_second_count >= 60) {
      s_second_count = 0;
      WriteMacInt32(0x20c, TimerDateTime());
      SetInterruptFlag(INTFLAG_1HZ);
      // DIAGNOSTIC (temporary): guest PC + opcode every host second, so
      // one boot yields a trajectory of where ROM init spins.
      uaecptr pc1 = m68k_getpc();
      bmx::mac68k::debug_log("mac68k: cpu tick: 1Hz update, mactime=0x%08x "
                             "pc=0x%08x op=0x%04x INTFLAGS=0x%08x started=%d "
                             "emulops=%lu",
                             (unsigned)TimerDateTime(), (unsigned)pc1,
                             (unsigned)(get_word(pc1) & 0xffffu),
                             (unsigned)InterruptFlags,
                             HasMacStarted() ? 1 : 0, bmx_emulop_count);
      // DIAGNOSTIC (temporary): when the PC sits in the ROM poll loop,
      // dump data/address registers to see WHAT is polled (which Mac
      // address the BTST/TST sequence tests). regs comes from newcpu.h.
      if (pc1 >= 0x40800000u && pc1 < 0x40880000u) {
        uaecptr sp = m68k_areg(regs, 7);
        bmx::mac68k::debug_log(
            "mac68k: regs d0=%08x d1=%08x d2=%08x d3=%08x a0=%08x a1=%08x "
            "a2=%08x a3=%08x a6=%08x sp=%08x stk=%08x %08x %08x %08x",
            (unsigned)m68k_dreg(regs, 0), (unsigned)m68k_dreg(regs, 1),
            (unsigned)m68k_dreg(regs, 2), (unsigned)m68k_dreg(regs, 3),
            (unsigned)m68k_areg(regs, 0), (unsigned)m68k_areg(regs, 1),
            (unsigned)m68k_areg(regs, 2), (unsigned)m68k_areg(regs, 3),
            (unsigned)m68k_areg(regs, 6), (unsigned)sp,
            (unsigned)get_long(sp), (unsigned)get_long(sp + 4),
            (unsigned)get_long(sp + 8), (unsigned)get_long(sp + 12));
        bmx::mac68k::debug_log(
            "mac68k: regs2 d4=%08x d5=%08x d6=%08x d7=%08x a4=%08x a5=%08x",
            (unsigned)m68k_dreg(regs, 4), (unsigned)m68k_dreg(regs, 5),
            (unsigned)m68k_dreg(regs, 6), (unsigned)m68k_dreg(regs, 7),
            (unsigned)m68k_areg(regs, 4), (unsigned)m68k_areg(regs, 5));
        // DIAGNOSTIC (temporary): interrupt mask + VBL counter. If the ROM
        // unmasked interrupts (intmask < 7) yet VBLCount never advances,
        // our 60Hz delivery never reaches the guest.
        bmx::mac68k::debug_log("mac68k: regs3 intmask=%d sr=0x%04x "
                               "vblcount=0x%08x time=0x%08x",
                               (int)regs.intmask, (unsigned)regs.sr,
                               (unsigned)get_long(0x160),
                               (unsigned)get_long(0x20c));
      }
    }

    // Match Unix one_tick/one_second: only raise the 68k interrupt when a
    // flag is actually pending (intlev() = InterruptFlags ? 1 : 0). Raising
    // SPCFLAG_INT with no flags just forces a pointless specialty check.
    if (InterruptFlags != 0) {
      TriggerInterrupt();
    }

    if (s_tick_count % 600 == 0) {
      bmx::mac68k::debug_log("mac68k: cpu tick heartbeat, ticks=%u",
                             s_tick_count);
    }
  }

  // F12 menu pump: the input drain filters F12 from the ADB stream and
  // raises the menu-request flag; run the common toggle handler on this
  // (emulator) thread so the menu opens while the guest runs.
  if (bmx::mac68k::mac68k_take_menu_request()) {
    bmx::mac68k::debug_log("mac68k: cpu tick: F12 menu request, pumping ui");
    ui_handle_toggle_or_quick_func();
  }

  // Reset requests (menu Soft/Hard Reset) arrive while m68k_execute()
  // blocks this thread, so the frame loop cannot see them until the guest
  // quits. Unwind the CPU here and hand the request back: the frame loop
  // takes it after m68k_execute returns and runs the full ExitAll +
  // boot_init reboot, applying staged model/ROM/RAM choices.
  {
    int reset_code = bmx::mac68k::mac68k_take_reset_request();
    if (reset_code != 0) {
      bmx::mac68k::debug_log("mac68k: cpu tick: reset request %d, unwinding "
                             "guest for reboot",
                             reset_code);
      bmx::mac68k::mac68k_request_reset(reset_code);
      mac68k_quit_requested = true;
      m68k_emulop_return();
    }
  }

  s_in_tick = false;
}
