//
// dos_phase1_link2.cpp
//
// BMX-owned link stubs, part 3: second-round undefined symbols from the
// on-target ARM link (aarch64-none-elf-ld) of the 72-file subset.
//
// The macOS host-link list (UNDEF_PHASE1.txt) under-approximated the true
// surface: newlib lacks several POSIX pieces glibc provides, and a few
// GUI/drive backends are referenced from more sites than the host link
// exposed. Everything here is inert Phase-1 no-op mirroring the upstream
// "backend absent" path. Drive-class ctors for shell-supported formats
// (ISO/overlay/physfs) report error through the caller's error flag and
// return: E_Exit would halt the machine (exit(1)) with no F12 recovery.
// Image-format ctors (D88/NFD/Teledisk/VFD/INT13/MSDOS-block/QCOW2) still
// E_Exit: IMGMOUNT has no error channel for them (Phase-2+ work).
//

#include "dosbox.h"
#include "setup.h"
#include "video.h"
#include "menu.h"
#include "menudef.h"
#include "mapper.h"
#include "sdlmain.h"
#include "drives.h"
#include "dos_inc.h"
#include "bios_disk.h"
#include "imagedisk_d88.h"
#include "imagedisk_nfd.h"
#include "imagedisk_teledisk.h"
#include "imagedisk_vfd.h"
#include "imagedisk_int13.h"
#include "imagedisk_msdosblockdev.h"
#include "qcow2_disk.h"
#include "joystick.h"
#include "pci_bus.h"
#include "pc98_gdc.h"
#include "8255.h"

#include <cerrno>
#include <cstring>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// ---- POSIX pieces absent from newlib ----------------------------
extern "C" {
int chmod(const char *path, mode_t mode) {
  (void)path; (void)mode;
  errno = ENOSYS;
  return -1;
}
int ftruncate(int fd, off_t length) {
  (void)fd; (void)length;
  errno = ENOSYS;
  return -1;
}
int futimens(int fd, const struct timespec times[2]) {
  (void)fd; (void)times;
  errno = ENOSYS;
  return -1;
}
int gethostname(char *name, size_t len) {
  if (name != nullptr && len > 0) name[0] = '\0';
  errno = ENOSYS;
  return -1;
}
// newlib fcntl() gateway: OS must provide _fcntl (drive_local lock probes).
int _fcntl(int fd, int cmd, int arg) {
  (void)fd; (void)cmd; (void)arg;
  errno = ENOSYS;
  return -1;
}
// (emux_atari_cart_types_for_kb / emux_atari_set_cart_type live in
// dos_bridge.cpp so minimal (vendored-free) kernels link too.)
} // extern "C"

// ---- Deleted owner: src/gui/sdlmain.cpp + outputs ----------------
// NOTE: GFX_Events is REAL (src/dos/dos_input.cpp): it drains the
// BMX key queue into the guest and pumps the F12 menu toggle. It runs in
// every Normal_Loop idle slice INCLUDING nested ones (SHELL_Run blocks
// inside the first boot slice), so input/menu stay alive after DOS boots.
void GFX_LosingFocus() {}
bool GFX_IsFullscreen() { return false; }
bool GFX_GetPreventFullscreen() { return true; }
void GFX_ForceRedrawScreen() {}
void GFX_ReleaseMouse() {}
void GFX_SwitchFullScreen() {}
void CheckAnotherDisplayDriver() {}
void DOSBox_SetMenu() {}
void DOSBox_NoMenu() {}
void toOutput(const char *what) { (void)what; }
void SetWindowTransparency(int trans) { (void)trans; }
bool systemmessagebox(char const *aTitle, char const *aMessage,
                       char const *aDialogType, char const *aIconType,
                       int aDefaultButton) {
  (void)aTitle; (void)aMessage; (void)aDialogType; (void)aIconType;
  (void)aDefaultButton;
  return false;
}
int GetNumScreen() { return 1; }
std::string GetDefaultOutput() { return std::string("surface"); }
void GetDrawWidthHeight(unsigned int *pdrawWidth, unsigned int *pdrawHeight) {
  if (pdrawWidth != nullptr) *pdrawWidth = 0;
  if (pdrawHeight != nullptr) *pdrawHeight = 0;
}
void GetMaxWidthHeight(unsigned int *pmaxWidth, unsigned int *pmaxHeight) {
  if (pmaxWidth != nullptr) *pmaxWidth = 0;
  if (pmaxHeight != nullptr) *pmaxHeight = 0;
}
void SetDisplayNumber(int display) { (void)display; }

