//
// amiga_config.h
//
// Amiga core configuration for BMX bare-metal: owns the upstream
// vamiga::Emulator instance, loads Kickstart from SD /kick, applies the
// selected model scheme plus the staged hardware settings. DF0-DF3 ADF
// only (no HDF).
//

#ifndef amiga_config_h
#define amiga_config_h

namespace bmx {
namespace amiga {

// Model indices (same order as upstream vamiga::ConfigScheme).
enum {
  AMIGA_MODEL_A1000 = 0,
  AMIGA_MODEL_A500 = 1,
  AMIGA_MODEL_A500_ECS = 2,
  AMIGA_MODEL_A500_PLUS = 3,
  AMIGA_MODEL_A1200 = 4,
  AMIGA_MODEL_COUNT = 5,
};

// Staged hardware settings: plain ints so the C-linkage bridge menu never
// includes vAmiga headers. Values match upstream enum ordinals
// (AgnusRevision, DeniseRev, CPURev, CIARev, RTCRevision, FilterType,
// SamplingMethod, FloppyDriveType, DriveMechanics).
struct HwSettings {
  int model;  // 0..4, see AMIGA_MODEL_* above
  // Memory (KB).
  int chip_kb;   // 256/512/1024/2048
  int slow_kb;   // 0..1536, step 256
  int fast_kb;   // 0/512/1024/2048/4096/8192
  int slow_delay;
  int slow_mirror;
  // Chipset / CPU.
  int agnus;      // 0..4 (OCS_OLD/OCS/ECS_1MB/ECS_2MB/AGA)
  int denise;     // 0..2 (OCS/ECS/AGA)
  int cpu;        // 0..2 (68000/68010/68EC020)
  int overclock;  // 0..8
  int cia;        // 0..1 (8520 DIP/PLCC)
  int rtc;        // 0..2 (NONE/OKI/RICOH)
  int blitter;    // 0..2 (fast / fast+bus / cycle-exact)
  // Floppy drives DF0..DF3.
  int drv_on[4];    // DF0 forced on
  int drv_type[4];  // 0..2 (DD_35/HD_35/DD_525)
  int drv_mech[4];  // 0..1 (NONE/A1010)
  int drv_sound;    // master drive-click emulation on/off
  int drv_step_vol;  // 0..100
  // Paula audio (live-applied, no reboot).
  int aud_filter;    // 0..6 (NONE/A500/A1000/A1200/LOW/LED/HIGH)
  int aud_sampling;  // 0..2 (NONE/NEAREST/LINEAR)
  int aud_vol_l;     // 0..100
  int aud_vol_r;     // 0..100
  int aud_vol[4];    // 0..100 per channel
  int aud_pan[4];    // 0..400 per channel
};

struct Config {
  // "-ntsc" or "-pal" BMX machine timing option (Amiga V1 is PAL-only;
  // anything else still boots PAL).
  const char *tv_mode;
};

// Staged hardware settings (defaults = A500 OCS 1MB). The menu mutates
// these through config_hw(); config_init() applies them at boot and
// config_apply_*() reapplies them live.
HwSettings &config_hw(void);

// Overwrites *dst with defaults (also used to validate loaded settings).
void config_hw_defaults(HwSettings *dst);

// Fills chipset/memory fields of *dst with the model preset (used when
// the user picks a new model; audio/drive fields are left alone).
void config_model_preset(int model, HwSettings *dst);

// Applies the staged model scheme + all hardware settings to the core.
// No reboot (caller is config_init, pre-powerOn). Returns false on error.
bool config_apply_all(void);

// Applies only the Paula audio fields (live, no reboot).
bool config_apply_audio(void);

// Applies audio + drive-click volumes (live, no reboot).
bool config_apply_live_volumes(void);

// Full apply + cold reset so memory/chipset/CPU/drive changes take.
// Used by the menu; returns false when the core is not up.
bool config_apply_hw_reset(void);

// Warp (fast-forward): staged by emux_set_warp, read by the frame loop.
// While on, the loop runs several frames per tick with no pacing.
void config_set_warp(int on);
int config_warp(void);

// Save/load a vAmiga snapshot (.asf) via plain C file I/O (no fstream).
// Snapshots capture RAM/chips; inserted floppies stay in their drives.
bool config_save_snapshot(const char *path);
bool config_load_snapshot(const char *path);

// Hardfiles: memory-backed HDF attach from an already-loaded image buffer
// (fopen-based, no fstream), detach, presence query. HD0/HD1 (drives 0/1).
// Size limit 256MB (matches the upstream HDC memory limit).
bool config_attach_hdf(int drive, const unsigned char *data, long len);
void config_detach_hd(int drive);
int config_hd_attached(int drive);
// Staged hardfile paths (persisted in settings, attached at boot).
void config_set_hd_path(int drive, const char *path);
const char *config_hd_path(int drive);
// (Re)attaches all staged hardfile paths from SD. Used at boot and after
// hardware apply (a scheme switch resets the controller wiring).
void config_attach_staged_hd(void);

// Polls the upstream message queue for drive-step/poll messages and mixes
// authentic click sounds into the Paula stream. Call once per tick after
// the frame(s). Honors the staged drive-sound toggle + click volume.
void config_poll_drive_sounds(void);

// Initializes the core (creates Emulator, sets A500_OCS_1MB, loads ROM,
// powers on). Returns false on failure (see bmretrosys-amiga.log).
bool config_init(const Config &config);

// True once config_init() has completed.
bool config_is_ready(void);

// Basename of the loaded Kickstart (or "<diagrom 1.3>" / "" when none).
const char *config_kick_name(void);

// Direct access to the upstream emulator for video/sound/input/bridge.
// Valid only after config_init() returns true.
void *config_emu(void);

// (Re)loads a Kickstart ROM image from an SD path into Chip ROM.
// Returns false on failure.
bool config_load_kick(const char *sd_path);

// Attaches an ADF image buffer already in memory to DF0..DF3
// (drives 0..3).
bool config_insert_adf(int drive, const unsigned char *data, long len,
                       bool write_protect);

// Ejects DF0..DF3 (drives 0..3).
void config_eject(int drive);

// Cold/warm reset helpers used by the bridge.
void config_reset(bool hard);

// Runs one PAL field (Amiga::computeFrame) with exception containment.
// Returns false when the frame threw (frame loop stops). Vamiga-only
// backing TU, so the circle-only frame loop never mixes type worlds.
bool config_step_frame(void);

// Drains up to n stereo float samples from Paula into l/r. Returns the
// sample count (0 when idle). Same TU discipline as config_step_frame.
int config_pull_audio(float *l, float *r, int n);

// AudioPort diagnostics for the bmretrosys-amiga.log sound line (all plain ints).
struct AudioMetrics {
  long produced;  // samples synthesized since boot
  long consumed;  // samples pulled via copyStereo since boot
  long idle;      // silence-path samples since boot
  int fill;       // current stream fill (samples)
  int capacity;   // stream capacity (samples)
};
void config_audio_metrics(AudioMetrics *dst);

}  // namespace amiga
}  // namespace bmx

#endif
