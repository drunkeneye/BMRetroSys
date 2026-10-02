//
// compileoptions.h — BMX bare-metal equivalent of ZEsarUX configure output.
//
// Upstream generates this file with ./configure, enabling one macro per
// available video/audio driver. On Circle there is no X11/SDL/ALSA/fbdev:
// every driver block stays disabled and the BMX glue in src/zxspectrum/
// provides the Circle video (scr_circle) and audio backends instead.
//

#ifndef COMPILEOPTIONS_H
#define COMPILEOPTIONS_H

// No COMPILE_XWINDOWS / COMPILE_SDL / COMPILE_SDL2 / COMPILE_FBDEV /
// COMPILE_CURSES / COMPILE_AA / COMPILE_CACA / COMPILE_STDOUT /
// COMPILE_SIMPLETEXT / COMPILE_COCOA: BMX drives video through
// src/zxspectrum/zx_video.c (FBL framebuffer).

// No COMPILE_ALSA / COMPILE_PULSE / COMPILE_DSP / COMPILE_SDL(2) /
// COMPILE_COREAUDIO / COMPILE_ONEBITSPEAKER: BMX drives audio through
// src/zxspectrum/zx_sound.c (circle_sound_write).

// Z80 emulation accuracy: keep contention + MEMPTR like desktop builds.
// Visual memory declarations stay on: upstream enables EMULATE_VISUALMEM
// by default and core files reference its types unconditionally (the
// debugger buffers themselves are never allocated on BMX).
#define EMULATE_CONTEND
#define EMULATE_MEMPTR
#define EMULATE_VISUALMEM
// #define EMULATE_CPU_STATS
// #define EMULATE_MEMORY_REFRESH_LIMIT
// #define EMULATE_SCF_CCF_UNDOC_FLAGS

// Use the reduced (fast) Spectrum core, as recommended for Raspberry Pi.
#define USE_REDUCED_CORE_SPECTRUM

// No Raspberry Pi GPIO speaker: audio goes over HDMI like the other kernels.
// #define EMULATE_RASPBERRY

// No libsndfile recording support.
// #define USE_SNDFILE

// Upstream configure stamps this into the generated config.h; ula.c reads
// it for the ZXI string register.
#define BUILDNUMBER "BMX"

#endif
