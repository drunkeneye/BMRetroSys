//
// dos_bridge.cpp
//
// Emulator-specific API bridge between BMX common code (menu/UI/kernel) and
// the DOSBox-X emulator core.  This fulfils the same role that
// third_party/vice-3.10/src/arch/raspi/vice_api.c plays for the VICE
// machines, but mapped onto the cut-down DOSBox-X core.
//
// Phase 0: every emux_* entry is a safe stub (returns 0/NULL, no-ops) so the
// DOS kernel links and boots to the placeholder frame loop. Real
// semantics (disks, state, settings, palette, input) land in Phases 1-4.
//
// The contract is the shared emux_api.h.  Functions that are meaningful for
// DOSBox-X are implemented against the core; the remaining VICE-specific
// facilities (REU, userport RS232, networking, wifi, updates, easyflash etc.)
// are given safe defaults so the kernel links and the common menu does not
// misbehave.
//

#include "dos_bridge.h"
#include "dos_config.h"
#include "dos_debug.h"
#include "dos_input.h"
#include "dos_perf.h"
#include "dos_snapshot.h"
#include "dos_sound.h"
#include "dos_video.h"

#include "src/monitor_phosphor.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <string>

// Full struct menu_item for the anchors below (emux_api.h only forwards).
extern "C" {
#include "ui.h"
}

