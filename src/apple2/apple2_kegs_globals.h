//
// apple2_kegs_globals.h
//
// extern declarations for the KEGS core globals the BMX glue drives.
// KEGS is C: all of these live in the GLOBAL namespace, so this header
// must be included (or its declarations repeated) at file-global scope,
// never inside namespace bmx::apple2 (an extern there would declare a
// different, undefined bmx::apple2:: symbol).
//

#ifndef apple2_kegs_globals_h
#define apple2_kegs_globals_h

extern "C" {
#include "defc.h"
#include "protos_base.h"
}

extern char *g_cfg_rom_path;
extern int g_limit_speed;
extern int g_zip_speed_mhz;
extern int g_config_control_panel;
extern int g_config_kegs_auto_update;
extern int g_audio_enable;
extern int g_preferred_rate;
extern int g_status_enable;
extern int g_rom_version;
extern word32 g_vbl_count;
extern word32 g_cur_a2_stat;
extern int g_mainwin_width;
extern int g_mainwin_height;
extern Kimage g_mainwin_kimage;
extern word32 g_alpha_mask;
extern word32 g_a2palette_1624[];
extern word32 *g_sound_shm_addr;
extern int g_sound_shm_pos;

#endif
