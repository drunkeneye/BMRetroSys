//
// mac68k_config.cpp
//
// Basilisk II core configuration for BMX bare-metal (Phase 0 placeholder).
//

#include "sysdeps.h"

#include "mac68k_config.h"

#include "mac68k_debug.h"

#include "prefs.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>

namespace bmx {
namespace mac68k {

namespace {

// ROM pool location on SD (user-supplied Mac ROM, never in git).
const char *kRomDir = "/roms/MAC68K";

struct ModelEntry {
  int id;
  const char *name;
  // Required ROM file (basename in /roms/MAC68K). The model owns its ROM:
  // there is no separate ROM selector, so a model/ROM mismatch is
  // impossible by construction.
  const char *rom;
};

// Placeholder model IDs; the real Basilisk II model IDs land with the
// upstream wiring. Mac IIci (id 5) is the default.
const ModelEntry kModels[] = {
  {5, "Mac IIci", "mac_iici.rom"},
  {6, "Mac Quadra 605", "mac_q605.rom"},
  {7, "Mac Quadra 650", "mac_q650.rom"},
  {8, "Mac Classic II", "mac_classic2.rom"},
  {9, "Mac Quadra 800", "mac_q800.rom"},
};

bool s_ready = false;
bool s_ntsc = true;
int s_ramsize_mb = 8;
int s_modelid = 5;
int s_cpu = 3;
int s_model_index = 0;

// Guest RAM menu options (MB). Index 0 (8MB) is the default.
const int kRamOptions[] = {8, 16, 32};
int s_ram_index = 0;

// ROM pool scan state (/roms/MAC68K, user-supplied, never in git).
const int kMaxRoms = 16;
char s_rom_paths[kMaxRoms][256];
int s_rom_count = 0;
bool s_rom_scanned = false;
int s_rom_choice = -1;

// Model availability: filtered kModels positions whose ROM file is present.
// Rebuilt by rom_scan_once; models without their ROM are hidden.
const int kMaxModels = 8;
int s_avail[kMaxModels];
int s_avail_count = -1;

// Forward: defined after rom_basename below, called at the end of the scan.
void rebuild_availability(void);

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

bool str_has_infix_ci(const char *haystack, const char *needle) {
  size_t hl = strlen(haystack);
  size_t nl = strlen(needle);
  if (nl == 0 || hl < nl) {
    return false;
  }
  for (size_t i = 0; i + nl <= hl; i++) {
    size_t j = 0;
    for (; j < nl; j++) {
      char a = haystack[i + j];
      char b = needle[j];
      if (a >= 'A' && a <= 'Z') {
        a = (char)(a - 'A' + 'a');
      }
      if (b >= 'A' && b <= 'Z') {
        b = (char)(b - 'A' + 'a');
      }
      if (a != b) {
        break;
      }
    }
    if (j == nl) {
      return true;
    }
  }
  return false;
}

// True for prefs/xpram sidecar files that are never ROMs.
bool rom_name_banned(const char *name) {
  if (str_has_suffix_ci(name, ".txt") || str_has_suffix_ci(name, ".log") ||
      str_has_suffix_ci(name, ".prefs")) {
    return true;
  }
  if (str_has_infix_ci(name, "prefs") || str_has_infix_ci(name, "xpram")) {
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
  debug_log("mac68k: config: scanning ROM dir %s", kRomDir);
  DIR *dir = opendir(kRomDir);
  if (dir == NULL) {
    debug_log("mac68k: config: cannot open ROM dir %s", kRomDir);
    return;
  }
  struct dirent *ent;
  while (s_rom_count < kMaxRoms && (ent = readdir(dir)) != NULL) {
    const char *name = ent->d_name;
    if (name[0] == '.') {
      continue;
    }
    if (rom_name_banned(name)) {
      debug_log("mac68k: config: ROM scan skipping %s", name);
      continue;
    }
    snprintf(s_rom_paths[s_rom_count], sizeof(s_rom_paths[0]), "%s/%s",
             kRomDir, name);
    debug_log("mac68k: config: ROM scan candidate %s",
              s_rom_paths[s_rom_count]);
    s_rom_count++;
  }
  closedir(dir);
  // Insertion sort by path for a stable menu order (opendir is arbitrary).
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
  debug_log("mac68k: config: ROM scan found %d file(s)", s_rom_count);
  rebuild_availability();
}

const char *rom_basename(const char *path) {
  const char *slash = strrchr(path, '/');
  return (slash != NULL && slash[1] != '\0') ? slash + 1 : path;
}

// Pool index of the file with the given basename (-1 when absent).
int pool_index_of_rom(const char *base) {
  if (base == NULL) {
    return -1;
  }
  for (int i = 0; i < s_rom_count; i++) {
    if (strcmp(rom_basename(s_rom_paths[i]), base) == 0) {
      return i;
    }
  }
  return -1;
}

// Stage the current model's ROM (or clear the choice when the model has
// no ROM). Called whenever the model changes and once per scan.
void stage_model_rom(void) {
  s_rom_choice = -1;
  if (s_model_index < 0 || s_model_index >= s_avail_count) {
    return;
  }
  const char *want = kModels[s_avail[s_model_index]].rom;
  s_rom_choice = pool_index_of_rom(want);
}

// Rebuild the availability filter and align the current model + staged
// ROM with it. Models without their ROM file are hidden everywhere.
void rebuild_availability(void) {
  s_avail_count = 0;
  const int n_models = (int)(sizeof(kModels) / sizeof(kModels[0]));
  for (int i = 0; i < n_models && s_avail_count < kMaxModels; i++) {
    if (pool_index_of_rom(kModels[i].rom) >= 0) {
      s_avail[s_avail_count++] = i;
    } else {
      debug_log("mac68k: config: hiding %s (missing %s)", kModels[i].name,
                kModels[i].rom);
    }
  }
  // Align current model: keep it when still available, else first one.
  int pos = -1;
  for (int i = 0; i < s_avail_count; i++) {
    if (kModels[s_avail[i]].id == s_modelid) {
      pos = i;
      break;
    }
  }
  if (pos < 0 && s_avail_count > 0) {
    pos = 0;
    s_modelid = kModels[s_avail[0]].id;
  }
  s_model_index = (pos < 0) ? 0 : pos;
  stage_model_rom();
}

}  // namespace

bool config_init(const Config &config) {
  s_ntsc = (config.tv_mode == nullptr) ||
           (strcmp(config.tv_mode, "-pal") != 0);
  s_ramsize_mb = config.ramsize_mb;
  s_modelid = config.modelid;
  s_cpu = config.cpu;
  // Align with availability (scan first so hidden models resolve): keep
  // the requested model when its ROM exists, else first available.
  rom_scan_once();
  s_model_index = 0;
  for (int i = 0; i < mac68k_model_count(); i++) {
    if (mac68k_model_id(i) == s_modelid) {
      s_model_index = i;
      break;
    }
  }
  if (mac68k_model_count() > 0) {
    s_modelid = mac68k_model_id(s_model_index);
    stage_model_rom();
  }
  s_ram_index = 0;
  for (int i = 0; i < mac68k_ramsize_count(); i++) {
    if (kRamOptions[i] == s_ramsize_mb) {
      s_ram_index = i;
      break;
    }
  }
  s_ready = true;
  return true;
}

bool config_is_ntsc(void) {
  return s_ntsc;
}

bool config_is_ready(void) {
  return s_ready;
}

int mac68k_model_count(void) {
  rom_scan_once();
  return s_avail_count < 0 ? 0 : s_avail_count;
}

const char *mac68k_model_name(int i) {
  if (i < 0 || i >= mac68k_model_count() || s_avail_count <= 0) {
    return nullptr;
  }
  return kModels[s_avail[i]].name;
}

int mac68k_model_id(int i) {
  if (i < 0 || i >= mac68k_model_count() || s_avail_count <= 0) {
    return -1;
  }
  return kModels[s_avail[i]].id;
}

int mac68k_model_current(void) {
  if (s_model_index < 0 || s_model_index >= mac68k_model_count()) {
    return 0;
  }
  return s_model_index;
}

void mac68k_model_set(int i) {
  if (i < 0 || i >= mac68k_model_count()) {
    return;
  }
  s_model_index = i;
  s_modelid = kModels[s_avail[i]].id;
  // The model owns its ROM: selecting a model stages its ROM image, so a
  // model/ROM mismatch cannot be constructed in the menu.
  stage_model_rom();
}

int mac68k_rom_count(void) {
  rom_scan_once();
  return s_rom_count;
}

const char *mac68k_rom_name(int i) {
  rom_scan_once();
  if (i < 0 || i >= s_rom_count) {
    return nullptr;
  }
  return rom_basename(s_rom_paths[i]);
}

const char *mac68k_rom_path(int i) {
  rom_scan_once();
  if (i < 0 || i >= s_rom_count) {
    return nullptr;
  }
  return s_rom_paths[i];
}

void mac68k_rom_set(int i) {
  rom_scan_once();
  if (i < 0 || i >= s_rom_count) {
    return;
  }
  s_rom_choice = i;
  // Live pref write (boot re-applies it after prefs_setup, which
  // re-scans heuristically and would otherwise override the choice).
  PrefsReplaceString("rom", s_rom_paths[i]);
  debug_log("mac68k: config: ROM set to %s (takes effect after Reset)",
            s_rom_paths[i]);
}

const char *mac68k_rom_staged_path(void) {
  if (s_rom_choice < 0 || s_rom_choice >= s_rom_count) {
    return nullptr;
  }
  return s_rom_paths[s_rom_choice];
}

int mac68k_rom_current(void) {
  rom_scan_once();
  if (s_rom_choice < 0 || s_rom_choice >= s_rom_count) {
    return 0;
  }
  return s_rom_choice;
}

int mac68k_ramsize_count(void) {
  return (int)(sizeof(kRamOptions) / sizeof(kRamOptions[0]));
}

int mac68k_ramsize_mb(int i) {
  if (i < 0 || i >= mac68k_ramsize_count()) {
    return -1;
  }
  return kRamOptions[i];
}

int mac68k_ramsize_current(void) {
  return s_ram_index;
}

void mac68k_ramsize_set(int i) {
  if (i < 0 || i >= mac68k_ramsize_count()) {
    return;
  }
  s_ram_index = i;
  s_ramsize_mb = kRamOptions[i];
  debug_log("mac68k: config: RAM set to %dMB (takes effect after Reset)",
            s_ramsize_mb);
}

int mac68k_ramsize_mb_current(void) {
  return kRamOptions[s_ram_index];
}

}  // namespace mac68k
}  // namespace bmx
