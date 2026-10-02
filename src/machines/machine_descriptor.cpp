#include "machines/machine_descriptor.h"

extern "C" {
#include "third_party/common/circle.h"
}

namespace bmc64 {

namespace {

constexpr MachineDescriptor kC64 = {
    MachineId::C64,
    "C64",
    "/roms/C64",
    "/systems/C64/disks",
    "/roms/C64/bootstat.txt",
    ".c64",
    "/roms/C64/rpi_pos_de.vkm",
    {1025700, 1026611, 982800, 984404, 982800},
    false,
    false,
    "+VICIIvcache",
    nullptr,
};

constexpr MachineDescriptor kScpu64 = {
    MachineId::SCPU64,
    "SuperCPU 64",
    "/roms/SCPU64",
    "/systems/SCPU64/disks",
    "/roms/SCPU64/bootstat.txt",
    ".scpu64",
    "/roms/SCPU64/rpi_pos_de.vkm",
    {1025700, 1026611, 982800, 984404, 982800},
    false,
    false,
    "+VICIIvcache",
    nullptr,
};

constexpr MachineDescriptor kC128 = {
    MachineId::C128,
    "C128",
    "/roms/C128",
    "/systems/C128/disks",
    "/roms/C128/bootstat.txt",
    ".c128",
    "/roms/C128/rpi_pos_de.vkm",
    {1025700, 1026611, 982800, 984404, 982800},
    true,
    true,
    "+VICIIvcache",
    "+VDCvcache",
};

constexpr MachineDescriptor kVIC20 = {
    MachineId::VIC20,
    "VIC-20",
    "/roms/VIC20",
    "/systems/VIC20/disks",
    "/roms/VIC20/bootstat.txt",
    ".vic20",
    "/roms/VIC20/rpi_pos_de.vkm",
    {1017900, 1018804, 1107600, 1109372, 1017900},
    false,
    true,
    "+VICvcache",
    nullptr,
};

constexpr MachineDescriptor kPlus4 = {
    MachineId::PLUS4,
    "Plus/4",
    "/roms/PLUS4",
    "/systems/PLUS4/disks",
    "/roms/PLUS4/bootstat.txt",
    ".plus4",
    "/roms/PLUS4/rpi_pos_de.vkm",
    {1792080, 1793672, 1778400, 1781245, 1778400},
    false,
    true,
    "+TEDvcache",
    nullptr,
};

constexpr MachineDescriptor kPet = {
    MachineId::PET,
    "PET",
    "/roms/PET",
    "/systems/PET/disks",
    "/roms/PET/bootstat.txt",
    ".pet",
    "/roms/PET/rpi_pos_de.vkm",
    {1013760, 1014661, 1001600, 1003202, 1000000},
    false,
    true,
    "+CRTCvcache",
    nullptr,
};

constexpr MachineDescriptor kAtari800 = {
    MachineId::ATARI800,
    "Atari 800",
    "/roms/ATARI800",
    "/systems/ATARI800/disks",
    "/roms/ATARI800/bootstat.txt",
    ".atari800",
    nullptr,
    {1789772, 1789790, 1773447, 1773420, 1789772},
    false,
    false,
    nullptr,
    nullptr,
};

constexpr MachineDescriptor kDos = {
    MachineId::DOS,
    "MS-DOS",
    "/roms/DOS",
    "/systems/DOS/disks",
    "/roms/DOS/bootstat.txt",
    ".dos",
    nullptr,
    // placeholder 10MHz 286 until Phase 5 profiling
    {10000000, 10000000, 10000000, 10000000, 10000000},
    false,
    false,
    nullptr,
    nullptr,
};

constexpr MachineDescriptor kAtariSt = {
    MachineId::ATARIST,
    "Atari ST",
    "/roms/ATARIST",
    "/systems/ATARIST/disks",
    "/roms/ATARIST/bootstat.txt",
    ".atarist",
    nullptr,
    // ST PAL 50Hz clock base until Phase 5 profiling (8MHz 68000).
    {8000000, 8000000, 8000000, 8000000, 8000000},
    false,
    false,
    nullptr,
    nullptr,
};

constexpr MachineDescriptor kAmiga = {
    MachineId::AMIGA,
    "Amiga",
    "/roms/AMIGA",
    "/systems/AMIGA/disks",
    "/roms/AMIGA/bootstat.txt",
    ".amiga",
    nullptr,
    // A500 PAL 68000 clock (7.09MHz).
    {7093790, 7093790, 7093790, 7093790, 7093790},
    false,
    false,
    nullptr,
    nullptr,
};

constexpr MachineDescriptor kZxSpectrum = {
    MachineId::ZXSPECTRUM,
    "ZX Spectrum",
    "/roms/ZXSPECTRUM",
    "/systems/ZXSPECTRUM/disks",
    "/roms/ZXSPECTRUM/bootstat.txt",
    ".zxspectrum",
    nullptr,
    // Z80 3.5MHz (PAL 50Hz frame clock).
    {3500000, 3500000, 3500000, 3500000, 3500000},
    false,
    false,
    nullptr,
    nullptr,
};

constexpr MachineDescriptor kCpc = {
    MachineId::CPC,
    "Amstrad CPC",
    "/roms/CPC",
    "/systems/CPC/disks",
    "/roms/CPC/bootstat.txt",
    ".cpc",
    nullptr,
    // Z80 4MHz nominal (PAL 50Hz frame clock, 19.968ms frames).
    {4000000, 4000000, 4000000, 4000000, 4000000},
    false,
    false,
    nullptr,
    nullptr,
};

constexpr MachineDescriptor kMsx = {
    MachineId::MSX,
    "MSX",
    "/roms/MSX",
    "/systems/MSX",
    "/roms/MSX/bootstat.txt",
    ".msx",
    nullptr,
    // Z80 3.579545MHz nominal (PAL 50Hz frame clock).
    {3579545, 3579545, 3579545, 3579545, 3579545},
    false,
    false,
    nullptr,
    nullptr,
};

constexpr MachineDescriptor kA2600 = {
    MachineId::A2600,
    "Atari 2600",
    "/systems/A2600",
    "/systems/A2600",
    "/systems/A2600/bootstat.txt",
    ".a2600",
    nullptr,
    // 6507 1.19MHz nominal (no BIOS ROM pool on a 2600).
    {1190476, 1190476, 1190476, 1190476, 1190476},
    false,
    false,
    nullptr,
    nullptr,
};

}  // namespace

const MachineDescriptor &CurrentMachine() {
#if defined(RASPI_C64) || defined(RASPI_C64SC)
  return kC64;
#elif defined(RASPI_SCPU64)
  return kScpu64;
#elif defined(RASPI_C128)
  return kC128;
#elif defined(RASPI_VIC20)
  return kVIC20;
#elif defined(RASPI_PLUS4)
  return kPlus4;
#elif defined(RASPI_PET)
  return kPet;
#elif defined(RASPI_ATARI800)
  return kAtari800;
#elif defined(RASPI_DOS)
  return kDos;
#elif defined(RASPI_ATARIST)
  return kAtariSt;
#elif defined(RASPI_AMIGA)
  return kAmiga;
#elif defined(RASPI_ZXSPECTRUM)
  return kZxSpectrum;
#elif defined(RASPI_CPC)
  return kCpc;
#elif defined(RASPI_MSX)
  return kMsx;
#elif defined(RASPI_A2600)
  return kA2600;
#else
#error Unknown RASPI_ variant
#endif
}

int CurrentMachineCyclesPerSecond(int timing, unsigned long custom_cycles) {
  const MachineCycleProfile &cycles = CurrentMachine().cycles;

  if (timing == MACHINE_TIMING_NTSC_HDMI || timing == MACHINE_TIMING_NTSC_DPI) {
    return cycles.ntsc_hdmi;
  }
  if (timing == MACHINE_TIMING_NTSC_COMPOSITE) {
    return cycles.ntsc_composite;
  }
  if (timing == MACHINE_TIMING_PAL_HDMI || timing == MACHINE_TIMING_PAL_DPI) {
    return cycles.pal_hdmi;
  }
  if (timing == MACHINE_TIMING_PAL_COMPOSITE) {
    return cycles.pal_composite;
  }
  if (timing == MACHINE_TIMING_NTSC_CUSTOM_HDMI ||
      timing == MACHINE_TIMING_NTSC_CUSTOM_DPI ||
      timing == MACHINE_TIMING_PAL_CUSTOM_HDMI ||
      timing == MACHINE_TIMING_PAL_CUSTOM_DPI) {
    return custom_cycles;
  }

  return cycles.fallback;
}

}  // namespace bmc64
