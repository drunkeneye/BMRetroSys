//
// bbc_drive.cpp
//
// See bbc_drive.h. 1770 DFS drives 0/1: .ssd/.dsd via Storage::Disk::SSD,
// .adf via AcornADF (needs the ADFS model). Both drives are re-sent on
// every change (see BMX PATCH P1 in the vendored insert_media).
//

#include "bbc_drive.h"
#include "bbc_debug.h"
#include "bbc_machine.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <filesystem>
#include <memory>
#include <string>

// CLK core (vendored subset under third_party/bbc).
#include "Analyser/Static/StaticAnalyser.hpp"
#include "Machines/Acorn/BBCMicro/BBCMicro.hpp"
#include "Machines/MediaTarget.hpp"
#include "Storage/Disk/DiskImage/DiskImage.hpp"
#include "Storage/Disk/DiskImage/Formats/AcornADF.hpp"
#include "Storage/Disk/DiskImage/Formats/SSD.hpp"
#include "Storage/FileHolder.hpp"

namespace bmx {
namespace bbc {

namespace clk {
BBCMicro::Machine *machine_ptr(void);
}  // namespace clk

namespace {

std::shared_ptr<Storage::Disk::Disk> s_disks[2];
char s_names[2][256];

bool has_suffix(const char *name, const char *suffix) {
  if (name == nullptr || suffix == nullptr) {
    return false;
  }
  size_t n = strlen(name);
  size_t s = strlen(suffix);
  if (s > n) {
    return false;
  }
  return strcasecmp(name + n - s, suffix) == 0;
}

void push_both(void) {
  BBCMicro::Machine *m = clk::machine_ptr();
  if (m == nullptr) {
    debug_log("drive: push with no machine, skipped");
    return;
  }
  Analyser::Static::Media media;
  // Always both slots (nulls eject); otherwise detaching one drive would
  // leave the other's stale disk in place (PATCH P1 maps by index).
  media.disks.push_back(s_disks[0]);
  media.disks.push_back(s_disks[1]);
  const bool ok =
      dynamic_cast<MachineTypes::MediaTarget *>(m)->insert_media(media);
  debug_log("drive: pushed d0=%s d1=%s (%s)", s_names[0], s_names[1],
            ok ? "ok" : "refused");
}

}  // namespace

const char *drive_attached_name(int drive) {
  if (drive < 0 || drive > 1) {
    return "";
  }
  return s_names[drive];
}

int drive_attach(int drive, const char *filename) {
  if (filename == nullptr || filename[0] == '\0') {
    return -1;
  }
  if (drive < 0 || drive > 1) {
    return -1;
  }
  std::shared_ptr<Storage::Disk::Disk> disk;
  const bool want_adf = has_suffix(filename, ".adf");
  // Pre-validate with the same rules as the CLK parsers (whose error
  // enums are private and can't be caught by name): SSD needs a
  // 256-byte multiple, 512 B .. 400 KiB. Anything else is refused here
  // with a clear reason instead of a bare parser exception.
  {
    FILE *probe = fopen(filename, "rb");
    if (probe == nullptr) {
      debug_log("drive: attach failed d%d %s (cannot open)", drive,
                filename);
      return -1;
    }
    fseek(probe, 0, SEEK_END);
    long size = ftell(probe);
    fclose(probe);
    if (!want_adf &&
        ((size & 255) != 0 || size < 512 || size > 80 * 2 * 10 * 256)) {
      debug_log("drive: attach failed d%d %s (%ld bytes: not a BBC "
                "DFS disc image; want .ssd/.dsd)",
                drive, filename, size);
      return -1;
    }
  }
  try {
    const std::filesystem::path path(filename);
    if (want_adf) {
      disk = std::make_shared<
          Storage::Disk::DiskImageHolder<Storage::Disk::AcornADF>>(path);
    } else {
      // .ssd/.dsd (and DFS-layout .dsk/.bbc/.img): DFS sector dump.
      disk = std::make_shared<
          Storage::Disk::DiskImageHolder<Storage::Disk::SSD>>(path);
    }
  } catch (const std::exception &e) {
    debug_log("drive: attach failed d%d %s (%s)", drive, filename,
              e.what());
    return -1;
  } catch (...) {
    debug_log("drive: attach failed d%d %s (disc parser rejected it)",
              drive, filename);
    return -1;
  }
  if (!disk) {
    debug_log("drive: attach failed d%d %s (null disk)", drive, filename);
    return -1;
  }
  s_disks[drive] = disk;
  snprintf(s_names[drive], sizeof(s_names[drive]), "%s", filename);
  debug_log("drive: attached d%d %s", drive, filename);
  push_both();
  return 0;
}

void drive_detach(int drive) {
  if (drive < 0 || drive > 1) {
    return;
  }
  s_disks[drive].reset();
  s_names[drive][0] = '\0';
  debug_log("drive: ejected d%d", drive);
  push_both();
}

void drive_repush_all(void) {
  debug_log("drive: repush d0=%s d1=%s", s_names[0], s_names[1]);
  push_both();
}

}  // namespace bbc
}  // namespace bmx
