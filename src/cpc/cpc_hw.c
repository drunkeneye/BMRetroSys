//
// cpc_hw.c
//
// Amstrad CPC hardware module (Gate Array, CRTC 6845 bookkeeping, PPI 8255,
// memory paging, flat contention, port dispatch glue).
//
// This implements the ZEsarUX CPC machine interface that the vendored core
// files (cpu.c, operaciones.c) reference (see third_party/zesarux/
// machines/cpc.h), plus the helpers the BMX core loop and video renderer
// need. Behaviour follows the CPC hardware references (cpctech/cpcwiki
// Gate Array / PPI / CRTC docs) and mirrors the Caprice32/WinAPE model:
//
// - GA port 0x7Fxx, function by data bits 7:6 (00 pen, 01 ink, 10 mode/ROM,
//   11 RAM banking on 128K/PAL only).
// - RMR bit4=1 clears the interrupt and resets the 52-HSYNC counter;
//   bit3/bit2 are upper/lower ROM *disables*; bits1:0 video mode.
// - RAM configs C0..C7 (pages 0-3 = base 64K, 4-7 = extra 64K).
// - Interrupts: GA counts HSYNC, INT every 52; Z80 ack clears bit5
//   (counter &= 0x1F, >=32 HSYNC spacing); RMR bit4 clears + resets.
// - PPI 0xF4..0xF7: A = PSG bus, B = cassette/vsync/links (input),
//   C = AY control + motor + cassette out + keyboard line, control word
//   + bit set/reset. Keyboard matrix lives behind AY register 14.
// - CRTC registers stored raw; MA/row/raster tracking lives here and is
//   consumed by cpc_video.c through the cpc_video_* getters below.
// - Video RAM is always the base 64K (paging never affects the display).
// - No contention on CPC: flat T-state accounting.
// - FDC (PD765) is not compiled: pd765_enabled stays 0, so the vendored
//   port handlers return 0xFF and fa7e writes are ignored (logged once).
//

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu.h"
#include "contend.h"
#include "operaciones.h"
#include "ay38912.h"
#include "machines/cpc.h"

#include "cpc_tape_iface.h"

// Forwarded to the BMX SD log (cpc_debug.cpp); declared here to keep this
// translation unit free of C++ headers.
extern void cpc_debug_log(const char *fmt, ...);
// Video palette refresh hook (cpc_video.cpp, C linkage).
extern void cpc_video_palette_dirty(void);
extern void cpu_panic(char *mensaje);

// ---------------------------------------------------------------------------
// Memory
// ---------------------------------------------------------------------------

// Base 64K (video + CPU window pages 0..3) and the 6128 second 64K.
static z80_byte s_ram_base[65536];
static z80_byte s_ram_extra[65536];
// ROM images: OS 16K, BASIC 16K, AMSDOS 16K (664/6128 only).
static z80_byte s_rom_os[16384];
static z80_byte s_rom_basic[16384];
static z80_byte s_rom_amsdos[16384];
static int s_has_amsdos = 0;
static int s_is_128k = 0;

// Compatibility exports (referenced by linked cpu.c paths).
// NOTE: memoria_spectrum and malloc_machine live in the vendored cpu.c
// (compiled in); only the CPC tables below are owned here.
z80_byte *cpc_rom_mem_table[4] = {NULL, NULL, NULL, NULL};

// 16K CPU windows (operaciones.c indexes these directly).
z80_byte *cpc_memory_paged_read[4] = {NULL, NULL, NULL, NULL};
z80_byte *cpc_memory_paged_write[4] = {NULL, NULL, NULL, NULL};

// RAM page pointers: 0..3 = base windows, 4..7 = extra bank.
static z80_byte *s_ram_pages[8] = {
    s_ram_base, s_ram_base + 16384, s_ram_base + 32768, s_ram_base + 49152,
    s_ram_extra, s_ram_extra + 16384, s_ram_extra + 32768,
    s_ram_extra + 49152,
};

