//
// msx_config.cpp
//
// MSX configuration: TV mode latch + /roms/MSX pool scan.
//

#include "msx_config.h"
#include "msx_debug.h"

#include <dirent.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

namespace bmx {
namespace msx {

namespace {

bool s_ready = false;
bool s_ntsc = false;

char s_rom_names[32][64];
char s_rom_paths[32][256];
int s_rom_count = 0;

void scan_pool(void) {
  s_rom_count = 0;
  DIR *dp = opendir("/roms/MSX");
  if (dp == nullptr) {
    dp = opendir("SD:/roms/MSX");
  }
  if (dp == nullptr) {
    return;
  }
  struct dirent *ep;
  while ((ep = readdir(dp)) != nullptr && s_rom_count < 32) {
    if (ep->d_name[0] == '.') {
      continue;
    }
    strncpy(s_rom_names[s_rom_count], ep->d_name,
            sizeof(s_rom_names[0]) - 1);
    s_rom_names[s_rom_count][sizeof(s_rom_names[0]) - 1] = '\0';
    snprintf(s_rom_paths[s_rom_count], sizeof(s_rom_paths[0]), "/roms/MSX/%s",
             ep->d_name);
    s_rom_count++;
  }
  closedir(dp);
}

}  // namespace

bool config_init(const Config &config) {
  s_ntsc = false;
  if (config.tv_mode != nullptr && strstr(config.tv_mode, "ntsc") != nullptr) {
    s_ntsc = true;
  }
  scan_pool();
  debug_log("config_init: tv_mode=%s pool_roms=%d",
            config.tv_mode != NULL ? config.tv_mode : "(null)", s_rom_count);
  s_ready = true;
  return true;
}

bool config_is_ntsc(void) { return s_ntsc; }
bool config_is_ready(void) { return s_ready; }

int config_rom_count(void) { return s_rom_count; }

const char *config_rom_name(int index) {
  if (index < 0 || index >= s_rom_count) {
    return "";
  }
  return s_rom_names[index];
}

void config_rom_path(int index, char *out, size_t out_size) {
  if (out == nullptr || out_size == 0) {
    return;
  }
  out[0] = '\0';
  if (index < 0 || index >= s_rom_count) {
    return;
  }
  strncpy(out, s_rom_paths[index], out_size - 1);
  out[out_size - 1] = '\0';
}

void config_rescan_roms(void) {
  scan_pool();
  debug_log("config: ROM pool rescanned, %d files", s_rom_count);
}

}  // namespace msx
}  // namespace bmx
