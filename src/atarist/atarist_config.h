//
// atarist_config.h
//
// Hatari core configuration for BMX bare-metal (Phase 0 placeholder).
//

#ifndef atarist_config_h
#define atarist_config_h

namespace bmx {
namespace atarist {

struct Config {
  // "-ntsc" or "-pal" (matches the machine timing option used by BMX).
  const char *tv_mode;
};

// Initializes the Hatari core. Phase 0: records the request, no core yet.
bool config_init(const Config &config);

// True if the emulator is configured for NTSC.
bool config_is_ntsc(void);

// True once config_init() has completed.
bool config_is_ready(void);

// TOS pool (/roms/ATARIST): *.img discovery, EmuTOS first.
// The menu stages an index; config_init boots it, config_apply_tos_reset
// switches it live with a cold reset.
int config_tos_count(void);
const char *config_tos_name(int index);
const char *config_tos_path(int index);
void config_set_tos_index(int index);
int config_tos_index(void);
// Switches to the staged TOS (plus matching machine type) and cold-resets.
// Returns false when the image fails to load (old TOS stays running).
bool config_apply_tos_reset(void);

}  // namespace atarist
}  // namespace bmx

#endif
