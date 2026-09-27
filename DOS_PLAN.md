# DOS Plan — cut-down DOSBox-X as a BMX machine (Option B)

> **Goal:** new BMX machine `DOS` alongside C64/+4/…/Atari800. Select DOS in
> Machine menu → reboot into DOS. In DOS, F12 opens the BMX menu **clone**
> rendered as an **80x25 text-mode TUI in Norton Commander style**. From there
> switch `machine=C64` (or any other) → reboot lands back on C64.
> **Acceptance test:** Monkey Island 2 (min: 80286, 640KB, DOS 3.0, VGA,
> AdLib/SB, mouse, HDD 9MB).
>
> **Status dashboard (update every session):**
> - Phase: 0 code done; Phase 1 subset vendored; **dos Pi4 kernel LINKS**
>   (`kernel8.img`, ~7.9MB, `build_pi4.sh --build-only --machine dos`
>   exit 0 on Linux cross toolchain 2026-09-24)
> - Last session: merged BMRetroSys bugfix/Atarimania-network tree into
>   BMRetroSys.dosbox (5 files 3-way merged incl. menu.c show_about fix +
>   update_path_policy regen + DOS OBJS for atarimania/netcheck);
>   wired link stubs into Makefile; fixed ARM-only gaps the mac host link
>   never saw (endian, alloca, clock_gettime, d_type, va_start); stubbed
>   the real 115-symbol ARM link surface (new dos_phase1_link2.cpp);
>   dropped 23 bogus mac-list dups actually defined by vendored
>   hardware.cpp/programs.cpp; deleted scratch UNDEF_PHASE1.txt
> - Next action: boot test on Pi4 hardware (reboot loop DOS<->C64
>   + F12), then Phase 1.2 entry-point/frame-loop wiring + Phase 2 text-mode
> - 2026-09-24 (stage): full `build_pi4.sh` green incl. stage to
>   `pi4-test/sdcard` (all 9 kernels; `kernel8.img.dos` 7.9MB; default
>   stays atari800). Fixed along the way: netcheck_host_trace weak no-op
>   default in netcheck.cpp (latent bug: VICE kernels never linked since
>   the Atarimania merge — only atari800 defined it; also fixes BMRetroSys
>   proper), dos COPY_DEFAULT 0 (fallback kernel must equal atari800).
>   7 niche drive ROMs unfetchable upstream (MISSING-ROMS.txt, pre-existing).
>
> **How to resume (every session):**
> 1. `git -C /Users/aydin/tmp/BMRetroSys status -sb && git log --oneline -5`
> 2. Read this file top-to-bottom (dashboard → session log → current phase).
> 3. Read `src/atari800/atari800_emulator_core.cpp:108-160` (frame-loop
>    template) and `src/atari800/atari800_bridge.cpp:1134-1172`
>    (`emux_trap_main_loop_ui` template).
> 4. Pick up at "Next action". Update dashboard + append session log at end.

## 0. Decisions (locked unless noted)

- **D1 core = cut-down DOSBox-X subset, NOT full DOSBox-X, NOT 86Box/PCem, NOT
  Faux86.** Reason: MI2 needs a real 286; Faux86 has only 8086/186 + limited
  286 (XT ~12MHz V20 performance) — boots maybe, plays no. 86Box/PCem are
  low-level + Qt/wxWidgets + per-machine ROMs — strictly harder than DOSBox-X
  on Circle (see prior analysis).
- **D2 Atari800 is the template.** Every step mirrors
  `third_party/atari800/` (pristine +1 `config.h`) + `src/atari800/` glue
  (~4.5k lines, 8 modules: bridge, emulator_core, video, sound, input,
  config, menu, debug). No new architecture.
