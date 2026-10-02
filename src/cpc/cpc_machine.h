//
// cpc_machine.h
//
// CPC model management: 464 / 664 / 6128 subset init (no all-machine
// cascade), ROM loading with size checks, Z80 + chip reset.
//

#ifndef cpc_machine_h
#define cpc_machine_h

namespace bmx {
namespace cpc {

struct CpcModel {
  const char *name;
  int id;  // MACHINE_ID_CPC_* from the vendored cpu.h
  const char *rom;
  int rom_size;  // 32768 (OS+BASIC) or 49152 (OS+BASIC+AMSDOS)
  int is_128k;
  int has_floppy;  // FDC ports decoded (stubbed device in v1)
};

int machine_model_count(void);
const CpcModel *machine_model(int index);
int machine_current_index(void);
// Select + init a model (loads ROMs, inits memory/paging/CPU/chips).
bool machine_init_model(int index);
// Soft reset to the ROM entry (paging + CPU + chips, RAM preserved).
void machine_reset(bool soft);
const char *machine_rom_dir(void);

}  // namespace cpc
}  // namespace bmx

#endif
