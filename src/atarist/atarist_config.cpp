//
// atarist_config.cpp
//
// AtariST core configuration for BMX bare-metal.
//
// Boots the vendored Hatari 2.6.1 core through its own Main_Init() (fake
// argv carries --tos/--machine/--memsize), exactly like the libretro port's
// retro_init(). TOS selection: EmuTOS default plus the genuine pool in
// /roms/ATARIST, switchable live from the Machine menu.
//

#include "atarist_config.h"
#include "atarist_debug.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <dirent.h>
// NOTE: <string> first -- Hatari cpu/sysdeps.h includes <string> under
// __cplusplus, which is only legal outside an extern "C" region.
#include <string>

extern "C" {
#include "main.h"
#include "configuration.h"
#include "reset.h"
#include "tos.h"
#include "sound.h"
#include "log.h"
#include "conv_st.h"
#include "joy.h"
}

namespace bmx {
namespace atarist {
namespace {

bool s_ready = false;
bool s_ntsc = false;

// TOS boot image on the SD card (staged from third_party/hatari/emutos/
// into /roms/ATARIST/).
const char *kEmuTosPath = "/roms/ATARIST/etos512uk.img";

bool file_present(const char *path) {
  FILE *fp = fopen(path, "rb");
  if (fp != NULL) {
    fclose(fp);
    return true;
  }
  return false;
}

// TOS pool: EmuTOS (index 0, resolved default) plus every other *.img in
// /roms/ATARIST, sorted. Scanned lazily so the menu (built
// before config_init) already sees the pool.
const int kMaxTos = 16;
char s_tos_paths[kMaxTos][64] = {{0}};
char s_tos_names[kMaxTos][48] = {{0}};
int s_tos_count = 0;
bool s_tos_scanned = false;
int s_tos_index = 0;

bool has_img_suffix(const char *name) {
  size_t n = strlen(name);
  if (n < 5) {
    return false;
  }
  const char *dot = name + n - 4;
  return (dot[0] == '.' &&
          (dot[1] == 'i' || dot[1] == 'I') &&
          (dot[2] == 'm' || dot[2] == 'M') &&
          (dot[3] == 'g' || dot[3] == 'G'));
}

void scan_tos_pool(void) {
  s_tos_scanned = true;
  s_tos_count = 0;
  // Entry 0 is always EmuTOS (the zero-config default).
  if (file_present(kEmuTosPath)) {
    snprintf(s_tos_paths[0], sizeof(s_tos_paths[0]), "%s", kEmuTosPath);
    snprintf(s_tos_names[0], sizeof(s_tos_names[0]), "EmuTOS (default)");
    s_tos_count = 1;
    kEmuTosPath = s_tos_paths[0];
  }
  if (s_tos_count == 0) {
    return;
  }
  // Genuine pool, sorted by filename, EmuTOS file itself skipped.
  static const char *pool_dirs[] = {"/roms/ATARIST"};
  char found[kMaxTos][64];
  int nfound = 0;
  for (size_t d = 0; d < sizeof(pool_dirs) / sizeof(pool_dirs[0]); d++) {
    DIR *dir = opendir(pool_dirs[d]);
    if (dir == NULL) {
      continue;
    }
    struct dirent *ent;
    while ((ent = readdir(dir)) != NULL && nfound < kMaxTos) {
      if (!has_img_suffix(ent->d_name)) {
        continue;
      }
      char full[64];
      snprintf(full, sizeof(full), "%s/%s", pool_dirs[d], ent->d_name);
      if (strcmp(full, s_tos_paths[0]) == 0) {
        continue;
      }
      bool dup = false;
      for (int i = 0; i < nfound; i++) {
        if (strcmp(found[i], full) == 0) {
          dup = true;
          break;
        }
      }
      if (!dup) {
        snprintf(found[nfound], sizeof(found[nfound]), "%s", full);
        nfound++;
      }
    }
    closedir(dir);
  }
  // Insertion sort by basename.
  for (int i = 1; i < nfound; i++) {
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%s", found[i]);
    int j = i - 1;
    while (j >= 0 &&
           strcmp(strrchr(found[j], '/') + 1, strrchr(tmp, '/') + 1) > 0) {
      snprintf(found[j + 1], sizeof(found[j + 1]), "%s", found[j]);
      j--;
    }
    snprintf(found[j + 1], sizeof(found[j + 1]), "%s", tmp);
  }
  for (int i = 0; i < nfound && s_tos_count < kMaxTos; i++) {
    snprintf(s_tos_paths[s_tos_count], sizeof(s_tos_paths[s_tos_count]),
             "%s", found[i]);
    const char *base = strrchr(found[i], '/');
    base = (base != NULL && base[1] != '\0') ? base + 1 : found[i];
    snprintf(s_tos_names[s_tos_count], sizeof(s_tos_names[s_tos_count]),
             "%s", base);
    s_tos_count++;
  }
  if (s_tos_index < 0 || s_tos_index >= s_tos_count) {
    s_tos_index = 0;
  }
}

}  // namespace

// TOS 2.06 only runs on a MegaSTE (MMU + RTC); everything else in the
// pool boots on a plain ST. Matched case-insensitively on the filename.
// Lives in bmx::atarist's anonymous namespace (same block as the pool).
bool tos_path_wants_megaste(const char *path) {
  if (path == NULL) {
    return false;
  }
  char lower[64];
  size_t n = strlen(path);
  if (n >= sizeof(lower)) {
    n = sizeof(lower) - 1;
  }
  for (size_t i = 0; i < n; i++) {
    char c = path[i];
    lower[i] = (char)(c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c);
  }
  lower[n] = '\0';
  return strstr(lower, "206") != NULL || strstr(lower, "mega") != NULL;
}

bool config_init(const Config &config) {
  debug_log("config_init: enter tv_mode=%s",
            config.tv_mode != NULL ? config.tv_mode : "(null)");
  s_ntsc = false;
  if (config.tv_mode != NULL && strstr(config.tv_mode, "ntsc") != NULL) {
    s_ntsc = true;
  }

  // The TOS image must exist before Main_Init: a missing TOS makes Hatari
  // bring up its (absent) GUI dialog and then ErrorExit. Fail loudly here
  // instead, with the exact SD path in the log.
  if (!s_tos_scanned) {
    scan_tos_pool();
  }
  if (s_tos_count == 0) {
    debug_log("config_init: FATAL missing TOS image '%s' (stage /roms/ATARIST first)",
              kEmuTosPath);
    return false;
  }
  // Staged TOS selection (menu-persisted); a pool that changed under us
  // falls back to EmuTOS.
  if (s_tos_index < 0 || s_tos_index >= s_tos_count) {
    s_tos_index = 0;
  }
  kEmuTosPath = s_tos_paths[s_tos_index];
  debug_log("config_init: TOS %d/%d '%s'", s_tos_index + 1, s_tos_count,
            kEmuTosPath);

  // Core Hatari log: point it at an SD file when writable ("stderr" is the
  // default, which would go to UART only). Probed first: a bad path here
  // would fail Log_Init and hang the boot.
  bool log_to_file = false;
  {
    FILE *probe = fopen("/bmretrosys-atarist-trace.log", "wb");
    if (probe != NULL) {
      fclose(probe);
      log_to_file = true;
    }
  }

  // Fake argv (Hatari parses --tos/--machine/--memsize in Opt_ParseParameters
  // during Main_Init, before the cold reset loads TOS).
  char arg0[] = "atarist";
  char arg_tos[] = "--tos";
  char arg_tos_path[64];
  char arg_machine[] = "--machine";
  char arg_machine_val[16];
  char arg_mem[] = "--memsize";
  char arg_mem_val[] = "1024";
  char arg_log[] = "--log-file";
  char arg_log_path[] = "/bmretrosys-atarist-trace.log";
  snprintf(arg_tos_path, sizeof(arg_tos_path), "%s", kEmuTosPath);
  snprintf(arg_machine_val, sizeof(arg_machine_val), "%s",
           tos_path_wants_megaste(kEmuTosPath) ? "megaste" : "st");
  char *argv[] = {arg0, arg_tos, arg_tos_path, arg_machine, arg_machine_val,
                  arg_mem, arg_mem_val,
                  arg_log, arg_log_path, NULL};
  int argc = log_to_file ? 9 : 7;

  debug_log("config_init: Main_Init (tos=%s machine=%s mem=1024K)...",
            arg_tos_path, arg_machine_val);
  Main_Init(argc, argv);

  // Phase 1 fixed choices (menu-editable from Phase 3/4):
  // - overscan on: fullscreen demos need borders (FBL sized 832x576)
  // - 44.1 kHz mixer (matches the Circle sink, no resampling in drain path)
  ConfigureParams.Screen.bAllowOverscan = true;
  // USB joysticks/gamepads arrive as BMX joy events; read them through the
  // REALSTICK seam (JoyUI_ReadJoystick serves the BMX port latches).
  // ST stick 1 is the primary games port.
  ConfigureParams.Joysticks.Joy[JOYID_JOYSTICK0].nJoystickMode =
      JOYSTICK_REALSTICK;
  ConfigureParams.Joysticks.Joy[JOYID_JOYSTICK1].nJoystickMode =
      JOYSTICK_REALSTICK;
  Configuration_Apply(true);
  // Apply() does not re-resolve the conversion: force it so the draw
  // functions match the no-overscan geometry (else ConvST keeps the
  // overscan size it computed during Main_Init and the blit crops).
  ConvST_ChangeResolution(true);
  Sound_SetOutputAudioFreq(44100);

  // The disassembly tracer writes to a SEPARATE file (TraceFile, default
  // stderr). Point it at the same SD file and re-open, so traces land on
  // the card instead of UART. (hLogFile re-truncates; core boot text is
  // already mirrored in bmretrosys-atarist.log via debug_log.)
  if (log_to_file) {
    snprintf(ConfigureParams.Log.sTraceFileName,
             sizeof(ConfigureParams.Log.sTraceFileName),
             "/bmretrosys-atarist-trace.log");
    Log_Init();
  }

  if (!bTosImageLoaded) {
    debug_log("config_init: FATAL TOS image failed to load");
    return false;
  }
  s_ready = true;
  debug_log("config_init: OK TOS=%s version=0x%x addr=0x%x machine=%d ntsc=%d",
            arg_tos_path, (unsigned)TosVersion, (unsigned)TosAddress,
            (int)ConfigureParams.System.nMachineType, (int)s_ntsc);
  return true;
}

bool config_is_ntsc(void) {
  return s_ntsc;
}

bool config_is_ready(void) {
  return s_ready;
}

int config_tos_count(void) {
  if (!s_tos_scanned) {
    scan_tos_pool();
  }
  return s_tos_count;
}

const char *config_tos_name(int index) {
  if (!s_tos_scanned) {
    scan_tos_pool();
  }
  if (index < 0 || index >= s_tos_count) {
    return "";
  }
  return s_tos_names[index];
}

const char *config_tos_path(int index) {
  if (!s_tos_scanned) {
    scan_tos_pool();
  }
  if (index < 0 || index >= s_tos_count) {
    return "";
  }
  return s_tos_paths[index];
}

void config_set_tos_index(int index) {
  if (!s_tos_scanned) {
    scan_tos_pool();
  }
  if (index < 0 || index >= s_tos_count) {
    index = 0;
  }
  s_tos_index = index;
}

int config_tos_index(void) {
  if (!s_tos_scanned) {
    scan_tos_pool();
  }
  if (s_tos_index < 0 || s_tos_index >= s_tos_count) {
    s_tos_index = 0;
  }
  return s_tos_index;
}

bool config_apply_tos_reset(void) {
  if (!s_ready) {
    return false;
  }
  int index = config_tos_index();
  const char *path = config_tos_path(index);
  if (path == NULL || *path == '\0' || !file_present(path)) {
    // Pool changed under us: back to EmuTOS.
    index = 0;
    s_tos_index = 0;
    path = config_tos_path(0);
    if (path == NULL || *path == '\0' || !file_present(path)) {
      return false;
    }
  }
  // Configuration_ChangeTos() is EMSCRIPTEN-only upstream; inline its
  // body (bounded copy into the ROM filename slot).
  snprintf(ConfigureParams.Rom.szTosImageFileName,
           sizeof(ConfigureParams.Rom.szTosImageFileName), "%s", path);
  ConfigureParams.System.nMachineType =
      tos_path_wants_megaste(path) ? MACHINE_MEGA_STE : MACHINE_ST;
  debug_log("tos: switching to '%s' machine=%d, cold reset", path,
            (int)ConfigureParams.System.nMachineType);
  if (Reset_Cold() != 0 || !bTosImageLoaded) {
    debug_log("tos: reload FAILED for '%s', restoring EmuTOS", path);
    snprintf(ConfigureParams.Rom.szTosImageFileName,
             sizeof(ConfigureParams.Rom.szTosImageFileName), "%s",
             config_tos_path(0));
    ConfigureParams.System.nMachineType = MACHINE_ST;
    Reset_Cold();
    s_tos_index = 0;
    return bTosImageLoaded ? true : false;
  }
  debug_log("tos: now version=0x%x addr=0x%x", (unsigned)TosVersion,
            (unsigned)TosAddress);
  return true;
}

}  // namespace atarist
}  // namespace bmx