- **D3 Machine-slot mechanics (reuse, don't reinvent):**
  - Compile-time `MACHINE_CLASS=RASPI_DOS` (`mk/machines/Makefile-DOS-310`,
    `Makefile` `ifeq` block like `RASPI_ATARI800`:179-293).
  - Runtime `MachineId::DOS` in `src/machines/machine_descriptor.{h,cpp}`,
    `BMC64_MACHINE_CLASS_DOS` in `third_party/common/emux_api.h`,
    enum + maps in `third_party/common/menu.c` (`MachineEmulator`,
    `machine_populate_emulators`, `menu_build_machine_switch`) and
    `menu_switch.c` (`parse_machine_class`, allowlist, `kernel_suffix_for_machine_class`
    → `.dos`, `switch_build_boot_plan` / `switch_apply_boot_plan`).
  - Boot: `src/viceapp.{h,cpp}` factory
    (`#elif RASPI_DOS → new DosboxxEmulatorCore`), `src/bootstat_dos.h`,
    `sdcard/machines.ini` `[machine DOS]` + `[video dos-*]` +
    `[scaling dos]`, `sd-layout.toml` kernels
    (`kernel8.img.dos`, `kernel_2712.img.dos`) + `/dos` system dir +
    `/systems/DOS/disks` + licenses, `/settings-dos.txt` in `menu.c`.
- **D4 F12 = pause + TUI menu (Atari pattern).** Frame loop calls
  `poll_input()` then `ui_handle_toggle_or_quick_func()` every frame
  (`atari800_emulator_core.cpp:150-154`); F12 key path is
  `src/kernel.cpp:827-829` → `emu_key_pressed(KEYCODE_F12)` →
  `third_party/common/menu.c:2761-2763` → `ui.c:860-862 ACTION_Exit` toggle.
  DOS port implements `emux_trap_main_loop_ui()` like
  `atari800_bridge.cpp:1134-1172`: `menu_about_to_activate()`,
  show UI layer, pump `ui_check_key()` + `ui_render_single_frame()` while
  `ui_enabled`, then hide layer and resume. Emulation is paused while open.
- **D5 Menu LOOK = 80x25 text-mode Norton Commander clone.** The menu TREE
  (Machine/Video/Sound/Drives/...) stays the BMX `menu.c` tree so all machines
  share it, but for the DOS kernel the RENDERER is a text-mode TUI:
  80x25 cell buffer, blue panels, double borders, pull-down/header bar,
  list boxes, `Esc/F12 closes` semantics (same as `menu.c:2991`). Two
  implementation candidates (decide in Phase 4 spike):
  - (a) **Skin the existing menu:** keep `menu.c` logic, add a text-mode
    backend that draws glyphs into the emulated VGA text buffer
    (8x16 font, 720x400) instead of `FB_LAYER_UI` graphics. Less logic fork,
    more renderer work.
  - (b) **Native DOS TUI:** separate `src/dos/dos_menu.cpp` that reads
    the same `emux_*` options/settings but draws its own NC-style panels
    directly to VGA text memory. More code, full visual control.
  - Either way the menu must be usable with USB keyboard only (arrows +
    Enter + Esc/F12), like the current menu.
- **D6 Video strategy (phased):** text 80x25 first (menu + DOS prompt + MI2
  installer), then VGA Mode 13h 320x200x256 (MI2 gameplay), then EGA
  tweaked modes. No SVGA/Voodoo/OpenGL — explicitly out.
- **D7 Audio strategy (phased):** PC speaker first, then AdLib/OPL
  (DOSBox `dbopl`), then SB DSP stub. No MT32/FluidSynth/GUS — explicitly out.
- **D8 DOSBox-X subset (include / stub):**
  - INCLUDE: `src/cpu/` (normal/286/8086 interpreter only, NO dynarec yet),
    `src/memory/`, `src/hardware/vga*` (text + mode13h subset), `src/hardware/
    pic|pit|dma|timer`, `src/hardware/keyboard`, `src/dos/` (kernel +
    drive_local/fat + cdrom stub), `src/shell/`, `src/ints/bios*`,
    `src/misc/` (setup/config), `src/fpu/` stub-off (`fpu=false` like QBasic
    note in dosbox-x README).
  - STUB/DELETE: `src/gui/sdlmain.cpp` (10.9k lines → replace with
    `dos_emulator_core.cpp` frame loop), `src/output/` (all 6 backends),
    `src/gui/render.cpp/sdl_mapper/sdl_gui`, SDL threads/timers/net,
    FluidSynth/MT32, pcap/slirp, ALSA/CoreAudio, OpenGL/D3D/Metal, FFmpeg
    capture, printer, curses debugger, Voodoo/Glide, xBRZ.
  - Self-contained DOS (no ROM hunt, unlike VICE/PCem) — `src/builtin/`
    tools ride along.

## 1. Phase 0 — Machine slot scaffold (no emulation yet)

Goal: `Machine=DOS` appears in menu, builds a kernel that boots to a
"no core yet" text screen, reboots back and forth.

- [ ] 0.1 `mk/machines/Makefile-DOS-310` (`MACHINE_CLASS=RASPI_DOS`,
      `VICELIBS:=`, like `Makefile-ATARI800-310:1-17`).
- [ ] 0.2 `Makefile` `ifeq ($(MACHINE_CLASS),RASPI_DOS)` OBJS block +
      `-I third_party/dosbox-x-subset` + pattern rules (copy `702-735` Atari
      block shape); early-return entry in `tools/lib/vice310_build_common.sh`.
- [ ] 0.3 `emux_api.h`: `BMC64_MACHINE_CLASS_DOS`; `menu_switch.c`:
      parse class, allowlist, `.dos` suffix; `menu.c`: `MachineEmulator`
      enum + name/map + `machine_populate_emulators`.
- [ ] 0.4 `machine_descriptor.{h,cpp}`: `MachineId::DOS`
      (`DOSBox, /dos, `/systems/DOS/disks`, /dos/bootstat.txt, .dos`,
      cycles = 286/10MHz profile placeholder).
- [ ] 0.5 `viceapp.{h,cpp}` factory arm + `src/bootstat_dos.h` +
      `src/dos/` skeleton (8 files, stubs returning safe defaults).
- [ ] 0.6 `sdcard/machines.ini` + `sd-layout.toml` (kernels, `/dos`,
      `/systems/DOS/disks`, license) + `sdcard/dos/ROMS.txt` (note:
      self-contained, empty pool OK) + `/settings-dos.txt` in `menu.c`.
- [ ] 0.7 Build `tools/pi4/build_pi4.sh --build-only --machine dos`,
      stage, verify: select DOS → reboot → placeholder screen → F12 menu
      opens/closes → select C64 → reboot → C64.
- Exit: reboot loop works, F12 pump works, no emulation.

## 2. Phase 1 — Minimal DOSBox-X subset extraction + boot to prompt

- [ ] 1.1 Vendor subset under `third_party/dosbox-x/` (pristine + single
      BMX `config.h`, document in `THIRD_PARTY_SOURCES.md`); wire
      `dos_config.cpp` (`config_init`, argv-equivalent, `memsize=16`
      default from `src/dosbox.cpp`).
- [ ] 1.2 `dos_emulator_core.cpp::RunFrameLoop`: replicate
      `atari800_emulator_core.cpp:133-160` timing skeleton
      (`CTimer::GetClockTicks`, `frame_ticks`, 5-frame resync,
      `poll_input → ui_handle_toggle_or_quick_func → core_step →
      video_render → sound_play`, `circle_boot_complete()` on frame 1).
- [ ] 1.3 Replace `sdlmain.cpp` event loop with frame-loop stepping;
      stub SDL threads/timers with Circle `CScheduler::Yield/MsSleep` +
      spinlocks; stub SDL_net/sockets.
- [ ] 1.4 Boot to `C:\>` using emulated text mode + `drive_local` mapped to
      `USER:`/systems/DOS/disks` (FATFS, like `viceapp.cpp:519-535` mounts).
- Exit: DOS prompt, keyboard echo, `dir` of SD-backed drive.

## 3. Phase 2 — Video: 80x25 text → Mode 13h → EGA

- [ ] 2.1 VGA text 80x25 (8x16 → 720x400) via FBL/KMS
      (`src/pi4kms/`, `src/pi5kms/`, `kernel.h:86-108`); palette +
      `emux_apply_video_adjustments/geometry_changed`.
- [ ] 2.2 Mode 13h 320x200x256 path (MI2 gameplay); `vga_draw.cpp` subset
      only (8.6k lines → text + mode13h first).
- [ ] 2.3 EGA tweaked modes (MI2 installer/note in README: EGA tweaked).
- Exit: MI2 installer screens readable; `video_save_screenshot()` BMP
  (mirror `atari800_video.cpp`).

## 4. Phase 3 — Input / Audio / Storage

- [ ] 3.1 Input (`dos_input.cpp` like `atari800_input.cpp:516`):
      USB kbd → PC AT codes (F12 reserved for menu, F1-F11 pass through),
      serial-mouse emulation (MI2 needs mouse), joysticks stub.
- [ ] 3.2 Audio: PC speaker → AdLib/dbopl → SB stub; fixed 44100Hz sink via
      `circle_sound_write()` (`atari800_sound.cpp` pattern);
      `emux_add_sound_options` + `set_sound_sample_rate`.
- [ ] 3.3 Storage: `drive_local` ↔ FATFS (`hd0.img` + dir-as-drive),
      read/write images, `emux_attach/detach_disk`, `autostart_file`;
      save/load state (`emux_save/load_state`, heap not stack — Atari
      ~210KB lesson in `emulator_core.cpp:184-190`).
- Exit: MI2 installed (9MB) from images, mouse works in menus.

## 5. Phase 4 — F12 NC-style TUI menu clone (the headline feature)

- [ ] 4.1 Spike: (a) skin vs (b) native TUI (see D5); timebox 2 days.
- [ ] 4.2 Implement 80x25 renderer: header bar, two-panel/ dialog layout,
      blue background, white/yellow text, `Esc/F12 closes; input consumed`.
- [ ] 4.3 Wire full option tree via existing `emux_add_*_options` +
      `emux_handle_menu_change/quick_func/rom_change` (copy Atari bridge
      coverage in `atari800_bridge.cpp:220-2423`; stub VICE-only with safe
      defaults).
- [ ] 4.4 Machine-switch plan end-to-end (DOS→C64→DOS) through
      `switch_build_boot_plan` / `switch_apply_boot_plan` /
      `perform_system_action → reboot()`.
- [ ] 4.5 Persist `/settings-dos.txt` round-trip
      (`save_additional_settings/load_settings_done/handle_loaded_setting`).
- Exit: in DOS, F12 → NC-blue menu, change Sound/Machine/Video, save,
  reboot lands correctly.

## 6. Phase 5 — MI2 validation + Pi5 + cleanup

- [ ] 5.1 MI2 playthrough smoke: install, new game, first island walk,
      save/load, iMUSE music present (no dropouts = pass).
- [ ] 5.2 Perf: measure vs 10MHz 286 target; if <50%, profile emu vs render
      vs sound (Atari per-stage profiling pattern
      `emulator_core.cpp:128-131`); consider interpreter tuning only
      (NO dynarec in this plan — separate project if needed).
- [ ] 5.3 Pi5 KMS/V3D path (`kernel_2712`), build stamps/touch rules
      (`build_piX.sh:179-185` touch `menu.c` + `dos_debug.cpp`).
- [ ] 5.4 Docs: `CHANGELOG.md` entry, `THIRD_PARTY_SOURCES.md` DOSBox-X
      entry, `ROMS.txt`/`bootstat.txt`, grey-out N/A submenus like Atari
      (Update/Developer).
- Exit: `CHANGELOG` 0.2 entry, Pi4 verified, Pi5 staged.

## 7. Explicit non-goals (this plan)

SVGA/VESA beyond Mode 13h, 386+/Pentium dynarec, Win9x, MT32/FluidSynth/GUS,
networking/IPX/pcap/slirp, Voodoo/Glide, OpenGL/D3D/Metal, FFmpeg capture,
printer, curses debugger, Pi0/2/3 legacy.

## 8. File inventory (SOT list)

- BMX slot: `Makefile`, `mk/machines/Makefile-DOS-310`,
  `tools/lib/vice310_build_common.sh`, `tools/pi4|pi5/build_pi*.sh`,
  `tools/sd_layout.py`, `sd-layout.toml`, `sdcard/machines.ini`,
  `sdcard/dos/{ROMS.txt,bootstat.txt}`, `src/machines/machine_descriptor.*`,
  `src/viceapp.*`, `src/bootstat_dos.h`, `src/kernel.cpp` (F12 path only).
- Emux contract: `third_party/common/{emux_api.h,menu.c,menu_switch.c,ui.c,
  kbd.c,keycodes.*}`.
- DOS glue (new): `src/dos/{dos_bridge.{h,cpp},dos_emulator_core.{h,cpp},
  dos_video.{h,cpp},dos_sound.{h,cpp},dos_input.{h,cpp},
  dos_config.{h,cpp},dos_menu.{h,cpp},dos_debug.{h,cpp}}`.
- Vendored: `third_party/dosbox-x/` (+1 `config.h`), `THIRD_PARTY_SOURCES.md`.

## 9. Risks (top 5)

1. SDL excision bigger than estimated (`sdlmain.cpp` 10.9k lines, SDL in 75
   files) → mitigate by strict subset (Phase 1 include-list) + stub walls.
2. C++ exceptions/STL on circle-stdlib (VICE/Atari precedent is C) → early
   Phase 1 compile spike decides `-fno-exceptions` feasibility.
3. VGA `vga_draw.cpp` breadth (8.6k lines) → text + mode13h only; EGA next.
4. Interpreter too slow for MI2 286/10MHz → accept degraded target first
   (installer + intro), dynarec explicitly deferred.
5. Menu renderer fork drift → prefer skin (D5-a) unless spike proves otherwise.

## 10. Session log (append-only)

- 2026-09-24 — Plan created from BMX+Atari+DOSBox-X/86Box/PCem/Faux86
  analysis. Decisions D1-D8 locked. Next: Phase 0.1.
- 2026-09-24 — Phase 0 implemented (3 parallel subagents): new
  `mk/machines/Makefile-DOS-310`, `Makefile` DOS OBJS block +
  `filter`-guard fix + pattern rules, `vice310_build_common.sh` dos case;
  `emux_api.h`/`menu_ids.inc`/`menu_switch.c`/`menu.c` plumbing (class, enum,
  `.dos` suffix, `dos` name, `/settings-dos.txt`, `/DOS`
  subdir, license entry, non-VICE branch coverage); `machine_descriptor`
  kDosboxx (placeholder 10MHz cycles), `viceapp` factory arms,
  `bootstat_dos.h`, `src/dos/` 16-file placeholder skeleton with
  F12 pump mirroring Atari, `machines.ini` + `sd-layout.toml` +
  `sdcard/dos/` + `sdcard/licenses/dosbox-x.txt`. Verified:
  `kernel-machines --board pi4|pi5` both end `…, atari800, dos`.
  NOT done: kernel build + stage + reboot-loop test (Phase 0.7).
- 2026-09-24 (cont.) — Pre-build integration push (no ARM build yet):
  link-gap fix (emu_mouse_* in dos_input.cpp, 7x emux_atari_* in bridge);
  subset vendored (72 .cpp + 280 .h, 14M); config.h grown (C_FPU=1,
  C_LIBPNG=1 shim-backed, RESDIR=/dos, PACKAGE, attr + Bit* type block);
  shims: bmx_sdl_stubs.h (+SDL_CD/SDL_mutex/Thread/cond), SDL/SDL_video/SDL_net,
  SDL_sound, chd, png, begin/close_code, output_{ttf,tools,gamelink,
  direct3d11-stub}, render_scalers.h, guid.h, aviwriter/{riff,riff_wav_writer,
  avi,avi_writer}.h, include/guid.h; Makefile: DOS_FORCE_INCLUDE
  (-include stubs+png), -I root + aviwriter. Host -fsyntax-only green for
  all 72 TUs; host link => 169 undefs (saved briefly, then worker consumed).
  Link-stub worker ABORTED after writing 3 files, before Makefile wiring +
  verification. UNVERIFIED — resume there. NOTE: README.BMX tail (§C_FPU,
  ftime notes) is stale vs config.h reality; refresh when stubs verify.
  /tmp/dos_objs/*.o + re-run recipe live in shell history only.
- 2026-09-24 — Boot crash diagnosed via host harness (g++, --gc-sections,
  /tmp/dos-trace/driver.cpp, outside repo): DOS_SetupConfigSections
  threw `basic_string: construction from null is not valid`. Two stub bugs:
  (1) sbGetSectionName returned "" forever -> SB loop (dosbox.cpp:3796) ran
  past its 2-element def_* arrays (OOB -> null -> throw at 3rd iteration);
  fixed to upstream {"sblaster","sblaster2"}+NULL. (2) ide_names all nullptr
  -> AddSection_prop(nullptr) at dosbox.cpp:5328; fixed to upstream 8 names.
  Host harness now prints "SetupConfigSections OK". Fixes are in OUR stubs
  only (deleted_owner_stubs.cpp, globals.cpp); vendored tree untouched.
  Next: rebuild kernel, re-run Pi boot (expect boot_init to advance past
  "sections ready").
- 2026-09-24 — Rebuilt (`build_pi4.sh --build-only --machine dos`,
  kernel8.img 7968384) + staged to pi4-test/sdcard (kernel8.img.dos
  16:31). MISSING-ROMS.txt unchanged (pre-existing niche drive ROMs).
  Ready for Pi boot test.
- 2026-09-24 — Pi log: sections now READY, then silence after
  `InitTickLoop + RealInit...` (no TERMINATE = hard fault, not exception).
  Cause: MAPPER_AddHandler stub ignored the `DOSBoxMenu::item**` out-param;
  RealInit calls item->set_text/description on it -> garbage pointer deref.
  Fixed with a static dummy (set_* are header-inline non-virtual assigns;
  BSS-zeroed so safe). Host harness extended (real BMX stub .o's + data
  stubs in /tmp/dos-trace) now runs Setup+TickLoop+RealInit clean.
  Rebuilt (7968416) + staged (16:38). Next: Pi boot, expect `RealInit done`.
- 2026-09-24 — Pi log reached ROMBIOS then `E_Exit: MEM_map_ROM_physmem
  ... handler page limit (0xf0-0xff >= 0x0)`: boot order skipped upstream's
  Init_AddressLimitAndGateMask/Init_MemoryAccessArray/Init_A20_Gate/
  Init_PS2_Port_92h before Init_RAM (sdlmain restart path). Added them +
  KEYBOARD_Init + Init_VGABIOS + MEM_A20_Enable(true) to dos_boot.cpp.
  Next crash (host full-boot harness, all 72 TUs + real stubs): CALLBACK_Init
  `Callback area spans 64KB segment` because RegionAllocTracking was stubbed
  to always fail. Vendored real src/misc/regionalloctracking.cpp (302 lines,
  +Makefile obj, stubs removed). Host full boot: ALL 34 STAGES OK through
  PROGRAMS_Init. Rebuilt (7980896) + staged (16:49). Next: Pi boot, expect
  `boot_init: ALL OK` then first guest screen (BIOS/VGA text).
- 2026-09-24 — BIG LEAP: guest runs to a DOS prompt (host-verified `Z:\>`
  in video memory). Wall-clock (SDL_GetTicks/Delay via CTimer in
  dos_debug.cpp); RENDER backend in dos_video.cpp (SetSize/
  StartUpdate/EndUpdate + XRGB32->RGB565 letterbox sink, TempLineAlloc);
  frame loop drives DOS_GetLoop slices; HID->KBD_KEYS map + KEYBOARD_AddKey
  drain; boot fires POWERON/RESET/RESET_END (CPU_OnReset, was running wild),
  S3 memsize 2MB stub, real VM_Boot_DOSBox_Kernel (DOS_Startup..SHELL_Run);
  prefix_overlay=".DBOVERLAY"; GFX_Events pumps keys+menu (SHELL_Run blocks
  in nested loop, frame loop alone would starve). Host fullboot: all stages
  OK, BIOS POST, logo 640x480, DOS boot, shell idles, `Z:\>` prompt.
  Screen row 0 shows `Bad command or filename - "z:\system\int...` (AUTOEXEC
  artifact, non-fatal, TBD). Rebuilt (7986208) + staged (17:48).
  Next: Pi boot — expect DOS text on screen, type `ver`, F12 menu alive.
