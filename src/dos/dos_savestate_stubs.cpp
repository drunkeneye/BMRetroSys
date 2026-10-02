//
// dos_savestate_stubs.cpp
//
// SaveState backend for BMX bare-metal.
//
// Upstream's savestates.cpp is excluded from the subset (it drags in
// mapper/render/SDL-audio chains), but ~18 vendored TUs register
// SerializeGlobalPOD components whose constructor calls
// SaveState::instance().registerComponent(). The Serialize/POD helpers
// themselves are header-inline in dosbox.h; the registry and the slot
// methods live here.
//
// Snapshots: the slot API stays a no-op (no mapper bindings reference it).
// The menu path (emux_save/load_state) uses the BMX-owned file format in
// dos_snapshot.h: magic + component blobs, like the Atari port's states.
// The guest must be paused while these run (the menu trap guarantees it).
//

#include "dosbox.h"
#include "dos_snapshot.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace {

// Own registry: registerComponent captures every component; the slot API
// below ignores them, snapshot_save/load_to_file serializes them.
std::map<std::string, SaveState::Component *> &SnapshotRegistry(void) {
  static std::map<std::string, SaveState::Component *> registry;
  return registry;
}

const char kSnapshotMagic[10] = {'B', 'M', 'X', 'D', 'O',
                                 'S', 'N', 'A', 'P', '1'};
// component count / name length / payload sanity caps (fail, don't OOM).
const uint32_t kSnapshotMaxComponents = 10000U;
const uint32_t kSnapshotMaxNameBytes = 1024U;
const uint64_t kSnapshotMaxPayloadBytes = UINT64_C(1) << 31;

void WriteU16LE(std::string &out, uint32_t v) {
  out.push_back(static_cast<char>(v & 0xffU));
  out.push_back(static_cast<char>((v >> 8) & 0xffU));
}

void WriteU32LE(std::string &out, uint32_t v) {
  for (int i = 0; i < 4; i++) {
    out.push_back(static_cast<char>((v >> (i * 8)) & 0xffU));
  }
}

void WriteU64LE(std::string &out, uint64_t v) {
  for (int i = 0; i < 8; i++) {
    out.push_back(static_cast<char>((v >> (i * 8)) & 0xffU));
  }
}

bool ReadExact(FILE *fp, void *dst, size_t n) {
  return n == 0 || fread(dst, 1, n, fp) == n;
}

uint32_t ReadU32LE(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

uint64_t ReadU64LE(const uint8_t *p) {
  uint64_t v = 0U;
  for (int i = 0; i < 8; i++) {
    v |= (uint64_t)p[i] << (i * 8);
  }
  return v;
}

}  // namespace

SaveState &SaveState::instance() {
  static SaveState singleton;
  return singleton;
}

void SaveState::registerComponent(const std::string &uniqueName,
                                  Component &comp) {
  SnapshotRegistry()[uniqueName] = &comp;
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

namespace bmx {
namespace dos {

bool snapshot_save_to_file(const char *path) {
  if (path == nullptr || *path == '\0') {
    return false;
  }
  FILE *fp = nullptr;
  try {
    // Serialize every component into heap strings first, so a throwing
    // component fails before any file is touched.
    std::vector<std::pair<std::string, std::string> > blobs;
    for (std::map<std::string, SaveState::Component *>::iterator it =
             SnapshotRegistry().begin();
         it != SnapshotRegistry().end(); ++it) {
      if (it->second == nullptr) {
        return false;
      }
      std::ostringstream stream;
      it->second->getBytes(stream);
      blobs.push_back(
          std::make_pair(it->first, stream.str()));
    }
    fp = fopen(path, "wb");
    if (fp == nullptr) {
      return false;
    }
    bool ok = fwrite(kSnapshotMagic, 1, sizeof(kSnapshotMagic), fp) ==
                  sizeof(kSnapshotMagic);
    std::string header;
    WriteU32LE(header, (uint32_t)blobs.size());
    if (ok) {
      ok = fwrite(header.data(), 1, header.size(), fp) == header.size();
    }
    for (size_t i = 0; ok && i < blobs.size(); i++) {
      const std::string &name = blobs[i].first;
      const std::string &payload = blobs[i].second;
      std::string entry;
      WriteU16LE(entry, (uint32_t)name.size());
      entry.append(name);
      WriteU64LE(entry, (uint64_t)payload.size());
      ok = fwrite(entry.data(), 1, entry.size(), fp) == entry.size();
      if (ok && !payload.empty()) {
        ok = fwrite(payload.data(), 1, payload.size(), fp) == payload.size();
      }
    }
    fclose(fp);
    fp = nullptr;
    if (!ok) {
      remove(path);
      return false;
    }
    return true;
  } catch (...) {
    if (fp != nullptr) {
      fclose(fp);
    }
    remove(path);
    return false;
  }
}

bool snapshot_load_from_file(const char *path) {
  if (path == nullptr || *path == '\0') {
    return false;
  }
  FILE *fp = nullptr;
  try {
    fp = fopen(path, "rb");
    if (fp == nullptr) {
      return false;
    }
    char magic[sizeof(kSnapshotMagic)];
    uint8_t count_buf[4];
    bool ok = ReadExact(fp, magic, sizeof(magic)) &&
              ReadExact(fp, count_buf, sizeof(count_buf)) &&
              memcmp(magic, kSnapshotMagic, sizeof(magic)) == 0;
    uint32_t count = ok ? ReadU32LE(count_buf) : 0U;
    if (count > kSnapshotMaxComponents) {
      ok = false;
    }
    std::map<std::string, SaveState::Component *> &registry =
        SnapshotRegistry();
    uint32_t applied = 0U;
    for (uint32_t i = 0; ok && i < count; i++) {
      uint8_t name_len_buf[2];
      uint8_t size_buf[8];
      ok = ReadExact(fp, name_len_buf, sizeof(name_len_buf));
      const uint32_t name_len =
          ok ? (uint32_t)name_len_buf[0] | ((uint32_t)name_len_buf[1] << 8)
             : 0U;
      std::string name;
      if (ok && (name_len == 0U || name_len > kSnapshotMaxNameBytes)) {
        ok = false;
      }
      if (ok) {
        name.resize(name_len);
        ok = ReadExact(fp, &name[0], name_len) &&
             ReadExact(fp, size_buf, sizeof(size_buf));
      }
      const uint64_t payload_size = ok ? ReadU64LE(size_buf) : 0U;
      std::string payload;
      if (ok && payload_size > kSnapshotMaxPayloadBytes) {
        ok = false;
      }
      if (ok && payload_size > 0U) {
        payload.resize((size_t)payload_size);
        ok = ReadExact(fp, &payload[0], (size_t)payload_size);
      }
      if (!ok) {
        break;
      }
      std::map<std::string, SaveState::Component *>::iterator found =
          registry.find(name);
      if (found == registry.end() || found->second == nullptr) {
        continue;  // forward compatibility: skip unknown entries
      }
      std::istringstream stream(payload);
      found->second->setBytes(stream);
      applied++;
    }
    fclose(fp);
    fp = nullptr;
    // At least one known component must have applied; otherwise this is
    // not a snapshot of this machine (or the registry is empty).
    return ok && applied > 0U;
  } catch (...) {
    if (fp != nullptr) {
      fclose(fp);
    }
    return false;
  }
}

}  // namespace dos
}  // namespace bmx
