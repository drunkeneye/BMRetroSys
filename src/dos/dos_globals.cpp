//
// dos_globals.cpp
//
// BMX-owned link stubs: DATA globals owned by deleted (non-vendored)
// DOSBox-X translation units, needed so the 72 vendored objects link.
//
// Rule (Phase 1): zero-init everything. Plain definitions for class types
// (SDL_Block, Render_t, MENU_Block, DOSBoxMenu, DB_Midi, GLIDE_Block,
// PC98_GDC_state, std::string, std::map) so no default-ctor questions arise;
// `= false` for bools, `= 0` for integers/chars/enums (JOY_NONE and
// MOUSE_EMULATION_NEVER are 0, matching upstream BSS state), `= nullptr`
// for pointers (including the 16 void* PIC/DMA callback globals and the
// RENDER_DrawLine scaler line-handler pointer), `= {}` for POD arrays.
// Where upstream documents a non-zero default it is noted in a comment for
// Phase 2 (which sets real values from config); nothing here boots DOS.
//
// RENDER_DrawLine is DATA (include/render.h: `extern ScalerLineHandler_t`,
// i.e. a function pointer), so it is defined here as nullptr. The real
// line handler lands in dos_video.cpp in Phase 2, which will assign it.
// Do NOT define `main` here: BMX src/main.cpp provides it on target.
//
// Never add vendored .cpp/.h edits for these; this file is the owner.
//

#include "dosbox.h"
#include "sdlmain.h"
#include "render.h"
#include "menu.h"
#include "menudef.h"
#include "midi.h"
#include "mouse.h"
#include "serialport.h"
#include "parport.h"
#include "vga.h"
#include "pc98_gdc.h"
#include "pc98_dac.h"
#include "jfont.h"
#include "joystick.h"
#include "dos_inc.h"
#include "fpu.h"
#include "ide.h"
#include "glidedef.h"
#include "pci_bus.h"
#include "shiftjis.h"

#include <map>
#include <string>

// ---- Deleted owner: src/gui/sdlmain.cpp ----
SDL_Block sdl;
Bitu frames = 0;
Bitu time_limit_ms = 0;
double rtdelta = 0;
std::string configfile = "";
std::string working_dir = "";
std::string strPasteBuffer = "";
bool dos_kernel_disabled = false;      // upstream: true until DOS_Init
bool dos_shell_running_program = false;
bool custom_bios = false;
bool enableime = false;
bool tonoime = false;
bool tooutttf = false;
bool usesystemcursor = false;
bool rtl = false;
const char *modifier = nullptr;        // upstream: unset until sdl section parsed
int paste_speed = 0;                   // upstream: 20
int wheel_key = 0;
bool wheel_guest = false;
bool clipboard_dosapi = false;         // upstream: true
bool clipboard_biospaste = false;      // upstream: true (non-WIN32 path)
unsigned int hostkeyalt = 0;
unsigned int sendkeymap = 0;
bool startup_state_numlock = false;
bool startup_state_capslock = false;
bool startup_state_scrlock = false;
MOUSE_EMULATION user_cursor_emulation = MOUSE_EMULATION_NEVER;
bool user_cursor_locked = false;
int user_cursor_sw = 0;                // upstream: 640
int user_cursor_sh = 0;                // upstream: 480
int user_cursor_x = 0;
int user_cursor_y = 0;
bool kana_input = false;
bool winrun = false;
bool use_save_file = false;
uint32_t GFX_Rmask = 0;
unsigned char GFX_Rshift = 0;
uint32_t GFX_Gmask = 0;
unsigned char GFX_Gshift = 0;
uint32_t GFX_Bmask = 0;
unsigned char GFX_Bshift = 0;
uint32_t GFX_Amask = 0;
unsigned char GFX_bpp = 0;

// ---- Deleted owner: src/gui/render.cpp ----
Render_t render;
int eurAscii = 0;                      // upstream: -1 (no euro mapping)
// RENDER_DrawLine is a function POINTER (ScalerLineHandler_t); nullptr
// until dos_video.cpp assigns the real handler in Phase 2.
ScalerLineHandler_t RENDER_DrawLine = nullptr;

// ---- Deleted owner: src/gui/menu.cpp ----
DOSBoxMenu mainMenu;
MENU_Block menu;

// ---- Deleted owner: src/gui/menu_callback.cpp ----
std::string savefilename = "";