// ---- Deleted owner: src/gui/sdl_mapper.cpp -----------------------
void ReloadMapper(Section_prop *sec, bool init) {
  (void)sec; (void)init;
}
void MAPPER_Init() {}
void MAPPER_Run(bool pressed) { (void)pressed; }
void GUI_Run(bool pressed) { (void)pressed; }
void setScanCode(Section_prop *section) { (void)section; }
void loadScanCode() {}
void setAspectRatio(Section_prop *section) { (void)section; }

// ---- Deleted owner: src/gui/menu.cpp (displaylist remainder) -----
DOSBoxMenu::displaylist::displaylist() {}
DOSBoxMenu::displaylist::~displaylist() {}
const DOSBoxMenu::mapper_event_t DOSBoxMenu::unassigned_mapper_event;

// ---- Deleted owner: various (printer/serial/reboot/TTF) ----------
// NOTE: OPL_SaveRawEvent is REAL (vendored adlib.cpp); not stubbed.
void runSerial(const char *str) { (void)str; }
void runParallel(const char *str) { (void)str; }
void RebootConfig(std::string filename, bool confirm) {
  (void)filename; (void)confirm;
}
void RebootLanguage(std::string filename, bool confirm) {
  (void)filename; (void)confirm;
}
void clearFontCache() {}
float JOYSTICK_GetMove_X(Bitu which) { (void)which; return 0.0f; }
float JOYSTICK_GetMove_Y(Bitu which) { (void)which; return 0.0f; }
bool JOYSTICK_IsEnabled(Bitu which) { (void)which; return false; }
bool has_pcibus_enable() { return false; }
void gdc_5mhz_mode_update_vars() {}
uint8_t Intel8255::inPortB() const { return 0; }

// ---- Deleted owner: PC98 backend (vga_pc98_*, pc98*.cpp) ----------
Bitu pc98_a1_read(Bitu port, Bitu iolen) {
  (void)port; (void)iolen; return 0;
}
void pc98_a1_write(Bitu port, Bitu val, Bitu iolen) {
  (void)port; (void)val; (void)iolen;
}
// PC98_BIOS_Bank_Switch_Reset lives in vendored dos_programs.cpp now.
Bitu pc98_crtc_read(Bitu port, Bitu iolen) {
  (void)port; (void)iolen; return 0;
}
void pc98_crtc_write(Bitu port, Bitu val, Bitu iolen) {
  (void)port; (void)val; (void)iolen;
}
Bitu pc98_egc4a0_read(Bitu port, Bitu iolen) {
  (void)port; (void)iolen; return 0;
}
Bitu pc98_egc4a0_read_warning(Bitu port, Bitu iolen) {
  (void)port; (void)iolen; return 0;
}
void pc98_egc4a0_write(Bitu port, Bitu val, Bitu iolen) {
  (void)port; (void)val; (void)iolen;
}
void pc98_egc4a0_write_warning(Bitu port, Bitu val, Bitu iolen) {
  (void)port; (void)val; (void)iolen;
}
Bitu pc98_gdc_read(Bitu port, Bitu iolen) {
  (void)port; (void)iolen; return 0;
}
void pc98_gdc_write(Bitu port, Bitu val, Bitu iolen) {
  (void)port; (void)val; (void)iolen;
}
void pc98_port6A_command_write(unsigned char b) { (void)b; }
Bitu pc98_read_9a0(Bitu port, Bitu iolen) {
  (void)port; (void)iolen; return 0;
}
void pc98_write_9a0(Bitu port, Bitu val, Bitu iolen) {
  (void)port; (void)val; (void)iolen;
}
Bitu pc98_read_9a8(Bitu port, Bitu iolen) {
  (void)port; (void)iolen; return 0;
}
void pc98_write_9a8(Bitu port, Bitu val, Bitu iolen) {
  (void)port; (void)val; (void)iolen;
}
Bitu pc98_read_artic(Bitu port, Bitu iolen) {
  (void)port; (void)iolen; return 0;
}
void PC98_Set24KHz() {}
void PC98_Set31KHz() {}
void PC98_Set31KHz_480line() {}
void pc98_set_digpal_entry(unsigned char ent, unsigned char grb) {
  (void)ent; (void)grb;
}
void pc98_update_palette() {}
void pc98_wait_write(Bitu port, Bitu val, Bitu iolen) {
  (void)port; (void)val; (void)iolen;
}
void PC98_show_cursor(bool show) { (void)show; }

