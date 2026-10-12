//
// mac68k_bmx_sys.cpp
//
// BMX bare-metal implementation of third_party/basilisk2/include/sys.h.
// Direct newlib/FatFs fopen/fread/fwrite/fseek/ftell/fclose (+ opendir for
// the disk scan). Signatures match sys.h exactly; loff_t is off_t via the
// bmx-shim sysdeps.h.
//

#include "sysdeps.h"

#include "mac68k_debug.h"

#include "macos_util.h"
#include "sys.h"
#include "prefs.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

namespace {

const char *kDiskDir = "/systems/MAC68K/disks";
const int kMaxScanAdds = 6;
// .img at or below this size is treated as a floppy (Sony bus), larger as
// a hard disk (Disk bus). 2MB covers every Mac floppy (400K/800K/1.4M)
// plus header slack.
const long kFloppyMaxBytes = 2L * 1024L * 1024L;

// Small Sys I/O handle: Sys_open callers only ever see void*.
struct BmxFileHandle {
  FILE *fp;
  bool read_only;
  // DiskCopy/header handling (upstream FileDiskLayout parity): guest offsets
  // are biased by start_byte, size reports the header-excluded real size.
  loff_t start_byte;
  loff_t real_size;
};

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

bool is_disk_image(const char *name) {
  return ends_with_ci(name, ".dsk") || ends_with_ci(name, ".hfv") ||
         ends_with_ci(name, ".img");
}

// Bus routing: .dsk -> floppy (Sony), .hfv -> disk (Disk), .img -> size.
// Upstream keeps the two buses strictly separate (floppy=/dev/fd*, disk=HFS
// partitions); putting every image on both buses double-opens the same file
// and makes bootdrive nondeterministic.
bool image_belongs_on_bus(const char *name, const char *pref_name) {
  const bool want_floppy = (strcmp(pref_name, "floppy") == 0);
  if (ends_with_ci(name, ".dsk")) {
    return want_floppy;
  }
  if (ends_with_ci(name, ".hfv")) {
    return !want_floppy;
  }
  if (ends_with_ci(name, ".img")) {
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", kDiskDir, name);
    struct stat st;
    if (stat(path, &st) != 0) {
      return false;
    }
    const bool is_floppy_size = (long)st.st_size <= kFloppyMaxBytes;
    return want_floppy == is_floppy_size;
  }
  return false;
}

// Shared opendir scan used by SysAddDiskPrefs/SysAddFloppyPrefs.
void scan_disks_into_prefs(const char *pref_name) {
  bmx::mac68k::debug_log("mac68k: SysAdd%sPrefs: scanning %s", pref_name,
                         kDiskDir);
  DIR *dir = opendir(kDiskDir);
  if (dir == NULL) {
    bmx::mac68k::debug_log(
        "mac68k: SysAdd%sPrefs: cannot open %s, adding nothing "
        "(DiskInit/SonyInit tolerate zero drives)",
        pref_name, kDiskDir);
    return;
  }
  int added = 0;
  struct dirent *ent;
  while (added < kMaxScanAdds && (ent = readdir(dir)) != NULL) {
    const char *name = ent->d_name;
    if (name[0] == '.') {
      continue;
    }
    if (!is_disk_image(name) || !image_belongs_on_bus(name, pref_name)) {
      bmx::mac68k::debug_log("mac68k: SysAdd%sPrefs: skipping %s/%s", pref_name,
                             kDiskDir, name);
      continue;
    }
    char path[256];
    snprintf(path, sizeof(path), "%s/%s", kDiskDir, name);
    PrefsAddString(pref_name, path);
    bmx::mac68k::debug_log("mac68k: SysAdd%sPrefs: added %s=%s", pref_name,
                           pref_name, path);
    added++;
  }
  closedir(dir);
  bmx::mac68k::debug_log("mac68k: SysAdd%sPrefs: done, added=%d", pref_name,
                         added);
}

}  // namespace

