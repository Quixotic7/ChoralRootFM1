#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Build ChoralRoot FM-1: the app, the update loader and an installable .fwsc package.

  tools/build.py [--release X.Y[-suffix]] [--board fm1|smk37] [--model pro|elite|mke]

The unit is firmware/src/choralroot.c (ChoralRoot's single compilation unit, a fork of Felucca's felucca.c).
Outputs in build/: choralroot.bin (app.bin: the boot stub's 0xEE0 bytes, then the app), choralroot.elf,
choralroot.dis (the app), stub/stub.{bin,elf,dis} (the boot stub, firmware/stub; docs/BOOT-SAFETY.md),
loader/ota.bin (update loader), choralroot.fwsc (package, identity FM-1_920). A release build (--release X.Y) keeps the identity, sets the
version string to "ChoralRoot X.Y" and writes choralroot-X.Y.fwsc and a folder release-X.Y/ with the package,
the app (choralroot-X.Y-app.bin), the single-core package and its app (choralroot-X.Y-1core.fwsc,
choralroot-X.Y-1core-app.bin: CR_SINGLE_CORE=1, docs/DUALCORE.md "Two packages"), SHA256SUMS and the licence files
(ChoralRoot is all-synth, its image holds no samples: no sample attribution). CR_SINGLE_CORE=1 without --release: build/choralroot.fwsc is the single-core package.
After the checks it prints the section sizes against the XIP slot and the RAM / POOL / NOINIT regions.

BOARD=smk37 (or --board smk37) builds ChoralRoot for the M-Vave SMK-37: -DBOARD_SMK37=1 for the app, the loader and
the boot stub, identity SMK-37 Pro_920 (constant, as the FM-1's), the SMK flash layout (fm1pkg_make.py), the SMK
stub's own lock (firmware/stub/stub_smk37.lock), and the package build/choralroot-smk37.fwsc. The default stays the FM-1.
An SMK release names its board in its version, which every reader shows (--release X.Y-smk37, e.g. 0.24-beta2-smk37:
"ChoralRoot 0.24-beta2-smk37"; one without "smk37" is refused, so that an SMK package never reads as the FM-1 release
of the same number), and goes to build/release-smk37/ for the three models together: choralroot-X.Y-smk37.fwsc, its
-app.bin, the -1core pair, the Elite's and the MKE-P37's with -elite / -mke after the version, SHA256SUMS over every
package and app there, SMK37.md and the licences (another version's files there are dropped).
MODEL=elite / mke (or --model) with smk37: the same build under the SMK-37 Elite's or the MKE-P37's model name
(identity SMK-37 Elite_920 / MKE-P37_920, the loader's and the update record's name too; -DSMK_MODEL), so M-Vave's
updater still takes that unit's factory package; build/choralroot-smk37-elite.fwsc, -mke.fwsc. The boot stub holds
the model's name in its update record: each model has its lock (stub_smk37-elite.lock, stub_smk37-mke.lock).
See BUILDING.md for the toolchain and the SDK.

The JieLi toolchain is Linux x86-64 only. JIELI_TOOLCHAIN points at it; on
macOS (or with JIELI_DOCKER=1) each tool runs in a linux/amd64 container.
"""
import argparse
import hashlib
import os
import platform
import re
import shutil
import struct
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

SRC = Path(__file__).resolve().parents[1]
FW = SRC / "firmware"
OUT = SRC / "build"
GEN = OUT / "gen"
LDR = OUT / "loader"
STB = OUT / "stub"
sys.path.insert(0, str(SRC / "tools"))
import fm1pkg_make  # noqa: E402
import ram_code  # noqa: E402
import lz4blk  # noqa: E402

STUB_XIP = 0x02000120               # app.bin offset 0 in the XIP map; the SPL jumps here: the boot stub
STUB_LEN = fm1pkg_make.STUB_LEN     # 0xEE0: the stub ends at flash 0x5000
APP_XIP = STUB_XIP + STUB_LEN       # 0x02001000: the app proper (the body), firmware/app.ld
APP_SLOT = fm1pkg_make.APP_SLOT
LOADER_LOAD = 0x01C0A800
LOADER_NAME = b"usb_hid_ota.bin"    # the file name the SPL looks for
DOCKER_IMAGE = os.environ.get("JIELI_DOCKER_IMAGE", "debian:bookworm-slim")
CFLAGS = ["-Os", "-ffunction-sections", "-fno-builtin", "-Wall", "-Wno-unused-function"]
LINE = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*\t(.*)$")

# SDK files of AC79NN_SDK_V1.2.1_2023-12-13 (the tested version)
SDK_SHA256 = {
    "uboot.boot": "4e3b4c220dc96641cb5a723f41e68ce41d5261ae9434bb33fbd7f2c59976ded4",
    "cfg_tool.bin": "276579954f076886a6a7694f65dc71c034a63a2c204b76749065c0ac7b010d1b",
    "cfg/eq_cfg_hw.bin": "41167491bffed4651750719c973d2758adeb9021a5670d02d6a53c85ed80ea7d",
}

UNIT = "choralroot"                 # firmware/src/UNIT.c; outputs build/UNIT.{bin,elf,dis,fwsc}
PRODUCT = "FM-1_920"                # package identity (ChoralRoot: constant, releases too)
BOARD = "fm1"                       # fm1 (default) or smk37: BOARD env or --board
BOARD_DEFS = {"fm1": [], "smk37": ["-DBOARD_SMK37=1"]}       # the FM-1 build gets no new flag (byte-identical)
BOARD_STEM = {"fm1": UNIT, "smk37": f"{UNIT}-smk37"}        # package name: STEM.fwsc, STEM-X.Y.fwsc
BOARD_LOCK = {"fm1": "stub.lock", "smk37": "stub_smk37.lock"}   # the boot stub that ships, per board (and model)
# smk37: the keyboard's model name (MODEL env or --model); one hardware, three names. pro adds no flag
MODELS = {"pro": "SMK-37 Pro", "elite": "SMK-37 Elite", "mke": "MKE-P37"}
SMK_DATA_FAMILY = "CHORAL"          # SMK store owner tag (storage.c st_owner_check); FELUCCA_DATA_FAMILY overrides
SMK_DATA_UNTAGGED_OK = 0            # an untagged SMK store is another firmware's: cleared (FELUCCA_DATA_UNTAGGED_OK)
VERSION = None                      # FELUCCA_VERSION for release builds (default: firmware/src/choralroot.c)
RAMTEXT_LEN, RAMTEXT_SPARE = 0x6000, 512            # firmware/app.ld RAMTEXT; what a build must leave of it
RAM_SWITCHES = ("CR_RAM_FX", "CR_RAM_MASTER", "CR_RAM_CORE", "CR_RAM_CZ", "CR_RAM_VA", "CR_RAM_FM6", "CR_RAM_NOTE")
# on in a device build unless set to 0: CR_RAM_VA since 3y177 (the unit, the VA chord shared between the cores: the
# block 2389 -> 2103 us, no late half and no shed while recording; docs/VA.md "Two cores"); CR_RAM_NOTE since 3y216 (the
# tick's note path: a long release steals all 8 voices at every chord change, each note through it twice)
RAM_DEFAULT_ON = ("CR_RAM_VA", "CR_RAM_NOTE")


def ram_on(flag):
    v = os.environ.get(flag)
    return v == "1" or (flag in RAM_DEFAULT_ON and v != "0")
XIP_LEN, RAM_LEN, POOL_LEN, NOINIT_LEN = fm1pkg_make.BODY_MAX, 96 * 1024, 0x54000, 0x3D30   # firmware/app.ld
RES_XIP, RES_LEN = 0x0208F040, fm1pkg_make.RES_HI - fm1pkg_make.RES_DATA   # firmware/app.ld RES (cr_res.h, FM-1 only)


def toolchain():
    tc = os.environ.get("JIELI_TOOLCHAIN")
    if not tc or not (Path(tc) / "pi32v2" / "bin" / "clang").exists():
        raise SystemExit("JIELI_TOOLCHAIN must point at the JieLi Linux toolchain "
                         "(the directory with pi32v2/ and common/; see tools/get_toolchain.sh)")
    return Path(tc).resolve()


def use_docker():
    native = platform.system() == "Linux" and platform.machine() in ("x86_64", "AMD64")
    return os.environ.get("JIELI_DOCKER", "0" if native else "1") == "1"


def tc(tool, *args):
    """run a toolchain binary (pi32v2/bin/..., common/bin/...) with cwd SRC; paths relative to SRC"""
    rel = [str(Path(a).resolve().relative_to(SRC)) if isinstance(a, Path) else a for a in args]
    if tool == "cc":                # the toolchain's cc wrapper needs python3; call clang directly
        tool, rel = "pi32v2/bin/clang", ["-target", "pi32v2", *rel]
    if use_docker():
        cmd = ["docker", "run", "--rm", "--platform", "linux/amd64", "-v", f"{SRC}:/work",
               "-v", f"{toolchain()}:/opt/jieli:ro", "-w", "/work", DOCKER_IMAGE, f"/opt/jieli/{tool}", *rel]
    else:
        cmd = [str(toolchain() / tool), *rel]
    r = subprocess.run(cmd, cwd=SRC, capture_output=True, text=True)
    if r.returncode:
        sys.stderr.write(r.stdout + r.stderr)
        raise SystemExit(f"build: {tool} failed")
    if r.stderr.strip():
        sys.stderr.write(r.stderr)
    return r.stdout


def tc_all(*cmds):
    with ThreadPoolExecutor(len(cmds)) as ex:
        return list(ex.map(lambda c: tc(*c), cmds))


def generate():
    """generated headers (UI fonts, icons, keycaps, palettes, tables, layouts, samples)"""
    GEN.mkdir(parents=True, exist_ok=True)
    for old in ("felucca_font.h", "felucca_icons.h"):     # headers of the bitmap font and icon atlas
        (GEN / old).unlink(missing_ok=True)
    tools = SRC / "tools"
    cmds = [[tools / "gen_aa_font.py", GEN / "ui_fonts.h", "--preset", "inter-tight"],
            [tools / "gen_aa_font.py", GEN / "cr_fonts.h", "--preset", "choralroot"],   # ChoralRoot's big type
            [tools / "gen_aa_icons.py", GEN / "ui_icons.h"],
            [tools / "gen_aa_keycaps.py", GEN / "ui_keycaps.h"],
            [tools / "gen_ui_palettes.py", GEN / "ui_palettes.h"],
            [tools / "gen_tables.py", GEN / "felucca_tables.h"],
            # the compiled layouts (docs/LAYOUT-TOOL.md "Step 4": P5's panel; no firmware file includes them yet)
            [tools / "gen_layout.py", GEN / "cr_layouts.h"],
            [tools / "gen_fm6_patches.py", GEN / "felucca_fm6.h"],
            [tools / "gen_cz1_factory.py", GEN / "melodee_cz1.h"],   # CZ-1: Casio's 64 tones (Melodee's)
            # PROPHET: Sequential's 200 factory programs (Melodee's; included with FELUCCA_P5_FACTORY)
            [tools / "gen_prophet_factory.py", GEN / "melodee_prophet_factory.h"],
            [tools / "gen_samples.py", GEN / "felucca_samples.h"]]
    procs = [subprocess.Popen([sys.executable, *map(str, c)], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                              text=True) for c in cmds]
    failed = []
    for c, p in zip(cmds, procs):
        sys.stdout.write(p.communicate()[0])
        if p.returncode:
            failed.append(c[0].name)
    if failed:
        raise SystemExit(f"build: {', '.join(failed)} failed")


# ---- update loader

def crc16(d, c=0):
    for b in d:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) if c & 0x8000 else c << 1
        c &= 0xFFFF
    return c


def ldr_head(dcrc, a, b, attr, name):
    body = struct.pack("<HIIBBH16s", dcrc, a, b, attr, 0, 0, name)
    return struct.pack("<H", crc16(body)) + body


def ldr_wrap(image):
    """ota.bin = outer JLFS head (32 B) + inner head (32 B, load address) +
    blocks [clen u32][dlen u32][LZ4 block], dlen 4096 except the last"""
    blocks = bytearray()
    for i in range(0, len(image), 4096):
        raw = image[i:i + 4096]
        c = lz4blk.compress(raw)
        if len(c) > 4096 or lz4blk.decompress(c) != raw:
            raise SystemExit(f"loader: block {i // 4096} does not compress below 4096 B")
        blocks += struct.pack("<II", len(c), len(raw)) + c
    inner = ldr_head(crc16(image), len(image), LOADER_LOAD, 0, LOADER_NAME)
    body = inner + bytes(blocks)
    return ldr_head(crc16(body), 0x20, len(body), 0x41, LOADER_NAME) + body


def build_loader():
    LDR.mkdir(parents=True, exist_ok=True)
    src = FW / "loader"
    flags = [*CFLAGS, *BOARD_DEFS[BOARD], "-Ifirmware/hal", "-Ifirmware/src"]
    tc_all(("cc", "-c", src / "crt0_ldr.S", "-o", LDR / "crt0_ldr.o"),
           ("cc", *flags, "-c", src / "loader.c", "-o", LDR / "loader.o"))
    elf = LDR / "loader.elf"
    tc("pi32v2/bin/ld", "--gc-sections", "-e", "_start", "-T", src / "loader.ld",
       LDR / "crt0_ldr.o", LDR / "loader.o", "-o", elf)
    _, dis, hdr, syms = tc_all(("common/bin/objcopy", "-O", "binary", "-j", ".text", elf, LDR / "loader.bin"),
                               ("common/bin/objdump", "-d", elf),
                               ("common/bin/objdump", "-h", elf),
                               ("common/bin/objdump", "-t", elf))
    (LDR / "loader.dis").write_text(dis)
    for ln in hdr.splitlines():     # the loader rewrites the flash: nothing may run from XIP
        p = ln.split()
        if len(p) > 4 and p[1].startswith(".") and int(p[3], 16) >= 0x02000000 and int(p[2], 16):
            raise SystemExit(f"loader: section {p[1]} at {p[3]} is outside RAM")
    image = (LDR / "loader.bin").read_bytes()
    if not image or len(image) > 0x14000:
        raise SystemExit("loader: image empty or too big")
    ota = ldr_wrap(image)
    if len(ota) > 0x4000:           # staged at 0xE0000 it must end below the record's sector (0xE4000), which the
        raise SystemExit(f"loader: ota.bin {len(ota)} B reaches the update record's sector")   # app and loader erase
    (LDR / "ota.bin").write_bytes(ota)
    bss = [int(ln.split()[0], 16) for ln in syms.splitlines() if ln.rstrip().endswith("_bss_end")]
    print(f"loader: image {len(image)} B at {LOADER_LOAD:#x}" + (f", bss end {bss[0]:#x}" if bss else ""))
    print(f"loader: ota.bin {len(ota)} B")
    return ota


# ---- boot stub

def build_stub():
    """firmware/stub -> app.bin's first STUB_LEN bytes (0xFF padded). Code and constants only, built from its own
    sources alone, so it is the same bytes in every build: the loader rewrites sector 0x4000 only when it changes
    (docs/BOOT-SAFETY.md). firmware/stub/stub.lock records the version and hash of the stub that is meant to ship."""
    STB.mkdir(parents=True, exist_ok=True)
    src = FW / "stub"
    tc_all(("cc", "-Ifirmware/stub", "-c", src / "crt0_stub.S", "-o", STB / "crt0_stub.o"),
           ("cc", *CFLAGS, *BOARD_DEFS[BOARD], "-Ifirmware/stub", "-c", src / "stub.c", "-o", STB / "stub.o"))
    elf = STB / "stub.elf"
    tc("pi32v2/bin/ld", "--gc-sections", "-e", "_start", "-T", src / "stub.ld",
       STB / "crt0_stub.o", STB / "stub.o", "-o", elf)
    (STB / "stub.bin").unlink(missing_ok=True)
    _, dis, hdr, syms = tc_all(("common/bin/objcopy", "-O", "binary", "-j", ".text", elf, STB / "stub.bin"),
                               ("common/bin/objdump", "-d", elf),
                               ("common/bin/objdump", "-h", elf),
                               ("common/bin/objdump", "-t", elf))
    (STB / "stub.dis").write_text(dis)
    img = (STB / "stub.bin").read_bytes()
    for ln in hdr.splitlines():     # no .data, no .bss: nothing but .text may have a size
        p = ln.split()
        if len(p) > 3 and p[0].isdigit() and p[1].startswith(".") and int(p[2], 16) and \
                p[1] not in (".text", ".shstrtab", ".symtab", ".strtab"):
            raise SystemExit(f"stub: section {p[1]} holds {int(p[2], 16)} B; the stub is code and constants only")
    m = re.search(r"^([0-9a-f]+) .*\s_start$", syms, re.M)
    if not m or int(m.group(1), 16) != STUB_XIP:
        raise SystemExit(f"stub: _start is not at {STUB_XIP:#x}")
    if img[:4] != bytes.fromhex("04818000"):
        raise SystemExit(f"stub: starts with {img[:4].hex()}, not the entry the SPL expects (goto +4; rts)")
    if not fm1pkg_make.has_stub(img + bytes(STUB_LEN)):
        raise SystemExit("stub: no CRSTUB signature at offset 8")
    if not img or len(img) > STUB_LEN - 256:     # keep a margin: a stub that grows to the brim cannot be fixed
        raise SystemExit(f"stub: {len(img)} B does not fit {STUB_LEN} B with 256 B to spare")
    for ln in dis.splitlines():     # nothing may call or load an address in the chip ROM (as the app's check)
        mm = LINE.match(ln)
        for v in (re.findall(r"= (-?\d+) <|call -?\d+ <[^:>]*: ([0-9a-f]+) >", mm.group(3)) if mm else ()):
            val = (int(v[0]) & 0xFFFFFFFF) if v[0] else int(v[1], 16) & 0xFFFFFFFF
            if 0xFFC00000 <= val < 0xFFD00000:
                raise SystemExit(f"stub: reference to ROM address {val:#010x}")
    used = len(img)
    img += b"\xff" * (STUB_LEN - len(img))
    ver, sha = fm1pkg_make.stub_version(img), hashlib.sha256(img).hexdigest()
    print(f"stub: {used} B of {STUB_LEN} ({100 * used / STUB_LEN:.1f}%), version {ver}, sha256 {sha[:16]}")
    lock = src / BOARD_LOCK[BOARD]
    want = lock.read_text().split() if lock.exists() else []
    if want != [str(ver), sha]:
        if os.environ.get("FM1_STUB_LOCK") == "update":
            lock.write_text(f"{ver} {sha}\n")
            print(f"stub: {lock.relative_to(SRC)} updated")
        else:
            msg = (f"the boot stub differs from {lock.relative_to(SRC)} ({' '.join(want)[:20] or 'none'}..): installing "
                   "this build rewrites sector 0x4000, the one window an interruption is not rescued in. If the "
                   "change is meant: bump STUB_VERSION (firmware/stub/stub_core.h), rebuild with "
                   "FM1_STUB_LOCK=update, and say so in the release notes")
            if VERSION:
                raise SystemExit("build: " + msg)
            print("WARNING: " + msg)
    return img


# ---- app

def build_app(stub, single=False):
    flags = [*CFLAGS, *BOARD_DEFS[BOARD], "-Ifirmware/hal", "-Ifirmware/src", "-Ibuild/gen"]
    if single:                      # the single-core package (docs/DUALCORE.md "Two packages"): core 1 never started,
        flags.append("-DCR_SINGLE_CORE=1")  # the version string "ChoralRoot X.Y 1-core" (firmware/src/cr_ui.h)
    # (Felucca's sequencer and the retired engines' switches, FELUCCA_SEQ, _SLICE, _FM4, _SAMPLE, _DRUM, _ANALOG,
    # _PHASE .. _PHYS, and FELUCCA_ICONS left with their sources in 3y232: core.h keeps them 0; the VA, CZ-1 and
    # FM TONE are in every build)
    for flag in ("FELUCCA_FLASH", "FELUCCA_OTA", "FELUCCA_OTA_DRYRUN", "FELUCCA_OTA_RAMONLY", "FELUCCA_CDC",
                 "FELUCCA_UART", "FELUCCA_UAC", "FELUCCA_KEYCAPS", "FELUCCA_SLICER", "FELUCCA_GRAIN",
                 "FELUCCA_PROPHET", "FELUCCA_P5_FACTORY", "FELUCCA_DATA_UNTAGGED_OK",
                 "CR_BLE"):         # CR_BLE=0: the FM-1 without Bluetooth (cr_ble.h; the radio-off comparison, 3y236)
        v = os.environ.get(flag)    # unset: the default in firmware/src/choralroot.c
        if v in ("0", "1"):
            flags.append(f"-D{flag}={v}")
    # Code in RAM (docs/FX.md "Code in RAM"). P5_RAM: the P5 engine's sample loop in .ram_text, 1 in a device build
    # since 3y41 (the unit: the chord part 2084 -> 1533 us; P5_RAM=0 for the flash build to measure against). The
    # host and the emulator never see these flags: there the source's default (0) holds
    flags.append("-DP5_RAM=" + ("0" if os.environ.get("P5_RAM") == "0" else "1"))
    for flag in RAM_SWITCHES:       # each 1 moves one stage's functions to RAMTEXT (firmware/src/cr_ram.h); unset: 0
        if ram_on(flag):            # (but RAM_DEFAULT_ON's: 1; CR_RAM_VA=0 is the all-flash VA to measure against)
            flags.append(f"-D{flag}=1")
    if os.environ.get("CR_RAM_TABLES_P5") == "1":   # the four tables the P5 engine reads every sample in RAM .data,
        flags.append("-DCR_RAM_TABLES_P5=1")        # 6.7 KB (build/gen/felucca_tables.h P5_TABLE)
    # the delay's and the reverb's rate (1 full, 2 half) and their loops in RAM (docs/FX.md "Half rate or full"): for
    # the builds that are measured against each other on the unit; unset: firmware/src/cr_fx.c's defaults
    for flag, ok in (("CR_FX_DELAY_DS", ("1", "2")), ("CR_FX_REVERB_DS", ("1", "2")), ("CR_FX_RAM", ("0", "1")),
                     ("CR_FX_LITE", ("0", "1")),
                     ("XD_N", ("16384", "32768")),      # the delay line's samples (cr_fx.c: 32768 / CR_FX_DELAY_DS)
                     ("LCD_BAUD", ("2", "3", "4", "5"))):   # the LCD's SPI clock, 60 MHz / (BAUD + 1): 4 = 12 MHz (lcd.c),
                                                        # 2 = 20 MHz (docs/EDITOR.md "Responsiveness": to try on the unit)
        v = os.environ.get(flag)
        if v in ok:
            flags.append(f"-D{flag}={v}")
    if os.environ.get("CR_ACK_LATE") == "1":      # the audio half's pending bit cleared after the render, as to 0.18's
        flags.append("-DFM1_AUDIO_ACK_LATE=1")    # second build (firmware/src/audio.c fm1_alnk0_irq)
    if os.environ.get("CR_OUT_MAKEUP") == "1":    # Options > Output Clean: the 2.5 dB its trim takes given back after the
        flags.append("-DCLN_MAKEUP=5461")         # knee, in the DAC word's upper 6 dB (firmware/src/fx.c CLN_MAKEUP;
                                                  # docs/AUDIO-QUALITY.md "Proposed": not a default, to be heard first)
    flags.append(f'-DFELUCCA_ID="{PRODUCT}"')
    if BOARD == "smk37":            # SMK store owner (storage.c): ChoralRoot's own family, never another firmware's data
        fam = os.environ.get("FELUCCA_DATA_FAMILY") or SMK_DATA_FAMILY
        if not re.fullmatch(r"[A-Z0-9]{1,7}", fam):
            raise SystemExit("FELUCCA_DATA_FAMILY: 1..7 of A-Z 0-9")
        flags.append(f'-DFELUCCA_DATA_FAMILY="{fam}"')
        if os.environ.get("FELUCCA_DATA_UNTAGGED_OK") not in ("0", "1"):
            flags.append(f"-DFELUCCA_DATA_UNTAGGED_OK={SMK_DATA_UNTAGGED_OK}")
    if VERSION:
        flags.append(f'-DFELUCCA_VERSION="{VERSION}"')
    # the unit goes to LLVM IR without the optimizer, the main-loop functions (UI, screens, stores) are
    # marked minsize (tools/size_fns.py), then the IR is compiled at -Os. FELUCCA_SIZE=0: -Os everywhere
    size = os.environ.get("FELUCCA_SIZE") != "0"
    unit = FW / "src" / f"{UNIT}.c"
    cmain = (("cc", *flags, "-S", "-emit-llvm", "-Xclang", "-disable-llvm-optzns", "-c",
              unit, "-o", OUT / f"{UNIT}.ll") if size else
             ("cc", *flags, "-c", unit, "-o", OUT / f"{UNIT}.o"))
    tc_all(("cc", "-c", FW / "crt0.S", "-o", OUT / "crt0.o"),
           ("cc", "-c", FW / "hal" / "fm1_vec.S", "-o", OUT / "fm1_vec.o"),
           ("cc", "-c", FW / "hal" / "fm1_isr.S", "-o", OUT / "fm1_isr.o"),
           ("cc", "-c", FW / "hal" / "fm1_cpu1.S", "-o", OUT / "fm1_cpu1.o"),
           cmain)
    if size:
        subprocess.run([sys.executable, SRC / "tools" / "size_fns.py", OUT / f"{UNIT}.ll", OUT / f"{UNIT}_size.ll"],
                       check=True)
        tc("cc", *[f for f in flags if not f.startswith(("-I", "-D", "-W"))], "-c",
           OUT / f"{UNIT}_size.ll", "-o", OUT / f"{UNIT}.o")
    # Code in RAM: the functions the sources mark for each switch that is on (firmware/src/cr_ram.h CR_HOT_x) get
    # their sections renamed in the object, .text.NAME -> .rhot.NAME, and app.ld links .rhot.* at RAMTEXT; the
    # direct calls across are pointed at the veneers the switch compiled in (cr_ram.c). Nothing else is compiled
    # differently. No switch: the object is not touched
    hot = [n for names in ram_code.marks([f for f in RAM_SWITCHES if ram_on(f)]).values() for n in names]
    if hot:
        sizes, calls = ram_code.place(OUT / f"{UNIT}.o", hot)
        print(f"ram: {len(sizes)} functions, {sum(sizes.values())} B, to RAMTEXT ("
              + ", ".join(f for f in RAM_SWITCHES if ram_on(f)) + "); calls across through a veneer: "
              + (", ".join(f"{n} x{c}" for n, c in calls.items()) or "none"))
    elf = OUT / f"{UNIT}.elf"
    # --gc-sections (as the loader and the stub): the functions nothing refers to stay out (-ffunction-sections: the
    # engine's and the looper's API this unit does not call, fm1_vec.S's weak fault handler when cr_cpu1.h has its
    # own). app.ld KEEPs crt0, .ram_text and .c1_text; the vector stubs, the ISR wrappers and the second core's entry
    # are referred to from C (fm1_irq.h, fm1_cpu1.h), so they stay
    tc("pi32v2/bin/ld", "--gc-sections", "-e", "_start", "-T", FW / "app.ld", OUT / "crt0.o", OUT / "fm1_vec.o",
       OUT / "fm1_isr.o", OUT / "fm1_cpu1.o", OUT / f"{UNIT}.o", "-o", elf)
    for sect in ("text.bin", "data.bin", "ramtext.bin", "c1text.bin", "res.bin"):
        (OUT / sect).unlink(missing_ok=True)
    *_, syms, dis, rt, hdr = tc_all(("common/bin/objcopy", "-O", "binary", "-j", ".text", elf, OUT / "text.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".res", elf, OUT / "res.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".data", elf, OUT / "data.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".ram_text", elf, OUT / "ramtext.bin"),
                               ("common/bin/objcopy", "-O", "binary", "-j", ".c1_text", elf, OUT / "c1text.bin"),
                               ("common/bin/objdump", "-t", elf),
                               ("common/bin/objdump", "-d", elf),
                               ("common/bin/objdump", "-d", "-j", ".ram_text", elf),
                               ("common/bin/objdump", "-h", elf))
    (OUT / f"{UNIT}.dis").write_text(dis)
    (OUT / f"{UNIT}.sym").write_text(syms)          # (tools/ram_code.py cmp: which of the listing's labels are code)

    def symv(name):
        return int(re.search(r"^([0-9a-f]+) .*\s" + name + r"$", syms, re.M).group(1), 16)
    img = bytearray((OUT / "text.bin").read_bytes())
    # .ram_text, .c1_text (the second core's loop, docs/DUALCORE.md) and .data follow .text at their load
    # addresses; main.c copies them by words
    for sect, lname in (("ramtext.bin", "_rt_load"), ("c1text.bin", "_c1_load"), ("data.bin", "_data_load")):
        load = symv(lname)
        if load % 4:
            raise SystemExit(f"{lname} {load:#x} is not word aligned")
        blob = (OUT / sect).read_bytes() if (OUT / sect).exists() else b""
        if blob:
            if load - APP_XIP < len(img):
                raise SystemExit(f"{lname} overlaps the image")
            img += b"\xff" * (load - APP_XIP - len(img))
            img += blob
    img += b"\xff" * (-len(img) % 4)
    img = stub + bytes(img)         # app.bin as it is packed: the stub, then the app from flash 0x5000
    (OUT / f"{UNIT}.bin").write_bytes(img)
    return img, syms, dis, rt, hdr


def size_line(hdr):
    """section sizes (objdump -h) against firmware/app.ld's regions"""
    sz = {}
    for ln in hdr.splitlines():
        p = ln.split()
        if len(p) > 3 and p[0].isdigit() and p[1].startswith("."):
            sz[p[1]] = int(p[2], 16)
    t, rt, c1, d, b = (sz.get(k, 0) for k in (".text", ".ram_text", ".c1_text", ".data", ".bss"))
    xip, ram = t + rt + c1 + d, d + b
    s = (f"size: .text {t} B, .ram_text {rt} B, .c1_text {c1} B (RAMTEXT {rt + c1} of {0x6000}), .data {d} B, .bss {b} B; "
         f"XIP {xip} B of {XIP_LEN} ({100 * xip / XIP_LEN:.1f}%), RAM {ram} B of {RAM_LEN} ({100 * ram / RAM_LEN:.1f}%)")
    if ".pool" in sz:
        s += f", POOL {sz['.pool']} B of {POOL_LEN} ({100 * sz['.pool'] / POOL_LEN:.1f}%)"
    if ".noinit" in sz:
        s += f", NOINIT {sz['.noinit']} B of {NOINIT_LEN} ({100 * sz['.noinit'] / NOINIT_LEN:.1f}%)"
    if sz.get(".res"):              # the resource region's data (firmware/src/cr_res.h): outside the slot
        s += f", RES {sz['.res']} B of {RES_LEN} ({100 * sz['.res'] / RES_LEN:.1f}%)"
    return s


def res_data(syms):
    """the resource region's data (.res, firmware/app.ld) as tools/fm1pkg_make.py --res takes it; None for a board
    without the region (SMK-37: the section must be empty)"""
    p = OUT / "res.bin"
    data = p.read_bytes() if p.exists() else b""
    m = re.search(r"^([0-9a-f]+) .*\s_res_start$", syms, re.M)
    if BOARD != "fm1":
        if data:
            raise SystemExit(f"build: {BOARD} has no resource region, but .res holds {len(data)} B")
        return None
    if not m or int(m.group(1), 16) != RES_XIP:
        raise SystemExit(f"build: _res_start is not at {RES_XIP:#x} (firmware/app.ld RES)")
    if not data or len(data) > RES_LEN:
        raise SystemExit(f"build: the resource region's data is {len(data)} B (1 .. {RES_LEN})")
    return data


def res_ble(syms):
    """the radio program's slot in the region's data (hal/ble_rf_tables.h ble_rf_prog, cr_ble.h): (offset, length),
    (0, 0) without Bluetooth"""
    m = re.search(r"^([0-9a-f]+) .*\.res\s+([0-9a-f]+) ble_rf_prog$", syms, re.M)
    return (int(m.group(1), 16) - RES_XIP, int(m.group(2), 16)) if m and BOARD == "fm1" else (0, 0)


def check(img, syms, dis, rt):
    errors, notes = [], []
    m = re.search(r"^([0-9a-f]+) .*\s_start$", syms, re.M)
    if not m or int(m.group(1), 16) != APP_XIP:
        errors.append(f"_start is not at {APP_XIP:#x}")
    if img[STUB_LEN:STUB_LEN + 4] != bytes.fromhex("04818000"):
        errors.append(f"the app starts with {img[STUB_LEN:STUB_LEN + 4].hex()}, not its entry (goto +4; rts)")
    # .ram_text has two parts (firmware/app.ld). Below _rt_hot: the flash driver (and P5_RAM's loop), which runs
    # with the flash off: no call at all. From _rt_hot: the audio code the CR_RAM_* switches moved; it runs with the
    # flash on, calls its neighbours directly and XIP through a register (hal/fm1_cc.h FM1_FAR): a direct call or
    # goto that leaves RAMTEXT cannot be encoded (the linker refuses it), checked here all the same
    hot = re.search(r"^([0-9a-f]+) .*\s_rt_hot$", syms, re.M)
    hot = int(hot.group(1), 16) if hot else 1 << 32
    rt_lo, rt_calls, hot_n, hot_far, hot_out = [], [], 0, 0, []
    for ln in rt.splitlines():
        mm = LINE.match(ln)
        if not mm:
            continue
        if int(mm.group(1), 16) < hot:
            rt_lo.append(ln)
            if re.search(r"\bcall\b", ln):
                rt_calls.append(ln)
            continue
        hot_n += 1
        hot_far += bool(re.search(r"\bcall r\d+", mm.group(3)))
        for a in re.findall(r"(?:call|goto) -?\d+ <[^:>]*: ([0-9a-f]+) >", mm.group(3)):
            if not 0x01C00000 <= int(a, 16) < 0x01C00000 + RAMTEXT_LEN:
                hot_out.append(ln.strip())
    if rt_calls:                    # RAM code runs with the flash off: no calls into XIP
        errors.append(f".ram_text contains calls: {rt_calls[:3]}")
    else:
        notes.append(f".ram_text: {len(rt_lo)} insns, no calls")
    if hot_out:
        errors.append(f".rhot: a direct call or goto out of RAMTEXT: {hot_out[:3]}")
    elif hot_n:
        notes.append(f".rhot: {hot_n} insns from {hot:#x}, {hot_far} calls through a register, none direct out of "
                     "RAMTEXT")
    for ln in dis.splitlines():     # nothing may call or load an address in the chip ROM
        mm = LINE.match(ln)
        if not mm:
            continue
        for v in re.findall(r"= (-?\d+) <|call -?\d+ <[^:>]*: ([0-9a-f]+) >", mm.group(3)):
            val = (int(v[0]) & 0xFFFFFFFF) if v[0] else int(v[1], 16) & 0xFFFFFFFF
            if 0xFFC00000 <= val < 0xFFD00000:
                errors.append(f"reference to ROM address {val:#010x}")
    if len(img) - STUB_LEN > XIP_LEN:
        errors.append(f"the app is {len(img) - STUB_LEN} B, the room below the marker's sector is {XIP_LEN} B")

    def sym(name):
        mm = re.search(r"^([0-9a-f]+) .*\s" + name + r"$", syms, re.M)
        return int(mm.group(1), 16) if mm else 0
    bss = sym("_bss_end") - 0x01C08000
    pool = sym("_pool_end") - sym("_pool_start")
    notes.append(f"image {len(img)} B (stub {STUB_LEN} + app {len(img) - STUB_LEN}); RAM .data+.bss {bss} B of 98304; "
                 f"pool {pool} B of {0x54000}")
    if bss > 96 * 1024:
        errors.append("RAM region overflow")
    rt_used = sym("_rt_end") - sym("_rt_start")   # .ram_text and .c1_text (the linker stops at the region's end)
    if RAMTEXT_LEN - rt_used < RAMTEXT_SPARE:
        errors.append(f"RAMTEXT {rt_used} B of {RAMTEXT_LEN}: under {RAMTEXT_SPARE} B spare (too many CR_RAM_* "
                      "switches at once: docs/FX.md \"Code in RAM\")")
    if 0x54000 - pool < 8192:                     # keep >= 8 KiB of the pool spare
        errors.append(f"pool headroom {0x54000 - pool} B < 8192 B")
    return errors, notes


# Every hardware register access lives in hal/. In src/ and loader/ (comments stripped):
#  - no volatile pointer cast, except of a C object's address (`*(volatile T *)&x`: a read-once
#    of a RAM flag shared with an ISR, e.g. usb.c ota_wire_send);
#  - no literal in a register or reserved window (core SFRs 0x10000-0x13FFF, SFC 0x40000-0x43FFF,
#    GPIO/IOMAP 0x50000-0x51FFF, CPU 0x1EE0000-0x1EEFFFF, RAM top 0x01C7F000- (boot info,
#    mailbox, vectors), XIP 0x02000000-0x020FFFFF);
#  - no inline asm, except the empty compiler barrier RING_PUBLISH() (emits no instruction);
#  - no HAL register macro (a hal/ #define that is, or expands to, a volatile access).
# Linker symbols (_bss_start[], _rt_load[], ...) are plain C objects and pass.
# firmware/stub is not checked: the boot stub carries its own copies of the register sequences it needs, on purpose
# (stub.c: a HAL change must not change the stub's bytes).
MMIO_LIT = re.compile(r"\b0x0*(1[0-3][0-9a-f]{3}|4[0-3][0-9a-f]{3}|5[01][0-9a-f]{3}|1ee[0-9a-f]{4}|"
                      r"1c7f[0-9a-f]{3}|20[0-9a-f]{5})u?l?\b", re.I)
MMIO_CAST = re.compile(r"\(\s*(?:const\s+)?volatile\b[^()]*\*\s*\)(?!\s*&)")
MMIO_ASM = re.compile(r"\b(?:__asm__|asm)\b(?!\s+volatile\s*\(\s*\"\"\s*:::\s*\"memory\"\s*\))")


def mmio_check():
    def strip(s):
        s = re.sub(r"/\*.*?\*/", lambda m: "\n" * m.group(0).count("\n"), s, flags=re.S)
        return re.sub(r"//[^\n]*", "", s)
    defs = {}
    for h in sorted((FW / "hal").glob("*.h")):
        for m in re.finditer(r"^#define\s+(\w+)(?:\([^)]*\))?\s+(.*)$", strip(h.read_text()), re.M):
            defs[m.group(1)] = m.group(2)
    regs = {n for n, b in defs.items() if re.search(r"\bvolatile\b", b)}
    while True:                                   # macros built on register macros (FM1_WR_LIMIT_H -> FM1_X2)
        more = {n for n, b in defs.items() if n not in regs and set(re.findall(r"\w+", b)) & regs}
        if not more:
            break
        regs |= more
    errors = []
    for f in sorted([*(FW / "src").glob("*.[ch]"), *(FW / "loader").glob("*.c")]):
        for no, ln in enumerate(strip(f.read_text()).splitlines(), 1):
            where = f"{f.relative_to(FW)}:{no}"
            for rx, what in ((MMIO_LIT, "register/window address"), (MMIO_CAST, "volatile pointer cast"),
                             (MMIO_ASM, "inline asm")):
                m = rx.search(ln)
                if m:
                    errors.append(f"{where}: {what} outside hal/ ({m.group(0).strip()}); add a hal/ helper")
            used = set(re.findall(r"\b[A-Z_][A-Z0-9_]*\b", ln)) & regs
            if used:
                errors.append(f"{where}: HAL register macro {sorted(used)[0]} outside hal/; use a hal/ helper")
    return errors


def main():
    global VERSION, BOARD, PRODUCT
    ap = argparse.ArgumentParser()
    ap.add_argument("--release", metavar="X.Y", help=f"release build: identity {PRODUCT}, version string ChoralRoot X.Y")
    ap.add_argument("--board", choices=sorted(BOARD_DEFS), default=None,
                    help="target board: fm1 (default) or smk37 (default: $BOARD, else fm1)")
    ap.add_argument("--model", choices=sorted(MODELS), default=None,
                    help="smk37: the model name, pro (default), elite or mke (default: $MODEL, else pro)")
    ap.add_argument("--sdk", type=Path, help="JieLi AC79 SDK checkout (default: $AC79_SDK)")
    a = ap.parse_args()
    BOARD = (a.board or os.environ.get("BOARD") or "fm1").lower().replace("-", "")
    if BOARD not in BOARD_DEFS:
        raise SystemExit(f"BOARD={BOARD}: use fm1 or smk37")
    model = (a.model or (os.environ.get("MODEL") if BOARD == "smk37" else None) or "pro").lower()
    if model not in MODELS or (model != "pro" and BOARD != "smk37"):
        raise SystemExit(f"MODEL={model}: pro, elite or mke, with BOARD=smk37")
    fm1pkg_make.set_board(BOARD)
    if BOARD != "fm1":              # the board's identity, as constant as the FM-1's (SMK-37 Pro_920)
        PRODUCT = MODELS[model] + PRODUCT[PRODUCT.index("_"):]
    stem = BOARD_STEM[BOARD]
    if model != "pro":              # another model name: identity, loader, update record, the stub's record (SMK_MODEL)
        BOARD_DEFS[BOARD] = [*BOARD_DEFS[BOARD], f'-DSMK_MODEL="{MODELS[model]}"']
        stem += "-" + model
        BOARD_LOCK[BOARD] = f"stub_smk37-{model}.lock"
    name = f"{stem}.fwsc"
    if a.release:                   # the identity stays FM-1_920: only the version string and the names change
        m = re.fullmatch(r"\d+\.\d+(?:\.\d+)?(?:-[A-Za-z0-9.]+)?", a.release)   # the identity never encodes it
        if BOARD != "fm1":          # (an SMK test build: X.Y-suffix-smk37 too, the board in the version)
            m = re.fullmatch(r"\d+\.\d+(?:\.\d+)?(?:-[A-Za-z0-9.]+)*", a.release)
            if m and BOARD not in a.release.lower():
                raise SystemExit(f"--release {a.release}: an SMK-37 release names the board, e.g. {a.release}-{BOARD}")
        elif m and "smk" in a.release.lower():
            raise SystemExit(f"--release {a.release}: an FM-1 release never names the SMK (BOARD=smk37 for that)")
        if not m:
            raise SystemExit(f"--release {a.release}: use X.Y, X.Y.Z or X.Y-suffix")
        VERSION = "ChoralRoot " + a.release.lower()      # e.g. ChoralRoot 1.0, ChoralRoot 1.1-rc1
        name = f"{stem}-{a.release}.fwsc"
        name1 = f"{stem}-{a.release}-1core.fwsc"     # the single-core package (docs/DUALCORE.md "Two packages")
        if BOARD != "fm1":          # choralroot-X.Y-smk37[-elite|-mke][-1core].fwsc
            tag = f"{UNIT}-{a.release}" + stem[len(BOARD_STEM[BOARD]):]
            name, name1 = f"{tag}.fwsc", f"{tag}-1core.fwsc"
    fm1pkg_make.SDK = a.sdk
    for rel, sha in SDK_SHA256.items():          # fail early without the SDK
        if hashlib.sha256(fm1pkg_make.sdk_file(rel)).hexdigest() != sha:
            print(f"warning: SDK {rel} differs from AC79NN_SDK_V1.2.1; the package will not match the reference")
    OUT.mkdir(parents=True, exist_ok=True)
    with ThreadPoolExecutor(3) as ex:
        gen, ldr, stb = ex.submit(generate), ex.submit(build_loader), ex.submit(build_stub)
        gen.result()
        ota = ldr.result()
        stub = stb.result()
    # CR_SINGLE_CORE=1: this build is the single-core package. A release builds both, the single-core one first, so
    # that build/choralroot.{bin,elf,dis} are the standard package's when it ends
    single_env = os.environ.get("CR_SINGLE_CORE") == "1"
    variants = [True, False] if a.release else [single_env]
    one = None
    for single in variants:
        img, syms, dis, rt, hdr = build_app(stub, single)
        errors, notes = check(img, syms, dis, rt)
        hal_err = mmio_check()
        errors += hal_err
        if not hal_err:
            notes.append("register access: hal/ only (src/, loader/ clean)")
        for n in notes:
            print("  ok   ", n)
        for e in errors:
            print("  FAIL ", e)
        print(("1-core " if single else "") + size_line(hdr))
        if errors:
            raise SystemExit("build: checks failed")
        flash = fm1pkg_make.flash_image(img, fm1pkg_make.KEY, res=res_data(syms), ble=res_ble(syms))
        pkg = fm1pkg_make.ufw(flash, ota, PRODUCT)
        print(f"sector 0x4000 (the directory and the stub: constant while the stub is) sha256 "
              f"{hashlib.sha256(flash[0x4000:0x5000]).hexdigest()[:16]}; marker at 0x91fe0, body "
              f"{len(img) - STUB_LEN + (-len(img) % 4)} B")
        if single and a.release:
            one = (pkg, bytes(img))
            (OUT / name1).write_bytes(pkg)
            print(f"package  {OUT / name1}  {len(pkg)} B, identity {PRODUCT} (single core)")
    (OUT / name).write_bytes(pkg)
    (OUT / "ATTRIBUTION.txt").unlink(missing_ok=True)  # (the SAMPLE sets' credit: no build has samples since 3y232)
    print(f"app      {OUT / f'{UNIT}.bin'}  {len(img)} B")
    print(f"loader   {LDR / 'ota.bin'}  {len(ota)} B")
    print(f"package  {OUT / name}  {len(pkg)} B, identity {PRODUCT}" + (" (single core)" if single_env and not a.release else "") +
          (f", board {BOARD}" if BOARD != "fm1" else ""))
    if a.release and BOARD != "fm1":   # the SMK test build: the three models in one folder (above)
        rel = OUT / f"release-{BOARD}"
        rel.mkdir(parents=True, exist_ok=True)
        mine = re.compile(re.escape(f"{UNIT}-{a.release}") + r"(-elite|-mke)?(-1core)?(\.fwsc|-app\.bin)")
        for f in rel.iterdir():         # another version's packages and apps go (this one's other models stay; the
            if f.is_file() and f.name.endswith((".fwsc", "-app.bin")) and not mine.fullmatch(f.name):   # .N respin's too)
                f.unlink()
        (rel / "LICENSES").mkdir(exist_ok=True)
        files = ((name, pkg), (name[:-5] + "-app.bin", bytes(img)), (name1, one[0]), (name1[:-5] + "-app.bin", one[1]))
        for n, b in files:
            (rel / n).write_bytes(b)
        sums = sorted(f.name for f in rel.iterdir() if f.is_file() and f.name.endswith((".fwsc", "-app.bin")))
        (rel / "SHA256SUMS").write_text("".join(f"{hashlib.sha256((rel / n).read_bytes()).hexdigest()}  {n}\n"
                                                for n in sums))
        shutil.copy(SRC / "SMK37.md", rel / "SMK37.md")
    elif a.release:                 # what a release carries: the package, the app and every licence they need
        rel = OUT / f"release-{a.release}"
        shutil.rmtree(rel, ignore_errors=True)
        (rel / "LICENSES").mkdir(parents=True)
        app = f"{stem}-{a.release}-app.bin"
        app1 = f"{stem}-{a.release}-1core-app.bin"
        files = ((name, pkg), (app, bytes(img)), (name1, one[0]), (app1, one[1]))
        for n, b in files:
            (rel / n).write_bytes(b)
        (rel / "SHA256SUMS").write_text("".join(f"{hashlib.sha256(b).hexdigest()}  {n}\n" for n, b in files))
    if a.release:
        for f in sorted((SRC / "LICENSES").glob("*.txt")):
            shutil.copy(f, rel / "LICENSES" / f.name)
        for doc in ("LICENSE", "LICENSING.md"):
            shutil.copy(SRC / doc, rel / doc)
        print(f"release  {rel}/")
    return 0


if __name__ == "__main__":
    sys.exit(main())
