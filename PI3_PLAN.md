# PI3_PLAN.md — Raspberry Pi 3 support (32-bit, legacy display)

Status: researched, not started. Target: Pi3 (BCM2710, Cortex-A53,
1 GB) as a third board next to pi4/pi5, reusing the proven Pi4-aarch32
legacy-display path (`kernel7l.img`, firmware dispmanx, no KMS/V3D).

> Source: codebase research 2026-09-27 (build-system + runtime
> viability sweeps). No implementation yet.

## Verdict

Very doable. The Pi4 `--aarch32` configuration already exercises the
exact runtime Pi3 needs (firmware dispmanx presents, `ViceSound`
HDMI/USB sink, legacy GLES `crt-pi` shader auto-offered on model ≤ 3,
VCHIQ firmware IPC). What's missing is almost entirely plumbing a
third board dimension through build/stage/update, plus on-device
performance validation. No `RASPPI==3` handling exists anywhere today.

Recommended shape: **32-bit only** (`kernel7l.img`, `arm-none-eabi`,
`-mcpu=cortex-a53`), `BMX_PI4_LEGACY_DISPLAY=1` forced, all 10
machines in the matrix, cut on measurement.

## Runtime risk per machine (Pi4 ~6–12 ms Amiga frame ⇒ ~12–28 ms est. on A53 vs 20 ms budget)

- LOW: VIC20/PET/PLUS4, C64 x64 fast-SID (auto-forced), Atari800.
- MEDIUM: C64SC, C128-VDC, SuperCPU (use x64 core, `raster_skip2`).
- MEDIUM–HIGH: Atari ST/STE (8 MHz 68k + Shifter conversion + YM/DMA mix).
- HIGH → MEDIUM tuned: Amiga A500 (blitter Fast-no-bus, Latest/Nearest
  resampling, 512 KB chip baseline, no warp).
- HIGH → LOW–MEDIUM tuned: DOS 386DX-33 default (drop to 286-12 /
  `fixed 3000` via the live `dos_perf` menu).

Audio: no Pi3 delta expected (same sink/drivers). RAM: 1 GB fits
except pathological dual-256 MB HDFs. SoC specifics (VCHIQ, DMA,
mailbox, GPIO, timers): all compile-gated, inert on Pi3.

## Work items

### 1. Build matrix
- `tools/pi3/build_pi3.sh` (clone `build_pi4.sh` arg surface),
  `tools/pi3/vice310_build_common.sh` (`BMX_BUILD_BOARD=pi3`),
  `tools/pi3/stage_pi3_sd.sh` (`--board pi3`).
- `tools/lib/vice310_build_common.sh:63-131`: new `pi3)` case
  (arm 14.2 toolchain, `CIRCLE_CONFIG_ARGS --raspberrypi=3`,
  `VICE_ARCH_FLAGS -mcpu=cortex-a53`, `TARGET_BASENAME=kernel7l`,
  `TARGET_AARCH=32`, `LEGACY_DISPLAY=1`).
- `Makefile:85,147-174` (`BOARD=pi3 → RASPPI:=3`, AARCH 32-only),
  `881-927` legacy `fbl.o` objects for `RASPPI==3`, `951-952`
  force `LEGACY_DISPLAY=1`, `1139-1145` VC4 libs condition.
- `tools/lib/build_paths.sh`, `tools/lib/circle_target_build.sh`
  (VC4/sound/armstub conditions), `build_stage_machine.sh` (hardcodes
  `kernel8`/`pi4-test`).

### 2. Layout / stage / firmware
- `sd_layout.py`: `BOARDS += "pi3"`, scope/review/validator/CLI updates.
- `sd-layout.toml`: `pi3_boot` source, `[[pi3.file]]` boot set, device
  DTB rules. **Missing, must be sourced** (same origin as pi4/pi5
  firmware): `start.elf`, `fixup.dat` (`bootcode.bin`?), 
  `bcm2710-rpi-3-b*.dtb`, `bcm2710-rpi-cm3.dtb`?, `armstub7`.
- `stage_bmx_sd.py`, `install_sd.sh` (pi3 case, `KERNEL_NAME`,
  selector allowlist), `create_bmx_sd.py` is board-agnostic already.

### 3. Runtime flags + update subsystem
- Force legacy display for `RASPPI==3`; exclude `fbl_native_kms.cpp`
  (has `#error` for non-Pi4).
- `platform.cpp` kernel-base assert, `update_types` /
  `RunningBoard()` / manifest (`count==2`, `mask==3`, filenames,
  models) / migration: today the update contract is **exactly-2
  boards**. Gate Pi3 behind a manifest version bump so old clients
  don't choke — or ship Pi3 build-and-stage-only with updates
  disabled. `update_path_policy.py` equality check (`pi4 != pi5`)
  must become 3-way; regenerate the header.
- `menu_switch.c` kernel table/selector/overclock (`{3,kernel8-32.img}`
  row exists but is dead), `config_migration.cpp` (`pi3kms*`?).

### 4. Validate
- Full pi3 build here; on-device per-machine frame times via existing
  heartbeats (Amiga pacing vars, Hatari `nFrameSkips`, Atari800/DOS
  stats, VICE `cycles_per_second`); tune defaults or drop machines
  that can't hold 50 fps; confirm 1080p/GLES cost + thermals.

## Open questions (parked)
1. 32-bit only, or 64-bit Pi3 later too? (Recommend 32-bit only.)
2. All 10 machines, or a subset? (Recommend all, cut on measurement.)
3. Updates for Pi3: proper 3-way versioning, or disabled-first?
4. Pi3 firmware source: same raspberrypi-firmware origin as pi4/pi5?