// RAM configs C0..C7: which page backs each CPU window.
static const uint8_t s_ram_configs[8][4] = {
    {0, 1, 2, 3},  // C0 normal
    {0, 1, 2, 7},  // C1 upper window -> extra page 3
    {4, 5, 6, 7},  // C2 all extra
    {0, 3, 2, 7},  // C3 (quirky: 4000->base page 3)
    {0, 4, 2, 3},  // C4 4000 -> extra page 0
    {0, 5, 2, 3},  // C5 4000 -> extra page 1
    {0, 6, 2, 3},  // C6 4000 -> extra page 2
    {0, 7, 2, 3},  // C7 4000 -> extra page 3
};

 // ---------------------------------------------------------------------------
// Gate Array + paging state
// ---------------------------------------------------------------------------

static uint8_t s_ga_pens[17];       // selected HW colour per pen (16+border)
static uint8_t s_ga_pen_select = 0;  // 0..15 pens, 16 = border
static uint8_t s_ga_mode = 1;        // video mode 0..3
static uint8_t s_ga_rom_config = 0;  // RMR value (bit2/3 = ROM disables)
static uint8_t s_ga_ram_config = 0;  // MMR value (128K only)
static uint8_t s_upper_rom = 0;      // ROMSEL via 0xDFxx
static uint8_t s_hsync_counter = 0;  // GA R52 (0..51)
static uint8_t s_int_pending = 0;    // INT line asserted
z80_byte cpc_port_df = 0;

// CRTC register file + select latch (timing derived in cpc_core.c).
z80_byte cpc_crtc_registers[18] = {0};
z80_byte cpc_crtc_last_selected_register = 0;

// MA tracking (word address, 14-bit). Reloaded from R12/R13 at frame start,
// advanced by R1 per character row (Caprice32/WinAPE model, matches the
// firmware-manual 80-byte row pitch).
static unsigned s_ma_base = 0x3000;
static unsigned s_raster = 0;  // 0..R9 within the current char row
static unsigned s_row = 0;     // char row 0..R4
static unsigned s_vsync_lines_left = 0;
static unsigned s_adjust_left = 0;  // R5 vertical-adjust countdown

// Recompute the CPU window pointers from GA state. Called on every mapping
// change (RMR/MMR/DF writes) and from cpc_set_memory_pages().
static void recompute_pages(void) {
  const uint8_t *cfg = s_ram_configs[s_ga_ram_config & 7];
  for (int w = 0; w < 4; w++) {
    cpc_memory_paged_write[w] = s_ram_pages[cfg[w]];
    cpc_memory_paged_read[w] = s_ram_pages[cfg[w]];
  }
  const int lower_on = (s_ga_rom_config & 0x04) == 0;
  const int upper_on = (s_ga_rom_config & 0x08) == 0;
  if (lower_on) {
    cpc_memory_paged_read[0] = s_rom_os;
  }
  if (upper_on) {
    // Caprice32-compatible fallback: an unpopulated slot reads as BASIC
    // (a valid ROM image), never RAM or open bus. Real hardware floats,
    // but a valid image keeps the firmware ROM walker safe.
    z80_byte *upper = s_rom_basic;
    if (s_upper_rom == 7 && s_has_amsdos) {
      upper = s_rom_amsdos;
    }
    cpc_memory_paged_read[3] = upper;
  }
}

void cpc_init_memory_tables(void) {
  cpc_rom_mem_table[0] = s_rom_os;
  cpc_rom_mem_table[1] = s_rom_basic;
  cpc_rom_mem_table[2] = s_has_amsdos ? s_rom_amsdos : NULL;
  cpc_rom_mem_table[3] = NULL;
  recompute_pages();
}

void cpc_set_memory_pages(void) { recompute_pages(); }

