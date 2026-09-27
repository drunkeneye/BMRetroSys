//
// amiga_config.cpp
//
// Amiga core configuration for BMX bare-metal.
//
// A single upstream vamiga::VAmiga is owned on the heap (never on the
// stack: it holds megabytes of Chip/Slow RAM + textures). The emulator
// thread is NEVER launched; instead we use VAmiga's public raw component
// pointers (amiga.amiga, mem, df0, ...) which all act synchronously.
// Anything CmdQueue-based (VAmiga::powerOn/run/set/press/trigger) is
// NEVER called -- commands are only consumed by the unlaunched thread.
//

#include "amiga_config.h"
#include "amiga_debug.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <memory>
#include <span>

// Upstream vAmiga (VAMIGA_INCLUDE_DIRS on the command line).
#include "vaconfig.h"
#include "VAmiga.h"
#include "Emulator.h"
#include "Amiga.h"
#include "AmigaTypes.h"
#include "HostTypes.h"
#include "OptionTypes.h"
#include "CoreError.h"
#include "ImageTypes.h"
#include "DiagRom.h"
#include "Snapshot.h"
#include "HDFFile.h"
#include "AutoMutex.h"

namespace bmx {
namespace amiga {

namespace {

vamiga::VAmiga *s_v = NULL;
vamiga::Amiga *s_a = NULL;
bool s_ready = false;
char s_kick_path[256] = {0};
char s_df0_path[256] = {0};
bool s_hw_init = false;
HwSettings s_hw;
int s_warp = 0;
char s_hd_path[2][256] = {{0}, {0}};
bool s_hd_live[2] = {false, false};
uint32_t s_click_seed = 0x12345678u;

bool file_size(const char *path, long *len_out) {
  FILE *fp = fopen(path, "rb");
  if (fp == NULL) {
    return false;
  }
  fseek(fp, 0, SEEK_END);
  long len = ftell(fp);
  fclose(fp);
  if (len <= 0) {
    return false;
  }
  if (len_out != NULL) {
    *len_out = len;
  }
  return true;
}

bool load_kick_from_file(const char *path) {
  long len = 0;
  if (!file_size(path, &len)) {
    return false;
  }
  // Kickstarts are 256K/512K/1M; reject absurd sizes early.
  if (len != 262144L && len != 524288L && len != 1048576L) {
    debug_log("config: kick '%s' bad size %ld (want 256K/512K/1M)", path, len);
    return false;
  }
  FILE *fp = fopen(path, "rb");
  if (fp == NULL) {
    return false;
  }
  unsigned char *buf = (unsigned char *)malloc((size_t)len);
  if (buf == NULL) {
    fclose(fp);
    return false;
  }
  size_t got = fread(buf, 1, (size_t)len, fp);
  fclose(fp);
  if ((long)got != len) {
    free(buf);
    return false;
  }
  bool ok = false;
  try {
    // Direct Memory::loadRom (not the Cmd-based facade).
    s_v->mem.loadRom(buf, (vamiga::isize)len);
    ok = true;
  } catch (std::exception &e) {
    debug_log("config: loadRom threw: %s", e.what());
    ok = false;
  } catch (...) {
    debug_log("config: loadRom threw (unknown)");
    ok = false;
  }
  free(buf);
  if (ok) {
    snprintf(s_kick_path, sizeof(s_kick_path), "%s", path);
    debug_log("config: kick OK '%s' (%ld bytes)", path, len);
  }
  return ok;
}

bool load_diag_fallback(void) {
  bool ok = false;
  try {
    s_v->mem.loadRom(diagROM13, (vamiga::isize)sizeof(diagROM13));
    ok = true;
  } catch (std::exception &e) {
    debug_log("config: diag fallback threw: %s", e.what());
    ok = false;
  } catch (...) {
    ok = false;
  }
  if (ok) {
    snprintf(s_kick_path, sizeof(s_kick_path), "<diagrom 1.3>");
    debug_log("config: no kickstart on SD, DiagROM fallback active");
  }
  return ok;
}

}  // namespace

void config_hw_defaults(HwSettings *dst) {
  if (dst == NULL) {
    return;
  }
  dst->model = AMIGA_MODEL_A500;
  config_model_preset(AMIGA_MODEL_A500, dst);
  dst->fast_kb = 0;
  dst->slow_delay = 1;
  dst->slow_mirror = 1;
  // DF1 connected (as before); DF2/DF3 off until the user enables them.
  dst->drv_on[0] = 1;
  dst->drv_on[1] = 1;
  dst->drv_on[2] = 0;
  dst->drv_on[3] = 0;
  for (int d = 0; d < 4; d++) {
    dst->drv_type[d] = 0;  // DD_35
    dst->drv_mech[d] = 1;  // A1010
  }
  dst->drv_sound = 1;
  dst->drv_step_vol = 50;
  dst->aud_filter = 1;  // A500 filter pipeline
  dst->aud_sampling = 0;  // NONE (latest sample)
  dst->aud_vol_l = 50;
  dst->aud_vol_r = 50;
  for (int c = 0; c < 4; c++) {
    dst->aud_vol[c] = 100;
  }
  // vAmiga defaults: 0..400, outer channels left, inner right.
  dst->aud_pan[0] = 50;
  dst->aud_pan[1] = 350;
  dst->aud_pan[2] = 350;
  dst->aud_pan[3] = 50;
}

void config_model_preset(int model, HwSettings *dst) {
  if (dst == NULL) {
    return;
  }
  // Same values as upstream Amiga::set(ConfigScheme).
  switch (model) {
  case AMIGA_MODEL_A1000:
    dst->cpu = 0;
    dst->overclock = 0;
    dst->agnus = 0;  // OCS_OLD
    dst->denise = 0;  // OCS
    dst->chip_kb = 512;
    dst->slow_kb = 512;
    break;
  case AMIGA_MODEL_A500_ECS:
    dst->cpu = 0;
    dst->overclock = 0;
    dst->agnus = 2;  // ECS_1MB
    dst->denise = 0;  // OCS
    dst->chip_kb = 512;
    dst->slow_kb = 512;
    break;
  case AMIGA_MODEL_A500_PLUS:
    dst->cpu = 0;
    dst->overclock = 0;
    dst->agnus = 3;  // ECS_2MB
    dst->denise = 1;  // ECS
    dst->chip_kb = 512;
    dst->slow_kb = 512;
    break;
  case AMIGA_MODEL_A1200:
    dst->cpu = 2;  // 68EC020
    dst->overclock = 2;
    dst->agnus = 4;  // AGA
    dst->denise = 2;  // AGA
    dst->chip_kb = 2048;
    dst->slow_kb = 0;
    break;
  case AMIGA_MODEL_A500:
  default:
    dst->cpu = 0;
    dst->overclock = 0;
    dst->agnus = 1;  // OCS
    dst->denise = 0;  // OCS
    dst->chip_kb = 512;
    dst->slow_kb = 512;
    break;
  }
  dst->model = (model >= 0 && model < AMIGA_MODEL_COUNT)
                   ? model
                   : AMIGA_MODEL_A500;
}

HwSettings &config_hw(void) {
  if (!s_hw_init) {
    config_hw_defaults(&s_hw);
    s_hw_init = true;
  }
  return s_hw;
}

namespace {

int clamp_int(int v, int lo, int hi) {
  if (v < lo) {
    return lo;
  }
  if (v > hi) {
    return hi;
  }
  return v;
}

void apply_audio_fields(const HwSettings &h) {
  s_a->set(vamiga::Opt::AUD_FILTER_TYPE, (vamiga::i64)h.aud_filter);
  s_a->set(vamiga::Opt::AUD_SAMPLING_METHOD, (vamiga::i64)h.aud_sampling);
  s_a->set(vamiga::Opt::AUD_VOLL, (vamiga::i64)h.aud_vol_l);
  s_a->set(vamiga::Opt::AUD_VOLR, (vamiga::i64)h.aud_vol_r);
  s_a->set(vamiga::Opt::AUD_VOL0, (vamiga::i64)h.aud_vol[0]);
  s_a->set(vamiga::Opt::AUD_VOL1, (vamiga::i64)h.aud_vol[1]);
  s_a->set(vamiga::Opt::AUD_VOL2, (vamiga::i64)h.aud_vol[2]);
  s_a->set(vamiga::Opt::AUD_VOL3, (vamiga::i64)h.aud_vol[3]);
  s_a->set(vamiga::Opt::AUD_PAN0, (vamiga::i64)h.aud_pan[0]);
  s_a->set(vamiga::Opt::AUD_PAN1, (vamiga::i64)h.aud_pan[1]);
  s_a->set(vamiga::Opt::AUD_PAN2, (vamiga::i64)h.aud_pan[2]);
  s_a->set(vamiga::Opt::AUD_PAN3, (vamiga::i64)h.aud_pan[3]);
}

void apply_drive_volumes(const HwSettings &h) {
  // Drive clicks: step/insert/eject follow the master toggle + volume.
  const vamiga::i64 step = (vamiga::i64)(h.drv_sound ? h.drv_step_vol : 0);
  for (int d = 0; d < 4; d++) {
    s_a->set(vamiga::Opt::DRIVE_STEP_VOLUME, step, {(vamiga::isize)d});
    s_a->set(vamiga::Opt::DRIVE_INSERT_VOLUME, step, {(vamiga::isize)d});
    s_a->set(vamiga::Opt::DRIVE_EJECT_VOLUME, step, {(vamiga::isize)d});
    s_a->set(vamiga::Opt::DRIVE_POLL_VOLUME, (vamiga::i64)0,
             {(vamiga::isize)d});
  }
}

}  // namespace

bool config_apply_all(void) {
  if (s_a == NULL) {
    return false;
  }
  HwSettings &h = config_hw();
  // Defensive clamps: setOption() trusts values on the direct path.
  h.model = clamp_int(h.model, 0, AMIGA_MODEL_COUNT - 1);
  h.agnus = clamp_int(h.agnus, 0, 4);
  h.denise = clamp_int(h.denise, 0, 2);
  h.cpu = clamp_int(h.cpu, 0, 2);
  h.overclock = clamp_int(h.overclock, 0, 8);
  h.cia = clamp_int(h.cia, 0, 1);
  h.rtc = clamp_int(h.rtc, 0, 2);
  h.blitter = clamp_int(h.blitter, 0, 2);
  if (h.chip_kb != 256 && h.chip_kb != 512 && h.chip_kb != 1024 &&
      h.chip_kb != 2048) {
    h.chip_kb = 512;
  }
  if (h.slow_kb < 0 || h.slow_kb > 1536 || (h.slow_kb % 256) != 0) {
    h.slow_kb = 512;
  }
  if (h.fast_kb < 0 || h.fast_kb > 8192 || (h.fast_kb % 64) != 0) {
    h.fast_kb = 0;
  }
  h.drv_step_vol = clamp_int(h.drv_step_vol, 0, 100);
  h.aud_filter = clamp_int(h.aud_filter, 0, 6);
  h.aud_sampling = clamp_int(h.aud_sampling, 0, 2);
  h.aud_vol_l = clamp_int(h.aud_vol_l, 0, 100);
  h.aud_vol_r = clamp_int(h.aud_vol_r, 0, 100);
  for (int i = 0; i < 4; i++) {
    h.drv_on[i] = h.drv_on[i] ? 1 : 0;
    h.drv_type[i] = clamp_int(h.drv_type[i], 0, 2);
    h.drv_mech[i] = clamp_int(h.drv_mech[i], 0, 1);
    h.aud_vol[i] = clamp_int(h.aud_vol[i], 0, 100);
    h.aud_pan[i] = clamp_int(h.aud_pan[i], 0, 400);
  }
  h.drv_on[0] = 1;  // DF0 is internal, always connected.
  try {
    // Model scheme first (resets config to that machine), then the BMX
    // host options (fixed), then every staged field on top.
    s_a->set((vamiga::ConfigScheme)h.model);
    s_a->set(vamiga::Opt::HOST_REFRESH_RATE, (vamiga::i64)50);
    s_a->set(vamiga::Opt::HOST_SAMPLE_RATE, (vamiga::i64)44100);
    // Force RGBA texels (0xRRGGBBFF): the default came up ABGR on BMX
    // (black rendered red). The blit below assumes RGBA packing.
    s_a->set(vamiga::Opt::HOST_TEX_FORMAT,
             (vamiga::i64)vamiga::TexFormat::RGBA);
    s_a->set(vamiga::Opt::AMIGA_RUN_AHEAD, (vamiga::i64)0);
    s_a->set(vamiga::Opt::AMIGA_WARP_BOOT, (vamiga::i64)0);
    s_a->set(vamiga::Opt::AMIGA_VSYNC, (vamiga::i64)1);
    // Memory / chipset / CPU.
    s_a->set(vamiga::Opt::MEM_BUS_WIDTH,
             (vamiga::i64)(h.model == AMIGA_MODEL_A1200 ? 32 : 16));
    s_a->set(vamiga::Opt::MEM_CHIP_RAM, (vamiga::i64)h.chip_kb);
    s_a->set(vamiga::Opt::MEM_SLOW_RAM, (vamiga::i64)h.slow_kb);
    s_a->set(vamiga::Opt::MEM_FAST_RAM, (vamiga::i64)h.fast_kb);
    s_a->set(vamiga::Opt::MEM_SLOW_RAM_DELAY, (vamiga::i64)h.slow_delay);
    s_a->set(vamiga::Opt::MEM_SLOW_RAM_MIRROR, (vamiga::i64)h.slow_mirror);
    s_a->set(vamiga::Opt::AGNUS_REVISION, (vamiga::i64)h.agnus);
    s_a->set(vamiga::Opt::DENISE_REVISION, (vamiga::i64)h.denise);
    s_a->set(vamiga::Opt::CPU_REVISION, (vamiga::i64)h.cpu);
    s_a->set(vamiga::Opt::CPU_OVERCLOCKING, (vamiga::i64)h.overclock);
    s_a->set(vamiga::Opt::CIA_REVISION, (vamiga::i64)h.cia);
    s_a->set(vamiga::Opt::RTC_MODEL, (vamiga::i64)h.rtc);
    s_a->set(vamiga::Opt::BLITTER_ACCURACY, (vamiga::i64)h.blitter);
    // Floppy drives DF1..DF3 connect; DF0 internal is always on.
    for (int d = 1; d < 4; d++) {
      s_a->set(vamiga::Opt::DRIVE_CONNECT, (vamiga::i64)h.drv_on[d],
               {(vamiga::isize)d});
    }
    for (int d = 0; d < 4; d++) {
      s_a->set(vamiga::Opt::DRIVE_TYPE, (vamiga::i64)h.drv_type[d],
               {(vamiga::isize)d});
      s_a->set(vamiga::Opt::DRIVE_MECHANICS, (vamiga::i64)h.drv_mech[d],
               {(vamiga::isize)d});
    }
    // Drive clicks: step/insert/eject follow the master toggle + volume.
    apply_drive_volumes(h);
    apply_audio_fields(h);
  } catch (std::exception &e) {
    debug_log("config: apply_all threw: %s", e.what());
    return false;
  } catch (...) {
    debug_log("config: apply_all threw (unknown)");
    return false;
  }
  return true;
}

bool config_apply_audio(void) {
  if (s_a == NULL || !s_ready) {
    return false;
  }
  HwSettings &h = config_hw();
  h.aud_filter = clamp_int(h.aud_filter, 0, 6);
  h.aud_sampling = clamp_int(h.aud_sampling, 0, 2);
  h.aud_vol_l = clamp_int(h.aud_vol_l, 0, 100);
  h.aud_vol_r = clamp_int(h.aud_vol_r, 0, 100);
  for (int i = 0; i < 4; i++) {
    h.aud_vol[i] = clamp_int(h.aud_vol[i], 0, 100);
    h.aud_pan[i] = clamp_int(h.aud_pan[i], 0, 400);
  }
  try {
    apply_audio_fields(h);
  } catch (std::exception &e) {
    debug_log("config: apply_audio threw: %s", e.what());
    return false;
  } catch (...) {
    return false;
  }
  return true;
}

bool config_apply_live_volumes(void) {
  if (s_a == NULL || !s_ready) {
    return false;
  }
  HwSettings &h = config_hw();
  h.drv_step_vol = clamp_int(h.drv_step_vol, 0, 100);
  h.aud_filter = clamp_int(h.aud_filter, 0, 6);
  h.aud_sampling = clamp_int(h.aud_sampling, 0, 2);
  h.aud_vol_l = clamp_int(h.aud_vol_l, 0, 100);
  h.aud_vol_r = clamp_int(h.aud_vol_r, 0, 100);
  for (int i = 0; i < 4; i++) {
    h.aud_vol[i] = clamp_int(h.aud_vol[i], 0, 100);
    h.aud_pan[i] = clamp_int(h.aud_pan[i], 0, 400);
  }
  try {
    apply_drive_volumes(h);
    apply_audio_fields(h);
  } catch (std::exception &e) {
    debug_log("config: apply_live_volumes threw: %s", e.what());
    return false;
  } catch (...) {
    return false;
  }
  return true;
}

bool config_apply_hw_reset(void) {
  if (s_a == NULL || !s_ready) {
    return false;
  }
  if (!config_apply_all()) {
    return false;
  }
  // A scheme switch resets the controller wiring; re-attach staged
  // hardfiles (no-op when none staged; keeps working media on failure).
  config_attach_staged_hd();
  // New hardware needs a reboot to take (memory map, chipset, drives).
  // Disks stay inserted across a hard reset, like the real machine.
  config_reset(true);
  return true;
}

void *config_emu(void) {
  return s_v;
}

bool config_is_ready(void) {
  return s_ready;
}

const char *config_kick_name(void) {
  const char *slash = strrchr(s_kick_path, '/');
  if (slash != NULL && slash[1] != '\0') {
    return slash + 1;
  }
  return (s_kick_path[0] != '\0') ? s_kick_path : "";
}

bool config_load_kick(const char *sd_path) {
  if (s_v == NULL || sd_path == NULL) {
    return false;
  }
  if (!load_kick_from_file(sd_path)) {
    return false;
  }
  // A new ROM needs a reboot to take (vector fetch at reset).
  config_reset(true);
  return true;
}

bool config_insert_adf(int drive, const unsigned char *data, long len,
                       bool write_protect) {
  if (s_v == NULL || data == NULL || len <= 0 || drive < 0 || drive > 3) {
    return false;
  }
  bool ok = false;
  try {
    // Direct FloppyDriveAPI::insert(span) -> FloppyDrive::swapDisk.
    std::span<const vamiga::u8> span((const vamiga::u8 *)data, (size_t)len);
    if (drive == 0) {
      s_v->df0.insert(span, vamiga::ImageFormat::ADF, write_protect);
    } else if (drive == 1) {
      s_v->df1.insert(span, vamiga::ImageFormat::ADF, write_protect);
    } else if (drive == 2) {
      s_v->df2.insert(span, vamiga::ImageFormat::ADF, write_protect);
    } else {
      s_v->df3.insert(span, vamiga::ImageFormat::ADF, write_protect);
    }
    ok = true;
  } catch (std::exception &e) {
    debug_log("config: df%d insert threw: %s", drive, e.what());
    ok = false;
  } catch (...) {
    ok = false;
  }
  return ok;
}

void config_eject(int drive) {
  if (s_v == NULL) {
    return;
  }
  try {
    if (drive == 0) {
      s_v->df0.ejectDisk();
    } else if (drive == 1) {
      s_v->df1.ejectDisk();
    } else if (drive == 2) {
      s_v->df2.ejectDisk();
    } else if (drive == 3) {
      s_v->df3.ejectDisk();
    }
  } catch (...) {
  }
  if (drive == 0) {
    s_df0_path[0] = '\0';
  }
}


void config_set_warp(int on) {
  s_warp = on ? 1 : 0;
}

int config_warp(void) {
  return s_warp;
}

bool config_save_snapshot(const char *path) {
  if (!s_ready || s_a == NULL || path == NULL || *path == '\0') {
    return false;
  }
  try {
    // In-memory snapshot (RLE2 needs no external libs), then plain C
    // file I/O like the rest of BMX (no fstream on bare metal).
    vamiga::Snapshot snap(*s_a, vamiga::Compressor::RLE2);
    if (snap.data.ptr == NULL || snap.data.size <= 0) {
      return false;
    }
    FILE *fp = fopen(path, "wb");
    if (fp == NULL) {
      debug_log("snapshot: cannot write '%s'", path);
      return false;
    }
    size_t want = (size_t)snap.data.size;
    bool ok = fwrite(snap.data.ptr, 1, want, fp) == want;
    fclose(fp);
    debug_log("snapshot: %s '%s' (%lu bytes)", ok ? "saved" : "FAILED",
              path, (unsigned long)want);
    return ok;
  } catch (std::exception &e) {
    debug_log("snapshot: save threw: %s", e.what());
    return false;
  } catch (...) {
    return false;
  }
}

bool config_load_snapshot(const char *path) {
  if (!s_ready || s_a == NULL || path == NULL || *path == '\0') {
    return false;
  }
  FILE *fp = fopen(path, "rb");
  if (fp == NULL) {
    return false;
  }
  fseek(fp, 0, SEEK_END);
  long len = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  // Snapshots hold at most ~10MB fast + 2MB chip + textures; 64MB cap.
  if (len <= 0 || len > 64L * 1024 * 1024) {
    fclose(fp);
    return false;
  }
  unsigned char *buf = (unsigned char *)malloc((size_t)len);
  if (buf == NULL) {
    fclose(fp);
    return false;
  }
  bool ok = fread(buf, 1, (size_t)len, fp) == (size_t)len;
  fclose(fp);
  // Snapshot magic, before touching the core.
  if (!ok || len < 6 || memcmp(buf, "VASNAP", 6) != 0) {
    free(buf);
    return false;
  }
  try {
    vamiga::Snapshot snap(buf, (vamiga::isize)len);
    s_a->loadSnapshot(snap);
  } catch (std::exception &e) {
    debug_log("snapshot: load threw: %s", e.what());
    free(buf);
    return false;
  } catch (...) {
    free(buf);
    return false;
  }
  free(buf);
  debug_log("snapshot: loaded '%s' (%ld bytes)", path, len);
  return true;
}

bool config_attach_hdf(int drive, const unsigned char *data, long len) {
  if (!s_ready || s_a == NULL || data == NULL || (drive != 0 && drive != 1)) {
    return false;
  }
  if (len < 512 || (len % 512) != 0) {
    return false;
  }
  // Matches the upstream HDC memory limit (256MB per drive).
  if (len > 256L * 1024 * 1024) {
    debug_log("hdf: image too large (%ld bytes, max 256MB)", len);
    return false;
  }
  try {
    // Buffer-backed HDF (no fstream on bare metal), then hand the image
    // to the drive. Old image is only replaced on success.
    auto hdf = std::make_unique<vamiga::HDFFile>();
    hdf->init(data, (vamiga::isize)len);
    if (drive == 0) {
      s_a->hd0.init(std::move(hdf));
    } else {
      s_a->hd1.init(std::move(hdf));
    }
    s_a->set(vamiga::Opt::HDC_CONNECT, (vamiga::i64)1,
             {(vamiga::isize)drive});
    s_hd_live[drive] = true;
    debug_log("hd%d attached (%ld bytes)", drive, len);
    return true;
  } catch (std::exception &e) {
    debug_log("hdf: attach threw: %s", e.what());
    return false;
  } catch (...) {
    return false;
  }
}

void config_detach_hd(int drive) {
  if (!s_ready || s_a == NULL || (drive != 0 && drive != 1)) {
    return;
  }
  try {
    // No public media detach on the raw drive; park the controller so the
    // Amiga sees the drive go away. The image object stays allocated but
    // unreachable until the next attach replaces it.
    s_a->set(vamiga::Opt::HDC_CONNECT, (vamiga::i64)0,
             {(vamiga::isize)drive});
  } catch (...) {
  }
  s_hd_live[drive] = false;
  s_hd_path[drive][0] = '\0';
  debug_log("hd%d detached", drive);
}

int config_hd_attached(int drive) {
  if (!s_ready || s_a == NULL || (drive != 0 && drive != 1)) {
    return 0;
  }
  if (!s_hd_live[drive]) {
    return 0;
  }
  try {
    return (drive == 0 ? s_a->hd0.hasDisk() : s_a->hd1.hasDisk()) ? 1 : 0;
  } catch (...) {
    return 0;
  }
}

void config_set_hd_path(int drive, const char *path) {
  if (drive < 0 || drive > 1) {
    return;
  }
  if (path == NULL) {
    s_hd_path[drive][0] = '\0';
    return;
  }
  snprintf(s_hd_path[drive], sizeof(s_hd_path[drive]), "%s", path);
}

const char *config_hd_path(int drive) {
  if (drive < 0 || drive > 1) {
    return "";
  }
  return s_hd_path[drive];
}

void config_attach_staged_hd(void) {
  if (!s_ready || s_a == NULL) {
    return;
  }
  for (int d = 0; d < 2; d++) {
    if (s_hd_path[d][0] == '\0') {
      continue;
    }
    FILE *fp = fopen(s_hd_path[d], "rb");
    if (fp == NULL) {
      debug_log("hd: staged '%s' unreadable", s_hd_path[d]);
      continue;
    }
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    if (len <= 0 || len > 256L * 1024 * 1024) {
      fclose(fp);
      debug_log("hd: staged '%s' bad size %ld", s_hd_path[d], len);
      continue;
    }
    unsigned char *buf = (unsigned char *)malloc((size_t)len);
    bool ok = buf != NULL && fread(buf, 1, (size_t)len, fp) == (size_t)len;
    fclose(fp);
    if (ok) {
      ok = config_attach_hdf(d, buf, len);
    }
    free(buf);
    debug_log("hd: staged %s '%s'", ok ? "attached" : "FAILED",
              s_hd_path[d]);
  }
}

void config_poll_drive_sounds(void) {
  if (!s_ready || s_a == NULL) {
    return;
  }
  HwSettings &h = config_hw();
  // Phase 1: drain the queue (bounded) so it never backlogs across warp.
  // Coalesce hard: at most 2 clicks per tick. A seeking drive emits dozens
  // of step messages per frame; synthesizing all of them piles seconds of
  // noise into the 4096-sample stream and sounds scrambled.
  struct Click {
    int kind;  // 0=step, 1=poll, 2=hard-disk step
    int vol;  // 0..100
  };
  Click clicks[2];
  int nclicks = 0;
  try {
    vamiga::Message msg;
    int n = 0;
    while (n++ < 64 && s_a->msgQueue.get(msg)) {
      if (!h.drv_sound || nclicks >= 2) {
        continue;
      }
      int kind = -1;
      if (msg.type == vamiga::Msg::DRIVE_STEP) {
        kind = 0;
      } else if (msg.type == vamiga::Msg::DRIVE_POLL) {
        kind = 1;
      } else if (msg.type == vamiga::Msg::HDR_STEP) {
        kind = 2;
      } else {
        continue;
      }
      int vol = (int)msg.drive.volume;  // 0..100, like the drive volumes
      if (vol < 0) {
        vol = 0;
      }
      if (vol > 100) {
        vol = 100;
      }
      clicks[nclicks].kind = kind;
      clicks[nclicks].vol = vol;
      nclicks++;
    }
  } catch (...) {
    return;
  }
  if (nclicks == 0) {
    return;
  }
  // Phase 2: mix mechanical clicks into the Paula stream. A real step is
  // a sharp attack (solenoid snap, ~2ms noise) over a low thump (head
  // resonance ~150Hz, ~25ms). Both parts share one quadratic decay.
  try {
    utl::AutoMutex guard(s_a->audioPort.stream.mutex);
    auto &stream = s_a->audioPort.stream;
    for (int c = 0; c < nclicks; c++) {
      float amp = 0.65f * ((float)clicks[c].vol / 100.0f) *
                  ((float)h.drv_step_vol / 100.0f);
      if (clicks[c].kind == 1) {
        amp *= 0.5f;
      }
      if (amp <= 0.001f) {
        continue;
      }
      int len = clicks[c].kind == 2 ? 1100 : 900;
      float prev = 0.0f;
      float phase = 0.0f;
      // 150Hz thump at 44.1kHz.
      const float step = 6.2831853f * 150.0f / 44100.0f;
      for (int i = 0; i < len; i++) {
        if (stream.free() <= 0) {
          break;
        }
        s_click_seed = s_click_seed * 1664525u + 1013904223u;
        float nz = (float)((s_click_seed >> 16) & 0x7fff) / 16383.5f - 1.0f;
        float smooth = 0.5f * (nz + prev);
        prev = nz;
        phase += step;
        float s = (smooth * 0.7f + sinf(phase) * 0.6f) * amp;
        float env = 1.0f - (float)i / (float)len;
        s *= env * env;
        stream.write(vamiga::SamplePair{s, s});
      }
    }
  } catch (...) {
  }
}

void config_reset(bool hard) {
  if (s_a == NULL) {
    return;
  }
  try {
    if (hard) {
      s_a->hardReset();
    } else {
      s_a->softReset();
    }
  } catch (std::exception &e) {
    debug_log("config: reset threw: %s", e.what());
  } catch (...) {
  }
}

bool config_step_frame(void) {
  if (s_a == NULL) {
    return true;
  }
  try {
    s_a->computeFrame();
  } catch (std::exception &e) {
    debug_log("config: computeFrame threw: %s", e.what());
    return false;
  } catch (...) {
    debug_log("config: computeFrame threw (unknown)");
    return false;
  }
  return true;
}

int config_pull_audio(float *l, float *r, int n) {
  if (s_v == NULL || l == NULL || r == NULL || n <= 0) {
    return 0;
  }
  try {
    vamiga::isize got = s_v->audioPort.copyStereo(l, r, (vamiga::isize)n);
    return (int)got;
  } catch (...) {
    return 0;
  }
}

void config_audio_metrics(AudioMetrics *dst) {
  if (dst == NULL) {
    return;
  }
  dst->produced = 0;
  dst->consumed = 0;
  dst->idle = 0;
  dst->fill = -1;
  dst->capacity = -1;
  if (s_v == NULL) {
    return;
  }
  try {
    // Raw component port (not the VAmiga facade): exposes metrics + stream.
    vamiga::AudioPortMetrics m = s_a->audioPort.cacheMetrics();
    dst->produced = (long)m.producedSamples;
    dst->consumed = (long)m.consumedSamples;
    dst->idle = (long)m.idleSamples;
    dst->fill = (int)s_a->audioPort.stream.count();
    dst->capacity = (int)s_a->audioPort.stream.cap();
  } catch (...) {
  }
}

bool config_init(const Config &config) {
  debug_log("config_init: enter tv_mode=%s",
            config.tv_mode != NULL ? config.tv_mode : "(null)");

  s_v = new vamiga::VAmiga();
  if (s_v == NULL || s_v->amiga.amiga == NULL) {
    debug_log("config_init: FATAL out of memory / no Amiga");
    return false;
  }
  s_a = s_v->amiga.amiga;
  debug_log("config_init: VAmiga @%p Amiga @%p (sizeof=%u)",
            (const void *)s_v, (const void *)s_a,
            (unsigned)sizeof(vamiga::Amiga));

  // Component initialization (what Thread::runLoop() runs first on
  // desktop via Emulator::initialize: main.initialize(), default config,
  // run-ahead clone). Skipping it leaves components uninitialized and
  // powerOn() crashes. Must precede set()/powerOn().
  try {
    s_v->emu->initialize();
    debug_log("config_init: emulator initialize OK");
  } catch (std::exception &e) {
    debug_log("config_init: initialize threw: %s", e.what());
    return false;
  } catch (...) {
    debug_log("config_init: initialize threw (unknown)");
    return false;
  }

  // Staged model scheme + hardware settings (menu-persisted). Replaces
  // the old fixed A500 OCS 1MB setup. NTSC requested -> still PAL.
  if (!config_apply_all()) {
    debug_log("config_init: hardware apply FAILED");
    return false;
  }
  debug_log("config_init: hardware apply OK (model=%d)", config_hw().model);

  // Kickstart pool on SD (user-supplied, never in git). First hit wins.
  static const char *candidates[] = {
      "/roms/kick/kick34005.A500",
      "/roms/kick/kick37175.A500",
      "/roms/kick/kick40063.A600",
      "/roms/kick/kick40068.A1200",
      "/roms/kick/aros-amiga-m68k-rom.bin",
      "SD:/roms/kick/kick34005.A500",
      "SD:/roms/kick/kick37175.A500",
      "SD:/roms/kick/kick40063.A600",
      "SD:/roms/kick/kick40068.A1200",
  };
  bool have_rom = false;
  for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
    if (load_kick_from_file(candidates[i])) {
      have_rom = true;
      break;
    }
  }
  if (!have_rom && !load_diag_fallback()) {
    debug_log("config_init: FATAL no ROM at all");
    return false;
  }