// newlib declares getpagesize() in <sys/unistd.h> but never defines it
// (verified: absent from libc.a). vm_alloc.cpp needs it; ARM pages are
// 4 KiB. NOT a macro (that would break the libc header declaration).
extern "C" int getpagesize(void) {
  return 4096;
}

void SysInit(void) {
  bmx::mac68k::debug_log("mac68k: SysInit (log-only, no host init needed)");}

void SysExit(void) {
  bmx::mac68k::debug_log("mac68k: SysExit (log-only)");
}

void SysAddFloppyPrefs(void) {
  scan_disks_into_prefs("floppy");
}

void SysAddDiskPrefs(void) {
  scan_disks_into_prefs("disk");
}

void SysAddCDROMPrefs(void) {
  // Don't scan for drives if nocdrom option given (mirrors sys_unix.cpp).
  if (PrefsFindBool("nocdrom")) {
    bmx::mac68k::debug_log(
        "mac68k: SysAddCDROMPrefs: nocdrom=true, adding nothing");
    return;
  }
  // BMX has no CD-ROM hardware: log-only even with nocdrom=false.
  bmx::mac68k::debug_log(
      "mac68k: SysAddCDROMPrefs: no CD hardware on BMX, adding nothing");
}

void SysAddSerialPrefs(void) {
  // REQUIRED by AddPrefsDefaults (prefs_items.cpp calls this first thing).
  // Use Replace (not Add): mac68k_prefs_setup() calls SysAddSerialPrefs a
  // second time after AddPrefsDefaults, and Add would append duplicates.
  PrefsReplaceString("seriala", "dummy");
  PrefsReplaceString("serialb", "dummy");
  bmx::mac68k::debug_log(
      "mac68k: SysAddSerialPrefs: seriala=dummy serialb=dummy");
}

void *Sys_open(const char *name, bool read_only) {
  if (name == NULL) {
    bmx::mac68k::debug_log("mac68k: Sys_open: NULL name, returning NULL");
    return NULL;
  }
  bmx::mac68k::debug_log("mac68k: Sys_open: name=%s read_only=%d", name,
                         (int)read_only);
  FILE *fp = NULL;
  bool ro = read_only;
  if (!ro) {
    fp = fopen(name, "r+b");
    if (fp == NULL) {
      // Read-write failed, try read-only (mirrors sys_unix.cpp fallback).
      bmx::mac68k::debug_log(
          "mac68k: Sys_open: r+b failed for %s, retrying read-only", name);
      ro = true;
      fp = fopen(name, "rb");
    }
  } else {
    fp = fopen(name, "rb");
  }
  if (fp == NULL) {
    bmx::mac68k::debug_log("mac68k: Sys_open: cannot open %s, returning NULL",
                           name);
    return NULL;
  }
  BmxFileHandle *fh =
      (BmxFileHandle *)malloc(sizeof(BmxFileHandle));
  if (fh == NULL) {
    bmx::mac68k::debug_log("mac68k: Sys_open: out of memory for %s", name);
    fclose(fp);
    return NULL;
  }
  fh->fp = fp;
  fh->read_only = ro;
  // Upstream FileDiskLayout parity (sys_unix.cpp:658-666 via
  // macos_util.cpp:93-104): 84-byte header for 419284/838484-byte DiskCopy
  // images, else size&0x1ff header. Guest I/O is biased by start_byte and
  // size reports the header-excluded real size.
  fh->start_byte = 0;
  fh->real_size = 0;
  {
    uint8 head[256];
    loff_t size = 0;
    if (fseek(fp, 0, SEEK_END) == 0) {
      long end = ftell(fp);
      if (end >= 0) {
        size = (loff_t)end;
      }
    }
    size_t head_n = 0;
    if (size > 0 && fseek(fp, 0, SEEK_SET) == 0) {
      head_n = fread(head, 1, sizeof(head), fp);
    }
    if (size > 0 && head_n > 0) {
      loff_t sb = 0, rs = 0;
      FileDiskLayout(size, head, sb, rs);
      fh->start_byte = sb;
      fh->real_size = rs;
    } else {
      fh->real_size = size;
    }
  }
  bmx::mac68k::debug_log("mac68k: Sys_open: opened %s handle=%p read_only=%d "
                         "start=%lld size=%lld",
                         name, (void *)fh, (int)ro,
                         (long long)fh->start_byte,
                         (long long)fh->real_size);
  return fh;
}

