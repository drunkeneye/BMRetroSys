//
// mac68k_snapshot.h
//
// BMX-owned Mac68k snapshot helpers (Phase 0 placeholder: inline stubs, no
// core state to serialize yet).
//

#ifndef mac68k_snapshot_h
#define mac68k_snapshot_h

namespace bmx {
namespace mac68k {

// Registers a save-state component. Phase 0: accepted, no-op.
inline bool snapshot_register(const char *name) {
  (void)name;
  return true;
}

// Write a snapshot of all registered components to path.
// Phase 0: no core state yet, always fails.
inline bool snapshot_save_to_file(const char *path) {
  (void)path;
  return false;
}

// Restore a snapshot written by snapshot_save_to_file.
// Phase 0: no core state yet, always fails.
inline bool snapshot_load_from_file(const char *path) {
  (void)path;
  return false;
}

}  // namespace mac68k
}  // namespace bmx

#endif
