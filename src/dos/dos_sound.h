//
// dos_sound.h
//
// DOSBox-X audio output for BMX bare-metal.
//

#ifndef dos_sound_h
#define dos_sound_h

namespace bmx {
namespace dos {

// Hardware PCM sink rate (ViceSound, shared with the C64/Atari kernels).
// The DOSBox-X mixer is forced to this rate at boot so no resampling is
// ever needed in the drain path.
const int kDosboxxSoundRate = 44100;

void sound_init(void);
void sound_play(void);

// Total mono frames handed to the sink (guest-speed proxy: /44100 =
// emulated seconds). Monotonic; for the 5s perf snapshot.
unsigned long long sound_frames_total(void);

// BMX sound menu backing store (Sound > Emulation). Counts/names feed
// multiple_choice items; current_* reads the live sections; set_* writes
// them (SB/OPL rebuild on the RESET event, so menu Reset applies).
int dos_sound_sbtype_count(void);
const char *dos_sound_sbtype_name(int i);
int dos_sound_sbtype_current(void);
void dos_sound_set_sbtype(int i);
int dos_sound_oplmode_count(void);
const char *dos_sound_oplmode_name(int i);
int dos_sound_oplmode_current(void);
void dos_sound_set_oplmode(int i);
int dos_sound_oplemu_count(void);
const char *dos_sound_oplemu_name(int i);
int dos_sound_oplemu_current(void);
void dos_sound_set_oplemu(int i);

}  // namespace dos
}  // namespace bmx

#endif
