/*
	mac128k_bmx_backend.c

	BMX bare-metal OSGLU backend for Mini vMac (Macintosh 128K).

	Replaces the Unix OSGLU backends (OSGLUXWN.c / OSGLUSDL.c): it is the
	only TU that includes COMOSGLU.h / PBUFSTDC.h / CONTROLM.h /
	PROGMAIN.h, so all shared platform logic (event queue, screen diff,
	Pbuf bookkeeping, messages) lands here exactly once.

	Exports (C linkage, called from the C++ wrapper):
	  bmx_mac128k_backend_set_rom_path / set_disk / key / mouse_button /
	  mouse_delta / init / run / shutdown / request_reboot / take_reboot /
	  poll_events_kind (input drain events are pushed from the wrapper).

	BMX-owned helpers implemented in mac128k_bmx_video.cpp /
	mac128k_bmx_audio.cpp / mac128k_debug side (declared below):
	  bmx_mac128k_video_blit_rows, bmx_mac128k_video_present,
	  bmx_mac128k_audio_write, bmx_mac128k_debug_log,
	  bmx_mac128k_poll_input (drains USB queue, returns event bits),
	  bmx_mac128k_open_menu.
*/

#include <stddef.h>
#include <stdio.h>
#include <string.h>

/* NOTE: Mini vMac headers are single-inclusion (double #include is a
   compile error by design). The backend TU includes the OSGLU chain
   (like OSGLUSDL.c does) and must NOT include PICOMMON.h (that chain is
   for the core .c files, which include only PICOMMON.h). */
#include "OSGCOMUI.h"
#include "OSGCOMUD.h"
#include "INTLCHAR.h"
#include "COMOSGLU.h"
#include "PBUFSTDC.h"
#include "CONTROLM.h"

#include "mac128k_bmx_backend.h"

/* --- simple utilities --- */

GLOBALOSGLUPROC MyMoveBytes(anyp srcPtr, anyp destPtr, si5b byteCount)
{
	(void) memcpy((char *)destPtr, (char *)srcPtr, byteCount);
}

/* Core entry points (also declared in PROGMAIN.h, included at the end;
   forward-declared here for use by ReserveAllocAll). */
EXPORTPROC EmulationReserveAlloc(void);
EXPORTPROC ProgramMain(void);

#if MySoundEnabled
/* Sound ring geometry (SDL values); the buffer itself is allocated in
   ReserveAllocAll below. Drain happens in DoneWithDrawingForTick. */
#define kLn2SoundBuffers 4
#define kSoundBuffers (1 << kLn2SoundBuffers)

#define kLnOneBuffLen 9
#define kLnAllBuffLen (kLn2SoundBuffers + kLnOneBuffLen)
#define kOneBuffLen (1UL << kLnOneBuffLen)
#define kAllBuffLen (1UL << kLnAllBuffLen)
#define kLnOneBuffSz (kLnOneBuffLen + kLn2SoundSampSz - 3)
#define kLnAllBuffSz (kLnAllBuffLen + kLn2SoundSampSz - 3)
#define kOneBuffSz (1UL << kLnOneBuffSz)
#define kAllBuffSz (1UL << kLnAllBuffSz)
#define kOneBuffMask (kOneBuffLen - 1)
#define kAllBuffMask (kAllBuffLen - 1)
#define dbhBufferSize (kAllBuffSz + kOneBuffSz)

LOCALVAR tpSoundSamp TheSoundBuffer = nullpr;
LOCALVAR ui4b ThePlayOffset = 0;
LOCALVAR ui4b TheFillOffset = 0;
LOCALVAR ui4b TheWriteOffset = 0;
#endif /* MySoundEnabled */

/* --- memory (SDL AllocMyMemory double-pass pattern) --- */

