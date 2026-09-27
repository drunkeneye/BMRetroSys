//
// atarist_bmx_platform.c
//
// BMX platform providers for the vendored Hatari core: wall-clock timing
// (replaces src/sdl/timing.c and src/retro/timing.c), status bar, debugger
// UI, keymap, GUI events and joystick UI (all headless no-ops / safe
// defaults on BMX; real IKBD/joystick injection lives in atarist_input).
// Plain C: Hatari headers are C-only.
//
// The critical function here is Timing_WaitOnVbl(): Hatari calls it at the
// end of every VBL interrupt, and (like the libretro port) we answer by
// breaking out of the CPU loop so the BMX frame loop regains control once
// per frame: pump F12, blit video, drain audio, pace to 50 Hz.
//

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "main.h"
#include "configuration.h"
#include "timing.h"
#include "statusbar.h"
#include "m68000.h"
#include "newcpu.h"
#include "keymap.h"
#include "gui_event.h"
#include "joy_ui.h"
#include "dialog.h"
#include "debugui.h"
#include "scandir.h"
#include "circle.h"
#include "utime.h"
#include <errno.h>
#include <dirent.h>
#include <stdlib.h>

// Joystick latch reader (atarist_input.c).
void atarist_joy_state(int st_id, int *x, int *y, int *buttons);

// File+serial log sink (atarist_debug.cpp). No printf(): no UART-only output.
void atarist_log_line(const char *line);

static uint32_t s_run_vbls = 0;

// Exception counters: every 68k exception funnels through
// DebugUI_Exceptions() (cpu/newcpu.c exception_debug), so this is an exact
// census of what the CPU is doing, with zero tracing overhead.
static unsigned long s_exc_count[64];
static unsigned long s_exc_total = 0;
// TRAP #n (vectors 32..47) histogram by D0 function number: identifies
// which GEMDOS/BIOS/XBIOS calls a wait loop is spinning on.
static unsigned long s_trap_count[16][64];

// Minimal scandir/alphasort on newlib dirent (no d_reclen there, so the
// vendored scandir.c cannot compile). Used by GEMDOS host directory search.
int alphasort(const struct dirent **d1, const struct dirent **d2) {
  return strcmp((*d1)->d_name, (*d2)->d_name);
}

int scandir(const char *dirname, struct dirent ***namelist,
            int (*sdfilter)(const struct dirent *),
            int (*comp)(const struct dirent **, const struct dirent **)) {
  DIR *dirp;
  struct dirent *entry;
  struct dirent **list = NULL;
  size_t count = 0, alloc = 0;

  if (namelist == NULL) {
    return -1;
  }
  *namelist = NULL;
  dirp = opendir(dirname);
  if (dirp == NULL) {
    return -1;
  }
  while ((entry = readdir(dirp)) != NULL) {
    struct dirent *copy;
    size_t namelen;
    if (sdfilter != NULL && !sdfilter(entry)) {
      continue;
    }
    if (count >= alloc) {
      size_t nalloc = alloc ? alloc * 2 : 32;
      struct dirent **nlist =
          (struct dirent **)realloc(list, nalloc * sizeof(*nlist));
      if (nlist == NULL) {
        break;
      }
      list = nlist;
      alloc = nalloc;
    }
    namelen = strlen(entry->d_name) + 1;
    copy = (struct dirent *)malloc(sizeof(*copy) + namelen);
    if (copy == NULL) {
      break;
    }
    memcpy(copy, entry, sizeof(*copy));
    memcpy(copy->d_name, entry->d_name, namelen);
    list[count++] = copy;
  }
  closedir(dirp);
  if (comp != NULL && count > 1) {
    qsort(list, count, sizeof(*list),
          (int (*)(const void *, const void *))comp);
  }
  if (count == 0) {
    free(list);
    return 0;
  }
  *namelist = list;
  return (int)count;
}

// Fatal error exit (defined in the excluded src/sdl/main_sdl.c upstream).
// Nearly unreachable on BMX: TOS presence is pre-checked in config_init,
// IPF init is stubbed true. Log and park: the bmretrosys-atarist.log tail shows
// exactly what failed.
void Main_ErrorExit(const char *msg1, const char *msg2, int errval) {
  char line[192];
  snprintf(line, sizeof(line), "Hatari FATAL [%d]: %s %s", errval,
           msg1 != NULL ? msg1 : "?", msg2 != NULL ? msg2 : "");
  atarist_log_line(line);
  while (1) {
  }
}

// utime() for newlib (see bmx-shim/utime.h): GEMDOS host-file date stamping
// is cosmetic on BMX; decline so file dates are left untouched.
int utime(const char *path, const struct utimbuf *times) {
  (void)path;
  (void)times;
  errno = ENOSYS;
  return -1;
}

