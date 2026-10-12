//
// bbc_fan.cpp
//
// See bbc_fan.h. The Pi5 firmware drives the fan from config.txt dtparams
// (fan_temp0..3 + hyst + speed, cooling_fan); there is no runtime API on
// bare metal, so the curve is rewritten here and picked up at reboot.
// Render mirrors menu_switch.c: line-copy to /config.new with the old
// BMX fan block excised, the new block appended last (last dtparam wins),
// then backup + commit. Any failure aborts before touching /config.txt.
//

#include "bbc_fan.h"
#include "bbc_debug.h"

#include <stdio.h>
#include <string.h>

namespace bmx {
namespace bbc {

namespace {

const char *kConfigPath = "/config.txt";
const char *kNewPath = "/config.new";
const char *kBackupPath = "/config.bak";

const char *kBegin = "# BMX-FAN-BEGIN";
const char *kEnd = "# BMX-FAN-END";

// Quiet is the default: matches the active curve in sdcard/config.txt.
int s_mode = BMX_FAN_QUIET;

bool copy_file(const char *from, const char *to) {
  FILE *in = fopen(from, "r");
  if (in == nullptr) {
    return false;
  }
  FILE *out = fopen(to, "w");
  if (out == nullptr) {
    fclose(in);
    return false;
  }
  int c;
  bool ok = true;
  while ((c = fgetc(in)) != EOF) {
    if (fputc(c, out) == EOF) {
      ok = false;
      break;
    }
  }
  fclose(in);
  if (fclose(out) != 0) {
    ok = false;
  }
  return ok;
}

bool is_marker(const char *line, const char *marker) {
  while (*line == ' ' || *line == '\t') {
    line++;
  }
  size_t n = strlen(line);
  while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
    n--;
  }
  return strlen(marker) == n && memcmp(line, marker, n) == 0;
}

void write_curve(FILE *out, int mode) {
  fprintf(out, "%s\n", kBegin);
  fprintf(out, "# BMX fan curve (reboot to apply; Fan menu)\n");
  fprintf(out, "dtparam=cooling_fan=on\n");
  if (mode == BMX_FAN_QUIET) {
    fprintf(out, "dtparam=fan_temp0=55000\n");
    fprintf(out, "dtparam=fan_temp0_hyst=5000\n");
    fprintf(out, "dtparam=fan_temp0_speed=60\n");
    fprintf(out, "dtparam=fan_temp1=65000\n");
    fprintf(out, "dtparam=fan_temp1_hyst=5000\n");
    fprintf(out, "dtparam=fan_temp1_speed=125\n");
    fprintf(out, "dtparam=fan_temp2=72500\n");
    fprintf(out, "dtparam=fan_temp2_hyst=5000\n");
    fprintf(out, "dtparam=fan_temp2_speed=175\n");
    fprintf(out, "dtparam=fan_temp3=80000\n");
    fprintf(out, "dtparam=fan_temp3_hyst=5000\n");
    fprintf(out, "dtparam=fan_temp3_speed=255\n");
  } else if (mode == BMX_FAN_COOL) {
    fprintf(out, "dtparam=fan_temp0=40000\n");
    fprintf(out, "dtparam=fan_temp0_hyst=5000\n");
    fprintf(out, "dtparam=fan_temp0_speed=75\n");
    fprintf(out, "dtparam=fan_temp1=50000\n");
    fprintf(out, "dtparam=fan_temp1_hyst=5000\n");
    fprintf(out, "dtparam=fan_temp1_speed=125\n");
    fprintf(out, "dtparam=fan_temp2=60000\n");
    fprintf(out, "dtparam=fan_temp2_hyst=5000\n");
    fprintf(out, "dtparam=fan_temp2_speed=175\n");
    fprintf(out, "dtparam=fan_temp3=70000\n");
    fprintf(out, "dtparam=fan_temp3_hyst=5000\n");
    fprintf(out, "dtparam=fan_temp3_speed=255\n");
  }
  fprintf(out, "%s\n", kEnd);
}

}  // namespace

const char *fan_mode_name(int mode) {
  switch (mode) {
    case BMX_FAN_QUIET:
      return "Quiet (55C+)";
    case BMX_FAN_COOL:
      return "Cool (40C+)";
    default:
      return "Default";
  }
}

int fan_get_mode(void) { return s_mode; }

bool fan_set_mode(int mode) {
  if (mode < 0 || mode >= BMX_FAN_MODES) {
    return false;
  }
  FILE *in = fopen(kConfigPath, "r");
  if (in == nullptr) {
    debug_log("fan: cannot read %s", kConfigPath);
    return false;
  }
  FILE *out = fopen(kNewPath, "w");
  if (out == nullptr) {
    fclose(in);
    debug_log("fan: cannot write %s", kNewPath);
    return false;
  }
  // Copy everything except any previous BMX fan block.
  char line[512];
  bool in_block = false;
  bool saw_begin = false;
  while (fgets(line, sizeof(line), in) != nullptr) {
    if (is_marker(line, kBegin)) {
      in_block = true;
      saw_begin = true;
      continue;
    }
    if (is_marker(line, kEnd)) {
      in_block = false;
      continue;
    }
    if (!in_block) {
      fputs(line, out);
    }
  }
  fclose(in);
  if (mode != BMX_FAN_DEFAULT) {
    write_curve(out, mode);
  } else if (!saw_begin) {
    // Nothing to remove and nothing to write; still a success (already
    // default), but skip the commit to avoid churning the card.
    fclose(out);
    remove(kNewPath);
    s_mode = mode;
    debug_log("fan: already default, config untouched");
    return true;
  }
  if (fclose(out) != 0) {
    debug_log("fan: failed closing %s", kNewPath);
    return false;
  }
  if (!copy_file(kConfigPath, kBackupPath)) {
    debug_log("fan: backup to %s failed", kBackupPath);
    return false;
  }
  if (!copy_file(kNewPath, kConfigPath)) {
    debug_log("fan: commit failed, restoring backup");
    copy_file(kBackupPath, kConfigPath);
    return false;
  }
  remove(kNewPath);
  s_mode = mode;
  debug_log("fan: mode %d (%s) written to %s, reboot to apply", mode,
            fan_mode_name(mode), kConfigPath);
  return true;
}

}  // namespace bbc
}  // namespace bmx
