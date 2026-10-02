//
// msx_cart.cpp
//
// See msx_cart.h.
//

#include "msx_cart.h"
#include "msx_debug.h"

#include <stdio.h>
#include <string.h>

extern "C" {
#include "cpu.h"
#include "msx.h"
}

namespace bmx {
namespace msx {

namespace {

char s_path[256] = "";

}  // namespace

int cart_attach(const char *path) {
  if (path == nullptr || path[0] == '\0') {
    return -1;
  }
  FILE *fp = fopen(path, "rb");
  if (fp == nullptr) {
    debug_log("cart: cannot open %s", path);
    return -1;
  }
  fseek(fp, 0, SEEK_END);
  long size = ftell(fp);
  fclose(fp);
  // Plain ROMs only in phase 1 (8/16/32/48K). Larger images need
  // verified mapper support (ASCII/Konami); refuse loudly instead of
  // booting garbage through the wrong mapper.
  if (size != 8192 && size != 16384 && size != 32768 && size != 49152) {
    debug_log("cart: bad size %ld %s (want 8/16/32/48K plain)", size,
              path);
    return -1;
  }
  msx_insert_rom_cartridge((char *)path);
  if (!msx_cartridge_inserted.v) {
    debug_log("cart: insert failed %s", path);
    return -1;
  }
  snprintf(s_path, sizeof(s_path), "%s", path);
  debug_log("cart: attached %s (%ld bytes)", path, size);
  return 0;
}

void cart_detach(void) {
  msx_empty_romcartridge_space();
  s_path[0] = '\0';
  debug_log("cart: detached");
}

bool cart_attached(void) { return s_path[0] != '\0'; }

const char *cart_name(void) { return s_path; }

}  // namespace msx
}  // namespace bmx
