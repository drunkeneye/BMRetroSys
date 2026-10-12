#!/usr/bin/env python3
"""Download missing firmware ROMs for BMX staging (stdlib only).

VICE ROMs come from https://www.zimmers.net/anonftp/pub/cbm/firmware/
(crawled + name-normalized, so layout changes don't break the mapping),
except the SuperCPU scpu64 binary, which is intentionally never fetched.
Atari ROMs come from https://github.com/ascrnet/FW-Altirra (Automatic/).
Amiga Kickstarts come from the Amiga Demobase archive on archive.org
(canonical BMX names); genuine TOS images from the TOSEC collection on
archive.org (unzipped, canonical pool names). The archive.org sources are
copyrighted ROMs redistributed there; fetching is opt-out via
--no-download-roms (default: on, like the rest).

Layout (repo-relative):
   roms/<machine-lower>/...   persistent cache (c64, c128, vic20, plus4,
                               pet, drives, scpu64, a800, amiga, tos, cpc, msx,
                               mac68k, mac128k, apple2, bbc)
  third_party/vice-3.10/data/<MACHINE>/...
                             build/stage input (populated by copying new
                             VICE downloads out of the cache)
  roms/a800/...              staged 1:1 to SD /roms/ATARI800 (atari800
                             matches by CRC32/size, so upstream names kept)
  roms/amiga/kick*.A*        staged 1:1 to SD /roms/AMIGA
  roms/tos/tos*.img          staged 1:1 to SD /roms/ATARIST
  roms/msx/msx.rom           staged 1:1 to SD /roms/MSX (user-supplied
                             MSX1 BIOS+BASIC 32K; see fetch_msx)

Files already present (size > 0) are never re-downloaded. New downloads
are length-checked when the server reports one. Failures warn loudly but
never fail the build: staging still emits MISSING-ROMS.txt as before.
"""
import html.parser
import json
import os
import re
import socket
import sys
import time
import urllib.parse
import urllib.request

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CACHE = os.path.join(REPO, "roms")
VICE_DATA = os.path.join(REPO, "third_party", "vice-3.10", "data")

ZIMMERS = "https://www.zimmers.net/anonftp/pub/cbm/firmware/"
FW_API = ("https://api.github.com/repos/ascrnet/FW-Altirra/"
          "contents/Automatic")
FW_RAW = ("https://raw.githubusercontent.com/ascrnet/FW-Altirra/"
          "main/Automatic/")

# Amiga Kickstarts from the Amiga Demobase archive on archive.org
# (copyrighted Commodore ROMs, same opt-in stance as the pools above:
# fetched by default, skipped with --no-download-roms).
# (canonical BMX name, in-zip path, expected size). Verified 2026-09-27:
# sizes match the kernel's Kickstart check and headers show genuine
# 68000 reset vectors. KS 3.1 A600 (kick40063.A600) is NOT in that
# archive under a stable name -- stays user-supplied (AROS covers boot).
AMIGA_BASE = ("https://archive.org/download/Amiga_Demobase_Version_3/"
              "Amiga_Demobase_Version_3.zip/")
AMIGA_KICKS = [
    ("kick34005.A500", "Kickstarts Winuae/KS ROM v1.3 "
     "(A500,A1000,A2000) rev 34.5 (256k).rom", 262144),
    ("kick37175.A500", "Kickstarts Winuae/KS ROM v2.04 "
     "(A500+) rev 37.175 (512k).rom", 524288),
    ("kick40068.A1200", "Kickstarts Winuae/KS ROM v3.1 "
     "(A1200) rev 40.68 (512k).rom", 524288),
]

# Genuine TOS images from the TOSEC collection on archive.org (copyrighted
# Atari ROMs, same opt-in stance as above). Each entry is a one-file .zip;
# the inner .img is renamed to the canonical pool name. Verified
# 2026-09-27: zips contain a single 256KB .img. (canonical pool name,
# in-zip base path, zip filename).
TOS_BASE = ("https://archive.org/download/Atari_ST_TOSEC_2012_04_23/"
            "Atari_ST_TOSEC_2012_04_23.zip/"
            "Atari ST [TOSEC]/TOS/Atari ST - TOS - [IMG] "
            "(TOSEC-v2009-09-01_CM)/")
TOS_IMAGES = [
    ("tos106uk.img",
     "TOS v1.06 (1989)(Atari Corp)(STE)(GB)[STE TOS, Rev 1].zip"),
    ("tos162uk.img",
     "TOS v1.62 (1990)(Atari Corp)(STE)(GB)[STE TOS, Rev 2].zip"),
    ("tos206uk.img",
     "TOS v2.06 (1991)(Atari Corp)(Mega-STE)(GB).zip"),
]
TOS_IMG_SIZE = 262144

# Amstrad CPC firmware parts from TomHarte/CLK on GitHub (Amstrad
# permitted redistribution of CPC ROMs for emulation; stable pinned
# source, verified 2026-09-29 via the GitHub contents API: all parts
# 16K, matching the kernel's split-ROM layout os+basic[+amsdos]).
CPC_BASE = ("https://raw.githubusercontent.com/TomHarte/CLK/master/"
            "ROMImages/AmstradCPC/")
CPC_PARTS = [
    "os464.rom",
    "basic464.rom",
    "os664.rom",
    "basic664.rom",
    "os6128.rom",
    "basic6128.rom",
    "amsdos.rom",
]
CPC_PART_SIZE = 16384

# MSX system ROM: no stable direct-download source is wired (OpenMSX BIOS
# sets move; C-BIOS SourceForge layout is JS-driven). Place a 32K MSX1
# BIOS+BASIC image at roms/msx/msx.rom by hand (or pass --only=msx to see
# the instructions); staging carries it 1:1 to /roms/MSX/.
MSX_ROM_NAME = "msx.rom"
MSX_ROM_SIZE = 32768

