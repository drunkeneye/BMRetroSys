//
// atarist_menu.h
//
// AtariST menu integration for BMX bare-metal (Phase 0 placeholder).
//

#ifndef atarist_menu_h
#define atarist_menu_h

namespace bmx {
namespace atarist {

// Initializes the AtariST menu for the current machine. No-op for now; the
// TOS/GEM-aware menu rows land in later phases.
void menu_init(void);

}  // namespace atarist
}  // namespace bmx

#endif
