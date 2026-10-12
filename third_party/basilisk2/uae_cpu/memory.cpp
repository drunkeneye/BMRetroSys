/*
 * UAE - The Un*x Amiga Emulator
 *
 * Memory management
 *
 * (c) 1995 Bernd Schmidt
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <stdio.h>
#include <stdlib.h>

#include "sysdeps.h"

#include "cpu_emulation.h"
#include "main.h"
#include "video.h"

#include "m68k.h"
#include "memory.h"
#include "readcpu.h"
#include "newcpu.h"

#if !REAL_ADDRESSING && !DIRECT_ADDRESSING

static bool illegal_mem = false;

#ifdef SAVE_MEMORY_BANKS
addrbank *mem_banks[65536];
#else
addrbank mem_banks[65536];
#endif

#ifdef WORDS_BIGENDIAN
# define swap_words(X) (X)
#else
# define swap_words(X) (((X) >> 16) | ((X) << 16))
#endif

#ifdef NO_INLINE_MEMORY_ACCESS
uae_u32 longget (uaecptr addr)
{
	return call_mem_get_func (get_mem_bank (addr).lget, addr);
}
uae_u32 wordget (uaecptr addr)
{
	return call_mem_get_func (get_mem_bank (addr).wget, addr);
}
uae_u32 byteget (uaecptr addr)
{
	return call_mem_get_func (get_mem_bank (addr).bget, addr);
}
void longput (uaecptr addr, uae_u32 l)
{
	call_mem_put_func (get_mem_bank (addr).lput, addr, l);
}
void wordput (uaecptr addr, uae_u32 w)
{
	call_mem_put_func (get_mem_bank (addr).wput, addr, w);
}
void byteput (uaecptr addr, uae_u32 b)
{
	call_mem_put_func (get_mem_bank (addr).bput, addr, b);
}
#endif


#ifdef BMX_LAB
#include <stdio.h>
static unsigned lab_trace_n = 0;
static unsigned lab_trace_wlo = 0;
static int lab_quiet_on(void)
{
	static int cached = -1;
	if (cached < 0) {
		const char *e = getenv("LAB_QUIET");
		cached = (e != NULL && e[0] == '1') ? 1 : 0;
	}
	return cached;
}
static void lab_trace_acc(const char *op, uaecptr addr, unsigned val)
{
  if (lab_quiet_on()) return;
  if (lab_trace_n >= 2500) return;
  uaecptr lab_pc_ = m68k_getpc();
  int is_write = (op[5] == 'P');
  // Always log low-RAM writes (event-flag producers live anywhere).
  if (is_write && addr < 0x20 && lab_trace_wlo < 400) {
    lab_trace_wlo++;
    printf("labtrace: %s addr=0x%08x val=0x%x pc=0x%08x\n", op,
           (unsigned)addr, val, (unsigned)lab_pc_);
    return;
  }
  int in_loop = (lab_pc_ >= 0x40842000u && lab_pc_ < 0x40844000u);
  int interesting = in_loop &&
                    ((addr < 0x100) || (addr >= 0xfc00 && addr < 0xfe00));
  if (!interesting) return;
  lab_trace_n++;
  printf("labtrace: %s addr=0x%08x val=0x%x pc=0x%08x\n", op,
         (unsigned)addr, val, (unsigned)m68k_getpc());
}
static unsigned lab_dummy_n = 0;
static void lab_trace_dummy(const char *op, uaecptr addr, unsigned val)
{
  if (lab_quiet_on()) return;
  if (lab_dummy_n >= 2500) return;
  lab_dummy_n++;
  printf("labtrace: %s addr=0x%08x val=0x%x pc=0x%08x\n", op,
         (unsigned)addr, val, (unsigned)m68k_getpc());
}
#endif


/* A dummy bank that only contains zeros */

// BMX file-log diagnostics: count unmapped/hardware reads so one boot log
// proves whether the stalled ROM guest polls missing hardware (VIA/SCC/SCSI
// all land here since BMX maps only RAM/ROM/frame). Logged from the mac68k
// tick; last addr names the chip.
unsigned long bmx_dummy_reads = 0;
unsigned int bmx_dummy_last_addr = 0;
unsigned long bmx_dummy_writes = 0;
unsigned int bmx_dummy_last_waddr = 0;
// First-seen unmapped read addresses (early-boot hardware presence probes
// steer init branches; the ORDER+SET names the steering). Bounded ring.
// PC-tagged (which guest instruction probed) so one debug boot proves
// whether the stall loop itself touches hardware (it doesn't - registers
// and lowmem only) vs a driver preamble. Logged from the mac68k tick.
unsigned int bmx_dummy_first_addrs[64] = {0};
unsigned int bmx_dummy_first_pcs[64] = {0};
unsigned char bmx_dummy_first_rw[64] = {0};  // 0 = read, 1 = write
unsigned int bmx_dummy_first_n = 0;
void bmx_dummy_reset(void)
{
	bmx_dummy_reads = 0;
	bmx_dummy_last_addr = 0;
	bmx_dummy_writes = 0;
	bmx_dummy_last_waddr = 0;
	bmx_dummy_first_n = 0;
}
static void bmx_dummy_note(unsigned int addr, unsigned char rw)
{
	for (unsigned i = 0; i < bmx_dummy_first_n; i++) {
		if (bmx_dummy_first_addrs[i] == addr && bmx_dummy_first_rw[i] == rw)
			return;
	}
	if (bmx_dummy_first_n < 64) {
		bmx_dummy_first_addrs[bmx_dummy_first_n] = addr;
		bmx_dummy_first_pcs[bmx_dummy_first_n] = (unsigned int)m68k_getpc();
		bmx_dummy_first_rw[bmx_dummy_first_n] = rw;
		bmx_dummy_first_n++;
	}
}
static void bmx_dummy_note_read(unsigned int addr)
{
	bmx_dummy_note(addr, 0);
}
static void bmx_dummy_note_write(unsigned int addr)
{
	bmx_dummy_note(addr, 1);
}

