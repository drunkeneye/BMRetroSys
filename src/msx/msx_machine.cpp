//
// msx_machine.cpp
//
// See msx_machine.h. Model table, ROM loading, Z80/VDP/AY reset.
// ROM layout (matches upstream ZEsarUX expectations):
//   msx.rom 32K = BIOS 16K (0000-3FFF) + BASIC 16K (4000-7FFF),
//   loaded at memoria_spectrum[0]; the vendored slot mapper
//   (msx_init_memory_tables) exposes it as slot 0.
//

#include "msx_machine.h"
#include "msx_debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include "cpu.h"
#include "operaciones.h"
#include "soundchips/ay38912.h"
#include "msx.h"
#include "vdp_9918a.h"
}

// Provided by msx_core.c.
extern "C" void msx_core_reset_clocks(void);
// AY internals for the TEMP boot self-test (file-scope globals in the
// vendored ay38912.c; the header only declares a subset).
extern "C" {
extern int freq_tono_A[];
extern int contador_tono_A[];
extern short ultimo_valor_tono_A[];
}
// Provided by cpu.c / joystick handling (C linkage, no extra headers).
extern "C" {
void malloc_machine(int size);
void random_ram(unsigned char *ptr, int len);
extern int joystick_emulation;
}

namespace bmx {
namespace msx {

// This kernel always renders through the V3D scaler (clean GPU upscale,
// no CRT effects by default): the HVS direct path shows stale-frame
// ghosts on MSX geometry. Strong override of the weak shared default in
// third_party/common/emux_api.c; the Video menu toggle is locked on.
extern "C" int bmx_machine_forces_v3d(void) {
  return 1;
}

namespace {

const MsxModel kModels[] = {
    {"MSX1 (Philips)", "msx.rom"},
    {"MSX1 (C-BIOS)", "msx-cbios.rom"},
};
const int kNumModels = (int)(sizeof(kModels) / sizeof(kModels[0]));

int s_current = 0;

void reset_cpu(void) {
  reg_pc = 0;
  reg_i = 0;
  reg_r = 0;
  reg_r_bit7 = 0;
  z80_wait_signal.v = 0;
  z80_halt_signal.v = 0;
  interrupcion_maskable_generada.v = 0;
  interrupcion_non_maskable_generada.v = 0;
  iff1.v = iff2.v = 0;
  im_mode = 0;
  reg_a = 0xFF;
  Z80_FLAGS = 0xFF;
  BC = HL = DE = 0xFFFF;
  reg_ix = reg_iy = reg_sp = 0xFFFF;
  reg_h_shadow = reg_l_shadow = reg_b_shadow = reg_c_shadow =
      reg_d_shadow = reg_e_shadow = reg_a_shadow = Z80_FLAGS_SHADOW = 0xFF;
  t_estados = 0;
}

}  // namespace

int machine_model_count(void) { return kNumModels; }

const MsxModel *machine_model(int index) {
  if (index < 0 || index >= kNumModels) {
    return &kModels[0];
  }
  return &kModels[index];
}

int machine_current_index(void) { return s_current; }

const char *machine_rom_dir(void) { return "/roms/MSX"; }

bool machine_init_model(int index) {
  if (index < 0 || index >= kNumModels) {
    index = 0;
  }
  const MsxModel *model = &kModels[index];
  current_machine_type = (z80_byte)110;  // MACHINE_ID_MSX1
  debug_log("machine_init_model: %s (rom=%s)", model->name, model->rom);

  init_cpu_tables();

  // 256K main store (4 slots x 64K) + random RAM pattern on the RAM
  // half, mirroring the upstream MSX branch.
  malloc_machine(65536 * 4);
  random_ram(memoria_spectrum + 32768, 32768);

  // 32K BIOS+BASIC at memoria_spectrum[0] (upstream convention).
  char path[256];
  snprintf(path, sizeof(path), "/roms/MSX/%s", model->rom);
  FILE *fp = fopen(path, "rb");
  if (fp == NULL) {
    debug_log("machine_init_model: no ROM %s", path);
    return false;
  }
  fseek(fp, 0, SEEK_END);
  long size = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  if (size != 32768 || fread(memoria_spectrum, 1, 32768, fp) != 32768) {
    debug_log("machine_init_model: ROM bad size %ld (expect 32768): %s",
              size, path);
    fclose(fp);
    return false;
  }
  fclose(fp);
  debug_log("machine_init_model: ROM %s", path);

  // Z80 memory/port dispatch: the vendored msx1 handlers.
  poke_byte = poke_byte_msx1;
  peek_byte = peek_byte_msx1;
  peek_byte_no_time = peek_byte_no_time_msx1;
  poke_byte_no_time = poke_byte_no_time_msx1;
  lee_puerto = lee_puerto_msx1;
  out_port = out_port_msx1;
  fetch_opcode = fetch_opcode_msx;
  push_valor = push_valor_default;
  machine_emulate_memory_refresh = 0;

  // AY at the MSX clock (init_chip_ay picks 1789772 via MACHINE_IS_MSX),
  // mono (authentic single-speaker MSX).
  ay_chip_present.v = 1;
  ay3_stereo_mode = 0;
  init_chip_ay();

  // Host joystick drives PSG R14 through puerto_especial_joystick.
  joystick_emulation = 13;  // JOYSTICK_MSX

  msx_alloc_vram_memory();
  msx_init_memory_tables();
  msx_reset();
  vdp_9918a_reset();
  reset_cpu();
  msx_core_reset_clocks();

  // TEMP DIAG AY self-test (PLAY tones arrive as DC only): program ch A
  // to a 440 Hz square at max volume, step 1 s worth at the scanline
  // rate, and sample the speaker output. Working silicon gives
  // min~-24, max~+24 with ~880 sign flips. Then restore pristine AY.
  {
    out_port_ay(65533, 0);
    out_port_ay(49149, 254);  // R0: period low (254 -> ~440 Hz @1789772)
    out_port_ay(65533, 1);
    out_port_ay(49149, 0);  // R1: period high
    out_port_ay(65533, 7);
    out_port_ay(49149, 0x3E);  // mixer: tone A only
    out_port_ay(65533, 8);
    out_port_ay(49149, 15);  // vol A max
    out_port_ay(65533, 9);
    out_port_ay(49149, 0);
    out_port_ay(65533, 10);
    out_port_ay(49149, 0);
    debug_log("AYTEST setup present=%d chips=%d stereo=%d speech=%d freqA=%d",
              ay_chip_present.v, ay_retorna_numero_chips(), ay3_stereo_mode,
              ay_speech_enabled.v, freq_tono_A[0]);
    int mn = 127, mx = -128, flips = 0, last = 0;
    for (int i = 0; i < 15600; i++) {
      int v = (int)(signed char)da_output_ay_izquierdo();
      if (v < mn) {
        mn = v;
      }
      if (v > mx) {
        mx = v;
      }
      if (i > 0 && ((last < 0) != (v < 0))) {
        flips++;
      }
      last = v;
      ay_chip_siguiente_ciclo();
    }
    debug_log("AYTEST result min=%d max=%d flips=%d contA=%d ultA=%d", mn, mx,
              flips, contador_tono_A[0], ultimo_valor_tono_A[0]);
    init_chip_ay();  // restore pristine (regs=255); phase garbage is fine
  }

  s_current = index;
  debug_log("machine_init_model: OK %s", model->name);
  return true;
}

void machine_reset(bool soft) {
  (void)soft;
  msx_reset();
  vdp_9918a_reset();
  reset_cpu();
  msx_core_reset_clocks();
  debug_log("machine_reset");
}

}  // namespace msx
}  // namespace bmx
