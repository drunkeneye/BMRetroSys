//
// bbc_sound.h
//
// Speaker::Delegate sink: mono S16 packets into a FIFO, Circle drain at
// 44.1 kHz (volume via the shared audiovolume menu value).
//

#ifndef BMX_BBC_SOUND_H
#define BMX_BBC_SOUND_H

namespace bmx {
namespace bbc {

void sound_init(void);
void sound_play(void);
unsigned long sound_frames_total(void);
unsigned long sound_drop_frames(void);
unsigned long sound_starve_calls(void);
int sound_last_in_samples(void);
int sound_out_min(void);
int sound_out_max(void);
void sound_window_reset(void);

}  // namespace bbc
}  // namespace bmx

#endif  // BMX_BBC_SOUND_H
