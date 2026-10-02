//
// a2600_cart.h
//
// Stella session lifetime: owns the OSystem, builds the console from a
// cartridge file, steps one frame at a time. Cartridges only (.a26/.bin;
// zips arrive pre-extracted by the menu zip flow).
//

#ifndef a2600_cart_h
#define a2600_cart_h

#include <stddef.h>
#include <stdint.h>

namespace bmx {
namespace a2600 {

// Build (or rebuild) the console from a cartridge path. Returns true on
// success; the previous session, if any, is replaced.
bool cart_create(const char *path);
void cart_destroy(void);
bool cart_has_console(void);

// Current cartridge path ("" when none).
const char *cart_path(void);

// Step one emulated frame. On true, out_pixels/out_pitch/out_w/out_h
// describe the Stella u32 frame (valid until the next call) and
// out_refresh is 50 or 60.
bool cart_run_frame(const uint32_t *&out_pixels, int &out_pitch, int &out_w,
                    int &out_h, int &out_refresh);

// Drain queued TIA audio into stereo pairs (capacity max_pairs).
// Returns pairs written and the Stella sample rate.
unsigned int cart_drain_audio(int16_t *stereo, unsigned int max_pairs,
                             int &stella_rate);

// Push one frame of USB-pad state into Stella (joysticks, console
// switches, difficulty). No-op without a console.
void cart_pump_input(void);

// Snapshot save/load through Stella's StateManager (Serializer blob).
bool cart_save_state(const char *path);
bool cart_load_state(const char *path);

}  // namespace a2600
}  // namespace bmx

#endif
