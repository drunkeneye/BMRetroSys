//
// mac68k_bmx_prefs.cpp
//
// BMX bare-metal preferences for Basilisk II: platform prefs items,
// tolerant LoadPrefs/SavePrefs (never exit like Unix LoadPrefs does),
// and mac68k_prefs_setup() which applies AddPrefsDefaults +
// AddPlatformPrefsDefaults + the BMX overrides.
//
// NOTE on PrefsInit: boot deliberately does NOT call PrefsInit(). The Unix
// LoadPrefs(vmdir) calls exit(1) when the prefs file is missing, and
// re-running AddPrefsDefaults after PrefsInit would append duplicate pref
// nodes. prefs_setup() below covers the same ground safely (defaults +
// tolerant file load + overrides, exactly once).
//

#include "sysdeps.h"

#include "mac68k_debug.h"
#include "mac68k_bmx_glue.h"
#include "mac68k_config.h"

#include "prefs.h"
#include "sys.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {

const char *kRomDir = "/roms/MAC68K";
const char *kRomFallback = "/roms/MAC68K/mac_rom.bin";
const char *kPrefsFileName = "prefs";

// Model mapping: mac68k_config placeholder IDs (see mac68k_config.cpp
// kModels) to the Basilisk II "modelid" pref, which is Gestalt ID minus 6
// (rom_patches.cpp writes the pref straight into UniversalInfo productKind;
// MacDesc table: IIci=11, ClassicII=23, Quadra650=36, Quadra605=94,
// Quadra800=35).
int32 map_model_to_pref(int config_id) {
  switch (config_id) {
    case 5:
      return 5;  // Mac IIci (11 - 6), the default
    case 6:
      return 88;  // Quadra 605 (94 - 6)
    case 7:
      return 30;  // Quadra 650 (36 - 6)
    case 8:
      return 17;  // Classic II (23 - 6)
    case 9:
      return 29;  // Quadra 800 (35 - 6)
    default:
      return 5;
  }
}

bool str_contains_ci(const char *haystack, const char *needle) {
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

bool has_suffix_ci(const char *name, const char *suffix) {
  size_t nl = strlen(name);
  size_t sl = strlen(suffix);
  if (nl < sl) {
    return false;
  }
  return str_contains_ci(name + nl - sl, suffix);
}

// Scan /roms/MAC68K: mac_rom.bin (boot default) first, else prefer a file
// with "rom" in the name, else the first non-.txt file. Returns true and
// fills out_path on success.
bool scan_rom_dir(char *out_path, size_t out_size) {
  bmx::mac68k::debug_log("mac68k: prefs: scanning ROM dir %s", kRomDir);
  // Deterministic default: mac_rom.bin wins when present, regardless of
  // readdir order (download_roms.py keeps it as a copy of the IIci ROM).
  FILE *probe = fopen(kRomFallback, "rb");
  if (probe != NULL) {
    fclose(probe);
    snprintf(out_path, out_size, "%s", kRomFallback);
    bmx::mac68k::debug_log("mac68k: prefs: ROM default %s", kRomFallback);
    return true;
  }
  DIR *dir = opendir(kRomDir);
  if (dir == NULL) {
    bmx::mac68k::debug_log("mac68k: prefs: cannot open ROM dir %s", kRomDir);
    return false;
  }
  char first[256];
  first[0] = '\0';
  char romish[256];
  romish[0] = '\0';
  struct dirent *ent;
  while ((ent = readdir(dir)) != NULL) {
    const char *name = ent->d_name;
    if (name[0] == '.') {
      continue;
    }
    if (has_suffix_ci(name, ".txt") || has_suffix_ci(name, ".log") ||
        has_suffix_ci(name, ".prefs")) {
      bmx::mac68k::debug_log("mac68k: prefs: ROM scan skipping %s", name);
      continue;
    }
    bmx::mac68k::debug_log("mac68k: prefs: ROM scan candidate %s", name);
    if (first[0] == '\0') {
      snprintf(first, sizeof(first), "%s/%s", kRomDir, name);
    }
    if (romish[0] == '\0' && str_contains_ci(name, "rom")) {
      snprintf(romish, sizeof(romish), "%s/%s", kRomDir, name);
    }
  }
  closedir(dir);
  const char *chosen = romish[0] != '\0' ? romish : first;
  if (chosen[0] == '\0') {
    bmx::mac68k::debug_log("mac68k: prefs: ROM scan found nothing in %s",
                           kRomDir);
    return false;
  }
  snprintf(out_path, out_size, "%s", chosen);
  bmx::mac68k::debug_log("mac68k: prefs: ROM scan chose %s", chosen);
  return true;
}

}  // namespace

// Platform-specific preferences items (BMX has none beyond the common set;
// the terminator-only list mirrors dummy/prefs_dummy.cpp).
prefs_desc platform_prefs_items[] = {
    {NULL, TYPE_END, false, NULL}  // End of list
};

// Tolerant prefs-file load: missing/unreadable file keeps the in-memory
// defaults (Unix LoadPrefs would exit(1); that must never happen on BMX).
void LoadPrefs(const char *vmdir) {
  char path[256];
  if (vmdir != NULL) {
    snprintf(path, sizeof(path), "%s/%s", vmdir, kPrefsFileName);
  } else {
    snprintf(path, sizeof(path), "%s/%s", kRomDir, kPrefsFileName);
  }
  bmx::mac68k::debug_log("mac68k: LoadPrefs: trying %s", path);
  FILE *f = fopen(path, "rb");
  if (f == NULL) {
    bmx::mac68k::debug_log(
        "mac68k: LoadPrefs: no prefs file at %s, keeping defaults", path);
    return;
  }
  LoadPrefsFromStream(f);
  fclose(f);
  bmx::mac68k::debug_log("mac68k: LoadPrefs: loaded %s", path);
}

