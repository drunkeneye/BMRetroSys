//
// amiga_bridge.cpp
//
// Emulator-specific API bridge between BMX common code and the vAmiga
// (A500 OCS) core. Same role as atarist_bridge.cpp for the ST kernel.
//
// Contract: third_party/common/emux_api.h. VICE-only facilities (REU,
// userport RS232, wifi, updates, easyflash, tapes, carts) get safe
// defaults. Amiga specifics: DF0-DF3 ADF attach (units 8-11 -> DF0-DF3),
// model/chipset/memory/Paula options, cold-boot autostart, Kickstart ROM
// change with reboot, BMP screenshots.
//
// Menu value flow: ui_init_menu() builds the Machine/Drives/Sound items
// from the staged HwSettings (config_hw); every change writes the staged
// value back and either live-applies (Paula audio, drive clicks) or
// full-applies + cold-resets the emulated machine (model, memory,
// chipset, CPU, drive wiring). Everything persists to settings-amiga.txt.
//

#include "amiga_bridge.h"
#include "amiga_config.h"
#include "amiga_debug.h"
#include "amiga_input.h"
#include "amiga_sound.h"
#include "amiga_video.h"
#include "src/monitor_phosphor.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>

extern "C" {

#include "emux_api.h"
#include "menu.h"
#include "menu_timing.h"

}  // extern "C"

namespace bmx {
namespace amiga {

namespace {

struct menu_item *s_df_status[4] = {NULL, NULL, NULL, NULL};
char s_df_state[4][64] = {
    {"State: Empty"}, {"State: Empty"}, {"State: Empty"}, {"State: Empty"}};

// Hardfile rows (HD0/HD1, built by emux_add_drive_option).
struct menu_item *s_hd_status[2] = {NULL, NULL};
char s_hd_state[2][64] = {{"State: Empty"}, {"State: Empty"}};

// Machine/Drives/Sound option items (built once by the emux_add_*
// functions below; kept for value sync after model presets change).
struct menu_item *s_model = NULL;
struct menu_item *s_agnus = NULL;
struct menu_item *s_denise = NULL;
struct menu_item *s_cpu = NULL;
struct menu_item *s_oc = NULL;
struct menu_item *s_blitter = NULL;
struct menu_item *s_cia = NULL;
struct menu_item *s_rtc = NULL;
struct menu_item *s_chip = NULL;
struct menu_item *s_slow = NULL;
struct menu_item *s_fast = NULL;
struct menu_item *s_slow_delay = NULL;
struct menu_item *s_slow_mirror = NULL;
struct menu_item *s_drv_on[4] = {NULL, NULL, NULL, NULL};
struct menu_item *s_drv_type[4] = {NULL, NULL, NULL, NULL};
struct menu_item *s_drv_mech[4] = {NULL, NULL, NULL, NULL};
struct menu_item *s_drv_sound = NULL;
struct menu_item *s_step_vol = NULL;
struct menu_item *s_aud_filter = NULL;
struct menu_item *s_aud_sampling = NULL;
struct menu_item *s_aud_voll = NULL;
struct menu_item *s_aud_volr = NULL;
struct menu_item *s_aud_vol[4] = {NULL, NULL, NULL, NULL};
struct menu_item *s_aud_pan[4] = {NULL, NULL, NULL, NULL};
// CRT monitor emulation row (Color/B&W/Green/Amber, applied at blit time).
struct menu_item *s_monitor_item = NULL;

}  // namespace

void sync_drive_items(void);
void sync_hd_items(void);
void sync_hd_state_from_core(void);
void sync_machine_items(void);
void sync_drive_setup_items(void);
void sync_sound_items(void);

}  // namespace amiga
}  // namespace bmx

