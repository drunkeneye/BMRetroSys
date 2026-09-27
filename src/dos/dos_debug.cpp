//
// dos_debug.cpp
//
// SD-card debug log for the BMX DOSBox-X machine (bmretrosys-dos.log).
// Mirrors atari800_debug.cpp with two differences for bring-up:
//  - lazy open: every debug_log() retries fopen until it succeeds, so the
//    FIRST log call after the FS is ready creates the file no matter which
//    core/stage emits it (core-0 constructor through frame loop);
//  - file only: no printf/UART (not wired on this setup).
// Every line is fsync'd, so the tail always shows where a hang stopped.
//

#include "dos_debug.h"
#include "dos_build_tag.h"

#include <circle/actled.h>
#include <circle/timer.h>

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace bmx {
namespace dos {

namespace {

FILE *s_log = NULL;
unsigned long s_boot_us = 0;
unsigned s_open_attempts = 0;
bool s_runtime_quiet = false;

}  // namespace

void debug_set_runtime_quiet(bool quiet) {
  s_runtime_quiet = quiet;
}

// Guest-sink filter: after boot, drop everything except error markers.
bool debug_guest_pass(const char *body) {
  if (!s_runtime_quiet) {
    return true;
  }
  if (body == NULL) {
    return false;
  }
  return strstr(body, "E_Exit") != NULL ||
         strstr(body, "TERMINATE") != NULL ||
         strstr(body, "ERROR") != NULL || strstr(body, "failed") != NULL ||
         strstr(body, "FAILED") != NULL;
}

// fflush() only reaches the FatFS cache; without fsync the directory entry
// keeps size 0 and a power cut loses everything. Sync per line: the log
// volume is tiny and every line must survive a hard hang/power-off.
void sync_log(void) {
  if (s_log == NULL) {
    return;
  }
  fflush(s_log);
  fsync(fileno(s_log));
}

unsigned long now_us(void) {
  if (s_boot_us == 0) {
    return 0;
  }
  return CTimer::GetClockTicks() - s_boot_us;
}

// Called on every log line until the file exists: creates bmretrosys-dos.log
// at the first opportunity (FS ready), wherever execution currently is.
void ensure_open(void) {
  if (s_log != NULL) {
    return;
  }
  if (s_boot_us == 0) {
    s_boot_us = CTimer::GetClockTicks();
  }
  static const char *candidates[] = {
      "SYS:/bmretrosys-dos.log",
      "SD:/bmretrosys-dos.log",
      "USER:/bmretrosys-dos.log",
      "/bmretrosys-dos.log",
  };
  s_open_attempts++;
  for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
    FILE *fp = fopen(candidates[i], "w");
    if (fp != NULL) {
      s_log = fp;
      fprintf(s_log, "BMRetroSys dos debug log\n");
      fprintf(s_log, "BUILD TAG: " DOS_BUILD_TAG "\n");
      fprintf(s_log, "log opened after %u attempts: %s\n", s_open_attempts,
              candidates[i]);
      sync_log();
      return;
    }
  }
}

void debug_init(void) {
  ensure_open();
  debug_log("debug_init: explicit init (build " DOS_BUILD_TAG ")");
}

void debug_log(const char *fmt, ...) {
  ensure_open();
  if (s_log == NULL) {
    return;
  }
  char body[512];
  va_list args;
  va_start(args, fmt);
  vsnprintf(body, sizeof(body), fmt, args);
  va_end(args);

  fprintf(s_log, "[+%10lu us] %s\n", now_us(), body);
  sync_log();
}

void debug_dump_core_log(void) {
  // No DOSBox-X core log buffer yet (Phase 1.2 wires it); marker keeps the
  // call sites meaningful in the trace.
  debug_log("debug_dump_core_log: no core log yet");
}

void debug_close(void) {
  if (s_log != NULL) {
    fprintf(s_log, "--- log closed ---\n");
    fclose(s_log);
    s_log = NULL;
  }
}

// No-FS diagnostics: blink the ACT LED `stage` times (150ms on/250ms off)
// then a 1.2s pause so consecutive codes are separable. Codes >= 10 are
// single LONG blinks (stage-10 ms on) so they stay countable (10 fast
// blinks blur into a steady light). Works from the very first dos
// instruction (constructor) with no FS, no UART, no video.
// Count the blinks in each group after the hang.
void debug_blink(unsigned stage) {
  CActLED *led = CActLED::Get();
  if (led == NULL) {
    return;
  }
  if (stage < 1) {
    stage = 1;
  }
  if (stage > 15) {
    stage = 15;
  }
  if (stage >= 10) {
    led->Blink(1, 200 + 100 * stage, 300);
  } else {
    led->Blink(stage, 150, 250);
  }
  CTimer::SimpleMsDelay(1200);
}

// Bring-up beacon: 3x (1s on, 1s off), ~6s total. Unmistakable by design:
// if this is not seen, dos code never runs at all.
void debug_blink_boot(void) {
  CActLED *led = CActLED::Get();
  if (led == NULL) {
    return;
  }
  led->Blink(3, 1000, 1000);
}

// Liveness pulse from the frame loop (double-blink, distinct from stages).
void debug_heartbeat(void) {
  CActLED *led = CActLED::Get();
  if (led != NULL) {
    led->Blink(2, 80, 140);
  }
}

}  // namespace dos
}  // namespace bmx

// Wall-clock for the DOSBox-X tick loop (bmx_sdl_stubs.h SDL_GetTicks /
// SDL_Delay). CTimer ticks are microseconds; SDL wants milliseconds.
extern "C" uint32_t bmx_host_ticks_ms(void) {
  return (uint32_t)(CTimer::GetClockTicks() / 1000u);
}

extern "C" void bmx_host_delay_ms(uint32_t ms) {
  if (ms != 0) {
    CTimer::SimpleMsDelay(ms);
  }
}

// Host trace sink for the shared Atarimania client
// (src/update/atarimania_client.cpp references this weakly; other kernels
// leave it null). Pre-formatted with a power-on timestamp.
extern "C" void netcheck_host_trace(const char *line) {
  bmx::dos::debug_log("networktest: %s", line != NULL ? line : "?");
}