- 2026-09-24 — Pi ran the NEW kernel (ALL OK) but stayed blue: guest threw
  int=3 (On_Software_CPU_Reset: port-92/8042/triple-fault) x6 early, no
  RENDER_SetSize ever. Host never does this. run_guest_slice now logs
  CS:IP+PIC at each throw and re-dispatches RESET/RESET_END on reboot
  (sdlmain reboot path) so a legit reboot recovers. Rebuilt (7986304) +
  staged (18:05). Next: Pi boot, read throw-site lines.
- 2026-09-24 — ROOT CAUSE of the blue screen: prior session cut TLB_SIZE to
  64K on Circle (256MB) for the 48MB kernel limit, but get_tlb_read() has
  no guard: the 386+ reset fetch at 0xFFFFFFF0 indexes entry 0xFFFFF out of
  bounds -> garbage fetch -> zero-sled -> triple-fault resets. Host has 1M
  entries, which is why it always worked there. Fix (vendored paging.h,
  __circle__-scoped like the cut itself): OOB get_tlb_read/write return
  null, get_tlb_readhandler/writehandler fall back to full-range
  MEM_GetPageHandler (alias ROM resolves correctly). Also covers the future
  S3 LFB at 0xE0000000. Rebuilt (8089952) + staged (19:42).
  Next: Pi boot — expect POST text, then DOS prompt.
