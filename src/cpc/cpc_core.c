//
// cpc_core.c
//
// Amstrad CPC frame loop (one cpu_core_loop_cpc() call per Z80 opcode; a
// frame ends when the CRTC line count wraps). Structure mirrors the
// vendored core_reduced_spectrum.c, minus everything Spectrum (no ULA
// contention, no rainbow store, no in-core pacing waits, no snapshots):
//
// - per opcode: fetch + dispatch through the vendored Z80 tables
//   (poke/peek/fetch/lee/out_port are the cpc_* variants assigned by
//   cpc_machine.c).
// - T-state driven scanlines (256 T each): HSYNC/INT counting, VSYNC,
//   raster/row/MA tracking (cpc_hw_end_of_line), per-line video render
//   (cpc_video_scanline), AY sample + step, audio send.
// - maskable interrupt service mirrors the vendored sequence (EI delay,
//   HALT release, IM1 vector 0x38, iff clear).
// - frame end when the CRTC total elapses (overscan-aware via R4/R5/R9).
//
// Pacing lives outside (cpc_timer pace after video+sound), so there is no
// esperando-style idle here: every call executes.
//

#include "cpu.h"
#include "operaciones.h"
#include "contend.h"
#include "debug.h"
#include "cpus/z80_codsinpr.h"
#include "audio/audio.h"
#include "soundchips/ay38912.h"

#include "cpc_tape_iface.h"

// Implemented in cpc_hw.c.
extern int cpc_hw_end_of_line(void);
extern void cpc_hw_end_of_frame(void);
extern int cpc_hw_irq_pending(void);
extern void cpc_hw_irq_ack(void);
extern unsigned cpc_hw_frame_lines(void);
// Implemented in cpc_video.c (C linkage).
extern void cpc_video_scanline(void);
extern void cpc_video_frame_start(void);
// Tape audio contribution, -1/0/+1 (cpc_tape.c).
extern int cpc_tape_audio(void);

static unsigned long s_next_line_t = 256;
static unsigned long s_lines_done = 0;
static unsigned long s_frame_lines = 312;
// Emulated video frame counter (the RunFrameLoop watches this advance,
// exactly like frames_video_total_infinito in the Spectrum kernel).
unsigned int frames_video_total_infinito = 0;
// Last seen T-state count for tape timetable advancement.
static unsigned long s_last_tape_t = 0;

// Resync line/tape clocks after a machine reset or model switch (t_estados
// restarts at 0; stale horizons would stall video/audio/INT for part of a
// frame). Called from cpc_machine.c.
void cpc_core_reset_clocks(void) {
  s_next_line_t = 256;
  s_lines_done = 0;
  s_frame_lines = 312;
  s_last_tape_t = 0;
}

// LD A,I / LD A,R leave P/V = IFF (NMOS quirk): an interrupt taken right
// after them must see PV cleared. Mirrors the vendored
// interrupcion_si_despues_lda_ir() (which lives in the uncompiled
// core_spectrum.c).
static void interrupcion_si_despues_lda_ir_local(z80_byte byte_leido) {
  if (byte_leido == 237) {
    if (pref237_opcode_leido == 87 || pref237_opcode_leido == 95) {
      Z80_FLAGS &= (255 - FLAG_PV);
    }
  }
}

void cpu_core_loop_cpc(void) {
  contend_read(reg_pc, 4);
  z80_byte byte_leido = fetch_opcode();
  reg_pc++;
  reg_r++;

  z80_no_ejecutado_block_opcodes();
  codsinpr[byte_leido]();

  // Tape timetable follows consumed T-states (per-opcode granularity so
  // short custom-turbo pulses stay accurate). t_estados rewinds at frame
  // end (see below); skip the single wrapping update.
  {
    unsigned long now = (unsigned long)t_estados;
    if (now >= s_last_tape_t) {
      unsigned delta = (unsigned)(now - s_last_tape_t);
      if (delta != 0 && delta < 1000000U) {
        cpc_tape_advance(delta);
      }
    }
    s_last_tape_t = now;
  }

  // Scanline stepping on T-state crossings (256 T per 64 us line).
  while ((unsigned long)t_estados >= s_next_line_t) {
    s_next_line_t += 256UL;
    s_lines_done++;

    // Per-line video FIRST (raster bars / splits need the state of the
    // line that just elapsed); the advance below moves to the next line.
    cpc_video_scanline();

    const int frame_end = cpc_hw_end_of_line();

    // AY sample + step per scanline (312 samples/frame into the shared
    // null-driver mixer, drained by cpc_sound.c like the ZX kernel).
    {
      int iz = (int)(signed char)da_output_ay_izquierdo();
      int de = (int)(signed char)da_output_ay_derecho();
      if (audiovolume != 100) {
        iz = audio_adjust_volume(iz);
        de = audio_adjust_volume(de);
      }
      const int tp = cpc_tape_audio();
      iz += tp;
      de += tp;
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

    if (frame_end || s_lines_done >= s_frame_lines) {
      // End of the CRTC frame: reload MA, re-derive the line count
      // (demos reprogram R4/R5/R9 for overscan).
      frames_video_total_infinito++;
      t_estados -= (int)(s_lines_done * 256UL);
      if (t_estados < 0) {
        t_estados = 0;
      }
      s_lines_done = 0;
      s_next_line_t = (unsigned long)t_estados + 256UL;
      s_last_tape_t = (unsigned long)t_estados;
      s_frame_lines = cpc_hw_frame_lines();
      cpc_hw_end_of_frame();
      cpc_video_frame_start();
    }
  }

  // Maskable interrupt: GA request + interrupts enabled (IM1 like the
  // firmware expects). Mirrors the vendored sequence (EI one-instruction
  // delay via opcode 0xFB = 251, HALT release, vector 0x38).
  if (cpc_hw_irq_pending() && iff1.v && byte_leido != 251) {
    z80_adjust_flags_interrupt_block_opcode();
    debug_anota_retorno_step_maskable();
    cpc_hw_irq_ack();
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