# Macintosh 68k ROMs: one per emulated model (see src/mac68k/mac68k_config
# ModelEntry), fetched as individual files out of archive.org zips.
# Primary pool is the "macroms" item's Mac_ROMs.zip (organized 68k
# collection, verified 2026-10-05); the standalone Quadra 800 ROM is the
# fallback. mac_rom.bin is the boot default (copy of the IIci ROM, the
# default model); the in-menu ROM selector offers every .rom in the pool.
# Size check is a flexible 64KiB-2MiB range (64K/128K/256K/512K/1M all
# pass CheckROM); every failure path just records MISSING (never fatal).
# NOTE: archive.org items come and go (takedowns); 503/404 here simply
# means manual provisioning — see sdcard/mac68k/ROMS.txt.
_MACROM_ZIP = "https://archive.org/download/macroms/Mac_ROMs.zip/"
MAC68K_SOURCES = (
    (_MACROM_ZIP + "Mac_ROMs%2F68k%2F512k%2FMacintosh%20IIci%2F1989-09%20-%20368CADFE%20-%20Mac%20IIci.ROM",
     "mac_iici.rom"),
    (_MACROM_ZIP + "Mac_ROMs%2F68k%2F512k%2FMacintosh%20Classic%20II%2F1991-10%20-%203193670E%20-%20Classic%20II.ROM",
     "mac_classic2.rom"),
    (_MACROM_ZIP + "Mac_ROMs%2F68k%2F1M%2FMacintosh%20Quadra%20605-LC%20475-575-Performa%20475-575%2F1993-10%20-%20FF7439EE%20-%20LC475%2C575%2CQuadra%20605%2CPerforma%20475%2C476%2C575%2C577%2C578.ROM",
     "mac_q605.rom"),
    (_MACROM_ZIP + "Mac_ROMs%2F68k%2F1M%2F1993-02%20-%20F1A6F343%20-%20Quadra%2C%20Centris%20610%2C650.ROM",
     "mac_q650.rom"),
    ("https://archive.org/download/800_20250604/800.ROM",
     "mac_q800.rom"),
)
MAC68K_ROM_MIN_SIZE = 65536
MAC68K_ROM_MAX_SIZE = 2097152
MAC68K_DEFAULT_ROM = "mac_rom.bin"
MAC68K_DEFAULT_SOURCE = "mac_iici.rom"

# Macintosh 128K ROM: 64KB only (Mini vMac -m 128K checksums 28BA61CE /
# 28BA4E50). Same macroms collection as the 68k pool.
MAC128K_SOURCES = (
    (_MACROM_ZIP + "Mac_ROMs%2F68k%2F64k%2FMacintosh%20128K%2F1984-01%20-%2028BA61CE%20-%20Macintosh%20128K.ROM",
     "Mac128K.ROM"),
)
MAC128K_ROM_SIZE = 65536

# Apple II system ROMs, assembled out of MAME dumps on archive.org
# (copyrighted Apple ROMs, same opt-out stance as the pools above:
# fetched by default, skipped with --no-download-roms). Pool names are
# the KEGS conventions the kernel boots (see sdcard/apple2/ROMS.txt).
# Verified 2026-10-09: per-file CRC32s match the MAME driver sources
# (apple2gs.cpp, apple2e.cpp, a2diskiing.cpp) and all three machines
# (IIgs ROM 01/ROM 3, //e) boot to their startup screens.
_MAME221 = "https://archive.org/download/mame-0.221-roms-merged/"
_MAME221_GS = _MAME221 + "apple2gs.zip/"
_MAME221_E = _MAME221 + "apple2e.zip/"
# Disk II P5 PROM lives in the non-merged device set (renamed a2diskiing
# in newer MAME; not present in the merged collection).
_MAME_P5 = ("https://archive.org/download/mame-roms-non-merged/"
            "MAME%20ROMs%20%28non-merged%29/a2diskiing.zip/341-0027-a.p5")
# (pool name, url, size, crc32 or None). Parts are assembled below, never
# staged raw (except the PROM spare).
APPLE2_SOURCES = (
    ("ROM.01", _MAME221_GS + "apple2gsr1%2F342-0077-b",
     131072, 0x42F124B0),
    ("341-0728", _MAME221_GS + "341-0728", 131072, 0x8D410067),
    ("341-0748", _MAME221_GS + "341-0748", 131072, 0x18190283),
    ("342-0135-b.64", _MAME221_E + "342-0135-b.64", 8192, 0xE248835E),
    ("342-0134-a.64", _MAME221_E + "342-0134-a.64", 8192, 0xFC3D59D8),
    ("diskII.prom", _MAME_P5, 256, 0xCE7144F6),
)

# BBC Micro system ROMs: MOS + BASIC out of the MAME Model B dumps, ADFS
# out of the MAME Model B+ dump, and the 1770 DFS out of the MAME
# software-list ROMs, all on archive.org (copyrighted Acorn ROMs, same
# opt-out stance as the pools above: fetched by default, skipped with
# --no-download-roms). Pool names are the CLK conventions the kernel
# boots (see sdcard/bbc/ROMS.txt). Verified 2026-10-11: per-file CRC32s
# match the MAME driver sources (bbcb.cpp: os12.rom, basic2.rom;
# bbcbp.cpp: adfs130.rom) and the software list (hash/bbc_rom.xml:
# dfs226 -> Acorn-DFS-2.26.rom, adfs130 -> Acorn-ADFS-1.30.rom), which in
# turn match the CLK catalogue (Machines/Utility/ROMCatalogue.cpp:
# BBCMicroMOS12, AcornBASICII, BBCMicro1770DFS226, BBCMicroADFS130).
_MAME221_BBCB = _MAME221 + "bbcb.zip/"
_MAME221_BBCBP = _MAME221 + "bbcbp.zip/"
# The 1770 DFS ships only in the software-list ROMs (merged set, member
# bbc_rom/<software>.zip); that inner zip is itself merged (clones
# inside), so the exact member is picked by name with zipfile (stdlib),
# like the TOS pool below.
_MAME228_SL = ("https://archive.org/download/"
               "MAME_0.228_Software_List_ROMs_merged/"
               "MAME_0.228_Software_List_ROMs_merged.zip/bbc_rom%2F")
