//
// dos_config.cpp
//
// DOSBox-X core configuration for BMX bare-metal (Phase 0 placeholder)
//

#include "dos_config.h"
#include "dos_debug.h"

#include <stddef.h>
#include <string.h>

namespace bmx {
namespace dos {
namespace {

bool s_ready = false;
bool s_ntsc = true;

}  // namespace

bool config_init(const Config &config) {
  debug_log("config_init: enter tv_mode=%s",
            config.tv_mode != NULL ? config.tv_mode : "(null)");
  s_ntsc = true;
  if (config.tv_mode != NULL && strstr(config.tv_mode, "pal") != NULL) {
    s_ntsc = false;
  }
  s_ready = true;
  debug_log("config_init: OK ntsc=%d", (int)s_ntsc);
  return true;
}

bool config_is_ntsc(void) {
  return s_ntsc;
}

bool config_is_ready(void) {
  return s_ready;
}

}  // namespace dos
}  // namespace bmx
