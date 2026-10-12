Mini vMac for BMRetroSys (Macintosh 128K kernel)
==================================================

Upstream: https://github.com/egrath/minivmac (Gryphel mirror of
https://www.gryphel.com/c/minivmac/) — Mini vMac 37.x, GPLv2
(see COPYING.txt). Original by Paul C. Pratt.

Vendored: the full `src/` tree (96 files) is copied verbatim so the
BMX build never depends on the setup tool at build time.

Generated config in `cfg/`: produced once with the upstream setup tool

    gcc -o setup_t setup/tool.c
    ./setup_t -n "bmx-mac128k" -t lx64 -m 128K -mf 1 -magnify 0 \
        -fullscreen 0 -var-fullscreen 0 -sound 1 > setup.sh
    ./setup.sh   # writes cfg/CNFUDALL.h CNFUDOSG.h CNFUDPIC.h
                 #         CNFUIALL.h CNFUIPIC.h SOUNDGLU.h STRCONST.h

(-mf 1/-magnify 0/-fullscreen 0/-var-fullscreen 0 drop the desktop
windowing/magnify code paths the BMX backend does not implement.)

for model 128K (128KB RAM, 64KB ROM, 512x342x1-bit, classic keyboard,
classic sound, 2 Sony drives). `cfg/CNFUIOSG.h` (Unix/X11) and the Unix
`SOUNDGLU.h` (ALSA) are NOT vendored; `bmx-shim/` provides bare-metal
replacements and must precede `cfg/` in the include path.

Compiled subset (mirrors the setup.sh Makefile for -m 128K, minus the
Unix OSGLU backend):
GLOBGLUE M68KITAB MINEM68K VIAEMDEV IWMEMDEV SCCEMDEV RTCEMDEV ROMEMDEV
SCSIEMDV SONYEMDV SCRNEMDV MOUSEMDV KBRDEMDV SNDEMDEV PROGMAIN
plus src/mac128k/mac128k_bmx_backend.c (BMX OSGLU replacement).

Model note: 128K/512Ke/Plus are compile-time variants in Mini vMac
(ROM size, RAM size, SCSI/800K hardware). This kernel builds -m 128K;
the ROM pool (/roms/MAC128K) accepts 64KB 128K ROMs (checksums
0x28BA61CE / 0x28BA4E50). 512Ke/Plus ROMs need a future -m Plus kernel.