// ---- Deleted owner: capture file writers (aviwriter/*.cpp) --------
struct avi_writer;
struct avi_writer_stream;
struct riff_wav_writer;
struct windows_WAVEFORMAT;
avi_writer *avi_writer_create() { return nullptr; }
void avi_writer_destroy(avi_writer *w) { (void)w; }
void avi_writer_open_file(avi_writer *w, const char *name) {
  (void)w; (void)name;
}
void avi_writer_close_file(avi_writer *w) { (void)w; }
void avi_writer_begin_header(avi_writer *w) { (void)w; }
void avi_writer_main_header(avi_writer *w) { (void)w; }
avi_writer_stream *avi_writer_new_stream(avi_writer *w) {
  (void)w; return nullptr;
}
void avi_writer_stream_header(avi_writer_stream *s) { (void)s; }
void avi_writer_stream_set_format(avi_writer_stream *s, void *fmt,
                                  unsigned long len) {
  (void)s; (void)fmt; (void)len;
}
void avi_writer_set_stream_writing(avi_writer *w) { (void)w; }
void avi_writer_begin_data(avi_writer *w) { (void)w; }
void avi_writer_stream_write(avi_writer *w, avi_writer_stream *s, void *buf,
                             unsigned long len, unsigned int flags) {
  (void)w; (void)s; (void)buf; (void)len; (void)flags;
}
void avi_writer_end_data(avi_writer *w) { (void)w; }
void avi_writer_finish(avi_writer *w) { (void)w; }
riff_wav_writer *riff_wav_writer_create() { return nullptr; }
void riff_wav_writer_destroy(riff_wav_writer *w) { (void)w; }
void riff_wav_writer_open_file(riff_wav_writer *w, const char *name) {
  (void)w; (void)name;
}
void riff_wav_writer_begin_header(riff_wav_writer *w) { (void)w; }
void riff_wav_writer_set_format(riff_wav_writer *w, windows_WAVEFORMAT *fmt) {
  (void)w; (void)fmt;
}
void riff_wav_writer_begin_data(riff_wav_writer *w) { (void)w; }
void riff_wav_writer_data_write(riff_wav_writer *w, void *buf,
                                unsigned long len) {
  (void)w; (void)buf; (void)len;
}
void riff_wav_writer_end_data(riff_wav_writer *w) { (void)w; }

// ---- Deleted owner: floppy image backends (imagedisk_*.cpp) -------
static Int13Status no_disk() { return Int13Status::DriveNotReady; }

imageDiskD88::imageDiskD88(FILE *imgFile, const char *imgName,
                           uint32_t imgSizeK, bool isHardDisk)
    : imageDisk(ID_D88) {
  (void)imgFile; (void)imgName; (void)imgSizeK; (void)isHardDisk;
  E_Exit("BMX Phase 1: D88 images not supported");
}
imageDiskD88::~imageDiskD88() {}
Int13Status imageDiskD88::Read_Sector(uint32_t h, uint32_t c, uint32_t s,
                                      void *d, unsigned int r) {
  (void)h; (void)c; (void)s; (void)d; (void)r; return no_disk();
}
Int13Status imageDiskD88::Write_Sector(uint32_t h, uint32_t c, uint32_t s,
                                       const void *d, unsigned int r) {
  (void)h; (void)c; (void)s; (void)d; (void)r; return no_disk();
}
Int13Status imageDiskD88::Read_AbsoluteSector(uint32_t n, void *d) {
  (void)n; (void)d; return no_disk();
}
Int13Status imageDiskD88::Write_AbsoluteSector(uint32_t n, const void *d) {
  (void)n; (void)d; return no_disk();
}

imageDiskNFD::imageDiskNFD(FILE *imgFile, const char *imgName,
                           uint32_t imgSizeK, bool isHardDisk,
                           unsigned int revision)
    : imageDisk(ID_NFD) {
  (void)imgFile; (void)imgName; (void)imgSizeK; (void)isHardDisk;
  (void)revision;
  E_Exit("BMX Phase 1: NFD images not supported");
}
imageDiskNFD::~imageDiskNFD() {}
Int13Status imageDiskNFD::Read_Sector(uint32_t h, uint32_t c, uint32_t s,
                                      void *d, unsigned int r) {
  (void)h; (void)c; (void)s; (void)d; (void)r; return no_disk();
}
Int13Status imageDiskNFD::Write_Sector(uint32_t h, uint32_t c, uint32_t s,
                                       const void *d, unsigned int r) {
  (void)h; (void)c; (void)s; (void)d; (void)r; return no_disk();
}
Int13Status imageDiskNFD::Read_AbsoluteSector(uint32_t n, void *d) {
  (void)n; (void)d; return no_disk();
}
Int13Status imageDiskNFD::Write_AbsoluteSector(uint32_t n, const void *d) {
  (void)n; (void)d; return no_disk();
}