# (pool name, url, size, crc32, in-zip member or None). Plain files land
# 1:1 under the pool name; zipped entries are extracted by member name
# and stored under the pool name. adfs130.rom is optional (the kernel
# boots DFS-only); the other three are required.
BBC_SOURCES = (
    ("os12.rom", _MAME221_BBCB + "os12.rom", 16384, 0x3c14fc70, None),
    ("basic2.rom", _MAME221_BBCB + "basic2.rom", 16384, 0x79434781, None),
    ("dfs226.rom", _MAME228_SL + "dfs226.zip", 16384, 0x5ae33e94,
     "Acorn-DFS-2.26.rom"),
    ("adfs130.rom", _MAME221_BBCBP + "adfs130.rom", 16384, 0xd3855588,
     None),
)

# (cache dir, vice-data dir or None, filename as staging needs it).
# Mirrors the features=["roms"] entries in sd-layout.toml.
VICE_ROMS = [
    ("c64", "C64", "basic-901226-01.bin"),
    ("c64", "C64", "kernal-901227-03.bin"),
    ("c64", "C64", "chargen-901225-01.bin"),
    ("c128", "C128", "basiclo-318018-04.bin"),
    ("c128", "C128", "basichi-318019-04.bin"),
    ("c128", "C128", "basic64-901226-01.bin"),
    ("c128", "C128", "kernal64-901227-03.bin"),
    ("c128", "C128", "kernal-318020-05.bin"),
    ("c128", "C128", "chargen-390059-01.bin"),
    ("vic20", "VIC20", "basic-901486-01.bin"),
    ("vic20", "VIC20", "chargen-901460-03.bin"),
    ("vic20", "VIC20", "kernal.901486-06.bin"),
    ("vic20", "VIC20", "kernal.901486-07.bin"),
    ("plus4", "PLUS4", "basic-318006-01.bin"),
    ("plus4", "PLUS4", "3plus1-317053-01.bin"),
    ("plus4", "PLUS4", "3plus1-317054-01.bin"),
    ("plus4", "PLUS4", "kernal-318004-05.bin"),
    ("plus4", "PLUS4", "kernal-318005-05.bin"),
    ("pet", "PET", "basic-4.901465-23-20-21.bin"),
    ("pet", "PET", "characters-2.901447-10.bin"),
    ("pet", "PET", "kernal-4.901465-22.bin"),
    ("pet", "PET", "edit-4-40-n-50Hz.901498-01.bin"),
    ("pet", "PET", "edit-4-80-b-50Hz.901474-04_.bin"),
    ("drives", "DRIVES", "dos1001-901887+8-01.bin"),
    ("drives", "DRIVES", "dos1540-325302+3-01.bin"),
    ("drives", "DRIVES", "dos1541-325302-01+901229-05.bin"),
    ("drives", "DRIVES", "dos1541ii-251968-03.bin"),
    ("drives", "DRIVES", "dos1551-318008-01.bin"),
    ("drives", "DRIVES", "dos1570-315090-01.bin"),
    ("drives", "DRIVES", "dos1571-310654-05.bin"),
    ("drives", "DRIVES", "dos1571cr-318047-01.bin"),
    ("drives", "DRIVES", "dos1581-318045-02.bin"),
    ("drives", "DRIVES", "dos2031-901484-03+05.bin"),
    ("drives", "DRIVES", "dos2040-901468-06+07.bin"),
    ("drives", "DRIVES", "dos3040-901468-11-13.bin"),
    ("drives", "DRIVES", "dos4040-901468-14-16.bin"),
    ("drives", "DRIVES", "dos9000-300516+7-revC.bin"),
    ("scpu64", "SCPU64", "chargen-901225-01.bin"),
    # NOTE: roms/SCPU64/scpu64 (the SuperCPU binary) is deliberately
    # absent: not downloadable from zimmers, stays in MISSING-ROMS.txt.
]

UA = {"User-Agent": "BMRetroSys-rom-fetch/1.0 (staging helper)"}

# Expected uncompressed sizes, taken from the known-good local set. Every
# download (single, alias or assembled) must match, otherwise it is dropped.
# Same MOS part number + same size = same ROM.
EXPECTED_SIZES = {
    "basic-901226-01.bin": 8192,
    "kernal-901227-03.bin": 8192,
    "chargen-901225-01.bin": 4096,
    "basiclo-318018-04.bin": 16384,
    "basichi-318019-04.bin": 16384,
    "basic64-901226-01.bin": 8192,
    "kernal64-901227-03.bin": 8192,
    "kernal-318020-05.bin": 16384,
    "chargen-390059-01.bin": 8192,
    "basic-901486-01.bin": 8192,
    "chargen-901460-03.bin": 4096,
    "kernal.901486-06.bin": 8192,
    "kernal.901486-07.bin": 8192,
    "basic-318006-01.bin": 16384,
    "kernal-318004-05.bin": 16384,
    "kernal-318005-05.bin": 16384,
    "basic-4.901465-23-20-21.bin": 12288,
    "characters-2.901447-10.bin": 2048,
    "kernal-4.901465-22.bin": 4096,
    "edit-4-40-n-50Hz.901498-01.bin": 2048,
    "edit-4-80-b-50Hz.901474-04_.bin": 2048,
    "dos1540-325302+3-01.bin": 16384,
    "dos1541-325302-01+901229-05.bin": 16384,
    "dos1541ii-251968-03.bin": 16384,
    "dos1551-318008-01.bin": 16384,
    "dos1570-315090-01.bin": 32768,
    "dos1571-310654-05.bin": 32768,
    "dos1571cr-318047-01.bin": 32768,
    "dos1581-318045-02.bin": 32768,
}

# Required name -> upstream basename. Only same-MOS-part renames
# (chargen/characters, basic64, kernal64, dos-prefix, -rom infix).
ALIASES = {
    "chargen-901225-01.bin": "characters.901225-01.bin",
    "basic64-901226-01.bin": "basic.901226-01.bin",
    "kernal64-901227-03.bin": "kernal.901227-03.bin",
    "basiclo-318018-04.bin": "basic-4000.318018-04.bin",
    "basichi-318019-04.bin": "basic-8000.318019-04.bin",
    "kernal-318020-05.bin": "kernal.318020-05.bin",
    "chargen-390059-01.bin": "characters.390059-01.bin",
    "chargen-901460-03.bin": "characters.901460-03.bin",
    "kernal-318004-05.bin": "kernal.318004-05.bin",
    "kernal-318005-05.bin": "kernal.318005-05.bin",
    "basic-318006-01.bin": "basic.318006-01.bin",
    "dos1541ii-251968-03.bin": "1541-II.251968-03.bin",
    "dos1551-318008-01.bin": "1551.318008-01.bin",
    "dos1570-315090-01.bin": "1570-rom.315090-01.bin",
    "dos1571-310654-05.bin": "1571-rom.310654-05.bin",
    "dos1571cr-318047-01.bin": "1571cr-rom.318047-01.bin",
    "dos1581-318045-02.bin": "1581-rom.318045-02.bin",
}

