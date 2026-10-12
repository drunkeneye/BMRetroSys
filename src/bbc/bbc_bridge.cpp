//
// bbc_bridge.cpp
//
// Emulator-specific API bridge between BMX common code (menu/UI/kernel) and
// the CLK BBC Micro core (vendored subset under third_party/bbc). Fulfils
// the shared emux_api.h contract; VICE-only facilities (REU, userport
// RS232, carts, palettes...) get safe defaults so the kernel links and
// the common menu behaves. No tape (the CLK BBC core has no cassette
// hookup) and no snapshots in v1: attach fails cleanly, state returns -1.
//

#include "bbc_bridge.h"
#include "bbc_config.h"
#include "bbc_debug.h"
#include "bbc_drive.h"
#include "bbc_fan.h"
#include "bbc_input.h"
#include "bbc_machine.h"
#include "bbc_sound.h"
#include "bbc_timer.h"
#include "bbc_video.h"
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

// Shared volume (defined in bbc_sound.cpp).
extern int audiovolume;

namespace bmx {
namespace bbc {

struct menu_item *s_model_item = nullptr;
struct menu_item *s_joystick_item = nullptr;
struct menu_item *s_speed_item = nullptr;
struct menu_item *s_monitor_item = nullptr;
struct menu_item *s_volume_item = nullptr;
struct menu_item *s_fan_item = nullptr;
struct menu_item *s_drive_status[2] = {nullptr, nullptr};

// Staged settings (applied live when the core is up).
int g_warp = 0;
double g_speed = 1.0;

// Speed menu choices (ints) <-> CLK multiplier.
double speed_mult_for(int choice) {
  switch (choice) {
    case 0:
      return 1.0;  // authentic 2 MHz
    case 1:
      return 2.0;
    case 2:
      return 4.0;
    case 3:
      return 8.0;  // warp pace on top
    default:
      return 1.0;
  }
}

int speed_choice_for(double mult) {
  if (mult >= 7.0) {
    return 3;
  }
  if (mult >= 3.0) {
    return 2;
  }
  if (mult >= 1.5) {
    return 1;
  }
  return 0;
}

void apply_speed_choice(int choice) {
  if (choice < 0 || choice > 3) {
    return;
  }
  g_speed = speed_mult_for(choice);
  ::bmx::bbc::machine_set_speed(g_speed);
  // Warp choice pins the frame pacer off; otherwise the warp toggle rules.
  ::bmx::bbc::timer_set_warp(choice == 3 || g_warp != 0);
}

// Disc autostart: attach, warp briefly past power-on, then SHIFT+BREAK
// into the MOS disc boot (*OPT 4 autoboot runs !BOOT; otherwise the
// machine lands at BASIC for manual start with *. / CHAIN). The whole
// sequence takes ~6s wall: warp wait (8x, unpaced) + recreate + shift
// hold through POST. Skipped if the user pressed keys meanwhile.
const unsigned long kAutostartWaitFrames = 100;
const unsigned long kAutostartShiftFrames = 200;
unsigned long s_autostart_wait = 0;
unsigned long s_autostart_shift = 0;
bool s_autostart_armed = false;
bool s_autostart_warp_save = false;
double s_autostart_speed_save = 1.0;

void bbc_autostart_poll(void) {
  if (!s_autostart_armed) {
    if (s_autostart_shift > 0) {
      if (--s_autostart_shift == 0) {
        ::bmx::bbc::machine_set_shift_held(false);
        ::bmx::bbc::debug_log("autostart: shift released");
      }
    }
    return;
  }
  if (s_autostart_wait > 0) {
    s_autostart_wait--;
    return;
  }
  s_autostart_armed = false;
  ::bmx::bbc::timer_set_warp(s_autostart_warp_save);
  ::bmx::bbc::g_warp = s_autostart_warp_save ? 1 : 0;
  ::bmx::bbc::machine_set_speed(s_autostart_speed_save);
  ::bmx::bbc::g_speed = s_autostart_speed_save;
  if (!::bmx::bbc::bbc_user_key_seen()) {
    ::bmx::bbc::machine_shift_break();
    s_autostart_shift = kAutostartShiftFrames;
    ::bmx::bbc::debug_log("autostart: shift+break (disc boot)");
  } else {
    ::bmx::bbc::debug_log("autostart: user keys seen, skip boot");
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
    const int cur = speed_choice_for(g_speed);
    for (int i = 0; i < s_speed_item->num_choices; i++) {
      if (s_speed_item->choice_ints[i] == cur) {
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
  if (s_fan_item != nullptr) {
    const int cur = fan_get_mode();
    for (int i = 0; i < s_fan_item->num_choices; i++) {
      if (s_fan_item->choice_ints[i] == cur) {
        s_fan_item->value = i;
        break;
      }
    }
  }
  sync_drive_items();
}

// Refresh the drive status rows ("[empty]" or the image basename).
// Order: Drive 0, Drive 1 (1770 DFS).
void sync_drive_items(void) {
  for (int d = 0; d < 2; d++) {
    if (s_drive_status[d] == nullptr) {
      continue;
    }
    const char *name = drive_attached_name(d);
    if (name == nullptr || name[0] == '\0') {
      snprintf(s_drive_status[d]->name, MAX_MENU_STR, "[empty]");
      continue;
    }
    const char *base = strrchr(name, '/');
    base = base != nullptr ? base + 1 : name;
    snprintf(s_drive_status[d]->name, MAX_MENU_STR, "%s", base);
  }
}

}  // namespace bbc
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
  emux_machine_class = BMC64_MACHINE_CLASS_BBC;
  emux_c64_core = BMC64_C64_CORE_UNKNOWN;
  ::bmx::bbc::debug_log("S2b emu_machine_init: class=BBC published");
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

// -- reset / disks / state -------------------------------------------------

void emux_reset(int isSoft) {
  ::bmx::bbc::debug_log("emux_reset(%s) enter", isSoft ? "soft" : "hard");
  ::bmx::bbc::machine_reset(isSoft != 0);
  ::bmx::bbc::debug_log("emux_reset done");
}

// Menu drive units 8/9 -> 1770 DFS drives 0/1. Unit 0 is the
// autostart/default flow (drive 0).
int emux_attach_disk_image(int unit, char *filename) {
  int drive = -1;
  if (unit == 8 || unit == 0) {
    drive = 0;
  } else if (unit == 9) {
    drive = 1;
  }
  if (drive < 0 || filename == NULL) {
    return -1;
  }
  if (::bmx::bbc::drive_attach(drive, filename) != 0) {
    return -1;
  }
  ::bmx::bbc::sync_drive_items();
  return 0;
}

void emux_detach_disk(int unit) {
  int drive = 0;
  if (unit == 9) {
    drive = 1;
  }
  ::bmx::bbc::drive_detach(drive);
  ::bmx::bbc::sync_drive_items();
}

int emux_attach_tape_image(char *filename) {
  (void)filename;
  return -1;  // no cassette hookup in the CLK BBC core
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

// BBC Micro discs autoboot on SHIFT+BREAK: attach the image to drive 0,
// reset to a clean machine, warp through POST, then shift+break into the
// disc boot (see bbc_autostart_poll). .adf needs the ADFS model; the DFS
// model refuses it at attach time. Returns 0 on success, -1 on failure.
int emux_autostart_file(char *filename, unsigned int program_number) {
  (void)program_number;
  if (filename == NULL) {
    return -1;
  }
  if (::bmx::bbc::drive_attach(0, filename) != 0) {
    ::bmx::bbc::debug_log("autostart: attach failed %s", filename);
    return -1;
  }
  ::bmx::bbc::sync_drive_items();
  // Warp past power-on at 8x (unpaced), then SHIFT+BREAK at the prompt.
  // No separate reset: shift_break recreates the machine itself.
  ::bmx::bbc::bbc_user_key_reset();
  ::bmx::bbc::s_autostart_warp_save = ::bmx::bbc::timer_warp();
  ::bmx::bbc::s_autostart_speed_save = ::bmx::bbc::g_speed;
  ::bmx::bbc::timer_set_warp(true);
  ::bmx::bbc::g_warp = 1;
  ::bmx::bbc::machine_set_speed(8.0);
  ::bmx::bbc::g_speed = 8.0;
  ::bmx::bbc::s_autostart_wait = ::bmx::bbc::kAutostartWaitFrames;
  ::bmx::bbc::s_autostart_armed = true;
  ::bmx::bbc::sync_all_menu_items();
  ::bmx::bbc::debug_log("autostart: attached d0, warp 8x, "
                        "shift+break in %lu frames",
                        ::bmx::bbc::kAutostartWaitFrames);
  return 0;
}

void emux_drive_change_model(int unit) {
  (void)unit;
}

void emux_set_warp(int warp) {
  ::bmx::bbc::g_warp = warp ? 1 : 0;
  ::bmx::bbc::timer_set_warp(warp != 0);
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
  ::bmx::bbc::debug_log("menu: opened (F12)");
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
  ::bmx::bbc::debug_log("menu: closed");
}

void emux_trap_main_loop(void (*trap_func)(uint16_t, void *data), void *data) {
  (void)trap_func;
  (void)data;
}

const char *file_system_get_disk_name(unsigned int unit, unsigned int drive) {
  (void)drive;
  int drv = 0;
  if (unit == 9) {
    drv = 1;
  }
  const char *name = ::bmx::bbc::drive_attached_name(drv);
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
  ::bmx::bbc::input_type_text(text);
  return 0;
}

void lib_free(void *ptr) {
  free(ptr);
}

// -- menu construction -------------------------------------------------------

void emux_add_drive_option(struct menu_item *parent, int drive) {
  // BBC Micro drives: units 8/9 = 1770 DFS drives 0/1 (.ssd/.dsd, plus
  // .adf on the ADFS model). Insert reuses menu.c's generic attach flow
  // (MENU_ATTACH_DISK_x sets the unit, MENU_DISK_FILE calls
  // emux_attach_disk_image); eject maps to emux_detach_disk.
  if (drive != -1) {
    return;
  }
  static const char *names[2] = {"Drive 0 (DFS 5.25\")",
                                 "Drive 1 (DFS 5.25\")"};
  static const int attach_ids[2] = {MENU_ATTACH_DISK_8, MENU_ATTACH_DISK_9};
  static const int eject_ids[2] = {MENU_DETACH_DISK_8, MENU_DETACH_DISK_9};
  for (int d = 0; d < 2; d++) {
    struct menu_item *folder = ui_menu_add_folder(parent, (char *)names[d]);
    ::bmx::bbc::s_drive_status[d] =
        ui_menu_add_button(MENU_TEXT, folder, (char *)"State");
    ui_menu_add_button(attach_ids[d], folder, (char *)"Insert disk...");
    ui_menu_add_button(eject_ids[d], folder, (char *)"Eject");
  }
  ::bmx::bbc::sync_drive_items();
}

void emux_add_keyboard_options(struct menu_item *parent) {
  struct menu_item *child = ::bmx::bbc::s_joystick_item =
      ui_menu_add_multiple_choice(MENU_BBC_JOYSTICK, parent,
                                  (char *)"Joystick");
  static const char *names[] = {"None", "Joystick 0", "Joystick 1"};
  child->num_choices = 3;
  for (int i = 0; i < 3; i++) {
    snprintf(child->choices[i], sizeof child->choices[i], "%s", names[i]);
    child->choice_ints[i] = i;
  }
  int cur = ::bmx::bbc::input_get_joystick();
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
  struct menu_item *child = ::bmx::bbc::s_volume_item =
      ui_menu_add_range(MENU_BBC_VOLUME, emulation_parent, (char *)"Volume %",
                        0, 100, 5, 100);
  (void)child;
}

void emux_add_reu_options(struct menu_item *parent) {
  (void)parent;
}

void emux_add_machine_options(struct menu_item *parent) {
  struct menu_item *child = ::bmx::bbc::s_model_item =
      ui_menu_add_multiple_choice(MENU_BBC_MACHINE, parent, (char *)"Model");
  int n = ::bmx::bbc::machine_model_count();
  if (n > 16) {
    n = 16;
  }
  child->num_choices = n;
  for (int i = 0; i < n; i++) {
    const ::bmx::bbc::BbcModel *m = ::bmx::bbc::machine_model(i);
    snprintf(child->choices[i], sizeof child->choices[i], "%s", m->name);
    child->choice_ints[i] = i;
  }
  child->value = 0;
  {
    int cur = ::bmx::bbc::machine_current_index();
    for (int i = 0; i < child->num_choices; i++) {
      if (child->choice_ints[i] == cur) {
        child->value = i;
        break;
      }
    }
  }

  child = ::bmx::bbc::s_speed_item =
      ui_menu_add_multiple_choice(MENU_BBC_SPEED, parent, (char *)"Speed");
  static const char *speed_names[] = {"Authentic 2 MHz", "2x", "4x",
                                      "Warp"};
  child->num_choices = 4;
  for (int i = 0; i < 4; i++) {
    snprintf(child->choices[i], sizeof child->choices[i], "%s",
             speed_names[i]);
    child->choice_ints[i] = i;
  }
  {
    child->value = 0;
    const int cur = ::bmx::bbc::speed_choice_for(::bmx::bbc::g_speed);
    for (int i = 0; i < child->num_choices; i++) {
      if (child->choice_ints[i] == cur) {
        child->value = i;
        break;
      }
    }
  }

  child = ::bmx::bbc::s_monitor_item =
      ui_menu_add_multiple_choice(MENU_BBC_MONITOR, parent,
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
    const int cur = ::bmx::bbc::video_get_monitor();
    child->value = 0;
    for (int i = 0; i < child->num_choices; i++) {
      if (child->choice_ints[i] == cur) {
        child->value = i;
        break;
      }
    }
  }

  struct menu_item *roms = ui_menu_add_folder(parent, (char *)"System ROMs");
  ui_menu_add_button(MENU_BBC_ROM_RESCAN, roms,
                     (char *)"Scan ROMs (/roms/BBC)");

  child = ::bmx::bbc::s_fan_item =
      ui_menu_add_multiple_choice(MENU_BBC_FAN, parent, (char *)"Fan");
  child->num_choices = ::bmx::bbc::BMX_FAN_MODES;
  for (int i = 0; i < ::bmx::bbc::BMX_FAN_MODES; i++) {
    snprintf(child->choices[i], sizeof child->choices[i], "%s",
             ::bmx::bbc::fan_mode_name(i));
    child->choice_ints[i] = i;
  }
  {
    const int cur = ::bmx::bbc::fan_get_mode();
    child->value = 0;
    for (int i = 0; i < child->num_choices; i++) {
      if (child->choice_ints[i] == cur) {
        child->value = i;
        break;
      }
    }
  }
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
  ::bmx::bbc::video_set_colors(brightness, contrast, gamma, tint,
                                  saturation);
}
}  // namespace

void emux_set_color_brightness(int display_num, int value) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::bbc::video_get_colors(&bri, &con, &gam, &tin, &sat);
  set_one_color(clamp_color(value, 2000), con, gam, tin, sat);
}

void emux_set_color_contrast(int display_num, int value) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::bbc::video_get_colors(&bri, &con, &gam, &tin, &sat);
  set_one_color(bri, clamp_color(value, 2000), gam, tin, sat);
}

void emux_set_color_gamma(int display_num, int value) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::bbc::video_get_colors(&bri, &con, &gam, &tin, &sat);
  set_one_color(bri, con, clamp_color(value, 4000), tin, sat);
}

void emux_set_color_tint(int display_num, int value) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::bbc::video_get_colors(&bri, &con, &gam, &tin, &sat);
  set_one_color(bri, con, gam, clamp_color(value, 2000), sat);
}

