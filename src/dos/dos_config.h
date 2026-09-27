//
// dos_config.h
//
// DOSBox-X core configuration for BMX bare-metal (Phase 0 placeholder)
//

#ifndef dos_config_h
#define dos_config_h

namespace bmx {
namespace dos {

struct Config {
  // "-ntsc" or "-pal" (matches the machine timing option used by BMX).
  const char *tv_mode;
};

// Initializes the DOSBox-X core. Phase 0: records the request, no core yet.
bool config_init(const Config &config);

// True if the emulator is configured for NTSC.
bool config_is_ntsc(void);

// True once config_init() has completed.
bool config_is_ready(void);

}  // namespace dos
}  // namespace bmx

#endif
