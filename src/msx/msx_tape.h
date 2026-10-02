//
// msx_tape.h
//
// MSX cassette (.cas) via the vendored BIOS trap path: TAPION (PC=E1h)
// and TAPIN (PC=E4h) are emulated by machines/msx.c straight from the
// file (instant, exact for standard BIOS loads). This module owns the
// tape globals the vendored code expects (tapefile, ptr_mycinta,
// tape_loadsave_inserted) plus attach/detach/transport state.
//

#ifndef msx_tape_h
#define msx_tape_h

namespace bmx {
namespace msx {

// Attach a .cas file for loading (0 ok, -1 reject). Opens the file for
// the vendored trap loader.
int tape_attach(const char *path);
void tape_detach(void);
bool tape_attached(void);
const char *tape_name(void);
void tape_rewind(void);
void tape_set_playing(bool playing);
bool tape_playing(void);

}  // namespace msx
}  // namespace bmx

#endif
