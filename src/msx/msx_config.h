//
// msx_config.h
//
// MSX configuration: TV mode latch + system ROM pool scan (/roms/MSX).
//

#ifndef msx_config_h
#define msx_config_h

#include <stddef.h>

namespace bmx {
namespace msx {

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

}  // namespace msx
}  // namespace bmx

#endif