imageDiskTeledisk::imageDiskTeledisk(FILE *imgFile, const char *imgName,
                                     uint32_t imgSizeK, bool isHardDisk)
    : imageDisk(ID_TELEDISK) {
  (void)imgFile; (void)imgName; (void)imgSizeK; (void)isHardDisk;
  E_Exit("BMX Phase 1: Teledisk images not supported");
}
imageDiskTeledisk::~imageDiskTeledisk() {}
Int13Status imageDiskTeledisk::Read_Sector(uint32_t h, uint32_t c, uint32_t s,
                                           void *d, unsigned int r) {
  (void)h; (void)c; (void)s; (void)d; (void)r; return no_disk();
}
Int13Status imageDiskTeledisk::Write_Sector(uint32_t h, uint32_t c, uint32_t s,
                                            const void *d, unsigned int r) {
  (void)h; (void)c; (void)s; (void)d; (void)r; return no_disk();
}
Int13Status imageDiskTeledisk::Read_AbsoluteSector(uint32_t n, void *d) {
  (void)n; (void)d; return no_disk();
}
Int13Status imageDiskTeledisk::Write_AbsoluteSector(uint32_t n,
                                                    const void *d) {
  (void)n; (void)d; return no_disk();
}

imageDiskVFD::imageDiskVFD(FILE *imgFile, const char *imgName,
                           uint32_t imgSizeK, bool isHardDisk)
    : imageDisk(ID_VFD) {
  (void)imgFile; (void)imgName; (void)imgSizeK; (void)isHardDisk;
  E_Exit("BMX Phase 1: VFD images not supported");
}
imageDiskVFD::~imageDiskVFD() {}
Int13Status imageDiskVFD::Read_Sector(uint32_t h, uint32_t c, uint32_t s,
                                      void *d, unsigned int r) {
  (void)h; (void)c; (void)s; (void)d; (void)r; return no_disk();
}
Int13Status imageDiskVFD::Write_Sector(uint32_t h, uint32_t c, uint32_t s,
                                       const void *d, unsigned int r) {
  (void)h; (void)c; (void)s; (void)d; (void)r; return no_disk();
}
Int13Status imageDiskVFD::Read_AbsoluteSector(uint32_t n, void *d) {
  (void)n; (void)d; return no_disk();
}
Int13Status imageDiskVFD::Write_AbsoluteSector(uint32_t n, const void *d) {
  (void)n; (void)d; return no_disk();
}

imageDiskINT13Drive::imageDiskINT13Drive(imageDisk *sdisk)
    : imageDisk(ID_INT13) {
  (void)sdisk;
  E_Exit("BMX Phase 1: INT13 drive wrapping not supported");
}
imageDiskINT13Drive::~imageDiskINT13Drive() {}
Int13Status imageDiskINT13Drive::Read_Sector(uint32_t h, uint32_t c,
                                             uint32_t s, void *d,
                                             unsigned int r) {
  (void)h; (void)c; (void)s; (void)d; (void)r; return no_disk();
}
Int13Status imageDiskINT13Drive::Write_Sector(uint32_t h, uint32_t c,
                                              uint32_t s, const void *d,
                                              unsigned int r) {
  (void)h; (void)c; (void)s; (void)d; (void)r; return no_disk();
}
Int13Status imageDiskINT13Drive::Read_AbsoluteSector(uint32_t n, void *d) {
  (void)n; (void)d; return no_disk();
}
Int13Status imageDiskINT13Drive::Write_AbsoluteSector(uint32_t n,
                                                      const void *d) {
  (void)n; (void)d; return no_disk();
}
void imageDiskINT13Drive::UpdateFloppyType() {}
void imageDiskINT13Drive::Set_Reserved_Cylinders(Bitu resCyl) {
  (void)resCyl;
}
uint32_t imageDiskINT13Drive::Get_Reserved_Cylinders() { return 0; }
void imageDiskINT13Drive::Set_Geometry(uint32_t h, uint32_t c, uint32_t s,
                                       uint32_t ss) {
  (void)h; (void)c; (void)s; (void)ss;
}
void imageDiskINT13Drive::Get_Geometry(uint32_t *h, uint32_t *c, uint32_t *s,
                                       uint32_t *ss) {
  if (h != nullptr) *h = 0;
  if (c != nullptr) *c = 0;
  if (s != nullptr) *s = 0;
  if (ss != nullptr) *ss = 0;
}
uint8_t imageDiskINT13Drive::GetBiosType(void) { return 0; }
uint32_t imageDiskINT13Drive::getSectSize(void) { return 0; }
bool imageDiskINT13Drive::detectDiskChange(void) { return false; }

