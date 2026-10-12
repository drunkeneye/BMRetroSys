//
// bbc_sound.cpp
//
// See bbc_sound.h. CLK Speaker::Delegate sink: mono S16 packets at the
// negotiated rate go into a FIFO; sound_play() drains it to the Circle
// 44.1 kHz stereo sink (mono duplicated, volume via audiovolume).
//

#include "bbc_sound.h"
#include "bbc_debug.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <vector>

extern "C" {
#include "third_party/common/circle.h"
}

// CLK core (vendored subset under third_party/bbc).
#include "Outputs/Speaker/Speaker.hpp"

// Shared volume (0-100, driven by the menu). The Apple II kernel defines
// its own; the BBC kernel does the same.
int audiovolume = 100;

namespace bmx {
namespace bbc {

namespace {

const int kSinkRate = 44100;
const int kPacketSize = 512;

// Core -> drain FIFO (mono samples).
const int kFifoSamples = 32768;
int16_t s_fifo[kFifoSamples];
int s_fifo_head = 0;
int s_fifo_tail = 0;
int s_fifo_count = 0;

int16_t s_out[1024 * 2];
unsigned long s_frames_total = 0;
unsigned long s_drop_frames = 0;
unsigned long s_starve_calls = 0;
int s_last_in_samples = 0;
int s_out_min = 32767;
int s_out_max = -32768;

Outputs::Speaker::Speaker *s_speaker = nullptr;
bool s_first_packet_logged = false;

void fifo_push(int16_t v) {
  if (s_fifo_count >= kFifoSamples) {
    return;  // overrun: refuse newest (keeps phase aligned)
  }
  s_fifo[s_fifo_head] = v;
  s_fifo_head = (s_fifo_head + 1) % kFifoSamples;
  s_fifo_count++;
}

class BbcSpeakerDelegate : public Outputs::Speaker::Speaker::Delegate {
 public:
  void speaker_did_complete_samples(
      Outputs::Speaker::Speaker & /*speaker*/,
      const std::vector<int16_t> &buffer) override {
    if (!s_first_packet_logged) {
      s_first_packet_logged = true;
      debug_log("sound: first packet n=%u stereo=%d",
                (unsigned)buffer.size(), 0);
    }
    for (size_t i = 0; i < buffer.size(); i++) {
      fifo_push(buffer[i]);
    }
  }
};

BbcSpeakerDelegate s_delegate;

void drain_produced(void) {
  if (s_fifo_count <= 0) {
    s_starve_calls++;
    return;
  }
  const int vol = audiovolume;
  int n = s_fifo_count;
  const int cap = (int)(sizeof(s_out) / sizeof(s_out[0])) / 2;
  if (n > cap) {
    n = cap;
  }
  int space = circle_sound_bufferspace();
  if (space < 0) {
    space = 0;
  }
  if (n > space) {
    n = space;
  }
  if (n <= 0) {
    s_drop_frames++;
    return;
  }
  s_last_in_samples = n;
  for (int i = 0; i < n; i++) {
    int idx = (s_fifo_tail + i) % kFifoSamples;
    int v = (int)s_fifo[idx] * vol / 100;
    if (v > 32767) {
      v = 32767;
    }
    if (v < -32768) {
      v = -32768;
    }
    if (v < s_out_min) {
      s_out_min = v;
    }
    if (v > s_out_max) {
      s_out_max = v;
    }
    s_out[i * 2] = (int16_t)v;
    s_out[i * 2 + 1] = (int16_t)v;
  }
  circle_sound_write(s_out, (size_t)n);
  s_frames_total += (unsigned long)n;
  s_fifo_tail = (s_fifo_tail + n) % kFifoSamples;
  s_fifo_count -= n;
}

}  // namespace

void bbc_sound_set_speaker(Outputs::Speaker::Speaker *speaker) {
  s_speaker = speaker;
  if (s_speaker == nullptr) {
    debug_log("sound: no speaker from core (mute)");
    return;
  }
  const float ideal =
      s_speaker->get_ideal_clock_rate_in_range(40000.0f, 48000.0f);
  float rate = ideal;
  if (rate < 40000.0f || rate > 48000.0f) {
    rate = (float)kSinkRate;
  }
  s_speaker->set_output_rate(rate, kPacketSize, false);
  s_speaker->set_delegate(&s_delegate);
  s_speaker->set_output_volume(1.0f);
  debug_log("sound: speaker rate=%.0f packet=%d stereo=%d", (double)rate,
            kPacketSize, s_speaker->get_is_stereo() ? 1 : 0);
}

void sound_init(void) {
  s_fifo_head = s_fifo_tail = s_fifo_count = 0;
  debug_log("sound_init: BMX drain (%dHz mono->stereo)", kSinkRate);
}

void sound_play(void) { drain_produced(); }

unsigned long sound_frames_total(void) { return s_frames_total; }
unsigned long sound_drop_frames(void) { return s_drop_frames; }
unsigned long sound_starve_calls(void) { return s_starve_calls; }
int sound_last_in_samples(void) { return s_last_in_samples; }
int sound_out_min(void) { return s_out_min; }
int sound_out_max(void) { return s_out_max; }
void sound_window_reset(void) {
  s_out_min = 32767;
  s_out_max = -32768;
}

}  // namespace bbc
}  // namespace bmx
