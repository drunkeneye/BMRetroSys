//
// msx_sound.h
//
// MSX audio for BMX bare-metal: the AY PSG (via the vendored audio.c mixer)
// produces signed 8-bit stereo at ~15600 Hz (one stereo pair per scanline,
// 312 per frame); this drains it per frame, resampled to the 44.1 kHz
// Circle sink (circle_sound_write). Tape screech is mixed in by the core
// loop before the samples land in the buffer.
//

#ifndef msx_sound_h
#define msx_sound_h

namespace bmx {
namespace msx {

void sound_init(void);
void sound_play(void);
// Total 44.1 kHz frames handed to the sink (heartbeat diagnostics).
unsigned long sound_frames_total(void);
// Output sample range over the current heartbeat window.
int sound_out_min(void);
int sound_out_max(void);
// Raw AY input range (signed 8-bit pairs) over the window: tells an AY
// producer stall apart from a drain/resampler problem.
int sound_in_min(void);
int sound_in_max(void);
void sound_window_reset(void);
// Flow diagnostics: sink-full drops vs empty drains, last drain size.
unsigned long sound_drop_frames(void);
unsigned long sound_starve_calls(void);
int sound_last_in_samples(void);
// Minimum sink free-space over the window (256*16 total): near-zero proves
// sink underrun (DMA running dry) that drop/starve cannot show.
int sound_sink_min_space(void);
// Max AY R8 volume nibble programmed over the window + envelope-bit flag.
int sound_ay8_max(void);
int sound_ay8_env(void);
// DIAG waveform capture: continuous MONO 16-bit recording from boot,
// split into 5 s chunks SYS:/MSXA0000.RAW, MSXA0001.RAW, ... (~441 KB
// each). Closed chunks are final on disk (safe to copy, survive card
// pulls). Write errors disable it with a loud log line.
void sound_dump_restart(void);

}  // namespace msx
}  // namespace bmx

#endif