namespace bmx {
namespace dos {

// Placeholder anchor for DOSBox-X-specific bridge state (drive names, staged
// settings). Real backing store lands with Phases 1-4.
struct menu_item *s_monitor_item = nullptr;

// Drive C mapping, as last requested from the menu. The guest owns the
// truth (mounts die with a guest reboot); this is only for the status line,
// reset to empty here and refreshed on every mount/unmount action.
struct menu_item *s_dos_c_status = nullptr;
char s_dos_c_mount[256] = {0};

static void dos_sync_c_status(void) {
  if (s_dos_c_status == nullptr) {
    return;
  }
  if (s_dos_c_mount[0] != '\0') {
    snprintf(s_dos_c_status->name, MAX_MENU_STR, "State: %.39s",
             s_dos_c_mount);
  } else {
    snprintf(s_dos_c_status->name, MAX_MENU_STR, "State: Empty");
  }
}

// Builds the "mount <dir> as C: + switch to it" shell script shared by
// autostart (which appends the exe name) and the Drives menu (which stops
// at the C: prompt). Returns false when the dir does not fit the typer.
bool dos_mount_c_script(const char *dir, std::string *out) {
  if (dir == nullptr || *dir == '\0' || out == nullptr) {
    return false;
  }
  std::string d(dir);
  if (d.size() + 32 > 480) {
    return false;
  }
  std::string script("mount c ");
  if (d.find(' ') != std::string::npos) {
    script += '"';
    script += d;
    script += '"';
  } else {
    script += d;
  }
  script += "\nc:\n";
  *out = script;
  return true;
}

void sync_all_menu_items(void) {
  if (s_monitor_item != nullptr) {
    const int cur = video_get_monitor();
    for (int i = 0; i < s_monitor_item->num_choices; i++) {
      if (s_monitor_item->choice_ints[i] == cur) {
        s_monitor_item->value = i;
        break;
      }
    }
  }
}

}  // namespace dos
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

// -- machine ---------------------------------------------------------------

void emu_machine_init(int raster_skip_enabled, int raster_skip2_enabled) {
  (void)raster_skip_enabled;
  (void)raster_skip2_enabled;
  bmx::dos::debug_log("emu_machine_init: enter");
  emux_machine_class = BMC64_MACHINE_CLASS_DOS;
  emux_c64_core = BMC64_C64_CORE_UNKNOWN;
  bmx::dos::debug_log("emu_machine_init: class=DOS published");
}

int emu_ui_uses_german_keyboard_layout(void) {
  return 0;
}

unsigned long emux_calculate_timing(double fps) {
  // No vsync-based timing adjustment for dos; the frame loop self-paces.
  (void)fps;
  return 0;
}

double emux_calculate_fps(void) {
  // VGA text-mode placeholder refresh until the real core reports timing.
  return 70.0;
}

// -- sound -----------------------------------------------------------------

int emu_set_sound_sample_rate(int sample_rate) {
  // Phase 0: no audio sink yet; echo the request.
  return sample_rate;
}

// -- reset / disks / state -------------------------------------------------

void emux_reset(int isSoft) {
  // Hand the reboot to the guest thread: GFX_Events throws it so it unwinds
  // through nested SHELL_Run like a guest reboot, then BIOS POST boots DOS
  // fresh (see dos_request_reset in dos_input.cpp). Returns
  // immediately; the menu stays responsive while the guest reboots.
  ::bmx::dos::dos_request_reset(isSoft ? 6 : 3);
}

int emux_attach_disk_image(int unit, char *filename) {
  // Phase 0: no drives yet. Report success so the menu does not error.
  (void)unit;
  (void)filename;
  return 0;
}

void emux_detach_disk(int unit) {
  (void)unit;
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
  if (filename == nullptr || *filename == '\0') {
    return -1;
  }
  // Guest must be paused (menu trap guarantees it); components serialize
  // mid-instruction exactly like upstream slot saves.
  if (!::bmx::dos::snapshot_load_from_file(filename)) {
    ::bmx::dos::debug_log("snapshot: load failed '%s'", filename);
    return -1;
  }
  ::bmx::dos::debug_log("snapshot: loaded '%s'", filename);
  return 0;
}

int emux_save_state(char *filename) {
  if (filename == nullptr || *filename == '\0') {
    return -1;
  }
  if (!::bmx::dos::snapshot_save_to_file(filename)) {
    ::bmx::dos::debug_log("snapshot: save failed '%s'", filename);
    return -1;
  }
  ::bmx::dos::debug_log("snapshot: saved '%s'", filename);
  return 0;
}

int emux_load_reu_image(char *filename) {
  // No REU on a 286 DOS machine.
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
  // Boot-a-game from the BMX file browser: queue shell keystrokes that mount
  // the game's folder as C:, switch to it, and run the file. The typer
  // drips the keys in from GFX_Events, so use this from the DOS prompt
  // (keystrokes typed into a running game go to the game).
  // filename looks like "SD:/DOS/disks/M/MONKEY2/MONKEY2.EXE".
  if (filename == NULL || *filename == '\0') {
    return -1;
  }
  const char *slash = strrchr(filename, '/');
  if (slash == NULL || slash == filename) {
    return -1;
  }
  std::string dir(filename, (size_t)(slash - filename));
  std::string base(slash + 1);
  if (base.empty() || dir.size() + base.size() + 32 > 480) {
    return -1;
  }
  std::string script;
  if (!::bmx::dos::dos_mount_c_script(dir.c_str(), &script)) {
    return -1;
  }
  script += base;
  script += "\n";
  ::bmx::dos::dos_type_text(script.c_str());
  snprintf(::bmx::dos::s_dos_c_mount, sizeof(::bmx::dos::s_dos_c_mount),
           "%s", dir.c_str());
  ::bmx::dos::dos_sync_c_status();
  ::bmx::dos::debug_log("dos autostart: queued '%s' (mount '%s' as C:)",
                            base.c_str(), dir.c_str());
  return 0;
}

// Mount a folder as C: and switch to it, without running anything.
// Powers the Drives > Drive C menu; autostart above is mount + run.
int emux_dos_mount_folder(char *dir) {
  if (dir == NULL || *dir == '\0') {
    return -1;
  }
  std::string script;
  if (!::bmx::dos::dos_mount_c_script(dir, &script)) {
    return -1;
  }
  ::bmx::dos::dos_type_text(script.c_str());
  snprintf(::bmx::dos::s_dos_c_mount, sizeof(::bmx::dos::s_dos_c_mount),
           "%s", dir);
  ::bmx::dos::dos_sync_c_status();
  ::bmx::dos::debug_log("dos mount: queued mount '%s' as C:", dir);
  return 0;
}

void emux_dos_unmount_c(void) {
  // DOSBox-X unmount spelling: "mount -u <letter>".
  ::bmx::dos::dos_type_text("mount -u c\n");
  ::bmx::dos::s_dos_c_mount[0] = '\0';
  ::bmx::dos::dos_sync_c_status();
  ::bmx::dos::debug_log("dos mount: queued unmount C:");
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

// -- Atari SIO/set stubs (link-only) ----------------------------------------
// The MENU_ATARI_* cases in third_party/common/menu.c:5528-5529,5902-5966
// are unconditionally compiled, so the linker needs these symbols in every
// non-Atari kernel too. A DOS machine has no SIO drives; report failure
// (-1) / no-op so the menu shows an error instead of crashing. Real disk
// semantics land in Phases 1-3 via emux_attach_disk_image above.

int emux_atari_mount_disk(int drive, char *path) {
  (void)drive;
  (void)path;
  return -1;
}

void emux_atari_sync_drives(void) {
}

// A DOS machine has no Atari cartridges; the menu's ATARI_CART_TYPE case
// needs the symbols to link. Report empty / ignore.
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
// resumes. NOTE: no emux_ensure_video() here -- it programs VICE canvas
// state (vic_enabled/vic_showing in videoarch, unlinked in DOS
// builds). The VIC layer stays as the frame loop left it.
void emux_trap_main_loop_ui(void) {
  bmx::dos::debug_log("Dosboxx: menu opened, entering pump");
  // Refresh VIC/UI geometry on every open: video_init's one-shot call can
  // race display bring-up (zeros are rejected and never retried). By open
  // time the output is up, so the UI layer sizes itself correctly.
  {
    int dpx = 0, dpy = 0, fbw = 0, fbh = 0, sw = 0, sh = 0, dw = 0, dh = 0;
    circle_get_fbl_dimensions(FB_LAYER_VIC, &dpx, &dpy, &fbw, &fbh, &sw, &sh,
                              &dw, &dh);
    bmx::dos::debug_log("Dosboxx: menu geometry dpx=%d dpy=%d", dpx,
                             dpy);
    emux_geometry_changed(FB_LAYER_VIC);
  }
  menu_about_to_activate();
  circle_show_fbl(FB_LAYER_UI);
  {
    int dpx = 0, dpy = 0, fbw = 0, fbh = 0, sw = 0, sh = 0, dw = 0, dh = 0;
    circle_get_fbl_dimensions(FB_LAYER_UI, &dpx, &dpy, &fbw, &fbh, &sw, &sh,
                              &dw, &dh);
    bmx::dos::debug_log(
        "Dosboxx: UI layer dpx=%d dpy=%d fb=%dx%d src=%dx%d dst=%dx%d "
        "ui_showing=%d",
        dpx, dpy, fbw, fbh, sw, sh, dw, dh, ui_showing);
  }
  while (ui_enabled) {
    circle_check_gpio();
    ui_check_key();
    ui_handle_toggle_or_quick_func();
    ui_render_single_frame();
    hdmi_timing_hook();
    // Same slot as VICE's emu_pause_trap: re-shows layers hidden by video
    // adjustments so stretch/border changes preview live instead of only
    // after the menu closes.
    emux_ensure_video();
  }
  menu_about_to_deactivate();
  circle_hide_fbl(FB_LAYER_UI);
  bmx::dos::debug_log("Dosboxx: menu pump exit, resuming emulation");
}

void emux_trap_main_loop(void (*trap_func)(uint16_t, void *data), void *data) {
  (void)trap_func;
  (void)data;
}

// -- VICE glue helpers referenced by kernel.cpp ----------------------------

// No SIO/ Winning drives tracked yet; report nothing attached.
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

// No PETSCII character set on the PC; return the buffer unchanged.
uint8_t *charset_petconvstring(uint8_t *text, int mode) {
  (void)mode;
  return text;
}

// No keyboard buffer feed for dos; report no consumption.
int kbdbuf_feed(const char *text) {
  (void)text;
  return 0;
}

// VICE util used by the shared menu/UI code.
void lib_free(void *ptr) {
  free(ptr);
}

// -- settings / video / palette --------------------------------------------

void emux_add_drive_option(struct menu_item *parent, int drive) {
  // Drive C only: folder mount as C: via typed shell keystrokes (same
  // mechanism as autostart, minus the exe launch). The status line tracks
  // the last mount requested from the menu; the guest owns the truth.
  if (drive != -1) {
    return;
  }
  if (parent == nullptr) {
    return;
  }
  struct menu_item *folder = ui_menu_add_folder(parent, (char *)"Drive C");
  if (folder == nullptr) {
    return;
  }
  ::bmx::dos::s_dos_c_status = ui_menu_add_button(MENU_TEXT, folder,
                                                  (char *)"State");
  ::bmx::dos::dos_sync_c_status();
  ui_menu_add_button(MENU_DOS_DRIVE_C_MOUNT, folder,
                     (char *)"Mount folder as C:...");
  ui_menu_add_button(MENU_DOS_DRIVE_C_UNMOUNT, folder, (char *)"Unmount C:");
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
  (void)sid_parent;
  (void)sound_parent;
  // DOS sound voice: SoundBlaster type + OPL mode/emulator. The guest
  // (re)constructs SB+OPL on the RESET event, so changes apply after the
  // menu Reset (soft or hard) — the change handler says so. PC speaker
  // stays at its compiled default (on); it has no runtime rebuild path.
  struct menu_item *it;
  int i, n;

  it = ui_menu_add_multiple_choice(MENU_DOS_SBTYPE, emulation_parent,
                                   (char *)"SoundBlaster");
  n = ::bmx::dos::dos_sound_sbtype_count();
  if (n > 16) {
    n = 16;
  }
  it->num_choices = n;
  for (i = 0; i < n; i++) {
    strcpy(it->choices[i], ::bmx::dos::dos_sound_sbtype_name(i));
    it->choice_ints[i] = i;
  }
  it->value = ::bmx::dos::dos_sound_sbtype_current();

  it = ui_menu_add_multiple_choice(MENU_DOS_OPLMODE, emulation_parent,
                                   (char *)"AdLib OPL mode");
  n = ::bmx::dos::dos_sound_oplmode_count();
  if (n > 16) {
    n = 16;
  }
  it->num_choices = n;
  for (i = 0; i < n; i++) {
    strcpy(it->choices[i], ::bmx::dos::dos_sound_oplmode_name(i));
    it->choice_ints[i] = i;
  }
  it->value = ::bmx::dos::dos_sound_oplmode_current();

  it = ui_menu_add_multiple_choice(MENU_DOS_OPLEMU, emulation_parent,
                                   (char *)"OPL emulator");
  n = ::bmx::dos::dos_sound_oplemu_count();
  if (n > 16) {
    n = 16;
  }
  it->num_choices = n;
  for (i = 0; i < n; i++) {
    strcpy(it->choices[i], ::bmx::dos::dos_sound_oplemu_name(i));
    it->choice_ints[i] = i;
  }
  it->value = ::bmx::dos::dos_sound_oplemu_current();
}

void emux_add_reu_options(struct menu_item *parent) {
  (void)parent;
}

void emux_add_machine_options(struct menu_item *parent) {
  // Performance (fast-with-fallback): CPU preset (cputype+cycles; preset
  // change reboots DOS when crossing families) + manual core/cycles
  // overrides (live, no reboot).
  struct menu_item *it;
  int i, n;

  it = ui_menu_add_multiple_choice(MENU_DOS_CPU, parent, (char *)"CPU");
  n = ::bmx::dos::perf_cpu_preset_count();
  if (n > 16) {
    n = 16;
  }
  it->num_choices = n;
  for (i = 0; i < n; i++) {
    strcpy(it->choices[i], ::bmx::dos::perf_cpu_preset_name(i));
    it->choice_ints[i] = i;
  }
  it->value = ::bmx::dos::perf_cpu_preset_current();

  it = ui_menu_add_multiple_choice(MENU_DOS_CPU_CORE, parent,
                                   (char *)"CPU core");
  n = ::bmx::dos::perf_core_count();
  if (n > 16) {
    n = 16;
  }
  it->num_choices = n;
  for (i = 0; i < n; i++) {
    strcpy(it->choices[i], ::bmx::dos::perf_core_name(i));
    it->choice_ints[i] = i;
  }
  it->value = ::bmx::dos::perf_core_current();

  it = ui_menu_add_multiple_choice(MENU_DOS_CYCLES, parent,
                                   (char *)"CPU cycles");
  n = ::bmx::dos::perf_cycles_count();
  if (n > 16) {
    n = 16;
  }
  it->num_choices = n;
  for (i = 0; i < n; i++) {
    strcpy(it->choices[i], ::bmx::dos::perf_cycles_name(i));
    it->choice_ints[i] = i;
  }
  it->value = ::bmx::dos::perf_cycles_current();

  it = ::bmx::dos::s_monitor_item =
      ui_menu_add_multiple_choice(MENU_DOS_MONITOR, parent, (char *)"Monitor");
  it->num_choices = 4;
  snprintf(it->choices[0], sizeof it->choices[0], "Color");
  it->choice_ints[0] = BMX_MONITOR_COLOR;
  snprintf(it->choices[1], sizeof it->choices[1], "B&W");
  it->choice_ints[1] = BMX_MONITOR_BW;
  snprintf(it->choices[2], sizeof it->choices[2], "Green");
  it->choice_ints[2] = BMX_MONITOR_GREEN;
  snprintf(it->choices[3], sizeof it->choices[3], "Amber");
  it->choice_ints[3] = BMX_MONITOR_AMBER;
  {
    const int cur = ::bmx::dos::video_get_monitor();
    it->value = 0;
    for (i = 0; i < it->num_choices; i++) {
      if (it->choice_ints[i] == cur) {
        it->value = i;
        break;
      }
    }
  }
}

struct menu_item *emux_add_cartridge_options(struct menu_item *parent) {
  // Phase 0: nothing to add; pass the parent through so the common menu
  // keeps building on a valid folder.
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
  // Setters push into the video pipeline immediately (the emulator keeps
  // running behind the menu, so sliders track live). Re-publish current
  // values here for callers that batch edits then notify once.
  int bri, con, gam, tin, sat;
  ::bmx::dos::video_get_color_adjustments(&bri, &con, &gam, &tin, &sat);
  ::bmx::dos::video_set_color_adjustments(bri, con, gam, tin, sat);
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
  ::bmx::dos::video_set_color_adjustments(brightness, contrast, gamma,
                                              tint, saturation);
}
}  // namespace

void emux_set_color_brightness(int display_num, int value) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::dos::video_get_color_adjustments(&bri, &con, &gam, &tin, &sat);
  bri = clamp_color(value, 2000);
  set_one_color(bri, con, gam, tin, sat);
}

