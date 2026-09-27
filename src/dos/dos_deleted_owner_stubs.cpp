//
// dos_deleted_owner_stubs.cpp
//
// BMX-owned link stubs, part 2: free functions and leaf methods owned by
// deleted (non-vendored) DOSBox-X translation units, discovered by
// host-link iteration over the 72 vendored objects. UNDEF_PHASE1.txt covered
// only a subset of the true undefined set; everything here is inert Phase-1
// no-op (null/absent backend paths), mirroring upstream miss-path behavior.
//
// What lives where:
//   dos_globals.cpp .......... all DATA globals (zero-init rule).
//   dos_ide_cdrom_stubs.cpp .. IDE attach/detach + MSCDEX backend.
//   THIS file .................... every other stub-able free function and
//                                  out-of-line method of a header-defined
//                                  class (signatures compiler-checked via the
//                                  upstream headers below).
//
// Deliberately NOT stubbed here (see README.BMX "resisters" + final report):
//   - vtable/typeinfo/methods of classes CONSTRUCTED by vendored code whose
//     objects carry live behavior (Program, imageDisk family, isoDrive,
//     Overlay_Drive, physfs drives, QCow2). Faking those vtables would be
//     wrong; their drive/shell backends are Phase-2+ work.
//
// Safe-default rule: void -> ignore params; bool -> false; integers/enums ->
// 0 (or the upstream "absent" enumerator); pointers -> nullptr; std::string
// -> empty; out-params -> zeroed. Nothing here runs before Phase 2 brings
// the real backends; the host-link test never calls any of it.
//

#include "dosbox.h"
#include "control.h"
#include "setup.h"
#include "mem.h"
#include "hardware.h"
#include "render.h"
#include "video.h"
#include "menu.h"
#include "programs.h"
#include "mouse.h"
#include "joystick.h"
#include "jfont.h"
#include "dos_inc.h"
#include "bios_disk.h"
#include "ide.h"
#include "vga.h"
#include "pc98_gdc.h"
#include "pc98_dac.h"
#include "pc98_artic.h"
#include "fpu.h"
#include "pci_bus.h"
#include "mapper.h"
#include "logging.h"
#include "8255.h"
#include "serialport.h"
#include "parport.h"
#include "regionalloctracking.h"
#include "shiftjis.h"
#include "int10.h"
#include "sdlmain.h"

#include "dos_debug.h"

#include <cstdarg>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iosfwd>
#include <map>
#include <string>
#include <vector>

// ---- Deleted owner: src/debug/debug_gui.cpp ----
// Route the DOSBox-X log channel into the BMX SD log (bmretrosys-dos.log) so
// guest-side diagnostics are visible on bare metal, where there is no
// stderr/console. LOG_MSG always forwards; LOG() forwards NORMAL and up
// (DEBUG stays off: per-instruction tracing would flood the SD card, and
// every debug_log line fsyncs).
void DEBUG_ShowMsg(char const *format, ...) {
  char body[1024];
  va_list ap;
  va_start(ap, format);
  vsnprintf(body, sizeof(body), format, ap);
  va_end(ap);
  if (!::bmx::dos::debug_guest_pass(body)) {
    return;
  }
  ::bmx::dos::debug_log("dosbox-x: %s", body);
}

void LOG::operator()(char const *format, ...) {
  if (d_severity < LOG_NORMAL) {
    return;
  }
  char body[1024];
  va_list ap;
  va_start(ap, format);
  vsnprintf(body, sizeof(body), format, ap);
  va_end(ap);
  if (!::bmx::dos::debug_guest_pass(body)) {
    return;
  }
  ::bmx::dos::debug_log("dosbox-x: %s", body);
}

