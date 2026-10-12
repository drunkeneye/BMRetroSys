//
// apple2_drive.cpp
//
// Disk attach/detach over the KEGS IWM (slots 5/6/7). See apple2_drive.h.
//

#include "apple2_drive.h"
#include "apple2_debug.h"

#include <stddef.h>
#include <string.h>

// KEGS core globals (global-scope externs; see the header). defc.h pulls
// in iwm.h (unguarded upstream); do not include iwm.h directly.
#include "apple2_kegs_globals.h"

namespace bmx {
namespace apple2 {

const char *drive_attached_name(int slot, int drive) {
  if (slot < 5 || slot > 7 || drive < 0 || drive > 1) {
    return "";
  }
  Disk *dsk = iwm_get_dsk_from_slot_drive(slot, drive);
  if (dsk == nullptr || dsk->name_ptr == nullptr ||
      dsk->name_ptr[0] == '\0') {
    return "";
  }
  // Ejected (fd < 0 with a stale name) reads as empty.
  if (dsk->fd < 0) {
    return "";
  }
  return dsk->name_ptr;
}

int drive_attach(int slot, int drive, const char *filename) {
  if (filename == nullptr || filename[0] == '\0') {
    return -1;
  }
  if (slot < 5 || slot > 7 || drive < 0 || drive > 1) {
    return -1;
  }
  // ejected=0, no partition, default blocks (mirrors
  // cfg_maybe_insert_disk for plain images).
  insert_disk(slot, drive, filename, 0, 0, -1, 0);
  Disk *dsk = iwm_get_dsk_from_slot_drive(slot, drive);
  if (dsk == nullptr || dsk->fd < 0) {
    debug_log("drive: attach failed s%dd%d %s", slot, drive + 1, filename);
    return -1;
  }
  debug_log("drive: attached s%dd%d %s", slot, drive + 1, filename);
  return 0;
}

void drive_detach(int slot, int drive) {
  if (slot < 5 || slot > 7 || drive < 0 || drive > 1) {
    return;
  }
  insert_disk(slot, drive, "", 1, 0, 0, 0);
  debug_log("drive: ejected s%dd%d", slot, drive + 1);
}

}  // namespace apple2
}  // namespace bmx