// cpc_machine.cpp fills these before calling cpc_init_memory_tables().
z80_byte *cpc_hw_ram_base(void) { return s_ram_base; }
z80_byte *cpc_hw_ram_extra(void) { return s_ram_extra; }
z80_byte *cpc_hw_rom_os(void) { return s_rom_os; }
z80_byte *cpc_hw_rom_basic(void) { return s_rom_basic; }
z80_byte *cpc_hw_rom_amsdos(void) { return s_rom_amsdos; }
void cpc_hw_set_model(int is_128k, int has_amsdos) {
  s_is_128k = is_128k;
  s_has_amsdos = has_amsdos;
  if (!is_128k) {
    s_ga_ram_config = 0;
  }
}

void cpc_reset(void) {
  memset(s_ga_pens, 0, sizeof(s_ga_pens));
  s_ga_pen_select = 0;
  s_ga_mode = 1;
  s_ga_rom_config = 0;
  s_ga_ram_config = 0;
  s_upper_rom = 0;
  cpc_port_df = 0;
  s_hsync_counter = 0;
  s_int_pending = 0;
  memset(cpc_crtc_registers, 0, sizeof(uint8_t) * 18);
  cpc_crtc_last_selected_register = 0;
  s_ma_base = 0x3000;
  s_raster = 0;
  s_row = 0;
  s_vsync_lines_left = 0;
  s_adjust_left = 0;
  // PPI latches intentionally not cleared here (declared below): BSS starts
  // zeroed and the firmware re-inits the PPI (mode set resets ports) on
  // every boot path.
  recompute_pages();
}

// ---------------------------------------------------------------------------
// Gate Array ports
// ---------------------------------------------------------------------------

void cpc_out_port_gate(z80_byte value) {
  const unsigned fn = (unsigned)(value >> 6) & 3U;
  if (fn == 0) {
    // PENR: bit4 set -> border, else pen 0..15.
    s_ga_pen_select = (value & 0x10) ? 16 : (value & 0x0F);
  } else if (fn == 1) {
    // INKR: 5-bit hardware colour for the selected pen.
    s_ga_pens[s_ga_pen_select] = value & 0x1F;
    cpc_video_palette_dirty();
  } else if (fn == 2) {
    // RMR: mode + ROM config + interrupt control.
    s_ga_mode = value & 0x03;
    s_ga_rom_config = value;
    if (value & 0x10) {
      // Clear INT request, reset the 52-HSYNC counter.
      s_int_pending = 0;
      s_hsync_counter = 0;
    }
    recompute_pages();
  } else {
    // MMR: RAM banking (128K PAL / expansion only).
    if (!s_is_128k) {
      return;
    }
    if ((value & 0x38) != 0) {
      cpc_debug_log("cpc: MMR bank bits set (%02x), only bank 0 present", value);
    }
    s_ga_ram_config = value & 0x07;
    recompute_pages();
  }
}

void cpc_out_port_df(z80_byte value) {
  s_upper_rom = value;
  cpc_port_df = value;
  recompute_pages();
}

// Floppy motor flipflop (FA7E bit0 = all motors on/off, CPCWiki).
// Forwarded to the FDC module (cpc_fdc.cpp, C linkage).
extern void cpc_fdc_set_motor(int on);

void cpc_out_port_fa7e(z80_byte value) {
  cpc_fdc_set_motor((value & 1) ? 1 : 0);
}

void cpc_out_port_crtc(z80_int puerto, z80_byte value) {
  const unsigned sub = ((unsigned)puerto >> 8) & 3U;
  if (sub == 0) {
    // 0xBCxx: register select.
    cpc_crtc_last_selected_register = value & 31U;
  } else if (sub == 1) {
    // 0xBDxx: register write (0..17; R8/R10/R11/R14+ stored, unused).
    if (cpc_crtc_last_selected_register < 18) {
      cpc_crtc_registers[cpc_crtc_last_selected_register] = value;
    }
  }
  // 0xBExx/0xBFxx (status/read): unreadable on CRTC type 0, ignore.
}

