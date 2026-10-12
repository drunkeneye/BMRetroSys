//
// apple2_bridge.cpp
//
// Emulator-specific API bridge between BMX common code (menu/UI/kernel) and
// the KEGS core (vendored GSplus subset). Fulfils the shared emux_api.h
// contract; VICE-only facilities (REU, userport RS232, carts, palettes...)
// get safe defaults so the kernel links and the common menu behaves.
// No tape (Apple II cassette play is a stub upstream) and no snapshots in
// v1: attach fails cleanly, state returns -1.
//

#include "apple2_bridge.h"
#include "apple2_config.h"
#include "apple2_debug.h"
#include "apple2_drive.h"
#include "apple2_input.h"
#include "apple2_machine.h"
#include "apple2_sound.h"
#include "apple2_timer.h"
#include "apple2_video.h"
#include "src/monitor_phosphor.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>

// Full struct menu_item for the anchors below (emux_api.h only forwards).
extern "C" {
#include "ui.h"
}

// KEGS core globals (global-scope externs; see the header).
#include "apple2_kegs_globals.h"

// Shared volume (defined in apple2_sound.cpp).
extern int audiovolume;

// KEGS core (vendored GSplus subset, C linkage).
extern "C" {
#include "defc.h"
#include "protos_base.h"
}

namespace bmx {
namespace apple2 {

struct menu_item *s_model_item = nullptr;
struct menu_item *s_joystick_item = nullptr;
struct menu_item *s_speed_item = nullptr;
struct menu_item *s_monitor_item = nullptr;
struct menu_item *s_volume_item = nullptr;
struct menu_item *s_drive_status[4] = {nullptr, nullptr, nullptr, nullptr};

// Staged settings (applied live when the core is up).
int g_warp = 0;

// Slot-6 autostart: the IIgs startup scan does not pick up 5.25" disks
// (verified: slot 5/7 autoboot, slot 6 needs PR#6), so autostarting a
// 5.25" image attaches it, resets to a clean machine, warps through POST
// at full speed, then types PR#6 at the BASIC prompt. Typing is skipped
// if the user pressed keys meanwhile (they own the keyboard then).
const unsigned long kAutostartWaitFrames = 3600;
unsigned long s_autostart_wait = 0;
bool s_autostart_armed = false;
bool s_autostart_warp_save = false;

void apple2_autostart_poll(void) {
  if (!s_autostart_armed) {
    return;
  }
  if (s_autostart_wait > 0) {
    s_autostart_wait--;
    return;
  }
  s_autostart_armed = false;
  ::bmx::apple2::timer_set_warp(s_autostart_warp_save);
  ::bmx::apple2::g_warp = s_autostart_warp_save ? 1 : 0;
  if (!::bmx::apple2::apple2_user_key_seen()) {
    ::bmx::apple2::input_type_text("PR#6\r");
    ::bmx::apple2::debug_log("autostart: typing PR#6");
  } else {
    ::bmx::apple2::debug_log("autostart: user keys seen, skip typing");
  }
}

void sync_all_menu_items(void) {
  if (s_model_item != nullptr) {
    int cur = machine_current_index();
    for (int i = 0; i < s_model_item->num_choices; i++) {
      if (s_model_item->choice_ints[i] == cur) {
        s_model_item->value = i;
        break;
      }
    }
  }
  if (s_joystick_item != nullptr) {
    int cur = input_get_joystick();
    for (int i = 0; i < s_joystick_item->num_choices; i++) {
      if (s_joystick_item->choice_ints[i] == cur) {
        s_joystick_item->value = i;
        break;
      }
    }
  }
  if (s_speed_item != nullptr) {
    for (int i = 0; i < s_speed_item->num_choices; i++) {
      if (s_speed_item->choice_ints[i] == g_limit_speed) {
        s_speed_item->value = i;
        break;
      }
    }
  }
  if (s_monitor_item != nullptr) {
    const int cur = video_get_monitor();
    for (int i = 0; i < s_monitor_item->num_choices; i++) {
      if (s_monitor_item->choice_ints[i] == cur) {
        s_monitor_item->value = i;
        break;
      }
    }
  }
  sync_drive_items();
}

// Refresh the drive status rows ("[empty]" or the image basename).
// Order: S6D1, S6D2, S5D1, S7D1.
void sync_drive_items(void) {
  static const int slots[4] = {6, 6, 5, 7};
  static const int drives[4] = {0, 1, 0, 0};
  for (int d = 0; d < 4; d++) {
    if (s_drive_status[d] == nullptr) {
      continue;
    }
    const char *name = drive_attached_name(slots[d], drives[d]);
    if (name == nullptr || name[0] == '\0') {
      snprintf(s_drive_status[d]->name, MAX_MENU_STR, "[empty]");
      continue;
    }
    const char *base = strrchr(name, '/');
    base = base != nullptr ? base + 1 : name;
    snprintf(s_drive_status[d]->name, MAX_MENU_STR, "%s", base);
  }
}

}  // namespace apple2
}  // namespace bmx

