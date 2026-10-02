//
// msx_bridge.cpp
//
// Emulator-specific API bridge between BMX common code (menu/UI/kernel) and
// the MSX core (vendored Z80 + BMX hardware module). Fulfils the shared
// emux_api.h contract; VICE-only facilities get safe defaults so the
// kernel links and the common menu behaves. No floppy in phase 1:
// cartridges (.rom, slot 1) and cassettes (.cas, BIOS trap loader).
//

#include "msx_bridge.h"
#include "msx_config.h"
#include "msx_debug.h"
#include "msx_input.h"
#include "msx_machine.h"
#include "msx_sound.h"
#include "msx_tape.h"
#include "msx_cart.h"
#include "msx_timer.h"
#include "msx_video.h"
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

namespace bmx {
namespace msx {

struct menu_item *s_model_item = nullptr;
struct menu_item *s_volume_item = nullptr;
struct menu_item *s_cart_status = nullptr;
struct menu_item *s_monitor_item = nullptr;

// Staged settings (applied live when the core is up).
int g_warp = 0;
int g_autostart_warp = 0;

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
}

// Refresh the Cartridge status row ("[empty]" or the image basename).
void sync_cart_item(void) {
  if (s_cart_status == nullptr) {
    return;
  }
  const char *name = cart_name();
  if (!cart_attached() || name == nullptr || name[0] == '\0') {
    snprintf(s_cart_status->name, MAX_MENU_STR, "[empty]");
    return;
  }
  const char *base = strrchr(name, '/');
  base = base != nullptr ? base + 1 : name;
  snprintf(s_cart_status->name, MAX_MENU_STR, "%s", base);
}

}  // namespace msx
}  // namespace bmx

// ===========================================================================
// C EMUX API (as declared in emux_api.h)
// ===========================================================================