void emux_set_color_saturation(int display_num, int value) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::bbc::video_get_colors(&bri, &con, &gam, &tin, &sat);
  set_one_color(bri, con, gam, tin, clamp_color(value, 2000));
}

int emux_get_color_brightness(int display_num) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::bbc::video_get_colors(&bri, &con, &gam, &tin, &sat);
  return bri;
}

int emux_get_color_contrast(int display_num) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::bbc::video_get_colors(&bri, &con, &gam, &tin, &sat);
  return con;
}

int emux_get_color_gamma(int display_num) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::bbc::video_get_colors(&bri, &con, &gam, &tin, &sat);
  return gam;
}

int emux_get_color_tint(int display_num) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::bbc::video_get_colors(&bri, &con, &gam, &tin, &sat);
  return tin;
}

int emux_get_color_saturation(int display_num) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::bbc::video_get_colors(&bri, &con, &gam, &tin, &sat);
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
    // No tape warp on BBC Micro; accepted for menu compatibility.
  }
}

void emux_set_int_1(IntSetting setting, int value, int param) {
  (void)param;
  emux_set_int(setting, value);
}

void emux_get_int(IntSetting setting, int *dest) {
  int v = 0;
  if (setting == Setting_WarpMode) {
    v = ::bmx::bbc::g_warp;
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
  ::bmx::bbc::sync_all_menu_items();
}

void emux_load_additional_settings(void) {}

void emux_save_additional_settings(FILE *fp) {
  if (fp == nullptr) {
    return;
  }
  int bri, con, gam, tin, sat;
  ::bmx::bbc::video_get_colors(&bri, &con, &gam, &tin, &sat);
  fprintf(fp, "bbc_model=%d\n", ::bmx::bbc::machine_current_index());
  fprintf(fp, "bbc_joystick=%d\n", ::bmx::bbc::input_get_joystick());
  fprintf(fp, "bbc_speed=%d\n", ::bmx::bbc::speed_choice_for(::bmx::bbc::g_speed));
  fprintf(fp, "bbc_bri=%d\nbbc_con=%d\nbbc_gam=%d\nbbc_tin=%d\n"
              "bbc_sat=%d\n",
          bri, con, gam, tin, sat);
  fprintf(fp, "bbc_monitor=%d\n", ::bmx::bbc::video_get_monitor());
  fprintf(fp, "bbc_fan=%d\n", ::bmx::bbc::fan_get_mode());
}

int emux_handle_loaded_setting(char *name, char *value_str, int value) {
  (void)value_str;
  if (name == nullptr) {
    return 0;
  }
  if (!strcmp(name, "bbc_model")) {
    if (value >= 0 && value < ::bmx::bbc::machine_model_count()) {
      ::bmx::bbc::machine_init_model(value);
    }
    return 1;
  }
  if (!strcmp(name, "bbc_joystick")) {
    ::bmx::bbc::input_set_joystick(value);
    return 1;
  }
  if (!strcmp(name, "bbc_speed")) {
    if (value >= 0 && value <= 3) {
      ::bmx::bbc::apply_speed_choice(value);
      ::bmx::bbc::sync_all_menu_items();
    }
    return 1;
  }
  if (!strcmp(name, "bbc_monitor")) {
    ::bmx::bbc::video_set_monitor(value);
    ::bmx::bbc::sync_all_menu_items();
    return 1;
  }
  if (!strcmp(name, "bbc_fan")) {
    if (value >= 0 && value < ::bmx::bbc::BMX_FAN_MODES) {
      if (!::bmx::bbc::fan_set_mode(value)) {
        ui_info("Fan curve write failed");
      }
      ::bmx::bbc::sync_all_menu_items();
    }
    return 1;
  }
  if (!strcmp(name, "bbc_bri") || !strcmp(name, "bbc_con") ||
      !strcmp(name, "bbc_gam") || !strcmp(name, "bbc_tin") ||
      !strcmp(name, "bbc_sat")) {
    int bri, con, gam, tin, sat;
    ::bmx::bbc::video_get_colors(&bri, &con, &gam, &tin, &sat);
    if (!strcmp(name, "bbc_bri")) {
      bri = value;
    } else if (!strcmp(name, "bbc_con")) {
      con = value;
    } else if (!strcmp(name, "bbc_gam")) {
      gam = value;
    } else if (!strcmp(name, "bbc_tin")) {
      tin = value;
    } else {
      sat = value;
    }
    ::bmx::bbc::video_set_colors(bri, con, gam, tin, sat);
    return 1;
  }
  return 0;
}

int emux_handle_menu_change(struct menu_item *item) {
  if (item == NULL) {
    return 0;
  }
  switch (item->id) {
    case MENU_BBC_MACHINE: {
      int index = item->value;
      if (index >= 0 && index < item->num_choices) {
        index = item->choice_ints[index];
      }
      // Silent on success (atariST rule): the rebooted model is visible
      // behind the menu. Only a real failure reports.
      if (!::bmx::bbc::machine_init_model(index)) {
        ui_info("Model change failed (ROM?)");
      }
      return 1;
    }
    case MENU_BBC_JOYSTICK:
      ::bmx::bbc::input_set_joystick(item->value);
      return 1;
    case MENU_BBC_SPEED: {
      int v = item->value;
      if (v >= 0 && v < item->num_choices) {
        v = item->choice_ints[v];
      }
      ::bmx::bbc::apply_speed_choice(v);
      return 1;
    }
    case MENU_BBC_MONITOR: {
      int v = item->value;
      if (v >= 0 && v < item->num_choices) {
        v = item->choice_ints[v];
      }
      ::bmx::bbc::video_set_monitor(v);
      return 1;
    }
    case MENU_BBC_VOLUME: {
      audiovolume = item->value;
      return 1;
    }
    case MENU_BBC_ROM_RESCAN:
      ::bmx::bbc::config_rescan_roms();
      ui_info("ROM pool rescanned");
      return 1;
    case MENU_BBC_FAN: {
      int v = item->value;
      if (v >= 0 && v < item->num_choices) {
        v = item->choice_ints[v];
      }
      // Firmware-owned curve: rewritten into /config.txt, applies at
      // the next reboot (config.bak keeps the previous file).
      if (!::bmx::bbc::fan_set_mode(v)) {
        ui_info("Fan curve write failed");
      } else if (v == ::bmx::bbc::BMX_FAN_DEFAULT) {
        ui_info("Stock fan curve - reboot to apply");
      } else {
        ui_info("Fan curve written - reboot to apply");
      }
      return 1;
    }    case MENU_ATARI_SCREENSHOT: {
      // Auto-named BMP dump on SD:/screenshots (never beside
      // snapshots); the folder is created on demand.
      bmx_screenshot_mkdir("bbc");
      char path[64];
      bool saved = false;
      for (int n = 0; n < 100 && !saved; n++) {
        bmx_screenshot_path(path, sizeof(path), "bbc", n);
        FILE *probe = fopen(path, "rb");
        if (probe != NULL) {
          fclose(probe);
          continue;  // name taken
        }
        if (::bmx::bbc::video_save_screenshot(path)) {
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
  ::bmx::bbc::debug_log("BBC: menu closed");
}

// Reboot-persistent trace for hung extractions (the on-screen overlay dies
// with the hang; this log file survives it). Overrides the weak no-op in
// zipbrowse.cpp for the BBC Micro kernel.
extern "C" void zipbrowse_trace(const char *line) {
  if (line != NULL) {
    ::bmx::bbc::debug_log("ziptrace: %s", line);
  }
}

}  // extern "C"