imageDiskMSDOSBlockDevice::imageDiskMSDOSBlockDevice()
    : imageDisk(ID_MSDOSBLOCKDEV) {
  E_Exit("BMX Phase 1: MSDOS block devices not supported");
}
imageDiskMSDOSBlockDevice::~imageDiskMSDOSBlockDevice() {}
Int13Status imageDiskMSDOSBlockDevice::Read_AbsoluteSector(uint32_t n,
                                                           void *d) {
  (void)n; (void)d; return no_disk();
}
Int13Status imageDiskMSDOSBlockDevice::Write_AbsoluteSector(uint32_t n,
                                                            const void *d) {
  (void)n; (void)d; return no_disk();
}
bool imageDiskMSDOSBlockDevice::detectDiskChange(void) { return false; }

// ---- Deleted owner: RAM disk (bios_memdisk.cpp) -------------------
// VENDORED (not stubbed): CreateRamDrive needs working imageDiskMemory.

// ---- Deleted owner: VHD (bios_vhd.cpp) ----------------------------
imageDiskVHD::~imageDiskVHD() {}
Int13Status imageDiskVHD::Read_AbsoluteSector(uint32_t n, void *d) {
  (void)n; (void)d; return no_disk();
}
Int13Status imageDiskVHD::Write_AbsoluteSector(uint32_t n, const void *d) {
  (void)n; (void)d; return no_disk();
}
imageDiskVHD::ErrorCodes imageDiskVHD::Open(const char *fileName,
                                            const bool readOnly,
                                            imageDisk **disk) {
  (void)fileName; (void)readOnly;
  if (disk != nullptr) *disk = nullptr;
  return ERROR_OPENING;
}
// Remaining VHD helpers (bios_vhd.cpp): statics + footer bits. VHD images
// report failure gracefully; fixed/dynamic creation unsupported on BMX.
uint32_t imageDiskVHD::ConvertFixed(const char *filename) {
  (void)filename;
  return ERROR_OPENING;
}
uint32_t imageDiskVHD::CreateDynamic(const char *filename, uint64_t size) {
  (void)filename; (void)size;
  return ERROR_OPENING;
}
uint32_t imageDiskVHD::CreateDifferencing(const char *filename,
                                          const char *basename) {
  (void)filename; (void)basename;
  return ERROR_OPENING;
}
uint32_t imageDiskVHD::GetInfo(const char *filename, VHDInfo **info) {
  (void)filename;
  if (info != nullptr) *info = nullptr;
  return ERROR_OPENING;
}
uint32_t imageDiskVHD::GetInfo(VHDInfo *info) {
  (void)info;
  return ERROR_OPENING;
}
imageDiskVHD::VHDTypes imageDiskVHD::GetVHDType(void) const {
  return VHD_TYPE_NONE;
}
bool imageDiskVHD::MergeSnapshot(uint32_t *merged, uint32_t *updated) {
  (void)merged; (void)updated;
  return false;
}
void imageDiskVHD::mk_uuid(uint8_t *buf) {
  if (buf != nullptr) {
    for (int i = 0; i < 16; i++) buf[i] = 0;
  }
}
void imageDiskVHD::VHDFooter::SwapByteOrder() {}
uint32_t imageDiskVHD::VHDFooter::CalculateChecksum() { return 0; }
void imageDiskVHD::VHDFooter::SetDefaults() {}

