// cpc_tape_iface.h
//
// C-safe interface between the CPC hardware module / core loop (C) and the
// CDT pulse player (cpc_tape.cpp, C++). Timings are Z80 T-states at the
// CPC 4 MHz clock.

#ifndef cpc_tape_iface_h
#define cpc_tape_iface_h

#ifdef __cplusplus
extern "C" {
#endif

// Advance the pulse timetable by n T-states (called per opcode; cheap when
// idle: returns immediately unless a tape is playing with the motor on).
void cpc_tape_advance(unsigned tstates);
// Current EAR level (0/1) for PPI Port B bit7 and the audio mix.
int cpc_tape_level(void);
// Cassette motor state (PPI Port C bit4).
int cpc_tape_motor_on(void);
// Non-zero once the stream has played to the end (used to end the
// autostart warp, like the ZX kernel does).
int cpc_tape_finished(void);

#ifdef __cplusplus
}
#endif

#endif