LOCALPROC ReserveAllocAll(void)
{
	ReserveAllocOneBlock(&ROM, kROM_Size, 5, falseblnr);
	ReserveAllocOneBlock(&screencomparebuff,
		vMacScreenNumBytes, 5, trueblnr);
#if UseControlKeys
	ReserveAllocOneBlock(&CntrlDisplayBuff,
		vMacScreenNumBytes, 5, falseblnr);
#endif
#if MySoundEnabled
	ReserveAllocOneBlock((ui3p *)&TheSoundBuffer,
		dbhBufferSize, 5, falseblnr);
#endif
	EmulationReserveAlloc();
}

LOCALFUNC blnr AllocMyMemory(void)
{
	uimr n;
	blnr IsOk = falseblnr;

	ReserveAllocOffset = 0;
	ReserveAllocBigBlock = nullpr;
	ReserveAllocAll();
	n = ReserveAllocOffset;
	ReserveAllocBigBlock = (ui3p)calloc(1, n);
	if (NULL == ReserveAllocBigBlock) {
		bmx_mac128k_debug_log(
			"mac128k: backend: out of memory for emulator block");
	} else {
		ReserveAllocOffset = 0;
		ReserveAllocAll();
		if (n != ReserveAllocOffset) {
			bmx_mac128k_debug_log(
				"mac128k: backend: alloc size mismatch");
		} else {
			IsOk = trueblnr;
		}
	}

	return IsOk;
}

LOCALPROC UnallocMyMemory(void)
{
	if (nullpr != ReserveAllocBigBlock) {
		free((char *)ReserveAllocBigBlock);
		ReserveAllocBigBlock = nullpr;
	}
}

/* --- ROM --- */

static char s_rom_path[256] = "";

void bmx_mac128k_backend_set_rom_path(const char *path)
{
	if (NULL == path) {
		s_rom_path[0] = 0;
	} else {
		unsigned int i = 0;
		while (0 != path[i] && i < sizeof(s_rom_path) - 1) {
			s_rom_path[i] = path[i];
			++i;
		}
		s_rom_path[i] = 0;
	}
}

LOCALFUNC tMacErr LoadMacRomFrom(const char *path)
{
	tMacErr err;
	FILE *ROM_File;
	long File_Size;

	ROM_File = fopen(path, "rb");
	if (NULL == ROM_File) {
		err = mnvm_fnfErr;
	} else {
		File_Size = (long)fread(ROM, 1, kROM_Size, ROM_File);
		if (File_Size != (long)kROM_Size) {
			bmx_mac128k_debug_log(
				"mac128k: backend: short ROM read");
			err = mnvm_eofErr;
		} else {
			err = ROM_IsValid();
		}
		fclose(ROM_File);
	}

	return err;
}

static blnr s_rom_error_logged = falseblnr;

LOCALFUNC blnr LoadMacRom(void)
{
	tMacErr err;

	if (0 == s_rom_path[0]) {
		err = mnvm_fnfErr;
	} else {
		err = LoadMacRomFrom(s_rom_path);
	}
	if (mnvm_noErr != err) {
		if (! s_rom_error_logged) {
			s_rom_error_logged = trueblnr;
			if (mnvm_fnfErr == err) {
				bmx_mac128k_debug_log(
					"mac128k: backend: no ROM file "
					"(need 64KB 128K ROM in /roms/MAC128K)");
			} else {
				bmx_mac128k_debug_log(
					"mac128k: backend: ROM rejected "
					"(need 128K ROM, ck 28BA61CE/28BA4E50)");
			}
		}
		return falseblnr;
	}
	s_rom_error_logged = falseblnr;
	return trueblnr;
}

/* --- drives (Sony floppies) --- */

LOCALVAR FILE *Drives[NumDrives];

/* Host path per drive (for GetName / EjectDelete; empty when no disk). */
static char s_inserted_path[NumDrives][256];

LOCALPROC InitDrives(void)
{
	tDrive i;

	for (i = 0; i < NumDrives; ++i) {
		Drives[i] = NULL;
	}
}

