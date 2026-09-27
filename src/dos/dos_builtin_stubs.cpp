//
// dos_builtin_stubs.cpp
//
// Empty builtin-binary blobs (upstream src/builtin/*.cpp, ~22MB sources).
// The C++ Z: programs (MOUNT/INTRO/BOOT/COMMAND/...) come from vendored
// dos_programs.cpp via PROGRAMS_MakeFile and are fully live. The binary
// blobs (DEBUG.EXE, FORMAT.EXE, 4DOS.COM, ...) are empty here and SKIPPED
// by VFILE_RegisterBuiltinFileBlob (vendored drive_virtual.cpp guard:
// empty name or null+0 length returns before VFILE_Register, which would
// otherwise throw via std::string(NULL) from VFILE_Generate_SFN), so Z:
// stays clean and DIR works. Full binaries are Phase-3 (disk space +
// demand).
//

#include "dos_inc.h"

struct BuiltinFileBlob bfb_25_COM = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_25_COM_ega = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_25_COM_other = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_28_COM = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_28_COM_ega = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_4DOS_COM = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_4DOS_HLP = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_4HELP_EXE = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_50_COM = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_APPEND_EXE = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_BATCOMP_EXE = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_BUFFERS_COM = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_CDPLAY_EXE = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_CGA_COM = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_CHKDSK_EXE = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_CLR_COM = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_COMP_COM = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_CWSDPMI_EXE = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_CWSDPMI_PC98_EXE = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_DEBUG_EXE = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_DEFRAG_EXE = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_DEVICE_COM = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_DISKCOPY_EXE = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_DOS32A_EXE = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_DOS4GW_EXE = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_DOSIDLE_EXE = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_DOSMID_EXE = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_DSXMENU_EXE_PC = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_DSXMENU_EXE_PC98 = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_EDIT_COM = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_EDLIN_EXE = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_EGA10_CPX = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_EGA11_CPX = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_EGA12_CPX = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_EGA13_CPX = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_EGA14_CPX = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_EGA15_CPX = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_EGA16_CPX = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_EGA17_CPX = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_EGA18_CPI = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_EGA2_CPX = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_EGA3_CPX = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_EGA4_CPX = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_EGA5_CPX = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_EGA6_CPX = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_EGA7_CPX = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_EGA8_CPX = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_EGA9_CPX = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_EGA_COM = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_EGA_CPX = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_EMSMAGIC_COM = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_EVAL_EXE = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_EVAL_HLP = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_EXAMPLES_BTM = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_FCBS_COM = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_FC_EXE = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_FDISK_EXE = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_FILES_COM = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_FIND_EXE = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_FORMAT_EXE = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_GLIDE2X_OVL = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_HEXMEM16_EXE = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_HEXMEM32_EXE = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_KEYBOARD_SYS = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_KEYBRD2_SYS = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_KEYBRD3_SYS = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_KEYBRD4_SYS = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_LASTDRIV_COM = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_LICENSE_TXT = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_MEM_EXE = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_MEM_EXE_PC98 = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_MOVE_EXE = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_MPXPLAY_EXE = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_NE2000_COM = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_OPTION_EXE = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_PRINT_COM = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_REPLACE_EXE = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_SHUTDOWN_COM = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_SORT_EXE = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_SYS_COM = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_VGA_COM = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_XCOPY_EXE = { "", nullptr, 0 };

// minizip API stubs (ZIP-drive support is Phase-3, backed by
// third_party/miniz later). All entry points report failure so
// mount/imgmount of .zip files fail gracefully with a message.
#include "ioapi.h"
#include "zip.h"
#include "unzip.h"

#include <string.h>

void fill_fopen64_filefunc(zlib_filefunc64_def *pzlib_filefunc_def) {
  if (pzlib_filefunc_def != nullptr) {
    memset(pzlib_filefunc_def, 0, sizeof(*pzlib_filefunc_def));
  }
}