void Sys_close(void *fh) {
  if (fh == NULL) {
    bmx::mac68k::debug_log("mac68k: Sys_close: NULL handle, ignoring");
    return;
  }
  BmxFileHandle *h = (BmxFileHandle *)fh;
  bmx::mac68k::debug_log("mac68k: Sys_close: handle=%p", fh);
  if (h->fp != NULL) {
    fclose(h->fp);
  }
  free(h);
}

size_t Sys_read(void *fh, void *buffer, loff_t offset, size_t length) {
  if (fh == NULL || buffer == NULL) {
    bmx::mac68k::debug_log(
        "mac68k: Sys_read: NULL handle/buffer (handle=%p buffer=%p), "
        "returning 0",
        fh, buffer);
    return 0;
  }
  BmxFileHandle *h = (BmxFileHandle *)fh;
  const loff_t phys = offset + h->start_byte;
  if (fseeko(h->fp, (off_t)phys, SEEK_SET) != 0) {
    bmx::mac68k::debug_log(
        "mac68k: Sys_read: fseek to %lld failed (handle=%p)", (long long)offset,
        fh);
    return 0;
  }
  size_t got = fread(buffer, 1, length, h->fp);
  if (got != length) {
    bmx::mac68k::debug_log(
        "mac68k: Sys_read: short read %lu/%lu at %lld (handle=%p)",
        (unsigned long)got, (unsigned long)length, (long long)offset, fh);
  }
  return got;
}

size_t Sys_write(void *fh, void *buffer, loff_t offset, size_t length) {
  if (fh == NULL || buffer == NULL) {
    bmx::mac68k::debug_log(
        "mac68k: Sys_write: NULL handle/buffer (handle=%p buffer=%p), "
        "returning 0",
        fh, buffer);
    return 0;
  }
  BmxFileHandle *h = (BmxFileHandle *)fh;
  if (h->read_only) {
    bmx::mac68k::debug_log(
        "mac68k: Sys_write: handle=%p is read-only, refusing %lu bytes at "
        "%lld",
        fh, (unsigned long)length, (long long)offset);
    return 0;
  }
  const loff_t phys = offset + h->start_byte;
  if (fseeko(h->fp, (off_t)phys, SEEK_SET) != 0) {
    bmx::mac68k::debug_log(
        "mac68k: Sys_write: fseek to %lld failed (handle=%p)",
        (long long)offset, fh);
    return 0;
  }
  size_t put = fwrite(buffer, 1, length, h->fp);
  if (put != length) {
    bmx::mac68k::debug_log(
        "mac68k: Sys_write: short write %lu/%lu at %lld (handle=%p)",
        (unsigned long)put, (unsigned long)length, (long long)offset, fh);
  }
  return put;
}

loff_t SysGetFileSize(void *fh) {
  if (fh == NULL) {
    bmx::mac68k::debug_log("mac68k: SysGetFileSize: NULL handle, returning 0");
    return 0;
  }
  BmxFileHandle *h = (BmxFileHandle *)fh;
  // Header-excluded size computed at open (FileDiskLayout parity).
  bmx::mac68k::debug_log("mac68k: SysGetFileSize: handle=%p size=%lld", fh,
                         (long long)h->real_size);
  return h->real_size;
}

void SysEject(void *fh) {
  bmx::mac68k::debug_log("mac68k: SysEject: handle=%p (log-only)", fh);
}

