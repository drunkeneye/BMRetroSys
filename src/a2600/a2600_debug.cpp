//
// a2600_debug.cpp
//
// See a2600_debug.h. File-only log sink for the 2600 kernel (MSX pattern).
//

#include "a2600_debug.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace bmx {
namespace a2600 {

namespace {

FILE *s_log = NULL;

void sync_log(void) {
  if (s_log == NULL) {
    return;
  }
  fflush(s_log);
  fsync(fileno(s_log));
}

}  // namespace

void debug_init(void) {
  static const char *candidates[] = {
      "SYS:/bmretrosys-a2600.log",
      "SD:/bmretrosys-a2600.log",
      "USER:/bmretrosys-a2600.log",
      "/bmretrosys-a2600.log",
  };
  for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
    FILE *fp = fopen(candidates[i], "w");
    if (fp != NULL) {
      s_log = fp;
      printf("A2600: debug log -> %s\n", candidates[i]);
      fprintf(s_log, "BMRetroSys a2600 debug log\n");
      fprintf(s_log, "BUILD TAG: " __DATE__ " " __TIME__ "\n");
      sync_log();
      return;
    }
  }
  printf("A2600: no writable volume for debug log, printf-only\n");
}

void debug_log(const char *fmt, ...) {
  char line[512];
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);

  // File only: no UART logging on this kernel.
  if (s_log != NULL) {
    fprintf(s_log, "%s\n", line);
    sync_log();
  } else {
    printf("A2600: %s\n", line);
  }
}

void debug_dump_core_log(void) {}

}  // namespace a2600
}  // namespace bmx
