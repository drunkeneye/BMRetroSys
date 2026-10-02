#!/usr/bin/env python3
"""Generate src/cpc/cpc_autostub.c: empty stubs + zero globals for
ZEsarUX symbols that are referenced by the compiled subset but defined in
modules BMX does not compile (other machines, desktop UI, network...).

Usage: cpc_gen_autostub.py <undefined-symbols.txt>
Scans third_party/zesarux headers (and uncompiled .c files as fallback)
for declarations and emits matching definitions. Symbols in SKIP (already
provided by glue or vendored separately) are ignored.
"""

import os
import re
import sys

ZX = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                  "../third_party/zesarux")

SKIP = {    # Provided by zx_shim.cpp / zx_timer.cpp / zx_tape.cpp / glue.
    "debug_printf", "debug_printf_source",
    "debug_printf_check_exclude_include", "cpu_panic",
    "debug_fired_interrupt", "debug_anota_retorno_step_nmi",
    "debug_anota_retorno_step_maskable", "util_malloc", "util_malloc_fill",
    "toggle_flash_state", "realjoystick_main", "puerto_especial_joystick",
    "joystick_emulation", "joystick_autofire_frequency",
    "joystick_autofire_counter", "joystick_autoleftright_enabled",
    "joystick_autoleftright_frequency", "joystick_autoleftright_counter",
    "joystick_autoleftright_status", "joystick_barato", "joystick_texto",
    "realtape_inserted", "realtape_playing", "realtape_loading_sound",
    "realtape_get_byte", "get_realtape_last_value", "superupgrade_enabled",
    "superupgrade_puerto_43b", "superupgrade_set_memory_pages",
    "timex_si_modo_8x1", "timex_port_ff", "tsconf_vector_fired_interrupt",
    "pd765_next_event_from_core", "gestionar_autoload_spectrum",
    "snapshot_add_in_ram", "pd765_enable", "find_sharedfile",
    "open_sharedfile", "open_sharedfile_write", "get_file_size",
    "get_rom_size", "tbblue_store_scanlines", "tbblue_store_scanlines_border",
    "tap_load_detect", "tape_trap_load_spectrum", "timer_check_interrupt",
    "timer_pause_waiting_end_frame", "timer_condicion_top_speed",
    "timer_get_elapsed_core_frame_pre", "timer_get_elapsed_core_frame_post",
    "timer_stats_current_time", "timer_stats_diference_time", "init_timer",
    "timer_restart", "timer_reset", "timer_sleep", "timer_sleep_machine",
    "original_timer_sleep_machine", "timer_add_timer_usleep_to_bottom",
    # Hand-written elsewhere (exact replicas or vendored files).
    "util_parity", "zxvision_key_not_sent_emulated_mach", "random_ram",
    "usleep", "interrupcion_fifty_generada",
    "codpred", "codprddfd", "audio_sine_table",
    # Types, not symbols.
    "menu_item",
    # Provided by cpc_shim.cpp.
    "realtape_inserted", "realtape_playing", "realtape_volumen",
    "debug_fired_in", "debug_fired_out",
}

# Symbols the regexes cannot see (comma-separated declarators etc.).
# name -> (kind, type, extra).
MANUAL = {
    "kempston_mouse_y": ("var", "z80_byte", ""),
}

FUNC_RE = re.compile(
    r"extern\s+([\w\s\*]*?)\s*(\w+)\s*\(([^;]*?)\)\s*;", re.S)
FUNC_PTR_RE = re.compile(
    r"extern\s+([\w\s\*]*?)\s*\(\*(\w+)\)\s*\(([^;]*?)\)\s*;", re.S)
VAR_RE = re.compile(
    r"extern\s+([\w\s\*]*?)\s*(\w+)((?:\s*\[[^\]]*\])*)\s*;", re.S)


def clean(t):
    t = re.sub(r"/\*.*?\*/", "", t, flags=re.S)
    t = re.sub(r"//[^\n]*", "", t)
    t = re.sub(r"\s+", " ", t).strip()
    return t


