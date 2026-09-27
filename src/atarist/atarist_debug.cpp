//
// atarist_debug.cpp
//
// See atarist_debug.h.
//

#include "atarist_debug.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace bmx {
namespace atarist {

namespace {

FILE *s_log = NULL;

// fflush() only reaches the FatFS cache; without fsync() the directory
// entry keeps size 0 and a power cut loses everything. Sync per line: the
// log volume is tiny and every line must survive a hard power-off.
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
      "SYS:/bmretrosys-atarist.log",
      "SD:/bmretrosys-atarist.log",
      "USER:/bmretrosys-atarist.log",
      "/bmretrosys-atarist.log",
  };
  for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
    FILE *fp = fopen(candidates[i], "w");
    if (fp != NULL) {
      s_log = fp;
      printf("AtariST: debug log -> %s\n", candidates[i]);
      fprintf(s_log, "BMRetroSys atarist debug log\n");
      fprintf(s_log, "BUILD TAG: " __DATE__ " " __TIME__ "\n");
      fflush(s_log);
      fsync(fileno(s_log));
      return;
    }
  }
  printf("AtariST: no writable volume for debug log, printf-only\n");
}

void debug_log(const char *fmt, ...) {
  char line[512];
  va_list args;
  va_start(args, fmt);
  vsnprintf(line, sizeof(line), fmt, args);
  va_end(args);

  printf("%s\n", line);
  if (s_log != NULL) {
    fprintf(s_log, "%s\n", line);
    sync_log();
  }
}

void debug_dump_core_log(void) {
  // The vendored core logs via Log_Printf (stderr on BMX); nothing buffered.
}

void debug_close(void) {
  if (s_log != NULL) {
    fclose(s_log);
    s_log = NULL;
  }
}

}  // namespace atarist
}  // namespace bmx

// C-linkage line sink for the plain-C backend providers
// (atarist_bmx_*.c cannot use the C++ namespace API directly).
// All diagnostics go to the log file (and serial); providers must never
// printf() on their own (no UART-only output).
extern "C" void atarist_log_line(const char *line) {
  bmx::atarist::debug_log("%s", line != NULL ? line : "?");
}
