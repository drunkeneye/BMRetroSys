//
// hatari_prefix.h -- force-included prefix for vendored Hatari C objects.
//
// A few Hatari headers (e.g. debug/symbols.h) rely on <stdint.h> /
// <stdbool.h> / <stdio.h> having been included by an earlier header in the
// TU, which is brittle under -I reordering. Injecting these idempotent
// system headers first (same pattern as DOSBOXX_FORCE_INCLUDE) makes every
// vendored translation unit self-sufficient without patching upstream files.
//

#ifndef BMX_HATARI_PREFIX_H
#define BMX_HATARI_PREFIX_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#endif  // BMX_HATARI_PREFIX_H