// ===========================================================================
// C EMUX API (as declared in emux_api.h)
// ===========================================================================

extern "C" {

#include "emux_api.h"
#include "menu.h"
#include "menu_timing.h"
#include "ui.h"

// -- machine ---------------------------------------------------------------

void emu_machine_init(int raster_skip_enabled, int raster_skip2_enabled) {
  (void)raster_skip_enabled;
  (void)raster_skip2_enabled;
  emux_machine_class = BMC64_MACHINE_CLASS_APPLE2;
  emux_c64_core = BMC64_C64_CORE_UNKNOWN;
  ::bmx::apple2::debug_log("S2b emu_machine_init: class=APPLE2 published");
}

int emu_ui_uses_german_keyboard_layout(void) {
  return 0;
}

unsigned long emux_calculate_timing(double fps) {
  (void)fps;
  return 0;
}

double emux_calculate_fps(void) {
  return 60.0;
}

// -- sound -----------------------------------------------------------------

int emu_set_sound_sample_rate(int sample_rate) {
  (void)sample_rate;
  return 44100;
}

// -- reset / disks / state -------------------------------------------------

void emux_reset(int isSoft) {
  ::bmx::apple2::debug_log("emux_reset(%s) enter", isSoft ? "soft" : "hard");
  ::bmx::apple2::machine_reset(isSoft != 0);
  ::bmx::apple2::debug_log("emux_reset done");
}

// Menu drive units 8/9/10/11 -> S6D1/S6D2/S5D1/S7D1. Unit 0 is the
// autostart/default flow (first 5.25" drive).
int emux_attach_disk_image(int unit, char *filename) {
  int slot = -1, drive = 0;
  if (unit == 8 || unit == 0) {
    slot = 6;
    drive = 0;
  } else if (unit == 9) {
    slot = 6;
    drive = 1;
  } else if (unit == 10) {
    slot = 5;
    drive = 0;
  } else if (unit == 11) {
    slot = 7;
    drive = 0;
  }
  if (slot < 0 || filename == NULL) {
    return -1;
  }
  if (::bmx::apple2::drive_attach(slot, drive, filename) != 0) {
    return -1;
  }
  ::bmx::apple2::sync_drive_items();
  return 0;
}

void emux_detach_disk(int unit) {
  int slot = 6, drive = 0;
  if (unit == 9) {
    drive = 1;
  } else if (unit == 10) {
    slot = 5;
  } else if (unit == 11) {
    slot = 7;
  }
  ::bmx::apple2::drive_detach(slot, drive);
  ::bmx::apple2::sync_drive_items();
}

int emux_attach_tape_image(char *filename) {
  (void)filename;
  return -1;  // no cassette support upstream either
}

void emux_detach_tape(void) {}

int emux_attach_cart(int bank, char *filename) {
  (void)bank;
  (void)filename;
  return -1;
}

void emux_detach_cart(int bank) {
  (void)bank;
}

void emux_set_cart_default(void) {}

int emux_load_state(char *filename) {
  (void)filename;
  // No snapshot format in v1.
  return -1;
}

int emux_save_state(char *filename) {
  (void)filename;
  return -1;
}

int emux_load_reu_image(char *filename) {
  (void)filename;
  return -1;
}

int emux_save_reu_image(char *filename) {
  (void)filename;
  return -1;
}

int emux_tape_control(int cmd) {
  (void)cmd;
  return 0;
}

void emux_show_cart_osd_menu(void) {}

// Apple II disks autoboot (DOS/ProDOS): attach in the right slot by image
// size (~140K 5.25" -> S6D1, ~800K 3.5" -> S5D1, larger -> S7D1), then
// reset to a clean machine. Slot 5/7 boot from the startup scan; slot 6
// (5.25") needs PR#6 typed at BASIC, so those arm the typer (see above).
// Returns 0 on success, -1 on failure.
int emux_autostart_file(char *filename, unsigned int program_number) {
  (void)program_number;
  if (filename == NULL) {
    return -1;
  }
  FILE *fp = fopen(filename, "rb");
  if (fp == nullptr) {
    return -1;
  }
  fseek(fp, 0, SEEK_END);
  long size = ftell(fp);
  fclose(fp);
  // Strip an optional 2IMG 64-byte header for the size sniff.
  long eff = size;
  if (size > 64) {
    FILE *probe = fopen(filename, "rb");
    if (probe != nullptr) {
      char magic[5] = {0, 0, 0, 0, 0};
      size_t got = fread(magic, 1, 4, probe);
      fclose(probe);
      if (got == 4 && memcmp(magic, "2IMG", 4) == 0) {
        eff = size - 64;
      }
    }
  }
  int slot = 6, drive = 0;
  if (eff > 900 * 1024) {
    slot = 7;  // hard-disk image -> SmartPort
  } else if (eff > 200 * 1024) {
    slot = 5;  // 800K 3.5"
  }
  if (::bmx::apple2::drive_attach(slot, drive, filename) != 0) {
    ::bmx::apple2::debug_log("autostart: attach failed %s", filename);
    return -1;
  }
  ::bmx::apple2::machine_reset(false);
  ::bmx::apple2::sync_drive_items();
  if (slot == 6) {
    // Warp through POST/scan, then PR#6 at the prompt.
    ::bmx::apple2::apple2_user_key_reset();
    ::bmx::apple2::s_autostart_warp_save = ::bmx::apple2::timer_warp();
    ::bmx::apple2::timer_set_warp(true);
    ::bmx::apple2::g_warp = 1;
    ::bmx::apple2::s_autostart_wait = ::bmx::apple2::kAutostartWaitFrames;
    ::bmx::apple2::s_autostart_armed = true;
    ::bmx::apple2::debug_log("autostart: attached s%dd%d, reset + warp, "
                             "PR#6 in %lu frames",
                             slot, drive + 1,
                             ::bmx::apple2::kAutostartWaitFrames);
  } else {
    ::bmx::apple2::debug_log("autostart: attached s%dd%d, reset", slot,
                             drive + 1);
  }
  return 0;
}

void emux_drive_change_model(int unit) {
  (void)unit;
}

void emux_set_warp(int warp) {
  ::bmx::apple2::g_warp = warp ? 1 : 0;
  ::bmx::apple2::timer_set_warp(warp != 0);
}

int emux_prepare_shutdown(void) {
  return 0;
}

// Atari-specific entry points reused by the common menu: no-ops here.
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

extern "C" int emux_atari_cart_types_for_kb(int kb, int *ids,
                                            const char **descs) {
  (void)kb;
  (void)ids;
  (void)descs;
  return 0;
}

extern "C" void emux_atari_set_cart_type(int type) {
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
  if (delta_x != NULL) {
    *delta_x = 0.0f;
  }
  if (delta_y != NULL) {
    *delta_y = 0.0f;
  }
  return 0;
}

void emux_kbd_set_latch_keyarr(int row, int col, int value) {
  (void)row;
  (void)col;
  (void)value;
}

void emux_keyboard_type_changed(void) {}

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
                                      char *buffer, size_t buffer_size) {
  (void)row;
  (void)column;
  (void)flags;
  if (buffer != NULL && buffer_size > 0) {
    buffer[0] = '\0';
  }
  return 0;
}

