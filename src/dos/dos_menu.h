//
// dos_menu.h
//
// DOSBox-X menu integration for BMX bare-metal (Phase 0 placeholder)
//

#ifndef dos_menu_h
#define dos_menu_h

namespace bmx {
namespace dos {

// Initializes the DOSBox-X menu for the current machine. Phase 0: empty;
// the real menu (NC-style TUI) lands in bridge Phase 4.
void menu_init(void);

}  // namespace dos
}  // namespace bmx

#endif
