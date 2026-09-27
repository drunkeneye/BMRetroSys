//
// atari800_config.cpp
//
// Atari800 core configuration for BMX bare-metal.
//
// atari800 (via libatari800) initializes from an argv list, exactly like the
// standalone program. We build a minimal argv that selects the TV mode and
// machine model, then call libatari800_init(). With EMUOS_ALTIRRA the Altirra
// OS and BASIC are compiled in as zero-config fallbacks; after init we scan
// /roms/ATARI800 (staged 1:1 from bmx/roms/a800/) with atari800's own CRC32/size
// database, so every machine model finds its ROMs however they are named.
// Explicit /atari/*.rom files and menu settings take precedence.
//



#include "atari800_config.h"
#include "atari800_debug.h"

// BMX boot timeline recorded by EmitBootTrace (src/viceapp.cpp). Declared
// locally instead of including viceapp.h: that header pulls in <ff.h> whose
// FatFS DIR typedef clashes with newlib dirent.h used below.
int BmxBootMarkCount(void);
const char *BmxBootMarkLabel(int index);
unsigned BmxBootMarkTicks(int index);

extern "C" {
#include "third_party/atari800/src/config.h"
#include "third_party/atari800/src/atari.h"
#include "third_party/atari800/src/crc32.h"
#include "third_party/atari800/src/memory.h"
#include "third_party/atari800/src/sound.h"
#include "third_party/atari800/src/sysrom.h"
#include "third_party/atari800/src/libatari800/libatari800.h"
}

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <circle/timer.h>

#ifdef HAVE_DIRENT_H
#include <dirent.h>
#else
struct dirent;
#endif

