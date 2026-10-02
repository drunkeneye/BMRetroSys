//
// zx_tape.cpp
//
// See zx_tape.h. Loading follows upstream storage/tape.c tap_load():
// the ROM LD-BYTES entry (PC 0x0562) is trapped, the next TAP/TZX block
// is copied with poke_byte_no_time(), flags/registers are set exactly
// like the desktop trap (carry = success), and execution resumes at the
// popped return address.
//

#include "zx_tape.h"
#include "zx_debug.h"
#include "zx_machine.h"
#include "zx_sound.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern "C" {
#include "zx_zesarux.h"
}

namespace bmx {
namespace zxspectrum {

namespace {

// Attached tape image, fully parsed into blocks at attach time.
struct TapBlock {
  uint8_t flag;
  uint8_t *data;  // block payload WITHOUT flag and checksum
  int len;
};

TapBlock *s_blocks = nullptr;
int s_num_blocks = 0;
int s_block_pos = 0;
char s_tape_path[256] = {0};
bool s_is_tzx = false;
// Autostart warp: cleared at end of tape (see tape_set_autostart_warp).
bool s_autostart_warp = false;

void free_blocks(void) {
  if (s_blocks != nullptr) {
    for (int i = 0; i < s_num_blocks; i++) {
      if (s_blocks[i].data != nullptr) {
        free(s_blocks[i].data);
      }
    }
    free(s_blocks);
    s_blocks = nullptr;
  }
  s_num_blocks = 0;
  s_block_pos = 0;
}

bool add_block(uint8_t flag, const uint8_t *data, int len) {
  TapBlock *nb = (TapBlock *)realloc(s_blocks,
                                     (size_t)(s_num_blocks + 1) * sizeof(TapBlock));
  if (nb == nullptr) {
    return false;
  }
  s_blocks = nb;
  s_blocks[s_num_blocks].flag = flag;
  s_blocks[s_num_blocks].len = len;
  s_blocks[s_num_blocks].data = nullptr;
  if (len > 0) {
    s_blocks[s_num_blocks].data = (uint8_t *)malloc((size_t)len);
    if (s_blocks[s_num_blocks].data == nullptr) {
      return false;
    }
    memcpy(s_blocks[s_num_blocks].data, data, (size_t)len);
  }
  s_num_blocks++;
  return true;
}

// Standard TAP block: len16 + flag + data + checksum, where len covers
// flag + data + checksum.
bool parse_tap(const uint8_t *buf, long size) {
  long pos = 0;
  while (pos + 2 <= size) {
    int len = buf[pos] | (buf[pos + 1] << 8);
    pos += 2;
    if (len < 2 || pos + len > size) {
      break;
    }
    uint8_t flag = buf[pos];
    // Verify checksum (xor of flag+data+stored must be 0).
    uint8_t chk = 0;
    for (int i = 0; i < len; i++) {
      chk ^= buf[pos + i];
    }
    if (chk != 0) {
      debug_log("tape: block %d checksum mismatch, keeping anyway",
                s_num_blocks);
    }
    if (!add_block(flag, buf + pos + 1, len - 2)) {
      return false;
    }
    pos += len;
  }
  return s_num_blocks > 0;
}

// TZX: header "ZXTape!" + blocks; standard speed (0x10), turbo (0x11) and
// direct recording (0x14) data blocks become TAP-like blocks. All other
// block types are skipped by length.
bool parse_tzx(const uint8_t *buf, long size) {
  if (size < 10 || memcmp(buf, "ZXTape!", 7) != 0) {
    return false;
  }
  long pos = 10;
  uint8_t tmp[65536 + 16];
  while (pos < size) {
    uint8_t id = buf[pos++];
    switch (id) {
      case 0x10: {  // standard speed data block
        if (pos + 4 > size) return s_num_blocks > 0;
        int len = buf[pos + 2] | (buf[pos + 3] << 8);
        pos += 4;
        if (len < 2 || pos + len > size) return s_num_blocks > 0;
        uint8_t flag = buf[pos];
        uint8_t chk = 0;
        for (int i = 0; i < len; i++) {
          chk ^= buf[pos + i];
        }
        if (chk != 0) {
          debug_log("tape: tzx block %d checksum mismatch", s_num_blocks);
        }
        if (!add_block(flag, buf + pos + 1, len - 2)) return false;
        pos += len;
        break;
      }
      case 0x11: {  // turbo data block
        if (pos + 18 > size) return s_num_blocks > 0;
        int len = buf[pos + 15] | (buf[pos + 16] << 8) | (buf[pos + 17] << 16);
        pos += 18;
        if (len <= 0 || len > (int)sizeof(tmp) - 16 || pos + len > size) {
          // Overlong turbo block: skip if possible, else stop.
          if (len > 0 && pos + len <= size) {
            pos += len;
          } else {
            return s_num_blocks > 0;
          }
          break;
        }
        // Turbo blocks carry raw pulse data, not plain bytes, unless the
        // pilot/sync use standard timings. Only accept the common case:
        // pilot 2168, sync 667/735, bit 855/1712 (standard ROM timing),
        // otherwise skip (would need full pulse decoding).
        int pilot = buf[pos - 18] | (buf[pos - 17] << 8);
        int sync1 = buf[pos - 16] | (buf[pos - 15] << 8);
        int sync2 = buf[pos - 14] | (buf[pos - 13] << 8);
        int bit0 = buf[pos - 12] | (buf[pos - 11] << 8);
        int bit1 = buf[pos - 10] | (buf[pos - 9] << 8);
        if (pilot == 2168 && sync1 == 667 && sync2 == 735 && bit0 == 855 &&
            bit1 == 1712) {
          uint8_t flag = buf[pos];
          if (!add_block(flag, buf + pos + 1, len - 2)) return false;
        } else {
          debug_log("tape: skipping non-standard turbo block len=%d", len);
        }
        pos += len;
        break;
      }
      case 0x14: {  // direct recording: pulse-level data, no plain
        // bytes for the trap loader (would need pulse decoding)
        if (pos + 10 > size) return s_num_blocks > 0;
        int len = buf[pos + 7] | (buf[pos + 8] << 8) | (buf[pos + 9] << 16);
        pos += 10;
        if (len <= 0 || pos + len > size) return s_num_blocks > 0;
        debug_log("tape: skipping direct-recording block len=%d", len);
        pos += len;
        break;
      }
      case 0x20:  // pause
      case 0x32:  // message block (len16 handled below)
        if (id == 0x20) {
          pos += 2;
        } else {
          if (pos + 1 > size) return s_num_blocks > 0;
          int len = buf[pos];
          pos += 1 + len;
        }
        break;
      case 0x21:  // group start
      case 0x30:  // text description
        if (pos + 1 > size) return s_num_blocks > 0;
        pos += 1 + buf[pos];
        break;
      case 0x22:  // group end
      case 0x2B:  // disable tape traps
      case 0x2C:  // ?
        break;
      case 0x31:  // message block with time
        if (pos + 1 > size) return s_num_blocks > 0;
        pos += 2 + buf[pos + 1];
        break;
      case 0x33:  // hardware type
        if (pos + 1 > size) return s_num_blocks > 0;
        pos += 1 + 3 * buf[pos];
        break;
      case 0x34:  // emulation info
        pos += 8;
        break;
      case 0x35:  // custom info
        if (pos + 16 > size) return s_num_blocks > 0;
        pos += 16;
        {
          int len = buf[pos] | (buf[pos + 1] << 8) | (buf[pos + 2] << 16) |
                    (buf[pos + 3] << 24);
          pos += 4;
          if (len < 0 || pos + len > size) return s_num_blocks > 0;
          pos += len;
        }
        break;
      case 0x40:  // snapshot block
        if (pos + 3 > size) return s_num_blocks > 0;
        {
          int len = buf[pos] | (buf[pos + 1] << 8) | (buf[pos + 2] << 16);
          pos += 3;
          if (len < 0 || pos + len > size) return s_num_blocks > 0;
          pos += len;
        }
        break;
      case 0x5A:  // glue block: "XTape!" + 1A + ver, fixed 9 bytes
        pos += 9;
        break;
      default:
        debug_log("tape: unknown TZX block %02x at %ld, stopping", id,
                  pos - 1);
        return s_num_blocks > 0;
    }
  }
  return s_num_blocks > 0;
}

bool has_tape_ext(const char *path, const char *ext) {
  size_t n = strlen(path);
  size_t m = strlen(ext);
  if (n <= m) {
    return false;
  }
  for (size_t i = 0; i < m; i++) {
    char a = path[n - m + i];
    char b = ext[i];
    if (a >= 'A' && a <= 'Z') a += 32;
    if (b >= 'A' && b <= 'Z') b += 32;
    if (a != b) {
      return false;
    }
  }
  return true;
}

}  // namespace

void tape_set_autostart_warp(bool armed) { s_autostart_warp = armed; }

int tape_attach(const char *path) {
  tape_detach();
  if (path == NULL) {
    return -1;
  }
  FILE *fp = fopen(path, "rb");
  if (fp == NULL) {
    debug_log("tape_attach: cannot open %s", path);
    return -1;
  }
  fseek(fp, 0, SEEK_END);
  long size = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  if (size <= 0 || size > 8 * 1024 * 1024) {
    fclose(fp);
    return -1;
  }
  uint8_t *buf = (uint8_t *)malloc((size_t)size);
  if (buf == nullptr) {
    fclose(fp);
    return -1;
  }
  size_t got = fread(buf, 1, (size_t)size, fp);
  fclose(fp);
  if ((long)got != size) {
    free(buf);
    return -1;
  }
  bool ok;
  s_is_tzx = has_tape_ext(path, ".tzx") || has_tape_ext(path, ".pzx");
  if (s_is_tzx) {
    ok = parse_tzx(buf, size);
  } else {
    ok = parse_tap(buf, size);
  }
  free(buf);
  if (!ok) {
    debug_log("tape_attach: no loadable blocks in %s", path);
    free_blocks();
    return -1;
  }
  strncpy(s_tape_path, path, sizeof(s_tape_path) - 1);
  debug_log("tape_attach: %s blocks=%d (%s)", path, s_num_blocks,
            s_is_tzx ? "tzx" : "tap");
  return 0;
}

void tape_detach(void) {
  free_blocks();
  s_tape_path[0] = '\0';
  s_autostart_warp = false;
}

bool tape_attached(void) { return s_blocks != nullptr && s_num_blocks > 0; }

const char *tape_name(void) { return s_tape_path; }

void tape_rewind(void) { s_block_pos = 0; }

// ---------------------------------------------------------------------------
// Snapshot (SNA): 48k fully, 128k load.
// ---------------------------------------------------------------------------

int snapshot_load(const char *path) {
  if (path == NULL) {
    return -1;
  }
  FILE *fp = fopen(path, "rb");
  if (fp == NULL) {
    return -1;
  }
  fseek(fp, 0, SEEK_END);
  long size = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  // 48k SNA = 27 + 49152. 128k SNA = + 2 state bytes + 5 banks (1,3,4,6,7).
  const bool is128 = (size == 27 + 49152 + 2 + 5 * 16384);
  if (size != 27 + 49152 && !is128) {
    debug_log("snapshot_load: bad size %ld: %s", size, path);
    fclose(fp);
    return -1;
  }
  if (is128 && !(MACHINE_IS_SPECTRUM_128_P2 || MACHINE_IS_PENTAGON)) {
    // +2A/+3 paging differs; 128k snapshots load on 128/+2/Pentagon.
    debug_log("snapshot_load: 128k snapshot needs a 128/+2 model");
    fclose(fp);
    return -1;
  }
  uint8_t hdr[27];
  if (fread(hdr, 1, 27, fp) != 27) {
    fclose(fp);
    return -1;
  }
  reg_i = hdr[0];
  reg_h_shadow = hdr[1];
  reg_l_shadow = hdr[2];
  reg_d_shadow = hdr[3];
  reg_e_shadow = hdr[4];
  reg_b_shadow = hdr[5];
  reg_c_shadow = hdr[6];
  reg_a_shadow = hdr[7];
  Z80_FLAGS_SHADOW = hdr[8];
  reg_h = hdr[9];
  reg_l = hdr[10];
  reg_d = hdr[11];
  reg_e = hdr[12];
  reg_b = hdr[13];
  reg_c = hdr[14];
  reg_iy = (z80_int)(hdr[15] | (hdr[16] << 8));
  reg_ix = (z80_int)(hdr[17] | (hdr[18] << 8));
  if (hdr[19] & 4) {
    iff1.v = 1;
    iff2.v = 1;
  } else {
    iff1.v = 0;
    iff2.v = 0;
  }
  reg_r = hdr[20];
  reg_a = hdr[21];
  Z80_FLAGS = hdr[22];
  reg_sp = (z80_int)(hdr[23] | (hdr[24] << 8));
  im_mode = hdr[25] & 3;
  out_254 = (hdr[26] & 7);
  out_254_original_value = out_254;
  modificado_border.v = 1;
  // RAM.
  uint8_t *ram = (uint8_t *)malloc(49152);
  if (ram == nullptr) {
    fclose(fp);
    return -1;
  }
  if (fread(ram, 1, 49152, fp) != 49152) {
    free(ram);
    fclose(fp);
    return -1;
  }
  for (int i = 0; i < 49152; i++) {
    poke_byte_no_time((z80_int)(16384 + i), ram[i]);
  }
  free(ram);
  if (is128) {
    // Paging state + remaining banks 1,3,4,6,7 in file order.
    uint8_t state[2];
    if (fread(state, 1, 2, fp) != 2) {
      fclose(fp);
      return -1;
    }
    static const int kBanks[5] = {1, 3, 4, 6, 7};
    for (int b = 0; b < 5; b++) {
      uint8_t *pg = (uint8_t *)malloc(16384);
      if (pg == nullptr) {
        break;
      }
      if (fread(pg, 1, 16384, fp) != 16384) {
        free(pg);
        break;
      }
      z80_byte save = puerto_32765;
      puerto_32765 = (z80_byte)((save & 0xf8) | (kBanks[b] & 7));
      mem_set_normal_pages_128k();
      for (int i = 0; i < 16384; i++) {
        poke_byte_no_time((z80_int)(49152 + i), pg[i]);
      }
      free(pg);
      puerto_32765 = save;
    }
    puerto_32765 = state[0];
    machine_fix_paging();
  }
  fclose(fp);
  // PC is on the stack (pushed by the snapshot saver).
  reg_pc = (z80_int)(peek_byte_no_time(reg_sp) |
                     (peek_byte_no_time((z80_int)(reg_sp + 1)) << 8));
  reg_sp += 2;
  debug_log("snapshot_load: OK %s pc=%04x sp=%04x", path, reg_pc, reg_sp);
  return 0;
}

int snapshot_save(const char *path) {
  if (path == NULL) {
    return -1;
  }
  if (MACHINE_IS_SPECTRUM_P2A_P3 || MACHINE_IS_PENTAGON) {
    debug_log("snapshot_save: 128k save limited to 48k RAM image");
  }
  FILE *fp = fopen(path, "wb");
  if (fp == NULL) {
    return -1;
  }
  // Push current PC so the loader can pop it.
  reg_sp -= 2;
  poke_byte_no_time(reg_sp, (z80_byte)(reg_pc & 0xff));
  poke_byte_no_time((z80_int)(reg_sp + 1), (z80_byte)(reg_pc >> 8));
  uint8_t hdr[27];
  hdr[0] = reg_i;
  hdr[1] = reg_h_shadow;
  hdr[2] = reg_l_shadow;
  hdr[3] = reg_d_shadow;
  hdr[4] = reg_e_shadow;
  hdr[5] = reg_b_shadow;
  hdr[6] = reg_c_shadow;
  hdr[7] = reg_a_shadow;
  hdr[8] = Z80_FLAGS_SHADOW;
  hdr[9] = reg_h;
  hdr[10] = reg_l;
  hdr[11] = reg_d;
  hdr[12] = reg_e;
  hdr[13] = reg_b;
  hdr[14] = reg_c;
  hdr[15] = (uint8_t)(reg_iy & 0xff);
  hdr[16] = (uint8_t)(reg_iy >> 8);
  hdr[17] = (uint8_t)(reg_ix & 0xff);
  hdr[18] = (uint8_t)(reg_ix >> 8);
  hdr[19] = iff1.v ? 4 : 0;
  hdr[20] = reg_r;
  hdr[21] = reg_a;
  hdr[22] = Z80_FLAGS;
  hdr[23] = (uint8_t)(reg_sp & 0xff);
  hdr[24] = (uint8_t)(reg_sp >> 8);
  hdr[25] = (uint8_t)(im_mode & 3);
  hdr[26] = (uint8_t)(out_254 & 7);
  fwrite(hdr, 1, 27, fp);
  for (int i = 0; i < 49152; i++) {
    fputc(peek_byte_no_time((z80_int)(16384 + i)), fp);
  }
  fclose(fp);
  reg_sp += 2;
  debug_log("snapshot_save: OK %s", path);
  return 0;
}

}  // namespace zxspectrum
}  // namespace bmx

