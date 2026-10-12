//
// a2600_bridge.cpp
//
// Emulator-specific API bridge between BMX common code (menu/UI/kernel) and
// the Stella Atari 2600 core. Fulfils the shared emux_api.h contract;
// VICE-only facilities (REU, userport RS232, drives, tapes, palettes...)
// get safe defaults so the kernel links and the common menu behaves.
// Cartridges only (.a26/.bin); the 2600 has no computer keyboard, but the
// console Select/Reset switches are mapped to user-choosable USB keys
// (Keyboard menu, defaults Space/Enter). No mouse/GPIO/network rows.
//

#include "a2600_bridge.h"
#include "a2600_config.h"
#include "a2600_debug.h"
#include "a2600_input.h"
#include "a2600_sound.h"
#include "a2600_cart.h"
#include "a2600_timer.h"
#include "a2600_video.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/types.h>

// Full struct menu_item for the anchors below (emux_api.h only forwards).
// keycodes.h joins it here so keycode_to_string()/raw_keycode_func use
// C linkage matching the C-compiled common library.
extern "C" {
#include "ui.h"
#include "keycodes.h"
}

namespace bmx {
namespace a2600 {

struct menu_item *s_volume_item = nullptr;
struct menu_item *s_cart_status = nullptr;
struct menu_item *s_diff_left_item = nullptr;
struct menu_item *s_diff_right_item = nullptr;
struct menu_item *s_select_key_item = nullptr;
struct menu_item *s_reset_key_item = nullptr;
struct menu_item *s_monitor_item = nullptr;
// Row currently waiting for a keypress (key capture armed), if any.
struct menu_item *s_key_wait_item = nullptr;

// Staged settings (applied live when the core is up).
int g_warp = 0;
int g_autostart_warp = 0;

void sync_all_menu_items(void) {
  if (s_volume_item != nullptr) {
    int vol = config_get_volume();
    if (vol < 0) {
      vol = 0;
    }
    if (vol > 300) {
      vol = 300;
    }
    s_volume_item->value = vol;
  }
  if (s_diff_left_item != nullptr) {
    s_diff_left_item->value = config_get_left_difficulty_a() ? 0 : 1;
  }
  if (s_diff_right_item != nullptr) {
    s_diff_right_item->value = config_get_right_difficulty_a() ? 0 : 1;
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
  sync_cart_item();
  sync_key_items();
}

// Refresh the Select/Reset shortcut rows with the bound key names.
void sync_key_items(void) {
  if (s_select_key_item != nullptr) {
    snprintf(s_select_key_item->displayed_value,
             sizeof(s_select_key_item->displayed_value), "%s",
             keycode_to_string(input_get_select_key()));
  }
  if (s_reset_key_item != nullptr) {
    snprintf(s_reset_key_item->displayed_value,
             sizeof(s_reset_key_item->displayed_value), "%s",
             keycode_to_string(input_get_reset_key()));
  }
}

// Drop an armed key capture (e.g. the menu was closed mid-capture) and
// restore the row label, so the next gameplay keypress is not swallowed.
void cancel_key_capture(void) {
  s_key_wait_item = nullptr;
  sync_key_items();
}

// Refresh the Cartridge status row ("[empty]" or the image basename).
void sync_cart_item(void) {
  if (s_cart_status == nullptr) {
    return;
  }
  const char *name = cart_path();
  if (!cart_has_console() || name == nullptr || name[0] == '\0') {
    snprintf(s_cart_status->name, MAX_MENU_STR, "[empty]");
    return;
  }
  const char *base = strrchr(name, '/');
  base = base != nullptr ? base + 1 : name;
  snprintf(s_cart_status->name, MAX_MENU_STR, "%s", base);
}

}  // namespace a2600
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
  emux_machine_class = BMC64_MACHINE_CLASS_A2600;
  emux_c64_core = BMC64_C64_CORE_UNKNOWN;
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

// -- sound -----------------------------------------------------------------

int emu_set_sound_sample_rate(int sample_rate) {
  (void)sample_rate;
  return 44100;
}

// -- reset / tapes / state -------------------------------------------------

void emux_reset(int isSoft) {
  (void)isSoft;
  // Fresh console from the same cartridge (2600 has no soft reset wire
  // here; reload the ROM which power-cycles the machine).
  const char *path = ::bmx::a2600::cart_path();
  if (path != nullptr && path[0] != '\0') {
    ::bmx::a2600::cart_create(path);
  }
}

int emux_attach_disk_image(int unit, char *filename) {
  // No floppy on a 2600: report failure so the menu shows an error
  // instead of pretending.
  (void)unit;
  (void)filename;
  return -1;
}

void emux_detach_disk(int unit) {
  (void)unit;
}

int emux_attach_tape_image(char *filename) {
  (void)filename;
  return -1;
}

void emux_detach_tape(void) {
}

int emux_attach_cart(int bank, char *filename) {
  (void)bank;
  if (filename == NULL) {
    return -1;
  }
  if (!::bmx::a2600::cart_create(filename)) {
    ::bmx::a2600::debug_log("attach: cart failed %s", filename);
    return -1;
  }
  ::bmx::a2600::sync_cart_item();
  return 0;
}

void emux_detach_cart(int bank) {
  (void)bank;
  ::bmx::a2600::cart_destroy();
  ::bmx::a2600::sync_cart_item();
}

void emux_set_cart_default(void) {}

int emux_load_state(char *filename) {
  if (filename == NULL) {
    return -1;
  }
  return ::bmx::a2600::cart_load_state(filename) ? 0 : -1;
}

int emux_save_state(char *filename) {
  if (filename == NULL) {
    return -1;
  }
  return ::bmx::a2600::cart_save_state(filename) ? 0 : -1;
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

int emux_autostart_file(char *filename, unsigned int program_number) {
  (void)program_number;
  if (filename == NULL) {
    return -1;
  }
  size_t n = strlen(filename);
  const bool is_cart =
      n > 4 && (strcasecmp(filename + n - 4, ".a26") == 0 ||
                strcasecmp(filename + n - 4, ".bin") == 0);
  if (!is_cart) {
    return -1;
  }
  if (!::bmx::a2600::cart_create(filename)) {
    ::bmx::a2600::debug_log("autostart: cart failed %s", filename);
    return -1;
  }
  ::bmx::a2600::sync_cart_item();
  ::bmx::a2600::debug_log("autostart: cart ready %s", filename);
  return 0;
}

void emux_drive_change_model(int unit) {
  (void)unit;
}

void emux_set_warp(int warp) {
  ::bmx::a2600::g_warp = warp ? 1 : 0;
  ::bmx::a2600::timer_set_warp(warp != 0);
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

extern "C" int emux_atari_cart_types_for_kb(int kb, int *ids,
                                            int max_ids) {
  (void)kb;
  (void)ids;
  (void)max_ids;
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
  (void)delta_x;
  (void)delta_y;
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
    snprintf(error, error_size, "No keyboard mapping on 2600");
  }
  return -1;
}

int emux_keymap_editor_save(const struct keymap_editor_model *model,
                            char *error, size_t error_size) {
  (void)model;
  if (error != NULL && error_size > 0) {
    snprintf(error, error_size, "No keyboard mapping on 2600");
  }
  return -1;
}

int emux_keymap_editor_restore_defaults(char *error, size_t error_size) {
  if (error != NULL && error_size > 0) {
    snprintf(error, error_size, "No keyboard mapping on 2600");
  }
  return -1;
}

vkbd_key_array emux_get_vkbd(void) {
  vkbd_key_array empty;
  memset(&empty, 0, sizeof(empty));
  return empty;
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
  bmx::a2600::debug_log("Atari2600: menu opened, entering pump");
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
  // A key capture left armed would swallow the next gameplay keypress.
  raw_keycode_func = NULL;
  ::bmx::a2600::cancel_key_capture();
  bmx::a2600::debug_log("Atari2600: menu pump exit, resuming emulation");
}

void emux_trap_main_loop(void (*trap_func)(uint16_t, void *data), void *data) {
  (void)trap_func;
  (void)data;
}

// -- VICE glue helpers referenced by kernel.cpp ----------------------------

const char *file_system_get_disk_name(unsigned int unit, unsigned int drive) {
  (void)unit;
  (void)drive;
  return NULL;
}

const char *tape_get_file_name(int port) {
  (void)port;
  return NULL;
}

char *cartridge_get_filename_by_slot(int slot) {
  (void)slot;
  return NULL;
}

}  // extern "C"

// Menu item handles (owned by the menu tree; valid while it is open).
namespace bmx {
namespace a2600 {
namespace {

void sync_difficulty_items(void) {
  if (s_diff_left_item != NULL) {
    s_diff_left_item->value = config_get_left_difficulty_a() ? 0 : 1;
  }
  if (s_diff_right_item != NULL) {
    s_diff_right_item->value = config_get_right_difficulty_a() ? 0 : 1;
  }
}

}  // namespace
}  // namespace a2600
}  // namespace bmx

extern "C" {

// -- menu construction -----------------------------------------------------

void emux_add_drive_option(struct menu_item *parent, int drive) {
  // 2600 cartridges: status + insert/eject. The Drives folder hosts it
  // (drive -1 = machine-wide options).
  if (drive != -1) {
    return;
  }
  struct menu_item *folder = ui_menu_add_folder(parent, (char *)"Cartridge");
  ::bmx::a2600::s_cart_status =
      ui_menu_add_button(MENU_TEXT, folder, (char *)"State");
  ui_menu_add_button(MENU_A2600_CART, folder, (char *)"Insert cartridge...");
  ui_menu_add_button(MENU_A2600_CART_EJECT, folder, (char *)"Eject");
  ::bmx::a2600::sync_cart_item();
}

// -- console-switch keyboard shortcuts -------------------------------------
// Key capture shared by the Select/Reset rows: activating a row arms the
// capture, and the next released USB key becomes the new shortcut.

static void a2600_keycode_captured(long keycode) {
  struct menu_item *item = ::bmx::a2600::s_key_wait_item;
  ::bmx::a2600::s_key_wait_item = NULL;
  raw_keycode_func = NULL;
  if (item == NULL) {
    return;
  }
  if (item == ::bmx::a2600::s_select_key_item) {
    ::bmx::a2600::input_set_select_key(keycode);
  } else if (item == ::bmx::a2600::s_reset_key_item) {
    ::bmx::a2600::input_set_reset_key(keycode);
  } else {
    return;
  }
  snprintf(item->displayed_value, sizeof(item->displayed_value), "%s",
           keycode_to_string(keycode));
}

static void a2600_console_key_row_activated(struct menu_item *item) {
  ::bmx::a2600::s_key_wait_item = item;
  raw_keycode_func = a2600_keycode_captured;
  snprintf(item->displayed_value, sizeof(item->displayed_value), "(waiting)");
}

void emux_add_keyboard_options(struct menu_item *parent) {
  // The 2600 has no computer keyboard, but its momentary console
  // switches want USB shortcuts. Each row shows the bound key; activating
  // it captures the next released key (same UX as Define Key Bindings).
  ::bmx::a2600::s_select_key_item = ui_menu_add_button_with_value(
      MENU_TEXT, parent, (char *)"Select key", 0, "",
      keycode_to_string(::bmx::a2600::input_get_select_key()));
  ::bmx::a2600::s_select_key_item->on_value_changed =
      a2600_console_key_row_activated;
  ::bmx::a2600::s_reset_key_item = ui_menu_add_button_with_value(
      MENU_TEXT, parent, (char *)"Reset key", 0, "",
      keycode_to_string(::bmx::a2600::input_get_reset_key()));
  ::bmx::a2600::s_reset_key_item->on_value_changed =
      a2600_console_key_row_activated;
  ::bmx::a2600::sync_key_items();
}

void emux_add_machine_options(struct menu_item *parent) {
  // Console switches: momentary Select/Reset plus difficulty A/B per
  // player. No models on a 2600.
  ui_menu_add_button(MENU_A2600_SELECT, parent, (char *)"Press Select");
  ui_menu_add_button(MENU_A2600_RESET, parent, (char *)"Press Reset");
  struct menu_item *child = ::bmx::a2600::s_diff_left_item =
      ui_menu_add_multiple_choice(MENU_A2600_DIFF_LEFT, parent,
                                  (char *)"Left difficulty");
  child->num_choices = 2;
  snprintf(child->choices[0], sizeof child->choices[0], "A (pro)");
  child->choice_ints[0] = 0;
  snprintf(child->choices[1], sizeof child->choices[1], "B (novice)");
  child->choice_ints[1] = 1;
  child = ::bmx::a2600::s_diff_right_item =
      ui_menu_add_multiple_choice(MENU_A2600_DIFF_RIGHT, parent,
                                  (char *)"Right difficulty");
  child->num_choices = 2;
  snprintf(child->choices[0], sizeof child->choices[0], "A (pro)");
  child->choice_ints[0] = 0;
  snprintf(child->choices[1], sizeof child->choices[1], "B (novice)");
  child->choice_ints[1] = 1;
  child = ::bmx::a2600::s_monitor_item =
      ui_menu_add_multiple_choice(MENU_A2600_MONITOR, parent,
                                  (char *)"Monitor");
  child->num_choices = 4;
  snprintf(child->choices[0], sizeof child->choices[0], "Color");
  child->choice_ints[0] = ::bmx::a2600::A2600_MONITOR_COLOR;
  snprintf(child->choices[1], sizeof child->choices[1], "B&W");
  child->choice_ints[1] = ::bmx::a2600::A2600_MONITOR_BW;
  snprintf(child->choices[2], sizeof child->choices[2], "Green");
  child->choice_ints[2] = ::bmx::a2600::A2600_MONITOR_GREEN;
  snprintf(child->choices[3], sizeof child->choices[3], "Amber");
  child->choice_ints[3] = ::bmx::a2600::A2600_MONITOR_AMBER;
  ::bmx::a2600::sync_all_menu_items();

  struct menu_item *roms = ui_menu_add_folder(parent, (char *)"About");
  ui_menu_add_button(MENU_TEXT, roms,
                     (char *)"2600 needs no BIOS ROMs");
}

void emux_add_tape_options(struct menu_item *parent) {
  (void)parent;
}

void emux_add_sound_options(struct menu_item *emulation_parent,
                            struct menu_item *sid_parent,
                            struct menu_item *sound_parent) {
  (void)sid_parent;
  (void)sound_parent;
  struct menu_item *child = ::bmx::a2600::s_volume_item =
      ui_menu_add_range(MENU_A2600_VOLUME, emulation_parent, (char *)"Volume %",
                        0, 300, 5, 100);
  (void)child;
}

void emux_add_reu_options(struct menu_item *parent) {
  (void)parent;
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
  if (display_num != 0) {
    return;
  }
  int bri = 0, con = 0, gam = 0, tin = 0, sat = 0;
  ::bmx::a2600::video_get_colors(&bri, &con, &gam, &tin, &sat);
  ::bmx::a2600::video_set_colors(bri, con, gam, tin, sat);
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
  ::bmx::a2600::video_set_colors(brightness, contrast, gamma, tint,
                                 saturation);
}
}  // namespace

void emux_set_color_brightness(int display_num, int value) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::a2600::video_get_colors(&bri, &con, &gam, &tin, &sat);
  set_one_color(clamp_color(value, 2000), con, gam, tin, sat);
}

void emux_set_color_contrast(int display_num, int value) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::a2600::video_get_colors(&bri, &con, &gam, &tin, &sat);
  set_one_color(bri, clamp_color(value, 2000), gam, tin, sat);
}

