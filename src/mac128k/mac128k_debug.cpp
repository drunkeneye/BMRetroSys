//
// mac128k_debug.cpp
//
// Debug sink for the BMX Mac128k machine.
//
// DESIGN (bring-up: the kernel used to die with zero output):
// every debug_log() line is captured ALWAYS, from the very first call
// (core0 constructor, pre-mount, pre-UART) into a 512 KiB RAM buffer.
// debug_init() opens bmretrosys-mac128k.log on SD, dumps the whole buffer,
// then keeps writing live. Every line is fsynced, so even a hard hang
// keeps everything up to the hang. No UART needed, ever.
//
// Rules this file obeys (learned the hard way):
// - debug_log() NEVER touches the filesystem (buffer + printf only).
// - Only debug_init() calls fopen(), and only on core1 post-mount.
// - No Circle headers except actled/timer (needed for blinks/timestamps);
//   in particular this TU must stay sysdeps-free.

#include "mac128k_debug.h"

#include <circle/actled.h>
#include <circle/timer.h>

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace bmx {
namespace mac128k {

namespace {

// 512 KiB linear capture buffer (~4000 log lines). Static BSS, zero-init,
// no constructor, no heap: safe from the very first instruction.
static const size_t kBufSize = 512u * 1024u;
static char s_buf[512u * 1024u];
static size_t s_buf_used = 0;
static bool s_buf_full_warned = false;

FILE *s_log = NULL;

unsigned long now_sec(void) {
  CTimer *timer = CTimer::Get();
  if (timer == nullptr) {
    return 0;
  }
  return timer->GetClockTicks() / 1000000;
}

void sync_log(void) {
  if (s_log == NULL) {
    return;
  }
  fflush(s_log);
  fsync(fileno(s_log));
}

// Formats one line as "[<sec>] <msg>". Returns length (without NUL).
int format_line(char *out, size_t out_size, const char *fmt, va_list ap) {
  int n = snprintf(out, out_size, "[%lu] ", now_sec());
  if (n < 0) {
    return 0;
  }
  size_t used = (size_t)n < out_size ? (size_t)n : out_size;
  int m = vsnprintf(out + used, out_size - used, fmt, ap);
  if (m < 0) {
    out[used] = '\0';
    return (int)used;
  }
  size_t total = used + (size_t)m;
  if (total >= out_size) {
    total = out_size - 1;
  }
  return (int)total;
}

void buf_append(const char *line, int len) {
  if (len <= 0) {
    return;
  }
  // +1 for '\n'. When full: stop capturing (but keep the file live).
  if (s_buf_used + (size_t)len + 1 > kBufSize) {
    if (!s_buf_full_warned) {
      s_buf_full_warned = true;
      const char *warn = "[0] LOG BUFFER FULL, oldest lines kept\n";
      printf("mac128k: %s", warn);
      if (s_log != NULL) {
        fputs(warn, s_log);
        sync_log();
      }
    }
    return;
  }
  memcpy(s_buf + s_buf_used, line, (size_t)len);
  s_buf_used += (size_t)len;
  s_buf[s_buf_used++] = '\n';
}

}  // namespace

void debug_init(void) {
  // Idempotent: first call opens (or retries when no volume was ready),
  // later calls are cheap no-ops so core0, Init() and core1 can all call.
  if (s_log != NULL) {
    return;
  }
  static const char *candidates[] = {
      "SYS:/bmretrosys-mac128k.log",
      "SD:/bmretrosys-mac128k.log",
      "USER:/bmretrosys-mac128k.log",
      "/bmretrosys-mac128k.log",
  };
  for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
    FILE *fp = fopen(candidates[i], "w");
    if (fp != NULL) {
      s_log = fp;
      printf("mac128k: debug log -> %s\n", candidates[i]);
      fprintf(s_log, "BMRetroSys mac128k debug log\n");
      fprintf(s_log, "BUILD TAG: " __DATE__ " " __TIME__ "\n");
      // Flush everything captured since moment zero.
      if (s_buf_used > 0) {
        fprintf(s_log, "--- %lu pre-open buffered bytes ---\n",
                (unsigned long)s_buf_used);
        fwrite(s_buf, 1, s_buf_used, s_log);
        fprintf(s_log, "--- end of pre-open buffer ---\n");
      }
      sync_log();
      return;
    }
  }
  printf("mac128k: no writable volume for debug log, buffer+printf only\n");
}

void debug_log(const char *fmt, ...) {
  if (fmt == nullptr) {
    return;
  }
  char line[512];
  va_list ap;
  va_start(ap, fmt);
  int len = format_line(line, sizeof(line), fmt, ap);
  va_end(ap);
  // ALWAYS captured, even pre-open: this is the whole point.
  buf_append(line, len);
  if (s_log != NULL) {
    fwrite(line, 1, (size_t)len, s_log);
    fputc('\n', s_log);
    sync_log();
  }
  printf("mac128k: %s\n", line);
}

void debug_dump_core_log(void) {}

// No-FS diagnostics: blink the ACT LED `stage` times, then a pause so
// consecutive codes stay separable (dos_debug.cpp pattern). Count the
// blinks after a hang to see how far boot got.
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

// Bring-up beacon (~6s). Call it once when nothing else explains a hang:
// if this is not seen, mac128k code never runs at all.
void debug_blink_boot(void) {
  CActLED *led = CActLED::Get();
  if (led == NULL) {
    return;
  }
  led->Blink(3, 1000, 1000);
}

void debug_heartbeat(void) {
  CActLED *led = CActLED::Get();
  if (led != NULL) {
    led->Blink(2, 80, 140);
  }
}

void debug_close(void) {
  if (s_log != NULL) {
    fprintf(s_log, "--- log closed ---\n");
    fclose(s_log);
    s_log = NULL;
  }
}

}  // namespace mac128k
}  // namespace bmx

// C entry for the Mini vMac backend (pure C TU): single-line messages,
// no formatting (the backend pre-formats where needed).
extern "C" void bmx_mac128k_debug_log(const char *s) {
  bmx::mac128k::debug_log("%s", s != nullptr ? s : "(null)");
}