// ---- Deleted owner: src/gui/midi.cpp ----
DB_Midi midi;

// ---- Deleted owner: src/gui/sdl_mapper.cpp ----
JoystickType joytype = JOY_NONE;
bool dotype = false;
bool load = false;

// ---- Deleted owner: src/hardware/serialport/serialport.cpp ----
CSerial *serialports[9] = {};
bool serialMouseEmulated = false;
void *Serial_EventHandler_PIC_Event = nullptr;

// ---- Deleted owner: src/hardware/parport/parport.cpp ----
CParallel *parallelPortObjects[9] = {};

// ---- Deleted owner: src/misc/savestates.cpp ----
bool auto_save_state = false;
bool force_load_state = false;
bool noremark_save_state = false;

// ---- Deleted owner: src/misc/clipboard.cpp ----
uint8_t *clipAscii = nullptr;
uint32_t clipSize = 0;
bool swapad = false;                   // upstream: true

// ---- Deleted owner: src/output/output_ttf.cpp ----
std::map<int, int> lowboxdrawmap;      // upstream: box-drawing table entries
std::map<int, int> pc98boxdrawmap;     // upstream: box-drawing table entries
uint16_t cpMap_AX[32] = {};            // upstream: AX codepage table

// ---- Deleted owner: src/dos/dos_programs.cpp ----
// VENDORED (not stubbed): MOUNT/INTRO/etc. need the real TU, which defines
// its own globals with upstream defaults. Nothing stubbed here anymore.

// ---- Deleted owner: src/dos/drive_overlay.cpp ----
std::string prefix_overlay = ".DBOVERLAY"; // upstream drive_overlay.cpp:46

// ---- Deleted owner: src/hardware/vga_pc98_crtc.cpp ----
uint8_t GDC_display_plane = 0;
uint8_t GDC_display_plane_pending = 0;
bool GDC_vsync_interrupt = false;
uint8_t GDC_display_plane_wait_for_vsync = 0;
bool gdc_5mhz_mode = false;
bool gdc_5mhz_mode_initial = false;
bool enable_pc98_16color = false;      // upstream: true
bool enable_pc98_256color = false;     // upstream: true
bool enable_pc98_256color_planar = false; // upstream: true
bool enable_pc98_188usermod = false;   // upstream: true
bool enable_pc98_grcg = false;
bool enable_pc98_egc = false;
bool pc98_256kb_boundary = false;
uint8_t pc98_gdc_modereg = 0;
uint8_t pc98_gdc_tile_counter = 0;
uint8_t pc98_gdc_vramop = 0;
egc_quad pc98_gdc_tiles;

// ---- Deleted owner: src/hardware/vga_pc98_cg.cpp ----
uint16_t a1_font_load_addr = 0;
uint8_t a1_font_char_offset = 0;
bool pc98_cg_kanji_dot_access_mode = false;

// ---- Deleted owner: src/hardware/vga_pc98_dac.cpp ----
uint8_t pc98_pal_vga[256 * 3] = {};
uint8_t pc98_pal_analog[256 * 3] = {};
uint8_t pc98_pal_digital[8] = {};
uint32_t pc98_text_palette[8] = {};

// ---- Deleted owner: src/hardware/vga_pc98_egc.cpp ----
uint8_t pc98_egc_access = 0;
uint8_t pc98_egc_maskef[2] = {};
uint8_t pc98_egc_mask[2] = {};
uint8_t pc98_egc_srcmask[2] = {};
uint8_t pc98_egc_fgc = 0;
uint8_t pc98_egc_lead_plane = 0;
uint8_t pc98_egc_compare_lead = 0;
uint8_t pc98_egc_lightsource = 0;
uint8_t pc98_egc_shiftinput = 0;
uint8_t pc98_egc_regload = 0;
uint8_t pc98_egc_rop = 0;              // upstream: 0xF0
bool pc98_egc_shift_descend = false;
uint8_t pc98_egc_shift_destbit = 0;
uint8_t pc98_egc_shift_srcbit = 0;
uint16_t pc98_egc_shift_length = 0;    // upstream: 0xF