// ---- Deleted owner: src/gui/sdlmain.cpp ----
bool DOSBox_Paused() { return false; }
void DOSBox_SetSysMenu() {}
void KeyboardLayoutDetect() {}
void UpdateKeyboardLEDState(Bitu led_state) { (void)led_state; }
bool sdl_wait_on_error() { return false; }
void MacOSEnableWindowCapture(unsigned int enable) { (void)enable; }
// VM_Boot_DOSBox_Kernel is REAL (src/dos/dos_boot.cpp); the deleted
// sound/CD/layout shutdown/startup helpers it references stay no-ops
// (never-started backends in Phase 1/2; US layout default).
// NOTE: SBLASTER_DOS_Shutdown is REAL (vendored sblaster.cpp); only GUS (no
// GUS hardware on BMX) stays stubbed here.
void GUS_DOS_Shutdown() {}
void MSCDEX_Startup(Section *sec) { (void)sec; }
void DOS_KeyboardLayout_Startup(Section *sec) { (void)sec; }
void PasteClipboard(bool bPressed) { (void)bPressed; }
// NOTE: PROGRAMS_Shutdown is defined by vendored programs.cpp (removed dup).
void POD_Save_Sdlmain(std::ostream &stream) { (void)stream; }
void POD_Load_Sdlmain(std::istream &stream) { (void)stream; }

// ---- Deleted owner: src/gui/render.cpp ----
// RENDER_SetSize/StartUpdate/EndUpdate + the DrawLine sink are REAL now
// (src/dos/dos_video.cpp); only the palette hook stays a no-op
// (DAC xlat tables are resolved internally, no SDL surface palette).
void RENDER_SetPal(uint8_t entry, uint8_t red, uint8_t green, uint8_t blue) {
  (void)entry; (void)red; (void)green; (void)blue;
}
bool RENDER_IsScalerCompatibleWithDoublescan() { return false; }

// ---- Deleted owner: src/gui/menu.cpp (DOSBoxMenu is header-defined;
// vendored code drives mainMenu through these; dummy item for ref returns)
DOSBoxMenu::DOSBoxMenu() {}
DOSBoxMenu::~DOSBoxMenu() {}
DOSBoxMenu::item::item() {}
DOSBoxMenu::item::~item() {}
void DOSBoxMenu::item::refresh_item(DOSBoxMenu &menu) { (void)menu; }
bool DOSBoxMenu::item_exists(const std::string &name) {
  (void)name; return false;
}
bool DOSBoxMenu::item_exists(const item_handle_t i) {
  (void)i; return false;
}
std::vector<DOSBoxMenu::item> DOSBoxMenu::get_master_list() {
  return std::vector<DOSBoxMenu::item>();
}
DOSBoxMenu::item &DOSBoxMenu::get_item(const std::string &name) {
  (void)name;
  static item dummy;
  return dummy;
}
DOSBoxMenu::item &DOSBoxMenu::get_item(const item_handle_t i) {
  (void)i;
  static item dummy;
  return dummy;
}
DOSBoxMenu::item &DOSBoxMenu::alloc_item(const enum item_type_t type,
                                         const std::string &name) {
  (void)type; (void)name;
  static item dummy;
  return dummy;
}
void DOSBox_CheckOS(int &id, int &major, int &minor) {
  id = 0; major = 0; minor = 0;
}
void GFX_SetTitle(int32_t cycles, int frameskip, Bits timing, bool paused) {
  (void)cycles; (void)frameskip; (void)timing; (void)paused;
}
void GUI_Shortcut(int select) { (void)select; }
void change_output(int output) { (void)output; }
void SetVal(const std::string &secname, const std::string &preval,
            const std::string &val) {
  (void)secname; (void)preval; (void)val;
}

// ---- Deleted owner: src/gui/sdl_mapper.cpp ----
void MAPPER_AddHandler(MAPPER_Handler *handler, MapKeys key, Bitu mods,
                       char const *const eventname, char const *const buttonname,
                       DOSBoxMenu::item **ret_menuitem) {
  (void)handler; (void)key; (void)mods;
  (void)eventname; (void)buttonname;
  // Callers (e.g. DOS_RealInit) immediately do item->set_text/
  // set_description on the out-param: leaving it untouched left a garbage
  // pointer and hard-faulted with no log. set_* are header-inline,
  // non-virtual string assigns, so a static dummy is safe (BSS-zeroed).
  if (ret_menuitem != nullptr) {
    static DOSBoxMenu::item dummy;
    *ret_menuitem = &dummy;
  }
}
void MAPPER_AutoType(std::vector<std::string> &sequence, const uint32_t wait_ms,
                     const uint32_t pace_ms, bool choice) {
  (void)sequence; (void)wait_ms; (void)pace_ms; (void)choice;
}
std::map<std::string, std::string> get_event_map() {
  return std::map<std::string, std::string>();
}
std::string mapper_event_keybind_string(const std::string &x) {
  (void)x; return std::string();
}
void set_eventbutton_text(const char *eventname, const char *buttonname) {
  (void)eventname; (void)buttonname;
}
void update_bindbutton_text() {}

