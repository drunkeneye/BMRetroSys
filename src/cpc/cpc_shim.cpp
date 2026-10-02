//
// cpc_shim.cpp
//
// BMX-hosted replacements for ZEsarUX translation units that are not
// compiled for bare metal (debug.c, utils.c file helpers, realjoystick.c,
// superupgrade/timex/tsconf/pd765 leftovers, snap-in-ram).
//
// Only symbols actually referenced by the compiled core files are defined
// here (verified against the link). Behaviour matches upstream for the
// CPC path; unreachable machine branches return inert values.
//

#include "cpc_debug.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <circle/timer.h>

extern "C" {
#include "cpu.h"
#include "operaciones.h"
}

// Forward declarations (defined below; used by shim_init_visualmem).
extern "C" void *util_malloc_fill(int total, char *mensaje_panic,
                                  z80_byte value);

enum {
  CPC_VERBOSE_ERR = 0,
  CPC_VERBOSE_WARN = 1,
  CPC_VERBOSE_INFO = 2,
  CPC_VERBOSE_DEBUG = 3,
};

namespace bmx {
namespace cpc {

namespace {
bool s_verbose = false;
}

void shim_set_verbose(bool enabled) { s_verbose = enabled; }
bool shim_verbose(void) { return s_verbose; }

void shim_init_visualmem(void) {
  // The fetch/read/write path touches these unconditionally (no null
  // check upstream); allocate the working set only, like the ZX kernel.
  if (visualmem_buffer == nullptr) {
    visualmem_buffer = (z80_byte *)util_malloc_fill(65536, (char *)"cpc visualmem", 0);
  }
  if (visualmem_read_buffer == nullptr) {
    visualmem_read_buffer =
        (z80_byte *)util_malloc_fill(65536, (char *)"cpc visualmem", 0);
  }
  if (visualmem_opcode_buffer == nullptr) {
    visualmem_opcode_buffer =
        (z80_byte *)util_malloc_fill(65536, (char *)"cpc visualmem", 0);
  }
}

}  // namespace cpc
}  // namespace bmx

