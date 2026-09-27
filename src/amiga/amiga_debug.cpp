//
// amiga_debug.cpp
//
// See amiga_debug.h (clone of atarist_debug.cpp, log name changed).
//

#include "amiga_debug.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

namespace bmx {
namespace amiga {

// All logging removed (see below): no state needed.

void debug_init(void) {
  // Logging removed: no file is created.
}

void debug_log(const char *fmt, ...) {
  // Logging removed: silent.
  (void)fmt;
}

void debug_dump_core_log(void) {}

void debug_close(void) {
  // Logging removed: nothing open.
}

}  // namespace amiga
}  // namespace bmx

extern "C" void amiga_log_line(const char *line) {
  bmx::amiga::debug_log("%s", line != NULL ? line : "?");
}
