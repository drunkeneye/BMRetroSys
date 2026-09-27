//
// atarist_menu.cpp
//
// Hatari menu integration for BMX bare-metal (Phase 0 placeholder).
//

#include "atarist_menu.h"

namespace bmx {
namespace atarist {

void menu_init(void) {
  // Empty: TOS selector / floppy attach / machine-model rows land in
  // Phases 3-4 via the bridge (emux_add_*).
}

}  // namespace atarist
}  // namespace bmx