int emux_keymap_editor_begin(struct keymap_editor_model *model, int *editable,
                             char *error, size_t error_size) {
  (void)model;
  (void)error;
  (void)error_size;
  if (editable != NULL) {
    *editable = 0;
  }
  return 0;
}

int emux_keymap_editor_save(const struct keymap_editor_model *model,
                            char *error, size_t error_size) {
  (void)model;
  (void)error;
  (void)error_size;
  return 0;
}

int emux_keymap_editor_restore_defaults(char *error, size_t error_size) {
  (void)error;
  (void)error_size;
  return 0;
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

void emux_trap_main_loop_ui(void) {
  ::bmx::apple2::debug_log("menu: opened (F12)");
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
  ::bmx::apple2::debug_log("menu: closed");
}

void emux_trap_main_loop(void (*trap_func)(uint16_t, void *data), void *data) {
  (void)trap_func;
  (void)data;
}

const char *file_system_get_disk_name(unsigned int unit, unsigned int drive) {
  (void)drive;
  int slot = 6, drv = 0;
  if (unit == 9) {
    drv = 1;
  } else if (unit == 10) {
    slot = 5;
  } else if (unit == 11) {
    slot = 7;
  }
  const char *name = ::bmx::apple2::drive_attached_name(slot, drv);
  if (name == nullptr || name[0] == '\0') {
    return NULL;
  }
  return name;
}

const char *tape_get_file_name(int port) {
  (void)port;
  return NULL;
}

char *cartridge_get_filename_by_slot(int slot) {
  (void)slot;
  return NULL;
}

uint8_t *charset_petconvstring(uint8_t *text, int mode) {
  (void)mode;
  return text;
}

int kbdbuf_feed(const char *text) {
  ::bmx::apple2::input_type_text(text);
  return 0;
}

void lib_free(void *ptr) {
  free(ptr);
}

// -- menu construction -------------------------------------------------------

void emux_add_drive_option(struct menu_item *parent, int drive) {
  // Apple II drives: units 8/9 = S6D1/S6D2, 10 = S5D1, 11 = S7D1. Insert
  // reuses menu.c's generic attach flow (MENU_ATTACH_DISK_x sets the unit,
  // MENU_DISK_FILE calls emux_attach_disk_image); eject maps to
  // emux_detach_disk.
  if (drive != -1) {
    return;
  }
  static const char *names[4] = {"Slot 6 Drive 1 (5.25\")",
                                 "Slot 6 Drive 2 (5.25\")",
                                 "Slot 5 Drive 1 (3.5\")",
                                 "Slot 7 Drive 1 (HDD)"};
  static const int attach_ids[4] = {MENU_ATTACH_DISK_8, MENU_ATTACH_DISK_9,
                                    MENU_ATTACH_DISK_10, MENU_ATTACH_DISK_11};
  static const int eject_ids[4] = {MENU_DETACH_DISK_8, MENU_DETACH_DISK_9,
                                   MENU_DETACH_DISK_10, MENU_DETACH_DISK_11};
  for (int d = 0; d < 4; d++) {
    struct menu_item *folder = ui_menu_add_folder(parent, (char *)names[d]);
    ::bmx::apple2::s_drive_status[d] =
        ui_menu_add_button(MENU_TEXT, folder, (char *)"State");
    ui_menu_add_button(attach_ids[d], folder, (char *)"Insert disk...");
    ui_menu_add_button(eject_ids[d], folder, (char *)"Eject");
  }
  ::bmx::apple2::sync_drive_items();
}

void emux_add_keyboard_options(struct menu_item *parent) {
  struct menu_item *child = ::bmx::apple2::s_joystick_item =
      ui_menu_add_multiple_choice(MENU_APPLE2_JOYSTICK, parent,
                                  (char *)"Joystick");
  static const char *names[] = {"None", "Joystick 0", "Joystick 1"};
  child->num_choices = 3;
  for (int i = 0; i < 3; i++) {
    snprintf(child->choices[i], sizeof child->choices[i], "%s", names[i]);
    child->choice_ints[i] = i;
  }
  int cur = ::bmx::apple2::input_get_joystick();
  child->value = (cur >= 0 && cur < 3) ? cur : 1;
}

void emux_add_tape_options(struct menu_item *parent) {
  (void)parent;
  // No cassette support.
}

void emux_add_sound_options(struct menu_item *emulation_parent,
                            struct menu_item *sid_parent,
                            struct menu_item *sound_parent) {
  (void)sid_parent;
  (void)sound_parent;
  struct menu_item *child = ::bmx::apple2::s_volume_item =
      ui_menu_add_range(MENU_APPLE2_VOLUME, emulation_parent, (char *)"Volume %",
                        0, 100, 5, 100);
  (void)child;
}

void emux_add_reu_options(struct menu_item *parent) {
  (void)parent;
}

void emux_add_machine_options(struct menu_item *parent) {
  struct menu_item *child = ::bmx::apple2::s_model_item =
      ui_menu_add_multiple_choice(MENU_APPLE2_MACHINE, parent, (char *)"Model");
  int n = ::bmx::apple2::machine_model_count();
  if (n > 16) {
    n = 16;
  }
  child->num_choices = n;
  for (int i = 0; i < n; i++) {
    const ::bmx::apple2::Apple2Model *m = ::bmx::apple2::machine_model(i);
    snprintf(child->choices[i], sizeof child->choices[i], "%s", m->name);
    child->choice_ints[i] = i;
  }
  child->value = 0;
  {
    int cur = ::bmx::apple2::machine_current_index();
    for (int i = 0; i < child->num_choices; i++) {
      if (child->choice_ints[i] == cur) {
        child->value = i;
        break;
      }
    }
  }

  child = ::bmx::apple2::s_speed_item =
      ui_menu_add_multiple_choice(MENU_APPLE2_SPEED, parent, (char *)"Speed");
  static const char *speed_names[] = {"Unlimited", "1 MHz authentic",
                                      "2.8 MHz", "Zip"};
  child->num_choices = 4;
  for (int i = 0; i < 4; i++) {
    snprintf(child->choices[i], sizeof child->choices[i], "%s",
             speed_names[i]);
    child->choice_ints[i] = i;
  }
  {
    child->value = 1;
    for (int i = 0; i < child->num_choices; i++) {
      if (child->choice_ints[i] == g_limit_speed) {
        child->value = i;
        break;
      }
    }
  }

  child = ::bmx::apple2::s_monitor_item =
      ui_menu_add_multiple_choice(MENU_APPLE2_MONITOR, parent,
                                  (char *)"Monitor");
  child->num_choices = 4;
  snprintf(child->choices[0], sizeof child->choices[0], "Color");
  child->choice_ints[0] = BMX_MONITOR_COLOR;
  snprintf(child->choices[1], sizeof child->choices[1], "B&W");
  child->choice_ints[1] = BMX_MONITOR_BW;
  snprintf(child->choices[2], sizeof child->choices[2], "Green");
  child->choice_ints[2] = BMX_MONITOR_GREEN;
  snprintf(child->choices[3], sizeof child->choices[3], "Amber");
  child->choice_ints[3] = BMX_MONITOR_AMBER;
  {
    const int cur = ::bmx::apple2::video_get_monitor();
    child->value = 0;
    for (int i = 0; i < child->num_choices; i++) {
      if (child->choice_ints[i] == cur) {
        child->value = i;
        break;
      }
    }
  }

  struct menu_item *roms = ui_menu_add_folder(parent, (char *)"System ROMs");
  ui_menu_add_button(MENU_APPLE2_ROM_RESCAN, roms,
                     (char *)"Scan ROMs (/roms/APPLE2)");
}

struct menu_item *emux_add_cartridge_options(struct menu_item *parent) {
  return parent;
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
  return 0;
}

int emux_set_palette_setting(int display_num, const char *setting) {
  (void)display_num;
  (void)setting;
  return 0;
}

const char *emux_get_palette_setting(int display_num) {
  (void)display_num;
  return NULL;
}

void emux_video_color_setting_changed(int display_num) {
  (void)display_num;
}

namespace {
int clamp_color(int value, int max) {
  if (value < 0) {
    return 0;
  }
  if (value > max) {
    return max;
  }
  return value;
}

void set_one_color(int brightness, int contrast, int gamma, int tint,
                   int saturation) {
  ::bmx::apple2::video_set_colors(brightness, contrast, gamma, tint,
                                  saturation);
}
}  // namespace

void emux_set_color_brightness(int display_num, int value) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::apple2::video_get_colors(&bri, &con, &gam, &tin, &sat);
  set_one_color(clamp_color(value, 2000), con, gam, tin, sat);
}