extern "C" {

#include "emux_api.h"
#include "menu.h"
#include "menu_timing.h"

// -- machine ---------------------------------------------------------------

void emu_machine_init(int raster_skip_enabled, int raster_skip2_enabled) {
  (void)raster_skip_enabled;
  (void)raster_skip2_enabled;
  bmx::amiga::debug_log("emu_machine_init: enter");
  emux_machine_class = BMC64_MACHINE_CLASS_AMIGA;
  emux_c64_core = BMC64_C64_CORE_UNKNOWN;
  bmx::amiga::debug_log("emu_machine_init: class=AMIGA published");
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

int request_sound_sample_rate(int sample_rate) {
  (void)sample_rate;
  return 44100;
}

// -- reset / disks / state -------------------------------------------------

void emux_reset(int isSoft) {
  bmx::amiga::debug_log("emux_reset: %s", isSoft ? "warm" : "cold");
  bmx::amiga::config_reset(!isSoft);
}

static bool load_file_to_heap(const char *path, unsigned char **out,
                              long *len_out) {
  if (path == NULL || out == NULL) {
    return false;
  }
  FILE *fp = fopen(path, "rb");
  if (fp == NULL) {
    return false;
  }
  fseek(fp, 0, SEEK_END);
  long len = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  if (len <= 0 || len > 2 * 1024 * 1024) {
    fclose(fp);
    return false;
  }
  unsigned char *buf = (unsigned char *)malloc((size_t)len);
  if (buf == NULL) {
    fclose(fp);
    return false;
  }
  bool ok = fread(buf, 1, (size_t)len, fp) == (size_t)len;
  fclose(fp);
  if (!ok) {
    free(buf);
    return false;
  }
  *out = buf;
  if (len_out != NULL) {
    *len_out = len;
  }
  return true;
}

// VICE drive units 8/9/10/11 map onto DF0..DF3.
static int amiga_drive_from_unit(int unit) {
  if (unit >= 8 && unit <= 11) {
    return unit - 8;
  }
  return -1;
}

// Hardfiles can be hundreds of MB (ADF loader caps at 2MB).
static bool load_hdf_to_heap(const char *path, unsigned char **out,
                             long *len_out) {
  if (path == NULL || out == NULL) {
    return false;
  }
  FILE *fp = fopen(path, "rb");
  if (fp == NULL) {
    return false;
  }
  fseek(fp, 0, SEEK_END);
  long len = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  if (len <= 0 || len > 256L * 1024 * 1024) {
    fclose(fp);
    return false;
  }
  unsigned char *buf = (unsigned char *)malloc((size_t)len);
  if (buf == NULL) {
    fclose(fp);
    return false;
  }
  bool ok = fread(buf, 1, (size_t)len, fp) == (size_t)len;
  fclose(fp);
  if (!ok) {
    free(buf);
    return false;
  }
  *out = buf;
  if (len_out != NULL) {
    *len_out = len;
  }
  return true;
}

int emux_attach_disk_image(int unit, char *filename) {
  int drive = amiga_drive_from_unit(unit);
  if (drive < 0 || filename == NULL || *filename == '\0') {
    return -1;
  }
  unsigned char *buf = NULL;
  long len = 0;
  if (!load_file_to_heap(filename, &buf, &len)) {
    bmx::amiga::debug_log("attach: cannot read '%s'", filename);
    return -1;
  }
  bool ok = bmx::amiga::config_insert_adf(drive, buf, len, false);
  free(buf);
  if (!ok) {
    bmx::amiga::debug_log("attach: ADF insert failed for '%s'", filename);
    return -1;
  }
  snprintf(bmx::amiga::s_df_state[drive], 64, "State: %.40s", filename);
  bmx::amiga::debug_log("attach: df%d <- '%s' (%ld bytes)", drive, filename,
                        len);
  return 0;
}

void emux_detach_disk(int unit) {
  int drive = amiga_drive_from_unit(unit);
  if (drive < 0) {
    return;
  }
  bmx::amiga::config_eject(drive);
  snprintf(bmx::amiga::s_df_state[drive], 64, "State: Empty");
  bmx::amiga::debug_log("detach: df%d ejected", drive);
}

// Strong override of the weak emux_api stub: HD0/HD1 hardfiles.
int emux_attach_hdf_image(int drive, char *filename) {
  if ((drive != 0 && drive != 1) || filename == NULL || *filename == '\0') {
    return -1;
  }
  unsigned char *buf = NULL;
  long len = 0;
  if (!load_hdf_to_heap(filename, &buf, &len)) {
    bmx::amiga::debug_log("hdf attach: cannot read '%s'", filename);
    return -1;
  }
  bool ok = bmx::amiga::config_attach_hdf(drive, buf, len);
  free(buf);
  if (!ok) {
    bmx::amiga::debug_log("hdf attach: failed for '%s'", filename);
    return -1;
  }
  bmx::amiga::config_set_hd_path(drive, filename);
  snprintf(bmx::amiga::s_hd_state[drive], 64, "State: %.40s", filename);
  bmx::amiga::sync_hd_items();
  return 0;
}

void emux_detach_hdf_image(int drive) {
  if (drive != 0 && drive != 1) {
    return;
  }
  bmx::amiga::config_detach_hd(drive);
  bmx::amiga::config_set_hd_path(drive, NULL);
  snprintf(bmx::amiga::s_hd_state[drive], 64, "State: Empty");
  bmx::amiga::sync_hd_items();
  bmx::amiga::debug_log("detach: hd%d ejected", drive);
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

void emux_set_cart_default(void) {
}

int emux_load_state(char *filename) {
  if (filename == NULL || *filename == '\0') {
    return -1;
  }
  if (!bmx::amiga::config_load_snapshot(filename)) {
    bmx::amiga::debug_log("load_state FAILED '%s'", filename);
    return -1;
  }
  return 0;
}

int emux_save_state(char *filename) {
  if (filename == NULL || *filename == '\0') {
    return -1;
  }
  if (!bmx::amiga::config_save_snapshot(filename)) {
    bmx::amiga::debug_log("save_state FAILED '%s'", filename);
    return -1;
  }
  return 0;
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
  // Boot-a-floppy from the BMX file browser: insert into DF0 and
  // cold-reset so Kickstart boots it.
  (void)program_number;
  if (filename == NULL || *filename == '\0') {
    return -1;
  }
  if (emux_attach_disk_image(8, filename) != 0) {
    return -1;
  }
  bmx::amiga::debug_log("autostart: booting '%s'", filename);
  bmx::amiga::config_reset(true);
  return 0;
}

void emux_drive_change_model(int unit) {
  (void)unit;
}

void emux_set_warp(int warp) {
  bmx::amiga::config_set_warp(warp);
}

int emux_prepare_shutdown(void) {
  return 0;
}

// -- Atari 8-bit SIO/cart stubs (link-only; MENU_ATARI_* compiled always) --

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

void emux_atari_sync_drives(void) {
}

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
  if (row != NULL) {
    *row = -1;
  }
  if (column != NULL) {
    *column = -1;
  }
  if (flags != NULL) {
    *flags = 0;
  }
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
  if (editable != NULL) {
    *editable = 0;
  }
  if (error != NULL && error_size > 0) {
    error[0] = '\0';
  }
  return 0;
}

int emux_keymap_editor_save(const struct keymap_editor_model *model,
                            char *error, size_t error_size) {
  (void)model;
  if (error != NULL && error_size > 0) {
    error[0] = '\0';
  }
  return 0;
}

int emux_keymap_editor_restore_defaults(char *error, size_t error_size) {
  if (error != NULL && error_size > 0) {
    error[0] = '\0';
  }
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

// -- traps / main loop -----------------------------------------------------

void emux_trap_main_loop_ui(void) {
  bmx::amiga::debug_log("Amiga: menu opened, entering pump");
  bmx::amiga::sync_drive_items();
  bmx::amiga::sync_hd_state_from_core();
  {
    int dpx = 0, dpy = 0, fbw = 0, fbh = 0, sw = 0, sh = 0, dw = 0, dh = 0;
    circle_get_fbl_dimensions(FB_LAYER_VIC, &dpx, &dpy, &fbw, &fbh, &sw, &sh,
                              &dw, &dh);
    bmx::amiga::debug_log("Amiga: menu geometry dpx=%d dpy=%d", dpx, dpy);
    emux_geometry_changed(FB_LAYER_VIC);
  }
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
  bmx::amiga::debug_log("Amiga: menu pump exit, resuming emulation");
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

uint8_t *charset_petconvstring(uint8_t *text, int mode) {
  (void)mode;
  return text;
}

int kbdbuf_feed(const char *text) {
  (void)text;
  return -1;
}

void lib_free(void *ptr) {
  free(ptr);
}

}  // extern "C"

namespace bmx {
namespace amiga {

const char *drive_basename(const char *path) {
  const char *slash = strrchr(path, '/');
  return (slash != NULL && slash[1] != '\0') ? slash + 1 : path;
}

void sync_drive_items(void) {
  for (int d = 0; d < 4; d++) {
    if (s_df_status[d] != NULL) {
      snprintf(s_df_status[d]->name, MAX_MENU_STR, "%s", s_df_state[d]);
    }
  }
}

void sync_hd_items(void) {
  for (int d = 0; d < 2; d++) {
    if (s_hd_status[d] != NULL) {
      snprintf(s_hd_status[d]->name, MAX_MENU_STR, "%s", s_hd_state[d]);
    }
  }
}

void sync_hd_state_from_core(void) {
  // Reconcile after boot/attach: live attachment wins, staged path shows
  // where it came from, otherwise empty.
  for (int d = 0; d < 2; d++) {
    if (config_hd_attached(d)) {
      if (strcmp(s_hd_state[d], "State: Empty") == 0) {
        const char *staged = config_hd_path(d);
        if (staged != NULL && staged[0] != '\0') {
          snprintf(s_hd_state[d], 64, "State: %.40s", staged);
        } else {
          snprintf(s_hd_state[d], 64, "State: attached");
        }
      }
    } else {
      snprintf(s_hd_state[d], 64, "State: Empty");
    }
  }
  sync_hd_items();
}

// Selects the choice index whose choice_ints[] equals v.
void set_choice(struct menu_item *item, int v) {
  if (item == NULL) {
    return;
  }
  for (int i = 0; i < item->num_choices; i++) {
    if (item->choice_ints[i] == v) {
      item->value = i;
      return;
    }
  }
  item->value = 0;
}

void sync_machine_items(void) {
  const HwSettings &h = config_hw();
  set_choice(s_model, h.model);
  set_choice(s_agnus, h.agnus);
  set_choice(s_denise, h.denise);
  set_choice(s_cpu, h.cpu);
  set_choice(s_blitter, h.blitter);
  set_choice(s_cia, h.cia);
  set_choice(s_rtc, h.rtc);
  set_choice(s_chip, h.chip_kb);
  set_choice(s_slow, h.slow_kb);
  set_choice(s_fast, h.fast_kb);
  if (s_oc != NULL) {
    s_oc->value = h.overclock;
  }
  if (s_slow_delay != NULL) {
    s_slow_delay->value = h.slow_delay ? 1 : 0;
  }
  if (s_slow_mirror != NULL) {
    s_slow_mirror->value = h.slow_mirror ? 1 : 0;
  }
  set_choice(s_monitor_item, video_get_monitor());
}

void sync_drive_setup_items(void) {
  const HwSettings &h = config_hw();
  for (int d = 0; d < 4; d++) {
    if (s_drv_on[d] != NULL) {
      s_drv_on[d]->value = h.drv_on[d] ? 1 : 0;
    }
    set_choice(s_drv_type[d], h.drv_type[d]);
    set_choice(s_drv_mech[d], h.drv_mech[d]);
  }
  if (s_drv_sound != NULL) {
    s_drv_sound->value = h.drv_sound ? 1 : 0;
  }
  if (s_step_vol != NULL) {
    s_step_vol->value = h.drv_step_vol;
  }
}

void sync_sound_items(void) {
  const HwSettings &h = config_hw();
  set_choice(s_aud_filter, h.aud_filter);
  set_choice(s_aud_sampling, h.aud_sampling);
  if (s_aud_voll != NULL) {
    s_aud_voll->value = h.aud_vol_l;
  }
  if (s_aud_volr != NULL) {
    s_aud_volr->value = h.aud_vol_r;
  }
  for (int c = 0; c < 4; c++) {
    if (s_aud_vol[c] != NULL) {
      s_aud_vol[c]->value = h.aud_vol[c];
    }
    if (s_aud_pan[c] != NULL) {
      s_aud_pan[c]->value = h.aud_pan[c];
    }
  }
}

}  // namespace amiga
}  // namespace bmx

extern "C" {

// -- settings / video / palette --------------------------------------------

void emux_add_drive_option(struct menu_item *parent, int drive) {
  // DF0..DF3 (units 8..11). Insert reuses menu.c's generic attach flow
  // (MENU_ATTACH_DISK_x sets the unit, MENU_DISK_FILE calls
  // emux_attach_disk_image); eject maps to emux_detach_disk.
  // Per-unit calls are unused, like the Hatari bridge.
  if (drive != -1) {
    return;
  }
  static const char *names[4] = {"Drive DF0", "Drive DF1", "Drive DF2",
                                 "Drive DF3"};
  static const int attach_ids[4] = {MENU_ATTACH_DISK_8, MENU_ATTACH_DISK_9,
                                    MENU_ATTACH_DISK_10, MENU_ATTACH_DISK_11};
  static const int eject_ids[4] = {MENU_DETACH_DISK_8, MENU_DETACH_DISK_9,
                                   MENU_DETACH_DISK_10, MENU_DETACH_DISK_11};
  static const int on_ids[4] = {0, MENU_AMIGA_DF1_ON, MENU_AMIGA_DF2_ON,
                                MENU_AMIGA_DF3_ON};
  static const int type_ids[4] = {MENU_AMIGA_DF0_TYPE, MENU_AMIGA_DF1_TYPE,
                                  MENU_AMIGA_DF2_TYPE, MENU_AMIGA_DF3_TYPE};
  static const int mech_ids[4] = {MENU_AMIGA_DF0_MECH, MENU_AMIGA_DF1_MECH,
                                  MENU_AMIGA_DF2_MECH, MENU_AMIGA_DF3_MECH};
  const bmx::amiga::HwSettings &h = bmx::amiga::config_hw();
  for (int d = 0; d < 4; d++) {
    struct menu_item *folder = ui_menu_add_folder(parent, (char *)names[d]);
    bmx::amiga::s_df_status[d] =
        ui_menu_add_button(MENU_TEXT, folder, bmx::amiga::s_df_state[d]);
    ui_menu_add_button(attach_ids[d], folder, (char *)"Insert disk...");
    ui_menu_add_button(eject_ids[d], folder, (char *)"Eject");
    if (d > 0) {
      // DF0 is internal; externals can be wired up or removed.
      bmx::amiga::s_drv_on[d] = ui_menu_add_toggle(
          on_ids[d], folder, (char *)"Connected", h.drv_on[d] ? 1 : 0);
    }
    struct menu_item *child = bmx::amiga::s_drv_type[d] =
        ui_menu_add_multiple_choice(type_ids[d], folder, (char *)"Type");
    child->num_choices = 3;
    strcpy(child->choices[0], "3.5\" DD");
    child->choice_ints[0] = 0;
    strcpy(child->choices[1], "3.5\" HD");
    child->choice_ints[1] = 1;
    strcpy(child->choices[2], "5.25\" DD");
    child->choice_ints[2] = 2;
    child = bmx::amiga::s_drv_mech[d] =
        ui_menu_add_multiple_choice(mech_ids[d], folder, (char *)"Mechanics");
    child->num_choices = 2;
    strcpy(child->choices[0], "No delays");
    child->choice_ints[0] = 0;
    strcpy(child->choices[1], "A1010");
    child->choice_ints[1] = 1;
  }
  bmx::amiga::s_drv_sound = ui_menu_add_toggle(
      MENU_AMIGA_DRIVE_SOUND, parent, (char *)"Drive sounds",
      h.drv_sound ? 1 : 0);
  bmx::amiga::s_step_vol = ui_menu_add_range(
      MENU_AMIGA_STEP_VOL, parent, (char *)"Drive click volume", 0, 100, 1,
      h.drv_step_vol);
  // Hardfiles HD0/HD1 (.hdf, up to 256MB each, memory-backed). Attach
  // reuses the generic browser via MENU_AMIGA_HDFx; eject is a bridge
  // button (config_detach_hd). Staged paths re-attach at boot.
  static const int hdf_ids[2] = {MENU_AMIGA_HDF0, MENU_AMIGA_HDF1};
  static const int hdf_eject_ids[2] = {MENU_AMIGA_HDF0_DETACH,
                                       MENU_AMIGA_HDF1_DETACH};
  for (int d = 0; d < 2; d++) {
    char folder_name[16];
    snprintf(folder_name, sizeof(folder_name), "Hard Drive HD%d", d);
    struct menu_item *hdfolder = ui_menu_add_folder(parent, folder_name);
    // Show the staged path until the core is up / attach resolves.
    {
      const char *staged = bmx::amiga::config_hd_path(d);
      if (staged != NULL && staged[0] != '\0') {
        snprintf(bmx::amiga::s_hd_state[d], 64, "State: %.40s", staged);
      }
    }
    bmx::amiga::s_hd_status[d] =
        ui_menu_add_button(MENU_TEXT, hdfolder, bmx::amiga::s_hd_state[d]);
    ui_menu_add_button(hdf_ids[d], hdfolder, (char *)"Attach hardfile...");
    ui_menu_add_button(hdf_eject_ids[d], hdfolder, (char *)"Eject");
  }
  bmx::amiga::sync_drive_items();
  bmx::amiga::sync_hd_state_from_core();
  bmx::amiga::sync_drive_setup_items();
}

void emux_add_keyboard_options(struct menu_item *parent) {
  (void)parent;
}

void emux_add_tape_options(struct menu_item *parent) {
  (void)parent;
}

void emux_add_sound_options(struct menu_item *emulation_parent,
                            struct menu_item *sid_parent,
                            struct menu_item *sound_parent) {
  // No SID on the Amiga; the Paula (audio filter, resampler, channel and
  // master volumes/pans) lives here instead. All live-applied, no reboot.
  (void)sid_parent;
  (void)sound_parent;
  const bmx::amiga::HwSettings &h = bmx::amiga::config_hw();
  struct menu_item *child = bmx::amiga::s_aud_filter =
      ui_menu_add_multiple_choice(MENU_AMIGA_AUD_FILTER, emulation_parent,
                                  (char *)"Audio filter");
  child->num_choices = 7;
  strcpy(child->choices[0], "None");
  child->choice_ints[0] = 0;
  strcpy(child->choices[1], "A500");
  child->choice_ints[1] = 1;
  strcpy(child->choices[2], "A1000");
  child->choice_ints[2] = 2;
  strcpy(child->choices[3], "A1200");
  child->choice_ints[3] = 3;
  strcpy(child->choices[4], "Low-pass");
  child->choice_ints[4] = 4;
  strcpy(child->choices[5], "LED");
  child->choice_ints[5] = 5;
  strcpy(child->choices[6], "High-pass");
  child->choice_ints[6] = 6;
  child = bmx::amiga::s_aud_sampling =
      ui_menu_add_multiple_choice(MENU_AMIGA_AUD_SAMPLING, emulation_parent,
                                  (char *)"Resampling");
  child->num_choices = 3;
  strcpy(child->choices[0], "Latest sample");
  child->choice_ints[0] = 0;
  strcpy(child->choices[1], "Nearest");
  child->choice_ints[1] = 1;
  strcpy(child->choices[2], "Linear");
  child->choice_ints[2] = 2;
  bmx::amiga::s_aud_voll = ui_menu_add_range(
      MENU_AMIGA_AUD_VOLL, emulation_parent, (char *)"Master volume L", 0,
      100, 1, h.aud_vol_l);
  bmx::amiga::s_aud_volr = ui_menu_add_range(
      MENU_AMIGA_AUD_VOLR, emulation_parent, (char *)"Master volume R", 0,
      100, 1, h.aud_vol_r);
  static const int vol_ids[4] = {MENU_AMIGA_AUD_VOL0, MENU_AMIGA_AUD_VOL1,
                                 MENU_AMIGA_AUD_VOL2, MENU_AMIGA_AUD_VOL3};
  static const int pan_ids[4] = {MENU_AMIGA_AUD_PAN0, MENU_AMIGA_AUD_PAN1,
                                 MENU_AMIGA_AUD_PAN2, MENU_AMIGA_AUD_PAN3};
  for (int c = 0; c < 4; c++) {
    char label[32];
    snprintf(label, sizeof(label), "Channel %d volume", c);
    bmx::amiga::s_aud_vol[c] = ui_menu_add_range(
        vol_ids[c], emulation_parent, label, 0, 100, 1, h.aud_vol[c]);
    snprintf(label, sizeof(label), "Channel %d pan", c);
    bmx::amiga::s_aud_pan[c] = ui_menu_add_range(
        pan_ids[c], emulation_parent, label, 0, 400, 10, h.aud_pan[c]);
  }
  bmx::amiga::sync_sound_items();
}

void emux_add_reu_options(struct menu_item *parent) {
  (void)parent;
}

void emux_add_machine_options(struct menu_item *parent) {
  // Model, chipset, CPU and memory. Anything here (except the info rows)
  // full-applies the staged hardware and cold-resets the emulated Amiga;
  // disks stay inserted across the reset like on the real machine.
  const bmx::amiga::HwSettings &h = bmx::amiga::config_hw();
  struct menu_item *child = bmx::amiga::s_model =
      ui_menu_add_multiple_choice(MENU_AMIGA_MODEL, parent, (char *)"Model");
  child->num_choices = 5;
  strcpy(child->choices[0], "Amiga 1000 (OCS, 1MB)");
  child->choice_ints[0] = 0;
  strcpy(child->choices[1], "Amiga 500 (OCS, 1MB)");
  child->choice_ints[1] = 1;
  strcpy(child->choices[2], "Amiga 500 (ECS, 1MB)");
  child->choice_ints[2] = 2;
  strcpy(child->choices[3], "Amiga 500+ (ECS, 1MB)");
  child->choice_ints[3] = 3;
  strcpy(child->choices[4], "Amiga 1200 (AGA, 2MB)");
  child->choice_ints[4] = 4;
  // Always alphabetical so every kernel lists machines in the same order.
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
  child = bmx::amiga::s_agnus =
      ui_menu_add_multiple_choice(MENU_AMIGA_AGNUS, parent, (char *)"Agnus");
  child->num_choices = 5;
  strcpy(child->choices[0], "OCS 8367 (A1000)");
  child->choice_ints[0] = 0;
  strcpy(child->choices[1], "OCS 8371 (A500)");
  child->choice_ints[1] = 1;
  strcpy(child->choices[2], "ECS 8372 (1MB)");
  child->choice_ints[2] = 2;
  strcpy(child->choices[3], "ECS 8375 (2MB)");
  child->choice_ints[3] = 3;
  strcpy(child->choices[4], "AGA Alice");
  child->choice_ints[4] = 4;
  child = bmx::amiga::s_denise =
      ui_menu_add_multiple_choice(MENU_AMIGA_DENISE, parent, (char *)"Denise");
  child->num_choices = 3;
  strcpy(child->choices[0], "OCS 8362");
  child->choice_ints[0] = 0;
  strcpy(child->choices[1], "ECS 8373");
  child->choice_ints[1] = 1;
  strcpy(child->choices[2], "AGA Lisa");
  child->choice_ints[2] = 2;
  child = bmx::amiga::s_cpu =
      ui_menu_add_multiple_choice(MENU_AMIGA_CPU, parent, (char *)"CPU");
  child->num_choices = 3;
  strcpy(child->choices[0], "68000");
  child->choice_ints[0] = 0;
  strcpy(child->choices[1], "68010");
  child->choice_ints[1] = 1;
  strcpy(child->choices[2], "68EC020");
  child->choice_ints[2] = 2;
  bmx::amiga::s_oc = ui_menu_add_range(MENU_AMIGA_OVERCLOCK, parent,
                                       (char *)"CPU overclock", 0, 8, 1,
                                       h.overclock);
  child = bmx::amiga::s_blitter = ui_menu_add_multiple_choice(
      MENU_AMIGA_BLITTER, parent, (char *)"Blitter");
  child->num_choices = 3;
  strcpy(child->choices[0], "Fast (no bus use)");
  child->choice_ints[0] = 0;
  strcpy(child->choices[1], "Fast (bus cycles)");
  child->choice_ints[1] = 1;
  strcpy(child->choices[2], "Cycle-exact");
  child->choice_ints[2] = 2;
  child = bmx::amiga::s_cia =
      ui_menu_add_multiple_choice(MENU_AMIGA_CIA, parent, (char *)"CIA");
  child->num_choices = 2;
  strcpy(child->choices[0], "8520 DIP");
  child->choice_ints[0] = 0;
  strcpy(child->choices[1], "8520 PLCC");
  child->choice_ints[1] = 1;
  child = bmx::amiga::s_rtc =
      ui_menu_add_multiple_choice(MENU_AMIGA_RTC, parent, (char *)"RTC");
  child->num_choices = 3;
  strcpy(child->choices[0], "None");
  child->choice_ints[0] = 0;
  strcpy(child->choices[1], "OKI");
  child->choice_ints[1] = 1;
  strcpy(child->choices[2], "Ricoh");
  child->choice_ints[2] = 2;
  child = bmx::amiga::s_chip =
      ui_menu_add_multiple_choice(MENU_AMIGA_CHIP, parent, (char *)"Chip RAM");
  child->num_choices = 4;
  strcpy(child->choices[0], "256 KB");
  child->choice_ints[0] = 256;
  strcpy(child->choices[1], "512 KB");
  child->choice_ints[1] = 512;
  strcpy(child->choices[2], "1 MB");
  child->choice_ints[2] = 1024;
  strcpy(child->choices[3], "2 MB");
  child->choice_ints[3] = 2048;
  child = bmx::amiga::s_slow =
      ui_menu_add_multiple_choice(MENU_AMIGA_SLOW, parent, (char *)"Slow RAM");
  child->num_choices = 7;
  strcpy(child->choices[0], "None");
  child->choice_ints[0] = 0;
  for (int i = 1; i < 7; i++) {
    snprintf(child->choices[i], MAX_MENU_STR, "%d KB", i * 256);
    child->choice_ints[i] = i * 256;
  }
  child = bmx::amiga::s_fast =
      ui_menu_add_multiple_choice(MENU_AMIGA_FAST, parent, (char *)"Fast RAM");
  child->num_choices = 6;
  static const int fast_kb[6] = {0, 512, 1024, 2048, 4096, 8192};
  static const char *fast_nm[6] = {"None", "512 KB", "1 MB",
                                   "2 MB", "4 MB",   "8 MB"};
  for (int i = 0; i < 6; i++) {
    snprintf(child->choices[i], sizeof child->choices[i], "%s", fast_nm[i]);
    child->choice_ints[i] = fast_kb[i];
  }
  bmx::amiga::s_slow_delay = ui_menu_add_toggle(
      MENU_AMIGA_SLOW_DELAY, parent, (char *)"Slow RAM delay",
      h.slow_delay ? 1 : 0);
  bmx::amiga::s_slow_mirror = ui_menu_add_toggle(
      MENU_AMIGA_SLOW_MIRROR, parent, (char *)"Slow RAM mirror",
      h.slow_mirror ? 1 : 0);
  // Kick path is tracked in the config module log.
  ui_menu_add_button(MENU_TEXT, parent, (char *)"Kickstart: see bmretrosys-amiga.log");
  ui_menu_add_button(MENU_ATARI_SCREENSHOT, parent, (char *)"Screenshot");
  child = bmx::amiga::s_monitor_item = ui_menu_add_multiple_choice(
      MENU_AMIGA_MONITOR, parent, (char *)"Monitor");
  child->num_choices = 4;
  strcpy(child->choices[0], "Color");
  child->choice_ints[0] = BMX_MONITOR_COLOR;
  strcpy(child->choices[1], "B&W");
  child->choice_ints[1] = BMX_MONITOR_BW;
  strcpy(child->choices[2], "Green");
  child->choice_ints[2] = BMX_MONITOR_GREEN;
  strcpy(child->choices[3], "Amber");
  child->choice_ints[3] = BMX_MONITOR_AMBER;
  bmx::amiga::set_choice(child, bmx::amiga::video_get_monitor());
  bmx::amiga::sync_machine_items();
}

struct menu_item *emux_add_cartridge_options(struct menu_item *parent) {
  (void)parent;
  return NULL;
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
  return -1;
}

int emux_apply_palette_setting(int display_num) {
  (void)display_num;
  return -1;
}

int emux_set_palette_setting(int display_num, const char *setting) {
  (void)display_num;
  (void)setting;
  return -1;
}

const char *emux_get_palette_setting(int display_num) {
  (void)display_num;
  return "";
}

void emux_video_color_setting_changed(int display_num) {
  if (display_num != 0) {
    return;
  }
  bmx::amiga::video_refresh_preview();
}

static int clamp_color(int value, int max) {
  if (value < 0) {
    return 0;
  }
  if (value > max) {
    return max;
  }
  return value;
}

static void set_one_color(int brightness, int contrast, int gamma, int tint,
                          int saturation) {
  bmx::amiga::video_set_color_adjustments(brightness, contrast, gamma, tint,
                                          saturation);
}

void emux_set_color_brightness(int display_num, int value) {
  if (display_num != 0) {
    return;
  }
  int b = 0, c = 0, g = 0, t = 0, s = 0;
  bmx::amiga::video_get_color_adjustments(&b, &c, &g, &t, &s);
  set_one_color(clamp_color(value, 2000), c, g, t, s);
}

void emux_set_color_contrast(int display_num, int value) {
  if (display_num != 0) {
    return;
  }
  int b = 0, c = 0, g = 0, t = 0, s = 0;
  bmx::amiga::video_get_color_adjustments(&b, &c, &g, &t, &s);
  set_one_color(b, clamp_color(value, 2000), g, t, s);
}

void emux_set_color_gamma(int display_num, int value) {
  if (display_num != 0) {
    return;
  }
  int b = 0, c = 0, g = 0, t = 0, s = 0;
  bmx::amiga::video_get_color_adjustments(&b, &c, &g, &t, &s);
  set_one_color(b, c, clamp_color(value, 4000), t, s);
}

void emux_set_color_tint(int display_num, int value) {
  if (display_num != 0) {
    return;
  }
  int b = 0, c = 0, g = 0, t = 0, s = 0;
  bmx::amiga::video_get_color_adjustments(&b, &c, &g, &t, &s);
  set_one_color(b, c, g, clamp_color(value, 2000), s);
}

void emux_set_color_saturation(int display_num, int value) {
  if (display_num != 0) {
    return;
  }
  int b = 0, c = 0, g = 0, t = 0, s = 0;
  bmx::amiga::video_get_color_adjustments(&b, &c, &g, &t, &s);
  set_one_color(b, c, g, t, clamp_color(value, 2000));
}

int emux_get_color_brightness(int display_num) {
  int b = 1000, c = 0, g = 0, t = 0, s = 0;
  (void)display_num;
  bmx::amiga::video_get_color_adjustments(&b, &c, &g, &t, &s);
  return b;
}

int emux_get_color_contrast(int display_num) {
  int b = 0, c = 1000, g = 0, t = 0, s = 0;
  (void)display_num;
  bmx::amiga::video_get_color_adjustments(&b, &c, &g, &t, &s);
  return c;
}

int emux_get_color_gamma(int display_num) {
  int b = 0, c = 0, g = 1000, t = 0, s = 0;
  (void)display_num;
  bmx::amiga::video_get_color_adjustments(&b, &c, &g, &t, &s);
  return g;
}

int emux_get_color_tint(int display_num) {
  int b = 0, c = 0, g = 0, t = 1000, s = 0;
  (void)display_num;
  bmx::amiga::video_get_color_adjustments(&b, &c, &g, &t, &s);
  return t;
}

int emux_get_color_saturation(int display_num) {
  int b = 0, c = 0, g = 0, t = 0, s = 1000;
  (void)display_num;
  bmx::amiga::video_get_color_adjustments(&b, &c, &g, &t, &s);
  return s;
}

void emux_get_default_color_setting(int *brightness, int *contrast,
                                    int *gamma, int *tint,
                                    int *saturation) {
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
  // WarpMode mirrors the live flag (same as the Atari bridge): the menu
  // rewrites the Warp toggle display from emux_get_int on every open, so
  // the stub returning 0 made warp look off while it was still running.
  if (setting == Setting_WarpMode) {
    bmx::amiga::config_set_warp(value);
    return;
  }
  (void)value;
}

void emux_set_int_1(IntSetting setting, int value, int param) {
  (void)setting;
  (void)value;
  (void)param;
}

void emux_get_int(IntSetting setting, int *dest) {
  if (dest == NULL) {
    return;
  }
  if (setting == Setting_WarpMode) {
    *dest = bmx::amiga::config_warp();
    return;
  }
  *dest = 0;
}

void emux_get_int_1(IntSetting setting, int *dest, int param) {
  (void)setting;
  (void)param;
  if (dest != NULL) {
    *dest = 0;
  }
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
}

void emux_load_additional_settings(void) {}

void emux_save_additional_settings(FILE *fp) {
  if (fp == NULL) {
    return;
  }
  const bmx::amiga::HwSettings &h = bmx::amiga::config_hw();
  fprintf(fp, "amiga_model=%d\n", h.model);
  fprintf(fp, "amiga_chip=%d\n", h.chip_kb);
  fprintf(fp, "amiga_slow=%d\n", h.slow_kb);
  fprintf(fp, "amiga_fast=%d\n", h.fast_kb);
  fprintf(fp, "amiga_slow_delay=%d\n", h.slow_delay);
  fprintf(fp, "amiga_slow_mirror=%d\n", h.slow_mirror);
  fprintf(fp, "amiga_agnus=%d\n", h.agnus);
  fprintf(fp, "amiga_denise=%d\n", h.denise);
  fprintf(fp, "amiga_cpu=%d\n", h.cpu);
  fprintf(fp, "amiga_oc=%d\n", h.overclock);
  fprintf(fp, "amiga_cia=%d\n", h.cia);
  fprintf(fp, "amiga_rtc=%d\n", h.rtc);
  fprintf(fp, "amiga_blitter=%d\n", h.blitter);
  fprintf(fp, "amiga_df1_on=%d\n", h.drv_on[1]);
  fprintf(fp, "amiga_df2_on=%d\n", h.drv_on[2]);
  fprintf(fp, "amiga_df3_on=%d\n", h.drv_on[3]);
  for (int d = 0; d < 4; d++) {
    fprintf(fp, "amiga_df%d_type=%d\n", d, h.drv_type[d]);
    fprintf(fp, "amiga_df%d_mech=%d\n", d, h.drv_mech[d]);
  }
  fprintf(fp, "amiga_drive_sound=%d\n", h.drv_sound);
  fprintf(fp, "amiga_step_vol=%d\n", h.drv_step_vol);
  fprintf(fp, "amiga_aud_filter=%d\n", h.aud_filter);
  fprintf(fp, "amiga_aud_sampling=%d\n", h.aud_sampling);
  fprintf(fp, "amiga_aud_voll=%d\n", h.aud_vol_l);
  fprintf(fp, "amiga_aud_volr=%d\n", h.aud_vol_r);
  for (int c = 0; c < 4; c++) {
    fprintf(fp, "amiga_aud_vol%d=%d\n", c, h.aud_vol[c]);
    fprintf(fp, "amiga_aud_pan%d=%d\n", c, h.aud_pan[c]);
  }
  fprintf(fp, "amiga_monitor=%d\n", bmx::amiga::video_get_monitor());
  for (int d = 0; d < 2; d++) {
    const char *hd = bmx::amiga::config_hd_path(d);
    if (hd != NULL && hd[0] != '\0') {
      fprintf(fp, "amiga_hd%d=%s\n", d, hd);
    }
  }
}

int emux_handle_loaded_setting(char *name, char *value_str, int value) {
  if (name == NULL) {
    return 0;
  }
  // Hardfile paths are strings, not ints.
  if (strcmp(name, "amiga_hd0") == 0 || strcmp(name, "amiga_hd1") == 0) {
    int d = (strcmp(name, "amiga_hd0") == 0) ? 0 : 1;
    if (value_str != NULL && value_str[0] != '\0') {
      // Basic sanity: must look like an HDF path (length + suffix).
      size_t len = strlen(value_str);
      if (len < 5 || len > 200) {
        return 1;
      }
      bmx::amiga::config_set_hd_path(d, value_str);
    }
    return 1;
  }
  bmx::amiga::HwSettings &h = bmx::amiga::config_hw();
// Table-driven ints with ranges; clamps, never trusts the file.
#define AMIGA_LOAD_INT(key, field, lo, hi)                 \
  if (strcmp(name, key) == 0) {                            \
    h.field = value < lo ? lo : (value > hi ? hi : value); \
    return 1;                                              \
  }
  AMIGA_LOAD_INT("amiga_model", model, 0, 4)
  AMIGA_LOAD_INT("amiga_agnus", agnus, 0, 4)
  AMIGA_LOAD_INT("amiga_denise", denise, 0, 2)
  AMIGA_LOAD_INT("amiga_cpu", cpu, 0, 2)
  AMIGA_LOAD_INT("amiga_oc", overclock, 0, 8)
  AMIGA_LOAD_INT("amiga_cia", cia, 0, 1)
  AMIGA_LOAD_INT("amiga_rtc", rtc, 0, 2)
  AMIGA_LOAD_INT("amiga_blitter", blitter, 0, 2)
  AMIGA_LOAD_INT("amiga_slow_delay", slow_delay, 0, 1)
  AMIGA_LOAD_INT("amiga_slow_mirror", slow_mirror, 0, 1)
  AMIGA_LOAD_INT("amiga_df1_on", drv_on[1], 0, 1)
  AMIGA_LOAD_INT("amiga_df2_on", drv_on[2], 0, 1)
  AMIGA_LOAD_INT("amiga_df3_on", drv_on[3], 0, 1)
  AMIGA_LOAD_INT("amiga_drive_sound", drv_sound, 0, 1)
  AMIGA_LOAD_INT("amiga_step_vol", drv_step_vol, 0, 100)
  AMIGA_LOAD_INT("amiga_aud_filter", aud_filter, 0, 6)
  AMIGA_LOAD_INT("amiga_aud_sampling", aud_sampling, 0, 2)
  AMIGA_LOAD_INT("amiga_aud_voll", aud_vol_l, 0, 100)
  AMIGA_LOAD_INT("amiga_aud_volr", aud_vol_r, 0, 100)
#undef AMIGA_LOAD_INT
  // Fixed-set values (validated, fall back to current on garbage).
  if (strcmp(name, "amiga_chip") == 0) {
    if (value == 256 || value == 512 || value == 1024 || value == 2048) {
      h.chip_kb = value;
    }
    return 1;
  }
  if (strcmp(name, "amiga_slow") == 0) {
    if (value >= 0 && value <= 1536 && (value % 256) == 0) {
      h.slow_kb = value;
    }
    return 1;
  }
  if (strcmp(name, "amiga_fast") == 0) {
    if (value >= 0 && value <= 8192 && (value % 64) == 0) {
      h.fast_kb = value;
    }
    return 1;
  }
  for (int d = 0; d < 4; d++) {
    char key[24];
    snprintf(key, sizeof(key), "amiga_df%d_type", d);
    if (strcmp(name, key) == 0) {
      if (value >= 0 && value <= 2) {
        h.drv_type[d] = value;
      }
      return 1;
    }
    snprintf(key, sizeof(key), "amiga_df%d_mech", d);
    if (strcmp(name, key) == 0) {
      if (value >= 0 && value <= 1) {
        h.drv_mech[d] = value;
      }
      return 1;
    }
  }
  for (int c = 0; c < 4; c++) {
    char key[24];
    snprintf(key, sizeof(key), "amiga_aud_vol%d", c);
    if (strcmp(name, key) == 0) {
      if (value >= 0 && value <= 100) {
        h.aud_vol[c] = value;
      }
      return 1;
    }
    snprintf(key, sizeof(key), "amiga_aud_pan%d", c);
    if (strcmp(name, key) == 0) {
      if (value >= 0 && value <= 400) {
        h.aud_pan[c] = value;
      }
      return 1;
    }
  }
  if (strcmp(name, "amiga_monitor") == 0) {
    // Setter clamps garbage; refresh the row so the menu shows it.
    bmx::amiga::video_set_monitor(value);
    bmx::amiga::sync_machine_items();
    return 1;
  }
  return 0;
}

int emux_handle_menu_change(struct menu_item *item) {
  if (item == NULL) {
    return 0;
  }
  bmx::amiga::HwSettings &h = bmx::amiga::config_hw();
  const int v = item->value;
  const int cv = (v >= 0 && v < item->num_choices) ? item->choice_ints[v]
                                                  : -9999;
  // Model: preset chipset/memory, full apply + reboot.
  if (item->id == MENU_AMIGA_MODEL && cv >= 0) {
    h.model = cv;
    bmx::amiga::config_model_preset(cv, &h);
    bmx::amiga::sync_machine_items();
    if (bmx::amiga::config_apply_hw_reset()) {
      ui_info("Model set, machine rebooted");
    } else {
      ui_info("Model set for next boot");
    }
    return 1;
  }
  // CRT monitor: per-pixel mapping at blit time, applies instantly
  // (no reboot, no preview rebuild).
  if (item->id == MENU_AMIGA_MONITOR && cv >= 0) {
    bmx::amiga::video_set_monitor(cv);
    return 1;
  }
  // Chipset / CPU / memory: full apply + reboot.
  bool hw_changed = true;
  switch (item->id) {
  case MENU_AMIGA_AGNUS:
    if (cv >= 0) {
      h.agnus = cv;
    }
    break;
  case MENU_AMIGA_DENISE:
    if (cv >= 0) {
      h.denise = cv;
    }
    break;
  case MENU_AMIGA_CPU:
    if (cv >= 0) {
      h.cpu = cv;
    }
    break;
  case MENU_AMIGA_OVERCLOCK:
    h.overclock = v;
    break;
  case MENU_AMIGA_BLITTER:
    if (cv >= 0) {
      h.blitter = cv;
    }
    break;
  case MENU_AMIGA_CIA:
    if (cv >= 0) {
      h.cia = cv;
    }
    break;
  case MENU_AMIGA_RTC:
    if (cv >= 0) {
      h.rtc = cv;
    }
    break;
  case MENU_AMIGA_CHIP:
    if (cv > 0) {
      h.chip_kb = cv;
    }
    break;
  case MENU_AMIGA_SLOW:
    if (cv >= 0) {
      h.slow_kb = cv;
    }
    break;
  case MENU_AMIGA_FAST:
    if (cv >= 0) {
      h.fast_kb = cv;
    }
    break;
  case MENU_AMIGA_SLOW_DELAY:
    h.slow_delay = v ? 1 : 0;
    break;
  case MENU_AMIGA_SLOW_MIRROR:
    h.slow_mirror = v ? 1 : 0;
    break;
  case MENU_AMIGA_DF1_ON:
    h.drv_on[1] = v ? 1 : 0;
    break;
  case MENU_AMIGA_DF2_ON:
    h.drv_on[2] = v ? 1 : 0;
    break;
  case MENU_AMIGA_DF3_ON:
    h.drv_on[3] = v ? 1 : 0;
    break;
  case MENU_AMIGA_DF0_TYPE:
    if (cv >= 0) {
      h.drv_type[0] = cv;
    }
    break;
  case MENU_AMIGA_DF1_TYPE:
    if (cv >= 0) {
      h.drv_type[1] = cv;
    }
    break;
  case MENU_AMIGA_DF2_TYPE:
    if (cv >= 0) {
      h.drv_type[2] = cv;
    }
    break;
  case MENU_AMIGA_DF3_TYPE:
    if (cv >= 0) {
      h.drv_type[3] = cv;
    }
    break;
  case MENU_AMIGA_DF0_MECH:
    if (cv >= 0) {
      h.drv_mech[0] = cv;
    }
    break;
  case MENU_AMIGA_DF1_MECH:
    if (cv >= 0) {
      h.drv_mech[1] = cv;
    }
    break;
  case MENU_AMIGA_DF2_MECH:
    if (cv >= 0) {
      h.drv_mech[2] = cv;
    }
    break;
  case MENU_AMIGA_DF3_MECH:
    if (cv >= 0) {
      h.drv_mech[3] = cv;
    }
    break;
  default:
    hw_changed = false;
    break;
  }
  if (hw_changed) {
    bmx::amiga::config_apply_hw_reset();
    bmx::amiga::sync_machine_items();
    bmx::amiga::sync_drive_setup_items();
    return 1;
  }
  // Paula audio + drive clicks: live, no reboot (safe per-tick).
  bool live_changed = true;
  switch (item->id) {
  case MENU_AMIGA_AUD_FILTER:
    if (cv >= 0) {
      h.aud_filter = cv;
    }
    break;
  case MENU_AMIGA_AUD_SAMPLING:
    if (cv >= 0) {
      h.aud_sampling = cv;
    }
    break;
  case MENU_AMIGA_AUD_VOLL:
    h.aud_vol_l = v;
    break;
  case MENU_AMIGA_AUD_VOLR:
    h.aud_vol_r = v;
    break;
  case MENU_AMIGA_AUD_VOL0:
    h.aud_vol[0] = v;
    break;
  case MENU_AMIGA_AUD_VOL1:
    h.aud_vol[1] = v;
    break;
  case MENU_AMIGA_AUD_VOL2:
    h.aud_vol[2] = v;
    break;
  case MENU_AMIGA_AUD_VOL3:
    h.aud_vol[3] = v;
    break;
  case MENU_AMIGA_AUD_PAN0:
    h.aud_pan[0] = v;
    break;
  case MENU_AMIGA_AUD_PAN1:
    h.aud_pan[1] = v;
    break;
  case MENU_AMIGA_AUD_PAN2:
    h.aud_pan[2] = v;
    break;
  case MENU_AMIGA_AUD_PAN3:
    h.aud_pan[3] = v;
    break;
  case MENU_AMIGA_DRIVE_SOUND:
    h.drv_sound = v ? 1 : 0;
    break;
  case MENU_AMIGA_STEP_VOL:
    h.drv_step_vol = v;
    break;
  default:
    live_changed = false;
    break;
  }
  if (live_changed) {
    bmx::amiga::config_apply_live_volumes();
    return 1;
  }
  if (item->id == MENU_AMIGA_HDF0_DETACH || item->id == MENU_AMIGA_HDF1_DETACH) {
    emux_detach_hdf_image(item->id == MENU_AMIGA_HDF0_DETACH ? 0 : 1);
    return 1;
  }
  if (item->id == MENU_ATARI_SCREENSHOT) {
    // Auto-named BMP dump on SD:/screenshots (never beside snapshots);
    // the folder is created on demand.
    bmx_screenshot_mkdir("amiga");
    char path[64];
    bool saved = false;
    for (int n = 0; n < 100 && !saved; n++) {
      bmx_screenshot_path(path, sizeof(path), "amiga", n);
      FILE *probe = fopen(path, "rb");
      if (probe != NULL) {
        fclose(probe);
        continue;
      }
      if (::bmx::amiga::video_save_screenshot(path)) {
        saved = true;
        char msg[64];
        snprintf(msg, sizeof(msg), "Saved shot%02d.bmp", n);
        ui_info(msg);
      } else if (n == 0) {
        break;
      }
    }
    if (!saved) {
      ui_info("Screenshot failed");
    }
    return 1;
  }
  return 0;
}

int emux_handle_quick_func(int button_func, fullpath_func f_fullpath) {
  (void)button_func;
  (void)f_fullpath;
  return 0;
}

int emux_handle_rom_change(struct menu_item *item, fullpath_func f_fullpath) {
  // Kickstart change from the ROM menu: reload + cold reset. V1 accepts
  // any *.rom resolved by the common browser.
  if (item == NULL || f_fullpath == NULL) {
    return 0;
  }
  char *path = f_fullpath(DIR_ROMS, item->name);
  if (path == NULL || *path == '\0') {
    return 0;
  }
  if (bmx::amiga::config_load_kick(path)) {
    ui_info("Kickstart loaded, rebooted");
    return 1;
  }
  ui_info("Kickstart load failed");
  return 1;
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

}  // extern "C"
