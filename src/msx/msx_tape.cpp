//
// msx_tape.cpp
//
// See msx_tape.h.
//

#include "msx_tape.h"
#include "msx_debug.h"
#include "msx_sound.h"

#include <stdio.h>
#include <string.h>

// Tape globals declared in storage/tape.h, defined nowhere in the
// vendored subset: owned here.
char *tapefile = nullptr;
FILE *ptr_mycinta = nullptr;
int tape_loadsave_inserted = 0;

namespace bmx {
namespace msx {

namespace {

char s_path[256] = "";
bool s_playing = false;

void close_file(void) {
  if (ptr_mycinta != nullptr) {
    fclose(ptr_mycinta);
    ptr_mycinta = nullptr;
  }
}

}  // namespace

int tape_attach(const char *path) {
  if (path == nullptr || path[0] == '\0') {
    return -1;
  }
  FILE *fp = fopen(path, "rb");
  if (fp == nullptr) {
    debug_log("tape: cannot open %s", path);
    return -1;
  }
  // Minimal .cas sanity: starts with the 8-byte sync block signature.
  unsigned char sig[8];
  size_t got = fread(sig, 1, sizeof(sig), fp);
  fclose(fp);
  static const unsigned char kSig[8] = {0x1F, 0xA6, 0xDE, 0xBA,
                                        0xCC, 0x13, 0x7D, 0x74};
  if (got != sizeof(sig) || memcmp(sig, kSig, sizeof(sig)) != 0) {
    debug_log("tape: bad .cas signature %s", path);
    return -1;
  }
  tape_detach();
  // Hand the path to the vendored trap loader (it fopens on demand).
  static char s_tapefile[256];
  snprintf(s_tapefile, sizeof(s_tapefile), "%s", path);
  tapefile = s_tapefile;
  tape_loadsave_inserted = 1;  // TAPE_LOAD_INSERTED
  snprintf(s_path, sizeof(s_path), "%s", path);
  s_playing = true;
  debug_log("tape: attached %s", path);
  return 0;
}

void tape_detach(void) {
  close_file();
  tapefile = nullptr;
  tape_loadsave_inserted = 0;
  s_path[0] = '\0';
  s_playing = false;
}

bool tape_attached(void) { return s_path[0] != '\0'; }

const char *tape_name(void) { return s_path; }

void tape_rewind(void) {
  // Trap loader reads forward from open; reopen resets to the start.
  close_file();
  debug_log("tape: rewound");
}

void tape_set_playing(bool playing) { s_playing = playing; }

bool tape_playing(void) { return s_playing; }

}  // namespace msx
}  // namespace bmx