extern "C" {

#include "emux_api.h"
#include "menu.h"
#include "menu_timing.h"
#include "ui.h"
#include "msx_zesarux.h"
#include "update/zipbrowse.h"

// -- machine ---------------------------------------------------------------

void emu_machine_init(int raster_skip_enabled, int raster_skip2_enabled) {
  (void)raster_skip_enabled;
  (void)raster_skip2_enabled;
  emux_machine_class = BMC64_MACHINE_CLASS_MSX;
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
  ::bmx::msx::machine_reset(isSoft != 0);
}

int emux_attach_disk_image(int unit, char *filename) {
  // No floppy in phase 1: report failure so the menu shows an error
  // instead of pretending.
  (void)unit;
  (void)filename;
  return -1;
}

void emux_detach_disk(int unit) {
  (void)unit;
}

int emux_attach_tape_image(char *filename) {
  if (filename == NULL) {
    return -1;
  }
  if (::bmx::msx::tape_attach(filename) != 0) {
    return -1;
  }
  ::bmx::msx::tape_rewind();
  // NOTE: the DIAG waveform dump is intentionally NOT restarted here: it
  // is one continuous file from boot until the card fills.
  return 0;
}

void emux_detach_tape(void) {
  ::bmx::msx::tape_detach();
}

int emux_attach_cart(int bank, char *filename) {
  (void)bank;
  if (filename == NULL) {
    return -1;
  }
  if (::bmx::msx::cart_attach(filename) != 0) {
    return -1;
  }
  ::bmx::msx::sync_cart_item();
  return 0;
}

void emux_detach_cart(int bank) {
  (void)bank;
  ::bmx::msx::cart_detach();
  ::bmx::msx::sync_cart_item();
}

void emux_set_cart_default(void) {}

int emux_load_state(char *filename) {
  (void)filename;
  // No snapshot format in phase 1.
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
  switch (cmd) {
    case EMUX_TAPE_REWIND:
    case EMUX_TAPE_RESET:
    case EMUX_TAPE_ZERO:
      ::bmx::msx::tape_rewind();
      break;
    case EMUX_TAPE_PLAY:
      ::bmx::msx::tape_set_playing(true);
      break;
    case EMUX_TAPE_STOP:
      ::bmx::msx::tape_set_playing(false);
      break;
    case EMUX_TAPE_RECORD:
    case EMUX_TAPE_FASTFORWARD:
      break;
    default:
      break;
  }
  return 0;
}

void emux_show_cart_osd_menu(void) {}

int emux_autostart_file(char *filename, unsigned int program_number) {
  (void)program_number;
  if (filename == NULL) {
    return -1;
  }
  size_t n = strlen(filename);
  const bool is_tape =
      n > 4 && (strcasecmp(filename + n - 4, ".cas") == 0);
  const bool is_cart =
      n > 4 && (strcasecmp(filename + n - 4, ".rom") == 0);
  if (is_cart) {
    // Cartridges boot themselves on reset: attach to slot 1 and reset.
    // Fresh waveform-capture window for the game audio.
    if (::bmx::msx::cart_attach(filename) != 0) {
      ::bmx::msx::debug_log("autostart: cart attach failed %s", filename);
      return -1;
    }
    ::bmx::msx::machine_reset(false);
    // NOTE: no sound_dump_restart: the DIAG dump is one continuous file
    // from boot until the card fills.
    ::bmx::msx::debug_log("autostart: cart attached, reset done");
    return 0;
  }
  if (is_tape) {
    if (::bmx::msx::tape_attach(filename) != 0) {
      return -1;
    }
  ::bmx::msx::tape_rewind();
  // NOTE: no sound_dump_restart here either: one continuous DIAG file
  // from boot until the card fills.
  // Autostart always starts from a clean machine, then types the
    // loader once the firmware reaches BASIC. No warp: the trap loader
    // is instant, so there is nothing to fast-forward through (and
    // nothing that would switch warp back off).
    ::bmx::msx::machine_reset(false);
    ::bmx::msx::input_type_autostart_cas();
    return 0;
  }
  ::bmx::msx::debug_log("autostart: unknown type %s", filename);
  return -1;
}

void emux_drive_change_model(int unit) {
  (void)unit;
}

void emux_set_warp(int warp) {
  ::bmx::msx::g_warp = warp ? 1 : 0;
  ::bmx::msx::timer_set_warp(warp != 0);
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
}

void emux_trap_main_loop(void (*trap_func)(uint16_t, void *data), void *data) {
  (void)trap_func;
  (void)data;
}

const char *file_system_get_disk_name(unsigned int unit, unsigned int drive) {
  (void)unit;
  (void)drive;
  return NULL;
}

const char *tape_get_file_name(int port) {
  (void)port;
  if (!::bmx::msx::tape_attached()) {
    return NULL;
  }
  return ::bmx::msx::tape_name();
}

char *cartridge_get_filename_by_slot(int slot) {
  (void)slot;
  if (!::bmx::msx::cart_attached()) {
    return NULL;
  }
  return (char *)::bmx::msx::cart_name();
}

uint8_t *charset_petconvstring(uint8_t *text, int mode) {
  (void)mode;
  return text;
}

int kbdbuf_feed(const char *text) {
  ::bmx::msx::input_type_text(text);
  return 0;
}

void lib_free(void *ptr) {
  free(ptr);
}

// -- menu construction -------------------------------------------------------

void emux_add_drive_option(struct menu_item *parent, int drive) {
  // MSX cartridges (slot 1): status + insert/eject. The Drives folder
  // hosts it (drive -1 = machine-wide options, same as CPC's A/B).
  if (drive != -1) {
    return;
  }
  struct menu_item *folder = ui_menu_add_folder(parent, (char *)"Cartridge");
  ::bmx::msx::s_cart_status =
      ui_menu_add_button(MENU_TEXT, folder, (char *)"State");
  ui_menu_add_button(MENU_MSX_CART, folder, (char *)"Insert cartridge...");
  ui_menu_add_button(MENU_MSX_CART_EJECT, folder, (char *)"Eject");
  ::bmx::msx::sync_cart_item();
}

void emux_add_keyboard_options(struct menu_item *parent) {
  (void)parent;
  // Host joystick drives PSG R14 always (port 1); no mode switch needed.
}

void emux_add_tape_options(struct menu_item *parent) {
  ui_menu_add_button(MENU_MSX_TAPE_REWIND, parent, (char *)"Rewind tape");
  ui_menu_add_button(MENU_MSX_TAPE_PLAY, parent, (char *)"Play/stop tape");
}

void emux_add_sound_options(struct menu_item *emulation_parent,
                            struct menu_item *sid_parent,
                            struct menu_item *sound_parent) {
  (void)sid_parent;
  (void)sound_parent;
  struct menu_item *child = ::bmx::msx::s_volume_item =
      ui_menu_add_range(MENU_MSX_VOLUME, emulation_parent, (char *)"Volume %",
                        0, 300, 5, 100);
  (void)child;
}

void emux_add_reu_options(struct menu_item *parent) {
  (void)parent;
}

void emux_add_machine_options(struct menu_item *parent) {
  struct menu_item *child = ::bmx::msx::s_model_item =
      ui_menu_add_multiple_choice(MENU_MSX_MACHINE, parent, (char *)"Model");
  int n = ::bmx::msx::machine_model_count();
  if (n > 16) {
    n = 16;
  }
  child->num_choices = n;
  for (int i = 0; i < n; i++) {
    const ::bmx::msx::MsxModel *m =
        ::bmx::msx::machine_model(i);
    snprintf(child->choices[i], sizeof child->choices[i], "%s", m->name);
    child->choice_ints[i] = i;
  }
  // Always alphabetical so every kernel lists models in the same order.
  for (int i = 1; i < child->num_choices; i++) {
    char tmp_name[MAX_MENU_STR];
    snprintf(tmp_name, sizeof tmp_name, "%s", child->choices[i]);
    int tmp_int = child->choice_ints[i];
    int tmp_dis = child->choice_disabled[i];
    int j = i - 1;
    while (j >= 0 && strcasecmp(child->choices[j], tmp_name) > 0) {
      snprintf(child->choices[j + 1], sizeof child->choices[j + 1], "%s",
               child->choices[j]);
      child->choice_ints[j + 1] = child->choice_ints[j];
      child->choice_disabled[j + 1] = child->choice_disabled[j];
      j--;
    }
    snprintf(child->choices[j + 1], sizeof child->choices[j + 1], "%s",
             tmp_name);
    child->choice_ints[j + 1] = tmp_int;
    child->choice_disabled[j + 1] = tmp_dis;
  }
  child->value = 0;
  {
    int cur = ::bmx::msx::machine_current_index();
    for (int i = 0; i < child->num_choices; i++) {
      if (child->choice_ints[i] == cur) {
        child->value = i;
        break;
      }
    }
  }

  child = ::bmx::msx::s_monitor_item =
      ui_menu_add_multiple_choice(MENU_MSX_MONITOR, parent, (char *)"Monitor");
  child->num_choices = 4;
  snprintf(child->choices[0], sizeof child->choices[0], "Color");
  child->choice_ints[0] = BMX_MONITOR_COLOR;
  snprintf(child->choices[1], sizeof child->choices[1], "B&W");
  child->choice_ints[1] = BMX_MONITOR_BW;
  snprintf(child->choices[2], sizeof child->choices[2], "Green");
  child->choice_ints[2] = BMX_MONITOR_GREEN;
  snprintf(child->choices[3], sizeof child->choices[3], "Amber");
  child->choice_ints[3] = BMX_MONITOR_AMBER;
  child->value = 0;
  {
    int cur = ::bmx::msx::video_get_monitor();
    for (int i = 0; i < child->num_choices; i++) {
      if (child->choice_ints[i] == cur) {
        child->value = i;
        break;
      }
    }
  }

  struct menu_item *roms = ui_menu_add_folder(parent, (char *)"System ROMs");
  ui_menu_add_button(MENU_MSX_ROM_RESCAN, roms,
                     (char *)"Scan ROMs (/roms/MSX)");
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
  ::bmx::msx::video_set_colors(brightness, contrast, gamma, tint,
                                      saturation);
}
}  // namespace