// ---- Deleted owner: src/gui/midi.cpp ----
void MIDI_ListHandler(Program *caller, const char *name) {
  (void)caller; (void)name;
}

// ---- Deleted owner: src/hardware/hardware.cpp (CAPTURE_* wave-data sinks
// are BMX-owned) ----
// NOTE: SB_Get_Address is REAL (vendored sblaster.cpp: reads live card
// state, correct for sbtype=none too). TS_Get_Address (Tandy) stays stubbed
// below: no Tandy hardware on BMX.
bool TS_Get_Address(Bitu &tsaddr, Bitu &tsirq, Bitu &tsdma) {
  tsaddr = 0; tsirq = 0; tsdma = 0; return false;
}

// ---- Deleted owner: src/hardware/mixer.cpp (declares only; defs above) ----

// ---- Deleted owners: midi.cpp / mpu401.cpp / gameblaster.cpp / pas.cpp --
// (MIDI/MPU-401/CMS/PAS backends absent on BMX. Referenced by vendored
// sblaster.cpp/adlib.cpp but never CALLED with the BMX defaults
// sbtype=sb16 + oplmode=opl3: MIDI only flows in MPU intelligent mode,
// PAS/CMS only init for pas/cms sbtypes, adlib_commandreg only matters
// for the GUS 0x388 redirect. MPU401_GetIRQ=-1 lands on the SC400
// "MPU IRQ disabled" path, which is the honest answer here.)
void MIDI_RawOutByte(uint8_t data) { (void)data; }
bool MIDI_Available(void) { return false; }
int MPU401_GetIRQ(void) { return -1; }
uint8_t adlib_commandreg = 0;
void CMS_Init(Section *sec) { (void)sec; }
void CMS_ShutDown(Section *sec) { (void)sec; }
bool PAS_FMSplit(void) { return false; }
void PAS_Init(unsigned int type, Bitu sb_base, Bitu irq, Bitu dma,
              bool use_mixer) {
  (void)type; (void)sb_base; (void)irq; (void)dma; (void)use_mixer;
}
void PAS_ShutDown(void) {}

// ---- Deleted owner: src/hardware/gus.cpp ----
void POD_Save_GUS(std::ostream &stream) { (void)stream; }
void POD_Load_GUS(std::istream &stream) { (void)stream; }

// ---- Deleted owner: src/hardware/mpu401.cpp ----
void POD_Save_MPU401(std::ostream &stream) { (void)stream; }
void POD_Load_MPU401(std::istream &stream) { (void)stream; }

// ---- Deleted owner: src/hardware/disney.cpp ----
void POD_Save_Disney(std::ostream &stream) { (void)stream; }
void POD_Load_Disney(std::istream &stream) { (void)stream; }

// ---- Deleted owner: src/hardware/gameblaster.cpp ----
void POD_Save_Gameblaster(std::ostream &stream) { (void)stream; }
void POD_Load_Gameblaster(std::istream &stream) { (void)stream; }

// ---- Deleted owner: src/hardware/tandy_sound.cpp ----
void POD_Save_Tandy_Sound(std::ostream &stream) { (void)stream; }
void POD_Load_Tandy_Sound(std::istream &stream) { (void)stream; }

// ---- Deleted owner: src/hardware/serialport/serialport.cpp ----
Bitu bios_post_comport_count() { return 0; }
void BIOS_Post_register_comports() {}
void BIOS_Post_register_comports_PNP() {}
void on_mouse_event_for_serial(int delta_x, int delta_y, uint8_t buttonstate) {
  (void)delta_x; (void)delta_y; (void)buttonstate;
}