GLOBALOSGLUFUNC tMacErr vSonyTransfer(blnr IsWrite, ui3p Buffer,
	tDrive Drive_No, ui5r Sony_Start, ui5r Sony_Count,
	ui5r *Sony_ActCount)
{
	tMacErr err = mnvm_miscErr;
	FILE *refnum = Drives[Drive_No];
	ui5r NewSony_Count = 0;

	if (Drive_No < NumDrives && NULL != refnum) {
		if (0 == fseek(refnum, (long)Sony_Start, SEEK_SET)) {
			if (IsWrite) {
				NewSony_Count = (ui5r)fwrite(Buffer, 1,
					Sony_Count, refnum);
			} else {
				NewSony_Count = (ui5r)fread(Buffer, 1,
					Sony_Count, refnum);
			}
			if (NewSony_Count == Sony_Count) {
				err = mnvm_noErr;
			}
		}
	}

	if (nullpr != Sony_ActCount) {
		*Sony_ActCount = NewSony_Count;
	}

	return err;
}

GLOBALOSGLUFUNC tMacErr vSonyGetSize(tDrive Drive_No, ui5r *Sony_Count)
{
	tMacErr err = mnvm_miscErr;
	FILE *refnum = Drives[Drive_No];
	long v;

	if (Drive_No < NumDrives && NULL != refnum) {
		if (0 == fseek(refnum, 0, SEEK_END)) {
			v = ftell(refnum);
			if (v >= 0) {
				*Sony_Count = (ui5r)v;
				err = mnvm_noErr;
			}
		}
	}

	return err;
}

LOCALFUNC tMacErr vSonyEject0(tDrive Drive_No, blnr deleteit)
{
	FILE *refnum;

	(void) deleteit;
	if (Drive_No >= NumDrives) {
		return mnvm_miscErr;
	}
	refnum = Drives[Drive_No];

	DiskEjectedNotify(Drive_No);

	if (NULL != refnum) {
		fclose(refnum);
		Drives[Drive_No] = NULL;
	}
	s_inserted_path[Drive_No][0] = 0;

	return mnvm_noErr;
}

GLOBALOSGLUFUNC tMacErr vSonyEject(tDrive Drive_No)
{
	return vSonyEject0(Drive_No, falseblnr);
}

LOCALPROC UnInitDrives(void)
{
	tDrive i;

	for (i = 0; i < NumDrives; ++i) {
		if (vSonyIsInserted(i)) {
			(void) vSonyEject(i);
		}
	}
}

#if IncludeSonyNew
GLOBALOSGLUFUNC tMacErr vSonyEjectDelete(tDrive Drive_No)
{
	tMacErr err;

	if (Drive_No >= NumDrives) {
		return mnvm_miscErr;
	}
	err = vSonyEject0(Drive_No, trueblnr);
	if (mnvm_noErr == err) {
		if (0 != s_inserted_path[Drive_No][0]) {
			(void) remove(s_inserted_path[Drive_No]);
			s_inserted_path[Drive_No][0] = 0;
		}
	}
	return err;
}
#endif

#if IncludeSonyGetName
GLOBALOSGLUFUNC tMacErr vSonyGetName(tDrive Drive_No, tPbuf *r)
{
	const char *base;
	unsigned int n;
	tMacErr err;
	tPbuf p;

	if (Drive_No >= NumDrives
		|| 0 == s_inserted_path[Drive_No][0])
	{
		return mnvm_miscErr;
	}
	base = s_inserted_path[Drive_No];
	{
		const char *s = base;
		while (0 != *s) {
			if ('/' == *s) {
				base = s + 1;
			}
			++s;
		}
	}
	n = 0;
	while (0 != base[n]) {
		++n;
	}
	err = PbufNew(n, &p);
	if (mnvm_noErr == err) {
		unsigned int i;
		/* PbufDat is LOCAL to this TU (via PBUFSTDC.h). */
		ui3p dst = (ui3p)PbufDat[p];
		for (i = 0; i < n; ++i) {
			dst[i] = (ui3b)base[i];
		}
		*r = p;
	}
	return err;
}
#endif