// Log-only: persisting prefs to SD is skipped (XPRAM has its own file;
// avoids SD wear from per-boot rewrites).
void SavePrefs(void) {
  bmx::mac68k::debug_log("mac68k: SavePrefs: log-only, not writing to SD");
}

// Add defaults of platform-specific prefs items. BMX has no platform items;
// log-only (mirrors the dummy backend's empty implementation).
void AddPlatformPrefsDefaults(void) {
  bmx::mac68k::debug_log("mac68k: AddPlatformPrefsDefaults: no BMX items");
}

namespace bmx {
namespace mac68k {

void mac68k_prefs_setup(void) {
  // AddPrefsDefaults uses PrefsAdd* (append). It must run exactly once per
  // process; a menu-driven reboot re-runs prefs_setup, and a second Add
  // would append duplicate singleton nodes (Find/Replace hit index 0, so
  // the dupes are unreachable but leak). Guard matches the once-only
  // contract in mac68k_bmx_glue.h; mutable items below use Replace.
  static bool s_defaults_done = false;
  debug_log("mac68k: prefs_setup: applying defaults");
  if (!s_defaults_done) {
    AddPrefsDefaults();
    s_defaults_done = true;
  }
  debug_log("mac68k: prefs_setup: AddPrefsDefaults done");
  // AddPrefsDefaults already calls SysAddSerialPrefs (prefs_items.cpp), but
  // the serial prefs are mandatory, so ensure them explicitly (same values).
  SysAddSerialPrefs();
  debug_log("mac68k: prefs_setup: SysAddSerialPrefs done");
  AddPlatformPrefsDefaults();
  debug_log("mac68k: prefs_setup: AddPlatformPrefsDefaults done");

  // Tolerant prefs-file overlay (keeps defaults when missing).
  LoadPrefs(kRomDir);

  // ROM path: SD scan, fallback to the conventional name.
  char rom_path[256];
  if (!scan_rom_dir(rom_path, sizeof(rom_path))) {
    snprintf(rom_path, sizeof(rom_path), "%s", kRomFallback);
    debug_log("mac68k: prefs_setup: no ROM found, fallback %s", rom_path);
  }
  PrefsReplaceString("rom", rom_path);
  debug_log("mac68k: prefs_setup: rom=%s", rom_path);

  PrefsReplaceInt32("ramsize", 8 * 1024 * 1024);
  debug_log("mac68k: prefs_setup: ramsize=%d", 8 * 1024 * 1024);

  int current = mac68k_model_current();
  int config_id =
      (current >= 0 && current < mac68k_model_count())
          ? mac68k_model_id(current)
          : 5;
  // Quadras are 68040 (with FPU); IIci/Classic II are 68030 (no FPU).
  // Running Quadra ROMs under a 68030 misbehaves, so the CPU follows the
  // model like the ROM does.
  // NOTE (2026-10-09): the IIci ROM (368CADFE) only reaches the boot
  // screen with an FPU present (matches upstream Basilisk II, where this
  // ROM warns without one and stays black). Enable it for the IIci too;
  // Classic II keeps the stock no-FPU setting.
  int32 cpu = 3;
  bool fpu = false;
  if (config_id == 6 || config_id == 7 || config_id == 9) {
    cpu = 4;
    fpu = true;
  }
  if (config_id == 5) {
    fpu = true;
  }
  PrefsReplaceInt32("cpu", cpu);
  debug_log("mac68k: prefs_setup: cpu=%d (config id %d)", (int)cpu, config_id);
  int32 modelid = map_model_to_pref(config_id);
  PrefsReplaceInt32("modelid", modelid);
  debug_log("mac68k: prefs_setup: modelid=%d (config id %d)", (int)modelid,
            config_id);

  PrefsReplaceBool("fpu", fpu);
  debug_log("mac68k: prefs_setup: fpu=%s", fpu ? "true" : "false");

  PrefsReplaceInt32("keyboardtype", 5);
  debug_log("mac68k: prefs_setup: keyboardtype=5");

  PrefsReplaceString("screen", "win/640/480");
  debug_log("mac68k: prefs_setup: screen=win/640/480");

  PrefsReplaceInt32("frameskip", 6);
  debug_log("mac68k: prefs_setup: frameskip=6");

  PrefsReplaceInt32("displaycolordepth", 16);
  debug_log("mac68k: prefs_setup: displaycolordepth=16");

  PrefsReplaceBool("nogui", true);
  debug_log("mac68k: prefs_setup: nogui=true");

  PrefsReplaceBool("noclipconversion", true);
  debug_log("mac68k: prefs_setup: noclipconversion=true");

  PrefsReplaceBool("nocdrom", true);
  debug_log("mac68k: prefs_setup: nocdrom=true");

  PrefsReplaceInt32("bootdrive", 0);
  debug_log("mac68k: prefs_setup: bootdrive=0");

  PrefsReplaceInt32("bootdriver", 0);
  debug_log("mac68k: prefs_setup: bootdriver=0");

  // Disk/floppy lists intentionally NOT set here: SonyInit/DiskInit call
  // SysAddDiskPrefs/SysAddFloppyPrefs (the /systems/MAC68K/disks scan in
  // mac68k_bmx_sys.cpp) when no prefs exist.
  debug_log("mac68k: prefs_setup: complete (disk/floppy via SysAdd* scan)");
}

}  // namespace mac68k
}  // namespace bmx