// ---------------------------------------------------------------------------
// PPI 8255 + keyboard matrix + AY routing
// ---------------------------------------------------------------------------

// Keyboard matrix: pressed[row][bit], row = PPI Port C low nibble 0..9.
static uint8_t s_keys[10][8] = {{0}};
static uint8_t s_ppi_a = 0;      // Port A latch (PSG databus)
static uint8_t s_ppi_c = 0;      // Port C latch
static uint8_t s_ppi_porta_in = 0;  // Port A direction (control bit4)
static uint8_t s_ay_selected = 0;
static int s_motor_logged = -1;  // last logged motor state

// Cassette motor transitions (PPI Port C bit4) are the visible heartbeat
// of tape loading; log changes for the debug log.
static void note_motor(void) {
  int motor = (s_ppi_c >> 4) & 1;
  if (motor != s_motor_logged) {
    s_motor_logged = motor;
    cpc_debug_log("cpc: cassette motor %s", motor ? "ON" : "OFF");
  }
}

void cpc_key_set(int row, int bit, int pressed) {
  if (row < 0 || row > 9 || bit < 0 || bit > 7) {
    return;
  }
  s_keys[row][bit] = pressed ? 1 : 0;
}

void cpc_key_clear_all(void) { memset(s_keys, 0, sizeof(s_keys)); }

// Cassette motor state for the tape player (PPI Port C bit4, 1 = on).
int cpc_hw_motor_on(void) { return (s_ppi_c >> 4) & 1; }

// Envelope cycle (ay38912.c; reset on R13 writes like the upstream port
// handler does).
extern int ciclo_envolvente[];

// AY register write through the vendored chip state. NOTE: out_port_ay()
// is the *Spectrum* port handler (it decodes 0xFFFD/0xBFFD port numbers),
// so CPC PPI writes go straight to the register file + derived tables,
// mirroring the 0xBFFD branch of out_port_ay.
static void ay_write_reg(uint8_t reg, uint8_t val) {
  if (reg > 13) {
    // R14/R15 (I/O ports): no output hardware modelled; ignore writes.
    return;
  }
  ay_3_8912_registro_sel[0] = reg;
  ay_3_8912_registros[0][reg] = val;
  ay_establece_frecuencias_todos_canales();
  if (reg == 13) {
    ciclo_envolvente[0] = 0;
  }
}

static uint8_t ay_read_reg(uint8_t reg) {
  if (reg == 14) {
    // Keyboard matrix row selected by PPI Port C bits 3..0; 0 = pressed.
    // Lines 10..15 float high. Joystick 0/1 are wired into lines 9/6 by
    // cpc_input (same cells as the matrix, like real hardware).
    //
    // Bit order is direct (bit b -> bit b), matching the MAME CPC matrix
    // (kbrow.N bit masks). An earlier table had the bits mirrored and a
    // read-side mirror compensated letters while breaking Shift/Return/
    // Esc; both are now sourced straight from MAME, no mirror anywhere.
    const unsigned line = s_ppi_c & 0x0F;
    if (line > 9) {
      return 0xFF;
    }
    uint8_t v = 0;
    for (int b = 0; b < 8; b++) {
      if (!s_keys[line][b]) {
        v |= (uint8_t)(1U << b);
      }
    }
    return v;
  }
  return 0xFF;
}

z80_byte cpc_in_ppi(z80_byte puerto_h) {
  // PPI function from address bits A9:A8 (ports F4/F5/F6/F7).
  const unsigned sub = (unsigned)puerto_h & 3U;
  if (sub == 0) {
    // Port A: PSG bus (input mode) or output latch.
    if (!s_ppi_porta_in) {
      return s_ppi_a;
    }
    const unsigned func = (s_ppi_c >> 6) & 3U;
    if (func == 1) {
      return ay_read_reg(s_ay_selected);
    }
    return 0xFF;
  }
  if (sub == 1) {
    // Port B: cassette in, printer busy, links, VSYNC.
    // bit7 tape (idle high), bit6 printer not-ready, bit5 /EXP,
    // bits4-1 links (floating high), bit0 CRTC VSYNC.
    uint8_t v = 0x7E;
    if (cpc_tape_level()) {
      v |= 0x80;
    }
    if (s_vsync_lines_left > 0) {
      v |= 0x01;
    }
    return v;
  }
  if (sub == 2) {
    return s_ppi_c;
  }
  return 0xFF;
}

