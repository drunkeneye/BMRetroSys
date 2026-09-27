//
// dos_sound_oplboards.cpp
//
// No-op bodies for the hardware-serial OPL backends referenced by vendored
// hardware/adlib.cpp:
//
//   - OPL2AudioBoard  (opl2board/opl2board.h, BMX-local stub header)
//   - Opl3DuoBoard    (opl3duoboard/opl3duoboard.h, BMX-local stub header)
//   - RetroWave_*     (RetroWaveLib/RetroWave_DOSBoX.hpp, BMX-local stub)
//
// The real implementations drive physical OPL hardware over host serial/SPI
// ports, which do not exist on bare-metal BMX. These backends only
// instantiate when the user explicitly selects oplemu=opl2board /
// opl3duoboard / retrowave_opl3; the default oplemu resolves to DBOPL and
// never touches this file. If selected anyway: one log line + silence,
// matching "hardware absent" semantics (never a crash, never a hang).
//

#include "dos_debug.h"

// Must match the stub headers (same include roots as adlib.cpp uses).
#include "opl2board/opl2board.h"
#include "opl3duoboard/opl3duoboard.h"
#include "RetroWaveLib/RetroWave_DOSBoX.hpp"

namespace {

bool s_oplboard_warned = false;

void warn_once(const char *which) {
  if (!s_oplboard_warned) {
    s_oplboard_warned = true;
    bmx::dos::debug_log(
        "OPL board stub: '%s' needs host serial/SPI hardware (absent on "
        "BMX); rendering silence. Use oplemu=default (DBOPL).",
        which);
  }
}

}  // namespace

OPL2AudioBoard::OPL2AudioBoard() {}
void OPL2AudioBoard::connect(const char *port) {
  (void)port;
  warn_once("opl2board");
}
void OPL2AudioBoard::disconnect() {}
void OPL2AudioBoard::reset() {}
void OPL2AudioBoard::write(uint8_t reg, uint8_t val) {
  (void)reg;
  (void)val;
}

Opl3DuoBoard::Opl3DuoBoard() {}
void Opl3DuoBoard::connect(const char *port) {
  (void)port;
  warn_once("opl3duoboard");
}
void Opl3DuoBoard::disconnect() {}
void Opl3DuoBoard::reset() {}
void Opl3DuoBoard::write(uint32_t reg, uint8_t val) {
  (void)reg;
  (void)val;
}

RetroWaveContext retrowave_global_context = {};
int retrowave_global_context_inited = 0;

void retrowave_init_dosbox(const std::string &bus, const std::string &path,
                           const std::string &spi_cs) {
  (void)bus;
  (void)path;
  (void)spi_cs;
  warn_once("retrowave_opl3");
}
void retrowave_flush(RetroWaveContext *ctx) { (void)ctx; }
void retrowave_opl3_reset(RetroWaveContext *ctx) { (void)ctx; }
void retrowave_opl3_emit_port0(RetroWaveContext *ctx, uint8_t reg,
                               uint8_t val) {
  (void)ctx;
  (void)reg;
  (void)val;
}
void retrowave_opl3_emit_port1(RetroWaveContext *ctx, uint8_t reg,
                               uint8_t val) {
  (void)ctx;
  (void)reg;
  (void)val;
}
void retrowave_opl3_queue_port0(RetroWaveContext *ctx, uint8_t reg,
                                uint8_t val) {
  (void)ctx;
  (void)reg;
  (void)val;
}
void retrowave_opl3_queue_port1(RetroWaveContext *ctx, uint8_t reg,
                                uint8_t val) {
  (void)ctx;
  (void)reg;
  (void)val;
}