- 2026-09-24 — TLB fix worked (fetch reaches handler path) then `E_Exit: No
  byte handler for read from fffffff0`: ROMAliasPageHandler has GetHost*
  but no readb override (upstream 1M TLB never falls back here). Added
  readb/readw/readd (+ignore writes) mirroring low ROM. Rebuilt (8090144)
  + staged (19:46). Next: Pi boot — POST should run now.
- 2026-09-24 — Host MOUNT verified live: vendored dos_programs.cpp boots to
  `Z:\>` and `mount` lists `Z Internal Virtual Drive DOS-X` (RUN OK).
  Fixed along the way: empty bfb stubs crashed Add_VFiles (VFILE_Generate_SFN
  returns NULL for "" -> std::string(NULL) throw); vendored drive_virtual.cpp
  now skips empty blobs in VFILE_RegisterBuiltinFileBlob + hardens the SFN
  path against NULL (Makefile already links dos_programs.o/drive_virtual.o,
  so the Pi build picks it up). Harness notes: SHELL_Run blocks the calling
  thread (nested Normal_Loop pumps keys), so the test runs the guest on a
  thread and polls video via mem_readb (phys_readb bypasses VGA handlers and
  reads blank in text mode). Next: Pi boot — expect prompt + working MOUNT.
- 2026-09-24 — Pi4 build+stage green (kernel8.img.dos 8410784, staged
  20:41). Two ARM-only gaps the host link never saw: (1) builtin_stubs.o
  rule lacked DOS_FORCE_INCLUDE, so the appended mapper.h/programs.h
  block hit `le16toh not declared` via mem.h — wall added to both rules;
  (2) link needed imageDiskMemory typeinfo (added ints/bios_memdisk.o +
  imagedisk_emptydrive.o + dos/eltorito.o to OBJS) and 6 more symbols
  (mountfro, both IDE_is_CDROM overloads, savestates ZIP helpers via new
  zipOpenNewFileInZip3_64 shim entry, flock->0 + dirname() POSIX shims with
  C linkage). Host MT harness re-verified RUN OK after the changes.
  Next: Pi boot — expect prompt + working MOUNT.
