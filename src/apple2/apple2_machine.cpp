//
// apple2_machine.cpp
//
// See apple2_machine.h. ROM reload mirrors the core's own ROM-file
// change path (config.c: new path -> load_roms_init_memory + do_reset).
//

#include "apple2_machine.h"
#include "apple2_config.h"
#include "apple2_debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// KEGS core globals (global-scope externs; see the header).
#include "apple2_kegs_globals.h"

namespace bmx {
namespace apple2 {

namespace {

const Apple2Model kModels[] = {
    {"Apple IIgs (ROM 3)", "ROM.03", 262144},
    {"Apple IIgs (ROM 01)", "ROM.01", 131072},
    {"Apple //e", "APPLE2E.ROM", 32768},
};

int s_current = 0;
bool s_core_up = false;

bool rom_present(const char *name, int want_size) {
  char path[256];
  snprintf(path, sizeof(path), "%s/%s", config_rom_dir(), name);
  FILE *fp = fopen(path, "rb");
  if (fp == nullptr) {
    return false;
  }
  fseek(fp, 0, SEEK_END);
  long size = ftell(fp);
  fclose(fp);
  if (want_size != 0 && size != (long)want_size) {
    return false;
  }
  return size > 0;
}

}  // namespace

int machine_model_count(void) {
  return (int)(sizeof(kModels) / sizeof(kModels[0]));
}

const Apple2Model *machine_model(int index) {
  if (index < 0 || index >= machine_model_count()) {
    return &kModels[0];
  }
  return &kModels[index];
}

int machine_current_index(void) { return s_current; }

bool machine_init_model(int index) {
  if (index < 0 || index >= machine_model_count()) {
    return false;
  }
  const Apple2Model *m = &kModels[index];
  if (!rom_present(m->rom, m->rom_size)) {
    debug_log("machine: model '%s' needs %s (%d bytes) in %s", m->name,
              m->rom, m->rom_size, config_rom_dir());
    return false;
  }
  char path[256];
  snprintf(path, sizeof(path), "%s/%s", config_rom_dir(), m->rom);
  // The core keeps the pointer; hand it a fresh malloc'd copy like the
  // config-file path does (never a literal it might free).
  char *copy = (char *)malloc(strlen(path) + 1);
  if (copy == nullptr) {
    return false;
  }
  strcpy(copy, path);
  g_cfg_rom_path = copy;
  if (s_core_up) {
    load_roms_init_memory();
    do_reset();
    debug_log("machine: switched to '%s' (rom_version=%d)", m->name,
              g_rom_version);
  } else {
    debug_log("machine: pre-init model '%s'", m->name);
  }
  s_current = index;
  return true;
}

void machine_note_core_up(void) { s_core_up = true; }

void machine_reset(bool soft) {
  (void)soft;
  // KEGS has one reset path (full re-init + reboot); the Apple II
  // Ctrl+Reset distinction buys nothing headless, so both go through it.
  debug_log("machine_reset(%s) vbl=%lu", soft ? "soft" : "hard",
            (unsigned long)g_vbl_count);
  do_reset();
  debug_log("machine: reset done");
}

const char *machine_rom_dir(void) { return config_rom_dir(); }

}  // namespace apple2
}  // namespace bmx