/* Staged disk images (set by the wrapper before init / on reboot). */
static char s_disk_path[NumDrives][256];
static blnr s_disk_ro[NumDrives];

void bmx_mac128k_backend_set_disk(int drive, const char *path,
	int read_only)
{
	if (drive < 0 || drive >= (int)NumDrives) {
		return;
	}
	if (NULL == path || 0 == path[0]) {
		s_disk_path[drive][0] = 0;
	} else {
		unsigned int i = 0;
		while (0 != path[i] && i < sizeof(s_disk_path[0]) - 1) {
			s_disk_path[drive][i] = path[i];
			++i;
		}
		s_disk_path[drive][i] = 0;
	}
	s_disk_ro[drive] = read_only ? trueblnr : falseblnr;
}

LOCALFUNC blnr LoadInitialImages(void)
{
	tDrive d;

	for (d = 0; d < NumDrives; ++d) {
		if (0 != s_disk_path[d][0]) {
			FILE *f;
			tDrive free_drive;
			blnr locked;
			/* Writable unless staged read-only or the host
			   file cannot be opened read-write. */
			locked = s_disk_ro[d];
			if (locked) {
				f = fopen(s_disk_path[d], "rb");
			} else {
				f = fopen(s_disk_path[d], "r+b");
				if (NULL == f) {
					f = fopen(s_disk_path[d], "rb");
					locked = trueblnr;
				}
			}
			if (NULL != f) {
				if (FirstFreeDisk(&free_drive)) {
					unsigned int i = 0;
					char msg[160];
					Drives[free_drive] = f;
					DiskInsertNotify(free_drive, locked);
					snprintf(msg, sizeof(msg),
						"mac128k: backend: disk %d inserted: %s%s",
						(int)free_drive, s_disk_path[d],
						locked ? " (locked)" : "");
					bmx_mac128k_debug_log(msg);
					while (0 != s_disk_path[d][i]
						&& i < sizeof(
							s_inserted_path[0]) - 1)
					{
						s_inserted_path[free_drive][i] =
							s_disk_path[d][i];
						++i;
					}
					s_inserted_path[free_drive][i] = 0;
				} else {
					fclose(f);
				}
			} else {
				char msg[160];
				snprintf(msg, sizeof(msg),
					"mac128k: backend: disk open FAILED: %s",
					s_disk_path[d]);
				bmx_mac128k_debug_log(msg);
			}
		}
	}
	return trueblnr;
}

/* --- clipboard (unsupported on BMX) --- */

#if IncludeHostTextClipExchange
GLOBALOSGLUFUNC tMacErr HTCEexport(tPbuf i)
{
	(void) i;
	return mnvm_miscErr;
}

GLOBALOSGLUFUNC tMacErr HTCEimport(tPbuf *r)
{
	(void) r;
	return mnvm_miscErr;
}
#endif

/* NOTE: WarnMsgUnsupportedDisk is defined by CONTROLM.h (default MacMsg
   path); not redefined here. Disk-format failures surface through the
   wrapper debug log at insert time. */

/* --- sound (22254Hz 8-bit mono ring, SDL logic, BMX drain) --- */

#if MySoundEnabled

GLOBALOSGLUFUNC tpSoundSamp MySound_BeginWrite(ui4r n, ui4r *actL)
{
	ui4b ToFillLen = kAllBuffLen - (TheWriteOffset - ThePlayOffset);
	ui4b WriteBuffContig =
		kOneBuffLen - (TheWriteOffset & kOneBuffMask);

	if (WriteBuffContig < n) {
		n = WriteBuffContig;
	}
	if (ToFillLen < n) {
		TheWriteOffset -= kOneBuffLen;
	}

	*actL = n;
	return TheSoundBuffer + (TheWriteOffset & kAllBuffMask);
}

