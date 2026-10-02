//
// msx_core.c
//
// MSX1 frame loop (one cpu_core_loop_msx() call per Z80 opcode; a frame
// ends after 312 scanlines). Structure mirrors cpc_core.c and the
// upstream core_msx.c reference (without its ZX Vision/zeng/snapshot
// baggage):
//
// - per opcode: .cas trap (PC=E1h/E4h, instant BIOS-level load) or fetch
//   + dispatch through the vendored Z80 tables (poke/peek/fetch/lee/
//   out_port are the msx1 variants assigned by msx_machine.c).
// - T-state driven scanlines (229 T each, 3579545/312/50): per-line VDP
//   render (msx_video_scanline), AY sample + step, audio send.
// - frame end: VDP status F flag (when IE0 is set) + maskable interrupt,
//   serviced below (EI delay, HALT release, IM1 vector 0x38, iff clear).
//
// Pacing lives outside (msx_timer pace after video+sound), so there is no
// idle here: every call executes.
//

#include "cpu.h"
#include "operaciones.h"
#include "contend.h"
#include "debug.h"
#include "cpus/z80_codsinpr.h"
#include "audio/audio.h"
#include "soundchips/ay38912.h"
#include "msx.h"
#include "vdp_9918a.h"

// SD-card log (msx_debug.cpp, C linkage; msx_debug.h is C++-only).
extern void msx_debug_log(const char *fmt, ...);

// Implemented in msx_video (C linkage).
extern void msx_video_scanline(void);
extern void msx_video_frame_start(void);

// Flat contention (MSX has none on RAM/ROM; matches upstream
// contend_read_msx1: T-states just elapse). NOTE: contend.h declares these
// as pointers, so they are defined as such here (contend.c is not linked;
// same pattern as cpc_hw.c).
static void contend_flat(z80_int direccion, int time) {
  (void)direccion;
  t_estados += time;
}

void (*contend_read)(z80_int direccion, int time) = contend_flat;
void (*contend_read_no_mreq)(z80_int direccion, int time) = contend_flat;
void (*contend_write_no_mreq)(z80_int direccion, int time) = contend_flat;

// Port contention hooks called by the vendored MSX port wrappers
// (out_port_msx1/lee_puerto_msx1 call these on EVERY access; upstream
// msx1 behavior is flat timing: early +1T, late +2T).
static void ula_contend_early_flat(z80_int port) {
  (void)port;
  t_estados++;
}

static void ula_contend_late_flat(z80_int port) {
  (void)port;
  t_estados += 2;
}

void (*ula_contend_port_early)(z80_int port) = ula_contend_early_flat;
void (*ula_contend_port_late)(z80_int port) = ula_contend_late_flat;

static unsigned long s_next_line_t = 229;
static unsigned long s_lines_done = 0;
// Emulated video frame counter (the RunFrameLoop watches this advance).
unsigned int frames_video_total_infinito = 0;

// Resync line clock after a machine reset or model switch (t_estados
// restarts at 0; a stale horizon would stall video/audio/INT for part of
// a frame). Called from msx_machine.c.
void msx_core_reset_clocks(void) {
  s_next_line_t = 229;
  s_lines_done = 0;
}

// LD A,I / LD A,R leave P/V = IFF (NMOS quirk): an interrupt taken right
// after them must see PV cleared.
static void interrupcion_si_despues_lda_ir_local(z80_byte byte_leido) {
  if (byte_leido == 237) {
    if (pref237_opcode_leido == 87 || pref237_opcode_leido == 95) {
      Z80_FLAGS &= (255 - FLAG_PV);
    }
  }
}

void cpu_core_loop_msx(void) {
  z80_byte byte_leido = 0;

  // One-boot breadcrumbs: first-32 opcode trace + 4K progress markers.
  // If the boot wedges, the last line pinpoints the stage (fetch?
  // dispatch? scanline? frame end?).
  static unsigned long s_ops = 0;
  static int s_traced = 0;

  // Cassette fast-load trap (vendored msx_cas_load emulates TAPION/TAPIN
  // straight from the file, then RETs).
  if (msx_cas_load_detect()) {
    msx_cas_load();
  } else {
    contend_read(reg_pc, 4);
    byte_leido = fetch_opcode();
    if (s_traced < 32) {
      msx_debug_log("msx: op %lu pc=%04x byte=%02x t=%d", s_ops, reg_pc,
                    byte_leido, t_estados);
      s_traced++;
    }
    reg_pc++;
    reg_r++;

    z80_no_ejecutado_block_opcodes();
    codsinpr[byte_leido]();
  }
  s_ops++;
  (void)s_ops;

  // Scanline stepping on T-state crossings (229 T per line).
  while ((unsigned long)t_estados >= s_next_line_t) {
    s_next_line_t += 229UL;
    s_lines_done++;

    // Per-line video FIRST (raster state of the line just elapsed).
    msx_video_scanline();

    // AY sample + step per scanline (312 samples/frame into the shared
    // null-driver mixer, drained by msx_sound.c).
    {
      int iz = (int)(signed char)da_output_ay_izquierdo();
      int de = (int)(signed char)da_output_ay_derecho();
      if (audiovolume != 100) {
        iz = audio_adjust_volume(iz);
        de = audio_adjust_volume(de);
      }
      if (iz > 127) {
        iz = 127;
      } else if (iz < -128) {
        iz = -128;
      }
      if (de > 127) {
        de = 127;
      } else if (de < -128) {
        de = -128;
      }
      audio_send_stereo_sample((char)iz, (char)de);
    }
    ay_chip_siguiente_ciclo();

    if (s_lines_done >= 312) {
      // End of frame: VDP status F flag (when IE0 set) + frame IRQ.
      frames_video_total_infinito++;
      t_estados -= (int)(s_lines_done * 229UL);
      if (t_estados < 0) {
        t_estados = 0;
      }
      s_lines_done = 0;
      s_next_line_t = (unsigned long)t_estados + 229UL;
      if (vdp_9918a_registers[1] & 32) {
        vdp_9918a_status_register |= 128;
      }
      interrupcion_maskable_generada.v = 1;
      msx_video_frame_start();
    }
  }

  // Maskable interrupt (frame IRQ, IM1 like the firmware expects).
  // Mirrors the vendored sequence (EI one-instruction delay via opcode
  // 0xFB = 251, HALT release, vector 0x38).
  if (interrupcion_maskable_generada.v && iff1.v && byte_leido != 251) {
    z80_adjust_flags_interrupt_block_opcode();
    debug_anota_retorno_step_maskable();
    interrupcion_maskable_generada.v = 0;
    if (z80_halt_signal.v) {
      z80_halt_signal.v = 0;
    }
    interrupcion_si_despues_lda_ir_local(byte_leido);
    push_valor(reg_pc, PUSH_VALUE_TYPE_MASKABLE_INTERRUPT);
    reg_r++;
    iff1.v = iff2.v = 0;
    if (im_mode == 0 || im_mode == 1) {
      cpu_common_jump_im01();
    } else {
      z80_int temp_i = get_im2_interrupt_vector();
      z80_byte dir_l = peek_byte(temp_i++);
      z80_byte dir_h = peek_byte(temp_i);
      reg_pc = value_8_to_16(dir_h, dir_l);
      t_estados += 7;
    }
  }
}