// ---- Deleted owner: src/hardware/parport/parport.cpp ----
Bitu bios_post_parport_count() { return 0; }
void BIOS_Post_register_parports() {}

// ---- Deleted owner: src/hardware/pci_bus.cpp ----
// (pcibus_enable DATA lives in dos_globals.cpp)
bool PCI_IsInitialized() { return false; }
void PCI_AddSVGADOSBoxIG_Device() {}
RealPt PCI_GetPModeInterface() { return 0; }

// ---- Deleted owner: src/hardware/ide.cpp (free fns; attach/detach in
// dos_ide_cdrom_stubs.cpp) ----
void IDE_Auto(signed char &index, bool &slave) {
  index = -1; slave = false;
}
void IDE_ResetDiskByBIOS(unsigned char disk) { (void)disk; }
void IDE_Hard_Disk_Detach(unsigned char bios_disk_index) {
  (void)bios_disk_index;
}
void IDE_EmuINT13DiskReadByBIOS(unsigned char disk, unsigned int cyl,
                                unsigned int head, unsigned sect) {
  (void)disk; (void)cyl; (void)head; (void)sect;
}
void IDE_EmuINT13DiskReadByBIOS_LBA(unsigned char disk, uint64_t lba) {
  (void)disk; (void)lba;
}
bool IDE_GetPhysGeometry(unsigned char disk, uint32_t &heads, uint32_t &cyl,
                         uint32_t &sect, uint32_t &size, uint64_t &LBA) {
  (void)disk;
  heads = 0; cyl = 0; sect = 0; size = 0; LBA = 0;
  return false;
}
void IDE_ATAPI_MediaChangeNotifyAll(bool immediate) { (void)immediate; }
void DOS_EnableDriveIDEMenu(unsigned int idx, unsigned char ms) {
  (void)idx; (void)ms;
}

// ---- Deleted owner: src/hardware/vga_s3.cpp (SVGA setup) ----
// Only the VRAM sizing is needed for boot/text: upstream sets 2MB when the
// user left vmemsize at 0 (vga_s3.cpp:698-699). Without it VGA_SetupMemory
// (end of VGA_Reset) allocates 32 bytes and the first text clear faults.
// CRTC/SEQ hooks + streams stay Phase-2 (SVGA modes); text uses the
// standard VGA path. Tseng/Paradise/ATI/XGA never selected (S3 default).
void SVGA_Setup_S3Trio() {
  if (vga.mem.memsize == 0) {
    vga.mem.memsize = vga.mem.memsize_original = 2u * 1024u * 1024u;
  }
}
void SVGA_Setup_TsengET3K() {}
void SVGA_Setup_TsengET4K() {}
void SVGA_Setup_ParadisePVGA1A() {}
void SVGA_Setup_ATI() {}
void VGA_SetupXGA() {}
bool SVGA_S3_AcceptsMode(Bitu mode) { (void)mode; return false; }
void PC98_FM_Destroy(Section *sec) { (void)sec; }
void PC98_FM_OnEnterPC98(Section *sec) { (void)sec; }

// ---- Deleted owner: src/hardware/vga_xga.cpp ----
Bitu XGA_Read(Bitu port, Bitu len) { (void)port; (void)len; return 0; }
void XGA_Write(Bitu port, Bitu val, Bitu len) {
  (void)port; (void)val; (void)len;
}
void POD_Save_VGA_XGA(std::ostream &stream) { (void)stream; }
void POD_Load_VGA_XGA(std::istream &stream) { (void)stream; }

// ---- Deleted owner: src/hardware/vga_s3.cpp ----
void POD_Save_VGA_S3(std::ostream &stream) { (void)stream; }
void POD_Load_VGA_S3(std::istream &stream) { (void)stream; }

// ---- Deleted owner: src/hardware/vga_tseng.cpp ----
void POD_Save_VGA_Tseng(std::ostream &stream) { (void)stream; }
void POD_Load_VGA_Tseng(std::istream &stream) { (void)stream; }
bool et4k_highcolor_half_pixel_rate() { return false; }

