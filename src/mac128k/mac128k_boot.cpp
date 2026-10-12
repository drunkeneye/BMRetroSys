//
// mac128k_boot.cpp
//
// Mac 128K guest bring-up.
//

#include "mac128k_boot.h"

#include "mac128k_bmx_backend.h"
#include "mac128k_bmx_glue.h"
#include "mac128k_config.h"
#include "mac128k_debug.h"
#include "mac128k_video.h"

#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

namespace bmx {
namespace mac128k {

namespace {

const char *kDiskDir = "/systems/MAC128K/disks";
const int kMaxDisks = 2;

char s_boot_disks[kMaxDisks][256];
bool s_disks_scanned = false;

bool ends_with_ci(const char *name, const char *suffix) {
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

// 128K has 400K single-sided drives: .dsk and small .img only.
bool is_floppy_image(const char *name) {
  return ends_with_ci(name, ".dsk") || ends_with_ci(name, ".img");
}

// First boot: auto-stage up to 2 floppy images found in the disk dir.
void scan_disks_once(void) {
  if (s_disks_scanned) {
    return;
  }
  s_disks_scanned = true;
  // Older cards predate the systems/MAC128K/disks stanza: create it so a
  // missing dir never means missing disks (mkdir is a no-op if present).
  mkdir(kDiskDir, 0777);
  DIR *dir = opendir(kDiskDir);
  if (dir == NULL) {
    debug_log("mac128k: boot: cannot open disk dir %s", kDiskDir);
    return;
  }
  int staged = 0;
  struct dirent *ent;
  while (staged < kMaxDisks && (ent = readdir(dir)) != NULL) {
    const char *name = ent->d_name;
    if (name[0] == '.') {
      continue;
    }
    if (!is_floppy_image(name)) {
      continue;
    }
    snprintf(s_boot_disks[staged], sizeof(s_boot_disks[0]), "%s/%s",
             kDiskDir, name);
    debug_log("mac128k: boot: auto-staged disk %d = %s", staged,
              s_boot_disks[staged]);
    staged++;
  }
  closedir(dir);
}

long file_size(const char *path) {
  FILE *f = fopen(path, "rb");
  if (f == NULL) {
    return -1;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return -1;
  }
  long n = ftell(f);
  fclose(f);
  return n;
}

}  // namespace

const char *boot_disk_path(int drive) {
  if (drive < 0 || drive >= kMaxDisks) {
    return "";
  }
  return s_boot_disks[drive];
}

void boot_disk_set(int drive, const char *path) {
  if (drive < 0 || drive >= kMaxDisks) {
    return;
  }
  if (path == NULL || path[0] == '\0') {
    s_boot_disks[drive][0] = '\0';
  } else {
    snprintf(s_boot_disks[drive], sizeof(s_boot_disks[0]), "%s", path);
  }
}

bool boot_init(void) {
  debug_log("mac128k: boot step 1: debug_init()");
  debug_init();
  video_boot_stage(1);

  const char *rom_path = mac128k_rom_staged_path();
  if (rom_path == NULL) {
    debug_log("mac128k: boot FAILED: no ROM in /roms/MAC128K "
              "(need 64KB 128K ROM)");
    return false;
  }
  debug_log("mac128k: boot step 2: ROM %s", rom_path);
  long rom_size = file_size(rom_path);
  if (rom_size != (long)kMac128kRomSize) {
    debug_log("mac128k: boot FAILED: bad ROM size %ld for %s (need %u)",
              rom_size, rom_path, kMac128kRomSize);
    return false;
  }
  debug_log("mac128k: boot step 2: ROM size %ld ok", rom_size);
  video_boot_stage(2);

  scan_disks_once();
  bmx_mac128k_backend_set_rom_path(rom_path);
  for (int d = 0; d < kMaxDisks; d++) {
    bmx_mac128k_backend_set_disk(d, s_boot_disks[d], 0);
    debug_log("mac128k: boot step 3: disk %d = %s", d,
              s_boot_disks[d][0] != '\0' ? s_boot_disks[d] : "(empty)");
  }
  video_boot_stage(3);

  debug_log("mac128k: boot step 4: backend_init()");
  if (!bmx_mac128k_backend_init()) {
    debug_log("mac128k: boot FAILED: backend_init (see backend log above)");
    return false;
  }
  video_boot_stage(8);
  debug_log("mac128k: boot complete, rom=%s", rom_path);
  return true;
}

}  // namespace mac128k
}  // namespace bmx