# Required name -> ordered part basenames, concatenated. Only ascending
# address order with exact part numbers (no guessing across revisions).
ASSEMBLIES = {
    "basic-4.901465-23-20-21.bin": [
        "basic-4-b000.901465-23.bin",
        "basic-4-c000.901465-20.bin",
        "basic-4-d000.901465-21.bin",
    ],
    "dos1540-325302+3-01.bin": [
        "1540-c000.325302-01.bin",
        "1540-e000.325303-01.bin",
    ],
    "dos1541-325302-01+901229-05.bin": [
        "1541-c000.325302-01.bin",
        "1541-e000.901229-05.bin",
    ],
}
# Deliberately NOT covered (ambiguous or incomplete upstream, keep local):
# 3plus1-*, edit-4-80-b-*, dos1001, dos2031, dos2040, dos3040, dos4040,
# dos9000, and the scpu64 binary (policy exclusion).


def fetch(url, timeout=60):
    """GET with retries for transient network/DNS blips (home-router DNS
    forwarders hand out EAI_AGAIN under load; the Pi here resolves via one).
    Retries URLError/timeout only; HTTP errors (404 etc.) fail at once."""
    req = urllib.request.Request(url, headers=UA)
    last = None
    for attempt, wait in ((1, 0), (2, 2), (3, 5)):
        if wait:
            time.sleep(wait)
        try:
            return urllib.request.urlopen(req, timeout=timeout)
        except urllib.error.HTTPError:
            raise
        except Exception as e:
            last = e
            print("  net try %d %s (%s)" % (attempt, url.split("?")[0], e))
    raise last


def dns_preflight():
    """Resolve both ROM hosts up front. Returns True if usable; prints a
    pinpointed diagnostic otherwise (this is what an offline run looks
    like, instead of 40 identical tracebacks)."""
    old_timeout = socket.getdefaulttimeout()
    socket.setdefaulttimeout(10)
    try:
        ok = True
        for host in ("www.zimmers.net", "api.github.com",
                     "archive.org"):
            try:
                socket.getaddrinfo(host, 443)
                print("  dns ok: %s" % host)
            except Exception as e:
                print("  DNS FAIL: %s (%s)" % (host, e))
                print("    check link (/etc/resolv.conf, router, WiFi)"
                      " and retry")
                ok = False
        return ok
    finally:
        socket.setdefaulttimeout(old_timeout)


def norm(name):
    """Normalize ROM filenames across naming styles (dots vs dashes)."""
    base = name.rsplit("/", 1)[-1].lower()
    return "".join(c for c in base if c not in "._- ")


class LinkParser(html.parser.HTMLParser):
    def __init__(self):
        super().__init__()
        self.links = []

    def handle_starttag(self, tag, attrs):
        if tag == "a":
            for k, v in attrs:
                if k == "href" and v and not v.startswith(
                        ("?", "#", "mailto:")):
                    self.links.append(v)


def crawl_zimmers():
    """Return {normalized-name: url} for firmware files.

    Single fetch of the ALLFILES.html index (full paths included);
    falls back to a bounded recursive crawl if the index is missing.
    """
    try:
        with fetch(ZIMMERS + "ALLFILES.html", timeout=60) as r:
            body = r.read().decode("utf-8", "replace")
        found = {}
        for m in re.finditer(r'href="([^"]+)"', body):
            href = m.group(1)
            if href.startswith(("?", "#", "mailto:", "../")):
                continue
            if href.endswith("/"):
                continue
            low = href.lower()
            if not low.endswith((".bin", ".rom")):
                continue
            full = (href if "://" in href
                    else urllib.parse.urljoin(ZIMMERS, href))
            if not full.startswith("https://www.zimmers.net/"):
                continue
            key = norm(full)
            if key not in found:
                found[key] = full
        if found:
            return found
    except Exception as e:
        print("  ALLFILES.html unavailable (%s), crawling" % e)
    return crawl_zimmers_recursive()


def crawl_zimmers_recursive():
    """Bounded recursive crawl (fallback only)."""
    seen_dirs = set()
    found = {}
    stack = [(ZIMMERS, 0)]
    pages = 0
    while stack and pages < 600:
        url, depth = stack.pop()
        if url in seen_dirs or depth > 4:
            continue
        seen_dirs.add(url)
        try:
            with fetch(url, timeout=30) as r:
                if "html" not in r.headers.get("Content-Type", ""):
                    continue
                body = r.read().decode("utf-8", "replace")
        except Exception as e:
            print("  crawl skip %s (%s)" % (url, e))
            continue
        pages += 1
        p = LinkParser()
        try:
            p.feed(body)
        except Exception:
            continue
        for href in p.links:
            if href in ("../", "/"):
                continue
            full = (href if "://" in href
                    else urllib.parse.urljoin(url, href))
            if not full.startswith(ZIMMERS):
                continue  # stay inside the firmware tree
            low = full.lower()
            # Directory pages link as subdir/index.html (no trailing slash).
            if href.endswith("/") or low.endswith("index.html") or (
                    "." not in href.rsplit("/", 1)[-1]):
                stack.append((full, depth + 1))
            elif low.endswith((".bin", ".rom", ".rom1", ".rom2")):
                key = norm(full)
                if key not in found:
                    found[key] = full
    return found


def fetch_bytes(url):
    """Return body bytes, or None on any failure/length mismatch."""
    # GitHub's API sometimes returns download_urls with raw spaces; some
    # hosts need '+' and parens escaped too. Unquote first (idempotent for
    # already-encoded URLs), then quote the path.
    url = urllib.parse.quote(urllib.parse.unquote(url), safe=":/")
    try:
        with fetch(url) as r:
            expected = r.headers.get("Content-Length")
            data = r.read()
        if expected is not None and len(data) != int(expected):
            print("  SHORT %s (%d/%s bytes)" % (url, len(data), expected))
            return None
        if not data:
            print("  EMPTY %s" % url)
            return None
        return data
    except Exception as e:
        print("  FAIL %s (%s)" % (url, e))
        return None


