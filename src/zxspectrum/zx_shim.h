//
// zx_shim.h
//
// BMX replacements for the ZEsarUX modules that are not compiled:
// debug.c, utils.c (subset), timer.c is in zx_timer, tape.c trap symbols
// are in zx_tape. This header exposes the few helpers owned here.
//

#ifndef zx_shim_h
#define zx_shim_h

namespace bmx {
namespace zxspectrum {

// Verbose core logging (VERBOSE_DEBUG) only when enabled; errors always.
void shim_set_verbose(bool enabled);
bool shim_verbose(void);
// Allocate the visualmem debugger buffers the compiled peek/poke/fetch
// path writes into (EMULATE_VISUALMEM is on upstream-style).
void shim_init_visualmem(void);

}  // namespace zxspectrum
}  // namespace bmx

#endif
