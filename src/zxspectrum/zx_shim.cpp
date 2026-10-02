//
// zx_shim.cpp
//
// BMX-hosted replacements for ZEsarUX translation units that are not
// compiled for bare metal (debug.c, utils.c, realjoystick.c, timex.c,
// tsconf.c, superupgrade.c, pd765.c, autoselectoptions.c, snap/ram slice).
//
// Only symbols actually referenced by the compiled core files are defined
// here (verified against the link). Behaviour matches upstream for the
// ZX Spectrum path; unreachable machine branches return inert values.
//

#include "zx_shim.h"
#include "zx_debug.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <circle/timer.h>

extern "C" {
#include "zx_zesarux.h"
}

// debug.h verbosity levels (mirrors upstream debug.h; the header itself
// cannot compile as C++ because it uses `operator` as a field name).
enum {
  ZX_VERBOSE_ERR = 0,
  ZX_VERBOSE_WARN = 1,
  ZX_VERBOSE_INFO = 2,
  ZX_VERBOSE_DEBUG = 3,
};

namespace bmx {
namespace zxspectrum {

namespace {
bool s_verbose = false;
}

void shim_set_verbose(bool enabled) { s_verbose = enabled; }
bool shim_verbose(void) { return s_verbose; }

void shim_init_visualmem(void) {
  // Only the three buffers the Spectrum fetch/read/write path touches.
  // (Upstream init_visualmembuffer also allocates MMC/HiLow/microdrive
  // megabytes that BMX never uses.)
  if (visualmem_buffer == nullptr) {
    visualmem_buffer = (z80_byte *)util_malloc_fill(65536, (char *)"zx visualmem", 0);
  }
  if (visualmem_read_buffer == nullptr) {
    visualmem_read_buffer =
        (z80_byte *)util_malloc_fill(65536, (char *)"zx visualmem", 0);
  }
  if (visualmem_opcode_buffer == nullptr) {
    visualmem_opcode_buffer =
        (z80_byte *)util_malloc_fill(65536, (char *)"zx visualmem", 0);
  }
}

}  // namespace zxspectrum
}  // namespace bmx

