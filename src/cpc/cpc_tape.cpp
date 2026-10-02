//
// cpc_tape.cpp
//
// See cpc_tape.h. CDT is the TZX format under another extension; only the
// pulse-level blocks are implemented (no trap):
//   0x10 standard data, 0x11 turbo data (own timings), 0x20 pause,
//   text/group blocks skipped by length. Loop/jump/stop/select blocks
//   (0x23-0x28, 0x2A, ...) and direct recording (0x14) stop the parse with
//   a log line: a partial tape in defined state beats misordered pulses.
//
// Player model: each data block is pilot x N, sync1, sync2, then data
// bytes MSB-first (each bit = two pulses of the bit length), then the
// block pause. Level toggles at every pulse edge, starting HIGH (idle
// pulled up). The timetable advances on consumed T-states (called per
// opcode from cpc_core.c); the firmware reads the level through PPI
// Port B bit7, so any loader (ROM or custom) works as long as its pulse
// timings are in the file.
//

#include "cpc_tape.h"
#include "cpc_tape_iface.h"
#include "cpc_debug.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" int cpc_hw_motor_on(void);

namespace bmx {
namespace cpc {

namespace {

struct TapeBlock {
  uint8_t type;  // 0x10, 0x11 data or 0x20 pause
  uint16_t pilot_len;
  uint16_t pilot_t;
  uint16_t sync1_t;
  uint16_t sync2_t;
  uint16_t bit0_t;
  uint16_t bit1_t;
  uint8_t *data;
  uint32_t data_len;
  uint16_t pause_ms;
  uint8_t last_bits;  // valid data bits in the final byte (0x11 only)
};

const int kMaxBlocks = 4096;
const uint32_t kMaxTotalData = 8 * 1024 * 1024;

TapeBlock *s_blocks = nullptr;
int s_num_blocks = 0;
char s_name[256] = {0};
bool s_user_playing = true;

// Player state.
int s_blk = 0;             // current block index
int s_phase = 0;           // 0 pilot, 1 sync1, 2 sync2, 3 data, 4 pause, 5 end
uint32_t s_pilot_left = 0;
uint32_t s_byte_pos = 0;
int s_bit_pos = 7;
uint32_t s_pause_left_t = 0;  // T-states remaining in pause
uint32_t s_pulse_left_t = 0;  // T-states remaining in current pulse
int s_level = 1;              // current EAR level (idle high)
bool s_finished = true;

void free_blocks(void) {
  if (s_blocks != nullptr) {
    for (int i = 0; i < s_num_blocks; i++) {
      free(s_blocks[i].data);
    }
    free(s_blocks);
    s_blocks = nullptr;
  }
  s_num_blocks = 0;
}

bool add_block(const TapeBlock &b) {
  if (s_num_blocks >= kMaxBlocks) {
    return false;
  }
  TapeBlock *nb =
      (TapeBlock *)realloc(s_blocks, (size_t)(s_num_blocks + 1) * sizeof(TapeBlock));
  if (nb == nullptr) {
    return false;
  }
  s_blocks = nb;
  s_blocks[s_num_blocks++] = b;
  return true;
}

uint16_t rd16(const uint8_t *p) {
  return (uint16_t)(p[0] | ((uint16_t)p[1] << 8));
}

uint32_t rd24(const uint8_t *p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}

bool parse_cdt(const uint8_t *buf, long size) {
  free_blocks();
  if (size < 10 || memcmp(buf, "ZXTape!", 7) != 0) {
    debug_log("tape: not a CDT/TZX image");
    return false;
  }
  long pos = 10;
  uint32_t total_data = 0;
  while (pos < size) {
    uint8_t id = buf[pos++];
    if (id == 0x10) {
      if (pos + 4 > size) {
        break;
      }
      uint16_t pause_ms = rd16(buf + pos);
      uint16_t len = rd16(buf + pos + 2);
      pos += 4;
      if (len < 1 || pos + len > size) {
        break;
      }
      if (total_data + len > kMaxTotalData) {
        debug_log("tape: image too large, stopping at block %d", s_num_blocks);
        break;
      }
      uint8_t *data = (uint8_t *)malloc(len);
      if (data == nullptr) {
        break;
      }
      memcpy(data, buf + pos, len);
      pos += len;
      total_data += len;
      TapeBlock b = {0x10, 3220, 2168, 667, 735, 855, 1712, data, len,
                     pause_ms, 8};
      if (!add_block(b)) {
        free(data);
        break;
      }
    } else if (id == 0x11) {
      if (pos + 18 > size) {
        break;
      }
      uint16_t pilot_t = rd16(buf + pos);
      uint16_t sync1_t = rd16(buf + pos + 2);
      uint16_t sync2_t = rd16(buf + pos + 4);
      uint16_t bit0_t = rd16(buf + pos + 6);
      uint16_t bit1_t = rd16(buf + pos + 8);
      uint16_t pilot_len = rd16(buf + pos + 10);
      uint32_t len = (uint32_t)buf[pos + 15] | ((uint32_t)buf[pos + 16] << 8) |
                     ((uint32_t)buf[pos + 17] << 16);
      uint16_t pause_ms = rd16(buf + pos + 13);
      uint8_t last_bits = buf[pos + 12];
      if (last_bits == 0 || last_bits > 8) {
        last_bits = 8;
      }
      pos += 18;
      if (len < 1 || pos + (long)len > size) {
        break;
      }
      if (total_data + len > kMaxTotalData) {
        debug_log("tape: image too large, stopping at block %d", s_num_blocks);
        break;
      }
      uint8_t *data = (uint8_t *)malloc(len);
      if (data == nullptr) {
        break;
      }
      memcpy(data, buf + pos, len);
      pos += (long)len;
      total_data += len;
      TapeBlock b = {0x11, pilot_len, pilot_t, sync1_t,  sync2_t,
                     bit0_t, bit1_t, data,     len,      pause_ms,
                     last_bits};
      if (!add_block(b)) {
        free(data);
        break;
      }
    } else if (id == 0x14) {      debug_log("tape: skipping direct-recording block (no pulse list yet)");
      if (pos + 10 > size) {
        break;
      }
      uint32_t len = rd24(buf + pos + 7);
      pos += 10;
      if (len == 0 || pos + (long)len > size) {
        break;
      }
      pos += (long)len;
    } else if (id == 0x13) {
      // Pulse sequence: raw pulses, not byte data. Skipped by length
      // (playing them needs a generic pulse-list block, v2).
      if (pos + 1 > size) {
        break;
      }
      unsigned n = buf[pos];
      if (pos + 1 + (long)n * 2 > size) {
        break;
      }
      pos += 1 + (long)n * 2;
      debug_log("tape: skipping pulse-sequence block (%u pulses)", n);
    } else if (id == 0x20) {
      if (pos + 2 > size) {
        break;
      }
      TapeBlock b = {};
      b.type = 0x20;
      b.pause_ms = rd16(buf + pos);
      pos += 2;
      if (!add_block(b)) {
        break;
      }
    } else if (id == 0x32) {
      // Archive info: 2-byte length (excluding the length field itself).
      // NOTE: a 1-byte length here desyncs the whole parse (seen as a
      // bogus "unsupported block 12" later); the TZX spec uses WORD.
      if (pos + 2 > size) {
        break;
      }
      unsigned len = rd16(buf + pos);
      if (pos + 2 + (long)len > size) {
        break;
      }
      pos += 2 + (long)len;
    } else if (id == 0x33) {
      // Hardware info: count byte + 4 bytes per entry.
      if (pos + 1 > size) {
        break;
      }
      unsigned n = buf[pos];
      if (pos + 1 + (long)n * 4 > size) {
        break;
      }
      pos += 1 + (long)n * 4;
    } else if (id == 0x34) {
      // Emulation info: fixed 8 bytes.
      if (pos + 8 > size) {
        break;
      }
      pos += 8;
    } else if (id == 0x35) {
      // Custom info: 16-byte ID + 4-byte length + data.
      if (pos + 20 > size) {
        break;
      }
      uint32_t len = (uint32_t)buf[pos + 16] | ((uint32_t)buf[pos + 17] << 8) |
                     ((uint32_t)buf[pos + 18] << 16) |
                     ((uint32_t)buf[pos + 19] << 24);
      if ((long)len < 0 || pos + 20 + (long)len > size) {
        break;
      }
      pos += 20 + (long)len;
    } else if (id == 0x21 || id == 0x30) {
      if (pos + 1 > size) {
        break;
      }
      pos += 1 + buf[pos];
    } else if (id == 0x22 || id == 0x2B) {
      // No payload.
    } else if (id == 0x31) {
      if (pos + 1 > size) {
        break;
      }
      pos += 2 + buf[pos + 1];
    } else if (id == 0x32) {
      if (pos + 1 > size) {
        break;
      }
      pos += 1 + buf[pos];
    } else {
      debug_log("tape: unsupported block %02x at end, stopping (%d blocks ok)",
                id, s_num_blocks);
      break;
    }
  }
  return s_num_blocks > 0;
}

// Enter block s_blk (phase pilot or pause).
void enter_block(void) {
  if (s_blk >= s_num_blocks) {
    if (!s_finished) {
      debug_log("tape: stream finished (%d blocks)", s_num_blocks);
    }
    s_finished = true;
    return;
  }
  const TapeBlock &b = s_blocks[s_blk];
  if (b.type == 0x20) {
    s_phase = 4;
    s_pause_left_t = (uint32_t)b.pause_ms * 4000U;
    if (s_pause_left_t == 0) {
      s_blk++;
      enter_block();
    }
    return;
  }
  s_phase = 0;
  s_pilot_left = b.pilot_len;
  s_byte_pos = 0;
  s_bit_pos = 7;
  if (s_pilot_left == 0) {
    s_phase = 1;
  }
  s_pulse_left_t = b.pilot_t;
}

// Advance within the current block to the next pulse; sets
// s_pulse_left_t for it. Moves to the next block (or finish) at the end.
void next_pulse(void) {
  const TapeBlock &b = s_blocks[s_blk];
  if (s_phase == 0) {
    if (s_pilot_left > 1) {
      s_pilot_left--;
      s_pulse_left_t = b.pilot_t;
      return;
    }
    s_phase = 1;
    s_pulse_left_t = b.sync1_t;
    return;
  }
  if (s_phase == 1) {
    s_phase = 2;
    s_pulse_left_t = b.sync2_t;
    return;
  }
  if (s_phase == 2) {
    if (b.data_len == 0) {
      s_phase = 4;
      s_pause_left_t = (uint32_t)b.pause_ms * 4000U;
      return;
    }
    s_phase = 3;
    s_byte_pos = 0;
    s_bit_pos = 7;
    s_pilot_left = 0;  // reused as data half-bit toggle below
  }
  if (s_phase == 3) {
    // Two pulses per bit, MSB first (each toggles the level on expiry).
    // The final byte may be partial (0x11 last-bits): only its top
    // last_bits bits are played.
    const uint8_t byte = b.data[s_byte_pos];
    const int bit = (byte >> s_bit_pos) & 1;
    s_pulse_left_t = bit ? b.bit1_t : b.bit0_t;
    const int end_bit = (s_byte_pos == b.data_len - 1) ? (8 - b.last_bits) : 0;
    // Track half-bits: reuse s_pilot_left as the half counter (0/1).
    if (s_pilot_left == 0) {
      s_pilot_left = 1;
    } else {
      s_pilot_left = 0;
      if (s_bit_pos == end_bit) {
        s_bit_pos = 7;
        s_byte_pos++;
        if (s_byte_pos >= b.data_len) {
          s_phase = 4;
          s_pause_left_t = (uint32_t)b.pause_ms * 4000U;
        }
      } else {
        s_bit_pos--;
      }
    }
    return;
  }
  // Phase 4 (pause) never has pulses; handled in advance().
  s_blk++;
  enter_block();
}

}  // namespace

int tape_attach(const char *path) {
  tape_detach();
  if (path == nullptr || path[0] == '\0') {
    return -1;
  }
  FILE *fp = fopen(path, "rb");
  if (fp == nullptr) {
    debug_log("tape: cannot open %s", path);
    return -1;
  }
  fseek(fp, 0, SEEK_END);
  long size = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  if (size <= 10 || size > 32 * 1024 * 1024) {
    fclose(fp);
    debug_log("tape: bad size %ld", size);
    return -1;
  }
  uint8_t *buf = (uint8_t *)malloc((size_t)size);
  if (buf == nullptr) {
    fclose(fp);
    return -1;
  }
  size_t got = fread(buf, 1, (size_t)size, fp);
  fclose(fp);
  if ((long)got != size || !parse_cdt(buf, size)) {
    free(buf);
    free_blocks();
    debug_log("tape: parse failed %s", path);
    return -1;
  }
  free(buf);
  snprintf(s_name, sizeof(s_name), "%s", path);
  s_user_playing = true;
  tape_rewind();
  int data_blocks = 0;
  for (int i = 0; i < s_num_blocks; i++) {
    if (s_blocks[i].type != 0x20) {
      data_blocks++;
    }
  }
  debug_log("tape: attached %s blocks=%d data=%d", path, s_num_blocks,
            data_blocks);
  return 0;
}

void tape_detach(void) {
  free_blocks();
  s_name[0] = '\0';
  s_blk = 0;
  s_phase = 5;
  s_level = 1;
  s_finished = true;
}

bool tape_attached(void) { return s_blocks != nullptr && s_num_blocks > 0; }

const char *tape_name(void) { return s_name; }

void tape_rewind(void) {
  s_blk = 0;
  s_level = 1;
  s_finished = false;
  s_pilot_left = 0;
  enter_block();
}

void tape_set_playing(bool playing) { s_user_playing = playing; }
bool tape_playing(void) { return s_user_playing; }

void tape_progress(int *current, int *total) {  int data_total = 0, data_cur = 0;
  for (int i = 0; i < s_num_blocks; i++) {
    if (s_blocks[i].type != 0x20) {
      data_total++;
      if (i < s_blk) {
        data_cur++;
      }
    }
  }
  if (current != nullptr) {
    *current = data_cur;
  }
  if (total != nullptr) {
    *total = data_total;
  }
}

bool tape_finished(void) { return s_finished; }

}  // namespace cpc
}  // namespace bmx