static uae_u32 REGPARAM2 dummy_lget (uaecptr) REGPARAM;
static uae_u32 REGPARAM2 dummy_wget (uaecptr) REGPARAM;
static uae_u32 REGPARAM2 dummy_bget (uaecptr) REGPARAM;
static void REGPARAM2 dummy_lput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM2 dummy_wput (uaecptr, uae_u32) REGPARAM;
static void REGPARAM2 dummy_bput (uaecptr, uae_u32) REGPARAM;

// Open-bus behavior for empty NuBus/PDS slot space: real Mac hardware reads
// all-ones from an unpopulated slot (data bus pullups; Slot Manager tests
// for -1 = "no card"). The old flat zero fabricated a "card present with a
// zeroed declROM", whose parser never terminates (black-screen park with
// frozen regs, no I/O, post-driver-install). RAM-extension space and real
// hardware registers keep returning 0 (sizing and early init depend on it).
static inline bool bmx_is_empty_slot_space(unsigned int addr)
{
	unsigned int top = addr & 0xF0000000u;
	return top == 0x20000000u || top == 0x90000000u || top == 0xF0000000u;
}
#ifdef BMX_LAB
// Lab-only: revert open-bus -1 to upstream flat 0 (tests whether the BMX
// slot-space change steers the stall). Gated by LAB_UPSTREAMDUMMY=1.
static int lab_upstreamdummy_on(void)
{
	static int cached = -1;
	if (cached < 0) {
		const char *e = getenv("LAB_UPSTREAMDUMMY");
		cached = (e != NULL && e[0] == '1') ? 1 : 0;
	}
	return cached;
}
// Lab-only: sticky byte registers for VIA-probe diagnosis (writes stick,
// reads echo last write). Gated by env LAB_VIAECHO=1 at runtime.
#include <stdlib.h>
static unsigned char lab_viaecho_vals[16];
static unsigned int lab_viaecho_addrs[16];
static unsigned lab_viaecho_n = 0;
static int lab_viaecho_on(void)
{
	static int cached = -1;
	if (cached < 0) {
		const char *e = getenv("LAB_VIAECHO");
		cached = (e != NULL && e[0] == '1') ? 1 : 0;
	}
	return cached;
}
static void lab_viaecho_put(unsigned int addr, unsigned int v)
{
	for (unsigned i = 0; i < lab_viaecho_n; i++) {
		if (lab_viaecho_addrs[i] == addr) {
			lab_viaecho_vals[i] = (unsigned char)v;
			return;
		}
	}
	if (lab_viaecho_n < 16) {
		lab_viaecho_addrs[lab_viaecho_n] = addr;
		lab_viaecho_vals[lab_viaecho_n] = (unsigned char)v;
		lab_viaecho_n++;
	}
}
static int lab_viaecho_get(unsigned int addr, unsigned int *out)
{
	for (unsigned i = 0; i < lab_viaecho_n; i++) {
		if (lab_viaecho_addrs[i] == addr) {
			*out = lab_viaecho_vals[i];
			return 1;
		}
	}
	return 0;
}
#endif

uae_u32 REGPARAM2 dummy_lget (uaecptr addr)
{
	if (illegal_mem)
		write_log ("Illegal lget at %08x\n", addr);

#ifdef BMX_LAB
	lab_trace_dummy("DUM_LGET", addr, 0);
#endif
	bmx_dummy_reads++;
	bmx_dummy_last_addr = (unsigned int)addr;
	bmx_dummy_note_read((unsigned int)addr);
#ifdef BMX_LAB
	if (!lab_upstreamdummy_on() && bmx_is_empty_slot_space((unsigned int)addr))
#else
	if (bmx_is_empty_slot_space((unsigned int)addr))
#endif
		return 0xFFFFFFFFu;
	return 0;
}

uae_u32 REGPARAM2 dummy_wget (uaecptr addr)
{
	if (illegal_mem)
		write_log ("Illegal wget at %08x\n", addr);

#ifdef BMX_LAB
	lab_trace_dummy("DUM_WGET", addr, 0);
#endif
	bmx_dummy_reads++;
	bmx_dummy_last_addr = (unsigned int)addr;
	bmx_dummy_note_read((unsigned int)addr);
#ifdef BMX_LAB
	if (!lab_upstreamdummy_on() && bmx_is_empty_slot_space((unsigned int)addr))
#else
	if (bmx_is_empty_slot_space((unsigned int)addr))
#endif
		return 0xFFFFu;
	return 0;
}