- 2026-09-24 — Three Pi-reported fixes, host-verified + rebuilt (8413312)
  + staged (20:59). (1) Blue borders: video_init's blue sign-of-life fill
  persisted as letterbox (line sink only paints the guest rect); canvas now
  fills black at init and every RENDER_StartUpdate. (2) `mount ... :...`
  freeze: any path containing ':' took the PhysFS branch and physfsDrive's
  ctor called E_Exit = exit(1) = silent bare-metal halt (no F12). The
  iso/overlay/physfs ctors now report through the caller's error flag
  instead (all three callers already handle it); host test types
  `mount c BOGUS:/x` and gets a clean PhysFS error + live prompt. Also maps
  BMX FatFs volumes (SD:/USB:/USER:/SYS:, case-insensitive, normalized) to
  local mounts in MOUNT, so `mount c SD:/disks` proceeds to localDrive.
  (3) Color menu dead: emux color hooks were no-ops returning 0; now stored
  (1000-neutral convention) and applied in the XRGB32->RGB565 sink via a
  bri/con/gamma LUT + saturation/hue matrix, with a zero-cost neutral fast
  path. Next: Pi boot — black borders, working color sliders, SD:/disks as
  C:.
- 2026-09-24 — Pi-report round 2, rebuilt (staged 21:10). (1) Borders: canvas
  clear moved from every RENDER_StartUpdate to once per geometry change in
  RENDER_EndUpdate (zero per-frame cost, no border rewrite under the menu).
  (2) EXE "Access denied": EXEC+OPEN_READ can only get error 5 from the
  GetFileAttr DIRECTORY check, so added __circle__-gated failure-path LOGs
  (GetFileAttr mode/attr, FileOpen open/share/stream errno+path, OpenFile
  deny attr) — bmretrosys-dos.log after `C:MONKEY2.EXE` will name the exact
  call. (3) Color sliders were no-op bridge stubs; now live via LUT+matrix.
  Next: Pi boot — confirm black static borders, slider effect, and send
  `DIR C:` output + exact message + log tail if EXE still denied.
- 2026-09-24 — Pi data: DIR C: lists fine (dates 01/01/2098 everywhere),
  EXEC->"Access denied - MONKEY2.EXE", COPY reads fail, rename works, no BMX
  log lines (user still on 20:41 kernel: tag proves it). Read-pattern says
  data reads return 0 bytes (EXEC zero-byte header -> err 5; COPY 0 bytes;
  rename/DIR need no data), so added a 4th __circle__ LOG at the
  DOS_Execute zero-byte branch. Rebuilt + staged (21:21). Next: user updates
  kernel, retries, sends BMX log lines — exactly one of the 4 sites will
  name the failing call.
- 2026-09-24 — Build-tag automation (user rule: every kernel update bumps
  the tag; never stage without it). BUILD TAG/bootcode came from
  __DATE__/__TIME__ per TU, so they froze whenever those TUs weren't
  recompiled. Now: generated src/dos/dos_build_tag.h (local time +
  zone) rewritten by build_pi4.sh on EVERY build next to the existing
  menu.c touch; debug.cpp/boot.cpp consume the macro and rebuild via the
  header dep. New tools/pi4/build_stage_machine.sh --machine dos builds (via build_pi4.sh
  --build-only --machine dos) and stages kernel8.img.dos to
  pi4-test/sdcard ONLY on success, printing tag+size. Verified: staged
  image strings contain the fresh tag (21:29 +0200). Use this script for
  all future dos kernel updates.
- 2026-09-24 — User right: guest LOG never reached the SD log. DEBUG_ShowMsg
  and LOG::operator() were deliberate drop-stubs in deleted_owner_stubs, so
  all four access-denied diagnostics vanished. Both now forward into
  bmretrosys-dos.log as "dosbox-x: ..." lines (LOG drops DEBUG, keeps NORMAL+;
  every line still fsyncs). Host harness proves it: 181 guest lines, RUN OK
  unaffected. Rebuilt + staged via wrapper (21:38 +0200). Next: user retries
  MONKEY2.EXE and sends the dosbox-x: lines — the BMX denial/open/zero-byte
  site will be visible.
- 2026-09-24 — Access-denied data (21:38 kernel): `fopen(SD:/.../MONKEY2.EXE,
  rb)` returns NULL twice per EXEC (both open attempts); SYS:/ writes fine.
  errno=88 untrustworthy (global errno, core0 clobbers). localDrive::FileOpen
  now falls back to open()+fdopen() when fopen fails (__circle__-gated),
  and the failure branch logs return-value triage (stat rc/size/mode +
  open fd, no errno reliance). Rebuilt + staged via wrapper (21:47 +0200).
  Next: user retries; EITHER fallback fixes reads outright (exec/copy work)
  OR triage lines name the failing layer (stat vs open vs FILE*).
- 2026-09-24 — Triage came back: stat=0 size=110811 mode=100600 (regular
  file, correct) + raw open()=fd 5..8, but fopen/fdopen NULL. Toolchain
  disassembly proves this newlib's fdopen has NO fcntl path, so the break
  is elsewhere (fdopen==__sflags+__sfp; fds never reused hints close() may
  also fail). FileOpen now logs fallback open/fdopen/close results
  separately, and the triage does an open/close/reopen slot-reuse probe.
  Rebuilt + staged via wrapper (22:00 +0200). Next: triage lines show
  whether close() leaks (reopenfd==openfd+1) and whether fallback fdopen
  fails after a successful open.
