//
// cpc_hw.h
//
// C++-side accessors for the CPC hardware module (cpc_hw.c): RAM/ROM
// image buffers and model configuration for machine init.
//
// NOTE: deliberately uses unsigned char* (identical to the vendored
// z80_byte) instead of including cpu.h, so this header can be used from
// translation units that also see BMX's struct menu_item (which collides
// with a ZEsarUX typedef, cf. zx_zesarux.h).
//

#ifndef cpc_hw_h
#define cpc_hw_h

#include <stdint.h>

extern "C" {
unsigned char *cpc_hw_ram_base(void);
unsigned char *cpc_hw_ram_extra(void);
unsigned char *cpc_hw_rom_os(void);
unsigned char *cpc_hw_rom_basic(void);
unsigned char *cpc_hw_rom_amsdos(void);
void cpc_hw_set_model(int is_128k, int has_amsdos);
}

namespace bmx {
namespace cpc {

inline uint8_t *hw_ram_base() {
  return reinterpret_cast<uint8_t *>(cpc_hw_ram_base());
}
inline uint8_t *hw_ram_extra() {
  return reinterpret_cast<uint8_t *>(cpc_hw_ram_extra());
}
inline uint8_t *hw_rom_os() {
  return reinterpret_cast<uint8_t *>(cpc_hw_rom_os());
}
inline uint8_t *hw_rom_basic() {
  return reinterpret_cast<uint8_t *>(cpc_hw_rom_basic());
}
inline uint8_t *hw_rom_amsdos() {
  return reinterpret_cast<uint8_t *>(cpc_hw_rom_amsdos());
}

}  // namespace cpc
}  // namespace bmx

#endif
