//
// atarist_bridge.cpp
//
// Emulator-specific API bridge between BMX common code (menu/UI/kernel) and
// the Hatari (Atari ST/STE/MegaSTE) emulator core. This fulfils the same
// role that third_party/vice-3.10/src/arch/raspi/vice_api.c plays for the
// VICE machines, but mapped onto Hatari.
//
// Phase 0: every emux_* entry is a safe stub (returns 0/NULL, no-ops) so the
// ATARIST kernel links and boots to the placeholder frame loop. Real
// semantics (TOS, floppy, state, settings, palette, input) land in
// Phases 1-4.
//
// The contract is the shared emux_api.h. Functions that are meaningful for
// Hatari are implemented against the core; the remaining VICE-specific
// facilities (REU, userport RS232, networking, wifi, updates, easyflash
// etc.) are given safe defaults so the kernel links and the common menu
// does not misbehave.
//

#include "atarist_bridge.h"
#include "atarist_config.h"
#include "atarist_debug.h"
#include "atarist_input.h"
#include "atarist_sound.h"
#include "atarist_video.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
// NOTE: <string> first -- Hatari cpu/sysdeps.h includes <string> under
// __cplusplus, which is only legal outside an extern "C" region.
#include <string>

extern "C" {
#include "floppy.h"
#include "reset.h"
#include "tos.h"
#include "configuration.h"
#include "m68000.h"
#include "newcpu.h"
}

namespace bmx {
namespace atarist {

// Placeholder anchor for Hatari-specific bridge state (floppy names, staged
// settings, TOS selection). Drive state lives further below (needs menu.h).

}  // namespace atarist
}  // namespace bmx

// ===========================================================================
// C EMUX API (as declared in emux_api.h)
//
// emux_api.h is included inside the extern "C" block so its function and
// variable declarations share C linkage with the common library (emux_api.c)
// and the definitions below.
// ===========================================================================