// ---- Deleted owner: src/hardware/vga_paradise.cpp ----
void POD_Save_VGA_Paradise(std::ostream &stream) { (void)stream; }
void POD_Load_VGA_Paradise(std::istream &stream) { (void)stream; }

// ---- Deleted owner: src/hardware/vga_pc98_gdc.cpp ----
bool gdc_5mhz_according_to_bios() { return false; }
void pc98_update_cpu_page_ptr() {}
void pc98_update_display_page_ptr() {}

// ---- Deleted owner: src/hardware/vga_pc98_crtc.cpp (declares; defs above
// live in dos_globals.cpp as DATA) ----

// ---- Deleted owner: src/hardware/pc98_fm.cpp ----
bool PC98_FM_SoundBios_Enabled() { return false; }
Bitu PC98_AVSDRV_PCM_Handler() { return 0; }

// ---- Deleted owner: src/hardware/floppy.cpp ----
void BIOS_Post_register_FDC() {}
void BIOS_Post_register_IDE() {}

// ---- Deleted owner: src/ints/int10_vesa.cpp ----
void VESA_OnReset_Clear_Callbacks() {}
uint8_t VESA_GetSVGAInformation(uint16_t seg, uint16_t off) {
  (void)seg; (void)off; return 0;
}
uint8_t VESA_GetSVGAModeInformation(uint16_t mode, uint16_t seg, uint16_t off) {
  (void)mode; (void)seg; (void)off; return 0;
}
uint8_t VESA_SetSVGAMode(uint16_t mode) { (void)mode; return 0; }
uint8_t VESA_GetSVGAMode(uint16_t &mode) { mode = 0; return 0; }
uint8_t VESA_SetCPUWindow(uint8_t window, uint16_t address) {
  (void)window; (void)address; return 0;
}
uint8_t VESA_GetCPUWindow(uint8_t window, uint16_t &address) {
  (void)window; address = 0; return 0;
}
uint8_t VESA_ScanLineLength(uint8_t subcall, uint16_t val, uint16_t &bytes,
                            uint16_t &pixels, uint16_t &lines) {
  (void)subcall; (void)val; bytes = 0; pixels = 0; lines = 0; return 0;
}
uint8_t VESA_SetDisplayStart(uint16_t x, uint16_t y, bool wait) {
  (void)x; (void)y; (void)wait; return 0;
}
uint8_t VESA_GetDisplayStart(uint16_t &x, uint16_t &y) {
  x = 0; y = 0; return 0;
}
uint8_t VESA_SetPalette(PhysPt data, Bitu index, Bitu count, bool wait) {
  (void)data; (void)index; (void)count; (void)wait; return 0;
}
uint8_t VESA_GetPalette(PhysPt data, Bitu index, Bitu count) {
  (void)data; (void)index; (void)count; return 0;
}
Bitu INT10_WriteVESAModeList(Bitu max_modes) {
  (void)max_modes; return 0;
}
void INT10_SetupVESA() {}
Bitu INT10_VideoState_GetSize(Bitu state) { (void)state; return 0; }
bool INT10_VideoState_Save(Bitu state, RealPt buffer) {
  (void)state; (void)buffer; return false;
}
bool INT10_VideoState_Restore(Bitu state, RealPt buffer) {
  (void)state; (void)buffer; return false;
}