zipFile zipOpen2_64(const void *path, int append, const char **globalcomment,
                    zlib_filefunc64_def *pzlib_filefunc_def) {
  (void)path; (void)append; (void)globalcomment; (void)pzlib_filefunc_def;
  return nullptr;
}

int zipClose(zipFile zf, const char *globalcomment) {
  (void)zf; (void)globalcomment;
  return ZIP_ERRNO;
}

int zipOpenNewFileInZip(zipFile zf, const char *filename,
                        const zip_fileinfo *zipfi, const void *extrafield_local,
                        unsigned int size_extrafield_local,
                        const void *extrafield_global,
                        unsigned int size_extrafield_global,
                        const char *comment, int method, int level) {
  (void)zf; (void)filename; (void)zipfi; (void)extrafield_local;
  (void)size_extrafield_local; (void)extrafield_global;
  (void)size_extrafield_global; (void)comment; (void)method; (void)level;
  return ZIP_PARAMERROR;
}

int zipWriteInFileInZip(zipFile zf, const void *buf, unsigned int len) {
  (void)zf; (void)buf; (void)len;
  return ZIP_ERRNO;
}

int zipCloseFileInZip(zipFile zf) {
  (void)zf;
  return ZIP_ERRNO;
}

unzFile unzOpen2_64(const void *path, zlib_filefunc64_def *pzlib_filefunc_def) {
  (void)path; (void)pzlib_filefunc_def;
  return nullptr;
}

int unzClose(unzFile zf) {
  (void)zf;
  return UNZ_OK;
}

int unzLocateFile(unzFile zf, const char *filename, int casesens) {
  (void)zf; (void)filename; (void)casesens;
  return UNZ_END_OF_LIST_OF_FILE;
}

int unzGetCurrentFileInfo64(unzFile zf, unz_file_info64 *info, char *filename,
                            unsigned long filename_size, void *extrafield,
                            unsigned long extrafield_size, char *comment,
                            unsigned long comment_size) {
  (void)zf; (void)info; (void)filename; (void)filename_size;
  (void)extrafield; (void)extrafield_size; (void)comment; (void)comment_size;
  return UNZ_PARAMERROR;
}

int unzOpenCurrentFile(unzFile zf) {
  (void)zf;
  return UNZ_PARAMERROR;
}

int unzCloseCurrentFile(unzFile zf) {
  (void)zf;
  return UNZ_OK;
}

int unzReadCurrentFile(unzFile zf, void *buf, unsigned int len) {
  (void)zf; (void)buf; (void)len;
  return -1;
}

// Remaining binary blobs declared in src/builtin/*.h (not builtin.h).
struct BuiltinFileBlob bfb_CDPLAY_TXT = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_CDPLAY_ZIP = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_IPSMAKE_EXE = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_IPSPATCH_EXE = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_UNZIP_EXE = { "", nullptr, 0 };
struct BuiltinFileBlob bfb_ZIP_EXE = { "", nullptr, 0 };

// Deleted-owner globals referenced by dos_programs.cpp:
// - forceCD (dos_mscdex.cpp): CD-ROM interface forcing, Phase-3.
// - mountiro[] (sdlmain drive menu): per-drive read-only flags.
// - boot_code_image*/boot_debug_break/custom_bios_image* (sdlmain custom
//   BIOS/boot-image feature): empty/absent on BMX.
int forceCD = -1;
bool mountiro[26] = {};

#include <vector>

std::vector<uint8_t> boot_code_image;
PhysPt boot_code_image_load_to = 0;
uint16_t boot_code_image_stack_ss = 0;
uint16_t boot_code_image_stack_sp = 0;
bool boot_debug_break = false;
size_t custom_bios_image_size = 0;
Bitu custom_bios_image_offset = 0;
unsigned char *custom_bios_image = nullptr;