extern "C" {

#include "emux_api.h"
#include "menu.h"
#include "menu_timing.h"

// Forward: drive status sync + bootsector check (defined below with C
// linkage, like the rest of this region).
namespace bmx {
namespace atarist {
void sync_drive_items(void);
void sync_machine_items(void);
void log_bootsector(int drive);
}  // namespace atarist
}  // namespace bmx

// -- machine ---------------------------------------------------------------

void emu_machine_init(int raster_skip_enabled, int raster_skip2_enabled) {
  (void)raster_skip_enabled;
  (void)raster_skip2_enabled;
  bmx::atarist::debug_log("emu_machine_init: enter");
  emux_machine_class = BMC64_MACHINE_CLASS_ATARIST;
  emux_c64_core = BMC64_C64_CORE_UNKNOWN;
  bmx::atarist::debug_log("emu_machine_init: class=ATARIST published");
}

int emu_ui_uses_german_keyboard_layout(void) {
  return 0;
}

unsigned long emux_calculate_timing(double fps) {
  // No vsync-based timing adjustment for atarist; the frame loop self-paces.
  (void)fps;
  return 0;
}

double emux_calculate_fps(void) {
  // ST placeholder refresh until the real core reports timing.
  return 50.0;
}

// -- sound -----------------------------------------------------------------

int emu_set_sound_sample_rate(int sample_rate) {
  // Fixed 44.1 kHz mixer (matches the Circle sink, no resampling).
  (void)sample_rate;
  return 44100;
}

int request_sound_sample_rate(int sample_rate) {
  (void)sample_rate;
  // Fixed 44.1 kHz mixer (matches the Circle sink, no resampling).
  return 44100;
}

// -- reset / disks / state -------------------------------------------------

void emux_reset(int isSoft) {
  if (isSoft) {
    bmx::atarist::debug_log("emux_reset: warm reset");
    Reset_Warm();
  } else {
    bmx::atarist::debug_log("emux_reset: cold reset");
    Reset_Cold();
  }
  // The CPU must come out of reset at the TOS entry (PC=[4], supervisor).
  // Anything else (PC=0, user mode) means the reset did not take and the
  // frame loop will execute garbage.
  bmx::atarist::debug_log("reset: pc=0x%06x sr=0x%04x", (unsigned)regs.pc,
                         (unsigned)regs.sr);
}

// VICE drive units 8/9 map onto ST floppy drives A:/B:.
int atarist_drive_from_unit(int unit) {
  if (unit == 8) {
    return 0;
  }
  if (unit == 9) {
    return 1;
  }
  return -1;
}

int emux_attach_disk_image(int unit, char *filename) {
  int drive = atarist_drive_from_unit(unit);
  if (drive < 0 || filename == NULL || *filename == '\0') {
    return -1;
  }
  Floppy_SetDiskFileName(drive, filename, NULL);
  if (!Floppy_InsertDiskIntoDrive(drive)) {
    bmx::atarist::debug_log("attach: Floppy_Insert failed for '%s'", filename);
    return -1;
  }
  bmx::atarist::debug_log("attach: drive %d <- '%s'", drive, filename);
  bmx::atarist::log_bootsector(drive);
  bmx::atarist::sync_drive_items();
  return 0;
}

void emux_detach_disk(int unit) {
  int drive = atarist_drive_from_unit(unit);
  if (drive >= 0) {
    Floppy_EjectDiskFromDrive(drive);
    bmx::atarist::debug_log("detach: drive %d ejected", drive);
    bmx::atarist::sync_drive_items();
  }
}

int emux_attach_tape_image(char *filename) {
  (void)filename;
  return 0;
}

void emux_detach_tape(void) {
}

int emux_attach_cart(int bank, char *filename) {
  // The ST has no cartridges in the BMX sense; accept and ignore.
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
  (void)filename;
  return 0;
}

int emux_save_state(char *filename) {
  (void)filename;
  return 0;
}

int emux_load_reu_image(char *filename) {
  // No REU on an ST.
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
  // Boot-a-floppy from the BMX file browser: insert into drive A: and
  // cold-reset so TOS boots it. Only .ST/.MSA/.STX images are meaningful.
  (void)program_number;
  if (filename == NULL || *filename == '\0') {
    return -1;
  }
  Floppy_SetDiskFileName(0, filename, NULL);
  if (!Floppy_InsertDiskIntoDrive(0)) {
    bmx::atarist::debug_log("autostart: insert failed for '%s'", filename);
    return -1;
  }
  bmx::atarist::debug_log("autostart: booting '%s'", filename);
  bmx::atarist::log_bootsector(0);
  Reset_Cold();
  return 0;
}

void emux_drive_change_model(int unit) {
  (void)unit;
}

void emux_set_warp(int warp) {
  (void)warp;
}

int emux_prepare_shutdown(void) {
  return 0;
}

// -- Atari 8-bit SIO/cart stubs (link-only) ---------------------------------
// The MENU_ATARI_* cases in third_party/common/menu.c are unconditionally
// compiled, so the linker needs these symbols in every non-Atari kernel
// too. An ST has no SIO drives; report failure (-1) / no-op so the menu
// shows an error instead of crashing.

int emux_atari_mount_disk(int drive, char *path) {
  (void)drive;
  (void)path;
  return -1;
}

void emux_atari_sync_drives(void) {
}

extern "C" int emux_atari_cart_types_for_kb(int kb, int *ids,
                                            const char **descs, int max) {
  (void)kb;
  (void)ids;
  (void)descs;
  (void)max;
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
  // No custom keymap in Phase 0 (descriptor default_keymap is nullptr).
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

// Runs the BMX menu on the emulator thread while it is open, mirroring
// emu_pause_trap() (the VICE path): emulation pauses, the UI layer shows,
// menu keys render. Returns when the menu closes (F12/ESC) and emulation
// resumes. NOTE: no emux_ensure_video() before the loop -- it programs VICE
// canvas state (unlinked in ATARIST builds); emux_api.c's null-safe version
// is used inside the loop only.
void emux_trap_main_loop_ui(void) {
  bmx::atarist::debug_log("AtariST: menu opened, entering pump");
  // Refresh drive status rows on every open (attach/eject happened while
  // the menu was closed).
  bmx::atarist::sync_drive_items();
  bmx::atarist::sync_machine_items();
  {
    int dpx = 0, dpy = 0, fbw = 0, fbh = 0, sw = 0, sh = 0, dw = 0, dh = 0;
    circle_get_fbl_dimensions(FB_LAYER_VIC, &dpx, &dpy, &fbw, &fbh, &sw, &sh,
                              &dw, &dh);
    bmx::atarist::debug_log("AtariST: menu geometry dpx=%d dpy=%d", dpx, dpy);
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
  bmx::atarist::debug_log("AtariST: menu pump exit, resuming emulation");
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

// No PETSCII character set on the ST; return the buffer unchanged.
uint8_t *charset_petconvstring(uint8_t *text, int mode) {
  (void)mode;
  return text;
}

int kbdbuf_feed(const char *text) {
  (void)text;
  return 0;
}

void lib_free(void *ptr) {
  free(ptr);
}

// -- settings / video / palette --------------------------------------------

namespace bmx {
namespace atarist {

// Drive status rows (built by emux_add_drive_option, refreshed by
// sync_drive_items on every menu open).
struct menu_item *s_drive_status[2] = {NULL, NULL};

// Machine option rows (built by emux_add_machine_options).
struct menu_item *s_model_status = NULL;
struct menu_item *s_tos_item = NULL;

const char *drive_basename(const char *path) {
  const char *slash = strrchr(path, '/');
  return (slash != NULL && slash[1] != '\0') ? slash + 1 : path;
}

const char *machine_model_name(void) {
  switch (ConfigureParams.System.nMachineType) {
  case MACHINE_ST:
    return "Atari ST";
  case MACHINE_STE:
    return "Atari STE";
  case MACHINE_MEGA_ST:
    return "Mega ST";
  case MACHINE_MEGA_STE:
    return "Mega STE";
  default:
    break;
  }
  return "?";
}

void sync_machine_items(void) {
  if (s_model_status != NULL) {
    if (config_is_ready()) {
      snprintf(s_model_status->name, MAX_MENU_STR, "Model: %s, %s v%u.%02u",
               machine_model_name(), bIsEmuTOS ? "EmuTOS" : "TOS",
               (unsigned)((TosVersion >> 8) & 0xff),
               (unsigned)(TosVersion & 0xff));
    } else {
      snprintf(s_model_status->name, MAX_MENU_STR, "Model: ...");
    }
  }
  if (s_tos_item != NULL) {
    int want = config_tos_index();
    for (int i = 0; i < s_tos_item->num_choices; i++) {
      if (s_tos_item->choice_ints[i] == want) {
        s_tos_item->value = i;
        return;
      }
    }
    s_tos_item->value = 0;
  }
}

void sync_drive_items(void) {
  for (int d = 0; d < 2; d++) {
    if (s_drive_status[d] == NULL) {
      continue;
    }
    if (EmulationDrives[d].bDiskInserted) {
      snprintf(s_drive_status[d]->name, MAX_MENU_STR, "State: %.40s",
               drive_basename(EmulationDrives[d].sFileName));
    } else {
      snprintf(s_drive_status[d]->name, sizeof(s_drive_status[d]->name),
               "State: Empty");
    }
  }
}

// Verifies the boot sector the FDC layer will serve TOS: for raw
// sector-linear images (.ST decoded .MSA) sector 0 sits at buffer offset 0
// and is bootable iff its 256 big-endian words sum to 0x1234. (.STX is
// track-based, unchecked here.) A mismatch here means the file/driver
// layer, not TOS, is at fault.
void log_bootsector(int drive) {
  if (drive < 0 || drive > 1) {
    return;
  }
  int type = EmulationDrives[drive].ImageType;
  if (type != FLOPPY_IMAGE_TYPE_ST && type != FLOPPY_IMAGE_TYPE_MSA) {
    bmx::atarist::debug_log("bootsec: drive %d type %d (unchecked)", drive,
                           type);
    return;
  }
  if (!EmulationDrives[drive].bDiskInserted ||
      EmulationDrives[drive].pBuffer == NULL ||
      EmulationDrives[drive].nImageBytes < 512) {
    bmx::atarist::debug_log("bootsec: drive %d has no data", drive);
    return;
  }
  const uint8_t *sec = EmulationDrives[drive].pBuffer;
  unsigned sum = 0;
  for (int i = 0; i < 512; i += 2) {
    sum += (unsigned)((sec[i] << 8) | sec[i + 1]);
  }
  sum &= 0xffff;
  bmx::atarist::debug_log("bootsec: drive %d sum=0x%04x %s", drive, sum,
                         sum == 0x1234 ? "EXECUTABLE" : "not bootable");
}

}  // namespace atarist
}  // namespace bmx

void emux_add_drive_option(struct menu_item *parent, int drive) {
  // ST floppy drives A:/B: (units 8/9). Insert reuses menu.c's generic
  // attach flow (MENU_ATTACH_DISK_x sets the unit, MENU_DISK_FILE calls
  // emux_attach_disk_image); eject maps to emux_detach_disk. Status rows
  // sync from EmulationDrives on every menu open (see trap pump below).
  // Per-unit calls are unused, like the Atari800 bridge.
  if (drive != -1) {
    return;
  }
  static const char *names[2] = {"Drive A", "Drive B"};
  static const int attach_ids[2] = {MENU_ATTACH_DISK_8, MENU_ATTACH_DISK_9};
  static const int eject_ids[2] = {MENU_DETACH_DISK_8, MENU_DETACH_DISK_9};
  for (int d = 0; d < 2; d++) {
    struct menu_item *folder = ui_menu_add_folder(parent, (char *)names[d]);
    ::bmx::atarist::s_drive_status[d] =
        ui_menu_add_button(MENU_TEXT, folder, (char *)"State");
    ui_menu_add_button(attach_ids[d], folder, (char *)"Insert disk...");
    ui_menu_add_button(eject_ids[d], folder, (char *)"Eject");
  }
  ::bmx::atarist::sync_drive_items();
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
  // Phase 0: YM2149 + STE DMA rows land in Phase 3.
  (void)emulation_parent;
  (void)sid_parent;
  (void)sound_parent;
}

void emux_add_reu_options(struct menu_item *parent) {
  (void)parent;
}

void emux_add_machine_options(struct menu_item *parent) {
  // Model readout (follows the TOS: 2.06 images boot a MegaSTE) plus the
  // TOS selector over the /roms/tos pool (EmuTOS default). Switching TOS
  // cold-resets the machine; disks stay inserted.
  bmx::atarist::s_model_status =
      ui_menu_add_button(MENU_TEXT, parent, (char *)"Model: ...");
  struct menu_item *child = bmx::atarist::s_tos_item =
      ui_menu_add_multiple_choice(MENU_ATARIST_TOS, parent, (char *)"TOS");
  int count = bmx::atarist::config_tos_count();
  if (count < 1) {
    count = 1;
  }
  if (count > 16) {
    count = 16;
  }
  child->num_choices = count;
  for (int i = 0; i < count; i++) {
    snprintf(child->choices[i], MAX_MENU_STR, "%s",
             bmx::atarist::config_tos_name(i));
    child->choice_ints[i] = i;
  }
  bmx::atarist::sync_machine_items();
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
  return "";
}

void emux_video_color_setting_changed(int display_num) {
  (void)display_num;
  // Setters push into the video pipeline immediately (the emulator keeps
  // running behind the menu, so sliders track live). Re-publish current
  // values here for callers that batch edits then notify once.
  int bri, con, gam, tin, sat;
  ::bmx::atarist::video_get_color_adjustments(&bri, &con, &gam, &tin, &sat);
  ::bmx::atarist::video_set_color_adjustments(bri, con, gam, tin, sat);
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
  ::bmx::atarist::video_set_color_adjustments(brightness, contrast, gamma,
                                             tint, saturation);
}

void emux_set_color_brightness(int display_num, int value) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::atarist::video_get_color_adjustments(&bri, &con, &gam, &tin, &sat);
  bri = clamp_color(value, 2000);
  set_one_color(bri, con, gam, tin, sat);
}

void emux_set_color_contrast(int display_num, int value) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::atarist::video_get_color_adjustments(&bri, &con, &gam, &tin, &sat);
  con = clamp_color(value, 2000);
  set_one_color(bri, con, gam, tin, sat);
}

void emux_set_color_gamma(int display_num, int value) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::atarist::video_get_color_adjustments(&bri, &con, &gam, &tin, &sat);
  gam = clamp_color(value, 4000);
  set_one_color(bri, con, gam, tin, sat);
}

void emux_set_color_tint(int display_num, int value) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::atarist::video_get_color_adjustments(&bri, &con, &gam, &tin, &sat);
  tin = clamp_color(value, 2000);
  set_one_color(bri, con, gam, tin, sat);
}