def present(path):
    """True if path exists with nonzero size (never re-download those)."""
    try:
        return os.path.getsize(path) > 0
    except OSError:
        return False


def write_file(dest, data):
    tmp = dest + ".part"
    try:
        with open(tmp, "wb") as f:
            f.write(data)
        os.replace(tmp, dest)
        return True
    except OSError as e:
        print("  WRITE-FAIL %s (%s)" % (dest, e))
        try:
            os.remove(tmp)
        except OSError:
            pass
        return False


def resolve_vice(name, index, by_base):
    """Return file bytes for a required ROM, or None.

    Order: exact normalized match, curated alias (same MOS part),
    mechanical assembly (ascending addresses). Every result must match
    EXPECTED_SIZES.
    """
    want = EXPECTED_SIZES.get(name)
    if index is not None:
        url = index.get(norm(name))
        if url is not None:
            data = fetch_bytes(url)
            if data is not None and (want is None or len(data) == want):
                return data
            print("  SIZE-MISMATCH %s" % name)
            return None
    if index is not None and name in ALIASES:
        url = by_base.get(ALIASES[name].lower())
        if url is not None:
            data = fetch_bytes(url)
            if data is not None and (want is None or len(data) == want):
                return data
            print("  SIZE-MISMATCH %s" % name)
            return None
    if index is not None and name in ASSEMBLIES:
        parts = []
        for base in ASSEMBLIES[name]:
            url = by_base.get(base.lower())
            if url is None:
                return None
            data = fetch_bytes(url)
            if data is None:
                return None
            parts.append(data)
        blob = b"".join(parts)
        if want is not None and len(blob) != want:
            print("  SIZE-MISMATCH %s" % name)
            return None
        return blob
    return None


def fetch_vice(index):
    by_base = {}
    if index is not None:
        for key, url in index.items():
            by_base.setdefault(url.rsplit("/", 1)[-1].lower(), url)
    got, skipped, failed = 0, 0, []
    for cache_dir, data_dir, name in VICE_ROMS:
        cache_path = os.path.join(CACHE, cache_dir, name)
        data_path = os.path.join(VICE_DATA, data_dir, name)
        if present(data_path):
            if not present(cache_path):
                # Mirror into the cache so roms/ reflects reality.
                try:
                    os.makedirs(os.path.dirname(cache_path), exist_ok=True)
                    with open(data_path, "rb") as f:
                        blob = f.read()
                    with open(cache_path, "wb") as f:
                        f.write(blob)
                except OSError as e:
                    print("  cache mirror failed %s (%s)" % (name, e))
            skipped += 1
            continue
        if present(cache_path):
            # Cache has it (e.g. manual drop): promote into build input.
            try:
                os.makedirs(os.path.dirname(data_path), exist_ok=True)
                with open(cache_path, "rb") as f:
                    blob = f.read()
                with open(data_path, "wb") as f:
                    f.write(blob)
                print("  promote %s" % name)
                got += 1
            except OSError as e:
                print("  promote failed %s (%s)" % (name, e))
                failed.append(name)
            continue
        if index is None:
            failed.append(name)
            continue
        print("  get %s" % name)
        data = resolve_vice(name, index, by_base)
        if data is None:
            print("  NOT-FOUND-UPSTREAM %s (keeping local/MISSING)" % name)
            failed.append(name)
            continue
        try:
            os.makedirs(os.path.dirname(cache_path), exist_ok=True)
            os.makedirs(os.path.dirname(data_path), exist_ok=True)
            if not write_file(cache_path, data):
                failed.append(name)
                continue
            with open(cache_path, "rb") as f:
                blob = f.read()
            if not write_file(data_path, blob):
                failed.append(name)
                continue
            got += 1
        except OSError as e:
            print("  store failed %s (%s)" % (name, e))
            failed.append(name)
    return got, skipped, failed


def fw_file_list():
    try:
        with fetch(FW_API) as r:
            items = json.load(r)
        out = []
        for it in items:
            if it.get("type") == "file" and it.get("name"):
                out.append((it["name"], it.get("download_url", "")))
        return out
    except Exception as e:
        print("  FW-Altirra list failed (%s)" % e)
        return None


def fetch_atari(files):
    dest_dir = os.path.join(CACHE, "a800")
    os.makedirs(dest_dir, exist_ok=True)
    got, skipped, failed = 0, 0, []
    for name, url in files:
        dest = os.path.join(dest_dir, name)
        if present(dest):
            skipped += 1
            continue
        if not url:
            failed.append(name)
            continue
        print("  get %s" % name)
        data = fetch_bytes(url)
        if data is not None and write_file(dest, data):
            got += 1
        else:
            failed.append(name)
    return got, skipped, failed


def fetch_amiga():
    """Kickstarts from archive.org into roms/amiga/ (canonical names)."""
    dest_dir = os.path.join(CACHE, "amiga")
    os.makedirs(dest_dir, exist_ok=True)
    got, skipped, failed = 0, 0, []
    for name, inner, size in AMIGA_KICKS:
        dest = os.path.join(dest_dir, name)
        if present(dest):
            skipped += 1
            continue
        # fetch_bytes() normalizes encoding (spaces/parens) itself.
        url = AMIGA_BASE + inner
        print("  get %s" % name)
        data = fetch_bytes(url)
        if data is None or len(data) != size:
            print("  SIZE-MISMATCH %s" % name)
            failed.append(name)
            continue
        if write_file(dest, data):
            got += 1
        else:
            failed.append(name)
    return got, skipped, failed


