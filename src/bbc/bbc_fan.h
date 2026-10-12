//
// bbc_fan.h
//
// Pi5 fan curve management (firmware-owned FAN_PWM; BMX cannot touch it at
// runtime). The curve lives in /config.txt as dtparams and applies at
// boot, exactly like arm_freq/over_voltage. This module maintains a
// BMX-managed block there (same .new/.bak/commit pattern as the kernel
// switcher in menu_switch.c). Reboot to apply.
//

#ifndef BMX_BBC_FAN_H
#define BMX_BBC_FAN_H

namespace bmx {
namespace bbc {

// Fan modes (persisted as bbc_fan in settings-bbc.txt).
enum {
  BMX_FAN_DEFAULT = 0,  // no BMX block: firmware defaults
  BMX_FAN_QUIET = 1,    // starts 55C whisper-quiet, full tilt past 80C
  BMX_FAN_COOL = 2,     // starts 40C, full tilt past 70C
  BMX_FAN_MODES = 3,
};

const char *fan_mode_name(int mode);
int fan_get_mode(void);
// Writes the curve into /config.txt (with .bak backup). Returns true on
// success; the curve applies at the next reboot.
bool fan_set_mode(int mode);

}  // namespace bbc
}  // namespace bmx

#endif  // BMX_BBC_FAN_H
