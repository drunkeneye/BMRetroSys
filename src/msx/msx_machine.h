//
// msx_machine.h
//
// MSX model table (MSX1 BIOS variants), ROM loading, Z80/VDP/AY reset.
//

#ifndef msx_machine_h
#define msx_machine_h

namespace bmx {
namespace msx {

struct MsxModel {
  const char *name;
  const char *rom;  // 32K BIOS+BASIC in /roms/MSX
};

int machine_model_count(void);
const MsxModel *machine_model(int index);
int machine_current_index(void);
const char *machine_rom_dir(void);
bool machine_init_model(int index);
void machine_reset(bool soft);

}  // namespace msx
}  // namespace bmx

#endif