uae_u32 REGPARAM2 dummy_bget (uaecptr addr)
{
	if (illegal_mem)
		write_log ("Illegal bget at %08x\n", addr);

#ifdef BMX_LAB
	lab_trace_dummy("DUM_BGET", addr, 0);
#endif
	bmx_dummy_reads++;
	bmx_dummy_last_addr = (unsigned int)addr;
	bmx_dummy_note_read((unsigned int)addr);
#ifdef BMX_LAB
	if (!lab_upstreamdummy_on() && bmx_is_empty_slot_space((unsigned int)addr))
#else
	if (bmx_is_empty_slot_space((unsigned int)addr))
#endif
		return 0xFFu;
#ifdef BMX_LAB
	{
		unsigned int echo_v = 0;
		if (lab_viaecho_on() && lab_viaecho_get((unsigned int)addr, &echo_v))
			return echo_v;
	}
#endif
	return 0;
}

void REGPARAM2 dummy_lput (uaecptr addr, uae_u32 l)
{
	if (illegal_mem)
		write_log ("Illegal lput at %08x\n", addr);
#ifdef BMX_LAB
	lab_trace_dummy("DUM_LPUT", addr, l);
#endif
	bmx_dummy_writes++;
	bmx_dummy_last_waddr = (unsigned int)addr;
	bmx_dummy_note_write((unsigned int)addr);
}
void REGPARAM2 dummy_wput (uaecptr addr, uae_u32 w)
{
	if (illegal_mem)
		write_log ("Illegal wput at %08x\n", addr);
#ifdef BMX_LAB
	lab_trace_dummy("DUM_WPUT", addr, w);
#endif
	bmx_dummy_writes++;
	bmx_dummy_last_waddr = (unsigned int)addr;
	bmx_dummy_note_write((unsigned int)addr);
}
void REGPARAM2 dummy_bput (uaecptr addr, uae_u32 b)
{
	if (illegal_mem)
		write_log ("Illegal bput at %08x\n", addr);
#ifdef BMX_LAB
	lab_trace_dummy("DUM_BPUT", addr, b);
	if (lab_viaecho_on())
		lab_viaecho_put((unsigned int)addr, b);
#endif
	bmx_dummy_writes++;
	bmx_dummy_last_waddr = (unsigned int)addr;
	bmx_dummy_note_write((unsigned int)addr);
}


/* Mac RAM (32 bit addressing) */

static uae_u32 REGPARAM2 ram_lget(uaecptr) REGPARAM;
static uae_u32 REGPARAM2 ram_wget(uaecptr) REGPARAM;
static uae_u32 REGPARAM2 ram_bget(uaecptr) REGPARAM;
static void REGPARAM2 ram_lput(uaecptr, uae_u32) REGPARAM;
static void REGPARAM2 ram_wput(uaecptr, uae_u32) REGPARAM;
static void REGPARAM2 ram_bput(uaecptr, uae_u32) REGPARAM;
static uae_u8 *REGPARAM2 ram_xlate(uaecptr addr) REGPARAM;

static uintptr RAMBaseDiff;	// RAMBaseHost - RAMBaseMac

uae_u32 REGPARAM2 ram_lget(uaecptr addr)
{
	uae_u32 *m;
#ifdef BMX_LAB
	uae_u32 lab_v_;
	m = (uae_u32 *)(RAMBaseDiff + addr);
	lab_v_ = do_get_mem_long(m);
	lab_trace_acc("RAM_LGET", addr, lab_v_);
	return lab_v_;
#else
	m = (uae_u32 *)(RAMBaseDiff + addr);
	return do_get_mem_long(m);
#endif
}

uae_u32 REGPARAM2 ram_wget(uaecptr addr)
{
	uae_u16 *m;
#ifdef BMX_LAB
	uae_u16 lab_v_;
	m = (uae_u16 *)(RAMBaseDiff + addr);
	lab_v_ = do_get_mem_word(m);
	lab_trace_acc("RAM_WGET", addr, lab_v_);
	return lab_v_;
#else
	m = (uae_u16 *)(RAMBaseDiff + addr);
	return do_get_mem_word(m);
#endif
}

uae_u32 REGPARAM2 ram_bget(uaecptr addr)
{
	return (uae_u32)*(uae_u8 *)(RAMBaseDiff + addr);
}

void REGPARAM2 ram_lput(uaecptr addr, uae_u32 l)
{

#ifdef LAB_WATCHLO
	if (addr < 32) {
		extern unsigned long lab_watchlo_n;
		if (lab_watchlo_n < 200) {
			printf("labwatch: %s addr=0x%08x pc=0x%08x\n",
			       __func__, (unsigned)addr, (unsigned)m68k_getpc());
			lab_watchlo_n++;
		}
	}
#endif
	uae_u32 *m;
#ifdef BMX_LAB
	lab_trace_acc("RAM_LPUT", addr, l);
#endif
	m = (uae_u32 *)(RAMBaseDiff + addr);
	do_put_mem_long(m, l);
}

