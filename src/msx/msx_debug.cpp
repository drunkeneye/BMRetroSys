//
// msx_debug.cpp
//
// See msx_debug.h.
//

#include "msx_debug.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace bmx {
namespace msx {

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
      "SYS:/bmretrosys-msx.log",
      "SD:/bmretrosys-msx.log",
      "USER:/bmretrosys-msx.log",
      "/bmretrosys-msx.log",
  };
  for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
    FILE *fp = fopen(candidates[i], "w");
    if (fp != NULL) {
      s_log = fp;
      printf("MSX: debug log -> %s\n", candidates[i]);
      fprintf(s_log, "BMRetroSys msx debug log\n");
      fprintf(s_log, "BUILD TAG: " __DATE__ " " __TIME__ "\n");
      fflush(s_log);
      fsync(fileno(s_log));
      return;
    }
  }
  printf("MSX: no writable volume for debug log, printf-only\n");
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
  }
}

void debug_dump_core_log(void) {}

void debug_close(void) {
  if (s_log != NULL) {
    fclose(s_log);
    s_log = NULL;
  }
}

}  // namespace msx
}  // namespace bmx

extern "C" void msx_debug_log(const char *fmt, ...) {
  char line[512];
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);
  bmx::msx::debug_log("%s", line);
}
