//
// zx_sound.h
//
// ZX Spectrum audio for BMX bare-metal: the ZEsarUX mixer produces signed
// 8-bit stereo at 15600 Hz (one sample per scanline); this drains it per
// frame, resampled to the 44.1 kHz Circle sink (circle_sound_write).
//

#ifndef zx_sound_h
#define zx_sound_h

namespace bmx {
namespace zxspectrum {

void sound_init(void);
void sound_play(void);
// Total 44.1 kHz frames handed to the sink (heartbeat diagnostics).
// NOTE: the sink's AddChunk returns 0 unconditionally, so accepted vs
// dropped cannot be measured here; this counts attempted frames.
unsigned long sound_frames_total(void);
// Output sample range over the current heartbeat window (proves the
// produced content is live/churning vs stuck).
int sound_out_min(void);
int sound_out_max(void);
void sound_window_reset(void);
// Flow diagnostics: frames dropped because the sink had no space (we run
// fast) vs drains with nothing produced (we run slow / audio off), plus
// the core sample count of the last drain (311 vs 312 scanlines).
unsigned long sound_drop_frames(void);
unsigned long sound_starve_calls(void);
int sound_last_in_samples(void);
// DIAG waveform capture: restarts the 30s SYS:/ZXAUDIO.RAW window (called
// on tape attach and at autostart-warp end, so the file holds game audio,
// not boot silence).
void sound_dump_restart(void);

}  // namespace zxspectrum
}  // namespace bmx

#endif
