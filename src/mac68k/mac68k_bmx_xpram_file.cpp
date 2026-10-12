//
// mac68k_bmx_xpram_file.cpp
//
// BMX bare-metal XPRAM file backend: LoadXPRAM/SaveXPRAM/ZapPRAM over
// /roms/MAC68K/xpram (256 bytes, XPRAM_SIZE without POWERPC_ROM).
// XPRAM[]/XPRAMInit/XPRAMExit themselves live in portable xpram.cpp.
//

#include "sysdeps.h"

#include "mac68k_debug.h"

#include "xpram.h"

#include <stdio.h>
#include <string.h>

namespace {

const char *kXpramDefault = "/roms/MAC68K/xpram";
// Retained vmdir from Load so Save/Zap reuse the same path (upstream
// xpram_unix.cpp keeps a static xpram_path set by Load).
char s_xpram_path[256] = "";

void xpram_path_for(const char *vmdir, char *out, size_t out_size) {
  if (vmdir != NULL && vmdir[0] != '\0') {
    snprintf(out, out_size, "%s/xpram", vmdir);
  } else {
    snprintf(out, out_size, "%s", kXpramDefault);
  }
}

}  // namespace

void LoadXPRAM(const char *vmdir) {
  char path[256];
  xpram_path_for(vmdir, path, sizeof(path));
  bmx::mac68k::debug_log("mac68k: LoadXPRAM: vmdir=%s path=%s",
                         vmdir != NULL ? vmdir : "(null)", path);
  FILE *f = fopen(path, "rb");
  if (f == NULL) {
    bmx::mac68k::debug_log(
        "mac68k: LoadXPRAM: no file at %s, keeping defaults "
        "(InitAll seeds NuMc signature)",
        path);
    return;
  }
  snprintf(s_xpram_path, sizeof(s_xpram_path), "%s", path);
  size_t got = fread(XPRAM, 1, XPRAM_SIZE, f);
  fclose(f);
  bmx::mac68k::debug_log("mac68k: LoadXPRAM: read %lu/%d bytes from %s",
                         (unsigned long)got, XPRAM_SIZE, path);
  if (got != (size_t)XPRAM_SIZE) {
    bmx::mac68k::debug_log(
        "mac68k: LoadXPRAM: short file %s, rest keeps defaults", path);
  }
}

void SaveXPRAM(void) {
  const char *path = (s_xpram_path[0] != '\0') ? s_xpram_path : kXpramDefault;
  bmx::mac68k::debug_log("mac68k: SaveXPRAM: writing %s (%d bytes)", path,
                         XPRAM_SIZE);
  FILE *f = fopen(path, "wb");
  if (f == NULL) {
    bmx::mac68k::debug_log("mac68k: SaveXPRAM: cannot open %s for writing",
                           path);
    return;
  }
  size_t put = fwrite(XPRAM, 1, XPRAM_SIZE, f);
  fclose(f);
  if (put != (size_t)XPRAM_SIZE) {
    bmx::mac68k::debug_log("mac68k: SaveXPRAM: short write %lu/%d to %s",
                           (unsigned long)put, XPRAM_SIZE, path);
  } else {
    bmx::mac68k::debug_log("mac68k: SaveXPRAM: saved %s", path);
  }
}

void ZapPRAM(void) {
  const char *path = (s_xpram_path[0] != '\0') ? s_xpram_path : kXpramDefault;
  bmx::mac68k::debug_log("mac68k: ZapPRAM: removing %s", path);
  if (remove(path) != 0) {
    bmx::mac68k::debug_log("mac68k: ZapPRAM: %s not present (nothing to do)",
                           path);
  }
}