void emux_set_color_brightness(int display_num, int value) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::msx::video_get_colors(&bri, &con, &gam, &tin, &sat);
  set_one_color(clamp_color(value, 2000), con, gam, tin, sat);
}

void emux_set_color_contrast(int display_num, int value) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::msx::video_get_colors(&bri, &con, &gam, &tin, &sat);
  set_one_color(bri, clamp_color(value, 2000), gam, tin, sat);
}

void emux_set_color_gamma(int display_num, int value) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::msx::video_get_colors(&bri, &con, &gam, &tin, &sat);
  set_one_color(bri, con, clamp_color(value, 4000), tin, sat);
}

void emux_set_color_tint(int display_num, int value) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::msx::video_get_colors(&bri, &con, &gam, &tin, &sat);
  set_one_color(bri, con, gam, clamp_color(value, 2000), sat);
}

void emux_set_color_saturation(int display_num, int value) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::msx::video_get_colors(&bri, &con, &gam, &tin, &sat);
  set_one_color(bri, con, gam, tin, clamp_color(value, 2000));
}

int emux_get_color_brightness(int display_num) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::msx::video_get_colors(&bri, &con, &gam, &tin, &sat);
  return bri;
}

int emux_get_color_contrast(int display_num) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::msx::video_get_colors(&bri, &con, &gam, &tin, &sat);
  return con;
}

int emux_get_color_gamma(int display_num) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::msx::video_get_colors(&bri, &con, &gam, &tin, &sat);
  return gam;
}

int emux_get_color_tint(int display_num) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::msx::video_get_colors(&bri, &con, &gam, &tin, &sat);
  return tin;
}

int emux_get_color_saturation(int display_num) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::msx::video_get_colors(&bri, &con, &gam, &tin, &sat);
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
    ::bmx::msx::g_autostart_warp = value ? 1 : 0;
  }
}

void emux_set_int_1(IntSetting setting, int value, int param) {
  (void)param;
  emux_set_int(setting, value);
}