// Deleted-backend helpers referenced by vendored dos_programs.cpp
// (MOUNT/IMGMOUNT/BOOT paths). All are inert Phase-1/2 no-ops with safe
// defaults; the local-dir/FAT-image paths they guard never call them.
#include "programs.h"
#include "mapper.h"
#include "mouse.h"
#include "ide.h"
#include "bios_disk.h"

#include <vector>

// Deleted owner: src/hardware/ne2000.cpp
bool addne2k = false;

// Deleted owner: src/gui/sdlmain.cpp + menu_callback.cpp
bool clear_screen() { return false; }
bool Mouse_IsLocked() { return false; }
void GFX_CaptureMouse(bool capture) { (void)capture; }
void CaptureMouseNotify(bool capture) { (void)capture; }
void show_prompt() {}

// Deleted owner: src/dos/dos_keyboard_layout.cpp (US default, Phase-3)
Bitu DOS_SwitchKeyboardLayout(const char *new_layout, int32_t &tried_cp) {
  (void)new_layout; (void)tried_cp;
  return 0;
}

// Deleted owner: src/hardware/floppy.cpp (no floppy controller, Phase-3)
bool FDC_AssignINT13Disk(unsigned char drv) {
  (void)drv;
  return false;
}
bool FDC_UnassignINT13Disk(unsigned char drv) {
  (void)drv;
  return false;
}

// Deleted owners: src/dos/dos_mscdex.cpp, src/hardware/ide.cpp,
// src/dos/cdrom*.cpp (CD/IDE backends, Phase-3). MOUNT/IMGMOUNT local and
// plain-image paths never reach these; CD paths fail gracefully.
int CDROM_AllocateInterface(const char *physicalPath, int forceCD,
                            uint16_t numDrive, CDROM_Interface **cdrom) {
  (void)physicalPath; (void)forceCD; (void)numDrive; (void)cdrom;
  return 0;
}
// ElTorito_* are REAL (vendored dos/eltorito.cpp); no stubs here.
std::string GetIDEPosition(unsigned char bios_disk_index) {
  (void)bios_disk_index;
  return std::string();
}
bool IDE_CDROM_Attach(const std::string &opts,
                      const std::vector<CDROM_Interface *> &cds,
                      bool opt_replace) {
  (void)opts; (void)cds; (void)opt_replace;
  return false;
}
bool IDE_CDROM_Detach(const std::string &opts) {
  (void)opts;
  return false;
}
bool IDE_controller_occupied(signed char index, bool slave) {
  (void)index; (void)slave;
  return false;
}
void IDE_Hard_Disk_Attach(signed char index, bool slave,
                          unsigned char bios_disk_index) {
  (void)index; (void)slave; (void)bios_disk_index;
}
int IDE_MatchCDROMDrive(char drv) {
  (void)drv;
  return -1;
}

// Deleted owner: src/gui/sdlmain.cpp mapper registry
std::vector<std::string> MAPPER_GetEventNames(const std::string &prefix) {
  (void)prefix;
  return std::vector<std::string>();
}

// Deleted program backends (serial/parallel/PC-98 utility): registered on
// Z: by DOS_SetupPrograms, but running one prints "not supported" instead
// of faulting on a null Program.
class UnsupportedProgram : public Program {
 public:
  UnsupportedProgram() : Program() {}
  void Run(void) override {
    WriteOut("This program is not supported on BMX.\n");
  }
};

void SERIAL_ProgramStart(Program **make) { *make = new UnsupportedProgram(); }
void PARALLEL_ProgramStart(Program **make) { *make = new UnsupportedProgram(); }
void PC98UTIL_ProgramStart(Program **make) { *make = new UnsupportedProgram(); }

// Deleted owner: MODE.COM line setter (still-deleted helper). Text 80x25
// works without it; other modes report failure gracefully.
bool setlines(const char *mname) {
  (void)mname;
  return false;
}

