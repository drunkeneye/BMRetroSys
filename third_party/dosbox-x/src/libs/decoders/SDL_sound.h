/* SDL_sound.h shim - CD-audio decoding (SDL_sound/libchdr) is a Phase-1
 * non-goal. The subset never calls Sound_* (verified: no Sound_ call sites
 * in any vendored .cpp); cdrom.h only holds a `Sound_Sample *` member, so a
 * forward declaration satisfies compilation. */
#ifndef BMX_SDL_SOUND_H_SHIM_
#define BMX_SDL_SOUND_H_SHIM_

struct Sound_Sample;

#endif /* BMX_SDL_SOUND_H_SHIM_ */
