# ZEsarUX vendored subset (BMX port)

Upstream: https://github.com/chernandezba/zesarux
Version: 13.1-SN (EMULATOR_VERSION in cpu.h)
Vendored from: local tree ~/code/zesarux @ 517b3f57 (2026-09-28)
License: GPL-3.0 (see LICENSE); staged on-device as /licenses/zesarux.txt

## What is included

ZX Spectrum emulation core (reduced/fast core) and its direct
dependencies, pristine upstream except the bmx-shim/ directory:

- cpu.c/h, operaciones.c/h, cpus/z80_codsinpr.c/h (Z80 CPU)
- ula.c/h, contend.c/h, mem128.c/h (ULA, contention, paging)
- cores/core_reduced_spectrum.c/h, cores/core_spectrum.h (frame loop)
- video/screen.c/h (scanline render, rainbow buffer, palettes)
- audio/audio.c/h, audio/audionull.c/h (mixing buffers, null backend)
- soundchips/ay38912.c/h (AY-3-8912)
- settings.c/h (emulator settings globals), charset.c/h, joystick.c/h
- every other *.h from upstream src/ (include graph only, no code)
- roms/: Spectrum ROMs shipped in the upstream git, treated as
  redistributable per project policy (48, 128, +2, +2A, +3, inves,
  pentagon + OpenSE/SE-Basic/sp-rom/clones)
- bmx-shim/compileoptions.h: bare-metal configure equivalent
  (no SDL/X11/ALSA/fbdev/pthreads drivers, reduced core, no GPIO)

## What is deliberately excluded

- All video drivers except the null template (scrsdl*, scrxwindows,
  scrcurses, scraa, scrcaca, scrstdout, scrsimpletext, scrfbdev):
  video goes through src/zxspectrum/zx_video.c (Circle FBL).
- All audio drivers except null (sdl, alsa, pulse, dsp, coreaudio,
  onebitspeaker): audio goes through src/zxspectrum/zx_sound.c.
- menu/, zxvision/ (replaced by the shared BMX menu + emux_api bridge),
  network/zrccp/zeng, printers, realjoystick, docs, screenshots,
  my_soft, tests, .mmc/.flash media images.
- storage/tape.c, snap/snap.c: TAP trap loading and SNA snapshots are
  implemented directly in src/zxspectrum/ (zx_tape.c) against the
  core's public symbols, avoiding the desktop file-format stack.
- timer.c, utils.c, debug.c: replaced by BMX shims in src/zxspectrum/
  (zx_timer.c/h, zx_utils.c/h, zx_debug.c/h) providing the exact
  symbols the core references, driven by Circle timers and the SD log.
- cores/ for other machines and machines/*.c: not needed for the
  Spectrum kernel. CPC and MSX kernels will vendor their own
  core_* + machines/* files into this same tree later; nothing in
  the Spectrum build depends on their absence.

## Build

Compiled directly by the main Makefile (RASPI_ZXSPECTRUM block),
like third_party/atari800 and third_party/hatari. Upstream files are
never edited; BMX specifics live in src/zxspectrum/.