void emux_set_color_contrast(int display_num, int value) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::dos::video_get_color_adjustments(&bri, &con, &gam, &tin, &sat);
  con = clamp_color(value, 2000);
  set_one_color(bri, con, gam, tin, sat);
}

void emux_set_color_gamma(int display_num, int value) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::dos::video_get_color_adjustments(&bri, &con, &gam, &tin, &sat);
  gam = clamp_color(value, 4000);
  set_one_color(bri, con, gam, tin, sat);
}

void emux_set_color_tint(int display_num, int value) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::dos::video_get_color_adjustments(&bri, &con, &gam, &tin, &sat);
  tin = clamp_color(value, 2000);
  set_one_color(bri, con, gam, tin, sat);
}

void emux_set_color_saturation(int display_num, int value) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::dos::video_get_color_adjustments(&bri, &con, &gam, &tin, &sat);
  sat = clamp_color(value, 2000);
  set_one_color(bri, con, gam, tin, sat);
}

int emux_get_color_brightness(int display_num) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::dos::video_get_color_adjustments(&bri, &con, &gam, &tin, &sat);
  return bri;
}

int emux_get_color_contrast(int display_num) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::dos::video_get_color_adjustments(&bri, &con, &gam, &tin, &sat);
  return con;
}

int emux_get_color_gamma(int display_num) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::dos::video_get_color_adjustments(&bri, &con, &gam, &tin, &sat);
  return gam;
}