// --- timing ---------------------------------------------------------------

int64_t Timing_GetTicks(void) {
  return (int64_t)circle_get_ticks();
}

void Timing_PrintSpeed(void) {
}

uint32_t Timing_SetRunVBLs(uint32_t vbls) {
  if (!vbls) {
    return s_run_vbls;
  }
  s_run_vbls = vbls;
  return 0;
}

const char *Timing_SetVBLSlowdown(int factor) {
  (void)factor;
  return NULL;
}

void Timing_WaitOnVbl(void) {
  // End of emulated frame: yield back to the BMX frame loop (same pattern
  // as the libretro port). The loop re-enters via m68k_run().
  M68000_SetSpecial(SPCFLAG_BRK);
  quit_program = UAE_QUIT;
}

void Timing_CheckForAccurateDelays(void) {
}

// --- status bar (none on BMX) ----------------------------------------------

void Statusbar_InitialSetup(void) {
}

int Statusbar_SetHeight(int ScreenWidth, int ScreenHeight) {
  (void)ScreenWidth;
  (void)ScreenHeight;
  return 0;
}

int Statusbar_GetHeightForSize(int width, int height) {
  (void)width;
  (void)height;
  return 0;
}

int Statusbar_GetHeight(void) {
  return 0;
}

void Statusbar_EnableHDLed(drive_led_t state) {
  (void)state;
}

void Statusbar_SetFloppyLed(drive_index_t drive, drive_led_t state) {
  (void)drive;
  (void)state;
}

void Statusbar_UpdateInfo(void) {
}

void Statusbar_AddMessage(const char *msg, uint32_t msecs) {
  (void)msg;
  (void)msecs;
}

// --- debugger UI (unavailable headless) ------------------------------------

void DebugUI_Init(void) {
}

void DebugUI(debug_reason_t reason) {
  (void)reason;
}

void DebugUI_Exceptions(int nr, long pc) {
  (void)pc;
  if (nr >= 0 && nr < 64) {
    s_exc_count[nr]++;
  }
  s_exc_total++;
  if (nr >= 32 && nr < 48) {
    // D0 holds the GEMDOS/BIOS/XBIOS function number at trap entry.
    unsigned d0 = regs.regs[0] & 63;
    s_trap_count[nr - 32][d0]++;
  }
}

// Heartbeat reporter for the exception counters (called from the BMX frame
// loop). Reports per-vector counts for the window, then restarts it.
void atarist_log_exc_stats(void) {
  char line[256];
  int pos;
  pos = snprintf(line, sizeof(line), "AtariST: exc total=%lu", s_exc_total);
  for (int v = 0; v < 64 && pos > 0 && (size_t)pos < sizeof(line) - 16; v++) {
    if (s_exc_count[v] != 0) {
      pos += snprintf(line + pos, sizeof(line) - (size_t)pos, " %d:%lu", v,
                      s_exc_count[v]);
    }
  }
  atarist_log_line(line);
  pos = snprintf(line, sizeof(line), "AtariST: traps");
  for (int t = 0; t < 16 && pos > 0 && (size_t)pos < sizeof(line) - 24; t++) {
    for (int f = 0; f < 64 && pos > 0 && (size_t)pos < sizeof(line) - 24;
         f++) {
      if (s_trap_count[t][f] != 0) {
        pos += snprintf(line + pos, sizeof(line) - (size_t)pos, " T%d.%d=%lu",
                        t, f, s_trap_count[t][f]);
      }
    }
  }
  atarist_log_line(line);
  for (int v = 0; v < 64; v++) {
    s_exc_count[v] = 0;
  }
  for (int t = 0; t < 16; t++) {
    for (int f = 0; f < 64; f++) {
      s_trap_count[t][f] = 0;
    }
  }
  s_exc_total = 0;
}

bool DebugUI_ParseLine(const char *input) {
  (void)input;
  return false;
}

bool DebugUI_AddParseFile(const char *input) {
  (void)input;
  return false;
}

void DebugUI_MemorySnapShot_Capture(const char *path, bool bSave) {
  (void)path;
  (void)bSave;
}

void DebugUI_UnInit(void) {
}

bool DebugUI_ParseFile(const char *path, bool reinit, bool verbose) {
  (void)path;
  (void)reinit;
  (void)verbose;
  return false;
}

// --- main-loop helpers (live in excluded src/sdl/main_sdl.c upstream) -----

static int s_quit_value = 0;

void Main_SetQuitValue(int exitval) {
  s_quit_value = exitval;
}

