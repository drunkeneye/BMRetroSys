//
// opl3duoboard.h — BMX-local stub (NOT upstream DOSBox-X).
//
// Same story as opl2board/opl2board.h: the real Opl3DuoBoard needs a host
// serial port. API-preserving stub; no-op bodies in
// src/dosboxx/dosboxx_sound_oplboards.cpp. oplemu=opl3duoboard on BMX logs
// once and renders silence; default DBOPL path unaffected.
//
#ifndef BMX_OPL3DUOBOARD_STUB_H
#define BMX_OPL3DUOBOARD_STUB_H

#include <stdint.h>

class Opl3DuoBoard {
 public:
  Opl3DuoBoard();
  void connect(const char* port);
  void disconnect();
  void reset();
  void write(uint32_t reg, uint8_t val);
};

#endif