int emux_get_color_tint(int display_num) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::dos::video_get_color_adjustments(&bri, &con, &gam, &tin, &sat);
  return tin;
}

int emux_get_color_saturation(int display_num) {
  (void)display_num;
  int bri, con, gam, tin, sat;
  ::bmx::dos::video_get_color_adjustments(&bri, &con, &gam, &tin, &sat);
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
  // Phase 0: nothing persistent yet.
  return 0;
}

void emux_load_settings_done(void) {
  ::bmx::dos::sync_all_menu_items();
}

void emux_load_additional_settings(void) {}

void emux_save_additional_settings(FILE *fp) {
  if (fp == nullptr) {
    return;
  }
  // Performance choices (applied live; boot re-applies via perf store).
  fprintf(fp, "dos_cpupreset=%d\n", ::bmx::dos::perf_cpu_preset_current());
  // Manual overrides win over the preset: saved + loaded after it.
  fprintf(fp, "dos_core=%d\n", ::bmx::dos::perf_core_current());
  fprintf(fp, "dos_cycles=%d\n", ::bmx::dos::perf_cycles_current());
  fprintf(fp, "dos_monitor=%d\n", ::bmx::dos::video_get_monitor());
}

int emux_handle_loaded_setting(char *name, char *value_str, int value) {
  (void)value_str;
  if (name == nullptr) {
    return 0;
  }
  if (!strcmp(name, "dos_cpupreset")) {
    ::bmx::dos::perf_set_cpu_preset(value);
    return 1;
  }
  if (!strcmp(name, "dos_core")) {
    ::bmx::dos::perf_set_core(value);
    return 1;
  }
  if (!strcmp(name, "dos_cycles")) {
    ::bmx::dos::perf_set_cycles(value);
    return 1;
  }
  if (!strcmp(name, "dos_monitor")) {
    ::bmx::dos::video_set_monitor(value);
    ::bmx::dos::sync_all_menu_items();
    return 1;
  }
  return 0;
}

