//
// a2600_config.cpp
//
// See a2600_config.h.
//

#include "a2600_config.h"
#include "a2600_debug.h"

#include <string.h>

namespace bmx {
namespace a2600 {

namespace {

bool s_ready = false;
bool s_ntsc = false;
int s_volume = 100;
bool s_left_a = true;
bool s_right_a = true;

}  // namespace

bool config_init(const Config &config) {
  s_ntsc = false;
  if (config.tv_mode != NULL && strstr(config.tv_mode, "ntsc") != NULL) {
    s_ntsc = true;
  }
  debug_log("config_init: tv_mode=%s", config.tv_mode != NULL ? config.tv_mode : "(null)");
  s_ready = true;
  return true;
}

bool config_is_ready(void) { return s_ready; }
bool config_is_ntsc(void) { return s_ntsc; }

int config_get_volume(void) { return s_volume; }

void config_set_volume(int pct) {
  if (pct < 0) {
    pct = 0;
  }
  if (pct > 300) {
    pct = 300;
  }
  s_volume = pct;
}

bool config_get_left_difficulty_a(void) { return s_left_a; }
bool config_get_right_difficulty_a(void) { return s_right_a; }
void config_set_left_difficulty_a(bool a) { s_left_a = a; }
void config_set_right_difficulty_a(bool a) { s_right_a = a; }

}  // namespace a2600
}  // namespace bmx
