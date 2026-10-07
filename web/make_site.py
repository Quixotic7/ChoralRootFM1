#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Make the site (GitHub Pages):

  index.html, site.css, img/,  the landing page and the manual: web/site/** copied, {{VERSION}} {{PKG}}
  InterTight.woff2, OFL.txt    {{PKG_URL}} {{PRODUCT}} filled in index.html (an unknown {{...}} fails)
  firmware/choralroot-VER.fwsc the package (+ LICENSE, LICENSING.md, LICENSES/: the package holds
                              JieLi SDK files under Apache-2.0, see LICENSING.md)
  webapp/installer/index.html index_pkg.html, self-contained (fm1pkg.js, fm1ota.js, fm1backup.js, fm1sounds.js,
                              metadata inlined)
  emu/                        "Try it in the browser": build/emu-web/* (made by sh tools/emu/web/build_web.sh;
                              EMU_DIR or a 4th argument overrides; missing: a warning, the site is made without it)
  src/                        not touched

  web/make_site.py build/choralroot.fwsc VERSION OUT_DIR [EMU_DIR]     (or build/choralroot-X.Y.fwsc X.Y)

No web editor: Felucca's editor protocol is not in ChoralRoot, so webapp/editor/ is not written (an old one in
OUT_DIR is left as it is). The package must be one made by tools/fm1pkg_make.py (Felucca's own loader, no
vendor files). Its identity (FM-1_9xx; ChoralRoot's is FM-1_920) is read from the package; the device must
report it after the install.
"""
import json
import os
import re
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
SITE = HERE / "site"
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F


def strip_module(src):
    src = re.sub(r"^export\s+", "", src, flags=re.M)
    return re.sub(r"^import .*?;\n", "", src, flags=re.M)


def product_of(raw):
    """the package identity: one marker byte after each of the first 20 blocks (fm1pkg.js productOf)"""
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def fill(fills):
    """web/site/index.html with the placeholders filled; an unknown {{...}} left over fails"""
    text = (SITE / "index.html").read_text(encoding="utf-8")
    for key, val in fills.items():
        text = text.replace("{{" + key + "}}", val)
    left = sorted(set(re.findall(r"\{\{[^}]*\}\}", text)))
    if left:
        raise SystemExit(f"{SITE / 'index.html'}: unknown placeholder(s) {', '.join(left)}; "
                         f"make_site.py fills {', '.join('{{' + k + '}}' for k in fills)}")
    return text


def landing(out, index):
    """web/site/** to the root of OUT_DIR (index.html as filled); returns the paths written"""
    written = []
    for src in sorted(p for p in SITE.rglob("*") if p.is_file() and not p.name.startswith(".")):
        rel = src.relative_to(SITE)
        dst = out / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        if rel.as_posix() == "index.html":
            dst.write_text(index, encoding="utf-8")
        else:
            shutil.copy(src, dst)
        written.append(rel.as_posix())
    return written


def main(pkg, version, out, emu=None):
    pkg, out = Path(pkg), Path(out)
    emu = Path(emu or os.environ.get("EMU_DIR") or HERE.parent / "build" / "emu-web")
    raw = pkg.read_bytes()
    product = product_of(raw)
    if not re.fullmatch(r"FM-1_9\d\d", product):
        raise SystemExit(f"{pkg}: identity {product!r} is not a Felucca / ChoralRoot package (FM-1_9xx)")
    if b"FELUCCA-LOADER-1" not in raw:              # Felucca loader marker: never publish a package with vendor files
        raise SystemExit(f"{pkg}: no Felucca loader in it; the site ships only fm1pkg_make.py packages "
                         "(a package patched from an official one carries vendor files)")
    html = (HERE / "index_pkg.html").read_text(encoding="utf-8")
    lib = strip_module((HERE / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((HERE / "fm1ota.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((HERE / "fm1backup.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((HERE / "fm1sounds.js").read_text(encoding="utf-8"))
    name = f"choralroot-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"
    meta = json.dumps({"version": version, "product": product, "pkg": "../../firmware/" + name})
    for mark in ("/*LIB*/", "/*META*/"):
        if html.count(mark) != 1:
            raise SystemExit(f"index_pkg.html must contain {mark} once; update make_site.py")
    index = fill({"VERSION": version, "PKG": name, "PKG_URL": "firmware/" + name, "PRODUCT": product})
    html = html.replace("/*LIB*/", lib).replace("/*META*/", meta)
    inst, fw = out / "webapp" / "installer", out / "firmware"
    for d in (inst, fw):
        d.mkdir(parents=True, exist_ok=True)
    for old in [*fw.glob("choralroot-*.fwsc"), *fw.glob("felucca-*.fwsc")]:   # one package: the current one
        old.unlink()
    (inst / "index.html").write_text(html, encoding="utf-8")
    shutil.copy(pkg, fw / name)
    lic = HERE.parent / "LICENSES"                  # the package holds JieLi SDK files (Apache-2.0): their
    (fw / "LICENSES").mkdir(exist_ok=True)          # licence travels next to it, with Felucca's own
    names = sorted(f.name for f in lic.glob("*.txt"))
    for n in names:
        shutil.copy(lic / n, fw / "LICENSES" / n)
    (fw / "LICENSES" / "index.html").write_text(    # the pages link this folder: Pages lists no folders
        '<!doctype html><meta charset="utf-8"><title>ChoralRoot FM-1 licences</title><h1>Licence texts</h1><ul>'
        + "".join(f'<li><a href="{n}">{n}</a></li>' for n in names)
        + '</ul><p><a href="../LICENSING.md">LICENSING.md</a> · <a href="../LICENSE">LICENSE (GPL-3.0)</a></p>\n',
        encoding="utf-8")
    for doc in ("LICENSE", "LICENSING.md"):
        shutil.copy(HERE.parent / doc, fw / doc)
    site = landing(out, index)
    if (emu / "index.html").is_file():
        (out / "emu").mkdir(exist_ok=True)
        emu_files = sorted(f.name for f in emu.iterdir() if f.is_file() and not f.name.startswith("."))
        for n in emu_files:
            shutil.copy(emu / n, out / "emu" / n)
        emu_note = f"emu/ ({', '.join(emu_files)})"
    else:
        emu_note = "no emu/"
        print(f"warning: {emu}/index.html missing: the site is made without the browser emulator (emu/), so the "
              "landing page's 'Try it in the browser' link has no target; build it with sh tools/emu/web/build_web.sh",
              file=sys.stderr)
    print(f"site: {out}: {', '.join(site)}; webapp/installer/index.html ({len(html)} B); "
          f"firmware/{name} ({len(raw)} B, {product}), firmware/LICENSE, firmware/LICENSING.md, "
          f"firmware/LICENSES/ ({len(names)} texts + index.html); {emu_note}")


if __name__ == "__main__":
    if len(sys.argv) not in (4, 5):
        sys.exit(__doc__)
    main(*sys.argv[1:5])