LOCALFUNC blnr MySound_EndWrite0(ui4r actL)
{
	blnr v;

	TheWriteOffset += actL;

	if (0 != (TheWriteOffset & kOneBuffMask)) {
		v = falseblnr;
	} else {
		TheFillOffset = TheWriteOffset;
		v = trueblnr;
	}

	return v;
}

GLOBALOSGLUPROC MySound_EndWrite(ui4r actL)
{
	(void) MySound_EndWrite0(actL);
}

/* Drain completed 512-sample blocks into the Circle sink (blocking). */
LOCALPROC MySound_Drain(void)
{
	while (ThePlayOffset != TheFillOffset) {
		bmx_mac128k_audio_write(
			(const unsigned char *)(TheSoundBuffer
				+ (ThePlayOffset & kAllBuffMask)),
			kOneBuffLen);
		ThePlayOffset += kOneBuffLen;
	}
}

#endif /* MySoundEnabled */

/* --- screen --- */

LOCALPROC HaveChangedScreenBuff(ui4r top, ui4r left,
	ui4r bottom, ui4r right)
{
	(void) left;
	(void) right;
	if ((int)bottom > vMacScreenHeight) {
		bottom = vMacScreenHeight;
	}
	if ((int)top < 0) {
		top = 0;
	}
	if (bottom > top) {
		bmx_mac128k_video_blit_rows(GetCurDrawBuff(),
			(int)top, (int)bottom);
	}
}

/* Backend tick stats (logged periodically from the wrapper-visible
   counter below). */
static unsigned long s_tick_no = 0;
static unsigned long s_blits = 0;

LOCALPROC MyDrawChangesAndClear(void)
{
	if (ScreenChangedBottom > ScreenChangedTop) {
		HaveChangedScreenBuff((ui4r)ScreenChangedTop, 0,
			(ui4r)ScreenChangedBottom, (ui4r)vMacScreenWidth);
		/* Empty the rect by hand: the shared ScreenClearChanges()
		   resets to FULL (force-redraw semantic), which would blit
		   all 342 rows and re-present every tick. Empty here means
		   idle ticks skip both blit and present (big tick-budget
		   win: blit ~3.5ms + vsync-blocked present ~16ms). */
		ScreenChangedTop = vMacScreenHeight;
		ScreenChangedBottom = 0;
		ScreenChangedLeft = vMacScreenWidth;
		ScreenChangedRight = 0;
		s_blits++;
	}
}

/* Layer visibility is cheap flag checks; run it every tick so a VIC
   hide (video-settings apply hides the layer, menu transitions, HDMI
   re-eval) always recovers on the next tick even when the guest frame
   is static and no present follows. Same parity as the mac68k kernel,
   which ensures video on every refresh. */
extern void emux_ensure_video(void);

GLOBALOSGLUPROC DoneWithDrawingForTick(void)
{
	unsigned long blits_before = s_blits;
	/* Cap VIC flips at ~30Hz (at most every 2nd tick): sustained 60Hz
	   full-frame presents can overrun flip completion, surfacing as
	   torn/black fallback frames during redraw bursts (mouse/keys).
	   Cursor motion stays smooth; static screens are unaffected
	   (they present rarely anyway). */
	static unsigned long s_last_present_tick = 0;
	MyDrawChangesAndClear();
	emux_ensure_video();
	if (s_blits != blits_before &&
		s_tick_no - s_last_present_tick >= 2)
	{
		s_last_present_tick = s_tick_no;
		bmx_mac128k_video_present();
	}
#if MySoundEnabled
	MySound_Drain();
#endif
	s_tick_no++;
	/* Periodic liveness line commented out (was per-10s log spam).
	   Loop iterations, blits, wall-time, guest audio bytes (= 370 per
	   guest tick at 1x), guest-screen probe (first nonzero byte). */
	/* if ((s_tick_no % 600) == 0) { */ if (0) {
		ui3p scr = GetCurDrawBuff();
		long first_nz = -1;
		long i;
		char msg[160];
		extern unsigned long bmx_mac128k_audio_total(void);
		for (i = 0; i < (long)vMacScreenNumBytes; ++i) {
			if (0 != scr[i]) {
				first_nz = i;
				break;
			}
		}
		snprintf(msg, sizeof(msg),
			"mac128k: tick=%lu blits=%lu ontime=%lu audio=%lu "
			"firstnz=%ld",
			s_tick_no, s_blits, (unsigned long)OnTrueTime,
			bmx_mac128k_audio_total(), first_nz);
		bmx_mac128k_debug_log(msg);
	}
}

