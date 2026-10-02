//
// zx_machine.cpp
//
// See zx_machine.h. This replicates exactly the ZX Spectrum branches of
// upstream set_machine_params() + malloc_mem_machine() + cold_start +
// reset_cpu. Calling the upstream functions directly would keep every
// other machine's init (QL, CPC, Z88, ...) linked; the Spectrum-only path
// below keeps the link bounded to the compiled subset.
//

#include "zx_machine.h"
#include "zx_debug.h"
#include "zx_shim.h"
#include "zx_timer.h"
#include "zx_video.h"

#include <stdio.h>
#include <string.h>

extern "C" {
#include "zx_zesarux.h"
// Not declared in any vendored header (defined in cpu.c).
void malloc_machine(int tamanyo);
void random_ram(z80_byte *puntero, int longitud);
}

namespace bmx {
namespace zxspectrum {

namespace {

// Model table: one kernel, menu-selectable like the atari800 submodels.
// IDs are MACHINE_ID_* from cpu.h.
const ZxModel kModels[] = {
    {"Spectrum 48k", 1, "48.rom", 16384},
    {"Spectrum 128k", 6, "128.rom", 32768},
    {"Spectrum +2", 8, "p2.rom", 32768},
    {"Spectrum +2A", 11, "p2a40.rom", 65536},
    {"Spectrum +3", 25, "p2a41.rom", 65536},
    {"Pentagon 128", 21, "pentagon.rom", 32768},
    {"Inves Spectrum+", 2, "inves.rom", 16384},
};
const int kNumModels = (int)(sizeof(kModels) / sizeof(kModels[0]));

int s_current = 0;

const char *rom_path(const char *rom, char *out, size_t out_size) {
#ifdef ZX_HOST_TEST_ROOT
  snprintf(out, out_size, "%s/%s", ZX_HOST_TEST_ROOT, rom);
#else
  snprintf(out, out_size, "/roms/ZXSPECTRUM/%s", rom);
#endif
  return out;
}

// Spectrum branch of malloc_mem_machine().
void malloc_spectrum(void) {
  if (MACHINE_IS_INVES) {
    malloc_machine(65536 + 16384);
    random_ram_inves(memoria_spectrum, 65536);
  } else if (MACHINE_IS_SPECTRUM_16_48) {
    malloc_machine(65536);
    random_ram(memoria_spectrum + 16384, 49152);
  } else if (MACHINE_IS_SPECTRUM_128_P2 || MACHINE_IS_PENTAGON) {
    malloc_machine((32 + 1024) * 1024);
    random_ram(memoria_spectrum + 32768, 1024 * 1024);
    mem_init_memory_tables_128k();
    mem_set_normal_pages_128k();
  } else if (MACHINE_IS_SPECTRUM_P2A_P3) {
    malloc_machine((64 + 1024) * 1024);
    random_ram(memoria_spectrum + 65536, 1024 * 1024);
    mem_init_memory_tables_p2a();
    mem_set_normal_pages_p2a();
  }
}

// Spectrum branch of set_machine_params() (see cpu.c).
void params_spectrum(void) {
  ay_chip_present.v = 0;
  original_timer_sleep_machine = 20000;
  set_emulator_speed();
  allow_write_rom.v = 0;

  // All device/interface enables (dandanator, diviface, multiface, ...) and
  // the sound-chip presents live in uncompiled device modules; they are BSS
  // zero and BMX never enables them, so there is nothing to clear here.

  out_port = out_port_spectrum;

  machine_emulate_memory_refresh = 0;

  push_valor = push_valor_default;

  screen_invisible_borde_superior = 8;
  screen_borde_superior = 56;
  screen_total_borde_inferior = 56;
  screen_total_borde_izquierdo = 48;
  screen_total_borde_derecho = 48;
  screen_invisible_borde_derecho = 96;
  screen_testados_linea = 224;

  cpu_turbo_speed = 1;
  // Interrupt pulse length in T-states (32 spectrum, 36 Pentagon).
  cpu_duracion_pulso_interrupcion = 32;

  port_from_ula = port_from_ula_48k;
  fetch_opcode = fetch_opcode_spectrum;

  screen_set_parameters_slow_machines();
  // Audio buffers are assigned by sound_init(), which runs after the first
  // machine init: only drain when they exist.
  if (audio_buffer != NULL) {
    audio_empty_buffer();
  }
  screen_init_colour_table();

  // Pentagon has no Timex video mode; BMX never enables it, and the
  // ULA timing switch below is all the model needs.

  if (MACHINE_IS_SPECTRUM_16_48) {
    contend_read = contend_read_48k;
    contend_read_no_mreq = contend_read_no_mreq_48k;
    contend_write_no_mreq = contend_write_no_mreq_48k;
    ula_contend_port_early = ula_contend_port_early_48k;
    ula_contend_port_late = ula_contend_port_late_48k;
    if (MACHINE_IS_INVES) {
      screen_testados_linea = 228;
      screen_invisible_borde_superior = 7;
      screen_invisible_borde_derecho = 104;
      keyboard_issue2.v = 0;
    }
  } else if (MACHINE_IS_SPECTRUM_128_P2_P2A_P3) {
    contend_read = contend_read_128k;
    contend_read_no_mreq = contend_read_no_mreq_128k;
    contend_write_no_mreq = contend_write_no_mreq_128k;
    ula_contend_port_early = ula_contend_port_early_128k;
    ula_contend_port_late = ula_contend_port_late_128k;
    screen_testados_linea = 228;
    screen_invisible_borde_superior = 7;
    screen_invisible_borde_derecho = 104;
    contend_pages_128k_p2a = contend_pages_128k;
    if (MACHINE_IS_SPECTRUM_P2A_P3) {
      port_from_ula = port_from_ula_p2a;
      contend_pages_128k_p2a = contend_pages_p2a;
    }
    if (MACHINE_IS_PENTAGON) {
      contend_enabled.v = 0;
      ula_enable_pentagon_timing_no_common();
      cpu_duracion_pulso_interrupcion = 36;
    }
  }

  // peek/poke/port assignment per model (set_machine_params switch).
  switch (current_machine_type) {
    case 2:  // Inves
      poke_byte = poke_byte_spectrum_inves;
      peek_byte = peek_byte_spectrum_inves;
      peek_byte_no_time = peek_byte_no_time_spectrum_inves;
      poke_byte_no_time = poke_byte_no_time_spectrum_inves;
      lee_puerto = lee_puerto_spectrum;
      break;
    case 6:   // 128
    case 8:   // +2
      poke_byte = poke_byte_spectrum_128k;
      peek_byte = peek_byte_spectrum_128k;
      peek_byte_no_time = peek_byte_no_time_spectrum_128k;
      poke_byte_no_time = poke_byte_no_time_spectrum_128k;
      lee_puerto = lee_puerto_spectrum;
      ay_chip_present.v = 1;
      break;
    case 11:  // +2A
    case 25:  // +3
      poke_byte = poke_byte_spectrum_128kp2a;
      peek_byte = peek_byte_spectrum_128kp2a;
      peek_byte_no_time = peek_byte_no_time_spectrum_128kp2a;
      poke_byte_no_time = poke_byte_no_time_spectrum_128kp2a;
      lee_puerto = lee_puerto_spectrum;
      ay_chip_present.v = 1;
      break;
    case 21:  // Pentagon 128
      poke_byte = poke_byte_spectrum_128k;
      peek_byte = peek_byte_spectrum_128k;
      peek_byte_no_time = peek_byte_no_time_spectrum_128k;
      poke_byte_no_time = poke_byte_no_time_spectrum_128k;
      lee_puerto = lee_puerto_spectrum;
      ay_chip_present.v = 1;
      break;
    case 1:  // 48k (default)
    default:
      poke_byte = poke_byte_spectrum_48k;
      peek_byte = peek_byte_spectrum_48k;
      peek_byte_no_time = peek_byte_no_time_spectrum_48k;
      poke_byte_no_time = poke_byte_no_time_spectrum_48k;
      lee_puerto = lee_puerto_spectrum;
      break;
  }
}

// cold_start_cpu_registers() spectrum path.
void cold_start(void) {
  reg_a = 0xff;
  Z80_FLAGS = 0xff;
  BC = HL = DE = 0xffff;
  reg_ix = reg_iy = reg_sp = 0xffff;
  reg_h_shadow = reg_l_shadow = reg_b_shadow = reg_c_shadow = reg_d_shadow =
      reg_e_shadow = reg_a_shadow = Z80_FLAGS_SHADOW = 0xff;
  reg_i = 0;
  reg_r = reg_r_bit7 = 0;

  out_254_original_value = out_254 = 0xff;
  if (MACHINE_IS_INVES) {
    out_254_original_value = out_254 = 0;
  }
  modificado_border.v = 1;
}

// reset_cpu() spectrum path.
void reset_core(void) {
  reg_pc = 0;
  reg_i = 0;
  z80_wait_signal.v = 0;
  puerto_32765 = 0;
  puerto_8189 = 0;
  zesarux_zxi_last_register = 0;
  zesarux_zxi_registers_array[0] = 0;
  zesarux_zxi_registers_array[4] = 0;
  zesarux_zxi_registers_array[5] = 0;
  interrupcion_maskable_generada.v = 0;
  interrupcion_non_maskable_generada.v = 0;
  interrupcion_timer_generada.v = 0;
  iff1.v = iff2.v = 0;
  im_mode = 0;
  reg_a = 0xff;
  Z80_FLAGS = 0xff;
  reg_sp = 0xffff;
  z80_halt_signal.v = 0;
  esperando_tiempo_final_t_estados.v = 0;
}

}  // namespace

int machine_model_count(void) { return kNumModels; }

const ZxModel *machine_model(int index) {
  if (index < 0 || index >= kNumModels) {
    return &kModels[0];
  }
  return &kModels[index];
}

int machine_current_index(void) { return s_current; }

bool machine_init_model(int index) {
  if (index < 0 || index >= kNumModels) {
    index = 0;
  }
  const ZxModel *model = &kModels[index];
  current_machine_type = (z80_byte)model->id;

  debug_log("machine_init_model: %s (id=%d rom=%s)", model->name, model->id,
            model->rom);

  // Z80 flag lookup tables (desktop start.c calls this once at boot; the
  // tables are process-global so one call is enough, but it is idempotent).
  init_cpu_tables();

  // Params first (upstream set_machine order): paging-table pointers
  // must exist before malloc_spectrum pages memory.
  params_spectrum();
  malloc_spectrum();
  if (memoria_spectrum == NULL) {
    debug_log("machine_init_model: memory allocation FAILED");
    return false;
  }
  shim_init_visualmem();

  char path[256];
  rom_path(model->rom, path, sizeof(path));
  // Pre-check size so a missing ROM fails here with a clear log line.
  FILE *probe = fopen(path, "rb");
  if (probe == NULL) {
    debug_log("machine_init_model: ROM missing: %s", path);
    return false;
  }
  fseek(probe, 0, SEEK_END);
  long size = ftell(probe);
  fclose(probe);
  if (size != model->rom_size) {
    debug_log("machine_init_model: ROM bad size %ld (expect %d): %s", size,
              model->rom_size, path);
    return false;
  }
  rom_load(path);

  screen_set_video_params_indices();
  inicializa_tabla_contend();
  // Full border + cached rainbow geometry (desktop does this in its video
  // init; without it the cached size stays 0 and nothing renders).
  enable_border();
  // Scanline render into the rainbow buffer (desktop realvideo default).
  enable_rainbow();
  init_screen_addr_table();
  init_rainbow();
  init_cache_putpixel();

  // AY chip state for 128k-class models.
  if (ay_chip_present.v) {
    init_chip_ay();
  }

  cold_start();
  reset_core();
  machine_fix_paging();
  timer_reset_frame_clock();

  s_current = index;
  debug_log("machine_init_model: OK t_line=%d t_total=%d scanlines=%d",
            screen_testados_linea, screen_testados_total, screen_scanlines);
  return true;
}

void machine_fix_paging(void) {
  if (MACHINE_IS_SPECTRUM_128_P2 || MACHINE_IS_PENTAGON) {
    mem_set_normal_pages_128k();
  } else if (MACHINE_IS_SPECTRUM_P2A_P3) {
    mem_set_normal_pages_p2a();
  }
}

void machine_reset(bool soft) {
  (void)soft;
  // No warm-start distinction on Spectrum: reset vector + paging restore.
  reset_core();
  machine_fix_paging();
  timer_reset_frame_clock();
  debug_log("machine_reset");
}

}  // namespace zxspectrum
}  // namespace bmx
