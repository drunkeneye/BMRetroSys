//
// mac128k_bmx_glue.h
//
// Shared declarations for the Mac 128K (Mini vMac) BMX wrapper.
//

#ifndef mac128k_bmx_glue_h
#define mac128k_bmx_glue_h

#include <stddef.h>

namespace bmx {
namespace mac128k {

// 512x342 1-bit guest geometry (Mini vMac -m 128K).
const int kMac128kWidth = 512;
const int kMac128kHeight = 342;
const int kMac128kRowBytes = 64;

// ROM requirements: 64KB Macintosh 128K ROM.
const unsigned kMac128kRomSize = 64u * 1024u;

// RAM: authentic 128K.
const unsigned kMac128kRamSize = 128u * 1024u;

}  // namespace mac128k
}  // namespace bmx

#endif