/* --- time, date --- */

/* Fixed BMX boot epoch: 2026-01-01 00:00 UTC (Unix 1767225600) + uptime,
   converted to Mac time (seconds since 1904-01-01, diff 2082844800).
   Same epoch as the mac68k kernel. */
#define BMX_MAC128K_UNIX_EPOCH 1767225600UL
#define BMX_MAC128K_UNIX2MAC 2082844800UL

LOCALVAR ui5b TrueEmulatedTime = 0;

#define MyInvTimeDivPow 16
#define MyInvTimeDiv (1 << MyInvTimeDivPow)
#define MyInvTimeDivMask (MyInvTimeDiv - 1)
/* 1000 / 60.14742 * 65536 */
#define MyInvTimeStep 1089590

LOCALVAR unsigned long LastTimeMs = 0;
LOCALVAR unsigned long NextIntTimeMs = 0;
LOCALVAR ui5b NextFracTime = 0;
LOCALVAR ui5b NewMacDateInSeconds = 0;

LOCALPROC IncrNextTime(void)
{
	NextFracTime += MyInvTimeStep;
	NextIntTimeMs += (unsigned long)(NextFracTime >> MyInvTimeDivPow);
	NextFracTime &= MyInvTimeDivMask;
}

LOCALPROC InitNextTime(unsigned long now_ms)
{
	NextIntTimeMs = now_ms;
	NextFracTime = 0;
	IncrNextTime();
}

LOCALFUNC blnr UpdateTrueEmulatedTime(void)
{
	unsigned long LatestMs =
		(unsigned long)(circle_get_ticks64() / 1000ULL);
	long TimeDiff;

	if (LatestMs != LastTimeMs) {
		NewMacDateInSeconds = BMX_MAC128K_UNIX_EPOCH
			+ BMX_MAC128K_UNIX2MAC + LatestMs / 1000UL;
		LastTimeMs = LatestMs;
		TimeDiff = (long)(LatestMs - NextIntTimeMs);
		if (TimeDiff >= 0) {
			if (TimeDiff > 256) {
				++TrueEmulatedTime;
				InitNextTime(LatestMs);
			} else {
				do {
					++TrueEmulatedTime;
					IncrNextTime();
					TimeDiff =
						(long)(LatestMs - NextIntTimeMs);
				} while (TimeDiff >= 0);
			}
			return trueblnr;
		} else {
			if (TimeDiff < -256) {
				InitNextTime(LatestMs);
			}
		}
	}

	return falseblnr;
}

LOCALFUNC blnr CheckDateTime(void)
{
	if (CurMacDateInSeconds != NewMacDateInSeconds) {
		CurMacDateInSeconds = NewMacDateInSeconds;
		return trueblnr;
	} else {
		return falseblnr;
	}
}

