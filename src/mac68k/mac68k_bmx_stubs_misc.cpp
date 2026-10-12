//
// mac68k_bmx_stubs_misc.cpp
//
// BMX bare-metal providers for the Basilisk II backends that have no BMX
// hardware (clipboard, serial ports, ethernet, SCSI). Log-once no-op stubs
// modelled on third_party/basilisk2/dummy/*.cpp.
//
// Deliberately NOT defined here (owned elsewhere):
//   - SerialOpen/Prime/Control/Status/Close/Interrupt + the_serd_port:
//     portable serial.cpp (compiled per README.BMX included subset).
//   - EtherInit/EtherExit/EtherReset + ether_udp_read: portable ether.cpp.
//   - SCSICmd et al.: portable scsi.cpp (only the scsi_* platform layer
//     declared in scsi.h is defined here).
//   - CDROM*: portable cdrom.cpp; nocdrom=true so CDROMInit finds zero
//     drives via the log-only SysAddCDROMPrefs (zero drives tolerated).
//   - ExtFS*: nothing references them with SUPPORTS_EXTFS=0 (all call sites
//     in main.cpp/emul_op.cpp are #if-guarded).
//   - Audio*/Video*: owned by the audio/video workstreams (not this one).
//   - Timer*/ADB*/Sony*/Disk*/XPRAMInit: portable timer.cpp/adb.cpp/
//     sony.cpp/disk.cpp/xpram.cpp. The timer platform time funcs
//     (TimerDateTime etc.) live in mac68k_bmx_tick.cpp.
//

#include "sysdeps.h"

#include "mac68k_debug.h"

#include "cpu_emulation.h"
#include "main.h"
#include "macos_util.h"
#include "prefs.h"
#include "clip.h"
#include "serial.h"
#include "serial_defs.h"
#include "ether.h"
#include "scsi.h"
#include "user_strings.h"

#include <stdio.h>
#include <string.h>

namespace {

bool s_clip_logged = false;
bool s_serial_logged = false;
bool s_ether_logged = false;
bool s_scsi_logged = false;

void log_once(bool &flag, const char *fmt, const char *arg) {
  if (!flag) {
    flag = true;
    if (arg != NULL) {
      bmx::mac68k::debug_log(fmt, arg);
    } else {
      bmx::mac68k::debug_log("%s", fmt);
    }
  }
}

}  // namespace

// ---- Clipboard (no BMX clipboard; mirrors clip_dummy.cpp) ----

void ClipInit(void) {
  log_once(s_clip_logged, "mac68k: ClipInit (log-only, no clipboard)", NULL);
}

void ClipExit(void) {
  bmx::mac68k::debug_log("mac68k: ClipExit (log-only)");
}

void GetScrap(void **handle, uint32 type, int32 offset) {
  bmx::mac68k::debug_log("mac68k: GetScrap handle=%p type=0x%08x offset=%d "
                         "(no clipboard, ignoring)",
                         (void *)handle, (unsigned)type, (int)offset);
}

void ZeroScrap() {
  bmx::mac68k::debug_log("mac68k: ZeroScrap (no clipboard, ignoring)");
}

void PutScrap(uint32 type, void *scrap, int32 length) {
  bmx::mac68k::debug_log("mac68k: PutScrap type=0x%08x data=%p length=%d "
                         "(no clipboard, ignoring)",
                         (unsigned)type, scrap, (int)length);
}

// ---- Serial (SERD ports only; driver core lives in serial.cpp) ----

class BmxSERDPort : public SERDPort {
 public:
  explicit BmxSERDPort(const char *dev) : device_name_(dev) {}

  virtual int16 open(uint16 config) {
    bmx::mac68k::debug_log("mac68k: SERD open dev=%s config=0x%x "
                           "(no serial, openErr)",
                           device_name_ != NULL ? device_name_ : "(null)",
                           (unsigned)config);
    return openErr;
  }
  virtual int16 prime_in(uint32 pb, uint32 dce) {
    bmx::mac68k::debug_log("mac68k: SERD prime_in pb=0x%x dce=0x%x "
                           "(no serial, readErr)",
                           (unsigned)pb, (unsigned)dce);
    return readErr;
  }
  virtual int16 prime_out(uint32 pb, uint32 dce) {
    bmx::mac68k::debug_log("mac68k: SERD prime_out pb=0x%x dce=0x%x "
                           "(no serial, writErr)",
                           (unsigned)pb, (unsigned)dce);
    return writErr;
  }
  virtual int16 control(uint32 pb, uint32 dce, uint16 code) {
    bmx::mac68k::debug_log("mac68k: SERD control pb=0x%x dce=0x%x code=%u "
                           "(no serial, controlErr)",
                           (unsigned)pb, (unsigned)dce, (unsigned)code);
    return controlErr;
  }
  virtual int16 status(uint32 pb, uint32 dce, uint16 code) {
    bmx::mac68k::debug_log("mac68k: SERD status pb=0x%x dce=0x%x code=%u "
                           "(no serial, statusErr)",
                           (unsigned)pb, (unsigned)dce, (unsigned)code);
    return statusErr;
  }
  virtual int16 close(void) {
    bmx::mac68k::debug_log("mac68k: SERD close (no serial, noErr)");
    return noErr;
  }

