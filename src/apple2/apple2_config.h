//
// apple2_config.h
//
// Apple II configuration: TV mode latch, /roms/APPLE2 pool scan, model
// (ROM) selection. The KEGS core picks the machine from the ROM image
// size (32K //e, 128K ROM 01, 256K ROM 3); the menu switches models by
// re-pointing g_cfg_rom_path and reloading (see apple2_machine).
//

#ifndef apple2_config_h
#define apple2_config_h

#include <stddef.h>

namespace bmx {
namespace apple2 {

struct Config {
  const char *tv_mode;
};

bool config_init(const Config &config);
bool config_is_ntsc(void);
bool config_is_ready(void);
int config_rom_count(void);
const char *config_rom_name(int index);
void config_rom_path(int index, char *out, size_t out_size);
void config_rescan_roms(void);
const char *config_rom_dir(void);

}  // namespace apple2
}  // namespace bmx

#endif