bool SysFormat(void *fh) {
  // Upstream returns false for NULL (sys_unix.cpp).
  if (fh == NULL) {
    return false;
  }
  bmx::mac68k::debug_log("mac68k: SysFormat: handle=%p returning true", fh);
  return true;
}

bool SysIsReadOnly(void *fh) {
  if (fh == NULL) {
    bmx::mac68k::debug_log("mac68k: SysIsReadOnly: NULL handle, returning true");
    return true;
  }
  bool ro = ((BmxFileHandle *)fh)->read_only;
  bmx::mac68k::debug_log("mac68k: SysIsReadOnly: handle=%p ro=%d", fh, (int)ro);
  return ro;
}

bool SysIsFixedDisk(void *fh) {
  bmx::mac68k::debug_log("mac68k: SysIsFixedDisk: handle=%p returning true", fh);
  return true;
}

bool SysIsDiskInserted(void *fh) {
  // Upstream returns false for NULL; true for files (sys_unix.cpp).
  if (fh == NULL) {
    return false;
  }
  bmx::mac68k::debug_log("mac68k: SysIsDiskInserted: handle=%p returning true",
                         fh);
  return true;
}

void SysPreventRemoval(void *fh) {
  bmx::mac68k::debug_log("mac68k: SysPreventRemoval: handle=%p (log-only)", fh);
}

void SysAllowRemoval(void *fh) {
  bmx::mac68k::debug_log("mac68k: SysAllowRemoval: handle=%p (log-only)", fh);
}

bool SysCDReadTOC(void *fh, uint8 *toc) {
  bmx::mac68k::debug_log("mac68k: SysCDReadTOC: handle=%p toc=%p (no CD, false)",
                         fh, (void *)toc);
  return false;
}

bool SysCDGetPosition(void *fh, uint8 *pos) {
  bmx::mac68k::debug_log(
      "mac68k: SysCDGetPosition: handle=%p pos=%p (no CD, false)", fh,
      (void *)pos);
  return false;
}

bool SysCDPlay(void *fh, uint8 start_m, uint8 start_s, uint8 start_f,
               uint8 end_m, uint8 end_s, uint8 end_f) {
  bmx::mac68k::debug_log(
      "mac68k: SysCDPlay: handle=%p %u:%u:%u-%u:%u:%u (no CD, false)", fh,
      start_m, start_s, start_f, end_m, end_s, end_f);
  return false;
}

bool SysCDPause(void *fh) {
  bmx::mac68k::debug_log("mac68k: SysCDPause: handle=%p (no CD, false)", fh);
  return false;
}

bool SysCDResume(void *fh) {
  bmx::mac68k::debug_log("mac68k: SysCDResume: handle=%p (no CD, false)", fh);
  return false;
}

bool SysCDStop(void *fh, uint8 lead_out_m, uint8 lead_out_s,
               uint8 lead_out_f) {
  bmx::mac68k::debug_log(
      "mac68k: SysCDStop: handle=%p %u:%u:%u (no CD, false)", fh, lead_out_m,
      lead_out_s, lead_out_f);
  return false;
}

bool SysCDScan(void *fh, uint8 start_m, uint8 start_s, uint8 start_f,
               bool reverse) {
  bmx::mac68k::debug_log(
      "mac68k: SysCDScan: handle=%p %u:%u:%u reverse=%d (no CD, false)", fh,
      start_m, start_s, start_f, (int)reverse);
  return false;
}

void SysCDSetVolume(void *fh, uint8 left, uint8 right) {
  bmx::mac68k::debug_log("mac68k: SysCDSetVolume: handle=%p %u/%u (log-only)", fh,
                         left, right);
}

void SysCDGetVolume(void *fh, uint8 &left, uint8 &right) {
  left = 0;
  right = 0;
  bmx::mac68k::debug_log("mac68k: SysCDGetVolume: handle=%p -> 0/0", fh);
}