namespace bmx {
namespace atari800 {

static bool s_ntsc = false;
static bool s_ready = false;

// Pool helpers defined below config_init (they need kRomPoolDirs).
static int count_filled_slots(void);
static void scan_pool_prefill(void);
static int s_last_pool_matches;

// Canonical SD locations for optional external ROMs. When present they
// override the compiled-in Altirra OS/BASIC via the core's own -osb_rom /
// -xlxe_rom / -5200_rom / -basic_rom options (same names the desktop SDL
// port documents). Absent files simply keep the zero-config Altirra boot.
struct RomProbe {
  const char *option;
  const char *path;
};

static const RomProbe kRomProbes[] = {
    {"-osb_rom", "/roms/ATARI800/atariosb.rom"},
    {"-xlxe_rom", "/roms/ATARI800/atarixl.rom"},
    {"-5200_rom", "/roms/ATARI800/atari5200.rom"},
    {"-basic_rom", "/roms/ATARI800/ataribasic.rom"},
};



bool config_init(const Config &config) {
  unsigned long t_cfg_start = CTimer::GetClockTicks();
  debug_log("boot timeline: power-on to atari config %lu us (%.2f s)",
            t_cfg_start, (double)t_cfg_start * 1e-6);
  int marks = BmxBootMarkCount();
  unsigned long prev = 0;
  for (int i = 0; i < marks; i++) {
    unsigned long t = (unsigned long)BmxBootMarkTicks(i);
    debug_log("boot timeline: BMX %-42s %10lu us (+%lu us)",
              BmxBootMarkLabel(i), t, t - prev);
    prev = t;
  }
  s_ntsc = (config.tv_mode != nullptr)
               ? (strcmp(config.tv_mode, "-ntsc") == 0)
               : false;
  const char *tv_arg = s_ntsc ? "-ntsc" : "-pal";

  // Default to the 800/XL/XE family, which the Altirra OS supports.
  // NOTE: argc counts only real args. Upstream disables BASIC by default
  // (Atari800_disable_basic=TRUE); the desktop port turns it back on via
  // config, so pass -basic here unless the menu settings staged an
  // explicit preference (loaded before the emulator launches).
  const char *argv[2 + 1 + 2 * 4 + 1];
  int argc = 0;
  argv[argc++] = "atari800";
  argv[argc++] = tv_arg;
  if (!basic_preference_loaded()) {
    argv[argc++] = "-basic";
  }
  for (size_t i = 0; i < sizeof(kRomProbes) / sizeof(kRomProbes[0]); i++) {
    FILE *probe = fopen(kRomProbes[i].path, "rb");
    if (probe == NULL) {
      continue;
    }
    fclose(probe);
    argv[argc++] = kRomProbes[i].option;
    argv[argc++] = kRomProbes[i].path;
  }
  argv[argc] = NULL;

  // Fill the SYSROM slots from the staged pool BEFORE init, so init's own
  // InitialiseMachine loads the pool files on the first pass. Scanning
  // after init (as before) loaded Altirra built-ins first and then had to
  // reboot the machine on every boot to pick up the pool ROMs.
  unsigned long t_scan_start = CTimer::GetClockTicks();
  scan_pool_prefill();
  unsigned long t_scan_us = CTimer::GetClockTicks() - t_scan_start;
  debug_log("boot timeline: ROM pool scan %lu us (%.2f s)", t_scan_us,
            (double)t_scan_us * 1e-6);

  if (!libatari800_init(argc, (char **)argv)) {
    return false;
  }
  s_ready = true;

  s_last_pool_matches = count_filled_slots();
  clamp_all_rom_prefs();
  apply_staged_colors();

  // libatari800_init initialises the hardware but leaves the CPU in a
  // zeroed power-on state; without an explicit cold start the first frame
  // faults with "invalid display list". Every upstream port cold-starts
  // here (cf. atari_x11.c), so do the same.
  debug_log("Atari800: machine=%d ram=%d tv=%d os_ver=%d basic=%d "
            "nobasic=%d prefbasic=%d os_magic=%02x%02x%02x%02x",
            Atari800_machine_type, MEMORY_ram_size, Atari800_tv_mode,
            Atari800_os_version, MEMORY_have_basic, Atari800_disable_basic,
            basic_preference_loaded() ? 1 : 0, MEMORY_os[0], MEMORY_os[1],
            MEMORY_os[2], MEMORY_os[3]);
  debug_log("Atari800: cold-starting machine");
  Atari800_Coldstart();
  unsigned long t_cfg_us = CTimer::GetClockTicks() - t_cfg_start;
  debug_log("boot timeline: config start to machine ready %lu us (%.2f s)",
            t_cfg_us, (double)t_cfg_us * 1e-6);
  return true;
}

// Candidate locations for the staged ROM pool, in probe order. Bare
// absolute paths match the rest of BMX (bootstat probes); the
// drive-prefixed forms cover newlib leaders that require an explicit
// logical drive.
static const char *kRomPoolDirs[] = {
    "/roms/ATARI800",
    "SD:/roms/ATARI800",
    "SYS:/roms/ATARI800",
};

static bool dir_readable(const char *path) {
#ifdef HAVE_DIRENT_H
  DIR *dir = opendir(path);
  if (dir == NULL) {
    return false;
  }
  closedir(dir);
  return true;
#else
  (void)path;
  return false;
#endif
}

// Currently active OS/BASIC choices (resolved preference, -1 if none).
static void current_os_basic(int *os_ver, int *basic_ver) {
  int xe_ver = 0;
  SYSROM_ChooseROMs(Atari800_machine_type, MEMORY_ram_size, Atari800_tv_mode,
                    os_ver, basic_ver, &xe_ver);
}

// Pool selection cache: <pool>/.bmx_poolcache. The full FindInDir walk
// (186 files, ~7.5s) only pays off when the pool changes, which for a
// staged card is never. The cache stores the name fingerprint plus every
// filled slot's path; on a fingerprint hit the filenames are restored
// directly (zero I/O) and only the chosen OS+BASIC images are re-verified
// by size+CRC (~80ms). Machine/RAM/TV are deliberately NOT part of the
// key: the fill set is context-free, the choice is recomputed fresh, so a
// machine switch with an unchanged pool still hits the cache.
static const char kPoolCacheName[] = ".bmx_poolcache";

static void pool_cache_path(const char *pool, char *out, size_t outsz) {
  snprintf(out, outsz, "%s/%s", pool, kPoolCacheName);
}

// CRC-verify one file against its slot's expectations.
static bool verify_slot_file(int id, const char *path) {
  FILE *fp = fopen(path, "rb");
  if (fp == NULL) {
    return false;
  }
  fseek(fp, 0, SEEK_END);
  long len = ftell(fp);
  bool ok = false;
  if (len == (long)SYSROM_roms[id].size) {
    rewind(fp);
    ULONG crc = 0;
    // A zero expected CRC (custom slots) accepts any content of the
    // right size; mirrors SYSROM_SetPath's check.
    if (CRC32_FromFile(fp, &crc) &&
        (SYSROM_roms[id].crc32 == 0 || crc == SYSROM_roms[id].crc32)) {
      ok = true;
    }
  }
  fclose(fp);
  return ok;
}

// Restore cached slot filenames. Returns the fill count, or -1 when the
// cache is missing/stale.
static int pool_cache_restore(const char *pool, unsigned fp) {
  char cache[FILENAME_MAX];
  pool_cache_path(pool, cache, sizeof(cache));
  FILE *fp_in = fopen(cache, "r");
  if (fp_in == NULL) {
    debug_log("Atari800 ROM cache: no cache file, full scan");
    return -1;
  }
  unsigned cached_fp = 0;
  char line[512];
  if (fgets(line, sizeof(line), fp_in) == NULL ||
      sscanf(line, "BMXPOOL %x", &cached_fp) != 1) {
    fclose(fp_in);
    debug_log("Atari800 ROM cache: bad header, full scan");
    return -1;
  }
  if (cached_fp != fp || cached_fp == 0) {
    fclose(fp_in);
    debug_log("Atari800 ROM cache: pool changed %x->%x, full scan",
              cached_fp, fp);
    return -1;
  }
  int filled = 0;
  while (fgets(line, sizeof(line), fp_in) != NULL) {
    int id = -1;
    char path[512];
    size_t ll = strlen(line);
    while (ll > 0 && (line[ll - 1] == '\n' || line[ll - 1] == '\r')) {
      line[--ll] = '\0';
    }
    // Pipe-delimited (NOT whitespace): pool filenames contain spaces
    // ("REV02 800XL-65XE-130XE.rom"), which sscanf %s truncates.
    char *sep = strchr(line, '|');
    if (sep == NULL || sep == line) {
      continue;
    }
    *sep = '\0';
    if (sscanf(line, "%d", &id) != 1 || id < 0 ||
        id >= SYSROM_LOADABLE_SIZE) {
      continue;
    }
    snprintf(path, sizeof(path), "%s", sep + 1);
    if (path[0] == '\0') {
      continue;
    }
    snprintf(SYSROM_roms[id].filename, FILENAME_MAX, "%s", path);
    filled++;
  }
  fclose(fp_in);
  if (filled == 0) {
    debug_log("Atari800 ROM cache: no slots parsed, full scan");
    return -1;
  }
  // Trust-but-verify: the fingerprint covers names, so re-check exactly
  // what will boot (a same-name content swap elsewhere only matters if
  // the user later selects that revision; the menu rescan refreshes it).
  int os_ver = 0, basic_ver = 0, xe_ver = 0;
  SYSROM_ChooseROMs(Atari800_machine_type, MEMORY_ram_size, Atari800_tv_mode,
                    &os_ver, &basic_ver, &xe_ver);
  if (os_ver < 0 ||
      SYSROM_roms[os_ver].filename[0] == '\0' ||
      !verify_slot_file(os_ver, SYSROM_roms[os_ver].filename)) {
    debug_log("Atari800 ROM cache: OS slot %d verify failed (%s), full scan",
              os_ver,
              os_ver >= 0 ? SYSROM_roms[os_ver].filename : "?");
    return -1;
  }
  if (basic_ver >= 0 && SYSROM_roms[basic_ver].filename[0] != '\0' &&
      !verify_slot_file(basic_ver, SYSROM_roms[basic_ver].filename)) {
    debug_log("Atari800 ROM cache: BASIC slot %d verify failed, full scan",
              basic_ver);
    return -1;
  }
  return filled;
}

static void pool_cache_save(const char *pool, unsigned fp) {
  if (fp == 0) {
    return;
  }
  char cache[FILENAME_MAX];
  pool_cache_path(pool, cache, sizeof(cache));
  FILE *fp_out = fopen(cache, "w");
  if (fp_out == NULL) {
    debug_log("Atari800 ROM cache: cannot write %s, caching off", cache);
    return;
  }
  fprintf(fp_out, "BMXPOOL %x\n", fp);
  int saved = 0;
  for (int id = 0; id < SYSROM_LOADABLE_SIZE; id++) {
    if (SYSROM_roms[id].filename[0] != '\0') {
      // Pipe-delimited: filenames contain spaces, see restore.
      fprintf(fp_out, "%d|%s\n", id, SYSROM_roms[id].filename);
      saved++;
    }
  }
  fclose(fp_out);
  debug_log("Atari800 ROM cache: saved %d slots to %s", saved, cache);
}
static unsigned pool_fingerprint(const char *pool) {
#ifdef HAVE_DIRENT_H
  unsigned hash = 2166136261u;
  int entries = 0;
  DIR *dir = opendir(pool);
  if (dir == NULL) {
    return 0;
  }
  struct dirent *entry = NULL;
  while ((entry = readdir(dir)) != NULL && entries < 2000) {
    // The selection cache lives in the pool dir; exclude it so the
    // fingerprint is stable from the very first scan.
    if (strcmp(entry->d_name, kPoolCacheName) == 0) {
      continue;
    }
    entries++;
    for (const char *p = entry->d_name; *p != '\0'; p++) {
      hash ^= (unsigned char)*p;
      hash *= 16777619u;
    }
    hash ^= 0xff;
    hash *= 16777619u;
  }
  closedir(dir);
  hash ^= (unsigned)entries + 0x9e3779b9u + (hash << 6) + (hash >> 2);
  return (entries == 0) ? 0 : hash;
#else
  (void)pool;
  return 0;
#endif
}

static unsigned s_last_pool_fp = 0;
static bool s_pool_probed = false;

int last_rom_pool_matches(void) {
  return s_last_pool_matches;
}

// Diagnostic walk mirroring SYSROM_FindInDir's steps with logging, to find
// why on-device matching fails. Counts entries, fopen successes and
// size-allowed files (with their CRCs).
static int count_filled_slots(void) {
  int filled = 0;
  for (int id = 0; id < SYSROM_LOADABLE_SIZE; id++) {
    if (SYSROM_roms[id].data != NULL || SYSROM_roms[id].filename[0] != '\0') {
      filled++;
    }
  }
  return filled;
}

static void debug_probe_pool(const char *pool) {
  int entries = 0, opened = 0, sized = 0;
  int filled_before = count_filled_slots();
  DIR *dir = opendir(pool);
  if (dir == NULL) {
    debug_log("Atari800 ROM scan: opendir %s FAILED", pool);
    return;
  }
  struct dirent *entry = NULL;
  char full[FILENAME_MAX];
  while ((entry = readdir(dir)) != NULL && entries < 500) {
    entries++;
    size_t dl = strlen(pool);
    size_t nl = strlen(entry->d_name);
    if (dl + 1 + nl >= sizeof(full)) {
      continue;
    }
    memcpy(full, pool, dl);
    full[dl] = '/';
    memcpy(full + dl + 1, entry->d_name, nl + 1);
    FILE *fp = fopen(full, "rb");
    if (fp == NULL) {
      if (entries <= 5) {
        debug_log("Atari800 ROM scan: [%d] '%s' fopen FAILED", entries,
                  entry->d_name);
      }
      continue;
    }
    opened++;
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    if (len == 0x2800 || len == 0x4000 || len == 0x0800 || len == 0x2000) {
      sized++;
      if (sized <= 8) {
        rewind(fp);
        ULONG crc = 0;
        int ok = CRC32_FromFile(fp, &crc);
        debug_log("Atari800 ROM scan: '%s' len=%ld crc=%08x (%s)",
                  entry->d_name, len, (unsigned)crc, ok ? "ok" : "CRC-FAIL");
      }
    } else if (entries <= 5) {
      debug_log("Atari800 ROM scan: [%d] '%s' len=%ld (skip)", entries,
                entry->d_name, len);
    }
    fclose(fp);
  }
  closedir(dir);
  debug_log("Atari800 ROM scan: entries=%d opened=%d sized=%d slots=%d",
            entries, opened, sized, filled_before);
}

// First readable pool directory, or NULL when no pool is staged (Altirra
// built-ins stay active).
static const char *find_pool_dir(void) {
  for (size_t i = 0; i < sizeof(kRomPoolDirs) / sizeof(kRomPoolDirs[0]); i++) {
    if (dir_readable(kRomPoolDirs[i])) {
      return kRomPoolDirs[i];
    }
  }
  return NULL;
}

// Fingerprint-gated diagnostic probe: skips the 124-fopen walk when the
// pool is unchanged since the last scan.
static void maybe_probe_pool(const char *pool) {
  unsigned pool_fp = pool_fingerprint(pool);
  if (!s_pool_probed || pool_fp != s_last_pool_fp || pool_fp == 0) {
    debug_probe_pool(pool);
    s_last_pool_fp = pool_fp;
    s_pool_probed = true;
  } else {
    debug_log("Atari800 ROM scan: pool unchanged, diagnostic probe skipped");
  }
}

// Pre-init pool fill for config_init: populates the SYSROM slots from the
// staged pool before libatari800_init runs, so init's own
// InitialiseMachine loads the pool files on the first pass (single boot,
// no reboot). Safe pre-init: the slot arrays are static with compile-time
// size/CRC data, filenames survive init (SYSROM_SetDefaults only clears
// unset flags), and explicit /atari/*.rom argv options processed by init
// override these fills.
static void scan_pool_prefill(void) {
#ifdef HAVE_DIRENT_H
  const char *pool = find_pool_dir();
  if (pool == NULL) {
    debug_log("Atari800: no ROM pool on SD (/roms/ATARI800), Altirra built-ins active");
    return;
  }
  unsigned fp = pool_fingerprint(pool);
  int restored = (fp != 0) ? pool_cache_restore(pool, fp) : -1;
  if (restored > 0) {
    s_last_pool_matches = count_filled_slots();
    s_last_pool_fp = fp;
    s_pool_probed = true;
    debug_log("Atari800 ROM prefill: cache hit, slots=%d (scan skipped)",
              s_last_pool_matches);
    return;
  }
  maybe_probe_pool(pool);
  // Pass FALSE like the desktop TUI's "Find ROM images in a directory":
  // pre-init every slot flag reads unset, so TRUE would early-return
  // without scanning anything. FALSE matches by CRC32/size into any slot.
  SYSROM_FindInDir(pool, FALSE);
  s_last_pool_matches = count_filled_slots();
  debug_log("Atari800 ROM prefill: slots after FindInDir=%d",
            s_last_pool_matches);
  pool_cache_save(pool, fp);
#else
  printf("Atari800: ROM pool scan disabled, Altirra built-ins active\n");
#endif
}

// Runtime/menu rescan (menu "Scan ROMs", machine switches).
// Reinitialises + reboots only when the active OS/BASIC selection actually
// changes, mirroring the SDL TUI's System ROM settings flow. Returns true
// when a reboot was triggered. Boot does NOT use this: scan_pool_prefill
// above fills the slots before init, so the pool files load first-pass.
bool rescan_rom_pool(void) {
#ifdef HAVE_DIRENT_H
  const char *pool = find_pool_dir();
  if (pool == NULL) {
    debug_log("Atari800: no ROM pool on SD (/roms/ATARI800), Altirra built-ins active");
    return false;
  }
  int old_os = 0, old_basic = 0, new_os = 0, new_basic = 0;
  current_os_basic(&old_os, &old_basic);
  maybe_probe_pool(pool);
  // Pass FALSE like the desktop TUI's "Find ROM images in a directory":
  // SYSROM_SetDefaults() clears every unset flag during init, so TRUE
  // early-returns without scanning anything. FALSE matches by CRC32/size
  // into any slot; explicit /atari/*.rom argv selections simply match
  // first and stay authoritative.
  SYSROM_FindInDir(pool, FALSE);
  s_last_pool_matches = count_filled_slots();
  debug_log("Atari800 ROM scan: slots after FindInDir=%d", s_last_pool_matches);
  pool_cache_save(pool, pool_fingerprint(pool));
  current_os_basic(&new_os, &new_basic);
  if (new_os != old_os || new_basic != old_basic) {
    debug_log("Atari800: ROM pool scan changed OS %d->%d BASIC %d->%d, "
              "reloading",
              old_os, new_os, old_basic, new_basic);
    Atari800_InitialiseMachine();
    Atari800_Coldstart();
    return true;
  }
  debug_log("Atari800: ROM pool %s scanned, no change", pool);
  return false;
#else
  printf("Atari800: ROM pool scan disabled, Altirra built-ins active\n");
  return false;
#endif
}

bool config_is_ntsc(void) { return s_ntsc; }

bool config_is_ready(void) { return s_ready; }

}  // namespace atari800
}  // namespace bmx