void REGPARAM2 ram_wput(uaecptr addr, uae_u32 w)
{

#ifdef LAB_WATCHLO
	if (addr < 32) {
		extern unsigned long lab_watchlo_n;
		if (lab_watchlo_n < 200) {
			printf("labwatch: %s addr=0x%08x pc=0x%08x\n",
			       __func__, (unsigned)addr, (unsigned)m68k_getpc());
			lab_watchlo_n++;
		}
	}
#endif
	uae_u16 *m;
#ifdef BMX_LAB
	lab_trace_acc("RAM_WPUT", addr, w);
#endif
	m = (uae_u16 *)(RAMBaseDiff + addr);
	do_put_mem_word(m, w);
}

void REGPARAM2 ram_bput(uaecptr addr, uae_u32 b)
{

#ifdef LAB_WATCHLO
	if (addr < 32) {
		extern unsigned long lab_watchlo_n;
		if (lab_watchlo_n < 200) {
			printf("labwatch: %s addr=0x%08x pc=0x%08x\n",
			       __func__, (unsigned)addr, (unsigned)m68k_getpc());
			lab_watchlo_n++;
		}
	}
#endif
#ifdef BMX_LAB
	lab_trace_acc("RAM_BPUT", addr, b);
#endif
	*(uae_u8 *)(RAMBaseDiff + addr) = b;
}

uae_u8 *REGPARAM2 ram_xlate(uaecptr addr)
{
	return (uae_u8 *)(RAMBaseDiff + addr);
}

/* Mac RAM (24 bit addressing) */

static uae_u32 REGPARAM2 ram24_lget(uaecptr) REGPARAM;
static uae_u32 REGPARAM2 ram24_wget(uaecptr) REGPARAM;
static uae_u32 REGPARAM2 ram24_bget(uaecptr) REGPARAM;
static void REGPARAM2 ram24_lput(uaecptr, uae_u32) REGPARAM;
static void REGPARAM2 ram24_wput(uaecptr, uae_u32) REGPARAM;
static void REGPARAM2 ram24_bput(uaecptr, uae_u32) REGPARAM;
static uae_u8 *REGPARAM2 ram24_xlate(uaecptr addr) REGPARAM;

uae_u32 REGPARAM2 ram24_lget(uaecptr addr)
{
	uae_u32 *m;
	m = (uae_u32 *)(RAMBaseDiff + (addr & 0xffffff));
	return do_get_mem_long(m);
}

uae_u32 REGPARAM2 ram24_wget(uaecptr addr)
{
	uae_u16 *m;
	m = (uae_u16 *)(RAMBaseDiff + (addr & 0xffffff));
	return do_get_mem_word(m);
}

uae_u32 REGPARAM2 ram24_bget(uaecptr addr)
{
	return (uae_u32)*(uae_u8 *)(RAMBaseDiff + (addr & 0xffffff));
}

void REGPARAM2 ram24_lput(uaecptr addr, uae_u32 l)
{
	uae_u32 *m;
	m = (uae_u32 *)(RAMBaseDiff + (addr & 0xffffff));
	do_put_mem_long(m, l);
}

void REGPARAM2 ram24_wput(uaecptr addr, uae_u32 w)
{
	uae_u16 *m;
	m = (uae_u16 *)(RAMBaseDiff + (addr & 0xffffff));
	do_put_mem_word(m, w);
}

void REGPARAM2 ram24_bput(uaecptr addr, uae_u32 b)
{
	*(uae_u8 *)(RAMBaseDiff + (addr & 0xffffff)) = b;
}

uae_u8 *REGPARAM2 ram24_xlate(uaecptr addr)
{
	return (uae_u8 *)(RAMBaseDiff + (addr & 0xffffff));
}

/* Mac ROM (32 bit addressing) */

static uae_u32 REGPARAM2 rom_lget(uaecptr) REGPARAM;
static uae_u32 REGPARAM2 rom_wget(uaecptr) REGPARAM;
static uae_u32 REGPARAM2 rom_bget(uaecptr) REGPARAM;
static void REGPARAM2 rom_lput(uaecptr, uae_u32) REGPARAM;
static void REGPARAM2 rom_wput(uaecptr, uae_u32) REGPARAM;
static void REGPARAM2 rom_bput(uaecptr, uae_u32) REGPARAM;
static uae_u8 *REGPARAM2 rom_xlate(uaecptr addr) REGPARAM;

static uintptr ROMBaseDiff;	// ROMBaseHost - ROMBaseMac

uae_u32 REGPARAM2 rom_lget(uaecptr addr)
{
	uae_u32 *m;
	m = (uae_u32 *)(ROMBaseDiff + addr);
	return do_get_mem_long(m);
}

uae_u32 REGPARAM2 rom_wget(uaecptr addr)
{
	uae_u16 *m;
	m = (uae_u16 *)(ROMBaseDiff + addr);
	return do_get_mem_word(m);
}

uae_u32 REGPARAM2 rom_bget(uaecptr addr)
{
	return (uae_u32)*(uae_u8 *)(ROMBaseDiff + addr);
}

void REGPARAM2 rom_lput(uaecptr addr, uae_u32 b)
{
	if (illegal_mem)
		write_log ("Illegal ROM lput at %08x\n", addr);
}