// ---- Deleted owner: src/ints/int_dosv.cpp ----
Bitu INT60_Handler() { return 0; }
Bitu INT6F_Handler() { return 0; }
void INT8_DOSV() {}
void INT8_J3() {}
void INT60_J3_Setup() {}
void J3_OffCursor() {}
bool J3_IsJapanese() { return false; }
bool J3_IsCga4Dcga() { return false; }
uint16_t J3_GetMachineCode() { return 0; }
void J3_SetBiosArea(uint16_t mode) { (void)mode; }
void J3_GetPalette(uint8_t no, uint8_t &r, uint8_t &g, uint8_t &b) {
  (void)no; r = 0; g = 0; b = 0;
}
void J3_SetType(std::string type, std::string back, std::string text) {
  (void)type; (void)back; (void)text;
}
void JFONT_Init() {}
void InitFontHandle() {}
void ShutFontHandle() {}
bool LoadFontxFile(const char *fname, int height, bool dbcs) {
  (void)fname; (void)height; (void)dbcs; return false;
}
uint8_t *GetDbcsFont(Bitu code) { (void)code; return nullptr; }
uint8_t *GetDbcs14Font(Bitu code, bool &is14) {
  (void)code; is14 = false; return nullptr;
}
uint8_t *GetDbcs24Font(Bitu code) { (void)code; return nullptr; }
uint8_t *GetSbcsFont(Bitu code) { (void)code; return nullptr; }
uint8_t *GetSbcs19Font(Bitu code) { (void)code; return nullptr; }
uint8_t *GetSbcs24Font(Bitu code) { (void)code; return nullptr; }
uint16_t GetTextSeg() { return 0; }
uint16_t GetGaijiSeg() { return 0; }
uint8_t GetKanjiAttr() { return 0; }
uint8_t GetKanjiAttr(Bitu x, Bitu y) { (void)x; (void)y; return 0; }
bool isKanji1(uint8_t chr) { (void)chr; return false; }
bool isKanji1_gbk(uint8_t chr) { (void)chr; return false; }
bool isKanji2(uint8_t chr) { (void)chr; return false; }
bool DOSV_CheckCJKVideoMode() { return false; }
bool INT10_DOSV_SetCRTBIOSMode(Bitu mode) { (void)mode; return false; }
void DOSV_SetConfig(Section_prop *section) { (void)section; }
void DOSV_Setup() {}
void DOSV_OffCursor() {}
enum DOSV_VTEXT_MODE DOSV_GetVtextMode(Bitu no) { (void)no; return DOSV_VGA; }
enum DOSV_FEP_CTRL DOSV_GetFepCtrl() {
  return static_cast<DOSV_FEP_CTRL>(0);
}
uint16_t DOSV_GetFontHandlerOffset(enum DOSV_FONT font) {
  (void)font; return 0;
}
uint8_t GetTrueVideoMode() { return 0; }
void SetTrueVideoMode(uint8_t mode) { (void)mode; }

// ---- Deleted owner: src/ints/pc98_lio.cpp ----
uint8_t PC98_BIOS_LIO_GINIT() { return 0; }
uint8_t PC98_BIOS_LIO_GSCREEN() { return 0; }
uint8_t PC98_BIOS_LIO_GVIEW() { return 0; }
uint8_t PC98_BIOS_LIO_GCOLOR1() { return 0; }
uint8_t PC98_BIOS_LIO_GCOLOR2() { return 0; }
uint8_t PC98_BIOS_LIO_GCLS() { return 0; }
uint8_t PC98_BIOS_LIO_GPSET() { return 0; }
uint8_t PC98_BIOS_LIO_GLINE() { return 0; }
uint8_t PC98_BIOS_LIO_GCIRCLE() { return 0; }
uint8_t PC98_BIOS_LIO_GPAINT1() { return 0; }
uint8_t PC98_BIOS_LIO_GPAINT2() { return 0; }
uint8_t PC98_BIOS_LIO_GPUT1() { return 0; }
uint8_t PC98_BIOS_LIO_GPUT2() { return 0; }
uint8_t PC98_BIOS_LIO_GGET() { return 0; }
uint8_t PC98_BIOS_LIO_GPOINT2() { return 0; }
void lio_read_parameter() {}
unsigned char byte_reverse(unsigned char c) {
  unsigned char r = 0;
  for (int i = 0; i < 8; i++) {
    r = static_cast<unsigned char>((r << 1) | (c & 1u));
    c = static_cast<unsigned char>(c >> 1);
  }
  return r;
}

// ---- Deleted owner: src/ints/mouse.cpp ----
// (Mouse_Drv/Mouse_Vertical DATA live in dos_globals.cpp)
void Mouse_AutoLock(bool enable) { (void)enable; }
bool MOUSE_IsLocked() { return false; }

// ---- Deleted owner: src/hardware/serialport/serialmouse.cpp ----
// (Serial_EventHandler DATA lives in dos_globals.cpp)

