//
// mac68k_bmx_glue.h
//
// BMX bare-metal platform layer for Basilisk II (Macintosh 68k).
// Shared declarations for the mac68k_bmx_*.cpp providers (sys, prefs,
// misc stubs, XPRAM file, main-hook, cpu tick).
//
// Global-scope upstream symbols live outside bmx::mac68k; only the small
// BMX-owned glue (prefs setup entry, quit flag) is declared here.
//

#ifndef mac68k_bmx_glue_h
#define mac68k_bmx_glue_h

namespace bmx {
namespace mac68k {

// Applies the BMX default preferences + SD overrides (ROM scan, model
// mapping, video/sound/nocdrom flags). Runs once, called by boot_init().
// Safe to call exactly once: AddPrefsDefaults-style PrefsAdd* calls append
// duplicates, so this must not run twice (boot_init calls it once).
void mac68k_prefs_setup(void);

}  // namespace mac68k
}  // namespace bmx

// Quit request flag, set by QuitEmulator(), polled by the frame loop.
// Global scope (like the upstream main.h symbols it serves).
extern volatile bool mac68k_quit_requested;

#endif