void REGPARAM2 rom_wput(uaecptr addr, uae_u32 b)
{
	if (illegal_mem)
		write_log ("Illegal ROM wput at %08x\n", addr);
}

void REGPARAM2 rom_bput(uaecptr addr, uae_u32 b)
{
	if (illegal_mem)
		write_log ("Illegal ROM bput at %08x\n", addr);
}

uae_u8 *REGPARAM2 rom_xlate(uaecptr addr)
{
	return (uae_u8 *)(ROMBaseDiff + addr);
}

/* Mac ROM (24 bit addressing) */

static uae_u32 REGPARAM2 rom24_lget(uaecptr) REGPARAM;
static uae_u32 REGPARAM2 rom24_wget(uaecptr) REGPARAM;
static uae_u32 REGPARAM2 rom24_bget(uaecptr) REGPARAM;
static uae_u8 *REGPARAM2 rom24_xlate(uaecptr addr) REGPARAM;

uae_u32 REGPARAM2 rom24_lget(uaecptr addr)
{
	uae_u32 *m;
	m = (uae_u32 *)(ROMBaseDiff + (addr & 0xffffff));
	return do_get_mem_long(m);
}

uae_u32 REGPARAM2 rom24_wget(uaecptr addr)
{
	uae_u16 *m;
	m = (uae_u16 *)(ROMBaseDiff + (addr & 0xffffff));
	return do_get_mem_word(m);
}

uae_u32 REGPARAM2 rom24_bget(uaecptr addr)
{
	return (uae_u32)*(uae_u8 *)(ROMBaseDiff + (addr & 0xffffff));
}

uae_u8 *REGPARAM2 rom24_xlate(uaecptr addr)
{
	return (uae_u8 *)(ROMBaseDiff + (addr & 0xffffff));
}

/* Frame buffer */

static uae_u32 REGPARAM2 frame_direct_lget(uaecptr) REGPARAM;
static uae_u32 REGPARAM2 frame_direct_wget(uaecptr) REGPARAM;
static uae_u32 REGPARAM2 frame_direct_bget(uaecptr) REGPARAM;
static void REGPARAM2 frame_direct_lput(uaecptr, uae_u32) REGPARAM;
static void REGPARAM2 frame_direct_wput(uaecptr, uae_u32) REGPARAM;
static void REGPARAM2 frame_direct_bput(uaecptr, uae_u32) REGPARAM;

static uae_u32 REGPARAM2 frame_host_555_lget(uaecptr) REGPARAM;
static uae_u32 REGPARAM2 frame_host_555_wget(uaecptr) REGPARAM;
static void REGPARAM2 frame_host_555_lput(uaecptr, uae_u32) REGPARAM;
static void REGPARAM2 frame_host_555_wput(uaecptr, uae_u32) REGPARAM;

static uae_u32 REGPARAM2 frame_host_565_lget(uaecptr) REGPARAM;
static uae_u32 REGPARAM2 frame_host_565_wget(uaecptr) REGPARAM;
static void REGPARAM2 frame_host_565_lput(uaecptr, uae_u32) REGPARAM;
static void REGPARAM2 frame_host_565_wput(uaecptr, uae_u32) REGPARAM;

static uae_u32 REGPARAM2 frame_host_888_lget(uaecptr) REGPARAM;
static void REGPARAM2 frame_host_888_lput(uaecptr, uae_u32) REGPARAM;

static uae_u8 *REGPARAM2 frame_xlate(uaecptr addr) REGPARAM;

static uintptr FrameBaseDiff;	// MacFrameBaseHost - MacFrameBaseMac

uae_u32 REGPARAM2 frame_direct_lget(uaecptr addr)
{
	uae_u32 *m;
	m = (uae_u32 *)(FrameBaseDiff + addr);
	return do_get_mem_long(m);
}

uae_u32 REGPARAM2 frame_direct_wget(uaecptr addr)
{
	uae_u16 *m;
	m = (uae_u16 *)(FrameBaseDiff + addr);
	return do_get_mem_word(m);
}

uae_u32 REGPARAM2 frame_direct_bget(uaecptr addr)
{
	return (uae_u32)*(uae_u8 *)(FrameBaseDiff + addr);
}

void REGPARAM2 frame_direct_lput(uaecptr addr, uae_u32 l)
{
	uae_u32 *m;
	m = (uae_u32 *)(FrameBaseDiff + addr);
	do_put_mem_long(m, l);
}

void REGPARAM2 frame_direct_wput(uaecptr addr, uae_u32 w)
{
	uae_u16 *m;
	m = (uae_u16 *)(FrameBaseDiff + addr);
	do_put_mem_word(m, w);
}

void REGPARAM2 frame_direct_bput(uaecptr addr, uae_u32 b)
{
	*(uae_u8 *)(FrameBaseDiff + addr) = b;
}

uae_u32 REGPARAM2 frame_host_555_lget(uaecptr addr)
{
	uae_u32 *m, l;
	m = (uae_u32 *)(FrameBaseDiff + addr);
	l = *m;
	return swap_words(l);
}

uae_u32 REGPARAM2 frame_host_555_wget(uaecptr addr)
{
	uae_u16 *m;
	m = (uae_u16 *)(FrameBaseDiff + addr);
	return *m;
}

