//
// mac128k_config.h
//
// Mac 128K machine configuration for BMX bare-metal.
//
// The model is fixed (Macintosh 128K, Mini vMac -m 128K): 128KB RAM,
// 64KB ROM, 512x342 1-bit video, classic keyboard/sound. What varies is
// the ROM image (both known 128K revisions are accepted) and the two
// floppy disk images. This TU scans the pools and stages selections that
// take effect after Reset.
//

#ifndef mac128k_config_h
#define mac128k_config_h

namespace bmx {
namespace mac128k {

struct Config {
  const char *tv_mode;  // "-pal" or similar; kept for core parity
  Config() : tv_mode(nullptr) {}
};

bool config_init(const Config &config);
bool config_is_ready(void);

// ROM pool (/roms/MAC128K): 64KB files only.
int mac128k_rom_count(void);
const char *mac128k_rom_name(int i);  // basename, e.g. "Mac128K.ROM"
const char *mac128k_rom_path(int i);  // full path
int mac128k_rom_current(void);
void mac128k_rom_set(int i);
// Staged ROM for the next boot (nullptr when the pool is empty).
const char *mac128k_rom_staged_path(void);

}  // namespace mac128k
}  // namespace bmx

#endif