void emux_set_color_saturation(int display_num, int value) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::atarist::video_get_color_adjustments(&bri, &con, &gam, &tin, &sat);
  sat = clamp_color(value, 2000);
  set_one_color(bri, con, gam, tin, sat);
}

int emux_get_color_brightness(int display_num) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::atarist::video_get_color_adjustments(&bri, &con, &gam, &tin, &sat);
  return bri;
}

int emux_get_color_contrast(int display_num) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::atarist::video_get_color_adjustments(&bri, &con, &gam, &tin, &sat);
  return con;
}

int emux_get_color_gamma(int display_num) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::atarist::video_get_color_adjustments(&bri, &con, &gam, &tin, &sat);
  return gam;
}

int emux_get_color_tint(int display_num) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::atarist::video_get_color_adjustments(&bri, &con, &gam, &tin, &sat);
  return tin;
}

int emux_get_color_saturation(int display_num) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::atarist::video_get_color_adjustments(&bri, &con, &gam, &tin, &sat);
  return sat;
}

void emux_get_default_color_setting(int *brightness, int *contrast,
                                    int *gamma, int *tint, int *saturation) {
  if (brightness != NULL) *brightness = 1000;
  if (contrast != NULL) *contrast = 1000;
  if (gamma != NULL) *gamma = 1000;
  if (tint != NULL) *tint = 1000;
  if (saturation != NULL) *saturation = 1000;
}