void REGPARAM2 frame_host_555_lput(uaecptr addr, uae_u32 l)
{
	uae_u32 *m;
	m = (uae_u32 *)(FrameBaseDiff + addr);
	*m = swap_words(l);
}

void REGPARAM2 frame_host_555_wput(uaecptr addr, uae_u32 w)
{
	uae_u16 *m;
	m = (uae_u16 *)(FrameBaseDiff + addr);
	*m = w;
}

uae_u32 REGPARAM2 frame_host_565_lget(uaecptr addr)
{
	uae_u32 *m, l;
	m = (uae_u32 *)(FrameBaseDiff + addr);
	l = *m;
	l = (l & 0x001f001f) | ((l >> 1) & 0x7fe07fe0);
	return swap_words(l);
}

uae_u32 REGPARAM2 frame_host_565_wget(uaecptr addr)
{
	uae_u16 *m, w;
	m = (uae_u16 *)(FrameBaseDiff + addr);
	w = *m;
	return (w & 0x1f) | ((w >> 1) & 0x7fe0);
}

void REGPARAM2 frame_host_565_lput(uaecptr addr, uae_u32 l)
{
	uae_u32 *m;
	m = (uae_u32 *)(FrameBaseDiff + addr);
	l = (l & 0x001f001f) | ((l << 1) & 0xffc0ffc0);
	*m = swap_words(l);
}

void REGPARAM2 frame_host_565_wput(uaecptr addr, uae_u32 w)
{
	uae_u16 *m;
	m = (uae_u16 *)(FrameBaseDiff + addr);
	*m = (w & 0x1f) | ((w << 1) & 0xffc0);
}

uae_u32 REGPARAM2 frame_host_888_lget(uaecptr addr)
{
	uae_u32 *m, l;
	m = (uae_u32 *)(FrameBaseDiff + addr);
	return *m;
}

void REGPARAM2 frame_host_888_lput(uaecptr addr, uae_u32 l)
{
	uae_u32 *m;
	m = (uae_u32 *)(MacFrameBaseHost + addr - MacFrameBaseMac);
	*m = l;
}

uae_u8 *REGPARAM2 frame_xlate(uaecptr addr)
{
	return (uae_u8 *)(FrameBaseDiff + addr);
}

/* Mac framebuffer RAM (24 bit addressing) */
static uae_u32 REGPARAM2 frame24_lget(uaecptr) REGPARAM;
static uae_u32 REGPARAM2 frame24_wget(uaecptr) REGPARAM;
static uae_u32 REGPARAM2 frame24_bget(uaecptr) REGPARAM;
static void REGPARAM2 frame24_lput(uaecptr, uae_u32) REGPARAM;
static void REGPARAM2 frame24_wput(uaecptr, uae_u32) REGPARAM;
static void REGPARAM2 frame24_bput(uaecptr, uae_u32) REGPARAM;

/*
 * Q: Why the magic number 0xa700 and 0xfc80?
 *
 * A: The M68K CPU used by the earlier Macintosh models such as
 * Macintosh 128K or Macintosh SE, its address space is limited
 * to 2^24 = 16MiB. The RAM limits to 4MiB.
 *
 * With 512x342 1 bit per pixel screen, the size of the frame buffer
 * is 0x5580 bytes.
 *
 * In Macintosh 128K [1], the frame buffer address is mapped from
 * 0x1A700 to 0x1FC7F.
 *
 * In Macintosh SE [2], the frame buffer address is mapped from
 * 0x3FA700 to 0x3FFC7F.
 *
 * The frame24_xxx memory banks mapping used the magic number to
 * retrieve the offset. The memory write operation does twice:
 * one for the guest OS and another for the host OS (the write operation
 * above MacFrameBaseHost).
 *
 *
 * See:
 *  [1] The Apple Macintosh Computer. http://www.1000bit.it/support/articoli/apple/mac128.pdf
 *  [2] Capturing Mac SE's video from PDS. http://synack.net/~bbraun/sevideo/
 *
 */

uae_u32 REGPARAM2 frame24_lget(uaecptr addr)
{
	uae_u32 *m;
	m = (uae_u32 *)(FrameBaseDiff + (addr & 0xffffff));
	return do_get_mem_long(m);
}

uae_u32 REGPARAM2 frame24_wget(uaecptr addr)
{
	uae_u16 *m;
	m = (uae_u16 *)(FrameBaseDiff + (addr & 0xffffff));
	return do_get_mem_word(m);
}

uae_u32 REGPARAM2 frame24_bget(uaecptr addr)
{
	return (uae_u32)*(uae_u8 *)(FrameBaseDiff + (addr & 0xffffff));
}

void REGPARAM2 frame24_lput(uaecptr addr, uae_u32 l)
{
	uae_u32 *m;
	m = (uae_u32 *)(FrameBaseDiff + (addr & 0xffffffff));
	do_put_mem_long(m, l);
}

void REGPARAM2 frame24_wput(uaecptr addr, uae_u32 w)
{
	uae_u16 *m;
	m = (uae_u16 *)(FrameBaseDiff + (addr & 0xffffffff));
	do_put_mem_word(m, w);
}

