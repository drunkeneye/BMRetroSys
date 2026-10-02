//
// msx_zesarux.h
//
// Single include point for vendored ZEsarUX headers in BMX glue files.
//
// ZEsarUX declares `typedef struct s_menu_item menu_item` (zxvision.h)
// which collides with BMX's `struct menu_item` (third_party/common/ui.h)
// in C++. The typedef is renamed to msx_menu_item for the glue translation
// units only; vendored C files are unaffected (they never see BMX headers).
//
// NOTE: utils.h and storage/tape.h are deliberately NOT included here:
// tape.h pulls zvfs.h, which uses `new` as an identifier and cannot
// compile as C++. The glue defines the handful of tape symbols it needs
// itself (msx_shim/msx_tape).
//

#ifndef msx_zesarux_h
#define msx_zesarux_h

#define menu_item msx_menu_item

#include "cpu.h"
#include "operaciones.h"
#include "audio/audio.h"
#include "soundchips/ay38912.h"
#include "settings.h"
#include "machines/msx.h"
#include "video_chips/vdp_9918a.h"

#undef menu_item

#endif