def fetch_tos():
    """Genuine TOS images from archive.org into roms/tos/.

    Each source is a one-file .zip; the inner .img is extracted with
    zipfile (stdlib) and stored under the canonical pool name. The pool
    doubles as the future TOS-selector source (atarist ROM menu)."""
    import zipfile
    import io
    dest_dir = os.path.join(CACHE, "tos")
    os.makedirs(dest_dir, exist_ok=True)
    got, skipped, failed = 0, 0, []
    for name, zip_name in TOS_IMAGES:
        dest = os.path.join(dest_dir, name)
        if present(dest):
            skipped += 1
            continue
        url = TOS_BASE + zip_name
        print("  get %s" % name)
        data = fetch_bytes(url)
        if data is None:
            failed.append(name)
            continue
        try:
            zf = zipfile.ZipFile(io.BytesIO(data))
            imgs = [i for i in zf.infolist()
                    if i.filename.lower().endswith(".img") and
                    i.file_size == TOS_IMG_SIZE and not i.is_dir()]
            if len(imgs) != 1:
                print("  TOS zip layout unexpected %s (%d imgs)" %
                      (name, len(imgs)))
                failed.append(name)
                continue
            blob = zf.read(imgs[0].filename)
        except Exception as e:
            print("  unzip failed %s (%s)" % (name, e))
            failed.append(name)
            continue
        if write_file(dest, blob):
            got += 1
        else:
            failed.append(name)
    return got, skipped, failed


def fetch_cpc():
    """CPC firmware parts from TomHarte/CLK into roms/cpc/ (split names)."""
    dest_dir = os.path.join(CACHE, "cpc")
    os.makedirs(dest_dir, exist_ok=True)
    got, skipped, failed = 0, 0, []
    for name in CPC_PARTS:
        dest = os.path.join(dest_dir, name)
        if present(dest):
            skipped += 1
            continue
        url = CPC_BASE + name
        print("  get %s" % name)
        data = fetch_bytes(url)
        if data is None or len(data) != CPC_PART_SIZE:
            print("  SIZE-MISMATCH %s" % name)
            failed.append(name)
            continue
        if write_file(dest, data):
            got += 1
        else:
            failed.append(name)
    return got, skipped, failed


def fetch_msx():
    """MSX system ROM: ensure cache dir, report status (manual supply)."""
    dest_dir = os.path.join(CACHE, "msx")
    os.makedirs(dest_dir, exist_ok=True)
    dest = os.path.join(dest_dir, MSX_ROM_NAME)
    if present(dest):
        if os.path.getsize(dest) == MSX_ROM_SIZE:
            return 0, 1, []
        print("  SIZE-MISMATCH %s (want %d)" % (MSX_ROM_NAME, MSX_ROM_SIZE))
        return 0, 0, [MSX_ROM_NAME]
    print("  msx: place a 32K MSX1 BIOS+BASIC image at roms/msx/msx.rom")
    print("  (e.g. Philips VG-8020 from an OpenMSX BIOS set, or C-BIOS")
    print("  cbios_main_msx1.rom renamed to msx.rom)")
    return 0, 0, [MSX_ROM_NAME]


def fetch_mac68k():
    """Mac ROM into roms/mac68k/ (best-effort archive.org sources).

    Tries each (url, dest-name) pair in MAC68K_SOURCES in order, accepting
    any 512KiB-1MiB image (32-bit-clean Mac II/Quadra ROMs vary). Any
    failure just records MISSING so staging lists it in MISSING-ROMS.txt;
    this never fails the build."""
    dest_dir = os.path.join(CACHE, "mac68k")
    os.makedirs(dest_dir, exist_ok=True)
    got, skipped, failed = 0, 0, []
    for url, name in MAC68K_SOURCES:
        dest = os.path.join(dest_dir, name)
        if present(dest):
            size = os.path.getsize(dest)
            if MAC68K_ROM_MIN_SIZE <= size <= MAC68K_ROM_MAX_SIZE:
                skipped += 1
                continue
            print("  SIZE-MISMATCH %s (want 512KiB-1MiB)" % name)
            failed.append(name)
            continue
        print("  get %s" % url)
        try:
            data = fetch_bytes(url)
        except Exception as e:
            print("  fetch failed %s (%s)" % (name, e))
            failed.append(name)
            continue
        if data is None or not (MAC68K_ROM_MIN_SIZE <= len(data)
                                <= MAC68K_ROM_MAX_SIZE):
            print("  MISSING %s (place a 32-bit-clean Mac II/Quadra ROM, "
                  "512KiB-1MiB, at roms/mac68k/mac_rom.bin)" % name)
            failed.append(name)
            continue
        if write_file(dest, data):
            got += 1
        else:
            failed.append(name)
    # Boot default: mac_rom.bin is a copy of the default-model ROM (IIci)
    # so the kernel boots deterministically regardless of readdir order.
    default_dest = os.path.join(dest_dir, MAC68K_DEFAULT_ROM)
    default_src = os.path.join(dest_dir, MAC68K_DEFAULT_SOURCE)
    if not present(default_dest) and present(default_src):
        try:
            with open(default_src, "rb") as fsrc, open(default_dest,
                                                       "wb") as fdst:
                fdst.write(fsrc.read())
            print("  default %s <- %s" % (MAC68K_DEFAULT_ROM,
                                          MAC68K_DEFAULT_SOURCE))
            got += 1
        except Exception as e:
            print("  default copy failed (%s)" % e)
            failed.append(MAC68K_DEFAULT_ROM)
    return got, skipped, failed


