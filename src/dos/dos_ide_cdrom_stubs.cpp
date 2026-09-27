//
// dos_ide_cdrom_stubs.cpp
//
// BMX-owned link stubs: FUNCTION symbols owned by deleted (non-vendored)
// DOSBox-X translation units (hardware/ide.cpp, dos/dos_mscdex.cpp).
// Phase-1 behavior is inert no-op: no IDE controllers and no MSCDEX driver
// exist (both backends are non-goals until Phase 2+), so every stub mirrors
// the upstream "backend absent" path and touches no hardware.
//
// Included upstream headers (ide.h, cdrom.h) make the compiler check the
// IDE/GetMSCDEXDrive signatures against their declarations; the MSCDEX
// entry points have no public header (call sites declare them locally),
// so those signatures are verified by the link against the mangled names
// the vendored objects expect:
//
//   MSCDEX_AddDrive(char, const char*, uint8_t&) -> int
//   MSCDEX_GetVolumeName(uint8_t, char*) -> bool
//   MSCDEX_HasMediaChanged(uint8_t) -> bool
//   MSCDEX_RemoveDrive(char) -> int
//   MSCDEX_Reset(Section*) -> void
//   MSCDEX_SetCDInterface(int, int) -> void
//
// NOTE: RENDER_DrawLine is NOT stubbed here. include/render.h declares it
// as `extern ScalerLineHandler_t` (a function pointer = DATA), so it is
// defined (nullptr) in dos_globals.cpp; dos_video.cpp assigns the
// real handler in Phase 2.
//
// Never add vendored .cpp/.h edits for these; this file is the owner.
//

#include "dosbox.h"
#include "setup.h"
#include "ide.h"
#include "cdrom.h"

#include <iosfwd>

// ---- Deleted owner: src/hardware/ide.cpp ----

void IDE_CDROM_Attach(signed char index, bool slave, unsigned char drive_index) {
  (void)index;
  (void)slave;
  (void)drive_index;
  // No IDE controllers on BMX Phase 1: attach is a no-op.
}

void IDE_CDROM_Detach(unsigned char drive_index) {
  (void)drive_index;
  // No IDE controllers on BMX Phase 1: detach is a no-op.
}

void IDE_CDROM_DetachAll() {
  // No IDE controllers on BMX Phase 1: nothing to detach.
}

void IDE_CDROM_Detach_Ret(signed char &indexret, bool &slaveret,
                           unsigned char drive_index) {
  (void)drive_index;
  // Mirror the upstream miss path (ide.cpp): no device found.
  indexret = -1;
  slaveret = false;
}

// ---- Deleted owner: src/dos/dos_mscdex.cpp ----

int MSCDEX_AddDrive(char driveLetter, const char *physicalPath, uint8_t &subUnit) {
  (void)driveLetter;
  (void)physicalPath;
  (void)subUnit;
  // Mirror the upstream mscdex==NULL path (dos_mscdex.cpp): driver absent.
  return 6;
}

bool MSCDEX_GetVolumeName(uint8_t subUnit, char *name) {
  (void)subUnit;
  (void)name;
  // No MSCDEX driver on BMX Phase 1: no volume name available.
  return false;
}

bool MSCDEX_HasMediaChanged(uint8_t subUnit) {
  (void)subUnit;
  // No MSCDEX driver on BMX Phase 1: no media to change.
  return false;
}

int MSCDEX_RemoveDrive(char driveLetter) {
  (void)driveLetter;
  // Mirror the upstream !mscdex path (dos_mscdex.cpp): nothing to remove.
  return 0;
}

void MSCDEX_Reset(Section * /*sec*/) {
  // No MSCDEX driver on BMX Phase 1: nothing to tear down.
}

void MSCDEX_SetCDInterface(int intNr, int numCD) {
  (void)intNr;
  (void)numCD;
  // No CD interfaces on BMX Phase 1: interface selection is a no-op.
}

void MSCDEX_Init() {
  // Currently unreferenced by the Phase-1 subset (no vendored object
  // requires it); provided so the no-op MSCDEX backend is complete.
  // The Section* overload declared by dosbox.cpp is likewise unreferenced.
}

bool GetMSCDEXDrive(unsigned char drive_letter, CDROM_Interface **ret_cdrom) {
  (void)drive_letter;
  if (ret_cdrom != nullptr) *ret_cdrom = nullptr;
  // No MSCDEX driver on BMX Phase 1: never resolves a drive.
  return false;
}

void POD_Save_DOS_Mscdex(std::ostream &stream) {
  (void)stream;
  // No MSCDEX state exists on BMX Phase 1: nothing to serialize.
  // (SaveState component registration itself is stubbed in
  // dos_savestate_stubs.cpp; only the MSCDEX payload helpers live here.)
}

void POD_Load_DOS_Mscdex(std::istream &stream) {
  (void)stream;
  // No MSCDEX state exists on BMX Phase 1: nothing to deserialize.
}