extern "C" {

void cpc_tape_advance(unsigned tstates) {
  using namespace bmx::cpc;
  if (!s_user_playing || s_finished || s_blk >= s_num_blocks) {
    return;
  }
  if (!cpc_hw_motor_on()) {
    return;
  }
  unsigned left = tstates;
  // Bound single-call work: blocks are pulse-dense, pulses are hundreds
  // of T-states; a huge catch-up (menu stall) must not spin here.
  unsigned guard = 0;
  while (left > 0 && !s_finished && guard < 4096) {
    guard++;
    if (s_phase == 4) {
      if (left >= s_pause_left_t) {
        left -= s_pause_left_t;
        s_pause_left_t = 0;
        s_blk++;
        enter_block();
        continue;
      }
      s_pause_left_t -= left;
      return;
    }
    if (s_phase == 5) {
      s_finished = true;
      return;
    }
    if (s_pulse_left_t == 0) {
      next_pulse();
      continue;
    }
    if (left >= s_pulse_left_t) {
      left -= s_pulse_left_t;
      s_pulse_left_t = 0;
      s_level ^= 1;
      next_pulse();
      continue;
    }
    s_pulse_left_t -= left;
    return;
  }
}

int cpc_tape_level(void) {
  using namespace bmx::cpc;
  return s_level;
}

int cpc_tape_motor_on(void) { return cpc_hw_motor_on(); }

int cpc_tape_finished(void) {
  using namespace bmx::cpc;
  return s_finished ? 1 : 0;
}

// Tape screech for the audio mix: modest square around silence, only while
// actually moving tape.
int cpc_tape_audio(void) {
  using namespace bmx::cpc;
  if (!s_user_playing || s_finished || !cpc_hw_motor_on()) {
    return 0;
  }
  return s_level ? 20 : -20;
}

}  // extern "C"