def fetch_apple2():
    """Apple II system ROMs into roms/apple2/ (MAME dumps, assembled).

    ROM.01 loads directly; ROM.03 is assembled from 341-0728 + 341-0748
    per upstream KEGS doc/README.ROM.files.txt (second 64K of 341-0748,
    then its first 64K); APPLE2E.ROM is zero padding with the Disk II
    PROM at offset $600, then CD (342-0135-b) + EF (342-0134-a) in the
    last 16K. Sizes and CRC32s are checked; failures just record MISSING
    so staging lists them in MISSING-ROMS.txt (never fatal)."""
    import zlib
    dest_dir = os.path.join(CACHE, "apple2")
    os.makedirs(dest_dir, exist_ok=True)
    got, skipped, failed = 0, 0, []
    parts = {}
    for name, url, size, crc in APPLE2_SOURCES:
        dest = os.path.join(dest_dir, name)
        if present(dest):
            data = open(dest, "rb").read()
            if len(data) == size and (crc is None or
                                      zlib.crc32(data) & 0xffffffff
                                      == crc):
                parts[name] = data
                skipped += 1
                continue
            print("  SIZE-MISMATCH %s" % name)
            failed.append(name)
            continue
        print("  get %s" % url)
        try:
            data = fetch_bytes(url)
        except Exception as e:
            print("  fetch failed %s (%s)" % (name, e))
            failed.append(name)
            continue
        if data is None or len(data) != size or (
                crc is not None and zlib.crc32(data) & 0xffffffff != crc):
            print("  MISSING %s (place %s at roms/apple2/%s)" %
                  (name, name, name))
            failed.append(name)
            continue
        parts[name] = data
        if write_file(dest, data):
            got += 1
        else:
            failed.append(name)
    # ROM.03 assembly (Kent's recipe from README.ROM.files.txt).
    rom03 = os.path.join(dest_dir, "ROM.03")
    if not present(rom03) and "341-0728" in parts and "341-0748" in parts:
        f748 = parts["341-0748"]
        blob = parts["341-0728"] + f748[65536:] + f748[:65536]
        if len(blob) == 262144 and write_file(rom03, blob):
            print("  assembled ROM.03 (256K ROM 3)")
            got += 1
        else:
            failed.append("ROM.03")
    elif present(rom03):
        skipped += 1
    else:
        failed.append("ROM.03")
    # Apple //e 32K: pad + Disk II PROM @ $600, then CD + EF last 16K.
    efile = os.path.join(dest_dir, "APPLE2E.ROM")
    if not present(efile) and "342-0135-b.64" in parts and \
            "342-0134-a.64" in parts and "diskII.prom" in parts:
        img = bytearray(32768)
        img[0x600:0x700] = parts["diskII.prom"]
        img[0x4000:0x6000] = parts["342-0135-b.64"]
        img[0x6000:0x8000] = parts["342-0134-a.64"]
        if write_file(efile, bytes(img)):
            print("  assembled APPLE2E.ROM (32K //e)")
            got += 1
        else:
            failed.append("APPLE2E.ROM")
    elif present(efile):
        if os.path.getsize(efile) == 32768:
            skipped += 1
        else:
            failed.append("APPLE2E.ROM")
    else:
        failed.append("APPLE2E.ROM")
    return got, skipped, failed


def fetch_bbc():
    """BBC Micro system ROMs into roms/bbc/ (MAME dumps, renamed).

    os12.rom + basic2.rom come straight out of the bbcb machine zip and
    adfs130.rom out of the bbcbp machine zip; dfs226.rom is extracted
    from the merged dfs226 software-list zip (member Acorn-DFS-2.26.rom)
    and stored under the pool name. Sizes and CRC32s are checked;
    failures just record MISSING so staging lists them in
    MISSING-ROMS.txt (never fatal). adfs130.rom is optional (the kernel
    boots DFS-only); the other three are required."""
    import zlib
    import zipfile
    import io
    dest_dir = os.path.join(CACHE, "bbc")
    os.makedirs(dest_dir, exist_ok=True)
    got, skipped, failed = 0, 0, []
    for name, url, size, crc, inner in BBC_SOURCES:
        dest = os.path.join(dest_dir, name)
        if present(dest):
            data = open(dest, "rb").read()
            if len(data) == size and (crc is None or
                                      zlib.crc32(data) & 0xffffffff
                                      == crc):
                skipped += 1
                continue
            print("  SIZE-MISMATCH %s" % name)
            failed.append(name)
            continue
        print("  get %s" % url)
        try:
            data = fetch_bytes(url)
        except Exception as e:
            print("  fetch failed %s (%s)" % (name, e))
            failed.append(name)
            continue
        if data is None:
            print("  MISSING %s (place %s at roms/bbc/%s)" %
                  (name, name, name))
            failed.append(name)
            continue
        if inner is not None:
            try:
                zf = zipfile.ZipFile(io.BytesIO(data))
                data = zf.read(inner)
            except Exception as e:
                print("  unzip failed %s (%s)" % (name, e))
                failed.append(name)
                continue
        if len(data) != size or (
                crc is not None and zlib.crc32(data) & 0xffffffff != crc):
            print("  MISSING %s (place %s at roms/bbc/%s)" %
                  (name, name, name))
            failed.append(name)
            continue
        if write_file(dest, data):
            got += 1
        else:
            failed.append(name)
    return got, skipped, failed


def fetch_mac128k():
    """Mac 128K ROM into roms/mac128k/ (best-effort archive.org source).

    Accepts exactly 64KiB images (Mini vMac -m 128K). Any failure just
    records MISSING so staging lists it in MISSING-ROMS.txt; this never
    fails the build."""
    dest_dir = os.path.join(CACHE, "mac128k")
    os.makedirs(dest_dir, exist_ok=True)
    got, skipped, failed = 0, 0, []
    for url, name in MAC128K_SOURCES:
        dest = os.path.join(dest_dir, name)
        if present(dest):
            size = os.path.getsize(dest)
            if size == MAC128K_ROM_SIZE:
                skipped += 1
                continue
            print("  SIZE-MISMATCH %s (want 64KiB)" % name)
            failed.append(name)
            continue
        print("  get %s" % url)
        try:
            data = fetch_bytes(url)
        except Exception as e:
            print("  fetch failed %s (%s)" % (name, e))
            failed.append(name)
            continue
        if data is None or len(data) != MAC128K_ROM_SIZE:
            print("  MISSING %s (place a 64KB 128K ROM "
                  "at roms/mac128k/Mac128K.ROM)" % name)
            failed.append(name)
            continue
        if write_file(dest, data):
            got += 1
        else:
            failed.append(name)
    return got, skipped, failed


