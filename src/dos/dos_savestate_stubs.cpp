//
// dos_savestate_stubs.cpp
//
// No-op SaveState backend for BMX bare-metal (Phase 1).
//
// Upstream's savestates.cpp is excluded from the Phase-1 subset (it drags
// in mapper/render/SDL-audio chains), but ~18 vendored TUs register
// SerializeGlobalPOD components whose constructor calls
// SaveState::instance().registerComponent(). The Serialize/POD helpers
// themselves are header-inline in dosbox.h; only the SaveState methods
// need definitions at link time, provided here as no-ops.
//
// Real snapshot save/load arrives in Phase 3 via emux_save/load_state
// (BMX owns the file format, like the Atari port's .ats states).
//

#include "dosbox.h"

SaveState &SaveState::instance() {
  static SaveState singleton;
  return singleton;
}

void SaveState::registerComponent(const std::string &uniqueName,
                                  Component &comp) {
  (void)uniqueName;
  (void)comp;
  // No backend: components are constructed but never serialized.
}

void SaveState::save(size_t slot) {
  (void)slot;
}

void SaveState::load(size_t slot) const {
  (void)slot;
}

bool SaveState::isEmpty(size_t slot) const {
  (void)slot;
  return true;
}

void SaveState::removeState(size_t slot) const {
  (void)slot;
}

std::string SaveState::getName(size_t slot, bool nl) const {
  (void)slot;
  (void)nl;
  return std::string();
}