void REGPARAM2 frame24_bput(uaecptr addr, uae_u32 b)
{
	*(uae_u8 *)(FrameBaseDiff + (addr & 0xffffffff)) = b;
}

/* Default memory access functions */

uae_u8 *REGPARAM2 default_xlate (uaecptr a)
{
	write_log("Your Mac program just did something terribly stupid\n");
	return NULL;
}

/* Address banks */

addrbank dummy_bank = {
	dummy_lget, dummy_wget, dummy_bget,
	dummy_lput, dummy_wput, dummy_bput,
	default_xlate
};

addrbank ram_bank = {
	ram_lget, ram_wget, ram_bget,
	ram_lput, ram_wput, ram_bput,
	ram_xlate
};

addrbank ram24_bank = {
	ram24_lget, ram24_wget, ram24_bget,
	ram24_lput, ram24_wput, ram24_bput,
	ram24_xlate
};

addrbank rom_bank = {
	rom_lget, rom_wget, rom_bget,
	rom_lput, rom_wput, rom_bput,
	rom_xlate
};

addrbank rom24_bank = {
	rom24_lget, rom24_wget, rom24_bget,
	rom_lput, rom_wput, rom_bput,
	rom24_xlate
};

addrbank frame_direct_bank = {
	frame_direct_lget, frame_direct_wget, frame_direct_bget,
	frame_direct_lput, frame_direct_wput, frame_direct_bput,
	frame_xlate
};

#ifdef USE_SCRATCHMEM_SUBTERFUGE
// BMX scratch bank: RAM-backed Mac range that rewritten hardware bases
// (VIA/SCC/IWM/SCSI in the ROM decoder tables) point at. Plain RAM
// semantics (writes stick) so ROM presence probes terminate; kept outside
// RAM/ROM/frame/slot space so sizing and Slot Manager never see it.
static uintptr ScratchBaseDiff = 0;
static uint32 ScratchMacBase = 0;
static uint32 ScratchMacSize = 0;
static uae_u32 REGPARAM2 scratch_lget(uaecptr) REGPARAM;
static uae_u32 REGPARAM2 scratch_wget(uaecptr) REGPARAM;
static uae_u32 REGPARAM2 scratch_bget(uaecptr) REGPARAM;
static void REGPARAM2 scratch_lput(uaecptr, uae_u32) REGPARAM;
static void REGPARAM2 scratch_wput(uaecptr, uae_u32) REGPARAM;
static void REGPARAM2 scratch_bput(uaecptr, uae_u32) REGPARAM;
static uae_u8 *REGPARAM2 scratch_xlate(uaecptr addr) REGPARAM;
static uae_u32 REGPARAM2 scratch_lget(uaecptr addr)
{
	uae_u32 *m = (uae_u32 *)(ScratchBaseDiff + addr);
	return do_get_mem_long(m);
}
static uae_u32 REGPARAM2 scratch_wget(uaecptr addr)
{
	uae_u16 *m = (uae_u16 *)(ScratchBaseDiff + addr);
	return do_get_mem_word(m);
}
static uae_u32 REGPARAM2 scratch_bget(uaecptr addr)
{
	return (uae_u32)*(uae_u8 *)(ScratchBaseDiff + addr);
}
static void REGPARAM2 scratch_lput(uaecptr addr, uae_u32 l)
{
	uae_u32 *m = (uae_u32 *)(ScratchBaseDiff + addr);
	do_put_mem_long(m, l);
}
static void REGPARAM2 scratch_wput(uaecptr addr, uae_u32 w)
{
	uae_u16 *m = (uae_u16 *)(ScratchBaseDiff + addr);
	do_put_mem_word(m, w);
}
static void REGPARAM2 scratch_bput(uaecptr addr, uae_u32 b)
{
	*(uae_u8 *)(ScratchBaseDiff + addr) = b;
}
static uae_u8 *REGPARAM2 scratch_xlate(uaecptr addr)
{
	return (uae_u8 *)(ScratchBaseDiff + addr);
}
addrbank scratch_bank = {
	scratch_lget, scratch_wget, scratch_bget,
	scratch_lput, scratch_wput, scratch_bput,
	scratch_xlate
};
// Backing store, provided by the platform backend (lab stub / Pi backend).
extern uint8 *ScratchMem;
static uint8 *ScratchMemHostBase = NULL;
// Fixed Mac range for scratch (128KB at 0x0F000000, outside RAM/ROM/frame,
// I/O and slot space). ScratchMem (middle) is what PatchHWBases publishes.
static const uint32 SCRATCH_MAC_BASE = 0x0F000000u;
static const uint32 SCRATCH_MAC_SIZE = 0x00020000u;
void bmx_scratch_init(uint8 *host_base)
{
	ScratchMemHostBase = host_base;
	ScratchMem = host_base + SCRATCH_MAC_SIZE / 2;
	ScratchBaseDiff = (uintptr)host_base - (uintptr)SCRATCH_MAC_BASE;
	ScratchMacBase = SCRATCH_MAC_BASE;
	ScratchMacSize = SCRATCH_MAC_SIZE;
}
uae_u32 get_virtual_address(uae_u8 *addr)
{
	uintptr a = (uintptr)addr;
	if (ScratchMemHostBase != NULL && a >= (uintptr)ScratchMemHostBase &&
	    a < (uintptr)ScratchMemHostBase + SCRATCH_MAC_SIZE)
		return (uae_u32)(SCRATCH_MAC_BASE + (a - (uintptr)ScratchMemHostBase));
	if (a >= (uintptr)RAMBaseHost && a < (uintptr)RAMBaseHost + RAMSize)
		return (uae_u32)(RAMBaseMac + (a - (uintptr)RAMBaseHost));
	if (a >= (uintptr)ROMBaseHost && a < (uintptr)ROMBaseHost + ROMSize)
		return (uae_u32)(ROMBaseMac + (a - (uintptr)ROMBaseHost));
	if (MacFrameBaseHost != NULL && MacFrameSize != 0 && a >= (uintptr)MacFrameBaseHost &&
	    a < (uintptr)MacFrameBaseHost + MacFrameSize)
		return (uae_u32)((TwentyFourBitAddressing ? MacFrameBaseMac24Bit : MacFrameBaseMac) +
		                 (a - (uintptr)MacFrameBaseHost));
	return 0;
}
#endif