LOCALFUNC blnr InitLocationDat(void)
{
	unsigned long now_ms =
		(unsigned long)(circle_get_ticks64() / 1000ULL);

	LastTimeMs = now_ms;
	InitNextTime(now_ms);
	NewMacDateInSeconds = BMX_MAC128K_UNIX_EPOCH
		+ BMX_MAC128K_UNIX2MAC + now_ms / 1000UL;
	CurMacDateInSeconds = NewMacDateInSeconds;
	return trueblnr;
}

GLOBALOSGLUFUNC blnr ExtraTimeNotOver(void)
{
	UpdateTrueEmulatedTime();
	return TrueEmulatedTime == OnTrueTime;
}

/* --- main tick (input drain, menu, pacing) --- */

static int s_reboot_code = 0;

void bmx_mac128k_backend_request_reboot(int code)
{
	s_reboot_code = code;
	ForceMacOff = trueblnr;
}

int bmx_mac128k_backend_take_reboot(void)
{
	int code = s_reboot_code;
	s_reboot_code = 0;
	return code;
}

/* Common menu toggle (third_party/common/ui.c): opens the menu itself
   when the F12 ui_toggle_pending set by kbd.c fires. Same pump the
   mac68k kernel uses from its tick hook. */
extern void ui_handle_toggle_or_quick_func(void);

GLOBALOSGLUPROC WaitForNextTick(void)
{
	unsigned int ev;
	unsigned long now_ms;
	long wait_ms;

	/* Drain USB input; handle menu + reset requests on this thread. */
	ev = bmx_mac128k_poll_input();
	if (0 != (ev & BMX_MAC128K_EV_MENU)) {
		DisconnectKeyCodes(0);
		MyMouseButtonSet(falseblnr);
	}
	/* Fires the menu (via ui_toggle_pending from kbd.c) when F12 was
	   pressed; harmless flag check otherwise. */
	ui_handle_toggle_or_quick_func();
	if (0 != (ev & BMX_MAC128K_EV_RESET_SOFT)) {
		WantMacReset = trueblnr;
	}
	if (ForceMacOff) {
		return;
	}

	if (ExtraTimeNotOver()) {
		/* Emulation ahead of wall clock: sleep the remainder. */
		now_ms = (unsigned long)(circle_get_ticks64() / 1000ULL);
		wait_ms = (long)NextIntTimeMs - (long)now_ms;
		if (wait_ms > 0) {
			unsigned long start = (unsigned long)
				circle_get_ticks64();
			if (wait_ms > 32) {
				wait_ms = 32;
			}
			while ((unsigned long)circle_get_ticks64()
				- start < (unsigned long)wait_ms * 1000UL)
			{
			}
		}
		/* Re-check after sleeping; fall through regardless so a
		   menu hold or breakpoint cannot wedge the loop. */
	}

	(void) CheckDateTime();

	/* Wedge watchdog commented out (log quiet). */
	OnTrueTime = TrueEmulatedTime;
}

/* --- input entry points (called from the wrapper input drain) --- */

void bmx_mac128k_backend_key(int mkc, int down)
{
	if (mkc < 0 || mkc > 127) {
		return;
	}
	Keyboard_UpdateKeyMap((ui3r)mkc, down ? trueblnr : falseblnr);
}

static int s_mac_mouse_h = 256;
static int s_mac_mouse_v = 171;

void bmx_mac128k_backend_mouse_delta(int dh, int dv)
{
	s_mac_mouse_h += dh;
	s_mac_mouse_v += dv;
	if (s_mac_mouse_h < 0) {
		s_mac_mouse_h = 0;
	}
	if (s_mac_mouse_h > 511) {
		s_mac_mouse_h = 511;
	}
	if (s_mac_mouse_v < 0) {
		s_mac_mouse_v = 0;
	}
	if (s_mac_mouse_v > 341) {
		s_mac_mouse_v = 341;
	}
	MyMousePositionSet((ui4r)s_mac_mouse_h, (ui4r)s_mac_mouse_v);
}

void bmx_mac128k_backend_mouse_button(int down)
{
	MyMouseButtonSet(down ? trueblnr : falseblnr);
}