def scan_decls():
    """name -> (kind, type, extra) from headers, fallback .c files."""
    decls = {}
    files = []
    for root, _, fns in os.walk(ZX):
        for fn in fns:
            if fn.endswith(".h"):
                files.append(os.path.join(root, fn))
    files.sort()
    for path in files:
        try:
            with open(path, errors="replace") as f:
                text = f.read()
        except OSError:
            continue
        for m in FUNC_PTR_RE.finditer(text):
            rtype, name, args = clean(m.group(1)), m.group(2), clean(m.group(3))
            if name not in decls and "[" not in rtype:
                decls[name] = ("fptr", rtype, args)
        for m in FUNC_RE.finditer(text):
            rtype, name, args = clean(m.group(1)), m.group(2), clean(m.group(3))
            if name not in decls:
                decls[name] = ("func", rtype, args)
        for m in VAR_RE.finditer(text):
            vtype, name, dims = clean(m.group(1)), m.group(2), m.group(3)
            if name not in decls and "(*" not in vtype:
                decls[name] = ("var", vtype, dims)
    return decls


def main():
    undef = []
    with open(sys.argv[1]) as f:
        for line in f:
            line = line.strip()
            if line and line not in SKIP and line not in undef:
                undef.append(line)
    decls = scan_decls()
    decls.update(MANUAL)
    # Targeted fallback for names declared without `extern` (e.g. m68k.h
    # prototypes) or defined in uncompiled .c files: search for the exact
    # name with a type on its left.
    missing = [n for n in undef if n not in decls]
    if missing:
        allfiles = []
        for root, _, fns in os.walk(ZX):
            for fn in fns:
                if fn.endswith(".h") or fn.endswith(".c"):
                    allfiles.append(os.path.join(root, fn))
        for path in allfiles:
            try:
                with open(path, errors="replace") as f:
                    text = f.read()
            except OSError:
                continue
            for name in missing:
                if name in decls:
                    continue
                # Declaration-like context only (line start or after ;{}):
                # avoids matching call sites and comments.
                m = re.search(
                    r"(?:^|[;{}])\s*([\w][\w\s\*]*?)\s*(\*?)\b" +
                    re.escape(name) +
                    r"\b\s*((?:\s*\[[^\]]*\])*)\s*(\([^;{}]*\))?\s*[;={]",
                    text, re.M)
                if m:
                    vtype = (clean(m.group(1)) + m.group(2)).strip()
                    dims = clean(m.group(3))
                    if m.group(4):
                        args = clean(m.group(4))
                        args = args[1:-1] if len(args) >= 2 else ""
                        decls[name] = ("func", vtype, args)
                    else:
                        decls[name] = ("var", vtype, dims)
    out = []
    out.append("//")
    out.append("// cpc_autostub.c")
    out.append("//")
    out.append("// AUTO-GENERATED by tools/cpc_gen_autostub.py — do not edit.")
    out.append("// Unreachable-on-Spectrum ZEsarUX symbols: empty stubs and")
    out.append("// zero-initialised globals. The core never calls these on the")
    out.append("// Spectrum path (verified: menu/tape/network/other machines).")
    out.append("//")
    out.append("")
    out.append('#include "cpu.h"')
    out.append('#include "settings.h"')
    out.append('#include "screen.h"')
    out.append('#include "audio.h"')
    out.append('#include "ula.h"')
    out.append('#include "mem128.h"')
    out.append('#include "contend.h"')
    out.append('#include "operaciones.h"')
    out.append('#include "joystick.h"')
    out.append('#include "timer.h"')
    out.append('#include "atomic.h"')
    out.append('#include "tape.h"')
    out.append("")
    notfound = []
    for name in undef:
        d = decls.get(name)
        if d is None:
            notfound.append(name)
            continue
        kind, vtype, extra = d
        if kind == "func":
            if vtype == "void":
                out.append(f"{vtype} {name}({extra}) {{}}")
            else:
                out.append(f"{vtype} {name}({extra}) {{ return 0; }}")
        elif kind == "fptr":
            out.append(f"{vtype} (*{name})({extra}) = 0;")
        else:
            dims = extra.strip()
            out.append(f"{vtype} {name}{dims} = {{0}};")
    out.append("")
    with open("src/cpc/cpc_autostub.c", "w") as f:
        f.write("\n".join(out))
    print(f"wrote {len(undef) - len(notfound)} stubs, "
          f"{len(notfound)} missing:")
    for name in notfound:
        print(f"  MISSING: {name}")


if __name__ == "__main__":
    main()