addrbank frame_host_555_bank = {
	frame_host_555_lget, frame_host_555_wget, frame_direct_bget,
	frame_host_555_lput, frame_host_555_wput, frame_direct_bput,
	frame_xlate
};

addrbank frame_host_565_bank = {
	frame_host_565_lget, frame_host_565_wget, frame_direct_bget,
	frame_host_565_lput, frame_host_565_wput, frame_direct_bput,
	frame_xlate
};

addrbank frame_host_888_bank = {
	frame_host_888_lget, frame_direct_wget, frame_direct_bget,
	frame_host_888_lput, frame_direct_wput, frame_direct_bput,
	frame_xlate
};

addrbank frame24_bank = {
	frame24_lget, frame24_wget, frame24_bget,
	frame24_lput, frame24_wput, frame24_bput,
	default_xlate
};

void memory_init(void)
{
	for(long i=0; i<65536; i++)
		put_mem_bank(i<<16, &dummy_bank);

	// Limit RAM size to not overlap ROM
	uint32 ram_size = RAMSize > ROMBaseMac ? ROMBaseMac : RAMSize;

	RAMBaseDiff = (uintptr)RAMBaseHost - (uintptr)RAMBaseMac;
	ROMBaseDiff = (uintptr)ROMBaseHost - (uintptr)ROMBaseMac;
	if (TwentyFourBitAddressing)
		FrameBaseDiff = (uintptr)MacFrameBaseHost - (uintptr)MacFrameBaseMac24Bit;
	else
		FrameBaseDiff = (uintptr)MacFrameBaseHost - (uintptr)MacFrameBaseMac;

	// Map RAM, ROM and display
	if (TwentyFourBitAddressing) {
		map_banks(&ram24_bank, RAMBaseMac >> 16, ram_size >> 16);
		map_banks(&rom24_bank, ROMBaseMac >> 16, ROMSize >> 16);

		// Map frame buffer at end of RAM.
		map_banks(&frame24_bank, MacFrameBaseMac24Bit >> 16, (MacFrameSize >> 16) + 1);
	} else {
		map_banks(&ram_bank, RAMBaseMac >> 16, ram_size >> 16);
		map_banks(&rom_bank, ROMBaseMac >> 16, ROMSize >> 16);
#ifdef USE_SCRATCHMEM_SUBTERFUGE
		// Scratch RAM for rewritten hardware bases (outside everything).
		if (ScratchMemHostBase != NULL)
			map_banks(&scratch_bank, SCRATCH_MAC_BASE >> 16, SCRATCH_MAC_SIZE >> 16);
#endif

		// Map frame buffer
		switch (MacFrameLayout) {
		case FLAYOUT_DIRECT:
			map_banks(&frame_direct_bank, MacFrameBaseMac >> 16, (MacFrameSize >> 16) + 1);
			break;
		case FLAYOUT_HOST_555:
			map_banks(&frame_host_555_bank, MacFrameBaseMac >> 16, (MacFrameSize >> 16) + 1);
			break;
		case FLAYOUT_HOST_565:
			map_banks(&frame_host_565_bank, MacFrameBaseMac >> 16, (MacFrameSize >> 16) + 1);
			break;
		case FLAYOUT_HOST_888:
			map_banks(&frame_host_888_bank, MacFrameBaseMac >> 16, (MacFrameSize >> 16) + 1);
			break;
		}
	}
}

void map_banks(addrbank *bank, int start, int size)
{
	int bnr;
	unsigned long int hioffs = 0, endhioffs = 0x100;

	if (start >= 0x100) {
		for (bnr = start; bnr < start + size; bnr++)
			put_mem_bank (bnr << 16, bank);
		return;
	}
	if (TwentyFourBitAddressing) endhioffs = 0x10000;
	for (hioffs = 0; hioffs < endhioffs; hioffs += 0x100)
		for (bnr = start; bnr < start+size; bnr++)
			put_mem_bank((bnr + hioffs) << 16, bank);
}

#endif /* !REAL_ADDRESSING && !DIRECT_ADDRESSING */

