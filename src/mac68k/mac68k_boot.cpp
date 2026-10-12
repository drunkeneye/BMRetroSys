//
// mac68k_boot.cpp
//
// Mac68k guest bring-up: prefs -> RAM malloc -> ROM load -> globals ->
// CheckROM -> InitAll (which runs XPRAM seeding, all drivers, Init680x0
// and PatchROM internally; see third_party/basilisk2/main.cpp).
//

#include "sysdeps.h"

#include "mac68k_boot.h"

#include "mac68k_bmx_glue.h"
#include "mac68k_config.h"
#include "mac68k_debug.h"
#include "mac68k_video.h"

#include "cpu_emulation.h"
#include "main.h"
#include "prefs.h"
#include "rom_patches.h"
#include "video.h"

// Diag counters owned by emul_op.cpp / uae_cpu/memory.cpp.
void bmx_emulop_reset(void);
void bmx_dummy_reset(void);

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace bmx {
namespace mac68k {

namespace {

const char *kVmDir = "/roms/MAC68K";

bool rom_size_valid(uint32 size) {
  return size == 64u * 1024u || size == 128u * 1024u ||
         size == 256u * 1024u || size == 512u * 1024u ||
         size == 1024u * 1024u;
}

// Guest RAM+ROM area, kept across calls so a menu-driven reboot can
// ExitAll(), free the old area and boot fresh (fresh ROM bytes => PatchROM
// patterns match again; staged disk/ROM/model/RAM choices apply).
uint8 *s_area = NULL;

}  // namespace

bool boot_init(void) {
  debug_log("mac68k: boot step 1: debug_init()");
  debug_init();
  video_boot_stage(1);

  debug_log("mac68k: boot step 2: mac68k_prefs_setup()");
  mac68k_prefs_setup();

  // Staged menu choices (ROM/RAM) re-applied after prefs_setup, which
  // re-scans the ROM heuristically and pins ramsize to its default.
  const char *staged_rom = mac68k_rom_staged_path();
  if (staged_rom != NULL) {
    PrefsReplaceString("rom", staged_rom);
    debug_log("mac68k: boot step 2: staged ROM re-applied: %s", staged_rom);
  }
  uint32 ram_bytes = (uint32)mac68k_ramsize_mb_current() * 1024u * 1024u;
  if (ram_bytes == 0) {
    ram_bytes = 8u * 1024u * 1024u;
  }
  PrefsReplaceInt32("ramsize", (int32)ram_bytes);
  debug_log("mac68k: boot step 2: ramsize pref=%u", (unsigned)ram_bytes);
  video_boot_stage(2);

  const char *rom_path = PrefsFindString("rom");
  if (rom_path == NULL) {
    debug_log("mac68k: boot FAILED: no rom pref set (prefs_setup scans %s)",
              kVmDir);
    return false;
  }
  debug_log("mac68k: boot step 3: sizing ROM %s", rom_path);
  FILE *rom_file = fopen(rom_path, "rb");
  if (rom_file == NULL) {
    debug_log("mac68k: boot FAILED: cannot open ROM file %s", rom_path);
    return false;
  }
  if (fseek(rom_file, 0, SEEK_END) != 0) {
    debug_log("mac68k: boot FAILED: cannot seek ROM file %s", rom_path);
    fclose(rom_file);
    return false;
  }
  long rom_size = ftell(rom_file);
  fclose(rom_file);
  if (rom_size <= 0 || !rom_size_valid((uint32)rom_size)) {
    debug_log("mac68k: boot FAILED: bad ROM size %ld for %s "
              "(need 64K/128K/256K/512K/1M)",
              rom_size, rom_path);
    return false;
  }
  debug_log("mac68k: boot step 3: ROM size %ld", rom_size);
  video_boot_stage(3);

  // One combined RAM+ROM area, mirroring the Unix ram_rom_area layout
  // (main_unix.cpp: ROMBaseHost = RAMBaseHost + RAMSize). 1MB headroom
  // covers the largest supported ROM; the ROM is copied to RAMBase+RAM.
  // The old area (if any: menu-driven reboot) is freed first.
  if (s_area != NULL) {
    debug_log("mac68k: boot step 4: freeing previous RAM+ROM area %p",
              (void *)s_area);
    free(s_area);
    s_area = NULL;
  }
  debug_log("mac68k: boot step 4: malloc %u bytes guest RAM (+1M ROM headroom)",
            (unsigned)ram_bytes);
  const uint32 kRomMax = 1024u * 1024u;
  uint8 *area = (uint8 *)malloc(ram_bytes + kRomMax);
  if (area == NULL) {
    debug_log("mac68k: boot FAILED: cannot malloc %u bytes RAM+ROM area",
              (unsigned)(ram_bytes + kRomMax));
    return false;
  }
  s_area = area;
  uint8 *ram = area;
  uint8 *rom = area + ram_bytes;
  // Unix parity: vm_alloc hands zero pages; malloc (and especially the
  // free+malloc reboot path) hands stale guest data that a queue/table walk
  // can chase forever. Zero the whole area once here; the ROM is fread
  // over the tail right below.
  memset(s_area, 0, (size_t)ram_bytes + kRomMax);
  // Per-boot diag hygiene so post-reboot numbers read clean.
  bmx_emulop_reset();
  bmx_dummy_reset();
  debug_log("mac68k: boot step 4: RAM at host %p size %u (zeroed)", (void *)ram,
            (unsigned)ram_bytes);
  video_boot_stage(4);

  debug_log("mac68k: boot step 5: loading ROM %s", rom_path);
  rom_file = fopen(rom_path, "rb");
  if (rom_file == NULL) {
    debug_log("mac68k: boot FAILED: cannot reopen ROM file %s", rom_path);
    free(s_area);
    s_area = NULL;
    return false;
  }
  size_t got = fread(rom, 1, (size_t)rom_size, rom_file);
  fclose(rom_file);
  if (got != (size_t)rom_size) {
    debug_log("mac68k: boot FAILED: short ROM read %lu/%ld from %s",
              (unsigned long)got, rom_size, rom_path);
    free(s_area);
    s_area = NULL;
    return false;
  }
  debug_log("mac68k: boot step 5: ROM loaded %ld bytes from %s to host %p",
            rom_size, rom_path, (void *)rom);
  video_boot_stage(5);

  debug_log("mac68k: boot step 6: publishing RAM/ROM globals");
  RAMBaseHost = ram;
  RAMSize = ram_bytes;
  ROMBaseHost = rom;
  ROMSize = (uint32)rom_size;
  debug_log("mac68k: boot step 6: RAMBaseHost=%p RAMSize=%u ROMBaseHost=%p "
            "ROMSize=%u",
            (void *)RAMBaseHost, (unsigned)RAMSize, (void *)ROMBaseHost,
            (unsigned)ROMSize);
  video_boot_stage(6);

  debug_log("mac68k: boot step 7: CheckROM() prevROM=0x%x",
            (unsigned)ROMVersion);
  if (!CheckROM()) {
    debug_log("mac68k: boot FAILED: CheckROM rejected %s (ROMVersion=0x%x; "
              "banks build needs CLASSIC 0x0276 or 32-bit-clean 0x067c)",
              rom_path, (unsigned)ROMVersion);
    return false;
  }
  debug_log("mac68k: boot step 7: CheckROM ok, ROMVersion=0x%x classic=%d "
            "(classic = 64K/PLUS/CLASSIC)",
            (unsigned)ROMVersion,
            (ROMVersion == 0x0000 || ROMVersion == 0x0075 ||
             ROMVersion == 0x0276)
                ? 1
                : 0);

  // Set TwentyFourBitAddressing based on ROM version, matching upstream
  // Basilisk II main.cpp behavior:
  //   ROM_VERSION_64K/_PLUS/_CLASSIC (0x0000/0x0075/0x0276) → 24-bit addressing
  //   ROM_VERSION_32 (0x067c) → 32-bit addressing
  if (ROMVersion == 0x067c) {
    TwentyFourBitAddressing = false;
  } else {
    TwentyFourBitAddressing = true;
  }
  debug_log("mac68k: boot step 7: TwentyFourBitAddressing=%s (ROM=0x%x)",
            TwentyFourBitAddressing ? "true" : "false", (unsigned)ROMVersion);
  video_boot_stage(7);

  debug_log("mac68k: boot step 8: InitAll(%s)", kVmDir);
  if (!InitAll(kVmDir)) {
    debug_log("mac68k: boot FAILED: InitAll(%s) returned false", kVmDir);
    return false;
  }
  debug_log("mac68k: boot step 8: InitAll ok (XPRAM seeded, drivers up, "
            "Init680x0+PatchROM ran inside InitAll) ROM=0x%x 24bit=%d "
            "cpu=%d fpu=%d screenW=%lu screenH=%lu",
            (unsigned)ROMVersion, TwentyFourBitAddressing ? 1 : 0, CPUType,
            FPUType, (unsigned long)MacScreenWidth,
            (unsigned long)MacScreenHeight);
  // DIAGNOSTIC (temporary): diff the patched RAM copy against the file to
  // see exactly what PatchROM changed (and whether expected patches are
  // missing). Lists up to 40 differing 16-bit words.
  {
    FILE *rf = fopen(rom_path, "rb");
    if (rf != NULL) {
      uint8 *orig = (uint8 *)malloc((size_t)rom_size);
      if (orig != NULL &&
          fread(orig, 1, (size_t)rom_size, rf) == (size_t)rom_size) {
        unsigned diffs = 0, scanned = 0;
        for (long o = 0; o + 1 < rom_size; o += 2) {
          scanned++;
          uint16 a = (uint16)((orig[o] << 8) | orig[o + 1]);
          uint16 b = (uint16)((rom[o] << 8) | rom[o + 1]);
          if (a != b) {
            if (diffs < 40) {
              debug_log("mac68k: romdiff @0x%05lx file=%04x ram=%04x", (unsigned long)o, a, b);
            }
            diffs++;
          }
        }
        debug_log("mac68k: romdiff: %u/%u words differ", diffs, scanned);
      }
      if (orig != NULL) {
        free(orig);
      }
      fclose(rf);
    }
  }
  video_boot_stage(8);

  // Init680x0() and PatchROM() are intentionally NOT re-run here:
  // InitAll() already ran both (main.cpp Init680x0 block + PatchROM
  // block). Re-running PatchROM would search for byte patterns it already
  // replaced and fail; re-running Init680x0 would re-init the UAE memory
  // banks underneath the patched ROM.
  debug_log("mac68k: boot complete, model=%d cpu=%d ramsize=%u",
            (int)PrefsFindInt32("modelid"), (int)PrefsFindInt32("cpu"),
            (unsigned)ram_bytes);
  return true;
}

}  // namespace mac68k
}  // namespace bmx
