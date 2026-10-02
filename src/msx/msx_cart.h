//
// msx_cart.h
//
// MSX cartridge (.rom) attach on top of the vendored slot mapper
// (msx_insert_rom_cartridge / msx_empty_romcartridge_space in
// machines/msx.c). Phase 1: plain 8/16/32/48K ROMs; 64K+ mapper images
// are rejected with a clean error until mappers are verified.
//

#ifndef msx_cart_h
#define msx_cart_h

namespace bmx {
namespace msx {

// Insert a cartridge (0 ok, -1 reject). Does not reset: cartridges boot
// on the next reset (autostart resets right after attaching).
int cart_attach(const char *path);
void cart_detach(void);
bool cart_attached(void);
const char *cart_name(void);

}  // namespace msx
}  // namespace bmx

#endif
