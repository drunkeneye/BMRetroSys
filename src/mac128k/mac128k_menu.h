//
// mac128k_menu.h
//
// Mac 128K menu integration for BMX bare-metal.
//

#ifndef mac128k_menu_h
#define mac128k_menu_h

namespace bmx {
namespace mac128k {

// Menu rows are built by the bridge (emux_add_*); nothing to prebuild.
void menu_init(void);

}  // namespace mac128k
}  // namespace bmx

#endif
