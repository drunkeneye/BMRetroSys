//
// mac68k_bridge.cpp
//
// Emulator-specific API bridge between BMX common code (menu/UI/kernel) and
// the Basilisk II emulator core. Fulfils the shared emux_api.h contract;
// VICE-only facilities (REU, userport RS232, drives, tapes, palettes...)
// get safe defaults so the kernel links and the common menu behaves.
// Disk/floppy attach (disk/floppy prefs), model/ROM/RAM/monitor/volume menu
// rows, ADB input (in mac68k_input.cpp) and the reset request are wired;
// snapshots stay unsupported (-1). Ether/serial/SCSI/CD-ROM/extfs stay
// stubs.
//
// BMC64_MACHINE_CLASS_MAC68K is added by another workstream; the publish
// below is guarded so this TU always compiles with or without it.
//
// Basilisk-internal symbols (CPUType, FPUType, ROMVersion, InterruptFlags,
// TwentyFourBitAddressing, EmulOp, op_smalltbl_*, table68k, read_table68k,
// lookuptab, fpu_*, idle_resume, do_merges) are owned by another
// workstream and are intentionally NOT defined here.
//

#include "mac68k_bridge.h"

#include "mac68k_config.h"
#include "mac68k_debug.h"
#include "mac68k_input.h"
#include "mac68k_sound.h"
#include "mac68k_video.h"

#include "src/monitor_phosphor.h"

#include "sysdeps.h"
#include "prefs.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

// Staged settings (applied live once the core grows real backends).
static int s_warp = 0;
static int s_autostart_warp = 0;
static int s_brightness = 1000;
static int s_contrast = 1000;
static int s_gamma = 1000;
static int s_tint = 1000;
static int s_saturation = 1000;

// ===========================================================================
// C EMUX API (as declared in emux_api.h)
// ===========================================================================

