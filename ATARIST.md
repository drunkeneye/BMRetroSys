# ATARIST.md — Atari ST/STE (+MegaSTE) as a BMX machine

> **Goal:** new BMX machine `ATARIST` alongside C64/DOS (`dos`)/Atari800. Select ST in Machine menu → reboot into TOS/GEM. F12 opens the BMX menu. Floppies from SD (`/systems/ATARIST/disks`: `.ST`/`.MSA`, later `.STX`). Machines: ST, STE, MegaSTE (no Falcon/TT).
> **Acceptance test:** an ST game boots from floppy with YM music *and* a raster demo (overscan/sync-scroll class) renders correctly. Games/demos to be named later by user (test corpus pending).
> **Status:** Phase 1 confirmed on hardware (2026-09-25): EmuTOS 1.4 UK boots
> to a readable GEM desktop (ST, 640x400, keyboard+mouse+beeps working).
> Next: Phase 2 video (overscan/FBL growth), Phase 3 joystick/sound/floppy
> validation with the user corpus, Phase 4 genuine TOS + MegaSTE.

## TOS pool (local: `./roms/atari.st/`, build stages to SD `/tos`)

| File (local) | TOS | Hatari machine | Role |
|---|---|---|---|
| `tos106uk.zip` | 1.06 UK (STE) | STE | Genuine ST/STE path |
| `tos162uk.zip` | 1.62 UK (STE) | STE | Genuine ST/STE path (latest STE TOS) |
| `tos206uk.zip` | 2.06 UK (MegaSTE) | MegaSTE | Genuine MegaSTE path (best for late demos) |
| `tos306uk.zip` | 3.06 UK (TT) | — | PARKED (TT excluded) |
| `tos492.zip` | 4.9x (Falcon) | — | PARKED (Falcon excluded) |
| EmuTOS (to fetch, GPL) | — | ST | Zero-config default (boots GEM + most games) |

TOS 2.06 does **not** run on plain-ST Hatari (probes MegaSTE hardware) — hence MegaSTE mode below. Genuine TOS is proprietary: never vendored in git; staged from the local pool at SD-image time, same pattern as VICE-from-zimmers.

## D1–D9 Decisions

- **D1 — core = cut-down Hatari 2.5.x (pinned), ST/STE + MegaSTE.** Vendor under `third_party/hatari/` with compile gates excluding `falcon/`, `dsp/` (DSP56001), TT. Keep Hatari's own UAE fork (`uae-cpu/`, pre-generated tables; 68000/010 kept, 020+ gated) — do not swap Musashi/Cyclone (MMU/bus-error model assumes it). MegaSTE = STE + 16MHz toggle + 16KB cache + minor chipset (no VME/SCSI/LAN emulation unless a title needs it).
- **D2 — Atari800/DOSBox-X is the template.** New `src/atarist/` glue (~5–8k lines, 8 modules: bridge, emulator_core, video, sound, input, config, menu, debug). No new architecture. Frame loop mirrors `atari800_emulator_core.cpp:108–160`; F12 pump mirrors `atari800_bridge.cpp:1134–1172`.
- **D3 — Machine-slot mechanics, reused verbatim.** Checklist (all verified in-tree 2026-09-25): `mk/machines/Makefile-ATARIST-310` (`MACHINE_CLASS=RASPI_ATARIST`); `Makefile` OBJS block + pattern rules (`BUILD_DIR` + `BMX_COMMON_BUILD_DIR`, `ATARIST_SRC_DIR`, include dirs, force-include stub wall); `vice310_build_common.sh` `atarist)` case (SUFFIX `atarist`, `COPY_DEFAULT=0`); `BMC64_MACHINE_CLASS_ATARIST`; `menu_switch.c` parse/allowlist/suffix; `menu.c` emulator enum + name + populate + `/settings-atarist.txt` + non-VICE guards + license entry; `MachineId::ATARIST` + `kHatari` (`/st`, `/systems/ATARIST/disks`, `.atarist`); `viceapp` factory + `bootstat_atarist.h`; `machines.ini` `[video atarist-*]`/`[scaling atarist]`/`[machine ATARIST]`; `sd-layout.toml` kernels (`kernel8.img.atarist`, `kernel_2712.img.atarist`) + `/tos` + `/systems/ATARIST/disks` + snapshots dir + licenses; `build_stage_machine.sh --machine atarist`; menu.c+debug touch in `build_pi*.sh`.
- **D4 — TOS strategy.** EmuTOS ships as zero-config default (ST mode). SD pool (`/tos`) takes user TOS; menu selector: EmuTOS / TOS 1.06 / TOS 1.62 / TOS 2.06 (2.06 forces MegaSTE machine). Document pool in `ROMS.txt`-style note; genuine TOS never in git.
- **D5 — Demos-set fidelity bar (user chose demos included).** Frame-granular is not enough: MFP/GLUE/Shifter scanline interrupts run at raster granularity inside sub-frame pump slices (overscan, sync-scroll, digi-sound). Cycle budget watched via counter-driven tuning (DOSBox-X `perf snap` precedent).
- **D6 — Video/audio mapping.** Shifter low/med/high (+MegaSTE same) → FBL RGB565 blit, per-scanline chunky conversion. YM2149 + STE DMA → 44.1kHz pull into `circle_sound_write` (proven sink; `FRAG_SIZE 256 × NUM_FRAGS 16` pattern). No SDL in guest path. 50Hz pacing like the ST.
- **D7 — SDL2 excision (the big surgery).** Hatari ≥2.0 is SDL2-only: `main.c`, `screen.c`, `audio.c`, `dialog.c`, config GUI all assume SDL. Same playbook as `sdlmain.cpp`→`dos_emulator_core.cpp`: frame-loop stepping, Circle timer/scheduler, hand-cut stub wall (`atarist_*_stubs`, `errno` shims). No library target exists — the wall is manual.
- **D8 — Floppy first, HD later.** Phase 1: `.ST`/`.MSA` via SD-FAT reader (WD1772 FDC + DMA sector engine). Demo milestone adds `.STX` (copy-protected, demo-relevant). Folder-HD (`gemdos.c`) only if cheap; ASCSI/IDE deferred. IPF/CAPS (proprietary lib) never.
- **D9 — Tension note (EmuTOS-default vs demos-included).** Demos bang TOS internals where EmuTOS differs — so the genuine-TOS pool (D4) is load-bearing, not cosmetic. Validate demos against genuine 1.62/2.06 early, not late.

