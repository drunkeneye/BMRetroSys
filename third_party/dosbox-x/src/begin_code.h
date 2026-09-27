/* begin_code.h shim — minimal SDL packing/declspec prologue for BMX.
 * Only what vendored SDL_ttf.h needs: DECLSPEC (empty: static link, no
 * DLL import/export) and structure-packing push. Pairs with close_code.h. */
#ifndef BMX_SDL_BEGIN_CODE_H_
#define BMX_SDL_BEGIN_CODE_H_

#ifndef DECLSPEC
#define DECLSPEC
#endif

/* Structure packing is irrelevant on ARM EABI with our stub structs (no
 * over-the-wire layouts), so this is a no-op. */
#if defined(_MSC_VER)
#pragma warning(disable:4244)
#pragma pack(push,8)
#endif

#endif /* BMX_SDL_BEGIN_CODE_H_ */