  // Power on (throws without ROM; ours is loaded by now).
  try {
    s_a->powerOn();
  } catch (std::exception &e) {
    debug_log("config_init: powerOn threw: %s", e.what());
    return false;
  } catch (...) {
    debug_log("config_init: powerOn threw (unknown)");
    return false;
  }
  s_ready = true;

  // Staged hardfiles first (a bootable DH0 beats DF0).
  config_attach_staged_hd();

  // Bootable DF0 if staged (Workbench autoboot); absence is fine.
  {
    long len = 0;
    static const char *boots[] = {
        "/systems/AMIGA/disks/boot.adf",
        "SD:/systems/AMIGA/disks/boot.adf",
    };
    for (size_t i = 0; i < sizeof(boots) / sizeof(boots[0]); i++) {
      if (!file_size(boots[i], &len)) {
        continue;
      }
      FILE *fp = fopen(boots[i], "rb");
      if (fp == NULL) {
        continue;
      }
      unsigned char *buf = (unsigned char *)malloc((size_t)len);
      if (buf != NULL && fread(buf, 1, (size_t)len, fp) == (size_t)len &&
          config_insert_adf(0, buf, len, false)) {
        snprintf(s_df0_path, sizeof(s_df0_path), "%s", boots[i]);
        debug_log("config: df0 autoboot '%s' (%ld bytes)", boots[i], len);
        free(buf);
        fclose(fp);
        break;
      }
      free(buf);
      fclose(fp);
    }
  }

  // One warm-up frame proves computeFrame() survives on bare metal before
  // the frame loop takes over (exceptions/FPU/mutex-shim smoke test).
  try {
    s_a->computeFrame();
    debug_log("config_init: warm-up frame OK");
  } catch (std::exception &e) {
    debug_log("config_init: warm-up frame threw: %s", e.what());
    return false;
  } catch (...) {
    debug_log("config_init: warm-up frame threw (unknown)");
    return false;
  }

  debug_log("config_init: OK kick='%s' df0='%s'", s_kick_path,
            s_df0_path[0] != '\0' ? s_df0_path : "(empty)");
  return true;
}

}  // namespace amiga
}  // namespace bmx
