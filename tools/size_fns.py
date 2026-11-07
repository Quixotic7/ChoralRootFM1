#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Build the main-loop code for size (build.py): the functions defined in SIZE_FILES get LLVM's
minsize (about -Oz) while everything else (the sound, render and ISR code, the flash and OTA code,
the main loop) keeps -Os. The JieLi clang 4 has no '#pragma clang attribute' and its minsize
attribute is an error on variables, so build.py compiles the unit (choralroot.c) to LLVM IR without the
optimizer, this script adds 'minsize' to those definitions, and the IR is then compiled at -Os
(without the edit, that round trip is byte-identical to compiling the unit directly). A listed file that is
missing is skipped. (Felucca's UI, editor and project files and the stores 3y231 retired left the list with their
sources in 3y232.)
  size_fns.py IN.ll OUT.ll"""
import re
import sys
from pathlib import Path

_ROOT = Path(__file__).resolve().parents[1]
SRC = _ROOT / "firmware" / "src" if (_ROOT / "firmware" / "src").is_dir() else _ROOT / "src"   # either layout
# the stores and the console: main loop only, never called by sound code
SIZE_FILES = ["storage.c", "upreset.c", "settings_persist.c", "console.c",
              # ChoralRoot's UI, screens and stores (not cr_engine.c, cr_out.c, cr_loop.c: they tick in the audio ISR)
              "cr_ui.c", "cr_edit.c", "cr_draw.c", "cr_gfx.c", "cr_pages.c", "cr_bank.c", "cr_name.c", "cr_settings.c",
              "cr_anim.c", "cr_shim.c", "cr_favs.c",
              # the flash diet (HISTORY 3y131): the stores, the backup, the pool, the layouts: main loop too
              "cr_backup.c", "cr_layout.c", "upool.c", "fm6_store.c", "fm6_bank.c", "cz_store.c", "cz_bank.c",
              "cz_legacy.h", "prophet_patch.c"]
# .. and these functions of files whose other functions render or tick in the ISR: the main loop's (a sound load, a
# blob, the deep pages' get / set, which the engine picks up in its block), and the main loop itself
SIZE_FUNCS = ["fm1_main", "felucca_init",
              "fm6_track_loaded", "fm6_load_slot", "fm6_put_patch", "fm6_set_patch", "fm6_init", "fm6_pack", "fm6_unpack",
              "fm6_dset", "fm6_dref", "fm6_dget", "fm6_blob_read", "fm6_blob_set", "fm6_rom_patch",
              "cz_track_loaded", "cz_factory_tone", "cz_factory_bank", "cz_ed_decode", "cz_dset", "cz_dref", "cz_dget",
              "quad_unpack", "quad_blob_ok", "quad_set", "quad_blob_set", "quad_blob_get", "quad_blob_preset",
              "va_unpack", "va_blob_ok", "va_set", "va_get", "va_desc",
              "p5_patch_valid", "p5_preset_patch"]
DEF = re.compile(r"^(?:static|void|int|uint\w*|int\w*|const)\b[^;=(]*?\b([A-Za-z_]\w*)\s*\(", re.M)
IR_DEF = re.compile(r"^(define [^\n]*?@\"?([\w.]+)\"?\([^\n]*\)(?: unnamed_addr| local_unnamed_addr)?)( #\d+[^\n]*\{)$",
                    re.M)


def definitions(text):
    """names of the functions text defines (a signature at column 0 whose ')' is followed by '{')"""
    names = []
    text = re.sub(r"/\*.*?\*/|//[^\n]*", " ", text, flags=re.S)        # comments (none hold code)
    for m in DEF.finditer(text):
        i, depth = m.end() - 1, 0
        while i < len(text):
            depth += {"(": 1, ")": -1}.get(text[i], 0)
            i += 1
            if depth == 0:
                break
        rest = re.sub(r"^(__attribute__\s*\(\(.*?\)\)\s*)+", "", text[i:i + 200].lstrip())
        if rest.startswith("{"):
            names.append(m.group(1))
    return names


def size_names():
    names = set(SIZE_FUNCS)
    for f in SIZE_FILES:
        if (SRC / f).is_file():
            names.update(definitions((SRC / f).read_text()))
    return names


def main(src, dst):
    names, hit = size_names(), set()

    def mark(m):
        if m.group(2) not in names:
            return m.group(0)
        hit.add(m.group(2))
        return m.group(1) + " minsize" + m.group(3)
    Path(dst).write_text(IR_DEF.sub(mark, Path(src).read_text()))
    print(f"size: {len(hit)} main-loop functions built for size ({len(names) - len(hit)} not in this build)")
    return 0


if __name__ == "__main__":
    sys.exit(main(*sys.argv[1:3]))
