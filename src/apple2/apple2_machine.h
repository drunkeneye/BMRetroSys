//
// apple2_machine.h
//
// Model table + reset. Models are KEGS ROM images in /roms/APPLE2;
// switching re-points g_cfg_rom_path and reloads (the core derives the
// machine from the ROM size: 32K //e, 128K ROM 01, 256K ROM 3).
//

#ifndef apple2_machine_h
#define apple2_machine_h

namespace bmx {
namespace apple2 {

struct Apple2Model {
  const char *name;
  const char *rom;   // pool file in /roms/APPLE2
  int rom_size;      // expected bytes (0 = any 64K-256K)
};

int machine_model_count(void);
const Apple2Model *machine_model(int index);
int machine_current_index(void);
bool machine_init_model(int index);
void machine_note_core_up(void);
void machine_reset(bool soft);
const char *machine_rom_dir(void);

}  // namespace apple2
}  // namespace bmx

#endif