void emux_set_video_cache(int value) {
  (void)value;
}

void emux_set_hw_scale(int value) {
  (void)value;
}

void emux_set_int(IntSetting setting, int value) {
  (void)setting;
  (void)value;
}

void emux_set_int_1(IntSetting setting, int value, int param) {
  (void)setting;
  (void)value;
  (void)param;
}

void emux_get_int(IntSetting setting, int *dest) {
  (void)setting;
  if (dest != NULL) {
    *dest = 0;
  }
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
  fprintf(fp, "atarist_tos=%d\n", bmx::atarist::config_tos_index());
}

int emux_handle_loaded_setting(char *name, char *value_str, int value) {
  (void)value_str;
  if (name != NULL && strcmp(name, "atarist_tos") == 0) {
    bmx::atarist::config_set_tos_index(value);
    return 1;
  }
  return 0;
}

int emux_handle_menu_change(struct menu_item *item) {
  if (item == NULL) {
    return 0;
  }
  if (item->id == MENU_ATARIST_TOS) {
    int v = item->value;
    int index = (v >= 0 && v < item->num_choices) ? item->choice_ints[v] : 0;
    bmx::atarist::config_set_tos_index(index);
    // Silent on success (no blocking popup): the rebooted TOS is visible
    // immediately behind the menu. Only a real failure reports.
    if (!bmx::atarist::config_apply_tos_reset()) {
      ui_info("TOS change failed");
    }
    bmx::atarist::sync_machine_items();
    return 1;
  }
  if (item->id == MENU_ATARI_SCREENSHOT) {
    static const char *kShotDirs[] = {
        "USER:/systems/ATARIST/snapshots",
        "SYS:/systems/ATARIST/snapshots",
        "SD:/systems/ATARIST/snapshots",
    };
    char path[64];
    bool saved = false;
    for (size_t d = 0;
         d < sizeof(kShotDirs) / sizeof(kShotDirs[0]) && !saved; d++) {
      // Best-effort directory creation (fresh cards may lack it; keep
      // rules preserve user content, so this never clobbers anything).
      char dir[48];
      snprintf(dir, sizeof(dir), "%s", kShotDirs[d]);
      mkdir(dir, 0777);
      for (int n = 0; n < 100 && !saved; n++) {
        snprintf(path, sizeof(path), "%s/shot%02d.bmp", kShotDirs[d], n);
        FILE *probe = fopen(path, "rb");
        if (probe != NULL) {
          fclose(probe);
          continue;  // name taken
        }
        if (::bmx::atarist::video_save_screenshot(path)) {
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
  (void)item;
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
// NOTE: emux_network_is_ready, emux_get_network_addresses, emux_wifi_* are
// provided by src/network/network_service.cpp (included in ATARIST builds).
// emux_update_*_explicit are provided by src/update/{update_service,
// menu_update_progress_bridge}.cpp. Do not redefine them here.

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
// The ATARIST kernel links shared BMX objects (async_network, kernel GPIO /
// userport paths, common menu/ui listing helpers) that reference VICE
// symbols on VICE machines. The Hatari core has no equivalent, so provide
// safe no-op stubs here. These intentionally return failure/empty so callers
// degrade gracefully instead of crashing.

// vicesocket (async_network.cpp)
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

// userport (kernel.cpp GPIO paths; an ST has no userport CIA)
uint8_t circle_get_userport_ddr(void) {
  return 0;
}

uint8_t circle_get_userport(void) {
  return 0xff;
}

void circle_set_userport(uint8_t value) {
  (void)value;
}

// monitor peek (kernel debug path)
uint8_t mem_bank_peek(int bank, uint16_t addr, void *context) {
  (void)bank;
  (void)addr;
  (void)context;
  return 0;
}

// disk/tape image listing (common menu.c)
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

// PETSCII helpers (common ui.c; an ST uses ASCII, no conversion)
uint8_t charset_petscii_to_screencode(uint8_t code, unsigned int reverse_mode) {
  (void)reverse_mode;
  return code;
}

// NOTE: emu_pause_trap() is provided by the common library (emux_api.c) --
// see the VICE path. Do not redefine it here (duplicate-symbol link error).

void vsync_suspend_speed_eval(void) {
  bmx::atarist::debug_log("AtariST: menu closed");
}

}  // extern "C"