extern "C" {

// ---------------------------------------------------------------------------
// debug.c
// ---------------------------------------------------------------------------

void debug_printf(int debuglevel, const char *format, ...) {
  if (debuglevel >= ZX_VERBOSE_DEBUG && !bmx::zxspectrum::shim_verbose()) {
    return;
  }
  char line[512];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  bmx::zxspectrum::debug_log("zx: %s", line);
}

void debug_printf_source(int debuglevel, char *archivo, int linea,
                         const char *funcion, const char *format, ...) {
  if (debuglevel >= ZX_VERBOSE_DEBUG && !bmx::zxspectrum::shim_verbose()) {
    return;
  }
  char line[512];
  va_list args;
  va_start(args, format);
  vsnprintf(line, sizeof(line), format, args);
  va_end(args);
  bmx::zxspectrum::debug_log("zx: %s:%d %s: %s", archivo != NULL ? archivo : "?",
                             linea, funcion != NULL ? funcion : "?", line);
}

int debug_printf_check_exclude_include(unsigned int clase_mensaje) {
  (void)clase_mensaje;
  return 1;
}

void cpu_panic(char *mensaje) {
  bmx::zxspectrum::debug_log("zx PANIC: %s",
                             mensaje != NULL ? mensaje : "?");
  // Halt: a faulted emulator must not spin silently.
#if defined(__aarch64__) || defined(__arm__)
  for (;;) {
#if AARCH == 64
    asm("dsb sy\n\t"
        "wfi\n\t");
#else
    asm("dsb\n\t"
        "wfi\n\t");
#endif
  }
#else
  // Host test builds: abort so the harness reports failure.
  fflush(NULL);
  abort();
#endif
}

int debug_fired_interrupt = 0;

void debug_anota_retorno_step_nmi(void) {}
void debug_anota_retorno_step_maskable(void) {}

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

void toggle_flash_state(void) {
  if (disable_change_flash.v) {
    estado_parpadeo.v = 0;
  } else {
    estado_parpadeo.v ^= 1;
  }
}

// ---------------------------------------------------------------------------
// realjoystick.c — function pointer + USB poll installed by zx_input.
// ---------------------------------------------------------------------------

static void empty_realjoystick_main(void) {}

void (*realjoystick_main)(void) = empty_realjoystick_main;

// joystick.c is not compiled; the Kempston state it owns lives here.
z80_byte puerto_especial_joystick = 0;
int joystick_emulation = 0;
int joystick_autofire_frequency = 0;
int joystick_autofire_counter = 0;
int joystick_autoleftright_enabled = 0;
int joystick_autoleftright_frequency = 0;
int joystick_autoleftright_counter = 0;
int joystick_autoleftright_status = 0;
int joystick_barato = 0;
char *joystick_texto[JOYSTICK_TOTAL] = {0};

// ---------------------------------------------------------------------------
// Tape globals (storage/tape.c not compiled; trap itself in zx_tape.c).
// ---------------------------------------------------------------------------

z80_bit realtape_inserted = {0};
z80_bit realtape_playing = {0};
z80_bit realtape_loading_sound = {0};

void realtape_get_byte(void) {}
char get_realtape_last_value(void) { return 0; }

// ---------------------------------------------------------------------------
// superupgrade.c / timex.c / tsconf.c (unreachable on Spectrum models).
// ---------------------------------------------------------------------------

z80_bit superupgrade_enabled = {0};
z80_byte superupgrade_puerto_43b = 0;
void superupgrade_set_memory_pages(void) {}

int timex_si_modo_8x1(void) { return 0; }
z80_byte timex_port_ff = 0;
z80_byte tsconf_vector_fired_interrupt = 0;

// ---------------------------------------------------------------------------
// utils.c file helpers (rom_load path only).
// ---------------------------------------------------------------------------

int find_sharedfile(char *archivo, char *ruta_final) {
  if (archivo == NULL || ruta_final == NULL) {
    return 0;
  }
  // BMX passes absolute SD paths (/roms/ZXSPECTRUM/...); accept them
  // directly. Anything else is resolved against the ROM pool as well.
  FILE *fp = fopen(archivo, "rb");
  if (fp != NULL) {
    fclose(fp);
    strncpy(ruta_final, archivo, 255);
    ruta_final[255] = '\0';
    return 1;
  }
  return 0;
}

void open_sharedfile(char *archivo, FILE **f) {
  if (f == NULL) {
    return;
  }
  *f = NULL;
  if (archivo == NULL) {
    return;
  }
  *f = fopen(archivo, "rb");
}

void open_sharedfile_write(char *archivo, FILE **f) {
  if (f == NULL) {
    return;
  }
  *f = NULL;
  if (archivo == NULL) {
    return;
  }
  *f = fopen(archivo, "wb");
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

// Mirrors utils.c get_rom_size for the Spectrum models this kernel runs;
// other machines are unreachable (returns 0 like upstream's default).
int get_rom_size(int machine) {
  (void)machine;
  if (MACHINE_IS_SPECTRUM_16_48) {
    return 16384;
  } else if (MACHINE_IS_SPECTRUM_128_P2) {
    return 32768;
  } else if (MACHINE_IS_SPECTRUM_P2A_P3) {
    return 65536;
  }
  return 0;
}

// ---------------------------------------------------------------------------
// pd765.c / autoselectoptions.c / snap-in-ram (no floppy/autoload here).
// ---------------------------------------------------------------------------

void pd765_next_event_from_core(void) {}
void gestionar_autoload_spectrum(void) {}
void snapshot_add_in_ram(void) {}
void pd765_enable(void) {}

// ---------------------------------------------------------------------------
// Exact replicas of utils.c / zxvision.c leaves the Spectrum path needs.
// ---------------------------------------------------------------------------

// utils.c: parity used by the Z80 flag calculation. Replicated exactly.
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

// zxvision.c: whether a key event is held by the desktop menu instead of
// the machine. BMX has no ZX Vision layer: the machine always gets keys.
int zxvision_key_not_sent_emulated_mach(void) {
  return 0;
}

// utils.c random_ram(): power-on RAM pattern. Deterministic LCG (fixed
// seed per boot would freeze some copy protections; seed from the wall
// clock like the desktop rand() path).
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

// newlib on Circle has no usleep(); busy-wait on the wall clock.
int usleep(unsigned int useconds) {
  unsigned long start = CTimer::GetClockTicks();
  while ((CTimer::GetClockTicks() - start) < useconds) {
  }
  return 0;
}

// The 50 Hz keyboard/joystick refresh flag (timer.h). Driven by zx_timer;
// the core clears it after each refresh.
z80_bit interrupcion_fifty_generada = {0};

}  // extern "C"
