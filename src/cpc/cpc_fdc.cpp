//
// cpc_fdc.cpp
//
// See cpc_fdc.h. uPD765A subset for the CPC firmware (AMSDOS + BIOS):
// SPECIFY, SENSE DRIVE, RECALIBRATE, SENSE INT, SEEK, READ/WRITE DATA,
// READ/WRITE DELETED, READ ID, READ TRACK, FORMAT. Everything else
// returns invalid (ST0 0x80), including SCAN and relative seeks.
//
// Model notes:
// - No timing anywhere: RQM is always set, seeks/motor act instantly.
//   Firmware timeouts and polling loops behave (they just never wait).
// - Deleted-data marks from DSK headers are honored (CM + SK skip).
// - Weak/fuzzy bits are NOT modelled (stable data returned).
// - Multi-track (MT) reads continue on the other head at R=1.
// - Writes and formats patch the in-memory image and flush the whole
//   file to SD immediately (game saves are rare; safest against loss).
// - No disc or motor off reads as NOT READY (ST0 0x48), like real HW.
//

#include "cpc_fdc.h"
#include "cpc_dsk.h"
#include "cpc_debug.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace bmx {
namespace cpc {

namespace {

// Command byte indices (Caprice32 layout).
enum {
  CMD_CODE = 0,
  CMD_UNIT = 1,
  CMD_C = 2,
  CMD_H = 3,
  CMD_R = 4,
  CMD_N = 5,
  CMD_EOT = 6,
  CMD_GPL = 7,
  CMD_DTL = 8,
  RES_ST0 = 0,
  RES_ST1 = 1,
  RES_ST2 = 2,
  RES_C = 3,
  RES_H = 4,
  RES_R = 5,
  RES_N = 6,
};

enum Phase { PH_CMD, PH_EXEC, PH_RESULT };
enum Dir { DIR_NONE, DIR_READ, DIR_WRITE };

struct FdcDrive {
  DskImage *img;
  int track;
  int side;
  int id_index;  // round-robin position for READ ID
  bool seek_pending;
  int seek_unit;
  int seek_head;
  char path[256];
};

FdcDrive s_drives[2] = {};
Phase s_phase = PH_CMD;
uint8_t s_cmd[10] = {0};
int s_cmd_need = 0;  // total command bytes expected
int s_cmd_got = 0;
uint8_t s_res[10] = {0};
int s_res_len = 0;
int s_res_idx = 0;
Dir s_dir = DIR_NONE;
bool s_mt = false;
bool s_sk = false;
// Execution transfer state.
DskSector *s_sec = nullptr;
const uint8_t *s_rd_ptr = nullptr;
uint8_t s_wr_buf[8192];
uint32_t s_xfer_left = 0;
uint32_t s_xfer_total = 0;
// FORMAT collect state.
uint8_t s_fmt_buf[256];
int s_fmt_need = 0;
int s_fmt_got = 0;
// Current command context (drive/head/cylinder).
int s_drive = 0;
int s_head = 0;
bool s_motor = false;

FdcDrive *active_drive(void) { return &s_drives[s_drive & 1]; }

bool drive_ready(FdcDrive *d) {
  return d->img != nullptr && s_motor;
}

// ST0 for a fresh command: unit/head + NR when not ready.
bool check_ready(void) {
  FdcDrive *d = active_drive();
  memset(s_res, 0, sizeof(s_res));
  uint8_t st0 = (uint8_t)((s_cmd[CMD_UNIT] & 7));
  if (!drive_ready(d)) {
    st0 |= 0x48;  // AT + Not Ready
    s_res[RES_ST0] = st0;
    return false;
  }
  s_res[RES_ST0] = st0;
  return true;
}

// FDC trace (one line per command + one per result set). Commands are
// low-frequency during loads, so this stays readable. Remove once disc
// support is device-verified.
void trace_command(void) {
  debug_log("fdc: cmd %02X %02X %02X %02X %02X %02X %02X %02X %02X", s_cmd[0],
            s_cmd[1], s_cmd[2], s_cmd[3], s_cmd[4], s_cmd[5], s_cmd[6],
            s_cmd[7], s_cmd[8]);
}

void trace_result(void) {
  debug_log("fdc: res ST0=%02X ST1=%02X ST2=%02X C=%02X H=%02X R=%02X N=%02X",
            s_res[0], s_res[1], s_res[2], s_res[3], s_res[4], s_res[5],
            s_res[6]);
}

void to_result(void) {
  s_phase = PH_RESULT;
  trace_result();
}

void load_chrn(void) {
  s_res[RES_C] = s_cmd[CMD_C];
  s_res[RES_H] = s_cmd[CMD_H];
  s_res[RES_R] = s_cmd[CMD_R];
  s_res[RES_N] = s_cmd[CMD_N];
}

// Normal end-of-track termination (AT + End of Cylinder), Caprice32's
// LOAD_RESULT_WITH_STATUS for the no-error case.
void load_ec_status(void) {
  s_res[RES_ST0] |= 0x40;
  s_res[RES_ST1] |= 0x80;
}

// If the sector ID exists on the track with a different cylinder,
// report No Cylinder (else Bad Cylinder only for 0xFF, skipped here).
void check_cylinder(DskImage *img, int track, int side, uint8_t c,
                    uint8_t r) {
  if (img == nullptr || track < 0 || track >= img->num_tracks || side < 0 ||
      side >= img->num_sides) {
    return;
  }
  const DskTrack *tr = &img->tracks[track * img->num_sides + side];
  for (int i = 0; i < tr->num_sectors; i++) {
    if (tr->sectors[i].r == r && tr->sectors[i].c != c) {
      s_res[RES_ST2] |= 0x10;  // No Cylinder
      return;
    }
  }
}

uint32_t data_length(void) {
  if (s_cmd[CMD_N] == 0) {
    uint32_t dtl = s_cmd[CMD_DTL];
    return dtl > 0x80 ? 0x80 : dtl;
  }
  return 128U << (s_cmd[CMD_N] & 7);
}

void flush_image(FdcDrive *d) {
  if (d->img == nullptr || !d->img->dirty) {
    return;
  }
  size_t len = 0;
  uint8_t *buf = dsk_serialize(d->img, &len);
  if (buf == nullptr || len == 0) {
    free(buf);
    return;
  }
  FILE *fp = fopen(d->img->path, "wb");
  if (fp == nullptr) {
    debug_log("fdc: cannot write back %s", d->img->path);
    free(buf);
    return;
  }
  size_t wrote = fwrite(buf, 1, len, fp);
  fclose(fp);
  free(buf);
  if (wrote == len) {
    d->img->dirty = false;
    debug_log("fdc: saved %lu bytes to %s", (unsigned long)len,
              d->img->path);
  } else {
    debug_log("fdc: short write %s", d->img->path);
  }
}

// Forward declarations for the transfer loop.
void read_next(void);
void write_next(void);

void read_next(void) {
  FdcDrive *d = active_drive();
  DskImage *img = d->img;
  const int side = (img->num_sides > 1) ? s_head : 0;
  DskSector *sec = dsk_find_sector(img, d->track, side, s_cmd[CMD_C],
                                   (uint8_t)s_head, s_cmd[CMD_R],
                                   s_cmd[CMD_N]);
  if (sec == nullptr) {
    check_cylinder(img, d->track, side, s_cmd[CMD_C], s_cmd[CMD_R]);
    s_res[RES_ST0] |= 0x40;
    s_res[RES_ST1] |= 0x04;  // No Data
    load_chrn();
    to_result();
    return;
  }
  s_res[RES_ST1] = sec->st1 & 0x25;
  s_res[RES_ST2] = sec->st2 & 0x61;
  const bool is_rdel = (s_cmd[CMD_CODE] & 0x1F) == 0x0C;
  if (is_rdel) {
    s_res[RES_ST2] ^= 0x40;
  }
  if (s_sk && (s_res[RES_ST2] & 0x40)) {
    // Wrong address mark for this command: skip unless at EOT.
    if (s_cmd[CMD_R] != s_cmd[CMD_EOT]) {
      s_cmd[CMD_R]++;
      read_next();
      return;
    }
    load_ec_status();
    load_chrn();
    to_result();
    return;
  }
  if (s_res[RES_ST2] & 0x40) {
    s_cmd[CMD_EOT] = s_cmd[CMD_R];  // opposite AM ends here
  }
  uint32_t n = data_length();
  if (n > sec->len) {
    n = sec->len;
  }
  s_sec = sec;
  s_rd_ptr = sec->data;
  s_xfer_left = n;
  s_xfer_total = n;
  s_dir = DIR_READ;
  // Transfer proceeds as the CPU reads the data register.
}

void read_done(void) {
  // A sector's bytes all transferred.
  if ((s_res[RES_ST1] & 0x31) || (s_res[RES_ST2] & 0x21)) {
    load_ec_status();
    load_chrn();
    to_result();
    return;
  }
  if (s_cmd[CMD_R] != s_cmd[CMD_EOT]) {
    s_cmd[CMD_R]++;
    read_next();
    return;
  }
  if (s_mt && s_head == 0) {
    s_head = 1;
    s_cmd[CMD_H] = 1;
    s_cmd[CMD_R] = 1;
    read_next();
    return;
  }
  load_ec_status();
  load_chrn();
  to_result();
}

void write_next(void) {
  FdcDrive *d = active_drive();
  DskImage *img = d->img;
  const int side = (img->num_sides > 1) ? s_head : 0;
  DskSector *sec = dsk_find_sector(img, d->track, side, s_cmd[CMD_C],
                                   (uint8_t)s_head, s_cmd[CMD_R],
                                   s_cmd[CMD_N]);
  if (sec == nullptr) {
    check_cylinder(img, d->track, side, s_cmd[CMD_C], s_cmd[CMD_R]);
    s_res[RES_ST0] |= 0x40;
    s_res[RES_ST1] |= 0x04;
    load_chrn();
    to_result();
    return;
  }
  const bool is_wdel = (s_cmd[CMD_CODE] & 0x1F) == 0x09;
  sec->st1 = 0;
  sec->st2 = is_wdel ? 0x40 : 0;
  s_sec = sec;
  uint32_t n = data_length();
  if (n > sizeof(s_wr_buf)) {
    n = sizeof(s_wr_buf);
  }
  if (n > sec->len) {
    n = sec->len;
  }
  s_xfer_left = n;
  s_xfer_total = n;
  s_dir = DIR_WRITE;
}

void write_done(void) {
  FdcDrive *d = active_drive();
  if (s_sec != nullptr && s_xfer_total != 0) {
    const uint32_t n =
        s_xfer_total < s_sec->len ? s_xfer_total : s_sec->len;
    memcpy(s_sec->data, s_wr_buf, n);
    d->img->dirty = true;
    flush_image(d);
  }
  if (s_cmd[CMD_R] != s_cmd[CMD_EOT]) {
    s_cmd[CMD_R]++;
    write_next();
    return;
  }
  load_ec_status();
  load_chrn();
  to_result();
}

// Command handlers (run once the full command phase arrived).
void h_invalid(void) {
  s_res[0] = 0x80;
  s_res_len = 1;
  s_res_idx = 0;
  to_result();
}

void h_specify(void) { s_phase = PH_CMD; }

void h_sense_drive(void) {
  FdcDrive *d = active_drive();
  uint8_t st3 = (uint8_t)(s_cmd[CMD_UNIT] & 7);
  if (d->img != nullptr && d->img->num_sides > 1) {
    st3 |= 0x08;
  }
  st3 |= (uint8_t)((s_head & 1) << 2);
  if (drive_ready(d)) {
    st3 |= 0x20;
  }
  if (d->track == 0) {
    st3 |= 0x10;
  }
  s_res[0] = st3;
  s_res_len = 1;
  s_res_idx = 0;
  to_result();
}

void h_recal(void) {
  FdcDrive *d = active_drive();
  if (drive_ready(d)) {
    d->track = 0;
  }
  d->seek_pending = true;
  d->seek_unit = s_cmd[CMD_UNIT] & 1;
  d->seek_head = (s_cmd[CMD_UNIT] >> 2) & 1;
  s_phase = PH_CMD;
}

void h_seek(void) {
  FdcDrive *d = active_drive();
  if (drive_ready(d) && d->img != nullptr) {
    int t = s_cmd[CMD_C];
    if (t >= d->img->num_tracks) {
      t = d->img->num_tracks - 1;
    }
    if (t < 0) {
      t = 0;
    }
    d->track = t;
  }
  d->seek_pending = true;
  d->seek_unit = s_cmd[CMD_UNIT] & 1;
  d->seek_head = (s_cmd[CMD_UNIT] >> 2) & 1;
  s_phase = PH_CMD;
}

void h_sense_int(void) {
  for (int i = 0; i < 2; i++) {
    if (s_drives[i].seek_pending) {
      s_drives[i].seek_pending = false;
      s_res[0] = (uint8_t)(0x20 | (s_drives[i].seek_unit & 1) |
                           ((s_drives[i].seek_head & 1) << 2));
      s_res[1] = (uint8_t)s_drives[i].track;
      s_res_len = 2;
      s_res_idx = 0;
      to_result();
      return;
    }
  }
  s_res[0] = 0x80;
  s_res_len = 1;
  s_res_idx = 0;
  to_result();
}

void h_read_id(void) {
  FdcDrive *d = active_drive();
  if (!check_ready()) {
    load_chrn();
    s_res_len = 7;
    s_res_idx = 0;
    to_result();
    return;
  }
  const int side = (d->img->num_sides > 1) ? s_head : 0;
  DskTrack *tr = &d->img->tracks[d->track * d->img->num_sides + side];
  if (tr->num_sectors == 0) {
    s_res[RES_ST0] |= 0x40;
    s_res[RES_ST1] |= 0x01;
    load_chrn();
  } else {
    if (d->id_index >= tr->num_sectors) {
      d->id_index = 0;
    }
    const DskSector *sec = &tr->sectors[d->id_index];
    d->id_index++;
    s_res[RES_C] = sec->c;
    s_res[RES_H] = sec->h;
    s_res[RES_R] = sec->r;
    s_res[RES_N] = sec->n;
  }
  s_res_len = 7;
  s_res_idx = 0;
  to_result();
}

void h_read(void) {
  if (!check_ready()) {
    load_chrn();
    s_res_len = 7;
    s_res_idx = 0;
    to_result();
    return;
  }
  FdcDrive *d = active_drive();
  const int side = (d->img->num_sides > 1) ? s_head : 0;
  DskTrack *tr = &d->img->tracks[d->track * d->img->num_sides + side];
  if (tr->num_sectors == 0) {
    s_res[RES_ST0] |= 0x40;
    s_res[RES_ST1] |= 0x01;
    load_chrn();
    s_res_len = 7;
    s_res_idx = 0;
    to_result();
    return;
  }
  s_res_len = 7;
  s_res_idx = 0;
  read_next();
  // read_next either stages a transfer (stays in EXEC) or resolved.
}

void h_write(void) {
  if (!check_ready()) {
    load_chrn();
    s_res_len = 7;
    s_res_idx = 0;
    to_result();
    return;
  }
  FdcDrive *d = active_drive();
  const int side = (d->img->num_sides > 1) ? s_head : 0;
  DskTrack *tr = &d->img->tracks[d->track * d->img->num_sides + side];
  if (tr->num_sectors == 0) {
    s_res[RES_ST0] |= 0x40;
    s_res[RES_ST1] |= 0x01;
    load_chrn();
    s_res_len = 7;
    s_res_idx = 0;
    to_result();
    return;
  }
  s_res_len = 7;
  s_res_idx = 0;
  write_next();
}

void h_read_track(void) {
  if (!check_ready()) {
    load_chrn();
    s_res_len = 7;
    s_res_idx = 0;
    to_result();
    return;
  }
  FdcDrive *d = active_drive();
  const int side = (d->img->num_sides > 1) ? s_head : 0;
  DskTrack *tr = &d->img->tracks[d->track * d->img->num_sides + side];
  if (tr->num_sectors == 0) {
    s_res[RES_ST0] |= 0x40;
    s_res[RES_ST1] |= 0x01;
    load_chrn();
    s_res_len = 7;
    s_res_idx = 0;
    to_result();
    return;
  }
  // Start at the first sector; EOT counts sectors to transfer. The
  // request must match (else No Data, like the reference behavior).
  DskSector *sec = &tr->sectors[0];
  if (sec->c != s_cmd[CMD_C] || sec->h != (uint8_t)s_head ||
      sec->r != s_cmd[CMD_R] || sec->n != s_cmd[CMD_N]) {
    s_res[RES_ST0] |= 0x40;
    s_res[RES_ST1] |= 0x04;
    load_chrn();
    s_res_len = 7;
    s_res_idx = 0;
    to_result();
    return;
  }
  s_res[RES_ST1] = sec->st1 & 0x25;
  s_res[RES_ST2] = sec->st2 & 0x61;
  s_cmd[CMD_C] = sec->c;
  s_cmd[CMD_H] = sec->h;
  s_cmd[CMD_R] = sec->r;
  s_cmd[CMD_N] = sec->n;
  uint32_t n = 128U << (sec->n & 7);
  if (n > sec->len) {
    n = sec->len;
  }
  s_sec = sec;
  s_rd_ptr = sec->data;
  s_xfer_left = n;
  s_xfer_total = n;
  s_dir = DIR_READ;
  s_res_len = 7;
  s_res_idx = 0;
  // Remaining EOT-1 sectors follow as bytes run out (handled on read).
}

void h_format(void) {
  if (!check_ready()) {
    load_chrn();
    s_res_len = 7;
    s_res_idx = 0;
    to_result();
    return;
  }
  // Collect 4 * SC bytes of CHRN stream, then rebuild the track.
  int sc = s_cmd[3];
  if (sc <= 0 || sc > 32) {
    s_res[0] = 0x80;
    s_res_len = 1;
    s_res_idx = 0;
    to_result();
    return;
  }
  s_fmt_need = sc * 4;
  s_fmt_got = 0;
  s_dir = DIR_WRITE;
  s_sec = nullptr;  // marks format collect (vs sector write)
  s_res_len = 7;
  s_res_idx = 0;
  // Bytes arrive through the data register into s_fmt_buf.
}

void h_version(void) {
  s_res[0] = 0x90;
  s_res_len = 1;
  s_res_idx = 0;
  to_result();
}

void h_configure(void) { s_phase = PH_CMD; }

void h_dumpreg(void) {
  for (int i = 0; i < 10; i++) {
    s_res[i] = 0;
  }
  s_res_len = 10;
  s_res_idx = 0;
  to_result();
}

void h_lock(void) {
  s_res[0] = 0x00;
  s_res_len = 1;
  s_res_idx = 0;
  to_result();
}

// Decode the first command byte once the phase starts.
void run_command(void) {
  trace_command();
  const uint8_t op = s_cmd[CMD_CODE];
  const uint8_t base = op & 0x1F;
  s_mt = false;
  s_sk = false;
  s_dir = DIR_NONE;
  s_sec = nullptr;
  // Data-phase commands with MFM + optional MT/SK.
  if ((op & 0x40) &&
      (base == 0x06 || base == 0x05 || base == 0x09 || base == 0x0C ||
       base == 0x02 || base == 0x0A || base == 0x0D)) {
    s_mt = (op & 0x80) != 0;
    s_sk = (op & 0x20) != 0;
    s_drive = s_cmd[CMD_UNIT] & 1;
    s_head = (s_cmd[CMD_UNIT] >> 2) & 1;
    switch (base) {
      case 0x06:
        h_read();
        return;
      case 0x05:
        h_write();
        return;  // 0x45 write, 0x49 deleted handled inside via opcode
      case 0x09:
        h_write();
        return;
      case 0x0C:
        h_read();
        return;
      case 0x02:
        h_read_track();
        return;
      case 0x0A:
        h_read_id();
        return;
      case 0x0D:
        h_format();
        return;
    }
  }
  s_drive = s_cmd[CMD_UNIT] & 1;
  s_head = (s_cmd[CMD_UNIT] >> 2) & 1;
  switch (op) {
    case 0x03:
      h_specify();
      return;
    case 0x04:
      h_sense_drive();
      return;
    case 0x07:
      h_recal();
      return;
    case 0x08:
      h_sense_int();
      return;
    case 0x0F:
      h_seek();
      return;
    case 0x10:
      h_version();
      return;
    case 0x13:
      h_configure();
      return;
    case 0x0E:
      h_dumpreg();
      return;
    case 0x94:
      h_lock();
      return;
    default:
      break;
  }
  h_invalid();
}

int command_length(uint8_t op) {
  const uint8_t base = op & 0x1F;
  if ((op & 0x40) &&
      (base == 0x06 || base == 0x05 || base == 0x09 || base == 0x0C ||
       base == 0x02)) {
    return 9;
  }
  if ((op & 0x40) && (base == 0x0A || base == 0x0D)) {
    return base == 0x0A ? 2 : 6;
  }
  switch (op) {
    case 0x03:
      return 3;
    case 0x04:
      return 2;
    case 0x07:
      return 2;
    case 0x08:
      return 1;
    case 0x0F:
      return 3;
    case 0x10:
      return 1;
    case 0x13:
      return 4;
    case 0x0E:
      return 1;
    case 0x94:
      return 2;
    default:
      return 1;  // unknown: single byte, then invalid result
  }
}

}  // namespace

void fdc_reset(void) {
  s_phase = PH_CMD;
  s_cmd_need = 0;
  s_cmd_got = 0;
  s_res_len = 0;
  s_res_idx = 0;
  s_dir = DIR_NONE;
  s_mt = s_sk = false;
  s_sec = nullptr;
  s_xfer_left = s_xfer_total = 0;
  s_fmt_need = s_fmt_got = 0;
  for (int i = 0; i < 2; i++) {
    s_drives[i].seek_pending = false;
  }
  s_motor = false;
}

void fdc_set_motor(int on) { s_motor = on ? true : false; }
int fdc_motor(void) { return s_motor ? 1 : 0; }

int fdc_attach(int drive, const char *path) {
  if (drive < 0 || drive > 1 || path == nullptr || path[0] == '\0') {
    return -1;
  }
  FILE *fp = fopen(path, "rb");
  if (fp == nullptr) {
    debug_log("fdc: cannot open %s", path);
    return -1;
  }
  fseek(fp, 0, SEEK_END);
  long size = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  if (size <= 0 || (size_t)size > 8 * 1024 * 1024) {
    fclose(fp);
    debug_log("fdc: bad size %ld %s", size, path);
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
  DskImage *img = dsk_parse(buf, (size_t)size, path);
  free(buf);
  if (img == nullptr) {
    debug_log("fdc: parse failed %s", path);
    return -1;
  }
  fdc_detach(drive);
  FdcDrive *d = &s_drives[drive];
  d->img = img;
  d->track = 0;
  d->side = 0;
  d->id_index = 0;
  d->seek_pending = false;
  snprintf(d->path, sizeof(d->path), "%s", path);
  debug_log("fdc: drive %c attached %s", drive == 0 ? 'A' : 'B', path);
  // Log the first tracks' sector IDs: tells missing-sector reports
  // (firmware hunting an ID) apart from parse drops at a glance.
  for (int t = 0; t < img->num_tracks && t < 3; t++) {
    for (int s = 0; s < img->num_sides; s++) {
      const DskTrack *tr = &img->tracks[t * img->num_sides + s];
      char ids[160];
      size_t o = 0;
      for (int i = 0; i < tr->num_sectors && o + 12 < sizeof(ids); i++) {
        o += (size_t)snprintf(ids + o, sizeof(ids) - o, "%02X ",
                              tr->sectors[i].r);
      }
      debug_log("fdc: t%d s%d: %d sectors [%s]", t, s, tr->num_sectors,
                ids);
    }
  }
  return 0;
}

void fdc_detach(int drive) {
  if (drive < 0 || drive > 1) {
    return;
  }
  FdcDrive *d = &s_drives[drive];
  dsk_free(d->img);
  d->img = nullptr;
  d->path[0] = '\0';
  d->track = 0;
  d->id_index = 0;
  d->seek_pending = false;
}

const char *fdc_attached_name(int drive) {
  if (drive < 0 || drive > 1 || s_drives[drive].img == nullptr) {
    return "";
  }
  return s_drives[drive].path;
}

// Best boot file for autostart (DISC.BAS > first *.BAS > first file).
// Returns 0 + filename without quotes, -1 when nothing found.
int fdc_boot_file(int drive, char *out, size_t out_size) {
  if (drive < 0 || drive > 1 || s_drives[drive].img == nullptr ||
      out == nullptr || out_size == 0) {
    return -1;
  }
  int rc = dsk_boot_file(s_drives[drive].img, out, out_size);
  if (rc == 0) {
    debug_log("fdc: boot file %s", out);
  } else {
    debug_log("fdc: no boot file found");
  }
  return rc;
}

bool fdc_attached(int drive) {
  if (drive < 0 || drive > 1) {
    return false;
  }
  return s_drives[drive].img != nullptr;
}

}  // namespace cpc
}  // namespace bmx

