//
// bbc_machine.cpp
//
// See bbc_machine.h. CLK BBCMicro lifecycle: the ROM fetcher serves the
// /roms/BBC pool by ROM::Name (validate() decides sufficiency); model
// switches and hard resets recreate the machine; soft reset pulses Break.
//

#include "bbc_machine.h"
#include "bbc_config.h"
#include "bbc_debug.h"
#include "bbc_video.h"
#include "bbc_sound.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <memory>
#include <vector>

// CLK core (vendored subset under third_party/bbc).
#include "Analyser/Static/Acorn/Target.hpp"
#include "Machines/Acorn/BBCMicro/BBCMicro.hpp"
#include "Machines/Acorn/BBCMicro/Keyboard.hpp"
#include "Machines/AudioProducer.hpp"
#include "Machines/JoystickMachine.hpp"
#include "Machines/KeyboardMachine.hpp"
#include "Machines/MediaTarget.hpp"
#include "Machines/ScanProducer.hpp"
#include "Machines/TimedMachine.hpp"
#include "Machines/Utility/ROMCatalogue.hpp"

namespace bmx {
namespace bbc {

// Forward declarations (defined in bbc_video.cpp / bbc_sound.cpp /
// bbc_drive.cpp).
Outputs::Display::ScanTarget *bbc_scan_target(void);
void bbc_sound_set_speaker(Outputs::Speaker::Speaker *speaker);
void drive_repush_all(void);

namespace {

const BbcModel kModels[] = {
    {"BBC Model B (DFS)", "dfs226.rom", true, false},
    {"BBC Model B (ADFS)", "adfs130.rom", false, true},
    {"BBC Model B (cassette)", "", false, false},
};

int s_current = 0;
bool s_core_up = false;

std::unique_ptr<BBCMicro::Machine> s_machine;

bool read_file(const char *path, std::vector<uint8_t> &out) {
  FILE *fp = fopen(path, "rb");
  if (fp == nullptr) {
    return false;
  }
  fseek(fp, 0, SEEK_END);
  long size = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  if (size <= 0 || size > 32768) {
    fclose(fp);
    return false;
  }
  out.resize((size_t)size);
  size_t got = fread(out.data(), 1, (size_t)size, fp);
  fclose(fp);
  return got == (size_t)size;
}

bool load_pool_rom(const char *name, std::vector<uint8_t> &out) {
  char path[256];
  snprintf(path, sizeof(path), "%s/%s", config_rom_dir(), name);
  if (!read_file(path, out)) {
    debug_log("machine: pool ROM missing/unreadable: %s", path);
    out.clear();
    return false;
  }
  debug_log("machine: pool ROM %s (%u bytes)", name,
            (unsigned)out.size());
  return true;
}

// Serves every known BBC ROM from the pool; Request::validate decides
// whether the set suffices (missing files -> empty vectors -> throw).
ROM::Map fetch_roms(const ROM::Request &) {
  ROM::Map map;
  std::vector<uint8_t> os, basic, dfs, adfs;
  load_pool_rom("os12.rom", os);
  load_pool_rom("basic2.rom", basic);
  load_pool_rom("dfs226.rom", dfs);
  load_pool_rom("adfs130.rom", adfs);
  if (!os.empty()) {
    map[ROM::Name::BBCMicroMOS12] = os;
  }
  if (!basic.empty()) {
    map[ROM::Name::AcornBASICII] = basic;
  }
  if (!dfs.empty()) {
    map[ROM::Name::BBCMicro1770DFS226] = dfs;
  }
  if (!adfs.empty()) {
    map[ROM::Name::BBCMicroADFS130] = adfs;
  }
  return map;
}

MachineTypes::TimedMachine *timed() {
  return dynamic_cast<MachineTypes::TimedMachine *>(s_machine.get());
}

bool create_machine(const BbcModel *m) {
  Analyser::Static::Acorn::BBCMicroTarget target;
  target.has_1770dfs = m->has_1770dfs;
  target.has_adfs = m->has_adfs;
  target.has_sideways_ram = true;
  target.tube_processor =
      Analyser::Static::Acorn::BBCMicroTarget::TubeProcessor::None;
  target.should_shift_restart = false;
  // No media at creation; discs attach afterwards via insert_media.

  debug_log("machine: create '%s' dfs=%d adfs=%d", m->name,
            m->has_1770dfs ? 1 : 0, m->has_adfs ? 1 : 0);
  try {
    s_machine = BBCMicro::Machine::create(target, fetch_roms);
  } catch (const std::exception &e) {
    debug_log("machine: create FAILED (%s)", e.what());
    s_machine.reset();
    return false;
  } catch (...) {
    debug_log("machine: create FAILED (unknown exception, ROMs?)");
    s_machine.reset();
    return false;
  }
  if (!s_machine) {
    debug_log("machine: create returned null");
    return false;
  }
  // Wire host endpoints.
  dynamic_cast<MachineTypes::ScanProducer *>(s_machine.get())
      ->set_scan_target(bbc_scan_target());
  Outputs::Speaker::Speaker *speaker =
      dynamic_cast<MachineTypes::AudioProducer *>(s_machine.get())
          ->get_speaker();
  bbc_sound_set_speaker(speaker);
  // Re-attach discs: a fresh 1770 starts empty (model switches and
  // resets recreate the machine).
  drive_repush_all();
  debug_log("machine: created ok speaker=%p", (void *)speaker);
  return true;
}

}  // namespace

int machine_model_count(void) {
  return (int)(sizeof(kModels) / sizeof(kModels[0]));
}

const BbcModel *machine_model(int index) {
  if (index < 0 || index >= machine_model_count()) {
    return &kModels[0];
  }
  return &kModels[index];
}

int machine_current_index(void) { return s_current; }

bool machine_init_model(int index) {
  if (index < 0 || index >= machine_model_count()) {
    return false;
  }
  const BbcModel *m = &kModels[index];
  // Pre-validate the pool so the log names the missing file.
  std::vector<uint8_t> probe;
  if (!load_pool_rom("os12.rom", probe) || probe.size() != 16384) {
    debug_log("machine: model '%s' needs os12.rom (16384 bytes) in %s",
              m->name, config_rom_dir());
    return false;
  }
  if (!load_pool_rom("basic2.rom", probe) || probe.size() != 16384) {
    debug_log("machine: model '%s' needs basic2.rom (16384 bytes) in %s",
              m->name, config_rom_dir());
    return false;
  }
  if (m->fs_rom[0] != '\0') {
    if (!load_pool_rom(m->fs_rom, probe) || probe.size() != 16384) {
      debug_log("machine: model '%s' needs %s (16384 bytes) in %s",
                m->name, m->fs_rom, config_rom_dir());
      return false;
    }
  }
  if (s_core_up) {
    // Live model switch: rebuild the machine (BBC has no soft ROM swap;
    // sideways slots are populated at construction).
    if (!create_machine(m)) {
      return false;
    }
    debug_log("machine: switched to '%s'", m->name);
  } else {
    debug_log("machine: pre-init model '%s'", m->name);
  }
  s_current = index;
  return true;
}

// First creation once the core is up (video/sound endpoints exist).
bool machine_create_initial(void) {
  if (s_machine) {
    return true;
  }
  return create_machine(machine_model(s_current));
}

void machine_note_core_up(void) { s_core_up = true; }
bool machine_is_up(void) { return s_core_up && s_machine != nullptr; }

void machine_reset(bool soft) {
  (void)soft;
  if (!s_machine) {
    return;
  }
  // Full recreation (same path as a model switch, proven in the field):
  // the Break-key pulse freezes the board once model switches have
  // fragmented the heap, so both menu resets go through re-init +
  // reboot. Attached discs are re-pushed by create_machine.
  debug_log("machine_reset(%s): recreate", soft ? "soft" : "hard");
  const BbcModel *m = machine_model(s_current);
  s_machine.reset();
  if (!create_machine(m)) {
    debug_log("machine: reset FAILED (recreate)");
    return;
  }
  debug_log("machine: reset done");
}

void machine_run_frame(void) {
  if (!s_machine) {
    return;
  }
  timed()->run_for(Time::Seconds(0.02));
  timed()->flush_output(MachineTypes::TimedMachine::Output::All);
}

void machine_run_slice(double seconds) {
  if (!s_machine) {
    return;
  }
  if (seconds < 0.001) {
    seconds = 0.001;
  }
  if (seconds > 0.02) {
    seconds = 0.02;
  }
  timed()->run_for(Time::Seconds(seconds));
  timed()->flush_output(MachineTypes::TimedMachine::Output::All);
}

void machine_set_speed(double mult) {
  if (!s_machine) {
    return;
  }
  if (mult < 0.25) {
    mult = 0.25;
  }
  if (mult > 16.0) {
    mult = 16.0;
  }
  timed()->set_speed_multiplier(mult);
  debug_log("machine: speed x%.2f", mult);
}

void machine_shift_break(void) {
  if (!s_machine) {
    return;
  }
  // SHIFT+BREAK: the MOS boots the disc when Shift is held at reset.
  // machine_reset recreates (keyboard state is fresh), so Shift is
  // asserted after the reboot; the MOS samples it during POST.
  machine_reset(false);
  machine_set_shift_held(true);
  debug_log("machine: shift+break (disc boot)");
}

void machine_set_shift_held(bool held) {
  if (!s_machine) {
    return;
  }
  dynamic_cast<MachineTypes::MappedKeyboardMachine *>(s_machine.get())
      ->keyboard()
      .set_key_pressed(Inputs::Keyboard::Key::LeftShift, 0, held, false);
  debug_log("machine: shift %s", held ? "held" : "released");
}

const char *machine_rom_dir(void) { return config_rom_dir(); }

// Internal accessors for input/drive glue (same TU, external linkage for
// the C++ glue only).
namespace clk {
BBCMicro::Machine *machine_ptr(void) { return s_machine.get(); }
}  // namespace clk

}  // namespace bbc
}  // namespace bmx
