//
// zx_config.h
//
// ZX Spectrum machine configuration for BMX bare-metal.
//

#ifndef zx_config_h
#define zx_config_h

namespace bmx {
namespace zxspectrum {

struct Config {
  const char *tv_mode;  // "-ntsc" or "-pal" from the BMX timing option
};

bool config_init(const Config &config);
bool config_is_ntsc(void);
bool config_is_ready(void);

// ROM pool: scan results for the System ROMs menu + rescan hook.
int config_rom_count(void);
const char *config_rom_name(int index);
const char *config_rom_path(int index);
void config_rescan_roms(void);

}  // namespace zxspectrum
}  // namespace bmx

#endif