// ---- Deleted owner: src/dos/dos_keyboard_layout.cpp ----
uint16_t GetDefaultCP() { return 0; }
bool isSupportedCP(int cp) { (void)cp; return false; }
Bitu DOS_LoadKeyboardLayout(const char *layoutname, int32_t codepage,
                            const char *codepagefile) {
  (void)layoutname; (void)codepage; (void)codepagefile; return 0;
}
Bitu DOS_ChangeKeyboardLayout(const char *layoutname, int32_t codepage) {
  (void)layoutname; (void)codepage; return 0;
}
Bitu DOS_ChangeCodepage(int32_t codepage, const char *codepagefile) {
  (void)codepage; (void)codepagefile; return 0;
}
bool DOS_LayoutKey(Bitu key, uint8_t flags1, uint8_t flags2, uint8_t flags3) {
  (void)key; (void)flags1; (void)flags2; (void)flags3; return false;
}
const char *DOS_GetLoadedLayout() { return ""; }

// ---- Deleted owner: src/dos/dos_programs.cpp ----
// VENDORED (not stubbed): DOS_SetupPrograms/Add_VFiles/runMount/etc. are
// real. Only dos_ver_menu stays (menu-owned helper, still deleted).
void dos_ver_menu(bool start) { (void)start; }

// ---- Deleted owner: src/misc/programs.cpp (Load_Language defined by
// vendored programs.cpp) ----
std::string GetDOSBoxXPath(bool withexe) { (void)withexe; return std::string(); }
void SetGameState_Run(int value) { (void)value; }
void SaveGameState_Run() {}
size_t GetGameState_Run() { return 0; }
void update_dos_ems_menu() {}

// ---- Deleted owner: src/misc/savestates.cpp (payload helpers; the
// SaveState methods themselves live in dos_savestate_stubs.cpp) ----
void AddSaveStateMapper() {}

// ---- Deleted owner: src/misc/clipboard.cpp ----
bool SetClipboard(std::string value) { (void)value; return false; }
bool getClipboard() { return false; }

// ---- Deleted owner: src/shell/shell_cmds.cpp (declares only; defs above) ----

// ---- Deleted owner: src/output/output_ttf.cpp ----
void refreshExtChar() {}
void initcodepagefont() {}
bool TTF_using() { return false; }

// ---- Deleted owner: src/output/output_tools.cpp (declares only) ----

// ---- Deleted owner: src/hardware/keyboard.cpp (declares only) ----

// ---- Deleted owner: src/cpu/fpu.cpp ----
int8_t SaturateWordSToByteS(int16_t value) {
  if (value > 0x7F) return 0x7F;
  if (value < -0x80) return -0x80;
  return static_cast<int8_t>(value);
}
uint8_t SaturateWordSToByteU(int16_t value) {
  if (value > 0xFF) return 0xFF;
  if (value < 0) return 0;
  return static_cast<uint8_t>(value);
}
int16_t SaturateDwordSToWordS(int32_t value) {
  if (value > 0x7FFF) return 0x7FFF;
  if (value < -0x8000) return -0x8000;
  return static_cast<int16_t>(value);
}
uint16_t SaturateDwordSToWordU(int32_t value) {
  if (value > 0xFFFFu) return 0xFFFFu;
  if (value < 0) return 0;
  return static_cast<uint16_t>(value);
}
void setFPUTagEmpty() {}

// ---- Deleted owner: src/dos/drive_fat.cpp (declares only) ----
// fopen_lock is REAL (vendored dos_programs.cpp); no stub here.

// ---- Deleted owner: src/hardware/vga_draw.cpp (declares only) ----

// ---- Header-defined leaf classes: out-of-line methods owned by deleted TUs
// (signatures compiler-checked; each is a no-op because the owning backend
// is absent Phase 1 and no live Phase-1 path invokes them).