// ---- Deleted owner: QCOW2 (qcow2_disk.cpp) ------------------------
const uint32_t QCow2Image::magic = 0x514649fb;
QCow2Image::QCow2Header QCow2Image::read_header(FILE *qcow2File) {
  (void)qcow2File;
  QCow2Header header = {};
  return header;
}
QCow2Image::QCow2Image(QCow2Header &hdr, FILE *file, const char *name,
                       uint32_t sectorSizeBytes)
    : header(hdr), sector_size(sectorSizeBytes) {
  (void)file; (void)name;
  this->file = nullptr;
  backing_image = nullptr;
  E_Exit("BMX Phase 1: QCOW2 images not supported");
}
QCow2Image::~QCow2Image() {}
uint8_t QCow2Image::read_sector(uint32_t sectnum, uint8_t *data) {
  (void)sectnum; (void)data;
  return 1;
}
QCow2Disk::QCow2Disk(QCow2Image::QCow2Header &hdr, FILE *file,
                     const char *imgName, uint64_t imgSize,
                     uint32_t sectorSizeBytes, bool isHardDisk)
    : imageDisk(file, imgName, imgSize, isHardDisk),
      qcowImage(hdr, file, imgName, sectorSizeBytes) {}
QCow2Disk::~QCow2Disk() {}
Int13Status QCow2Disk::Read_AbsoluteSector(uint32_t n, void *d) {
  (void)n; (void)d; return no_disk();
}
Int13Status QCow2Disk::Write_AbsoluteSector(uint32_t n, const void *d) {
  (void)n; (void)d; return no_disk();
}

// ---- Deleted owner: ISO / overlay / physfs drives ----------------
// NOTE: these ctors must NEVER E_Exit. E_Exit ends in exit(1), i.e. a
// silent bare-metal halt (no F12, no menu). All three callers check the
// error flag and return to the shell with a message, so report failure
// through it instead.
isoDrive::isoDrive(char driveLetter, const char *fileName, uint8_t mediaid,
                   int &error, std::vector<std::string> &options) {
  (void)driveLetter; (void)fileName; (void)mediaid; (void)options;
  error = 1;
}
isoDrive::~isoDrive() {}
bool isoDrive::FileOpen(DOS_File **f, const char *n, uint32_t fl) {
  (void)f; (void)n; (void)fl; return false;
}
bool isoDrive::FileCreate(DOS_File **f, const char *n, uint16_t a) {
  (void)f; (void)n; (void)a; return false;
}
bool isoDrive::FileUnlink(const char *n) { (void)n; return false; }
bool isoDrive::RemoveDir(const char *d) { (void)d; return false; }
bool isoDrive::MakeDir(const char *d) { (void)d; return false; }
bool isoDrive::TestDir(const char *d) { (void)d; return false; }
bool isoDrive::FindFirst(const char *d, DOS_DTA &dta, bool fcb) {
  (void)d; (void)dta; (void)fcb; return false;
}
bool isoDrive::FindNext(DOS_DTA &dta) { (void)dta; return false; }
bool isoDrive::SetFileAttr(const char *n, uint16_t a) {
  (void)n; (void)a; return false;
}
bool isoDrive::GetFileAttr(const char *n, uint16_t *a) {
  (void)n; (void)a; return false;
}
bool isoDrive::GetFileAttrEx(char *n, struct stat *s) {
  (void)n; (void)s; return false;
}
unsigned long isoDrive::GetCompressedSize(char *n) { (void)n; return 0; }
bool isoDrive::Rename(const char *o, const char *n) {
  (void)o; (void)n; return false;
}
bool isoDrive::AllocationInfo(uint16_t *b, uint8_t *s, uint16_t *t,
                              uint16_t *f) {
  (void)b; (void)s; (void)t; (void)f; return false;
}
bool isoDrive::FileExists(const char *n) { (void)n; return false; }
bool isoDrive::FileStat(const char *n, FileStat_Block *const s) {
  (void)n; (void)s; return false;
}
uint8_t isoDrive::GetMediaByte(void) { return 0; }
void isoDrive::EmptyCache(void) {}
void isoDrive::MediaChange(void) {}
void isoDrive::MediaChangeImmediate() {}
bool isoDrive::isRemote(void) { return false; }
bool isoDrive::isRemovable(void) { return true; }
Bits isoDrive::UnMount(void) { return 0; }
bool isoDrive::loadImage() { return false; }