// Deleted owner: src/gui/sdlmain.cpp drive-menu flags. mountiro was already
// here; mountfro (force read-only) is its sibling, read by MOUNT/IMGMOUNT.
bool mountfro[26] = {};

// Deleted owners: src/hardware/ide.cpp CD detection. No IDE backend
// (Phase-3), so nothing is ever a CD-ROM.
bool IDE_is_CDROM(signed char index, bool slave) {
  (void)index; (void)slave;
  return false;
}
bool IDE_is_CDROM(const std::string &opts) {
  (void)opts;
  return false;
}

// Deleted owner: src/misc/savestates.cpp ZIP helpers. Save states are
// Phase-3; mirror upstream shape (zipOutOpenFile delegates to
// zipOpenNewFileInZip3_64, which the minizip shim reports as an error).
int zipOpenNewFileInZip3_64(zipFile zf, const char *filename,
                            const zip_fileinfo *zipfi,
                            const void *extrafield_local,
                            unsigned int size_extrafield_local,
                            const void *extrafield_global,
                            unsigned int size_extrafield_global,
                            const char *comment, int method, int level, int raw,
                            int windowBits, int memLevel, int strategy,
                            const char *password, unsigned long crcFile,
                            int zip64) {
  (void)zf; (void)filename; (void)zipfi; (void)extrafield_local;
  (void)size_extrafield_local; (void)extrafield_global;
  (void)size_extrafield_global; (void)comment; (void)method; (void)level;
  (void)raw; (void)windowBits; (void)memLevel; (void)strategy;
  (void)password; (void)crcFile; (void)zip64;
  return ZIP_PARAMERROR;
}

void zipSetCurrentTime(zip_fileinfo &zi) {
  zi.dosDate = 0;
  zi.internal_fa = 0;
  zi.external_fa = 0;
  zi.tmz_date.tm_sec = 0;
  zi.tmz_date.tm_min = 0;
  zi.tmz_date.tm_hour = 0;
  zi.tmz_date.tm_mday = 0;
  zi.tmz_date.tm_mon = 0;
  zi.tmz_date.tm_year = 0;
}

int zipOutOpenFile(zipFile zf, const char *zfname, zip_fileinfo &zi,
                   const bool compress) {
  const int opt_compress_level = compress ? 9 : 0;
  return zipOpenNewFileInZip3_64(zf, zfname, &zi, NULL, 0, NULL, 0, NULL,
                                 (opt_compress_level != 0) ? Z_DEFLATED : 0,
                                 opt_compress_level, 0, -MAX_WBITS,
                                 DEF_MEM_LEVEL, Z_DEFAULT_STRATEGY,
                                 NULL, 0, 1);
}

// POSIX shims missing from the Circle C library (declared via system
// headers with C linkage, so compilation succeeds, but Newlib has no
// definitions):
// - flock(): fopen_lock() in vendored dos_programs.cpp takes an exclusive
//   non-blocking lock on image files. BMX is single-tasked: always succeed.
// - dirname(): IMGMAKE path check only needs the directory portion; a small
//   POSIX-conformant implementation on a static buffer (never touches input,
//   since dos_programs.cpp passes a c_str() cast).
extern "C" {
int flock(int fd, int operation) {
  (void)fd; (void)operation;
  return 0;
}

char *dirname(char *path) {
  static char buf[1024];
  if (path == nullptr || path[0] == '\0') {
    buf[0] = '.';
    buf[1] = '\0';
    return buf;
  }
  size_t len = strlen(path);
  if (len >= sizeof(buf)) len = sizeof(buf) - 1;
  memcpy(buf, path, len);
  buf[len] = '\0';
  while (len > 1 && buf[len - 1] == '/') buf[--len] = '\0';
  char *slash = strrchr(buf, '/');
  if (slash == nullptr) {
    buf[0] = '.';
    buf[1] = '\0';
  } else if (slash == buf) {
    buf[1] = '\0';
  } else {
    *slash = '\0';
  }
  return buf;
}
}  // extern "C"
