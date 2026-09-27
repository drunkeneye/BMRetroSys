//
// RetroWave_DOSBoX.hpp — BMX-local stub (NOT upstream RetroWave).
//
// The real RetroWave backend drives external OPL3 hardware over serial/SPI
// via RetroWaveLib (POSIX/Win32 serial + Linux SPI). None of that exists on
// bare-metal BMX, so this header declares exactly the symbols that
// hardware/adlib.cpp references; no-op bodies live in
// src/dosboxx/dosboxx_sound_oplboards.cpp. oplemu=retrowave_opl3 on BMX logs
// once and renders silence; default DBOPL path unaffected.
//
#pragma once

#include <stdint.h>
#include <string>

// Opaque context: adlib.cpp only ever passes its address through.
struct RetroWaveContext {
  int dummy;
};

extern RetroWaveContext retrowave_global_context;
extern int retrowave_global_context_inited;

extern void retrowave_init_dosbox(const std::string& bus,
                                  const std::string& path,
                                  const std::string& spi_cs);
extern void retrowave_flush(RetroWaveContext* ctx);
extern void retrowave_opl3_reset(RetroWaveContext* ctx);
extern void retrowave_opl3_emit_port0(RetroWaveContext* ctx, uint8_t reg,
                                      uint8_t val);
extern void retrowave_opl3_emit_port1(RetroWaveContext* ctx, uint8_t reg,
                                      uint8_t val);
extern void retrowave_opl3_queue_port0(RetroWaveContext* ctx, uint8_t reg,
                                       uint8_t val);
extern void retrowave_opl3_queue_port1(RetroWaveContext* ctx, uint8_t reg,
                                       uint8_t val);