Overlay_Drive::Overlay_Drive(const char *startdir, const char *overlay,
                             uint16_t bs, uint8_t sc, uint16_t tc, uint16_t fc,
                             uint8_t mediaid, uint8_t &error,
                             std::vector<std::string> &options)
    : localDrive(startdir, bs, sc, tc, fc, mediaid, options),
      special_prefix("") {
  (void)overlay;
  // error==1 prints MIXED_BASE, ==2 SAME_AS_BASE; anything else prints the
  // generic OVERLAY_ERROR, which is the honest message here.
  error = 3;
}
// (Implicit virtual dtor: defined inline by the compiler.)
bool Overlay_Drive::FileOpen(DOS_File **f, const char *n, uint32_t fl) {
  (void)f; (void)n; (void)fl; return false;
}
bool Overlay_Drive::FileCreate(DOS_File **f, const char *n, uint16_t a) {
  (void)f; (void)n; (void)a; return false;
}
bool Overlay_Drive::FindFirst(const char *d, DOS_DTA &dta, bool fcb) {
  (void)d; (void)dta; (void)fcb; return false;
}
bool Overlay_Drive::FindNext(DOS_DTA &dta) { (void)dta; return false; }
bool Overlay_Drive::FileUnlink(const char *n) { (void)n; return false; }
bool Overlay_Drive::SetFileAttr(const char *n, uint16_t a) {
  (void)n; (void)a; return false;
}
bool Overlay_Drive::GetFileAttr(const char *n, uint16_t *a) {
  (void)n; (void)a; return false;
}
std::string Overlay_Drive::GetHostName(const char *name) {
  (void)name; return std::string();
}
bool Overlay_Drive::FileExists(const char *n) { (void)n; return false; }
bool Overlay_Drive::Rename(const char *o, const char *n) {
  (void)o; (void)n; return false;
}
bool Overlay_Drive::FileStat(const char *n, FileStat_Block *const s) {
  (void)n; (void)s; return false;
}
void Overlay_Drive::EmptyCache(void) {}
FILE *Overlay_Drive::create_file_in_overlay(const char *n, char const *m) {
  (void)n; (void)m; return nullptr;
}
Bits Overlay_Drive::UnMount(void) { return 0; }
bool Overlay_Drive::TestDir(const char *d) { (void)d; return false; }
bool Overlay_Drive::RemoveDir(const char *d) { (void)d; return false; }
bool Overlay_Drive::MakeDir(const char *d) { (void)d; return false; }

physfsDrive::physfsDrive(const char driveLetter, const char *startdir,
                         uint16_t bs, uint8_t sc, uint16_t tc, uint16_t fc,
                         uint8_t mediaid, int &error,
                         std::vector<std::string> &options)
    : localDrive(startdir, bs, sc, tc, fc, mediaid, options),
      driveLetter(driveLetter) {
  // Never E_Exit here (see note above): MOUNT checks error and prints
  // PROGRAM_MOUNT_PHYSFS_ERROR, returning to the shell.
  error = 1;
}
physfsDrive::~physfsDrive() {}
bool physfsDrive::FileOpen(DOS_File **f, const char *n, uint32_t fl) {
  (void)f; (void)n; (void)fl; return false;
}
bool physfsDrive::FileCreate(DOS_File **f, const char *n, uint16_t a) {
  (void)f; (void)n; (void)a; return false;
}
bool physfsDrive::FileUnlink(const char *n) { (void)n; return false; }
bool physfsDrive::RemoveDir(const char *d) { (void)d; return false; }
bool physfsDrive::MakeDir(const char *d) { (void)d; return false; }
bool physfsDrive::TestDir(const char *d) { (void)d; return false; }
bool physfsDrive::FindFirst(const char *d, DOS_DTA &dta, bool fcb) {
  (void)d; (void)dta; (void)fcb; return false;
}
bool physfsDrive::FindNext(DOS_DTA &dta) { (void)dta; return false; }
bool physfsDrive::Rename(const char *o, const char *n) {
  (void)o; (void)n; return false;
}
bool physfsDrive::SetFileAttr(const char *n, uint16_t a) {
  (void)n; (void)a; return false;
}
bool physfsDrive::GetFileAttr(const char *n, uint16_t *a) {
  (void)n; (void)a; return false;
}
bool physfsDrive::GetFileAttrEx(char *n, struct stat *s) {
  (void)n; (void)s; return false;
}
unsigned long physfsDrive::GetCompressedSize(char *n) {
  (void)n; return 0;
}
bool physfsDrive::AllocationInfo(uint16_t *b, uint8_t *s, uint16_t *t,
                                uint16_t *f) {
  (void)b; (void)s; (void)t; (void)f; return false;
}
bool physfsDrive::FileExists(const char *n) { (void)n; return false; }
bool physfsDrive::FileStat(const char *n, FileStat_Block *const s) {
  (void)n; (void)s; return false;
}
bool physfsDrive::isRemote(void) { return false; }
bool physfsDrive::isRemovable(void) { return false; }

