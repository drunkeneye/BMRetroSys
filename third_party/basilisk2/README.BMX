// third_party/basilisk2/README.BMX
//
// Basilisk II (68k Mac) integration for BMX bare-metal.
//
// Upstream source: /home/aydin/code/macemu/BasiliskII/src
// (full tree copied; no upstream commit hash available -- directory copy
// date 2026-10-05). Upstream COPYING kept as COPYING.BASILISK2 alongside,
// upstream README.md kept as README.upstream.md. This file (README.BMX)
// plus bmx-shim/ are the only BMX-owned files here; everything else is
// pristine upstream.
//
// Included subset (compiled for BMX):
//   - CrossPlatform/      (portable CPU/memory/video glue)
//   - uae_cpu/ minus compiler/ (UAE 68k interpreter: cpuemu, cpustbl,
//     newcpu, memory, readcpu etc.)
//   - include/            (shared headers)
//   - portable tops       (adb, audio, cdrom, disk, emul_op, ether, extfs
//     stubs, macos_util, main, prefs, prefs_items, rom_patches,
//     rsrc_patches, scsi, serial, slot_rom, sony, timer, user_strings,
//     video, xpram, pict)
//
// Excluded list (kept on disk as reference, NOT compiled):
//   - native_cpu/         (host x86 direct-execution path)
//   - powerrom_cpu/       (PowerPC ROM patch engine)
//   - uae_cpu/compiler/   (JIT code generator -- interpreter only on BMX)
//   - SDL/                (SDL video/audio frontends)
//   - Unix/ backends except as reference (X11/ALSA/pthreads host glue)
//   - slirp/              (userland TCP/IP stack)
//   - BeOS/, Windows/, MacOSX/, AmigaOS/, dummy/ (foreign-OS backends,
//     dummy stubs)
//   - GTK prefs editor    (Unix/GTK preferences UI)
//
// Build flags:
//   -D__BMX__ -DUSE_JIT=0 (also forced via bmx-shim/basilisk_prefix.h,
//   -include'd on every TU)
//   banks (UAE) addressing: REAL_ADDRESSING=0, DIRECT_ADDRESSING=0
//   interpreter only: EMULATED_68K=1, USE_JIT=0, ENABLE_VOSF=0
//   no host services: SUPPORTS_EXTFS=0, USE_SDL_VIDEO=0, USE_SDL_AUDIO=0
//
// License: GPL-2.0 (see COPYING.BASILISK2).
//
// To update: re-copy macemu/BasiliskII/src/ over this directory
// (mkdir -p ... && cp -a <src>/. third_party/basilisk2/), refresh
// COPYING.BASILISK2 / README.upstream.md if changed, keep bmx-shim/
// and this file untouched.
//
