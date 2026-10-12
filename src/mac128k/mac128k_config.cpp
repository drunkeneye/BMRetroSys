//
// mac128k_config.cpp
//
// Mac 128K ROM pool scan + staging.
//

#include "mac128k_config.h"

#include "mac128k_debug.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>

namespace bmx {
namespace mac128k {

namespace {

const char *kRomDir = "/roms/MAC128K";

bool s_ready = false;

const int kMaxRoms = 16;
char s_rom_paths[kMaxRoms][256];
int s_rom_count = 0;
bool s_rom_scanned = false;
int s_rom_choice = -1;

bool str_has_suffix_ci(const char *name, const char *suffix) {
  size_t nl = strlen(name);
  size_t sl = strlen(suffix);
  if (nl < sl) {
    return false;
  }
  const char *tail = name + nl - sl;
  for (size_t i = 0; i < sl; i++) {
    char a = tail[i];
    char b = suffix[i];
    if (a >= 'A' && a <= 'Z') {
      a = (char)(a - 'A' + 'a');
    }
    if (b >= 'A' && b <= 'Z') {
      b = (char)(b - 'A' + 'a');
    }
    if (a != b) {
      return false;
    }
  }
  return true;
}

// Sidecar files that are never ROMs.
bool rom_name_banned(const char *name) {
  if (str_has_suffix_ci(name, ".txt") ||
      str_has_suffix_ci(name, ".log") ||
      str_has_suffix_ci(name, ".prefs")) {
    return true;
  }
  return false;
}

void rom_scan_once(void) {
  if (s_rom_scanned) {
    return;
  }
  s_rom_scanned = true;
  s_rom_count = 0;
  debug_log("mac128k: config: scanning ROM dir %s", kRomDir);
  DIR *dir = opendir(kRomDir);
  if (dir == NULL) {
    debug_log("mac128k: config: cannot open ROM dir %s", kRomDir);
    return;
  }
  struct dirent *ent;
  while (s_rom_count < kMaxRoms && (ent = readdir(dir)) != NULL) {
    const char *name = ent->d_name;
    if (name[0] == '.') {
      continue;
    }
    if (rom_name_banned(name)) {
      debug_log("mac128k: config: ROM scan skipping %s", name);
      continue;
    }
    snprintf(s_rom_paths[s_rom_count], sizeof(s_rom_paths[0]), "%s/%s",
             kRomDir, name);
    debug_log("mac128k: config: ROM scan candidate %s",
              s_rom_paths[s_rom_count]);
    s_rom_count++;
  }
  closedir(dir);
  for (int i = 1; i < s_rom_count; i++) {
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s", s_rom_paths[i]);
    int j = i - 1;
    while (j >= 0 && strcmp(s_rom_paths[j], tmp) > 0) {
      snprintf(s_rom_paths[j + 1], sizeof(s_rom_paths[j + 1]), "%s",
               s_rom_paths[j]);
      j--;
    }
    snprintf(s_rom_paths[j + 1], sizeof(s_rom_paths[j + 1]), "%s", tmp);
  }
  debug_log("mac128k: config: ROM scan found %d file(s)", s_rom_count);
  // Default: prefer Mac128K.ROM, else first file.
  s_rom_choice = -1;
  for (int i = 0; i < s_rom_count; i++) {
    const char *slash = strrchr(s_rom_paths[i], '/');
    const char *base = (slash != NULL) ? slash + 1 : s_rom_paths[i];
    if (strcmp(base, "Mac128K.ROM") == 0) {
      s_rom_choice = i;
      break;
    }
  }
  if (s_rom_choice < 0 && s_rom_count > 0) {
    s_rom_choice = 0;
  }
}

const char *rom_basename(const char *path) {
  const char *slash = strrchr(path, '/');
  return (slash != NULL && slash[1] != '\0') ? slash + 1 : path;
}

}  // namespace

bool config_init(const Config &config) {
  (void)config;
  rom_scan_once();
  s_ready = true;
  return true;
}

bool config_is_ready(void) {
  return s_ready;
}

int mac128k_rom_count(void) {
  rom_scan_once();
  return s_rom_count;
}

const char *mac128k_rom_name(int i) {
  rom_scan_once();
  if (i < 0 || i >= s_rom_count) {
    return nullptr;
  }
  return rom_basename(s_rom_paths[i]);
}

const char *mac128k_rom_path(int i) {
  rom_scan_once();
  if (i < 0 || i >= s_rom_count) {
    return nullptr;
  }
  return s_rom_paths[i];
}

void mac128k_rom_set(int i) {
  rom_scan_once();
  if (i < 0 || i >= s_rom_count) {
    return;
  }
  s_rom_choice = i;
  debug_log("mac128k: config: ROM set to %s (takes effect after Reset)",
            s_rom_paths[i]);
}

const char *mac128k_rom_staged_path(void) {
  if (s_rom_choice < 0 || s_rom_choice >= s_rom_count) {
    return nullptr;
  }
  return s_rom_paths[s_rom_choice];
}

int mac128k_rom_current(void) {
  rom_scan_once();
  if (s_rom_choice < 0 || s_rom_choice >= s_rom_count) {
    return 0;
  }
  return s_rom_choice;
}

}  // namespace mac128k
}  // namespace bmx
