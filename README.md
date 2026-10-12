# BMRetroSys 0.4

BMRetroSys is bare-metal emulation for the Raspberry Pi 4/5 family:
nineteen emulated machines on one SD card, no Linux underneath.

This is a fork of [BMX](https://github.com/kdre/bmx) at commit
`81efb7e` (public source export 2026.08.19). All credit for the
foundation, BMX itself, and through it BMC64, VICE, Circle and the
whole bare-metal stack, goes to the respective authors. Your
work is legendary. Every change on
top of that base is mine; upstream history is preserved verbatim in
[`CHANGELOG_BMX.md`](CHANGELOG_BMX.md).



| Machine | Core |
| --- | --- |
| Commodore 64 (fast + cycle-exact cores) | VICE 3.10 |
| SuperCPU 64 | VICE 3.10 |
| Commodore 128 | VICE 3.10 |
| VIC-20 | VICE 3.10 |
| Plus/4 | VICE 3.10 |
| PET | VICE 3.10 |
| Atari 400/800/XL/XE/5200 | atari800 |
| Atari ST/STE/Mega ST/Mega STE (EmuTOS built in, genuine TOS pool) | Hatari |
| ZX Spectrum 48k/128k/+2/+2A/+3, Pentagon, Inves (in-menu model select) | ZEsarUX |
| Amstrad CPC (in-menu model select) | ZEsarUX |
| MSX | ZEsarUX |
| Amiga A1000/A500/A500+/A1200 (OCS/ECS/AGA, DF0–DF3, HDF, snapshots) | vAmiga |
| PC 286 DOS | DOSBox-X |
| Atari 2600 (cartridges, console-switch shortcuts) | Stella |
| Macintosh 128K | Mini vMac |
| Mac IIci/Quadra | Basilisk II |
| Apple II: IIgs (ROM 3/ROM 01) + //e (in-menu model select) | GSplus/KEGS |
| BBC Micro Model B: DFS/ADFS/cassette (in-menu model select) | CLK |


## AI all over the place

> Built with AI assistance: vibe-coded with Muse Spark 1.3 via
> [opencode.ai](https://opencode.ai). This is not a fully supported
> release, it is unclear whether any reported bugs will ever be
> fixed. 


## BUGS

And there a lot of bugs, AI is real human here, e.g., MSX sound is scrambled,
BBC does not really loads disks and screen flickers sometimes. 
I will not use these kernels often, so for now these will be kept buggy,
until someone creates an issue, then we will see, what the AI can do to fix.


## SD-card layout

One directory per machine under `systems/` (content) and firmware
pools under `roms/` — the same tree on every install:

```text
systems/C64/{carts,disks,snapshots,tapes[,prg]}  ... one dir per machine
systems/AMIGA/{disks,hdf,snapshots}              ... for everything it owns
systems/ATARIST/{disks,snapshots}                ...
systems/MSX/{snapshots}                          ... flat: carts/tapes/zips loose
systems/A2600/{snapshots}                        ... flat: cartridges loose
roms/C64, roms/DRIVES, ...                     ... system ROMs + keymaps
roms/AMIGA, roms/ATARIST, ...                   ... firmware pools (TOS/Kick live
                                                  inside the machine folders)
```

(One folder per machine for everything it owns; shared pools stay
flat under `roms/`. Case is significant on paper, irrelevant on the
FAT32 card.)


## What changed (0.4)

- New machines: Apple II (GSplus/KEGS: IIgs ROM 3/ROM 01 + //e,
  Slot 6/5/7 disks, monitor options, screenshots, PR#6 autostart),
  Macintosh 128K (Mini vMac), Mac IIci/Quadra (Basilisk II),
  BBC Micro Model B DFS/ADFS/cassette (CLK: 2×1770 DFS drives,
  SHIFT+BREAK autostart, Pi5 fan curve menu).
- Full list: [`CHANGELOG.md`](CHANGELOG.md).

## What changed (0.3)

- New machine: Atari 2600 (vendored Stella core, cartridge slot,
  splash logo, F12 menu, Select/Reset keyboard shortcuts defaulting
  to Space/Enter and remappable, persisted).
- Monitor emulation everywhere (Color/B&W/Green/Amber, applied live):
  per-pixel phosphor mapping on MSX, Spectrum, CPC, Amiga, ST, DOS
  and 2600 (Atari 8-bit already had it); VICE machines get the
  upstream green/amber `.vpl` palettes in the Palette menu (plus
  generated green/amber variants for VIC20, Plus/4 and the C128 VDC).
- ZIP browser opens ROM packs with non-ASCII names (French/Portuguese
  DOS codepages and UTF-8 fold to FAT-safe ASCII; strict firmware
  update flow unchanged).
- Full list: [`CHANGELOG.md`](CHANGELOG.md).

## What changed (0.2)

- Rename BMX8bit → BMRetroSys (About, logs, SD paths, docs;
  C identifiers intentionally unchanged).
- `systems/<machine>/…` content layout + `roms/…` firmware pools, with
  archive.org ROM fetching (Kickstarts, TOS) and per-machine staging.
- New machines: Atari ST (Hatari, EmuTOS + genuine TOS selector),
  Amiga (vAmiga: 5 models, chipset/CPU/memory/Paula options, DF0–DF3,
  HDF, snapshots, warp, drive clicks), DOS (DOSBox-X).
- Machine renames HATARI → ATARIST, DOSBOX → DOS across menus,
  kernels, settings and SD layout.
- Full list: [`CHANGELOG.md`](CHANGELOG.md).

## What changed (0.1)

- New machines: Atari 8-bit (vendored atari800), Atari ST (vendored
  Hatari 2.6.1, EmuTOS zero-config + genuine TOS selector), Amiga
  (vAmiga: 5 models, chipset/CPU/memory/Paula options, DF0–DF3, HDF,
  snapshots, warp, drive clicks), DOS (DOSBox-X).
- Automatic firmware ROM download during staging
  (`--download-roms`, on by default): VICE ROMs from zimmers.net,
  Atari ROMs from FW-Altirra, Kickstarts + TOS images from
  archive.org. Files already present are kept; whatever cannot be
  fetched is reported in `MISSING-ROMS.txt`.
- Harmonized SD layout (`systems/<machine>/…`, `roms/…`) with legacy
  fallbacks removed; per-machine stage script
  (`build_stage_machine.sh --machine …`); full-tree rebuild keeps all
  kernels and `machines.ini` in sync.
- Monitor modes, PAL video blend, BMP screenshots.
- Sound menus (stereo dual POKEY, HiFi, click, Bienias fix, latency).
- USB keyboard: fixed F1..F5 console keys, user-mappable F6..F11,
  raw hardware arrow codes; combined hat+analog joysticks.
- SIO drives D1:..D8:, disk sets, blank ATRs, SIO patch, LEDs.
- Cartridges with reboot-on-change and a blank creator.
- ZIP browser with nested archives, progress bar and cancel.
- Full list: [`CHANGELOG.md`](CHANGELOG.md).

## What's missing / known issues

- NTSC variants are PAL-only where noted (Amiga video path).
- Attaching from very large nested archives can hang after
  extraction; under diagnosis, see `CHANGELOG.md`.
- The SuperCPU binary plus seven drive/PET ROM variants cannot be
  auto-downloaded and stay manual (see `MISSING-ROMS.txt`).
- The keyboard mapping editor is hidden for the Atari port for now.
- Atari on Pi 5 is staged but not device-tested.
- System updates are disabled (menu entries greyed out): there is no
  update channel in this fork. Install new releases by writing a fresh
  SD image. The Developer submenu is greyed out for the same reason.

## Building

Build, staging, installation and SD-card creation are documented in
[`BUILDING.md`](BUILDING.md). A fresh clone builds with:

```sh
tools/pi4/build_pi4.sh
```

which fetches the ARM toolchain, all missing ROMs, then stages a
bootable tree (default `pi4-test/sdcard`).

It will also download intros and demos from ftp.pigwa.net and place
it into the disks folders, just that you have some cool stuff to start with.


## Credits

- BMX / BMC64 and all third parties listed in
  [`THIRD_PARTY_SOURCES.md`](THIRD_PARTY_SOURCES.md) and the on-device
  Licenses menu — everything we stand on.
- atari800 (GPL-2.0) by the Atari800 team; Altirra OS/BASIC for the
  zero-config fallback.
- Hatari (GPL-2.0) by the Hatari team; EmuTOS for the zero-config TOS.
- vAmiga (MPL-2.0) by Dirk W. Hoffmann; DiagROM for the no-Kickstart
  fallback.
- DOSBox-X for the DOS machine.
- GSplus/KEGS (GPL-3.0) by Kent Dickey for the Apple II machine.
- CLK (MIT) by Tom Harte for the BBC Micro machine.
- Atari firmware pool: [FW-Altirra](https://github.com/ascrnet/FW-Altirra).
- Commodore firmware: [zimmers.net](https://www.zimmers.net/anonftp/pub/cbm/firmware/).
- Amiga Kickstarts + Atari TOS images: [archive.org](https://archive.org)
  (Amiga Demobase / TOSEC collections, fetched at staging time).