def main():
    only = set(a[7:] for a in sys.argv[1:] if a.startswith("--only="))
    do_vice = not only or "vice" in only or "c64" in only
    do_atari = not only or "atari" in only or "a800" in only
    do_amiga = not only or "amiga" in only or "kick" in only
    do_tos = not only or "tos" in only or "atarist" in only or "st" in only
    do_cpc = not only or "cpc" in only or "amstrad" in only
    do_msx = not only or "msx" in only
    do_mac68k = not only or "mac68k" in only or "mac" in only
    do_mac128k = not only or "mac128k" in only or "mac" in only
    do_apple2 = not only or "apple2" in only or "apple" in only or "a2" in only
    do_bbc = not only or "bbc" in only or "beeb" in only or "acorn" in only

    # Staging copies repo pools 1:1 (both.tree requires the source to
    # exist, even when empty), so the cache dirs always exist afterwards
    # — even fully offline, when there is nothing to put in them yet.
    for cache_dir, _, _ in VICE_ROMS:
        os.makedirs(os.path.join(CACHE, cache_dir), exist_ok=True)
    os.makedirs(os.path.join(CACHE, "msx"), exist_ok=True)
    os.makedirs(os.path.join(CACHE, "a800"), exist_ok=True)
    os.makedirs(os.path.join(CACHE, "amiga"), exist_ok=True)
    os.makedirs(os.path.join(CACHE, "tos"), exist_ok=True)
    os.makedirs(os.path.join(CACHE, "cpc"), exist_ok=True)
    os.makedirs(os.path.join(CACHE, "mac68k"), exist_ok=True)
    os.makedirs(os.path.join(CACHE, "mac128k"), exist_ok=True)
    os.makedirs(os.path.join(CACHE, "apple2"), exist_ok=True)
    os.makedirs(os.path.join(CACHE, "bbc"), exist_ok=True)

    total_failed = []
    net_ok = dns_preflight()
    if do_vice:
        print("VICE ROMs (zimmers.net, scpu64 binary excluded):")
        if net_ok:
            try:
                index = crawl_zimmers()
                print("  index: %d files" % len(index))
            except Exception as e:
                print("  index failed (%s); only local files apply" % e)
                index = None
        else:
            print("  offline; only local files apply")
            index = None
        got, skipped, failed = fetch_vice(index)
        print("  vice: %d fetched, %d present, %d failed" %
              (got, skipped, len(failed)))
        total_failed += ["vice:" + n for n in failed]
    if do_atari:
        print("Atari ROMs (FW-Altirra Automatic/):")
        files = fw_file_list() if net_ok else None
        if files is None:
            if net_ok:
                print("  atari: list unavailable, skipping")
            else:
                print("  atari: offline, skipping")
        else:
            got, skipped, failed = fetch_atari(files)
            print("  atari: %d fetched, %d present, %d failed" %
                  (got, skipped, len(failed)))
            total_failed += ["a800:" + n for n in failed]
    if do_amiga:
        print("Amiga Kickstarts (archive.org Amiga Demobase):")
        if not net_ok:
            print("  amiga: offline, skipping")
        else:
            got, skipped, failed = fetch_amiga()
            print("  amiga: %d fetched, %d present, %d failed" %
                  (got, skipped, len(failed)))
            total_failed += ["amiga:" + n for n in failed]
    if do_tos:
        print("Atari TOS images (archive.org TOSEC):")
        if not net_ok:
            print("  tos: offline, skipping")
        else:
            got, skipped, failed = fetch_tos()
            print("  tos: %d fetched, %d present, %d failed" %
                  (got, skipped, len(failed)))
            total_failed += ["tos:" + n for n in failed]
    if do_msx:
        print("MSX system ROM (user-supplied, see above):")
        got, skipped, failed = fetch_msx()
        print("  msx: %d fetched, %d present, %d failed" %
              (got, skipped, len(failed)))
        total_failed += ["msx:" + n for n in failed]
    if do_mac68k:
        print("Macintosh 68k ROM (archive.org pool, user-supplied fallback):")
        if not net_ok:
            print("  mac68k: offline, skipping")
        else:
            try:
                got, skipped, failed = fetch_mac68k()
            except Exception as e:
                print("  mac68k: fetch error (%s), marking missing" % e)
                got, skipped, failed = 0, 0, [n for _, n in MAC68K_SOURCES]
            print("  mac68k: %d fetched, %d present, %d failed" %
                  (got, skipped, len(failed)))
            total_failed += ["mac68k:" + n for n in failed]
    if do_mac128k:
        print("Macintosh 128K ROM (archive.org pool, user-supplied fallback):")
        if not net_ok:
            print("  mac128k: offline, skipping")
        else:
            try:
                got, skipped, failed = fetch_mac128k()
            except Exception as e:
                print("  mac128k: fetch error (%s), marking missing" % e)
                got, skipped, failed = 0, 0, [n for _, n in MAC128K_SOURCES]
            print("  mac128k: %d fetched, %d present, %d failed" %
                  (got, skipped, len(failed)))
            total_failed += ["mac128k:" + n for n in failed]
    if do_apple2:
        print("Apple II system ROMs (archive.org MAME dumps, assembled):")
        if not net_ok:
            print("  apple2: offline, skipping")
        else:
            try:
                got, skipped, failed = fetch_apple2()
            except Exception as e:
                print("  apple2: fetch error (%s), marking missing" % e)
                got, skipped, failed = 0, 0, ["ROM.01", "ROM.03",
                                              "APPLE2E.ROM"]
            print("  apple2: %d fetched, %d present, %d failed" %
                  (got, skipped, len(failed)))
            total_failed += ["apple2:" + n for n in failed]
    if do_bbc:
        print("BBC Micro system ROMs (archive.org MAME dumps, renamed):")
        if not net_ok:
            print("  bbc: offline, skipping")
        else:
            try:
                got, skipped, failed = fetch_bbc()
            except Exception as e:
                print("  bbc: fetch error (%s), marking missing" % e)
                got, skipped, failed = 0, 0, ["os12.rom", "basic2.rom",
                                              "dfs226.rom"]
            print("  bbc: %d fetched, %d present, %d failed" %
                  (got, skipped, len(failed)))
            total_failed += ["bbc:" + n for n in failed]
    if do_cpc:
        print("Amstrad CPC firmware (TomHarte/CLK):")
        if not net_ok:
            print("  cpc: offline, skipping")
        else:
            got, skipped, failed = fetch_cpc()
            print("  cpc: %d fetched, %d present, %d failed" %
                  (got, skipped, len(failed)))
            total_failed += ["cpc:" + n for n in failed]
    if total_failed:
        print("STILL MISSING (%d):" % len(total_failed))
        for n in total_failed:
            print("  - %s" % n)
        print("(staging will list these in MISSING-ROMS.txt as before)")
    else:
        print("All ROMs present.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