 private:
  const char *device_name_;
};

void SerialInit(void) {
  log_once(s_serial_logged,
           "mac68k: SerialInit: creating dummy SERD ports (no hardware)",
           NULL);
  // the_serd_port[] itself is owned by portable serial.cpp; only fill it.
  the_serd_port[0] = new BmxSERDPort(PrefsFindString("seriala"));
  the_serd_port[1] = new BmxSERDPort(PrefsFindString("serialb"));
  bmx::mac68k::debug_log("mac68k: SerialInit: ports=%p,%p",
                         (void *)the_serd_port[0], (void *)the_serd_port[1]);
}

void SerialExit(void) {
  bmx::mac68k::debug_log("mac68k: SerialExit: deleting dummy SERD ports");
  delete (BmxSERDPort *)the_serd_port[0];
  delete (BmxSERDPort *)the_serd_port[1];
  the_serd_port[0] = NULL;
  the_serd_port[1] = NULL;
}

// ---- Ethernet platform layer (log-only; EtherInit/Exit in ether.cpp) ----

bool ether_init(void) {
  log_once(s_ether_logged,
           "mac68k: ether_init: no network hardware, returning false", NULL);
  return false;
}

void ether_exit(void) {
  bmx::mac68k::debug_log("mac68k: ether_exit (log-only)");
}

void ether_reset(void) {
  bmx::mac68k::debug_log("mac68k: ether_reset (log-only)");
}

int16 ether_add_multicast(uint32 pb) {
  bmx::mac68k::debug_log("mac68k: ether_add_multicast pb=0x%x (noErr)",
                         (unsigned)pb);
  return noErr;
}

int16 ether_del_multicast(uint32 pb) {
  bmx::mac68k::debug_log("mac68k: ether_del_multicast pb=0x%x (noErr)",
                         (unsigned)pb);
  return noErr;
}

int16 ether_attach_ph(uint16 type, uint32 handler) {
  bmx::mac68k::debug_log(
      "mac68k: ether_attach_ph type=%u handler=0x%x (noErr)", (unsigned)type,
      (unsigned)handler);
  return noErr;
}

int16 ether_detach_ph(uint16 type) {
  bmx::mac68k::debug_log("mac68k: ether_detach_ph type=%u (noErr)",
                         (unsigned)type);
  return noErr;
}

int16 ether_write(uint32 wds) {
  bmx::mac68k::debug_log("mac68k: ether_write wds=0x%x (no net, noErr)",
                         (unsigned)wds);
  return noErr;
}

bool ether_start_udp_thread(int socket_fd) {
  bmx::mac68k::debug_log(
      "mac68k: ether_start_udp_thread fd=%d (SUPPORTS_UDP_TUNNEL=0, false)",
      socket_fd);
  return false;
}

void ether_stop_udp_thread(void) {
  bmx::mac68k::debug_log("mac68k: ether_stop_udp_thread (log-only)");
}

void EtherInterrupt(void) {
  bmx::mac68k::debug_log("mac68k: EtherInterrupt (no net, ignoring)");
}

// ---- User strings (platform GetString; table lives in user_strings.cpp,
// platform_strings[] is never defined on BMX, so search common only) ----

const char *GetString(int num) {
  for (const user_string_def *s = common_strings; s->num >= 0; s++) {
    if (s->num == num) {
      return s->str != NULL ? s->str : "";
    }
  }
  bmx::mac68k::debug_log("mac68k: GetString: unknown id=%d", num);
  return "";
}

// ---- SCSI platform layer (log-only; SCSIReset etc. in scsi.cpp) ----

void SCSIInit(void) {
  // Match dummy/scsi_dummy.cpp: reset the portable bus phase so a rebooted
  // InitAll never inherits stale state (SCSIReset only clears statics in
  // portable scsi.cpp; safe with no targets).
  log_once(s_scsi_logged, "mac68k: SCSIInit (no SCSI bus, resetting phase)",
           NULL);
  SCSIReset();
}

void SCSIExit(void) {
  bmx::mac68k::debug_log("mac68k: SCSIExit (log-only)");
}

void scsi_set_cmd(int cmd_length, uint8 *cmd) {
  bmx::mac68k::debug_log("mac68k: scsi_set_cmd len=%d cmd=%p (no target)",
                         cmd_length, (void *)cmd);
}

bool scsi_is_target_present(int id) {
  bmx::mac68k::debug_log("mac68k: scsi_is_target_present id=%d (false)", id);
  return false;
}

bool scsi_set_target(int id, int lun) {
  bmx::mac68k::debug_log("mac68k: scsi_set_target id=%d lun=%d (false)", id,
                         lun);
  return false;
}

bool scsi_send_cmd(size_t data_length, bool reading, int sg_index,
                   uint8 **sg_ptr, uint32 *sg_len, uint16 *stat,
                   uint32 timeout) {
  bmx::mac68k::debug_log(
      "mac68k: scsi_send_cmd len=%lu reading=%d sg=%d timeout=%u (false)",
      (unsigned long)data_length, (int)reading, sg_index, (unsigned)timeout);
  (void)sg_ptr;
  (void)sg_len;
  (void)stat;
  return false;
}
