//
// cpc_sound.h
//
// CPC audio for BMX bare-metal: the AY PSG (via the vendored audio.c mixer)
// produces signed 8-bit stereo at ~15625 Hz (one stereo pair per scanline,
// 312 per frame); this drains it per frame, resampled to the 44.1 kHz
// Circle sink (circle_sound_write). Tape screech is mixed in by the core
// loop before the samples land in the buffer.
//

#ifndef cpc_sound_h
#define cpc_sound_h

namespace bmx {
namespace cpc {

void sound_init(void);
void sound_play(void);
// Total 44.1 kHz frames handed to the sink (heartbeat diagnostics).
unsigned long sound_frames_total(void);
// Output sample range over the current heartbeat window.
int sound_out_min(void);
int sound_out_max(void);
void sound_window_reset(void);
// Flow diagnostics: sink-full drops vs empty drains, last drain size.
unsigned long sound_drop_frames(void);
unsigned long sound_starve_calls(void);
int sound_last_in_samples(void);
// DIAG waveform capture: restarts the 30s SYS:/CPCAUDIO.RAW window.
void sound_dump_restart(void);

}  // namespace cpc
}  // namespace bmx

#endif
