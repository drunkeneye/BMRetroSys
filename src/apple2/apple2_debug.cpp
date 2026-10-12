//
// apple2_debug.cpp
//
// See apple2_debug.h. Bootrail design: every debug_log() line lands in a
// RAM ring first (always available, even before the SD volumes mount) and
// in the log file once debug_init() opens it. The ring is replayed into
// the file at init, so one boot captures the whole trail with no UART.
//

#include "apple2_debug.h"
#include "apple2_build_tag.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <circle/actled.h>

namespace bmx {
namespace apple2 {

namespace {

// 16KB RAM ring: ~100 lines of history, no heap, no FS, safe anywhere.
const unsigned kRailSize = 16 * 1024;
char s_rail[kRailSize];
unsigned s_rail_head = 0;
unsigned long s_rail_seq = 0;

FILE *s_log = NULL;

void sync_log(void) {
  if (s_log == NULL) {
    return;
  }
  fflush(s_log);
  fsync(fileno(s_log));
}

void rail_append(const char *line) {
  // Format: "#<seq> <line>\n", truncated to fit; ring overwrites oldest.
  char tmp[544];
  int n = snprintf(tmp, sizeof(tmp), "#%lu %s\n", s_rail_seq, line);
  if (n <= 0) {
    return;
  }
  s_rail_seq++;
  if (n >= (int)sizeof(tmp)) {
    n = (int)sizeof(tmp) - 1;
    tmp[n - 1] = '\n';
  }
  for (int i = 0; i < n; i++) {
    s_rail[s_rail_head] = tmp[i];
    s_rail_head = (s_rail_head + 1) % kRailSize;
  }
}

void rail_replay(void) {
  if (s_log == NULL) {
    return;
  }
  // Oldest-first: scan for the first newline (end of oldest partial line)
  // after head, then dump to the next wrap.
  unsigned start = s_rail_head;
  if (s_rail_seq > 0) {
    // Find oldest complete line: walk forward to first '\n'.
    for (unsigned i = 0; i < kRailSize; i++) {
      unsigned idx = (s_rail_head + i) % kRailSize;
      if (s_rail[idx] == '\n') {
        start = (idx + 1) % kRailSize;
        break;
      }
      if (s_rail[idx] == '\0') {
        // Never-wrapped region: content starts at 0.
        start = 0;
        break;
      }
    }
  }
  fprintf(s_log, "--- bootrail replay (%lu lines, oldest first) ---\n",
          s_rail_seq);
  if (start == 0 && s_rail[0] == '\0') {
    fprintf(s_log, "(rail empty)\n");
  } else {
    unsigned idx = start;
    for (unsigned i = 0; i < kRailSize; i++) {
      char c = s_rail[idx];
      if (c == '\0') {
        break;
      }
      fputc(c, s_log);
      idx = (idx + 1) % kRailSize;
      if (idx == start) {
        break;
      }
    }
  }
  fprintf(s_log, "--- end replay, live logging follows ---\n");
  sync_log();
}

}  // namespace

void debug_init(void) {
  static const char *candidates[] = {
      "SYS:/bmretrosys-apple2.log",
      "SD:/bmretrosys-apple2.log",
      "USER:/bmretrosys-apple2.log",
      "/bmretrosys-apple2.log",
  };
  for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
    FILE *fp = fopen(candidates[i], "w");
    if (fp != NULL) {
      s_log = fp;
      printf("APPLE2: debug log -> %s\n", candidates[i]);
      fprintf(s_log, "BMRetroSys apple2 debug log\n");
      fprintf(s_log, "BUILD TAG: %s\n", APPLE2_BUILD_TAG);
      fflush(s_log);
      rail_replay();
      return;
    }
  }
  printf("APPLE2: no writable volume for debug log, printf-only\n");
}

void debug_log(const char *fmt, ...) {
  char line[512];
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);

  rail_append(line);
  // File only: no UART logging on this kernel.
  if (s_log != NULL) {
    fprintf(s_log, "%s\n", line);
    sync_log();
  }
}

void bootrail_blink(int n) {
  if (n <= 0 || n > 9) {
    return;
  }
  CActLED *led = CActLED::Get();
  if (led == NULL) {
    return;
  }
  led->Blink((unsigned)n, 80, 120);
}

void debug_dump_core_log(void) { sync_log(); }

void debug_close(void) {
  if (s_log != NULL) {
    fclose(s_log);
    s_log = NULL;
  }
}

}  // namespace apple2
}  // namespace bmx

extern "C" void apple2_debug_log(const char *fmt, ...) {
  char line[512];
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  ::bmx::apple2::debug_log("%s", line);
}
