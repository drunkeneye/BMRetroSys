//
// dos_snapshot.h
//
// BMX-owned DOS snapshot format (see dos_savestate_stubs.cpp). The menu
// passes absolute snapshot paths via emux_save/load_state; these helpers
// serialize every registered SaveState component to that path.
//

#ifndef dos_snapshot_h
#define dos_snapshot_h

namespace bmx {
namespace dos {

// Write a snapshot of all registered components to path (heap buffers
// only, never the task stack). Returns false on any I/O or component
// error, leaving no partial file behind.
bool snapshot_save_to_file(const char *path);
// Restore a snapshot written by snapshot_save_to_file. Unknown entries
// are skipped (forward compatibility); a corrupt entry fails the load.
bool snapshot_load_from_file(const char *path);

}  // namespace dos
}  // namespace bmx

#endif
