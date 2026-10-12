//
// bbc_config.h
//
// Model table lives in bbc_machine; this covers the TV mode latch and the
// /roms/BBC pool scan (Circle FATFS via opendir, mirrors cpc_config).
//

#ifndef BMX_BBC_CONFIG_H
#define BMX_BBC_CONFIG_H

#include <stddef.h>

namespace bmx {
namespace bbc {

struct Config {
  const char *tv_mode;
};

bool config_init(const Config &config);
bool config_is_ntsc(void);
bool config_is_ready(void);

const char *config_rom_dir(void);
int config_rom_count(void);
const char *config_rom_name(int index);
void config_rom_path(int index, char *out, size_t out_size);
void config_rescan_roms(void);

}  // namespace bbc
}  // namespace bmx

#endif  // BMX_BBC_CONFIG_H
