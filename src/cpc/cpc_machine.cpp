//
// cpc_machine.cpp
//
// See cpc_machine.h. Model table, ROM loading, Z80/GA/PPI/CRTC/AY reset.
// ROM layout (matches upstream ZEsarUX expectations):
//   cpc464.rom 32K = OS 16K + BASIC 16K
//   cpc664/cpc6128.rom 48K = OS 16K + BASIC 16K + AMSDOS 16K
// Upper ROM slots: 0 = BASIC, 7 = AMSDOS (unpopulated slots read as
// BASIC, Caprice32-compatible).
//

#include "cpc_machine.h"
#include "cpc_hw.h"
#include "cpc_fdc.h"
#include "cpc_debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
#include "cpu.h"
#include "operaciones.h"
#include "soundchips/ay38912.h"
}

// Provided by cpc_hw.c.
extern "C" void cpc_init_memory_tables(void);
extern "C" void cpc_set_memory_pages(void);
extern "C" void cpc_reset(void);
// FDC enable flag lives in cpc_hw.c (gates the vendored port handlers).
extern "C" {
extern z80_bit pd765_enabled;
}
// Provided by cpc_core.c.
extern "C" void cpc_core_reset_clocks(void);

namespace bmx {
namespace cpc {

namespace {

const CpcModel kModels[] = {
    {"CPC 464", 140, "cpc464.rom", 32768, 0, 0},
    {"CPC 664", 142, "cpc664.rom", 49152, 0, 1},
    {"CPC 6128", 143, "cpc6128.rom", 49152, 1, 1},
};
const int kNumModels = (int)(sizeof(kModels) / sizeof(kModels[0]));

int s_current = 2;  // 6128 default (most software targets it)

const char *rom_path(const char *rom, char *out, size_t out_size) {
  snprintf(out, out_size, "/roms/CPC/%s", rom);
  return out;
}

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

const CpcModel *machine_model(int index) {
  if (index < 0 || index >= kNumModels) {
    return &kModels[0];
  }
  return &kModels[index];
}

int machine_current_index(void) { return s_current; }

const char *machine_rom_dir(void) { return "/roms/CPC"; }

bool machine_init_model(int index) {
  if (index < 0 || index >= kNumModels) {
    index = 0;
  }
  const CpcModel *model = &kModels[index];
  current_machine_type = (z80_byte)model->id;
  debug_log("machine_init_model: %s (id=%d rom=%s)", model->name, model->id,
            model->rom);

  init_cpu_tables();

  // ROM layout, preferred first:
  //   combined: cpc464.rom 32K = OS+BASIC, cpc664/cpc6128.rom 48K =
  //             OS+BASIC+AMSDOS (upstream ZEsarUX convention), or
  //   split:    os464/os664/os6128.rom + basic464/basic664/basic6128.rom
  //             [+ amsdos.rom], 16K each (TOSEC/CLK convention).
  uint8_t *filebuf = (uint8_t *)malloc(49152);
  if (filebuf == NULL) {
    debug_log("machine_init_model: out of memory");
    return false;
  }
  bool have_roms = false;
  const char *split_base =
      model->id == 140 ? "464" : (model->id == 142 ? "664" : "6128");
  char path[256];
  rom_path(model->rom, path, sizeof(path));
  FILE *fp = fopen(path, "rb");
  if (fp != NULL) {
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (size == model->rom_size &&
        fread(filebuf, 1, (size_t)size, fp) == (size_t)size) {
      have_roms = true;
      debug_log("machine_init_model: combined ROM %s", path);
    } else {
      debug_log("machine_init_model: ROM bad size %ld (expect %d): %s",
                size, model->rom_size, path);
    }
    fclose(fp);
  }
  if (!have_roms) {
    // Split fallback: os+ basic (+amsdos for 664/6128).
    char os_path[256], basic_path[256], amsdos_path[256];
    snprintf(os_path, sizeof(os_path), "/roms/CPC/os%s.rom", split_base);
    snprintf(basic_path, sizeof(basic_path), "/roms/CPC/basic%s.rom",
             split_base);
    snprintf(amsdos_path, sizeof(amsdos_path), "/roms/CPC/amsdos.rom");
    uint8_t *parts[3] = {filebuf, filebuf + 16384, filebuf + 32768};
    const char *part_paths[3] = {os_path, basic_path, amsdos_path};
    const int need = model->rom_size > 32768 ? 3 : 2;
    have_roms = true;
    for (int i = 0; i < need; i++) {
      FILE *pf = fopen(part_paths[i], "rb");
      long psz = -1;
      if (pf != NULL) {
        fseek(pf, 0, SEEK_END);
        psz = ftell(pf);
        fseek(pf, 0, SEEK_SET);
        if (psz != 16384 ||
            fread(parts[i], 1, 16384, pf) != 16384) {
          psz = -1;
        }
        fclose(pf);
      }
      if (psz != 16384) {
        debug_log("machine_init_model: split ROM missing/bad: %s",
                  part_paths[i]);
        have_roms = false;
        break;
      }
    }
    if (have_roms) {
      debug_log("machine_init_model: split ROMs os+basic%s (%s)",
                need > 2 ? "+amsdos" : "", split_base);
    }
  }
  if (!have_roms) {
    free(filebuf);
    debug_log("machine_init_model: no ROMs for %s (want %s %d bytes, or "
              "os%s+basic%s%s 16K parts)",
              model->name, model->rom, model->rom_size, split_base,
              split_base, model->rom_size > 32768 ? "+amsdos" : "");
    return false;
  }
  memcpy(hw_rom_os(), filebuf, 16384);
  memcpy(hw_rom_basic(), filebuf + 16384, 16384);
  if (model->rom_size > 32768) {
    memcpy(hw_rom_amsdos(), filebuf + 32768, 16384);
  } else {
    memset(hw_rom_amsdos(), 0, 16384);
  }
  free(filebuf);

  cpc_hw_set_model(model->is_128k, model->rom_size > 32768 ? 1 : 0);
  cpc_init_memory_tables();

  // Z80 memory/port dispatch: the vendored cpc_* handlers.
  poke_byte = poke_byte_cpc;
  peek_byte = peek_byte_cpc;
  peek_byte_no_time = peek_byte_no_time_cpc;
  poke_byte_no_time = poke_byte_no_time_cpc;
  lee_puerto = lee_puerto_cpc;
  out_port = out_port_cpc;
  fetch_opcode = fetch_opcode_cpc;
  push_valor = push_valor_default;
  machine_emulate_memory_refresh = 0;

  // AY at the CPC 1 MHz clock, CBA stereo (matches upstream cpu.c).
  ay_chip_present.v = 1;
  ay3_stereo_mode = 4;
  init_chip_ay();

  cpc_reset();
  reset_cpu();
  cpc_core_reset_clocks();
  // FDC present on 664/6128 (gates the vendored FB7E/FB7F handlers);
  // the 464 has no controller and keeps 0xFF reads.
  pd765_enabled.v = model->has_floppy ? 1 : 0;
  fdc_reset();

  s_current = index;
  debug_log("machine_init_model: OK %s", model->name);
  return true;
}

void machine_reset(bool soft) {
  (void)soft;
  cpc_reset();
  reset_cpu();
  cpc_core_reset_clocks();
  cpc_init_memory_tables();
  fdc_reset();
  debug_log("machine_reset");
}

}  // namespace cpc
}  // namespace bmx