void emux_set_color_gamma(int display_num, int value) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::a2600::video_get_colors(&bri, &con, &gam, &tin, &sat);
  set_one_color(bri, con, clamp_color(value, 4000), tin, sat);
}

void emux_set_color_tint(int display_num, int value) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::a2600::video_get_colors(&bri, &con, &gam, &tin, &sat);
  set_one_color(bri, con, gam, clamp_color(value, 2000), sat);
}

void emux_set_color_saturation(int display_num, int value) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::a2600::video_get_colors(&bri, &con, &gam, &tin, &sat);
  set_one_color(bri, con, gam, tin, clamp_color(value, 2000));
}

int emux_get_color_brightness(int display_num) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::a2600::video_get_colors(&bri, &con, &gam, &tin, &sat);
  return bri;
}

int emux_get_color_contrast(int display_num) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::a2600::video_get_colors(&bri, &con, &gam, &tin, &sat);
  return con;
}

int emux_get_color_gamma(int display_num) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::a2600::video_get_colors(&bri, &con, &gam, &tin, &sat);
  return gam;
}

int emux_get_color_tint(int display_num) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::a2600::video_get_colors(&bri, &con, &gam, &tin, &sat);
  return tin;
}

int emux_get_color_saturation(int display_num) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::a2600::video_get_colors(&bri, &con, &gam, &tin, &sat);
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
    ::bmx::a2600::g_autostart_warp = value ? 1 : 0;
  }
}