// Vendored port interface (operaciones.c calls these when the machine has
// a floppy unit and pd765_enabled is set by machine init).
extern "C" {

void cpc_fdc_set_motor(int on) { bmx::cpc::fdc_set_motor(on); }

uint8_t pd765_read_status_register(void) {
  using namespace bmx::cpc;
  uint8_t msr = 0x80;  // RQM always set (synchronous model)
  if (s_phase == PH_EXEC) {
    msr |= 0x10;  // busy
    if (s_dir == DIR_READ) {
      msr |= 0x60;  // DIO + non-DMA
    } else if (s_dir == DIR_WRITE) {
      msr |= 0x20;  // non-DMA
    }
  } else if (s_phase == PH_RESULT) {
    msr |= 0x50;  // DIO + busy
  } else if (s_cmd_got != 0) {
    msr |= 0x10;  // receiving command bytes
  }
  return msr;
}

uint8_t pd765_read(void) {
  using namespace bmx::cpc;
  if (s_phase == PH_EXEC && s_dir == DIR_READ && s_sec != nullptr) {
    uint8_t v = *s_rd_ptr++;
    if (--s_xfer_left == 0) {
      // Sector done: advance like a transfer completion.
      if (s_cmd[CMD_CODE] == 0x42) {
        // READ TRACK: EOT counts down sectors.
        if (--s_cmd[CMD_EOT] != 0) {
          FdcDrive *d = active_drive();
          DskImage *img = d->img;
          const int side = (img->num_sides > 1) ? s_head : 0;
          DskTrack *tr =
              &img->tracks[d->track * img->num_sides + side];
          int idx = 0;
          for (int i = 0; i < tr->num_sectors; i++) {
            if (&tr->sectors[i] == s_sec) {
              idx = i + 1;
              break;
            }
          }
          if (idx >= tr->num_sectors) {
            idx = 0;
          }
          s_sec = &tr->sectors[idx];
          s_cmd[CMD_R]++;
          uint32_t n = 128U << (s_sec->n & 7);
          if (n > s_sec->len) {
            n = s_sec->len;
          }
          s_rd_ptr = s_sec->data;
          s_xfer_left = n;
          return v;
        }
        load_ec_status();
        load_chrn();
        to_result();
        return v;
      }
      read_done();
    }
    return v;
  }
  if (s_phase == PH_RESULT && s_res_idx < s_res_len) {
    uint8_t v = s_res[s_res_idx++];
    if (s_res_idx >= s_res_len) {
      s_res_idx = 0;
      s_res_len = 0;
      s_phase = PH_CMD;
      s_cmd_got = 0;
      s_dir = DIR_NONE;
    }
    return v;
  }
  return 0xFF;
}

void pd765_out_port_data_register(uint8_t value) {
  using namespace bmx::cpc;
  if (s_phase == PH_EXEC && s_dir == DIR_WRITE) {
    if (s_sec == nullptr) {
      // FORMAT collect.
      if (s_fmt_got < (int)sizeof(s_fmt_buf) && s_fmt_got < s_fmt_need) {
        s_fmt_buf[s_fmt_got++] = value;
      }
      if (s_fmt_got >= s_fmt_need && s_fmt_need > 0) {
        FdcDrive *d = active_drive();
        const int count = s_fmt_need / 4;
        uint8_t n = s_cmd[2];
        uint8_t fill = s_cmd[5];
        const int side = (d->img->num_sides > 1) ? s_head : 0;
        if (dsk_format_track(d->img, d->track, side, s_fmt_buf, count,
                             fill)) {
          flush_image(d);
        }
        s_res[RES_ST0] = (uint8_t)(s_cmd[CMD_UNIT] & 7);
        s_res[RES_ST1] = 0;
        s_res[RES_ST2] = 0;
        s_res[RES_C] = s_fmt_buf[(count - 1) * 4];
        s_res[RES_H] = s_fmt_buf[(count - 1) * 4 + 1];
        s_res[RES_R] = s_fmt_buf[(count - 1) * 4 + 2];
        s_res[RES_N] = n;
        s_res_len = 7;
        s_res_idx = 0;
        to_result();
      }
      return;
    }
    if (s_xfer_left > 0 && (s_xfer_total - s_xfer_left) < sizeof(s_wr_buf)) {
      s_wr_buf[s_xfer_total - s_xfer_left] = value;
      if (--s_xfer_left == 0) {
        write_done();
      }
    }
    return;
  }
  if (s_phase != PH_CMD) {
    return;
  }
  if (s_cmd_got == 0) {
    s_cmd[0] = value;
    s_cmd_need = command_length(value);
    s_cmd_got = 1;
    if (s_cmd_need <= 1) {
      s_cmd_got = 0;
      s_phase = PH_EXEC;
      run_command();
      return;
    }
    s_phase = PH_CMD;
    return;
  }
  if (s_cmd_got < 10) {
    s_cmd[s_cmd_got++] = value;
  }
  if (s_cmd_got >= s_cmd_need) {
    s_cmd_got = 0;
    s_phase = PH_EXEC;
    run_command();
  }
}

}  // extern "C"
