//
// zx_machine.h
//
// ZX Spectrum model management for BMX bare-metal: model table (menu +
// machine IDs), spectrum-only machine init (replicating the Spectrum
// branches of set_machine/malloc_mem_machine without the all-machine
// cascade), ROM loading through the real rom_load(), and reset.
//

#ifndef zx_machine_h
#define zx_machine_h

namespace bmx {
namespace zxspectrum {

struct ZxModel {
  const char *name;   // menu label
  int id;             // ZEsarUX current_machine_type value
  const char *rom;    // ROM file in /roms/ZXSPECTRUM
  int rom_size;       // expected bytes
};

int machine_model_count(void);
const ZxModel *machine_model(int index);
// Index of the model running, 0-based into the table above.
int machine_current_index(void);

// (Re)initialise the core for the given model index. Cold-boots the CPU.
// Returns false when the ROM is missing or bad.
bool machine_init_model(int index);
// Soft (1) / hard (0) reset of the current model.
void machine_reset(bool soft);
// Re-apply paging after a reset for 128k-class models.
void machine_fix_paging(void);

}  // namespace zxspectrum
}  // namespace bmx

#endif
