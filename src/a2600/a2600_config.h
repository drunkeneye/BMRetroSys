//
// a2600_config.h
//
// 2600 kernel configuration: TV-mode latch (from machines.ini timing),
// master volume and console-switch defaults. The 2600 needs no BIOS ROM
// pool: cartridges carry everything.
//

#ifndef a2600_config_h
#define a2600_config_h

namespace bmx {
namespace a2600 {

struct Config {
  const char *tv_mode;  // "ntsc"/"pal" timing tag, may be NULL
};

bool config_init(const Config &config);
bool config_is_ready(void);
bool config_is_ntsc(void);

// Persisted knobs (settings-a2600.txt).
int config_get_volume(void);
void config_set_volume(int pct);
bool config_get_left_difficulty_a(void);
bool config_get_right_difficulty_a(void);
void config_set_left_difficulty_a(bool a);
void config_set_right_difficulty_a(bool a);

}  // namespace a2600
}  // namespace bmx

#endif