// 8255.h (owner: src/hardware/disney.cpp). Vendored keyboard.cpp defines
// PC98_*_8255 subclasses on top of this base; the PPI chips never exist
// Phase 1 (no disney object), so these never execute.
Intel8255::Intel8255() {}
Intel8255::~Intel8255() {}
void Intel8255::strobePortA() {}
void Intel8255::strobePortB() {}
uint8_t Intel8255::readByPort(const uint8_t p03) { (void)p03; return 0; }
void Intel8255::writePortA(const uint8_t data, uint8_t mask) {
  (void)data; (void)mask;
}
void Intel8255::writePortC(const uint8_t data, uint8_t mask) {
  (void)data; (void)mask;
}
void Intel8255::writeControl(const uint8_t data) { (void)data; }
void Intel8255::writeByPort(const uint8_t p03, const uint8_t data) {
  (void)p03; (void)data;
}
uint8_t Intel8255::inPortA() const { return 0; }
uint8_t Intel8255::inPortC() const { return 0; }
void Intel8255::outPortA(const uint8_t mask) { (void)mask; }
void Intel8255::outPortB(const uint8_t mask) { (void)mask; }
void Intel8255::outPortC(const uint8_t mask) { (void)mask; }
void Intel8255::sigINTR_A() {}
void Intel8255::sigINTR_B() {}

// serialport.h (owner: src/hardware/serialport/serialport.cpp). The
// serialports[] array is all-nullptr Phase 1 and every vendored call site
// null-checks before calling, so these never execute.
bool CSerial::Getchar(uint8_t *data, uint8_t *lsr, bool wait_dsr, Bitu timeout) {
  (void)data; (void)lsr; (void)wait_dsr; (void)timeout; return false;
}
bool CSerial::Putchar(uint8_t data, bool wait_dsr, bool wait_cts, Bitu timeout) {
  (void)data; (void)wait_dsr; (void)wait_cts; (void)timeout; return false;
}

// parport.h (owner: src/hardware/parport/parport.cpp). Same reasoning as
// CSerial: parallelPortObjects[] is all-nullptr and vendored bios.cpp
// null-checks before calling.
uint8_t CParallel::getPrinterStatus() { return 0; }
void CParallel::initialize() {}

// pc98_gdc.h (owner: src/hardware/vga_pc98_gdc.cpp). pc98_gdc[2] is
// zero-init in dos_globals.cpp; the GDC engine never steps Phase 1.
PC98_GDC_state::PC98_GDC_state() {}
bool PC98_GDC_state::write_fifo_param(const unsigned char c) {
  (void)c; return false;
}
bool PC98_GDC_state::write_fifo_command(const unsigned char c) {
  (void)c; return false;
}
void PC98_GDC_state::force_fifo_complete() {}
void PC98_GDC_state::cursor_advance() {}
void PC98_GDC_state::set_vectw(uint8_t ope, uint8_t dir, uint16_t dc,
                               uint16_t d, uint16_t d2, uint16_t d1,
                               uint16_t dm) {
  (void)ope; (void)dir; (void)dc; (void)d; (void)d2; (void)d1; (void)dm;
}
void PC98_GDC_state::exec(uint8_t command) { (void)command; }
void PC98_GDC_state::set_vectl(int x1, int y1, int x2, int y2) {
  (void)x1; (void)y1; (void)x2; (void)y2;
}
void PC98_GDC_state::set_mode(uint8_t mode) { (void)mode; }
void PC98_GDC_state::set_csrw(uint32_t ead, uint8_t dad) {
  (void)ead; (void)dad;
}
void PC98_GDC_state::set_textw(uint16_t pattern) { (void)pattern; }
void PC98_GDC_state::set_textw(uint8_t *tile, uint8_t len) {
  (void)tile; (void)len;
}
void PC98_GDC_state::begin_frame() {}
void PC98_GDC_state::next_line() {}

// shiftjis.h (owner: src/ints/int_dosv.cpp). DBCS decode never runs Phase 1.
ShiftJISDecoder::ShiftJISDecoder() {}
bool ShiftJISDecoder::take(unsigned char c) { (void)c; return false; }

// regionalloctracking.h (owner: src/misc/regionalloctracking.cpp).
// VENDORED (not stubbed): CALLBACK_Init needs working ROMBIOS_GetMemory at
// boot ("Callback area spans 64KB segment" otherwise). The real TU is
// small (302 lines) and header-deps-only. Nothing stubbed here.
