//
// apple2_menu.h
//
// The shared BMX menu tree is built by ui_init_menu() through the emux_*
// bridge; nothing machine-specific is needed here (mirrors cpc_menu).
//

#ifndef apple2_menu_h
#define apple2_menu_h

namespace bmx {
namespace apple2 {

void menu_init(void);

}  // namespace apple2
}  // namespace bmx

#endif