/* --- runtime disk insert/eject (menu, while emulating) --- */

int bmx_mac128k_backend_insert_disk(const char *path, int read_only)
{
	FILE *f;
	tDrive free_drive;
	blnr locked;

	if (NULL == path || 0 == path[0]) {
		return -1;
	}
	locked = read_only ? trueblnr : falseblnr;
	if (locked) {
		f = fopen(path, "rb");
	} else {
		f = fopen(path, "r+b");
		if (NULL == f) {
			f = fopen(path, "rb");
			locked = trueblnr;
		}
	}
	if (NULL == f) {
		return -1;
	}
	if (! FirstFreeDisk(&free_drive)) {
		fclose(f);
		return -1;
	}
	{
		unsigned int i = 0;
		Drives[free_drive] = f;
		DiskInsertNotify(free_drive, locked);
		while (0 != path[i]
			&& i < sizeof(s_inserted_path[0]) - 1)
		{
			s_inserted_path[free_drive][i] = path[i];
			++i;
		}
		s_inserted_path[free_drive][i] = 0;
	}
	return (int)free_drive;
}

int bmx_mac128k_backend_eject_disk(int drive)
{
	if (drive < 0 || drive >= (int)NumDrives) {
		return -1;
	}
	if (! vSonyIsInserted(drive)) {
		return -1;
	}
	if (mnvm_noErr != vSonyEject((tDrive)drive)) {
		return -1;
	}
	s_inserted_path[drive][0] = 0;
	return 0;
}

const char *bmx_mac128k_backend_disk_path(int drive)
{
	if (drive < 0 || drive >= (int)NumDrives) {
		return NULL;
	}
	if (! vSonyIsInserted(drive)) {
		return NULL;
	}
	return s_inserted_path[drive];
}

/* --- lifecycle --- */

#include "PROGMAIN.h"

static blnr s_memory_allocated = falseblnr;

int bmx_mac128k_backend_init(void)
{
	TrueEmulatedTime = 0;
	OnTrueTime = 0;
	ForceMacOff = falseblnr;
	WantMacReset = falseblnr;
	WantMacInterrupt = falseblnr;
	s_reboot_code = 0;
	/* Authentic 1x speed (7.8MHz 128K). The generated default is 8x
	   (SpeedValue 3), which overproduces audio 8:1 against the
	   blocking Circle drain and throttles the guest to ~quarter
	   speed (desktop needs ~900 ticks: 15s at 1x, 60s+ throttled).
	   At 1x the backend generates exactly 370 samples per wall tick,
	   which the drain paces precisely. */
	SpeedValue = 0;
#if EnableAutoSlow
	QuietTime = 0;
	QuietSubTicks = 0;
#endif
#if MySoundEnabled
	ThePlayOffset = 0;
	TheFillOffset = 0;
	TheWriteOffset = 0;
#endif
	MyMouseButtonSet(falseblnr);
	s_mac_mouse_h = 256;
	s_mac_mouse_v = 171;

	InitDrives();
	InitKeyCodes();
	if (! AllocMyMemory()) {
		return 0;
	}
	s_memory_allocated = trueblnr;
	if (! LoadMacRom()) {
		return 0;
	}
	(void) LoadInitialImages();
	if (! InitLocationDat()) {
		return 0;
	}
	ScreenChangedAll();
	return 1;
}

void bmx_mac128k_backend_run(void)
{
	/* Black letterbox from the first guest frame: the wrapper paints
	   stage fills (white) right before this call, so clear AFTER them. */
	bmx_mac128k_video_clear();
	ProgramMain();
}

void bmx_mac128k_backend_shutdown(void)
{
#if IncludePbufs
	UnInitPbufs();
#endif
	UnInitDrives();
	if (s_memory_allocated) {
		UnallocMyMemory();
		s_memory_allocated = falseblnr;
	}
}