void emux_set_int_1(IntSetting setting, int value, int param) {
  (void)param;
  emux_set_int(setting, value);
}

void emux_get_int(IntSetting setting, int *dest) {
  int v = 0;
  if (setting == Setting_WarpMode) {
    v = ::bmx::a2600::g_warp;
  } else if (setting == Setting_AutostartWarp) {
    v = ::bmx::a2600::g_autostart_warp;
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
  ::bmx::a2600::sync_all_menu_items();
}

void emux_load_additional_settings(void) {}

void emux_save_additional_settings(FILE *fp) {
  if (fp == nullptr) {
    return;
  }
  fprintf(fp, "a2600_volume=%d\n", ::bmx::a2600::config_get_volume());
  fprintf(fp, "a2600_diff_left=%d\n",
          ::bmx::a2600::config_get_left_difficulty_a() ? 0 : 1);
  fprintf(fp, "a2600_diff_right=%d\n",
          ::bmx::a2600::config_get_right_difficulty_a() ? 0 : 1);
  fprintf(fp, "a2600_key_select=%ld\n", ::bmx::a2600::input_get_select_key());
  fprintf(fp, "a2600_key_reset=%ld\n", ::bmx::a2600::input_get_reset_key());
  fprintf(fp, "a2600_monitor=%d\n", ::bmx::a2600::video_get_monitor());
}

int emux_handle_loaded_setting(char *name, char *value_str, int value) {
  (void)value_str;
  if (name == nullptr) {
    return 0;
  }
  if (!strcmp(name, "a2600_volume")) {
    ::bmx::a2600::config_set_volume(value);
    return 1;
  }
  if (!strcmp(name, "a2600_diff_left")) {
    ::bmx::a2600::config_set_left_difficulty_a(value == 0);
    return 1;
  }
  if (!strcmp(name, "a2600_diff_right")) {
    ::bmx::a2600::config_set_right_difficulty_a(value == 0);
    return 1;
  }
  if (!strcmp(name, "a2600_key_select")) {
    ::bmx::a2600::input_set_select_key((long)value);
    ::bmx::a2600::sync_key_items();
    return 1;
  }
  if (!strcmp(name, "a2600_key_reset")) {
    ::bmx::a2600::input_set_reset_key((long)value);
    ::bmx::a2600::sync_key_items();
    return 1;
  }
  if (!strcmp(name, "a2600_monitor")) {
    ::bmx::a2600::video_set_monitor(value);
    ::bmx::a2600::sync_all_menu_items();
    return 1;
  }
  return 0;
}

int emux_handle_menu_change(struct menu_item *item) {
  if (item == NULL) {
    return 0;
  }
  switch (item->id) {
    case MENU_A2600_SELECT:
      ::bmx::a2600::input_press_select();
      ui_info("Select pressed");
      return 1;
    case MENU_A2600_RESET:
      ::bmx::a2600::input_press_reset();
      ui_info("Reset pressed");
      return 1;
    case MENU_A2600_DIFF_LEFT: {
      int v = item->value;
      if (v >= 0 && v < item->num_choices) {
        v = item->choice_ints[v];
      }
      ::bmx::a2600::config_set_left_difficulty_a(v == 0);
      return 1;
    }
    case MENU_A2600_DIFF_RIGHT: {
      int v = item->value;
      if (v >= 0 && v < item->num_choices) {
        v = item->choice_ints[v];
      }
      ::bmx::a2600::config_set_right_difficulty_a(v == 0);
      return 1;
    }
    case MENU_A2600_MONITOR: {
      int v = item->value;
      if (v >= 0 && v < item->num_choices) {
        v = item->choice_ints[v];
      }
      // Per-pixel mapping at blit time: applies instantly, no rebuild.
      ::bmx::a2600::video_set_monitor(v);
      return 1;
    }
    case MENU_A2600_VOLUME: {
      int v = item->value;
      if (v < 0) {
        v = 0;
      }
      if (v > 300) {
        v = 300;
      }
      ::bmx::a2600::config_set_volume(v);
      return 1;
    }
    case MENU_A2600_CART_EJECT:
      ::bmx::a2600::cart_destroy();
      ::bmx::a2600::sync_cart_item();
      ui_info("Cartridge ejected");
      return 1;
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
  ::bmx::a2600::debug_log("A2600: menu closed");
}

}  // extern "C"