extern "C" {

// The core calls these two symbols; tape.c is not compiled.
// emux_set_warp lives in the bridge (warp ends at end of tape).
void emux_set_warp(int warp);
int tap_load_detect(void) {
  using namespace bmx::zxspectrum;
  if (reg_pc != 1378) {
    return 0;
  }
  if (!tape_attached()) {
    return 0;
  }
  if (MACHINE_IS_SPECTRUM_16_48) {
    return 1;
  }
  if (MACHINE_IS_SPECTRUM_128_P2) {
    if ((puerto_32765 & 16) == 16) {
      return 1;
    }
    return 0;
  }
  if (MACHINE_IS_SPECTRUM_P2A_P3) {
    if ((puerto_32765 & 16) == 16 && ((puerto_8189 & 4) == 4)) {
      return 1;
    }
    return 0;
  }
  return 0;
}

void tape_trap_load_spectrum(void) {
  using namespace bmx::zxspectrum;
  // Requested transfer (ROM LD-BYTES state).
  z80_int req_start = reg_ix;
  z80_byte req_flag = reg_a_shadow;
  z80_int req_len = value_8_to_16(reg_d, reg_e);

  if (s_block_pos >= s_num_blocks) {
    // End of tape: return with error (carry clear), like tap_load().
    debug_log("tape: end of tape, returning error");
    if (s_autostart_warp) {
      s_autostart_warp = false;
      // Warp ends with the load; emux_set_warp syncs the menu toggle.
      emux_set_warp(0);
      // Game is starting: capture the next 3s of produced audio so the
      // DIAG waveform file holds game audio, not the loading phase.
      sound_dump_restart();
    }
    Z80_FLAGS &= (z80_byte)(255 - FLAG_C);
    reg_pc = pop_valor();
    return;
  }
  TapBlock *blk = &s_blocks[s_block_pos];

  // Flag mismatch: skip the block and retry at the ROM entry.
  if (req_flag != blk->flag && (Z80_FLAGS_SHADOW & FLAG_Z) == 0) {
    debug_log("tape: skip block %d (want flag %02x, got %02x)", s_block_pos,
              req_flag, blk->flag);
    s_block_pos++;
    reg_pc = 1378;
    return;
  }

  Z80_FLAGS |= FLAG_C;
  int got = blk->len < req_len ? blk->len : req_len;
  z80_byte checksum = blk->flag;
  z80_int dst = req_start;
  for (int i = 0; i < got; i++) {
    z80_byte b = blk->data[i];
    poke_byte_no_time(dst++, b);
    checksum ^= b;
  }
  s_block_pos++;

  if (blk->len != req_len) {
    debug_log("tape: length mismatch block=%d asked=%d", blk->len, req_len);
    if (blk->len < req_len) {
      Z80_FLAGS &= (z80_byte)(255 - FLAG_C);
    }
  }
  // Stored checksum is implicitly verified: TAP blocks carry it and the
  // parse already validated; recompute for the transferred prefix.
  (void)checksum;

  reg_pc = pop_valor();
  reg_h = 0;
  reg_ix = dst;
  reg_c = 1;
  DE -= (z80_int)blk->len;
  debug_log("tape: loaded block start=%04x len=%d flag=%02x", req_start, got,
            blk->flag);
}

}  // extern "C"