void cpc_out_ppi(z80_byte puerto_h, z80_byte value) {
  const unsigned sub = (unsigned)puerto_h & 3U;
  if (sub == 0) {
    s_ppi_a = value;
    return;
  }
  if (sub == 2) {
    const unsigned old_c = s_ppi_c;
    s_ppi_c = value;
    const unsigned old_func = (old_c >> 6) & 3U;
    const unsigned func = (value >> 6) & 3U;
    if (func == 3) {
      s_ay_selected = s_ppi_a;
    } else if (func == 2 && old_func != 2) {
      // Latch on rising edge into write mode (matches firmware sequence).
      ay_write_reg(s_ay_selected, s_ppi_a);
    }
    note_motor();
    return;
  }
  if (sub == 3) {
    if (value & 0x80) {
      // Mode set: only Port A direction matters on CPC; the write
      // resets ports A/B/C to 0 (8255 behaviour, firmware relies on it
      // during PPI init, which also drops the motor + cassette out).
      s_ppi_porta_in = (value & 0x10) ? 1 : 0;
      s_ppi_a = 0;
      s_ppi_c = 0;
    } else {
      // Bit set/reset on Port C.
      const unsigned bit = ((unsigned)value >> 1) & 7U;
      if (value & 0x01) {
        s_ppi_c |= (uint8_t)(1U << bit);
      } else {
        s_ppi_c &= (uint8_t) ~(1U << bit);
      }
      if (bit == 7 || bit == 6) {
        const unsigned func = (s_ppi_c >> 6) & 3U;
        if (func == 3) {
          s_ay_selected = s_ppi_a;
        } else if (func == 2) {
          ay_write_reg(s_ay_selected, s_ppi_a);
        }
      }
    }
    note_motor();
    return;
  }
  // Port B is input-only; ignore writes.
}

// ---------------------------------------------------------------------------
// CRTC/GA line + frame stepping (called from cpc_core.c)
// ---------------------------------------------------------------------------

// Advance one scanline (256 T-states): HSYNC/INT counting, VSYNC window,
// raster/row advance, MA update. Returns non-zero at frame end.
int cpc_hw_end_of_line(void) {
  // HSYNC -> GA R52 counter -> 300 Hz interrupt.
  s_hsync_counter++;
  if (s_hsync_counter >= 52) {
    s_hsync_counter = 0;
    s_int_pending = 1;
  }
  if (s_vsync_lines_left > 0) {
    s_vsync_lines_left--;
  }

  const unsigned r9 = cpc_crtc_registers[9] & 31U;
  const unsigned r1 = cpc_crtc_registers[1];
  const unsigned r4 = cpc_crtc_registers[4] & 127U;
  const unsigned r5 = cpc_crtc_registers[5] & 31U;
  const unsigned r6 = cpc_crtc_registers[6] & 127U;
  const unsigned r7 = cpc_crtc_registers[7] & 127U;
  const unsigned r3 = cpc_crtc_registers[3];

  s_raster++;
  int frame_end = 0;
  if (s_raster > r9) {
    s_raster = 0;
    s_row++;
    // MA advances by the displayed char count per row (firmware-manual
    // 80-byte pitch; Caprice32/WinAPE model).
    s_ma_base = (s_ma_base + (r1 > 64 ? 64 : r1)) & 0x3FFF;
    if (s_row == r7) {
      // VSYNC width in scanlines (R3 high nibble, type 0).
      unsigned vsw = (r3 >> 4) & 15U;
      s_vsync_lines_left = vsw != 0 ? vsw : 3;
    }
    if (s_row > r4) {
      // Vertical total + adjust (R5 extra scanlines).
      if (s_adjust_left == 0) {
        s_adjust_left = r5;
      }
      if (s_adjust_left > 0) {
        s_adjust_left--;
        s_row = r4 + 1;  // hold past the end during adjust
      } else {
        frame_end = 1;
      }
    }
  }
  (void)r6;
  return frame_end;
}

