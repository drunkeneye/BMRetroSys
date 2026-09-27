//
// opl2board.h — BMX-local stub (NOT upstream DOSBox-X).
//
// The real OPL2AudioBoard drives a physical OPL2 board over a host serial
// port (serialport/libserial.h + std::thread). Bare-metal BMX has neither,
// so this header preserves the class API used by hardware/adlib.cpp while
// the no-op bodies live in src/dosboxx/dosboxx_sound_oplboards.cpp.
// Selecting oplemu=opl2board on BMX logs once and renders silence; the
// default oplemu (DBOPL) is unaffected.
//
#ifndef BMX_OPL2BOARD_STUB_H
#define BMX_OPL2BOARD_STUB_H

#include <stdint.h>

class OPL2AudioBoard {
 public:
  OPL2AudioBoard();
  void connect(const char* port);
  void disconnect();
  void reset();
  void write(uint8_t reg, uint8_t val);
};

#endif
