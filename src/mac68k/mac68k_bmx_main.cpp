//
// mac68k_bmx_main.cpp
//
// BMX bare-metal implementations of the platform functions from
// third_party/basilisk2/include/main.h: QuitEmulator, alerts, B2 mutexes,
// FlushCodeCache. InterruptFlags/Set/ClearInterruptFlag are NOT duplicated
// here (owned by mac68k_basilisk_globals.cpp); TriggerInterrupt lives in
// portable uae_cpu/basilisk_glue.cpp.
//

#include "sysdeps.h"

#include "mac68k_debug.h"
#include "mac68k_bmx_glue.h"

#include "main.h"
#include "user_strings.h"
#include "cpu_emulation.h"  // memory.h must precede newcpu.h (m68k_setpc uses
                            // get_real_address/get_long/put_long)
#include "uae_cpu/newcpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Quit request flag (polled by the frame loop after m68k_execute returns).
volatile bool mac68k_quit_requested = false;

// vde switch variable (declared in main.h; Unix defines it in main_unix.cpp).
// BMX has no VDE; stays NULL.
char *vde_sock = NULL;

void QuitEmulator(void) {
  // Unix QuitEmulator tears everything down and calls exit(0); on BMX we
  // cannot exit, so unwind the CPU (m68k_emulop_return sets quit_program,
  // which makes m68k_execute return) and let the frame loop idle so the
  // user can switch machines.
  bmx::mac68k::debug_log(
      "mac68k: QuitEmulator: setting quit flag + m68k_emulop_return()");
  mac68k_quit_requested = true;
  m68k_emulop_return();
}

void ErrorAlert(const char *text) {
  bmx::mac68k::debug_log("mac68k: ErrorAlert: %s",
                         text != NULL ? text : "(null)");
}

// NOTE: ErrorAlert(int)/WarningAlert(int) live in upstream main.cpp and
// forward to the const char* versions above via GetString() (provided by
// mac68k_bmx_stubs_misc.cpp). Do NOT define the int overloads here.

void WarningAlert(const char *text) {
  bmx::mac68k::debug_log("mac68k: WarningAlert: %s",
                         text != NULL ? text : "(null)");
}

bool ChoiceAlert(const char *text, const char *pos, const char *neg) {
  // No GUI on BMX: log the choice and always take the negative path.
  bmx::mac68k::debug_log("mac68k: ChoiceAlert: %s [%s/%s] -> false",
                         text != NULL ? text : "(null)",
                         pos != NULL ? pos : "(null)",
                         neg != NULL ? neg : "(null)");
  return false;
}

// Single emulator thread on BMX: the Unix pthread variant collapses to a
// dummy struct with no-op lock/unlock (mirrors the !HAVE_PTHREADS branch
// of main_unix.cpp).
struct B2_mutex {
  int dummy;
};

B2_mutex *B2_create_mutex(void) {
  bmx::mac68k::debug_log("mac68k: B2_create_mutex (dummy, single-threaded)");
  B2_mutex *m = new B2_mutex;
  if (m != NULL) {
    m->dummy = 0;
  } else {
    bmx::mac68k::debug_log("mac68k: B2_create_mutex: out of memory");
  }
  return m;
}

void B2_lock_mutex(B2_mutex *mutex) {
  (void)mutex;
}

void B2_unlock_mutex(B2_mutex *mutex) {
  (void)mutex;
}

void B2_delete_mutex(B2_mutex *mutex) {
  bmx::mac68k::debug_log("mac68k: B2_delete_mutex %p", (void *)mutex);
  delete mutex;
}

void FlushCodeCache(void *start, uint32 size) {
  // No JIT on BMX (USE_JIT=0): nothing to flush.
  bmx::mac68k::debug_log("mac68k: FlushCodeCache start=%p size=%u (no-op)",
                         start, (unsigned)size);
}