void cpc_hw_end_of_frame(void) {
  // Reload MA from R12/R13 (page + offset); reset row/raster.
  const unsigned r12 = cpc_crtc_registers[12];
  const unsigned r13 = cpc_crtc_registers[13];
  s_ma_base = (((unsigned)r12 << 8) | r13) & 0x3FFF;
  s_row = 0;
  s_raster = 0;
}

// Z80 interrupt service query: raise only when the CPU allows it (IM1 via
// iff1). Ack clears the GA request and bit5 of the counter.
int cpc_hw_irq_pending(void) { return s_int_pending; }

void cpc_hw_irq_ack(void) {
  s_int_pending = 0;
  s_hsync_counter &= 0x1F;
}

// Snapshot of frame geometry for the video renderer (clamped).
unsigned cpc_hw_frame_lines(void) {
  const unsigned r4 = cpc_crtc_registers[4] & 127U;
  const unsigned r9 = cpc_crtc_registers[9] & 31U;
  const unsigned r5 = cpc_crtc_registers[5] & 31U;
  unsigned lines = (r4 + 1) * (r9 + 1) + r5;
  if (lines < 100) {
    lines = 100;
  }
  if (lines > 400) {
    lines = 400;
  }
  return lines;
}

// ---------------------------------------------------------------------------
// Getters for cpc_video.c
// ---------------------------------------------------------------------------

unsigned cpc_video_ma(void) { return s_ma_base; }unsigned cpc_video_raster(void) { return s_raster; }
unsigned cpc_video_row(void) { return s_row; }
unsigned cpc_video_mode(void) { return s_ga_mode & 3U; }
unsigned cpc_video_r1(void) { return cpc_crtc_registers[1]; }
unsigned cpc_video_r6(void) { return cpc_crtc_registers[6] & 127U; }
unsigned cpc_video_r12(void) { return cpc_crtc_registers[12]; }
const uint8_t *cpc_video_pens(void) { return s_ga_pens; }
const z80_byte *cpc_video_ram(void) { return s_ram_base; }
int cpc_video_vsync(void) { return s_vsync_lines_left > 0; }

// ---------------------------------------------------------------------------
// Flat contention (CPC has none). NOTE: contend.h declares these as
// pointers/table pointers, so they are defined as such here (contend.c is
// deliberately not compiled for CPC).
// ---------------------------------------------------------------------------

z80_byte contend_pages_actual[4] = {0, 0, 0, 0};
// Never dereferenced (all pages zero), but the symbols must exist.
z80_byte *contend_table = NULL;
z80_byte *contend_table_no_mreq = NULL;

static void contend_flat(z80_int direccion, int time) {
  (void)direccion;
  t_estados += time;
}

static void ula_port_noop(z80_int port) { (void)port; }

static int ula_in_noop(z80_int puerto) {
  (void)puerto;
  return 255;
}

void (*contend_read)(z80_int direccion, int time) = contend_flat;
void (*contend_read_no_mreq)(z80_int direccion, int time) = contend_flat;
void (*contend_write_no_mreq)(z80_int direccion, int time) = contend_flat;
void (*ula_contend_port_early)(z80_int port) = ula_port_noop;
void (*ula_contend_port_late)(z80_int port) = ula_port_noop;
int (*port_from_ula)(z80_int puerto) = ula_in_noop;

// FDC stays disabled in v1 (no disc emulation); the vendored port code
// returns 0xFF / ignores writes on this flag.
z80_bit pd765_enabled = {0};
