//
// mac68k_basilisk_globals.cpp
//
// BMX bare-metal providers for Basilisk II globals referenced by the
// vendored uae_cpu core (newcpu/memory/basilisk_glue/fpu) and the
// host-generated CPU tables in third_party/basilisk2/gen.
//
// Owned symbols: CPUType, CPUIs68060, FPUType, TwentyFourBitAddressing,
// InterruptFlags (+ Set/ClearInterruptFlag), GetTicks_usec,
// Delay_usec, idle_wait, idle_resume.
// emux_* / vice_network_* are owned by another workstream and are
// intentionally NOT defined here.
//
// NOTE: EmulOp is NOT defined here. The real ROM-patch dispatch from
// upstream emul_op.cpp links into the BMX kernel, so any stub here would
// be a duplicate-symbol error (the BMX platform backends it needs —
// Sys_*, SerialInit, ClipInit, SCSIInit, ether/sound/video/timer platform
// layers — are provided by mac68k_bmx_*.cpp).
//

#include "sysdeps.h"
#include "main.h"
#include "rom_patches.h"
#include "timer.h"

#include <stdint.h>

// NOTE: this TU must NOT include <circle/timer.h> (or any Circle header):
// Circle's circle/types.h redefines uintptr/intptr incompatibly with the
// sysdeps.h definitions above. Tick access goes through the header-free
// ::circle_get_ticks*() globals from src/kernel.cpp instead (which forward
// to CTimer::GetClockTicks*() on the running kernel).
extern "C" {
extern unsigned long circle_get_ticks(void);
extern uint64_t circle_get_ticks64(void);
}

// CPU and FPU type, addressing mode (cf. Unix/main_unix.cpp).
// 68030 with no FPU; 24-bit addressing for classic ROMs.
int CPUType = 3;
bool CPUIs68060 = false;
int FPUType = 0;
bool TwentyFourBitAddressing = true;

// ROM version (owned by upstream rom_patches.cpp; set by CheckROM() during
// boot before anything reads it).
// NOTE: do NOT define ROMVersion here (duplicate symbol).

// Currently pending interrupts (single emulator thread on BMX, so the
// Unix pthread-guarded variants collapse to plain accesses).
uint32 InterruptFlags = 0;

void SetInterruptFlag(uint32 flag)
{
	InterruptFlags |= flag;
}

void ClearInterruptFlag(uint32 flag)
{
	InterruptFlags &= ~flag;
}

// Microsecond timer (cf. Unix/timer_unix.cpp GetTicks_usec/Delay_usec).
// Backed by the Circle system timer via src/kernel.cpp. Delay_usec is a
// busy wait, matching Circle's own SimpleusDelay() implementation.
uint64 GetTicks_usec(void)
{
	return (uint64)circle_get_ticks64();
}

void Delay_usec(uint32 usec)
{
	uint64_t start = circle_get_ticks64();
	while (circle_get_ticks64() - start < usec) {
	}
}

// Idle sleep/wake (cf. Unix/timer_unix.cpp). No second thread on BMX:
// wake is a no-op, sleep is a short yield so the frame loop keeps pumping.
void idle_wait(void)
{
	Delay_usec(1000);
}

void idle_resume(void)
{
}