## Phases

- **Phase 0 — slot scaffold.** D3 checklist; `src/atarist/` 16-file placeholder skeleton with F12 pump. Exit: ST in menu, placeholder boots, reboot loop ST↔C64↔DOS works, F12 opens/closes, `kernel-machines` lists `atarist` on pi4+pi5.
- **Phase 1 — vendor + boot EmuTOS to GEM.** Import subset: `uae-cpu/` + `m68000/ioMem/mmu/glue` + Shifter `video/screen` + `mfp/ikbd/acia` + `psg/dmaSnd/audio-mixer-core` + `fdc/floppy/dma` + TOS boot + minimal `configuration/file/zip/cartridge` + MegaSTE delta (cache + 8/16MHz). SDL wall + `config.h` feature select. Exit: EmuTOS desktop, ST and MegaSTE machine modes selectable.
- **Phase 2 — video fidelity.** Low/med/high + border/overscan, screenshots (BMP), sub-frame raster interrupts. Exit: reference overscan demo renders clean (demo #1 of user corpus).
- **Phase 3 — input/sound/storage.** USB kbd→IKBD (F12 reserved), mouse/joy, YM+DMA→sink, sound menu options, `.ST`/`.MSA` attach + autostart, save states. Exit: game with music + mouse, playable start to finish.
- **Phase 4 — genuine TOS + `.STX` + persistence.** TOS pool + menu selector (EmuTOS/1.06/1.62/2.06+MegaSTE auto), `.STX` support (demo milestone), `/settings-atarist.txt` round-trip, Pi5 kernel.
- **Phase 5 — validation.** User corpus (2–3 games + 1–2 demos, TBD): full playthrough smoke + demo-render check; perf vs 8/16MHz ST; docs (`THIRD_PARTY_SOURCES.md`, `ROMS.txt`, changelog).

## Non-goals (locked)

Falcon/VIDEL/DSP56001, TT (hence tos306/492 parked), MegaSTE VME/SCSI/LAN, MIDI, printer, serial, debugger/disassembler, IPF/CAPS, PNG snapshots, network.

## Risks (top 5)

1. **SDL2 excision surface** (DOSBox-X-scale; no library target). Mitigate: strict subset + stub walls.
2. **EmuTOS vs demos (D9).** Mitigate: genuine-TOS validation early (Phase 2, not Phase 4).
3. **Scanline-interrupt fidelity vs frame-loop model.** Mitigate: sub-frame slices from day one; demo regression list.
4. **FDC/DMA timing for `.STX`.** Gated behind demo milestone; never blocks boot-to-GEM.
5. **68k throughput + 50Hz pacing under Circle.** Counter-driven tuning, same method that fixed DOSBox-X pacing/audio.

## File inventory (new)

- Slot: `mk/machines/Makefile-ATARIST-310`, `Makefile` block+rules, `vice310_build_common.sh` case, `emux_api.h`, `menu_switch.c`, `menu.c`/`menu_ids.inc`, `machine_descriptor.*`, `viceapp.*`, `bootstat_atarist.h`, `machines.ini`, `sd-layout.toml`, `sdcard/atarist/*`, `sdcard/licenses/{atarist,emutos}.txt`, `build_stage_machine.sh --machine atarist`.
- Glue: `src/atarist/{bridge,emulator_core,video,sound,input,config,menu,debug}.*`.
- Vendored: `third_party/hatari/` (+1 `config.h`), `THIRD_PARTY_SOURCES.md` entry, `THIRD_PARTY_SOURCES` ROM note.

## Open questions (for work start, no action now)

- Acceptance corpus: 2–3 games + 1–2 demos (user to name when test corpus is ready).
- MegaSTE-day-one stands; fallback to ST/STE+EmuTOS-first remains valid if Phase 0/1 cost surprises.