extern "C" {

// ---------------------------------------------------------------------------
// debug.c
// ---------------------------------------------------------------------------

void debug_printf(int debuglevel, const char *format, ...) {
  if (debuglevel >= CPC_VERBOSE_DEBUG && !bmx::cpc::shim_verbose()) {
    return;
  }
  char line[512];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  bmx::cpc::debug_log("cpc: %s", line);
}

void debug_printf_source(int debuglevel, char *archivo, int linea,
                         const char *funcion, const char *format, ...) {
  if (debuglevel >= CPC_VERBOSE_DEBUG && !bmx::cpc::shim_verbose()) {
    return;
  }
  char line[512];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  bmx::cpc::debug_log("cpc: %s:%d %s: %s", archivo != NULL ? archivo : "?",
                             linea, funcion != NULL ? funcion : "?", line);
}

int debug_printf_check_exclude_include(unsigned int clase_mensaje) {
  (void)clase_mensaje;
  return 1;
}

void cpu_panic(char *mensaje) {
  bmx::cpc::debug_log("cpc PANIC: %s", mensaje != NULL ? mensaje : "?");
  for (;;) {
#if AARCH == 64
    asm("dsb sy\n\t"
        "wfi\n\t");
#else
    asm("dsb\n\t"
        "wfi\n\t");
#endif
  }
}

int debug_fired_interrupt = 0;
int debug_fired_in = 0;
int debug_fired_out = 0;

void debug_anota_retorno_step_nmi(void) {}
void debug_anota_retorno_step_maskable(void) {}

// Tape globals (storage/tape.c not compiled; the CPC pulse player lives
// in cpc_tape.cpp and drives PPI Port B directly, so these stay clear).
z80_bit realtape_inserted = {0};
z80_bit realtape_playing = {0};
char realtape_volumen = 0;

// ---------------------------------------------------------------------------
// utils.c (subset)
// ---------------------------------------------------------------------------

void *util_malloc(int total, char *mensaje_panic) {
  void *memoria = malloc((size_t)total);
  if (memoria == NULL) {
    cpu_panic(mensaje_panic);
  }
  return memoria;
}

void *util_malloc_fill(int total, char *mensaje_panic, z80_byte value) {
  char *memoria = (char *)util_malloc(total, mensaje_panic);
  for (int i = 0; i < total; i++) {
    memoria[i] = (char)value;
  }
  return memoria;
}

// utils.c random_ram(): power-on RAM pattern (LCG seeded from the wall
// clock, like the ZX kernel). Unused by default (BSS zeros are
// deterministic); kept for link compatibility with cpu.c helpers.
void random_ram(z80_byte *puntero, int longitud) {
  static unsigned long s_seed = 0;
  if (s_seed == 0) {
    s_seed = CTimer::GetClockTicks() | 1UL;
  }
  for (int i = 0; i < longitud; i++) {
    s_seed = s_seed * 1103515245UL + 12345UL;
    puntero[i] = (z80_byte)((s_seed >> 16) & 0xff);
  }
}

int usleep(unsigned int useconds) {
  unsigned long start = CTimer::GetClockTicks();
  while ((CTimer::GetClockTicks() - start) < useconds) {
  }
  return 0;
}

// 50 Hz refresh flag slot (timer.h): the CPC firmware scans the keyboard
// through PPI itself, so this stays clear; defined for link compatibility.
z80_bit interrupcion_fifty_generada = {0};

// ---------------------------------------------------------------------------
// realjoystick.c — not compiled; joystick state lives in the matrix.
// ---------------------------------------------------------------------------

static void empty_realjoystick_main(void) {}

void (*realjoystick_main)(void) = empty_realjoystick_main;

z80_byte puerto_especial_joystick = 0;
int joystick_emulation = 0;
int joystick_autofire_frequency = 0;
int joystick_autofire_counter = 0;
int joystick_autoleftright_enabled = 0;
int joystick_autoleftright_frequency = 0;
int joystick_autoleftright_counter = 0;
int joystick_autoleftright_status = 0;
int joystick_barato = 0;

// ---------------------------------------------------------------------------
// superupgrade / timex / tsconf (unreachable on CPC models).
// ---------------------------------------------------------------------------

z80_bit superupgrade_enabled = {0};
z80_byte superupgrade_puerto_43b = 0;
void superupgrade_set_memory_pages(void) {}

int timex_si_modo_8x1(void) { return 0; }
z80_byte timex_port_ff = 0;
z80_byte tsconf_vector_fired_interrupt = 0;

// ---------------------------------------------------------------------------
// pd765 / autoload / snap-in-ram (no floppy/autoload here yet).
// ---------------------------------------------------------------------------

void pd765_next_event_from_core(void) {}
void gestionar_autoload_spectrum(void) {}
void snapshot_add_in_ram(void) {}
void pd765_enable(void) {}

// ---------------------------------------------------------------------------
// Exact replicas of utils.c leaves the CPC path needs.
// ---------------------------------------------------------------------------

int util_parity(z80_byte value) {
  int i;
  int cantidad_unos = 1;
  for (i = 0; i < 8; i++) {
    if (value & 1) {
      cantidad_unos++;
    }
    value = (z80_byte)(value >> 1);
  }
  return cantidad_unos & 1;
}

int zxvision_key_not_sent_emulated_mach(void) { return 0; }

// Mirrors utils.c get_rom_size for the CPC models this kernel runs.
int get_rom_size(int machine) {
  (void)machine;
  if (MACHINE_IS_CPC_464 || MACHINE_IS_CPC_4128) {
    return 32768;
  } else if (MACHINE_IS_CPC_664 || MACHINE_IS_CPC_6128) {
    return 49152;
  }
  return 0;
}

// utils.c file helpers (referenced by linked cpu.c/audio.c paths; the CPC
// machine init uses fopen directly).
int find_sharedfile(char *archivo, char *ruta_final) {
  if (archivo == NULL || ruta_final == NULL) {
    return 0;
  }
  FILE *fp = fopen(archivo, "rb");
  if (fp != NULL) {
    fclose(fp);
    strncpy(ruta_final, archivo, 255);
    ruta_final[255] = '\0';
    return 1;
  }
  return 0;
}

long long int get_file_size(char *nombre) {
  if (nombre == NULL) {
    return 0;
  }
  FILE *fp = fopen(nombre, "rb");
  if (fp == NULL) {
    return 0;
  }
  fseek(fp, 0, SEEK_END);
  long size = ftell(fp);
  fclose(fp);
  return size < 0 ? 0 : (long long int)size;
}

}  // extern "C"
