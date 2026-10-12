//
// bbc_machine.h
//
// CLK BBCMicro machine lifecycle (create/destroy/reset/run) plus the model
// table. The CLK Machine base exposes nothing; access goes through the
// MachineTypes interfaces (TimedMachine/ScanProducer/AudioProducer/
// MappedKeyboardMachine/JoystickMachine/MediaTarget/KeyActions).
//

#ifndef BMX_BBC_MACHINE_H
#define BMX_BBC_MACHINE_H

namespace bmx {
namespace bbc {

struct BbcModel {
  const char *name;      // menu label
  const char *fs_rom;    // filing-system ROM in /roms/BBC ("" = none)
  bool has_1770dfs;      // WD1770 + DFS sideways ROM
  bool has_adfs;         // ADFS sideways ROM
};

int machine_model_count(void);
const BbcModel *machine_model(int index);
int machine_current_index(void);

// Validates pool ROMs and (re)creates the CLK machine. Returns false when
// a required ROM is missing (logged verbosely).
bool machine_init_model(int index);
void machine_note_core_up(void);
bool machine_is_up(void);

// First creation once the core is up (video/sound endpoints exist).
bool machine_create_initial(void);

// Full recreation (clean reboot). soft is accepted for menu
// compatibility; the BBC has one reset path.
void machine_reset(bool soft);

// Run one 50 Hz frame worth of cycles and flush video+audio output.
void machine_run_frame(void);
// Run a short slice (seconds of emulated time) and flush output.
// The core loop slices until a complete field is ready to present.
void machine_run_slice(double seconds);

// Speed multiplier (1.0 = authentic 2 MHz). Applied live.
void machine_set_speed(double mult);

// SHIFT+BREAK disc boot helper for autostart (holds shift, pulses break).
void machine_shift_break(void);
void machine_set_shift_held(bool held);

const char *machine_rom_dir(void);

}  // namespace bbc
}  // namespace bmx

#endif  // BMX_BBC_MACHINE_H