void emux_set_color_contrast(int display_num, int value) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::apple2::video_get_colors(&bri, &con, &gam, &tin, &sat);
  set_one_color(bri, clamp_color(value, 2000), gam, tin, sat);
}

void emux_set_color_gamma(int display_num, int value) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::apple2::video_get_colors(&bri, &con, &gam, &tin, &sat);
  set_one_color(bri, con, clamp_color(value, 4000), tin, sat);
}

void emux_set_color_tint(int display_num, int value) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::apple2::video_get_colors(&bri, &con, &gam, &tin, &sat);
  set_one_color(bri, con, gam, clamp_color(value, 2000), sat);
}

void emux_set_color_saturation(int display_num, int value) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::apple2::video_get_colors(&bri, &con, &gam, &tin, &sat);
  set_one_color(bri, con, gam, tin, clamp_color(value, 2000));
}

int emux_get_color_brightness(int display_num) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::apple2::video_get_colors(&bri, &con, &gam, &tin, &sat);
  return bri;
}

int emux_get_color_contrast(int display_num) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::apple2::video_get_colors(&bri, &con, &gam, &tin, &sat);
  return con;
}

int emux_get_color_gamma(int display_num) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::apple2::video_get_colors(&bri, &con, &gam, &tin, &sat);
  return gam;
}

