//
// zx_zesarux.h
//
// Single include point for vendored ZEsarUX headers in BMX glue files.
//
// ZEsarUX declares `typedef struct s_menu_item menu_item` (zxvision.h)
// which collides with BMX's `struct menu_item` (third_party/common/ui.h)
// in C++. The typedef is renamed to zx_menu_item for the glue translation
// units only; vendored C files are unaffected (they never see BMX headers).
//

#ifndef zx_zesarux_h
#define zx_zesarux_h

#define menu_item zx_menu_item

// NOTE: utils.h and storage/tape.h are deliberately NOT included here:
// utils.h pulls zxvision.h (handled) but tape.h pulls zvfs.h, which uses
// `new` as an identifier and cannot compile as C++. The glue defines the
// handful of tape/utils symbols it needs itself (zx_shim/zx_tape).
#include "cpu.h"
#include "operaciones.h"
#include "mem128.h"
#include "ula.h"
#include "contend.h"
#include "screen.h"
#include "audio.h"
#include "ay38912.h"
#include "settings.h"
#include "joystick.h"
#include "realjoystick.h"
#include "timer.h"
#include "charset.h"
#include "cores/core_reduced_spectrum.h"
#include "cores/core_spectrum.h"

#undef menu_item

#endif
