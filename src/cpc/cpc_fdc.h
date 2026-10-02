//
// cpc_fdc.h
//
// uPD765A floppy controller (programmed-I/O subset) + two drive slots.
// Synchronous model: bytes are always ready (firmware polls RQM and
// proceeds), seeks complete instantly, no timeouts. Status semantics
// mirror Caprice32 (proven against real firmware): successful reads end
// with AT+EC, missing sectors with AT+ND, and so on.
//

#ifndef cpc_fdc_h
#define cpc_fdc_h

#include <cstddef>

namespace bmx {
namespace cpc {

// Reset FDC state (phase, pending seeks; heads stay, motor off).
void fdc_reset(void);
// Floppy motor flipflop (port FA7E bit0, shared by both drives).
void fdc_set_motor(int on);
int fdc_motor(void);
// Drive slots 0 = A, 1 = B. Attach parses the file (0 ok, -1 reject);
// detach frees. Name returns the attached path or "".
int fdc_attach(int drive, const char *path);
void fdc_detach(int drive);
const char *fdc_attached_name(int drive);
bool fdc_attached(int drive);
// Boot file for autostart (DISC.BAS > first *.BAS > first file).
// Writes the name without quotes, returns 0; -1 when nothing found.
int fdc_boot_file(int drive, char *out, size_t out_size);

}  // namespace cpc
}  // namespace bmx

#endif