int emux_get_color_tint(int display_num) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::apple2::video_get_colors(&bri, &con, &gam, &tin, &sat);
  return tin;
}

int emux_get_color_saturation(int display_num) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::apple2::video_get_colors(&bri, &con, &gam, &tin, &sat);
  return sat;
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
    // No tape warp on Apple II; accepted for menu compatibility.
  }
}

void emux_set_int_1(IntSetting setting, int value, int param) {
  (void)param;
  emux_set_int(setting, value);
}

void emux_get_int(IntSetting setting, int *dest) {
  int v = 0;
  if (setting == Setting_WarpMode) {
    v = ::bmx::apple2::g_warp;
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

void emux_load_settings_done(void) {
  ::bmx::apple2::sync_all_menu_items();
}

void emux_load_additional_settings(void) {}

void emux_save_additional_settings(FILE *fp) {
  if (fp == nullptr) {
    return;
  }
  int bri, con, gam, tin, sat;
  ::bmx::apple2::video_get_colors(&bri, &con, &gam, &tin, &sat);
  fprintf(fp, "apple2_model=%d\n", ::bmx::apple2::machine_current_index());
  fprintf(fp, "apple2_joystick=%d\n", ::bmx::apple2::input_get_joystick());
  fprintf(fp, "apple2_speed=%d\n", g_limit_speed);
  fprintf(fp, "apple2_bri=%d\napple2_con=%d\napple2_gam=%d\napple2_tin=%d\n"
              "apple2_sat=%d\n",
          bri, con, gam, tin, sat);
  fprintf(fp, "apple2_monitor=%d\n", ::bmx::apple2::video_get_monitor());
}

int emux_handle_loaded_setting(char *name, char *value_str, int value) {
  (void)value_str;
  if (name == nullptr) {
    return 0;
  }
  if (!strcmp(name, "apple2_model")) {
    if (value >= 0 && value < ::bmx::apple2::machine_model_count()) {
      ::bmx::apple2::machine_init_model(value);
    }
    return 1;
  }
  if (!strcmp(name, "apple2_joystick")) {
    ::bmx::apple2::input_set_joystick(value);
    return 1;
  }
  if (!strcmp(name, "apple2_speed")) {
    if (value >= 0 && value <= 3) {
      g_limit_speed = value;
      ::bmx::apple2::sync_all_menu_items();
    }
    return 1;
  }
  if (!strcmp(name, "apple2_monitor")) {
    ::bmx::apple2::video_set_monitor(value);
    ::bmx::apple2::sync_all_menu_items();
    return 1;
  }
  if (!strcmp(name, "apple2_bri") || !strcmp(name, "apple2_con") ||
      !strcmp(name, "apple2_gam") || !strcmp(name, "apple2_tin") ||
      !strcmp(name, "apple2_sat")) {
    int bri, con, gam, tin, sat;
    ::bmx::apple2::video_get_colors(&bri, &con, &gam, &tin, &sat);
    if (!strcmp(name, "apple2_bri")) {
      bri = value;
    } else if (!strcmp(name, "apple2_con")) {
      con = value;
    } else if (!strcmp(name, "apple2_gam")) {
      gam = value;
    } else if (!strcmp(name, "apple2_tin")) {
      tin = value;
    } else {
      sat = value;
    }
    ::bmx::apple2::video_set_colors(bri, con, gam, tin, sat);
    return 1;
  }
  return 0;
}

int emux_handle_menu_change(struct menu_item *item) {
  if (item == NULL) {
    return 0;
  }
  switch (item->id) {
    case MENU_APPLE2_MACHINE: {
      int index = item->value;
      if (index >= 0 && index < item->num_choices) {
        index = item->choice_ints[index];
      }
      // Silent on success (atariST rule): the rebooted model is visible
      // behind the menu. Only a real failure reports.
      if (!::bmx::apple2::machine_init_model(index)) {
        ui_info("Model change failed (ROM?)");
      }
      return 1;
    }
    case MENU_APPLE2_JOYSTICK:
      ::bmx::apple2::input_set_joystick(item->value);
      return 1;
    case MENU_APPLE2_SPEED: {
      int v = item->value;
      if (v >= 0 && v < item->num_choices) {
        v = item->choice_ints[v];
      }
      if (v >= 0 && v <= 3) {
        g_limit_speed = v;
      }
      return 1;
    }
    case MENU_APPLE2_MONITOR: {
      int v = item->value;
      if (v >= 0 && v < item->num_choices) {
        v = item->choice_ints[v];
      }
      ::bmx::apple2::video_set_monitor(v);
      return 1;
    }
    case MENU_APPLE2_VOLUME: {
      audiovolume = item->value;
      return 1;
    }
    case MENU_APPLE2_ROM_RESCAN:
      ::bmx::apple2::config_rescan_roms();
      ui_info("ROM pool rescanned");
      return 1;
    case MENU_ATARI_SCREENSHOT: {
      // Auto-named BMP dump on SD:/screenshots (never beside
      // snapshots); the folder is created on demand.
      bmx_screenshot_mkdir("apple2");
      char path[64];
      bool saved = false;
      for (int n = 0; n < 100 && !saved; n++) {
        bmx_screenshot_path(path, sizeof(path), "apple2", n);
        FILE *probe = fopen(path, "rb");
        if (probe != NULL) {
          fclose(probe);
          continue;  // name taken
        }
        if (::bmx::apple2::video_save_screenshot(path)) {
          saved = true;
          char msg[64];
          snprintf(msg, sizeof(msg), "Saved shot%02d.bmp", n);
          ui_info(msg);
        } else if (n == 0) {
          break;  // dir itself unwritable
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

void emux_vice_easy_flash(void) {}

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

void emux_set_rs232_hayes_audio(int hayes_audio) {
  (void)hayes_audio;
}

// -- VICE-only link stubs (same set as the DOS kernel) ----------------------

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

int vice_network_socket_close(vice_network_socket_t *sockfd) {
  (void)sockfd;
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

int vice_network_get_errorcode(void) {
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
  ::bmx::apple2::debug_log("APPLE2: menu closed");
}

// Reboot-persistent trace for hung extractions (the on-screen overlay dies
// with the hang; this log file survives it). Overrides the weak no-op in
// zipbrowse.cpp for the Apple II kernel.
extern "C" void zipbrowse_trace(const char *line) {
  if (line != NULL) {
    ::bmx::apple2::debug_log("ziptrace: %s", line);
  }
}

}  // extern "C"
