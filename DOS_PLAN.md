# DOS Plan — cut-down DOSBox-X as a BMX machine

> Goal: BMX machine `DOS` next to the other machines. Select DOS in the
> Machine menu → reboot into DOS; F12 opens the menu; switch machines →
> reboot lands back. Acceptance test: Monkey Island 2 (min: 80286,
> 640KB, DOS 3.0, VGA, AdLib/SB, mouse, HDD 9MB).
>
> Scope (locked): DOSBox-X subset (interpreter CPU, no dynarec), text +
> Mode 13h (+EGA via upstream draw procs), PC speaker + AdLib/OPL + SB,
> no MT32/GUS, no SVGA/networking. No NC-style TUI — the shared BMX
> menu is used as-is.

## Done

Machine slot, DOS prompt boot (`Z:\>`, SD mounts), text + Mode 13h
video (tiling fixed), reset/autostart/typer, SB+AdLib sound, perf work
(presets, mailbox presents), keyboard, Select/Reset shortcuts,
Monitor (Color/B&W/Green/amber), guest mouse (INT33/serial/PS/2 via
`drain_guest_mouse`, UI-gated), snapshots (`.dos` files over
registered components, `dos_snapshot.h`).

## Open (hardware verification pending)

- Guest mouse in a real game (MI2 cursor + clicks).
- Snapshot save/load round-trip on hardware (`.dos` via Snapshots menu).
- EGA tweaked modes: works through upstream draw procs, never
  explicitly verified.
- MI2 full validation: playthrough, save/load, iMUSE without dropouts.
- Cosmetic: `DIR` dates show 01/01/2098; N/A submenus not greyed out.
- Pi5 kernel not rebuilt with the latest fixes.

## Pointers

- Glue: `src/dos/` (`dos_bridge`, `dos_emulator_core`, `dos_video`,
  `dos_sound`, `dos_input`, `dos_config`, `dos_debug`, `dos_snapshot.h`,
  `dos_savestate_stubs.cpp` = SaveState backend).
- Vendored subset: `third_party/dosbox-x/` (+ BMX `config.h`); no
  vendored-tree edits except `__circle__`-gated ones.
- Frame-loop template: `src/atari800/atari800_emulator_core.cpp`;
  menu trap template: `src/atari800/atari800_bridge.cpp`
  (`emux_trap_main_loop_ui`).
- Rebuild one kernel: `tools/pi4/build_pi4.sh --build-only
  --machine dos`; stage everything: `tools/pi4/build_pi4.sh`.
- Full session history: git log on `src/dos/`.