void Main_RequestQuit(int exitval) {
  // No host OS to quit to on BMX; record and ignore (menu reboot owns this).
  s_quit_value = exitval;
}

// chmod() for newlib (which declares but does not define it): GEMDOS
// attribute changes are cosmetic on BMX; decline.
#include <sys/stat.h>
int chmod(const char *path, mode_t mode) {
  (void)path;
  (void)mode;
  return -1;
}

// _fcntl() syscall for newlib (declared nowhere, referenced by libc's
// reentrancy layer): nothing on BMX needs file-descriptor flags; decline.
int _fcntl(int fd, int cmd, int arg) {
  (void)fd;
  (void)cmd;
  (void)arg;
  return -1;
}

// --- keymap (IKBD injection bypasses the SDL keymap) ------------------------

void Keymap_Init(void) {
}

void Keymap_InitShortcutDefaultKeys(void) {
}

void Keymap_LoadRemapFile(const char *pszFileName) {
  (void)pszFileName;
}

void Keymap_SimulateCharacter(char asckey, bool press) {
  (void)asckey;
  (void)press;
}

int Keymap_GetKeyFromName(const char *name) {
  (void)name;
  return -1;
}

const char *Keymap_GetKeyName(int keycode) {
  (void)keycode;
  return "";
}

void Keymap_SetCountry(int countrycode) {
  (void)countrycode;
}

bool Keymap_IsShiftPressed(void) {
  return false;
}

// --- GUI events (BMX menu pumps F12 itself) ---------------------------------

void GuiEvent_WarpMouse(int x, int y, bool restore) {
  (void)x;
  (void)y;
  (void)restore;
}

void GuiEvent_EventHandler(void) {
}

// --- joystick UI (keyboard-emulated sticks; see atarist_input) ---------------

void JoyUI_Init(void) {
}

void JoyUI_UnInit(void) {
}

const char *JoyUI_GetName(int id) {
  (void)id;
  return "BMX";
}

int JoyUI_GetMaxId(void) {
  return 0;
}

int JoyUI_NumJoysticks(void) {
  return 0;
}

bool JoyUI_ValidateJoyId(int i) {
  (void)i;
  return false;
}

void JoyUI_SetDefaultKeys(int stjoy_id) {
  (void)stjoy_id;
}

bool JoyUI_ReadJoystick(int nStJoyId, JOYREADING *pJoyReading) {
  int x = 0, y = 0, buttons = 0;
  atarist_joy_state(nStJoyId, &x, &y, &buttons);
  if (pJoyReading != NULL) {
    pJoyReading->XPos = x;
    pJoyReading->YPos = y;
    pJoyReading->Buttons = buttons;
  }
  return true;
}

int JoyUI_GetRealFireButtons(int nStJoyId) {
  (void)nStJoyId;
  return 0;
}

// --- dialogs (no GUI on BMX; TOS load failure aborts with a log line) -------

int Dialog_MainDlg(bool *bReset, bool *bLoadedSnapshot) {
  (void)bReset;
  (void)bLoadedSnapshot;
  return 0;
}

void Dialog_AboutDlg(void) {
}

void DlgCpu_Main(void) {
}

bool DlgAlert_Notice(const char *text) {
  char line[160];
  snprintf(line, sizeof(line), "AtariST: DlgAlert_Notice: %s",
           text != NULL ? text : "?");
  atarist_log_line(line);
  return true;
}

bool DlgAlert_Query(const char *text) {
  char line[160];
  snprintf(line, sizeof(line), "AtariST: DlgAlert_Query: %s",
           text != NULL ? text : "?");
  atarist_log_line(line);
  return false;
}

void Dialog_DeviceDlg(void) {
}

void DlgFloppy_Main(void) {
}

char *DlgFloppy_ShortCutSel(const char *path_and_name, char **zip_path) {
  (void)path_and_name;
  (void)zip_path;
  return NULL;
}

void Dialog_HaltDlg(void) {
}

void DlgHardDisk_Main(void) {
}

void Dialog_JoyDlg(void) {
}

void Dialog_KeyboardDlg(void) {
}

bool Dialog_MemDlg(void) {
  return false;
}

char *DlgNewDisk_Main(void) {
  return NULL;
}

void Dialog_MonitorDlg(void) {
}

void Dialog_WindowDlg(void) {
}

void Dialog_RecordingDlg(void) {
}

void Dialog_SoundDlg(void) {
}

void DlgSystem_Main(void) {
}

void DlgRom_Main(void) {
}

bool Dialog_DoProperty(void) {
  atarist_log_line("AtariST: Dialog_DoProperty: no GUI on BMX");
  return false;
}