int emux_handle_menu_change(struct menu_item *item) {
  if (item == NULL) {
    return 0;
  }
  switch (item->id) {
    case MENU_DOS_SBTYPE:
      ::bmx::dos::dos_sound_set_sbtype(item->value);
      ui_info("SoundBlaster applies after Reset");
      return 1;
    case MENU_DOS_OPLMODE:
      ::bmx::dos::dos_sound_set_oplmode(item->value);
      ui_info("OPL mode applies after Reset");
      return 1;
    case MENU_DOS_OPLEMU:
      ::bmx::dos::dos_sound_set_oplemu(item->value);
      ui_info("OPL emulator applies after Reset");
      return 1;
    case MENU_DOS_CPU_CORE:
      ::bmx::dos::perf_set_core(item->value);
      return 1;
    case MENU_DOS_CYCLES:
      ::bmx::dos::perf_set_cycles(item->value);
      return 1;
    case MENU_DOS_CPU:
      if (::bmx::dos::perf_set_cpu_preset(item->value)) {
        ui_info("CPU change reboots DOS");
      }
      return 1;
    case MENU_DOS_MONITOR: {
      int v = item->value;
      if (v >= 0 && v < item->num_choices) {
        v = item->choice_ints[v];
      }
      ::bmx::dos::video_set_monitor(v);
      return 1;
    }
    case MENU_DOS_DRIVE_C_UNMOUNT: {
      ui_info("Unmounting...");
      emux_dos_unmount_c();
      ui_pop_all_and_toggle();
      return 1;
    }
    case MENU_ATARI_SCREENSHOT: {
      // Auto-named BMP dump of the VIC canvas (same volume preference as
      // the atari800 flow: USB stick first, then SD system volume).
      static const char *kShotDirs[] = {
          "USER:/systems/DOS/snapshots",
          "SYS:/systems/DOS/snapshots",
          "SD:/systems/DOS/snapshots",
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
          if (::bmx::dos::video_save_screenshot(path)) {
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
// provided by src/network/network_service.cpp (included in ATARI800 builds).
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
// The DOS kernel links shared BMX objects (async_network, kernel GPIO /
// userport paths, common menu/ui listing helpers) that reference VICE
// symbols on VICE machines. The DOSBox-X core has no equivalent, so provide
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

// userport (kernel.cpp GPIO paths; a PC has no userport CIA)
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

// PETSCII helpers (common ui.c; a PC uses ASCII, no conversion)
uint8_t charset_petscii_to_screencode(uint8_t code, unsigned int reverse_mode) {
  (void)reverse_mode;
  return code;
}

void vsync_suspend_speed_eval(void) {
  bmx::dos::debug_log("Dosboxx: menu closed");
}

}  // extern "C"