void emux_get_int(IntSetting setting, int *dest) {
  int v = 0;
  if (setting == Setting_WarpMode) {
    v = ::bmx::msx::g_warp;
  } else if (setting == Setting_AutostartWarp) {
    v = ::bmx::msx::g_autostart_warp;
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
  ::bmx::msx::sync_all_menu_items();
}

void emux_load_additional_settings(void) {}

void emux_save_additional_settings(FILE *fp) {
  if (fp == nullptr) {
    return;
  }
  int bri, con, gam, tin, sat;
  ::bmx::msx::video_get_colors(&bri, &con, &gam, &tin, &sat);
  fprintf(fp, "msx_model=%d\n", ::bmx::msx::machine_current_index());
  fprintf(fp, "msx_bri=%d\nmsx_con=%d\nmsx_gam=%d\nmsx_tin=%d\nmsx_sat=%d\n", bri,
          con, gam, tin, sat);
  fprintf(fp, "msx_monitor=%d\n", ::bmx::msx::video_get_monitor());
}

int emux_handle_loaded_setting(char *name, char *value_str, int value) {
  (void)value_str;
  if (name == nullptr) {
    return 0;
  }
  if (!strcmp(name, "msx_model")) {
    if (value >= 0 &&
        value < ::bmx::msx::machine_model_count()) {
      ::bmx::msx::machine_init_model(value);
    }
    return 1;
  }
  if (!strcmp(name, "msx_bri") || !strcmp(name, "msx_con") ||
      !strcmp(name, "msx_gam") || !strcmp(name, "msx_tin") ||
      !strcmp(name, "msx_sat")) {
    int bri, con, gam, tin, sat;
    ::bmx::msx::video_get_colors(&bri, &con, &gam, &tin, &sat);
    if (!strcmp(name, "msx_bri")) {
      bri = value;
    } else if (!strcmp(name, "msx_con")) {
      con = value;
    } else if (!strcmp(name, "msx_gam")) {
      gam = value;
    } else if (!strcmp(name, "msx_tin")) {
      tin = value;
    } else {
      sat = value;
    }
    ::bmx::msx::video_set_colors(bri, con, gam, tin, sat);
    return 1;
  }
  if (!strcmp(name, "msx_monitor")) {
    ::bmx::msx::video_set_monitor(value);
    ::bmx::msx::sync_all_menu_items();
    return 1;
  }
  return 0;
}

int emux_handle_menu_change(struct menu_item *item) {
  if (item == NULL) {
    return 0;
  }
  switch (item->id) {
    case MENU_MSX_MACHINE: {
      int index = item->value;
      if (index >= 0 && index < item->num_choices) {
        index = item->choice_ints[index];
      }
      // Silent on success: the rebooted model is visible behind the
      // menu. Only a real failure reports.
      if (!::bmx::msx::machine_init_model(index)) {
        ui_info("Model change failed (ROM?)");
      }
      return 1;
    }
    case MENU_MSX_VOLUME: {
      audiovolume = item->value;
      return 1;
    }
    case MENU_MSX_MONITOR: {
      int v = item->value;
      if (v >= 0 && v < item->num_choices) {
        v = item->choice_ints[v];
      }
      ::bmx::msx::video_set_monitor(v);
      return 1;
    }
    case MENU_MSX_ROM_RESCAN:
      ::bmx::msx::config_rescan_roms();
      ui_info("ROM pool rescanned");
      return 1;
    case MENU_MSX_TAPE_REWIND:
      ::bmx::msx::tape_rewind();
      return 1;
    case MENU_MSX_TAPE_PLAY: {
      bool playing = !::bmx::msx::tape_playing();
      ::bmx::msx::tape_set_playing(playing);
      ui_info(playing ? "Tape playing" : "Tape stopped");
      return 1;
    }
    case MENU_MSX_CART_EJECT:
      ::bmx::msx::cart_detach();
      ::bmx::msx::sync_cart_item();
      ui_info("Cartridge ejected");
      return 1;
    case MENU_ATARI_SCREENSHOT: {
      // Auto-named BMP dump of the emulator framebuffer on
      // SD:/screenshots (never beside snapshots). The folder chain is
      // created on demand so this works on older staged cards too.
      bmx_screenshot_mkdir("msx");
      char path[64];
      bool saved = false;
      for (int n = 0; n < 100 && !saved; n++) {
        bmx_screenshot_path(path, sizeof(path), "msx", n);
        FILE *probe = fopen(path, "rb");
        if (probe != NULL) {
          fclose(probe);
          continue;  // name taken
        }
        if (::bmx::msx::video_save_screenshot(path)) {
          saved = true;
          char msg[64];
          snprintf(msg, sizeof(msg), "Saved shot%02d.bmp", n);
          ui_info(msg);
        } else if (n == 0) {
          break;  // dir itself unwritable
        }
      }
      if (!saved) {
        ui_error("Screenshot failed");
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
  ::bmx::msx::debug_log("MSX: menu closed");
}

}  // extern "C"
