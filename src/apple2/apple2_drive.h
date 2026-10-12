//
// apple2_bridge.h
//
// (forward declarations live here; the emux API table is apple2_bridge.cpp)
//

#ifndef apple2_drive_h
#define apple2_drive_h

namespace bmx {
namespace apple2 {

// Slot/drive -> attached image path ("" when empty). Slots: 6 = 5.25" x2,
// 5 = 3.5", 7 = SmartPort HDD.
const char *drive_attached_name(int slot, int drive);
// Attach (returns 0) / detach a disk image.
int drive_attach(int slot, int drive, const char *filename);
void drive_detach(int slot, int drive);

}  // namespace apple2
}  // namespace bmx

#endif