physfscdromDrive::physfscdromDrive(const char driveLetter, const char *startdir,
                                   uint16_t bs, uint8_t sc, uint16_t tc,
                                   uint16_t fc, uint8_t mediaid, int &error,
                                   std::vector<std::string> &options)
    : physfsDrive(driveLetter, startdir, bs, sc, tc, fc, mediaid, error,
                  options) {}
// (Implicit virtual dtor: defined inline by the compiler.)
bool physfscdromDrive::FileOpen(DOS_File **f, const char *n, uint32_t fl) {
  (void)f; (void)n; (void)fl; return false;
}
bool physfscdromDrive::FileCreate(DOS_File **f, const char *n, uint16_t a) {
  (void)f; (void)n; (void)a; return false;
}
bool physfscdromDrive::FileUnlink(const char *n) { (void)n; return false; }
bool physfscdromDrive::RemoveDir(const char *d) { (void)d; return false; }
bool physfscdromDrive::MakeDir(const char *d) { (void)d; return false; }
bool physfscdromDrive::Rename(const char *o, const char *n) {
  (void)o; (void)n; return false;
}
bool physfscdromDrive::GetFileAttr(const char *n, uint16_t *a) {
  (void)n; (void)a; return false;
}
bool physfscdromDrive::FindFirst(const char *d, DOS_DTA &dta, bool fcb) {
  (void)d; (void)dta; (void)fcb; return false;
}
void physfscdromDrive::SetDir(const char *path) { (void)path; }
bool physfscdromDrive::isRemote(void) { return false; }
bool physfscdromDrive::isRemovable(void) { return true; }
Bits physfscdromDrive::UnMount(void) { return 0; }
const char *physfscdromDrive::GetInfo(void) { return ""; }

// ---- Round 3: vtable-driven follow-ons ---------------------------
// Referenced (directly or via vtables/typeinfo) by vendored MOUNT/IMGMOUNT
// paths; all inert in Phase 1.
bool imageDiskVHD::loadBlock(const uint32_t blockNumber) {
  (void)blockNumber; return false;
}
void isoDrive::Activate(void) {}
bool Overlay_Drive::add_special_file_to_disk(const char *dosname,
                                             const char *operation,
                                             uint16_t value, bool isdir) {
  (void)dosname; (void)operation; (void)value; (void)isdir; return false;
}
void Overlay_Drive::remove_special_file_from_disk(const char *dosname,
                                                  const char *operation) {
  (void)dosname; (void)operation;
}
std::string Overlay_Drive::create_filename_of_special_operation(
    const char *dosname, const char *operation, bool expand) {
  (void)dosname; (void)operation; (void)expand; return std::string();
}
void *physfsDrive::opendir(const char *name) { (void)name; return nullptr; }
void physfsDrive::closedir(void *handle) { (void)handle; }
bool physfsDrive::read_directory_first(void *dirp, char *entry_name,
                                       char *entry_sname, bool &is_directory) {
  (void)dirp; (void)entry_name; (void)entry_sname; (void)is_directory;
  return false;
}
bool physfsDrive::read_directory_next(void *dirp, char *entry_name,
                                      char *entry_sname, bool &is_directory) {
  (void)dirp; (void)entry_name; (void)entry_sname; (void)is_directory;
  return false;
}
const char *physfsDrive::getOverlaydir(void) { return ""; }
bool physfsDrive::setOverlaydir(const char *name) {
  (void)name; return false;
}
const char *physfsDrive::GetInfo(void) { return ""; }
Bits physfsDrive::UnMount(void) { return 0; }
UDFextents::UDFextents() {}
UDFFileSetDescriptor::UDFFileSetDescriptor() {}
UDFLogicalVolumeDescriptor::UDFLogicalVolumeDescriptor() {}
UDFPartitionDescriptor::UDFPartitionDescriptor() {}
UDFPrimaryVolumeDescriptor::UDFPrimaryVolumeDescriptor() {}
// (UDF member structs: implicit-member default ctors referenced above.)
UDFcharspec::UDFcharspec() {}
UDFdstring::UDFdstring() {}
UDFextent_ad::UDFextent_ad() {}
UDFlong_ad::UDFlong_ad() {}
UDFregid::UDFregid() {}
UDFTagId::UDFTagId() {}
UDFtimestamp::UDFtimestamp() {}
UDFlb_addr::UDFlb_addr() {}