- 2026-09-24 — ROOT CAUSE for access-denied/copy-failures: `share` defaults
  true, so localDrive::FileOpen/FileCreate take the open/share/fdopen
  branch — but circle-newlib's fdopen() requires fcntl(F_GETFL) (HAVE_FCNTL
  disassembly-confirmed absent from the ARM toolchain libc yet present in
  circle-newlib), which Circle answers ENOSYS: every share-mode open fails
  (errno 88 was real, not stale). stat/open/opendir/rename never touch
  fdopen, hence DIR/rename/mount/stat working while all data access died.
  Fix: share defaults false on __circle__ (vendored dosbox.cpp) + forced
  false in boot_init after RealInit (survives upstream syncs); single DOS
  session needs no share locking anyway. Plain fopen() was already proven
  by the boot log. Rebuilt + staged via wrapper (22:07 +0200). Next: EXEs
  run, COPY works; then the 2098-date cosmetic issue.
- 2026-09-24 — Menu Reset + autostart implemented (were no-op stubs).
  emux_reset hands a reboot throw (6=soft/3=hard) to GFX_Events, which
  throws so it unwinds through nested SHELL_Run like a guest reboot;
  run_guest_slice re-dispatches RESET/RESET_END and BIOS POST boots DOS
  fresh (throw sites verified rethrow: SHELL_Run, PROGRAMS_Handler,
  CALLBACK_RunRealInt; GFX_Events only fires from Normal_Loop). Host MT
  harness proved throw->unwind->redispatch (int=6 caught, reset
  re-dispatched); full POST loop could not complete on host (known Bitu
  32-vs-64 ~0UL assert in BIOS_OnResetComplete, Pi-unaffected) — Pi test
  pending. emux_autostart_file splits an SD path into dir+base and queues
  `mount c <dir> / c: / <base>` keystrokes via a paced 512-event typer in
  dos_input (1 event/GFX_Events call, 20ms spacing, 5s initial delay;
  lowercase+HID map incl. Shift combos for : _ "); menu Autostart... entry
  already exists for DOS. Rebuilt + staged via wrapper (22:25 +0200).
  OPEN: MI2/MI1 4-copies display (need RENDER_SetSize/Set Video Mode log
  lines), sound (SB+backend = Phase-3 chunk, not started).
- 2026-09-24 — MI2 boots via menu autostart (typer proven: mount+C:+run
  typed itself, Mode 13, src=320x200, game reads MONKEY2.00x). Known open:
  VGA image shows 4-across tiling. Added RENDER geometry diagnostics
  (EndUpdate logs src WxH vs lines delivered vs vga.draw WxH + mode, first
  8 mismatches + every-600 summary) to name the mismatch; staged (22:31).
  Reset/autostart shipped untested-on-Pi (host proved throw path only).
  Sound (SB+backend) acknowledged as Phase-3 chunk. Next: geometry lines
  from game session -> scaler fix; reset/autostart Pi confirmation.
- 2026-09-24 — Pi proved soft+hard menu reset (throw 3/6 -> POST -> fresh
  DOS; user confirms soft works) and autostart end-to-end. Autostart delay
  5s->2.5s (menu already says Starting...). Display: game geometry is
  PERFECT (320x200, 200/200 lines, draw==src, mode 3), so tiling must be in
  TempLine content; added one-shot sampler (3 lines, quarter-equality
  stats). Build hiccup: sampler landed in the anonymous namespace (internal
  linkage) while callers want external -> moved beside video_clear_canvas.
  Staged via wrapper (22:45 +0200). Next: sample lines -> scaler/content
  fix; hard-reset + autostart Pi confirmation.
- 2026-09-24 — Sampler only caught black fade-in frames (all-zero, useless).
  Now commits the first 3 NON-blank 320-wide lines (any frame) with source
  line numbers. Typer: re-select replaces pending script + restarts delay
  (was: append); initial delay 2.5s->1.5s (menu already says Starting...).
  Staged via wrapper (22:53 +0200). Next: non-blank sample lines -> tiling
  verdict + scaler fix.
- 2026-09-24 — Screenshots for debugging (and keeps): video_save_screenshot
  now writes 720x400 24-bit BMPs; menu Video section gains "Save
  screenshot" for DOS (reuses MENU_ATARI_SCREENSHOT id); dos
  emux_handle_menu_change auto-names SYS:/systems/DOS/snapshots/shotNN.bmp
  (USB/SYS/SD probe order, atari800 pattern). Staged via wrapper (22:59).
  Next: user screenshot of the 4-tile screen -> see the actual pixels.
- 2026-09-24 — Screenshot forensics: shot01 shows 640x100 strip (rows 0-99,
  cols 40-679) of period-160 content; sampler only ever saw black fade-in
  lines, so TempLine gameplay content is still unverdict. Two hardening
  moves: (1) quiesced screenshots (EndUpdate copies the finished frame to a
  stable buffer on menu request; BMP writer waits <=1s, else live-read
  fallback) so grabs can't tear mid-frame; (2) sampler re-arms every 600
  frames for a fresh gameplay verdict. Staged via wrapper (23:13 +0200).
  Next: quiesced shot + fresh sample lines (+ phone photo of the live
  screen as ground truth).
- 2026-09-24 — Display forensics: shot01 measures a 640x100 strip (rows
  0-99, cols 40-679) of period-160 content = period-80 source quarters,
  mostly-identical with drift (stale frames?). TempLine sampler only caught
  black fade-in so far. Added draw-struct fields (line_length/blocks/add/
  step/panning/bpp) to geometry lines + quiesced screenshots + sampler
  re-arm per 600 frames. Staged via wrapper (23:18 +0200). Next: llen value
  tells whether the bmode proc underdraws (80px) + fresh sample/screenshot.
- 2026-09-24 — Sampler had selection bias (border lines first every window,
  never active video). Now skips s_line_y<40; summaries gain linear_base +
  addr_shift. Staged via wrapper (23:50 +0200). Next: gameplay quarter-eq
  (low=tiled TempLine -> proc inputs; high=content fine -> per-mode display
  path) + quiesced shot from current kernel.
- 2026-09-24 — Display ROOT CAUSE + fix: M_VGA took the CRTC-stride
  (bmode) draw proc; with dword addressing (ashift=2, standard for chained
  mode 13h) its skip=16 packs 4 subsampled VRAM rows side by side into
  every output line = the 4-across tiles (llen=1280 proved full 320px
  writes; tile math 80px quarters x4 rows matches screenshot to the pixel).
  Fix (__circle__-gated, M_VGA S3 branch only): use
  VGA_Draw_Xlat32_Linear_Line, whose consecutive chain4-linear reads are
  provably pixel-correct for chunky content in both chained and unchained
  layouts (same as the ET4K/MCGA branch already does). Rebuilt + staged via
  wrapper (23:56 +0200). Next: user confirms one clean MI1 title image.
- 2026-09-25 — Display ROOT CAUSE (closed): S3 Trio keeps compatible_chain4
  off, so guest chain4 writes land FLAT via the map handler while the stock
  M_VGA draw path assumes the spaced (dword) layout: bmode skip=16 packed 4
  subsampled rows per line (the 4-across tiles), and with the linear-proc
  experiment the 1280 stride sampled every 4th row (quarter-squish). Fix
  (__circle__-gated, M_VGA S3 branch): linear draw proc (consecutive reads
  match flat writes, as ET4K/MCGA already do) + address_add=scan_len*8 (one
  flat row per output line). Flat+flat+320 is self-consistent by
  construction and immune to CRTC/S3 stride subtleties. Rebuilt + staged via
  wrapper (00:18 +0200). Next: user confirms one clean MI1 title image.
- 2026-09-25 — Live color preview: menu pauses the guest, so sliders painted
  nothing until close. Now keeping an XRGB32 shadow (≤640x480) of the last
  frame in the sink and repainting the canvas from it on every slider tick
  (video_refresh_preview, same mapping as the sink). Staged via wrapper
  (00:26 +0200). Sound scoped for next: circle_sound_write() backend exists
  (C64/Atari path); SB job = vendor sblaster+mixer+dma+opl (~8k lines),
  route MIXER output to circle_sound_write; AdLib music comes along free;
  MT-32 needs a MUNT synth port (large, deferred).
- 2026-09-25 — Sound (SB + AdLib/OPL voice) implemented, staged via wrapper
  (09:44 +0200, 8.68MB). Vendored pristine: hardware/sblaster.cpp,
  adlib.cpp, dbopl.cpp (+opl.cpp include), nukedopl.cpp, cqm.c,
  mame/{fmopl,ymf262,ymdeltat}.{cpp,h}+emu.h, esfmu/{esfm,esfm_registers}.c.
  BMX-local stub headers+glue for hardware-serial OPL boards only
  (opl2board/opl3duoboard/RetroWave: log-once + silence if selected;
  default oplemu=DBOPL never touches them). Drain: MIXER_BMX_Pull/RATE
  (__circle__ export in vendored mixer.cpp reusing the real SDL callback
  body) -> stereo-mono downmix -> circle_sound_write @44100Hz, pulled per
  frame on wall-clock (self-clocking under/overrun handling). boot_init:
  mixer rate forced 44100 pre-MIXER_Init + SBLASTER_Init (OPL rides along
  via SBLASTER ctor; PC speaker was already mixing, now audible).
  Deleted-owner stubs retired where now real (SB/OPL POD, sbGetSectionName,
  SB_Get_Address, OPL_SaveRawEvent, SB PIC-event globals); absent backends
  stubbed (MIDI/MPU401/CMS/PAS: link-only, never called on sb16 defaults).
  MT-32 deliberately out (needs MUNT synth + ROMs). Next: user runs MI1/MI2
  with sbtype=sb16 — expect AdLib music + SB sfx; send `sound_play:` log
  lines (peak>0 = guest is generating audio).
- 2026-09-25 — Sound FIX + menu + log quiet (staged via wrapper, 10:13
  +0200). (1) REAL no-sound cause: the outer frame loop parks inside the
  first guest slice forever (SHELL_Run never returns), so sound_play()
  behind it ran once and never again (no sound_play lines in the 09:44
  log at all). Drain moved into GFX_Events (fires on every Normal_Loop
  idle slice, nested SHELL included); sound_play reworked (unconditional
  first-call log + call/pull heartbeat, 10ms gate, s_last_ms advances on
  pulls only). Frame-loop call kept for pre-shell coverage. (2) Sound >
  Emulation menu populated (was empty -> "none"): SoundBlaster
  none/sb1/sb2/sbpro1/sbpro2/sb16 + OPL mode + OPL emulator, live
  section read/write via HandleInputline, applies after menu Reset
  (SBLASTER reconstructs on RESET); new MENU_DOS_* ids; PC speaker
  left at default on (no runtime rebuild path). (3) VGA log spam muted:
  7 vendored __circle__ guards (CRTC/SEQ unknown index, MEM_SlowPath,
  Blinking, Lines left, refresh rate, Set Video Mode x2) + DOS_VGA_TRACE
  macro (0) gating glue RENDER/sample/slice/PC/heartbeat chatter. Boot,
  sound, menu, error lines unaffected.
- 2026-09-25 — Sound SINK fix (staged via wrapper, 10:21 +0200). The 10:13
  log proved the drain worked (pulls climbing, peak ~11k in game) yet
  space=4096 frozen + menu Output=None: the ViceSound device never existed.
  circle_boot_complete() creates it, but lives behind the parked frame
  loop (first guest slice never returns), so it never ran. Now called
  once from the first GFX_Events pump (same ~1s timing, idempotent).
  Next: user confirms audible music + Output=HDMI + space fluctuating.
- 2026-09-25 — Fast-with-fallback performance (staged via wrapper, 11:22
  +0200). (1) Pacing: outer 50ms sleep -> adaptive 8ms period top-up
  (emulator_core; the 50ms+20-tick clamp capped the guest at ~40% speed,
  which also caused the robotic audio); vendored ticks clamp 20->50
  (__circle__-gated, dosbox.cpp). (2) Defaults: core=simple + cycles=
  fixed 15000 via cpu section in boot_init (pre-CPU_PreInit); new
  dos_perf module owns tables/state/boot-apply. (3) Performance menu
  in emux_add_machine_options (CPU core auto/normal/simple/full + CPU
  cycles auto/fixed 7500/15000/30000/max, both LIVE via onpropchange, new
  MENU_DOS_CPU_CORE/_CYCLES ids) + settings-dos.txt persistence
  (dos_core/dos_cycles, applied at boot). (4) Silent wins: video
  column-map LUT (u64 div per pixel -> table load, identical output) +
  oplrate forced 44100 (no mixer resample). Next: user confirms full-speed
  MI1/MI2 + clean audio; tune fixed value if needed.
- 2026-09-25 — Perf instrumentation (staged via wrapper, 11:38 +0200).
  All core/cycles combos equally slow => cap is per-tick/per-slice fixed
  cost, not batch size (cputype=auto leaves cpudecoder alone, verified in
  cpu.cpp:3928ff). Added: __circle__ Normal_Loop split timers in vendored
  dosbox.cpp (bmx_perf_cpu_us/n vs bmx_perf_other_us/n, CTimer µs) + glue
  outer timers (slice/video/snd) + 5s `perf snap` line (iters/s, ms
  splits, guest% from audio frames). Also removed the "applies
  immediately" menu popups (kept "after Reset" ones). Next: user runs MI
  ~30s, sends 3-4 `perf snap` lines -> data-driven fix, then instrumentation
  stays gated or goes.
- 2026-09-25 — Perf snapshot relocated into the live pump (staged via
  wrapper, 11:48 +0200). The 11:38 snapshot never fired (outer-loop
  parking, same bug). Now: perf_audio_tick + perf_snapshot_tick in
  dos_perf, called from GFX_Events; snapshot = pump rate, vendored
  cpu/other split, drain time, audio fps (= guest speed), guest video
  fps + video sink ms/frame (new video_guest_frames/video_sink_stats).
  Dead outer-loop snapshot removed. Key hint from the user's numbers:
  audio runs at ~44kfps (guest time full speed) while the game feels
  slow -> video presentation is the prime suspect now, snapshot will
  confirm (guestfps vs vid ms).
- 2026-09-25 — Video-frame accounting (staged via wrapper, 12:02 +0200).
  Snapshot now: pump rate, cpu/other split, drain time, audio fps (guest
  speed), guest fps (completed frames), abort fps (dropped frames),
  emulated refresh (RENDER_SetSize fps), sink ms/frame, present ms/call.
  Decisive combos: guest+abort ~= emufps -> VGA timer fine, drops in abort
  path; guest+abort << emufps -> VGA timer itself slow (PAL coupling?);
  pres high -> display pipeline blocks the guest. Next: user's snap lines.
- 2026-09-25 — ROOT CAUSE + mailbox fix (staged via wrapper, 12:24 +0200).
  perf snap proved: audio 44.1k (guest time full), cpu 14% idle, sink
  0.01ms, but guest video pinned at exactly 50.0fps with pres=~15ms per
  present. Cause: native KMS PresentLayerList fences EVERY present via
  pi4_native_kms SynchronizePreviousPresent (= wait previous vblank),
  serializing a 70fps producer to the 50Hz PAL display; 750ms/s burned
  waiting, game logic coupled to frames runs at 50/70. Fix: per-layer
  mailbox flag (fbl.h + both fbl backends, circle_set_mailbox_fbl API +
  kernel wrapper, dos sets it on VIC in video_init); mailbox layers
  skip the fence (double-buffered slots: pending back buffer overwritten,
  never a scanning one), everyone else byte-identical incl. menu overlay
  (non-mailbox -> fence preserved). Expect: pres ~1-2ms, guest -> ~70fps.
- 2026-09-25 — CPU preset menu (staged via wrapper, 14:45 +0200).
  Machine-accurate presets (cputype+cycles): 8088 4.77MHz (8086/300),
  80286 12MHz (286/3000), 80386DX 33MHz (386/15000, default),
  80486DX2 66MHz (486/40000), Pentium 100MHz (pentium/60000), + Custom
  (manual tweaks; selecting it is a no-op). 8086/286 force authentic
  normal sub-decoders, 386+ keep simple; no _prefetch (E_Exit risk), no
  pentium_mmx (FPU dep). Preset change reboots DOS when crossing families
  (upstream; existing machinery) with a one-time warning popup; manual
  core/cycles stay live with no popups. Cycles menu extended to all preset
  values (300..60000) so it tracks presets. Persisted (dos_cpupreset,
  applied before manual core/cycles); boot applies preset->core->cycles.
- 2026-09-25 — CPU preset crash + flicker (staged via wrapper, 14:59
  +0200). (1) Crash: cputype HandleInputline throws int(3) across menu C
  frames on family crossing -> terminate. Now: preset applies cycles
  live, stages cputype, requests standard soft reset; run_guest_slice
  catch applies it via perf_apply_pending_reset (Change_Config outside
  any CPU batch, reboot throw swallowed — values assigned pre-throw).
  Same-cputype reselect = cycles-only, no reset. (2) Flicker: mailbox
  skip-fence allowed back-to-back submits to overwrite the actively
  scanning buffer (unlike portions per frame). Now: submits throttled to
  >=20ms apart in RENDER_EndUpdate — skipped frames aren't dropped work
  (canvas keeps updating, next submit shows latest, guest never waits);
  mailbox covers transients, fence covers lifecycle ops.
- 2026-09-25 — Audio never-block (staged via wrapper, 15:13 +0200). The
  stutter: ViceSound::AddChunk spin-waits (50us slices) while the HDMI
  queue is full, so any overlong write stalls the guest thread and the
  next pull arrives later (cascade; 5-50ms `other`/`snd` spikes in the
  snap). Worse at high cycles (longer worst-case batches -> deeper
  stalls -> longer spins). Fix: drain caps every write at
  circle_sound_bufferspace (units match, mono frames); overflow is
  dropped (ms glitch) instead of stalling (audible stutter). Dropped
  frames counted in the sound heartbeat. Mixer still advances on every
  pull, so no mixer-side overrun.
- 2026-09-25 — Rare-stutter fix (staged via wrapper, 15:44 +0200). Never-block
  worked (no more 5-50ms spikes), but `dropped` still ticked +500-800 in
  clusters = 12-18ms gaps. Source: `60:Unhandled command 0` bursts (MI1
  programs the 8042 in bursts; each LOG_ERROR line is an SD fsync stalling
  the guest into a pull backlog). Muted __circle__-style (ACK behavior
  unchanged). Expect: dropped stays flat, rare stutters gone.
- 2026-09-25 — DSP command log mute (staged via wrapper, 16:04 +0200).
  7s hell episode: guest spinning on DSP 0x10 (direct DAC; upstream notes
  games use timer+0x10 for sfx), snap showed cpu=271ms/pump, pump=4/s,
  guest=0.4fps, audio=15k. DSP_DoCommand logged EVERY command at NORMAL
  (= SD fsync each), amplifying each iteration into ms. Muted
  __circle__-style (behavior unchanged). If episodes persist without the
  log lines, next suspect is DSP status-spin, not logging.
- 2026-09-25 — Runtime log quiet (staged via wrapper, 16:16 +0200). Guest
  (dosbox-x:) sink drops everything ~8s after boot-complete except error
  markers (E_Exit/TERMINATE/ERROR/failed); DOS boot lines (XMS/COMMAND/
  file opens) still logged inside the window. Glue sound heartbeat +
  perf snap gated behind DOS_RUNTIME_TRACE (0). Menu/bridge action
  lines kept (rare, user-initiated). Re-enable: flip the macro (glue) or
  comment the quiet call in GFX_Events (guest).
