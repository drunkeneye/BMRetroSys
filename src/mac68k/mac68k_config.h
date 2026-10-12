//
// mac68k_config.h
//
// Basilisk II core configuration for BMX bare-metal (Phase 0 placeholder)
//

#ifndef mac68k_config_h
#define mac68k_config_h

namespace bmx {
namespace mac68k {

struct Config {
  // "-ntsc" or "-pal" (matches the machine timing option used by BMX).
  const char *tv_mode;
  // Guest RAM in MB (Basilisk II ramsize pref, in bytes upstream).
  int ramsize_mb = 8;
  // Basilisk II modelid (5 = Mac IIci, the default).
  int modelid = 5;
  // Basilisk II cpu pref (3 = 68030 with FPU, the default).
  int cpu = 3;
};

// Initializes the Mac68k core. Phase 0: records the request, no core yet.
bool config_init(const Config &config);

// True if the emulator is configured for NTSC.
bool config_is_ntsc(void);

// True once config_init() has completed.
bool config_is_ready(void);

// Mac model list (a few models: IIci default, Quadra 605/650, Classic II).
int mac68k_model_count(void);
const char *mac68k_model_name(int i);
int mac68k_model_id(int i);
int mac68k_model_current(void);
void mac68k_model_set(int i);

// Mac ROM pool under /roms/MAC68K (user-supplied, never in git).
// opendir scan (16 entries max); mac68k_rom_set stages the choice and
// writes the live "rom" pref, boot re-applies it after prefs_setup (which
// re-scans heuristically). Takes effect after Reset.
int mac68k_rom_count(void);
const char *mac68k_rom_name(int i);
const char *mac68k_rom_path(int i);
void mac68k_rom_set(int i);
// Staged ROM choice index (0 when none staged, so menu rows and settings
// always show something valid).
int mac68k_rom_current(void);
// Staged ROM override chosen via the menu (NULL = none staged yet).
const char *mac68k_rom_staged_path(void);

// Guest RAM sizes (menu multiple-choice). The boot malloc honors the
// staged size; takes effect after Reset.
int mac68k_ramsize_count(void);
int mac68k_ramsize_mb(int i);
int mac68k_ramsize_current(void);
void mac68k_ramsize_set(int i);
int mac68k_ramsize_mb_current(void);

}  // namespace mac68k
}  // namespace bmx

#endif
