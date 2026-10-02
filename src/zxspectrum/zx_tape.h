//
// zx_tape.h
//
// TAP trap loading + SNA snapshots for BMX bare-metal. Only the symbols
// referenced by the reduced core are exposed with C linkage; everything
// else is driven by the emux bridge (attach/detach/autostart).
//

#ifndef zx_tape_h
#define zx_tape_h

#include <stdio.h>

namespace bmx {
namespace zxspectrum {

// Attach a tape file (.tap fully; .tzx standard blocks). Returns 0 on ok.
int tape_attach(const char *path);
// Detach (also on model change).
void tape_detach(void);
bool tape_attached(void);
const char *tape_name(void);
// Rewind to the first block.
void tape_rewind(void);
// Autostart warp: when armed, warp ends automatically at end of tape.
void tape_set_autostart_warp(bool armed);

// SNA snapshots (48k fully; 128k load). Returns 0 on ok.
int snapshot_load(const char *path);
int snapshot_save(const char *path);

}  // namespace zxspectrum
}  // namespace bmx

#endif