extern "C" {

#if defined(__has_include)
#if __has_include("third_party/common/emux_api.h")
#include "third_party/common/emux_api.h"
#include "third_party/common/menu.h"
#include "third_party/common/menu_timing.h"
#include "third_party/common/ui.h"
#elif __has_include("emux_api.h")
#include "emux_api.h"
#include "menu.h"
#include "menu_timing.h"
#include "ui.h"
#endif
#endif

// Fallback declarations if emux_api.h was not found (never expected; the
// real header always ships in third_party/common). ABI-compatible: C
// linkage names are what the link needs, enums pass as int.
#ifndef RASPI_EMUX_API_H_
struct menu_item;
struct keymap_editor_model;
struct vkbd_key;
typedef struct vkbd_key *vkbd_key_array;
typedef int IntSetting;
typedef int StringSetting;
typedef enum {
  DIR_ROOT,
  DIR_DISKS,
  DIR_TAPES,
  DIR_CARTS,
  DIR_SNAPS,
  DIR_ROMS,
  DIR_DRIVE_ROMS,
  DIR_IEC,
  DIR_PHONEBOOK,
} DirType;
typedef char *(*fullpath_func)(DirType dir_type, char *name);
#endif

// -- machine ---------------------------------------------------------------

void emu_machine_init(int raster_skip_enabled, int raster_skip2_enabled) {
  (void)raster_skip_enabled;
  (void)raster_skip2_enabled;
  ::bmx::mac68k::debug_log("emu_machine_init: enter");
  // NOTE: BMC64_MACHINE_CLASS_MAC68K is an enum constant, not a macro, so
  // it cannot be tested with #ifdef (that is always false and silently
  // skipped publishing). Assign unconditionally like a2600_bridge.cpp does.
  emux_machine_class = BMC64_MACHINE_CLASS_MAC68K;
  emux_c64_core = BMC64_C64_CORE_UNKNOWN;
  ::bmx::mac68k::debug_log("emu_machine_init: class=MAC68K published");
}

int emu_ui_uses_german_keyboard_layout(void) {
  return 0;
}

unsigned long emux_calculate_timing(double fps) {
  (void)fps;
  return 0;
}

double emux_calculate_fps(void) {
  return 50.0;
}

// -- mouse (owned by mac68k_input.cpp: accumulators + ADB drain there) -----
// The BMX mouse driver (src/mouse_input.cpp) expects emu_mouse_* with C
// linkage; their definitions live in mac68k_input.cpp alongside the
// accumulators they feed (same split as src/atarist/atarist_input.cpp).

// -- sound -----------------------------------------------------------------

int emu_set_sound_sample_rate(int sample_rate) {
  (void)sample_rate;
  return 44100;
}

// -- reset / disks / tapes / carts / state ----------------------------------

void emux_reset(int isSoft) {
  // Classic convention codes (mirrored by mac68k_input.h): 6 = soft,
  // 3 = hard. The emulator thread takes it and fully reboots the guest.
  ::bmx::mac68k::debug_log("Mac68k: reset request (%s)", isSoft ? "soft" : "hard");
  ::bmx::mac68k::mac68k_request_reset(isSoft ? 6 : 3);
}

// Mac disk slots over the generic VICE unit flow (menu.c sets the unit via
// MENU_ATTACH_DISK_x and calls emux_attach_disk_image; detach via
// MENU_DETACH_DISK_x -> emux_detach_disk). Units 8/9 are hard disks
// ("disk" pref indices 0/1, consumed by DiskInit), units 10+ are floppies
// ("floppy" pref indices 0/1, consumed by SonyInit). Changes take effect
// after Reset: the drivers only scan prefs at boot.
static bool mac68k_disk_slot(int unit, const char **pref_name, int *index) {
  if (unit == 8 || unit == 9) {
    if (pref_name != NULL) {
      *pref_name = "disk";
    }
    if (index != NULL) {
      *index = unit - 8;
    }
    return true;
  }
  if (unit == 10 || unit == 11) {
    if (pref_name != NULL) {
      *pref_name = "floppy";
    }
    if (index != NULL) {
      *index = unit - 10;
    }
    return true;
  }
  return false;
}

static const char *mac68k_drive_basename(const char *path) {
  if (path == NULL) {
    return NULL;
  }
  const char *slash = strrchr(path, '/');
  return (slash != NULL && slash[1] != '\0') ? slash + 1 : path;
}

// Status rows under the "Mac Disk" folder (built once, refreshed here).
static struct menu_item *s_disk_status[2] = {NULL, NULL};
static struct menu_item *s_floppy_status[2] = {NULL, NULL};

static void mac68k_sync_disk_items(void) {
  for (int d = 0; d < 2; d++) {
    if (s_disk_status[d] != NULL) {
      const char *path = PrefsFindString("disk", d);
      if (path != NULL && path[0] != '\0') {
        snprintf(s_disk_status[d]->name, MAX_MENU_STR, "Disk %d: %.40s",
                 d + 1, mac68k_drive_basename(path));
      } else {
        snprintf(s_disk_status[d]->name, MAX_MENU_STR, "Disk %d: Empty",
                 d + 1);
      }
    }
    if (s_floppy_status[d] != NULL) {
      const char *path = PrefsFindString("floppy", d);
      if (path != NULL && path[0] != '\0') {
        snprintf(s_floppy_status[d]->name, MAX_MENU_STR, "Floppy %d: %.38s",
                 d + 1, mac68k_drive_basename(path));
      } else {
        snprintf(s_floppy_status[d]->name, MAX_MENU_STR, "Floppy %d: Empty",
                 d + 1);
      }
    }
  }
}

int emux_attach_disk_image(int unit, char *filename) {
  // VICE convention: 0 on success, -1 on failure.
  const char *pref_name = NULL;
  int index = 0;
  if (!mac68k_disk_slot(unit, &pref_name, &index) || filename == NULL ||
      filename[0] == '\0') {
    return -1;
  }
  PrefsReplaceString(pref_name, filename, index);
  ::bmx::mac68k::debug_log("Mac68k: attached %s %d = %s (takes effect after "
                           "Reset)",
                           pref_name, index, filename);
  mac68k_sync_disk_items();
  return 0;
}

void emux_detach_disk(int unit) {
  const char *pref_name = NULL;
  int index = 0;
  if (!mac68k_disk_slot(unit, &pref_name, &index)) {
    return;
  }
  // Empty string: PrefsFindString keeps returning a node, DiskInit/SonyInit
  // Sys_open("") fails and the slot is skipped (logged there).
  PrefsReplaceString(pref_name, "", index);
  ::bmx::mac68k::debug_log("Mac68k: detached %s %d (takes effect after "
                           "Reset)",
                           pref_name, index);
  mac68k_sync_disk_items();
}

int emux_attach_tape_image(char *filename) {
  (void)filename;
  return 0;
}

void emux_detach_tape(void) {
}

int emux_attach_cart(int bank, char *filename) {
  (void)bank;
  (void)filename;
  return 0;
}

void emux_detach_cart(int bank) {
  (void)bank;
}

void emux_set_cart_default(void) {}

int emux_load_state(char *filename) {
  // No Mac snapshot backend (needs a CPU/RAM dump): always report failure
  // (-1, the dos-stub convention) so the menu shows an error.
  (void)filename;
  ::bmx::mac68k::debug_log("Mac68k: snapshots unsupported, load denied");
  return -1;
}

int emux_save_state(char *filename) {
  (void)filename;
  ::bmx::mac68k::debug_log("Mac68k: snapshots unsupported, save denied");
  return -1;
}

int emux_tape_control(int cmd) {
  (void)cmd;
  return 0;
}

void emux_show_cart_osd_menu(void) {}

int emux_autostart_file(char *filename, unsigned int program_number) {
  (void)filename;
  (void)program_number;
  return 0;
}

void emux_drive_change_model(int unit) {
  (void)unit;
}

void emux_set_warp(int warp) {
  s_warp = warp ? 1 : 0;
}

int emux_prepare_shutdown(void) {
  return 0;
}

int emux_atari_mount_disk(int drive, char *path) {
  (void)drive;
  (void)path;
  return -1;
}

// DOS Drive C folder mount (common menu.c): no DOS drives here.
int emux_dos_mount_folder(char *dir) {
  (void)dir;
  return -1;
}

void emux_atari_sync_drives(void) {}

int emux_atari_cart_types_for_kb(int kb, int *ids, int max_ids) {
  (void)kb;
  (void)ids;
  (void)max_ids;
  return 0;
}

void emux_atari_set_cart_type(int type) {
  (void)type;
}

int emux_atari_blank_cart(char *path) {
  (void)path;
  return -1;
}

int emux_atari_save_set(char *path) {
  (void)path;
  return -1;
}

int emux_atari_load_set(char *path) {
  (void)path;
  return -1;
}

int emux_atari_blank_atr(char *path) {
  (void)path;
  return -1;
}

int emux_atari_uncompress(char *path, char *out_path, size_t out_size) {
  (void)path;
  (void)out_path;
  (void)out_size;
  return -1;
}

void emux_set_iec_dir(int unit, char *dir) {
  (void)unit;
  (void)dir;
}

void emux_set_joy_port_device(int port_num, int dev_id) {
  (void)port_num;
  (void)dev_id;
}

void emux_set_joy_pot_x(int port, int value) {
  (void)port;
  (void)value;
}

void emux_set_joy_pot_y(int port, int value) {
  (void)port;
  (void)value;
}

void emux_mouse_input_clear(void) {}

int emux_mouse_preview_poll(float *delta_x, float *delta_y) {
  (void)delta_x;
  (void)delta_y;
  return 0;
}

void emux_kbd_set_latch_keyarr(int row, int col, int value) {
  (void)row;
  (void)col;
  (void)value;
}

const char *emux_keyboard_mapping_file(void) {
  return NULL;
}

int emux_keyboard_mapping_lookup(long keycode, unsigned char usb_modifiers,
                                 int *row, int *column, int *flags) {
  (void)keycode;
  (void)usb_modifiers;
  (void)row;
  (void)column;
  (void)flags;
  return 0;
}

int emux_keyboard_mapping_target_name(int row, int column, int flags,
                                      char *out, size_t out_size) {
  (void)row;
  (void)column;
  (void)flags;
  if (out != NULL && out_size > 0) {
    out[0] = '\0';
  }
  return 0;
}

int emux_keymap_editor_begin(struct keymap_editor_model *model,
                             int *editable, char *error, size_t error_size) {
  (void)model;
  (void)editable;
  if (error != NULL && error_size > 0) {
    snprintf(error, error_size, "No keyboard mapping on Mac68k");
  }
  return -1;
}

int emux_keymap_editor_save(const struct keymap_editor_model *model,
                            char *error, size_t error_size) {
  (void)model;
  if (error != NULL && error_size > 0) {
    snprintf(error, error_size, "No keyboard mapping on Mac68k");
  }
  return -1;
}

int emux_keymap_editor_restore_defaults(char *error, size_t error_size) {
  if (error != NULL && error_size > 0) {
    snprintf(error, error_size, "No keyboard mapping on Mac68k");
  }
  return -1;
}

vkbd_key_array emux_get_vkbd(void) {
  return NULL;
}

int emux_get_vkbd_width(void) {
  return 0;
}

int emux_get_vkbd_height(void) {
  return 0;
}

int emux_get_vkbd_size(void) {
  return 0;
}

// Minimal safe stub: do NOT pump the Stella-style UI loop here. The real
// menu trap is wired in a later phase.
void emux_trap_main_loop_ui(void) {
  ::bmx::mac68k::debug_log("Mac68k: menu opened, entering pump");
  menu_about_to_activate();
  circle_show_fbl(FB_LAYER_UI);
  while (ui_enabled) {
    circle_check_gpio();
    ui_check_key();
    ui_handle_toggle_or_quick_func();
    ui_render_single_frame();
    hdmi_timing_hook();
    emux_ensure_video();
  }
  menu_about_to_deactivate();
  circle_hide_fbl(FB_LAYER_UI);
  ::bmx::mac68k::debug_log("Mac68k: menu pump exit, resuming emulation");
}

// -- VICE glue helpers referenced by kernel.cpp ----------------------------

const char *file_system_get_disk_name(unsigned int unit, unsigned int drive) {
  (void)drive;
  // Basename of the staged pref path (what the next boot will mount).
  const char *pref_name = NULL;
  int index = 0;
  if (!mac68k_disk_slot((int)unit, &pref_name, &index)) {
    return NULL;
  }
  const char *path = PrefsFindString(pref_name, index);
  if (path == NULL || path[0] == '\0') {
    return NULL;
  }
  return mac68k_drive_basename(path);
}

const char *tape_get_file_name(int port) {
  (void)port;
  return NULL;
}

char *cartridge_get_filename_by_slot(int slot) {
  (void)slot;
  return NULL;
}

// -- menu construction (Mac rows; driven by mac68k_config/video/sound) ----

void emux_add_drive_option(struct menu_item *parent, int drive) {
  // Mac disks/floppies under one "Mac Disk" folder: per-slot status rows
  // plus Insert/Eject buttons reusing the generic VICE disk flow
  // (MENU_ATTACH_DISK_x picks the file and dispatches to
  // emux_attach_disk_image with units 8/9 = disks, 10/11 = floppies;
  // MENU_DETACH_DISK_x detaches). Same shape as the atarist Drive A/B
  // folders. Per-unit calls (8..11, from the VICE Drive folders menu.c
  // also builds for this class) are ignored here.
  if (drive != -1 || parent == NULL) {
    return;
  }
  struct menu_item *folder = ui_menu_add_folder(parent, (char *)"Mac Disk");
  if (folder == NULL) {
    return;
  }
  static const int attach_ids[4] = {MENU_ATTACH_DISK_8, MENU_ATTACH_DISK_9,
                                    MENU_ATTACH_DISK_10, MENU_ATTACH_DISK_11};
  static const int detach_ids[4] = {MENU_DETACH_DISK_8, MENU_DETACH_DISK_9,
                                    MENU_DETACH_DISK_10, MENU_DETACH_DISK_11};
  static const char *insert_labels[4] = {
      "Insert disk 1...", "Insert disk 2...", "Insert floppy 1...",
      "Insert floppy 2..."};
  static const char *eject_labels[4] = {"Eject disk 1", "Eject disk 2",
                                        "Eject floppy 1", "Eject floppy 2"};
  for (int d = 0; d < 4; d++) {
    struct menu_item *row =
        ui_menu_add_button(MENU_TEXT, folder, (char *)"State");
    if (d < 2) {
      s_disk_status[d] = row;
    } else {
      s_floppy_status[d - 2] = row;
    }
    ui_menu_add_button(attach_ids[d], folder, insert_labels[d]);
    ui_menu_add_button(detach_ids[d], folder, eject_labels[d]);
  }
  mac68k_sync_disk_items();
}

void emux_add_keyboard_options(struct menu_item *parent) {
  // The Mac has a full ADB keyboard: no remappable console switches
  // (the a2600 Select/Reset capture rows do not apply). One readout row.
  if (parent == NULL) {
    return;
  }
  ui_menu_add_button(MENU_TEXT, parent,
                     (char *)"USB keyboard -> ADB (F12 opens menu)");
}

void emux_add_machine_options(struct menu_item *parent) {
  // Model / ROM pool / RAM size (all reboot-gated: the core only reads
  // them at boot) plus the live monitor selector (dos/a2600 pattern:
  // per-pixel mapping at blit time). No new MENU_* ids: the reused ids
  // below are bridge-owned (menu.c has no common handling for them) and
  // emux_handle_menu_change consumes them first.
  if (parent == NULL) {
    return;
  }
  struct menu_item *child;
  int i;

  child = ui_menu_add_multiple_choice(MENU_ATARI_MACHINE, parent,
                                       (char *)"Model");
  int models = ::bmx::mac68k::mac68k_model_count();
  if (models > 16) {
    models = 16;
  }
  if (models < 1) {
    child->num_choices = 1;
    snprintf(child->choices[0], sizeof(child->choices[0]),
             "No ROMs (add to /roms/MAC68K)");
    child->choice_ints[0] = -1;
    child->value = 0;
  } else {
    child->num_choices = models;
    for (i = 0; i < models; i++) {
      snprintf(child->choices[i], sizeof(child->choices[i]), "%s",
               ::bmx::mac68k::mac68k_model_name(i));
      child->choice_ints[i] = i;
    }
    child->value = ::bmx::mac68k::mac68k_model_current();
  }

  child = ui_menu_add_multiple_choice(MENU_ATARI_RAM, parent,
                                      (char *)"RAM size");
  int ram_opts = ::bmx::mac68k::mac68k_ramsize_count();
  if (ram_opts > 16) {
    ram_opts = 16;
  }
  child->num_choices = ram_opts;
  for (i = 0; i < ram_opts; i++) {
    snprintf(child->choices[i], sizeof(child->choices[i]), "%d MB",
             ::bmx::mac68k::mac68k_ramsize_mb(i));
    child->choice_ints[i] = i;
  }
  child->value = ::bmx::mac68k::mac68k_ramsize_current();

  child = ui_menu_add_multiple_choice(MENU_DOS_MONITOR, parent,
                                      (char *)"Monitor");
  child->num_choices = 4;
  snprintf(child->choices[0], sizeof(child->choices[0]), "Color");
  child->choice_ints[0] = BMX_MONITOR_COLOR;
  snprintf(child->choices[1], sizeof(child->choices[1]), "B&W");
  child->choice_ints[1] = BMX_MONITOR_BW;
  snprintf(child->choices[2], sizeof(child->choices[2]), "Green");
  child->choice_ints[2] = BMX_MONITOR_GREEN;
  snprintf(child->choices[3], sizeof(child->choices[3]), "Amber");
  child->choice_ints[3] = BMX_MONITOR_AMBER;
  {
    const int cur = ::bmx::mac68k::video_get_monitor();
    child->value = 0;
    for (i = 0; i < child->num_choices; i++) {
      if (child->choice_ints[i] == cur) {
        child->value = i;
        break;
      }
    }
  }
}

void emux_add_tape_options(struct menu_item *parent) {
  (void)parent;
}

void emux_add_sound_options(struct menu_item *emulation_parent,
                            struct menu_item *sid_parent,
                            struct menu_item *sound_parent) {
  (void)sid_parent;
  (void)sound_parent;
  // Master volume (atarist pattern): percent routed to the Mac audio
  // backend, applies live.
  if (emulation_parent == NULL) {
    return;
  }
  ui_menu_add_range(MENU_ATARIST_VOLUME, emulation_parent, (char *)"Volume %",
                    0, 300, 5, ::bmx::mac68k::sound_get_volume());
}

void emux_add_userport_joys(struct menu_item *parent) {
  (void)parent;
}

void emux_create_disk(struct menu_item *item, fullpath_func f_fullpath) {
  (void)item;
  (void)f_fullpath;
}

void emux_create_tape(struct menu_item *item, fullpath_func f_fullpath) {
  (void)item;
  (void)f_fullpath;
}

struct menu_item *emux_add_cartridge_options(struct menu_item *parent) {
  return parent;
}

struct menu_item *emux_add_palette_options(int menu_id,
                                           struct menu_item *parent) {
  (void)menu_id;
  (void)parent;
  return NULL;
}

int emux_change_palette(int display_num, int palette_index) {
  (void)display_num;
  (void)palette_index;
  return 0;
}

int emux_apply_palette_setting(int display_num) {
  (void)display_num;
  return -1;
}

int emux_set_palette_setting(int display_num, const char *setting) {
  (void)display_num;
  (void)setting;
  return 0;
}

const char *emux_get_palette_setting(int display_num) {
  (void)display_num;
  return "";
}

void emux_video_color_setting_changed(int display_num) {
  (void)display_num;
}

void emux_set_color_brightness(int display_num, int value) {
  (void)display_num;
  s_brightness = value;
}

void emux_set_color_contrast(int display_num, int value) {
  (void)display_num;
  s_contrast = value;
}

void emux_set_color_gamma(int display_num, int value) {
  (void)display_num;
  s_gamma = value;
}

void emux_set_color_tint(int display_num, int value) {
  (void)display_num;
  s_tint = value;
}

void emux_set_color_saturation(int display_num, int value) {
  (void)display_num;
  s_saturation = value;
}

int emux_get_color_brightness(int display_num) {
  (void)display_num;
  return s_brightness;
}

int emux_get_color_contrast(int display_num) {
  (void)display_num;
  return s_contrast;
}

int emux_get_color_gamma(int display_num) {
  (void)display_num;
  return s_gamma;
}

int emux_get_color_tint(int display_num) {
  (void)display_num;
  return s_tint;
}

int emux_get_color_saturation(int display_num) {
  (void)display_num;
  return s_saturation;
}

void emux_get_default_color_setting(int *brightness, int *contrast,
                                    int *gamma, int *tint, int *saturation) {
  if (brightness != NULL) {
    *brightness = 1000;
  }
  if (contrast != NULL) {
    *contrast = 1000;
  }
  if (gamma != NULL) {
    *gamma = 1000;
  }
  if (tint != NULL) {
    *tint = 1000;
  }
  if (saturation != NULL) {
    *saturation = 1000;
  }
}

void emux_set_video_cache(int value) {
  (void)value;
}

void emux_set_hw_scale(int value) {
  (void)value;
}

void emux_set_int(IntSetting setting, int value) {
  if (setting == Setting_WarpMode) {
    emux_set_warp(value);
  } else if (setting == Setting_AutostartWarp) {
    s_autostart_warp = value ? 1 : 0;
  }
}

void emux_set_int_1(IntSetting setting, int value, int param) {
  (void)param;
  emux_set_int(setting, value);
}

void emux_get_int(IntSetting setting, int *dest) {
  int v = 0;
  if (setting == Setting_WarpMode) {
    v = s_warp;
  } else if (setting == Setting_AutostartWarp) {
    v = s_autostart_warp;
  }
  if (dest != NULL) {
    *dest = v;
  }
}

void emux_get_int_1(IntSetting setting, int *dest, int param) {
  (void)param;
  emux_get_int(setting, dest);
}

void emux_get_string_1(StringSetting setting, const char **dest, int param) {
  (void)setting;
  (void)param;
  if (dest != NULL) {
    *dest = "";
  }
}

int emux_save_settings(void) {
  return 0;
}

void emux_load_settings_done(void) {}

void emux_load_additional_settings(void) {}

void emux_save_additional_settings(FILE *fp) {
  if (fp == NULL) {
    return;
  }
  // Human-readable values (names, MB), not indices: the file is meant to
  // be readable and robust against pool reorderings.
  const char *model =
      ::bmx::mac68k::mac68k_model_name(::bmx::mac68k::mac68k_model_current());
  fprintf(fp, "mac68k_model=%s\n", model != NULL ? model : "Mac IIci");
  fprintf(fp, "mac68k_ramsize=%d\n", ::bmx::mac68k::mac68k_ramsize_mb_current());
  fprintf(fp, "mac68k_volume=%d\n", ::bmx::mac68k::sound_get_volume());
  fprintf(fp, "mac68k_monitor=%d\n", ::bmx::mac68k::video_get_monitor());
}

namespace {

// Old numeric files (indices) still load: all-digits value that matches
// nothing by name falls back to the old index meaning.
bool setting_is_index(const char *s) {
  if (s == NULL || *s == '\0') {
    return false;
  }
  for (const char *p = s; *p != '\0'; p++) {
    if (*p < '0' || *p > '9') {
      return false;
    }
  }
  return true;
}

}  // namespace

int emux_handle_loaded_setting(char *name, char *value_str, int value) {
  (void)value;
  if (name != NULL && strcmp(name, "mac68k_model") == 0) {
    if (value_str != NULL) {
      for (int i = 0; i < ::bmx::mac68k::mac68k_model_count(); i++) {
        const char *n = ::bmx::mac68k::mac68k_model_name(i);
        if (n != NULL && strcmp(n, value_str) == 0) {
          ::bmx::mac68k::mac68k_model_set(i);
          return 1;
        }
      }
      if (setting_is_index(value_str)) {
        ::bmx::mac68k::mac68k_model_set(atoi(value_str));
        return 1;
      }
    }
    return 1;
  }
  if (name != NULL && strcmp(name, "mac68k_rom") == 0) {
    // Retired key (ROM selection was removed; the model owns its ROM).
    // Consumed silently so old files load without warnings.
    return 1;
  }
  if (name != NULL && strcmp(name, "mac68k_ramsize") == 0) {
    if (value_str != NULL) {
      for (int i = 0; i < ::bmx::mac68k::mac68k_ramsize_count(); i++) {
        if (::bmx::mac68k::mac68k_ramsize_mb(i) == atoi(value_str) &&
            atoi(value_str) > 0) {
          ::bmx::mac68k::mac68k_ramsize_set(i);
          return 1;
        }
      }
      if (setting_is_index(value_str)) {
        ::bmx::mac68k::mac68k_ramsize_set(atoi(value_str));
        return 1;
      }
    }
    return 1;
  }
  if (name != NULL && strcmp(name, "mac68k_volume") == 0) {
    ::bmx::mac68k::sound_set_volume(atoi(value_str != NULL ? value_str : ""));
    return 1;
  }
  if (name != NULL && strcmp(name, "mac68k_monitor") == 0) {
    ::bmx::mac68k::video_set_monitor(atoi(value_str != NULL ? value_str : ""));
    return 1;
  }
  return 0;
}

int emux_handle_menu_change(struct menu_item *item) {
  if (item == NULL) {
    return 0;
  }
  switch (item->id) {
  case MENU_ATARI_MACHINE: {
    int v = item->value;
    int index = (v >= 0 && v < item->num_choices) ? item->choice_ints[v] : -1;
    if (index < 0 || index >= ::bmx::mac68k::mac68k_model_count()) {
      return 1;
    }
    ::bmx::mac68k::mac68k_model_set(index);
    ::bmx::mac68k::debug_log("Mac68k: model -> %s",
                             ::bmx::mac68k::mac68k_model_name(index));
    return 1;
  }
  case MENU_ATARIST_TOS: {
    // No ROM selector: the model owns its ROM (see mac68k_config). Kept
    // only so stale menu events cannot desync anything.
    return 1;
  }
  case MENU_ATARI_RAM: {
    int v = item->value;
    int index = (v >= 0 && v < item->num_choices) ? item->choice_ints[v] : -1;
    if (index < 0 || index >= ::bmx::mac68k::mac68k_ramsize_count()) {
      return 1;
    }
    ::bmx::mac68k::mac68k_ramsize_set(index);
    return 1;
  }
  case MENU_DOS_MONITOR: {
    int v = item->value;
    if (v >= 0 && v < item->num_choices) {
      v = item->choice_ints[v];
    }
    // Per-pixel mapping at blit time: applies instantly, no reboot.
    ::bmx::mac68k::video_set_monitor(v);
    ::bmx::mac68k::debug_log("Mac68k: monitor -> %d (live)", v);
    return 1;
  }
  case MENU_ATARIST_VOLUME: {
    int v = item->value;
    if (v < 0) {
      v = 0;
    }
    if (v > 300) {
      v = 300;
    }
    ::bmx::mac68k::sound_set_volume(v);
    ::bmx::mac68k::debug_log("Mac68k: volume -> %d%% (live)", v);
    return 1;
  }
  case MENU_ATARI_SCREENSHOT: {
    // Auto-named BMP dump of the Mac canvas (dos pattern: USB stick
    // first, then SD system volume).
    static const char *kShotDirs[] = {
        "USER:/systems/MAC68K/snapshots",
        "SYS:/systems/MAC68K/snapshots",
        "SD:/systems/MAC68K/snapshots",
    };
    char path[64];
    bool saved = false;
    for (size_t d = 0;
         d < sizeof(kShotDirs) / sizeof(kShotDirs[0]) && !saved; d++) {
      for (int n = 0; n < 100 && !saved; n++) {
        snprintf(path, sizeof(path), "%s/shot%02d.bmp", kShotDirs[d], n);
        FILE *probe = fopen(path, "rb");
        if (probe != NULL) {
          fclose(probe);
          continue;  // name taken
        }
        if (::bmx::mac68k::video_save_screenshot(path)) {
          saved = true;
          char msg[64];
          snprintf(msg, sizeof(msg), "Saved shot%02d.bmp", n);
          ui_info(msg);
        } else if (n == 0) {
          break;  // dir itself unwritable, try next volume
        }
      }
    }
    if (!saved) {
      ui_info("Screenshot failed");
    }
    return 1;
  }
  default:
    break;
  }
  return 0;
}

int emux_handle_quick_func(int button_func, fullpath_func f_fullpath) {
  (void)button_func;
  (void)f_fullpath;
  return 0;
}

int emux_handle_rom_change(struct menu_item *item, fullpath_func f_fullpath) {
  (void)item;
  (void)f_fullpath;
  return 0;
}

// -- network / developer / api ---------------------------------------------

int emux_developer_mode_enabled(void) {
  return 0;
}

int emux_get_developer_password(char *password, unsigned password_size) {
  (void)password;
  (void)password_size;
  return 0;
}

unsigned emux_get_developer_log_buffer_kb(void) {
  return 0;
}

int emux_api_mode_enabled(void) {
  return 0;
}

int emux_get_api_password(char *password, unsigned password_size) {
  (void)password;
  (void)password_size;
  return 0;
}

// -- RS232 / userport ------------------------------------------------------

int emux_apply_rs232net(int enabled, int mode, int interface,
                        const char *target, int baud, int ip232,
                        int hayes_audio, const char *phonebook) {
  (void)enabled;
  (void)mode;
  (void)interface;
  (void)target;
  (void)baud;
  (void)ip232;
  (void)hayes_audio;
  (void)phonebook;
  return 0;
}

// -- VICE-only link stubs ----------------------------------------------------

typedef struct vice_network_socket_s vice_network_socket_t;
typedef struct vice_network_socket_address_s vice_network_socket_address_t;

vice_network_socket_t *vice_network_client(
    const vice_network_socket_address_t *server_address) {
  (void)server_address;
  return NULL;
}

vice_network_socket_address_t *vice_network_address_generate(const char *address,
                                                             unsigned short port) {
  (void)address;
  (void)port;
  return NULL;
}

void vice_network_address_close(vice_network_socket_address_t *addr) {
  (void)addr;
}

uint8_t *charset_petconvstring(uint8_t *text, int mode) {
  (void)mode;
  return text;
}

int kbdbuf_feed(const char *text) {
  (void)text;
  return 0;
}

int vice_network_socket_close(vice_network_socket_t *sockfd) {
  (void)sockfd;
  return -1;
}

int vice_network_get_errorcode(void) {
  return -1;
}

ssize_t vice_network_send(vice_network_socket_t *sockfd, const void *buffer,
                          size_t buffer_length, int flags) {
  (void)sockfd;
  (void)buffer;
  (void)buffer_length;
  (void)flags;
  return -1;
}

ssize_t vice_network_receive(vice_network_socket_t *sockfd, void *buffer,
                             size_t buffer_length, int flags) {
  (void)sockfd;
  (void)buffer;
  (void)buffer_length;
  (void)flags;
  return -1;
}

int vice_network_select_poll_one(vice_network_socket_t *readsockfd) {
  (void)readsockfd;
  return -1;
}

uint8_t circle_get_userport_ddr(void) {
  return 0;
}

uint8_t circle_get_userport(void) {
  return 0xff;
}

void circle_set_userport(uint8_t value) {
  (void)value;
}

uint8_t mem_bank_peek(int bank, uint16_t addr, void *context) {
  (void)bank;
  (void)addr;
  (void)context;
  return 0;
}

struct image_contents_s;

struct image_contents_s *diskcontents_filesystem_read(const char *file_name) {
  (void)file_name;
  return NULL;
}

struct image_contents_s *tapecontents_read(const char *file_name) {
  (void)file_name;
  return NULL;
}

void lib_free(void *ptr) {
  free(ptr);
}

typedef struct image_contents_s image_contents_t;
typedef struct image_contents_file_list_s image_contents_file_list_t;

void image_contents_destroy(image_contents_t *contents) {
  (void)contents;
}

char *image_contents_to_string(image_contents_t *contents, char out_charset) {
  (void)contents;
  (void)out_charset;
  return NULL;
}

char *image_contents_file_to_string(image_contents_file_list_t *p,
                                    char out_charset) {
  (void)p;
  (void)out_charset;
  return NULL;
}

uint8_t charset_petscii_to_screencode(uint8_t code, unsigned int reverse_mode) {
  (void)reverse_mode;
  return code;
}

void vsync_suspend_speed_eval(void) {
  ::bmx::mac68k::debug_log("Mac68k: menu closed");
}

}  // extern "C"