// ---- Deleted owner: src/hardware/vga_pc98_gdc.cpp ----
struct PC98_GDC_state pc98_gdc[2];
const UINT8 gdc_defsyncm24[8] = {};    // upstream: {0x10,0x4e,0x07,0x25,0x07,0x07,0x90,0x65}
const UINT8 gdc_defsyncs24[8] = {};    // upstream: {0x06,0x26,0x03,0x11,0x83,0x07,0x90,0x65}
const UINT8 gdc_defsyncm31[8] = {};    // upstream: {0x10,0x4e,0x47,0x0c,0x07,0x0d,0x90,0x89}
const UINT8 gdc_defsyncs31[8] = {};    // upstream: {0x06,0x26,0x41,0x0c,0x83,0x0d,0x90,0x89}

// ---- Deleted owner: src/ints/int10_vesa.cpp ----
int hack_lfb_xadjust = 0;
int hack_lfb_yadjust = 0;
bool vesa_bank_switch_window_range_check = false; // upstream: true
bool vesa_bank_switch_window_mirror = false;
int vesa_set_display_vsync_wait = 0;   // upstream: -1 (auto)
bool vesa_zero_on_get_information = false; // upstream: true

// ---- Deleted owner: src/ints/int_dosv.cpp ----
bool del_flag = false;                 // upstream: true
uint8_t jfont_sbcs_16[256 * 16] = {};
uint8_t jfont_sbcs_19[256 * 19] = {};
uint8_t jfont_dbcs_16[65536 * 32] = {};
uint8_t jfont_cache_dbcs_16[65536] = {};
uint16_t j3_font_offset = 0;           // upstream: 0xca00
// pc98_freecg_sbcs is DEFINED in include/jfont.h but inside `#ifdef
// INCJFONT` (font-table TU opt-in); the Phase-1 subset never defines
// INCJFONT, so define it here with the exact upstream size (256x16).
uint8_t pc98_freecg_sbcs[256 * 16] = {};

// ---- Deleted owner: src/cpu/mmx.cpp ----
MMX_reg *reg_mmx[8] = {};
MMX_reg *lookupRMregMM[256] = {};

// ---- Deleted owner: src/ints/imagedisk_int13.cpp ----
unsigned int INT13Xfer = 0;

// ---- Deleted owner: src/hardware/tandy_sound.cpp ----
uint8_t BIOS_tandy_D4_flag = 0;
void *TandyDAC_DMA_CallBack_Func = nullptr;

// ---- Deleted owner: src/hardware/sblaster.cpp ----
// VENDORED (not stubbed): DMA/DSP PIC-event + callback pointers are real.

// ---- Deleted owner: src/hardware/gus.cpp ----
void *GUS_TimerEvent_PIC_Event = nullptr;
void *GUS_DMA_Callback_Func = nullptr;

// ---- Deleted owner: src/hardware/mpu401.cpp ----
void *MPU401_Event_PIC_Event = nullptr;

// ---- Deleted owner: src/hardware/disney.cpp ----
void *DISNEY_disable_PIC_Event = nullptr;

// ---- Deleted owner: src/hardware/pc98_fm.cpp ----
void *fmport_a_pic_event_PIC_Event = nullptr;
void *fmport_b_pic_event_PIC_Event = nullptr;

// ---- Deleted owner: src/hardware/glide.cpp ----
GLIDE_Block glide;
bool addovl = false;

// ---- Deleted owner: src/hardware/ide.cpp ----
// Upstream names, verbatim (ide.cpp): SetupConfigSections creates one config
// section per name, and IDE_Init looks them up by name. Zero-init ({}) left
// every entry nullptr and crashed SetupConfigSections in std::string(nullptr).
const char *ide_names[MAX_IDE_CONTROLLERS] = {
    "ide, primary",   "ide, secondary", "ide, tertiary", "ide, quaternary",
    "ide, quinternary", "ide, sexternary", "ide, septernary", "ide, octernary"};

// ---- Deleted owner: src/hardware/pci_bus.cpp ----
bool pcibus_enable = false;

// ---- Deleted owner: src/debug/debug_gui.cpp ----
int log_dev_con = 0;
bool log_fileio = false;
bool log_int21 = false;

// NOTE (ARM link): the capture/autosave globals once listed here
// (CaptureState, enable_autosave, autosave_*, video_debug_overlay,
// config_run_var_device/devparm, device_nextdrive) are DEFINED by vendored
// hardware.cpp/programs.cpp (the mac host-link list was generated while
// those objects failed to compile, so it wrongly marked them undefined).
// They live there now; nothing to define here.
