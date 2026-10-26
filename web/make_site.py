#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Make the site (GitHub Pages):

  index.html, site.css, img/,  the landing page and the manual: web/site/** copied, {{VERSION}} {{PKG}}
  InterTight.woff2, OFL.txt    {{PKG_URL}} {{PRODUCT}} {{NEWS}} {{LAYOUT}} {{EMU_NOTE}} filled in index.html (an
                              unknown {{...}} fails); {{NEWS}} is "New in VER": the version's bullets from
                              web/releases.json. When VERSION is a beta ("beta": true), the landing page and the
                              installer's own lines name the newest stable version offered (front()); the beta is in
                              the installer's list only, behind "Show beta releases". {{EMU_NOTE}}: a line saying the
                              browser emulator runs a beta when its version.txt says so (emu_line())
  firmware/choralroot-X.Y.fwsc the packages: the current version's (PKG) and every other version of
                              web/releases.json found in the packages folder, each checked against the sha256
                              in releases.json; for a version that lists sha256_1core, its single-core package
  firmware/choralroot-X.Y-1core.fwsc (docs/DUALCORE.md "Two packages") beside it, checked the same way: the current
                              version's next to PKG or in the packages folder, the others' in the packages folder; index.html (the files, their checksums, each version's source at
                              its tag), SHA256SUMS, LICENSE, LICENSING.md, LICENSES/ (the packages hold JieLi SDK
                              files under Apache-2.0, see LICENSING.md)
  webapp/installer/index.html index_pkg.html, self-contained (fm1pkg.js, fm1ota.js, fm1backup.js, fm1sounds.js
                              and the metadata inlined: the versions offered, their notes and checksums)
  webapp/installer/manager.html  the device manager (docs/DEVICE-MANAGER.md): manager.html, self-contained
                              (fm1backup.js, fm1sounds.js, fm1manager.js and the packs of web/packs inlined; ?sim=1
                              runs it against a simulated FM-1)
  webapp/installer/packs/     web/packs/*.json (the packs as files, to download)
  emu/                        "Try it in the browser": build/emu-web/* (made by sh tools/emu/web/build_web.sh;
                              EMU_DIR or a 4th argument overrides; missing: a warning, the site is made without it)
  webapp/layout/              the layout viewer (docs/LAYOUT-TOOL.md): layout.html as index.html (fm1sketch.js, fm1layout.js
                              inlined) beside build/layout-web/crlayout.js and crlayout.wasm (made by
                              sh tools/layout/build_layout.sh; LAYOUT_DIR overrides; missing: a warning, the site is
                              made without it and the landing page has no link to it: {{LAYOUT}} is empty)
  webapp/smk37/index.html     the SMK-37 test installer (SMK37.md): smk37.html, self-contained (fm1pkg.js, fm1ota.js,
                              fm1backup.js and the metadata inlined), not linked from any other page (noindex): the
                              versions of web/releases-smk37.json, each with its three models' packages
  firmware/smk37/             those packages (choralroot-X.Y-smk37[-elite|-mke][-1core].fwsc), each checked against
                              the sha256 in releases-smk37.json and its identity (the model's, _9xx, Felucca's
                              loader), SHA256SUMS and index.html (noindex). --smk-packages DIR (or
                              CR_SMK_PACKAGES_DIR; build/release-smk37 when it exists) holds them; a local build skips
                              a version whose files are not all there, --require-all stops. No version offered: no
                              SMK page (a line says so)
  webapp/smk37/emu/           the SMK-37 browser emulator beside it: build/emu-web-smk37/* (sh tools/emu/web/build_web.sh;
                              SMK_EMU_DIR overrides; missing: a warning, the page is made without it and without the
                              link to it: smk37.html's <!--EMU--> is empty); linked from the SMK page alone (noindex)
  src/                        not touched

  web/make_site.py build/choralroot.fwsc VERSION OUT_DIR [EMU_DIR]     (or build/choralroot-X.Y.fwsc X.Y)
                   [--packages DIR] [--smk-packages DIR] [--require-all]

The other versions: --packages DIR (or CR_PACKAGES_DIR; build/scratch/packages when it exists) holds their
choralroot-X.Y.fwsc (and choralroot-X.Y-1core.fwsc where listed). A local build skips a version whose file is not there, with a "skipped:" line, and builds a
VERSION that is not in releases.json as a local build without notes. --require-all (the Pages job) stops instead:
every listed version must be there (with its single-core package when it lists one) and VERSION must be the newest
listed; a local build offers a version without the single-core choice when that file is not there ("skipped:"). A
package whose sha256 is not the one in releases.json, or whose identity is not the current package's, always stops
the build, the single-core package's as the standard one's.

No web editor: Felucca's editor protocol is not in ChoralRoot, so webapp/editor/ is not written (an old one in
OUT_DIR is left as it is). The package must be one made by tools/fm1pkg_make.py (Felucca's own loader, no
vendor files). Its identity (FM-1_9xx; ChoralRoot's is FM-1_920) is read from the package; the device must
report it after the install.
"""
import argparse
import hashlib
import json
import os
import re
import shutil
import sys
from html import escape as html_escape
from pathlib import Path

HERE = Path(__file__).resolve().parent
SITE = HERE / "site"
RELEASES = Path(os.environ.get("CR_RELEASES_JSON") or HERE / "releases.json")   # the versions the installer offers
# (newest first), their notes; CR_RELEASES_JSON: another list (tests/site_test.py, a draft of a release's entry)
SMK_RELEASES = Path(os.environ.get("CR_SMK_RELEASES_JSON") or HERE / "releases-smk37.json")   # the SMK-37 test builds
SMK_MODELS = {"pro": ("SMK-37 Pro", ""), "elite": ("SMK-37 Elite", "-elite"), "mke": ("MKE-P37", "-mke")}   # key: the
# model name its packages carry, the file name's suffix (tools/build.py: choralroot-X.Y-smk37[-elite|-mke][-1core].fwsc)
MONTHS = ("January", "February", "March", "April", "May", "June", "July", "August", "September", "October",
          "November", "December")
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


def manager_page():
    """web/manager.html with /*LIB*/ (fm1backup.js, fm1dnsnd.js, fm1sounds.js, fm1manager.js) and the packs (web/packs) inlined"""
    html = (HERE / "manager.html").read_text(encoding="utf-8")
    lib = "\n".join(strip_module((HERE / n).read_text(encoding="utf-8")) for n in ("fm1backup.js", "fm1dnsnd.js", "fm1sounds.js", "fm1manager.js"))
    packs = HERE / "packs"
    index = json.loads((packs / "index.json").read_text(encoding="utf-8")) if (packs / "index.json").is_file() else {"packs": []}
    embed = {"index": [{**p, "url": "packs/" + p["file"]} for p in index["packs"]],
             "packs": {p["file"]: json.loads((packs / p["file"]).read_text(encoding="utf-8")) for p in index["packs"]}}
    for mark in ("/*LIB*/", "/*PACKS*/null"):
        if html.count(mark) != 1:
            raise SystemExit(f"manager.html must contain {mark} once; update make_site.py")
    return html.replace("/*LIB*/", lib).replace("/*PACKS*/null", json.dumps(embed, separators=(",", ":")).replace("</", "<\\/"))


def layout_page():
    """web/layout.html with /*LIB*/ (fm1sketch.js, then fm1layout.js, which imports it) inlined; crlayout.js stays a file
    beside it (the firmware's wasm)"""
    html = (HERE / "layout.html").read_text(encoding="utf-8")
    if html.count("/*LIB*/") != 1:
        raise SystemExit("layout.html must contain /*LIB*/ once; update make_site.py")
    lib = "\n".join(strip_module((HERE / n).read_text(encoding="utf-8")) for n in LAYOUT_LIB)   # (not spelled as the
    return html.replace("/*LIB*/", lib)             # installer's libraries are: web/test_installer.mjs reads those from this file)


LAYOUT_LIB = ("fm1sketch.js", "fm1layout.js")
LAYOUT_FILES = ("crlayout.js", "crlayout.wasm")
LAYOUT_LINK = ('<p class="note">Every page of the sound editor, drawn by the firmware in your browser:</p>'
               '<p class="note note-try"><a href="webapp/layout/">Layout viewer</a></p>')


def check_package(path, raw):
    """the identity of a package the site may publish: FM-1_9xx and Felucca's loader, or the build stops"""
    product = product_of(raw)
    if not re.fullmatch(r"FM-1_9\d\d", product):
        raise SystemExit(f"{path}: identity {product!r} is not a Felucca / ChoralRoot package (FM-1_9xx)")
    if b"FELUCCA-LOADER-1" not in raw:              # Felucca loader marker: never publish a package with vendor files
        raise SystemExit(f"{path}: no Felucca loader in it; the site ships only fm1pkg_make.py packages "
                         "(a package patched from an official one carries vendor files)")
    return product


def pkg_name(version):
    return f"choralroot-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}.fwsc"


def pkg_name_1core(version):
    """the single-core package (docs/DUALCORE.md "Two packages"): tools/build.py --release X.Y writes it beside the other"""
    return f"choralroot-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}-1core.fwsc"


def date_text(iso):
    y, m, d = (int(x) for x in iso.split("-"))
    return f"{d} {MONTHS[m - 1]} {y}"


def collect(pkg, version, packages, require_all):
    """the versions the site offers, newest first: [(entry, raw bytes)], the identity, and the lines to print.

    The current version comes from pkg; every other version of web/releases.json from PACKAGES/choralroot-X.Y.fwsc.
    A package is refused when its sha256 is not the one in releases.json, or its identity is not the current one's.
    A listed version whose file is missing is skipped with a line (a local build), or stops the build
    (--require-all: the Pages job)."""
    data = json.loads(RELEASES.read_text(encoding="utf-8"))
    listed = data["releases"]
    raw = Path(pkg).read_bytes()
    product = check_package(pkg, raw)
    digest = hashlib.sha256(raw).hexdigest()
    notes, out = [], []
    by_version = {r["version"]: r for r in listed}
    if version in by_version:
        want = by_version[version]["sha256"]
        if digest != want:
            raise SystemExit(f"{pkg}: sha256 {digest} is not the released {pkg_name(version)} "
                             f"({want} in web/releases.json): the site publishes the release's own file")
        if require_all and listed[0]["version"] != version:
            raise SystemExit(f"version {version} is not the newest in web/releases.json ({listed[0]['version']} is): "
                             "the site is published for the newest version")
    else:
        if require_all:
            raise SystemExit(f"version {version} is not in web/releases.json: add it (its bullets, date, tag and the "
                             f"package's sha256 {digest}); docs/HANDOFF.md, 'Release steps'")
        notes.append(f"note: {version} is not in web/releases.json: published as a local build, without notes")
        out.append(({"version": version, "sha256": digest, "bullets": []}, raw))
    for r in listed:
        v, name = r["version"], pkg_name(r["version"])
        if v == version:
            out.append((r, raw))
            continue
        src = packages / name if packages else None
        if src is None or not src.is_file():
            where = f"not in {packages}" if packages else "no packages folder given"
            if require_all:
                raise SystemExit(f"version {v}: {name} is missing ({where}). Every version of web/releases.json "
                                 f"must be published: download it from the release {r['tag']}, or take {v} out of "
                                 "web/releases.json")
            notes.append(f"skipped: {name} ({where}): version {v} is not offered by this build "
                         "(--packages DIR or CR_PACKAGES_DIR; python3 tools/fetch_packages.py fetches them)")
            continue
        old = src.read_bytes()
        got = hashlib.sha256(old).hexdigest()
        if got != r["sha256"]:
            raise SystemExit(f"version {v}: {src} has sha256 {got}, web/releases.json says {r['sha256']}: refused")
        if check_package(src, old) != product:
            raise SystemExit(f"version {v}: {src} has the identity {product_of(old)}, the current package {product}: "
                             "refused (the installer verifies one identity)")
        out.append((r, old))
    one = {}                                        # version -> the single-core package's bytes (where listed)
    for r, _ in out:
        v, want = r["version"], r.get("sha256_1core")
        if not want:
            continue
        name = pkg_name_1core(v)
        where = ([Path(pkg).parent / name] if v == version else []) + ([packages / name] if packages else [])
        src = next((w for w in where if w.is_file()), None)
        if src is None:
            if require_all:
                raise SystemExit(f"version {v}: web/releases.json lists its single-core package, but {name} is missing "
                                 f"({', '.join(map(str, where)) or 'no packages folder given'}): attach it to the release "
                                 f"{r['tag']}, or take sha256_1core out of {v}'s entry")
            notes.append(f"skipped: {name} (not in {', '.join(map(str, where)) or 'no packages folder given'}): version {v} "
                         "is offered without the single-core choice by this build")
            continue
        b = src.read_bytes()
        got = hashlib.sha256(b).hexdigest()
        if got != want:
            raise SystemExit(f"version {v}: {src} has sha256 {got}, web/releases.json says {want} (sha256_1core): refused")
        if check_package(src, b) != product:
            raise SystemExit(f"version {v}: {src} has the identity {product_of(b)}, the current package {product}: refused")
        one[v] = b
    return data, out, product, notes, one


def front(offered, version):
    """the version the landing page (its version line, Install sentence, Download button, command line) and the
    installer's own lines (its META version and package) present: VERSION, unless VERSION is a beta (releases.json
    "beta": true) and a stable version is offered; then the newest stable one. A beta is opt-in (BUILDING.md "A beta
    release"): it is in the installer's list, behind "Show beta releases", and nowhere else"""
    cur = next((r for r, _ in offered if r["version"] == version), None)
    if cur is None or not cur.get("beta"):
        return version
    return next((r["version"] for r, _ in offered if not r.get("beta")), version)


def emu_version(emu):
    """the version the browser emulator in EMU runs: its version.txt (tools/emu/web/build_web.sh writes the firmware's
    "ChoralRoot X.Y"), None when there is no such file"""
    f = Path(emu) / "version.txt"
    m = re.fullmatch(r"ChoralRoot\s+(\S+)", f.read_text(encoding="utf-8").strip()) if f.is_file() else None
    return m.group(1) if m else None


def emu_line(data, offered, emu):
    """{{EMU_NOTE}}: one short line under "Try it first" when the emulator runs a beta (the owner, 2026-10-14: nothing
    on the page may suggest the beta is what the installer writes), else nothing"""
    v = emu_version(emu)
    entry = next((r for r in data["releases"] if r["version"] == v), None)
    if not v or not (re.search(r"-beta\d+$", v) or (entry and entry.get("beta"))):
        return ""
    stable = next((r["version"] for r, _ in offered if not r.get("beta")), None)
    tail = f" (the installer's default is {html_escape(stable)})" if stable and stable != v else ""
    return f'<p class="note note-beta">It runs ChoralRoot {html_escape(v)}, a beta{tail}.</p>'


def news_html(data, offered, version):
    """the landing page's 'New in X': the current version's bullets, the links to its notes, the older versions.
    A beta (releases.json "beta": true) is opt-in in the installer: the landing page tells of the newest stable one"""
    cur = next((r for r, _ in offered if not r.get("beta")), offered[0][0])
    if cur is not offered[0][0]:
        version = cur["version"]
    esc = html_escape
    repo = data["repo"]
    if cur.get("bullets"):
        head = f'<h3 id="new">New in {esc(version)} <span class="sub">{esc(date_text(cur["date"]))}</span></h3>'
        body = '<ul class="news">' + "".join(f"<li>{esc(b)}</li>" for b in cur["bullets"]) + "</ul>"
        more = f'<a href="{esc(repo)}/blob/main/docs/releases/{esc(version)}.md">The full notes of {esc(version)}</a>. '
    else:
        head = f'<h3 id="new">Version {esc(version)} <span class="sub">a local build</span></h3>'
        body, more = "", ""
    older = [r for r, _ in offered if r is not cur and not r.get("beta")]
    if older:
        more += ("The installer also offers " + ", ".join(
            f'<a href="firmware/{esc(pkg_name(r["version"]))}" download>{esc(r["version"])}</a>' for r in older)
            + ', each with what going back means; <a href="firmware/">all files and checksums</a>. ')
    more += f'<a href="{esc(repo)}/releases">Older versions are on GitHub</a>.'
    return f'{head}\n      {body}\n      <p class="note">{more}</p>'


def firmware_index(data, offered, product, one=None):
    """firmware/index.html: every package with its checksum, its notes and its source (the tag), and the licences;
    a version's single-core package (one: version -> bytes) beside it"""
    esc, repo, rows, one = html_escape, data["repo"], [], one or {}
    for r, raw in offered:
        v, name = r["version"], pkg_name(r["version"])
        src = (f'<a href="{esc(repo)}/tree/{esc(r["tag"])}">source at {esc(r["tag"])}</a> '
               f'(<a href="{esc(repo)}/archive/refs/tags/{esc(r["tag"])}.zip">zip</a>) · '
               f'<a href="{esc(repo)}/blob/main/docs/releases/{esc(v)}.md">notes</a>'
               if r.get("tag") else "a local build of the working tree")
        rows.append(f'<li><a href="{esc(name)}" download>{esc(name)}</a> · {len(raw)} bytes · {esc(product)} · {src}'
                    f'<br><code>sha256 {esc(r["sha256"])}</code>'
                    + (f'<br><a href="{esc(pkg_name_1core(v))}" download>{esc(pkg_name_1core(v))}</a> · {len(one[v])} bytes · '
                       f'single core (for an FM-1 that crashes or freezes with the standard package)'
                       f'<br><code>sha256 {esc(r["sha256_1core"])}</code>' if v in one else "") + '</li>')
    return ('<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">'
            '<title>ChoralRoot FM-1 firmware files</title><h1>ChoralRoot FM-1: firmware files</h1>'
            '<p>The packages the <a href="../webapp/installer/">web installer</a> installs, for the command-line tool '
            '(<code>python3 tools/fm1_install.py FILE</code>). <a href="SHA256SUMS">SHA256SUMS</a> lists the same '
            'checksums.</p><ul>' + "".join(rows) + '</ul>'
            '<p>Every package here is free software under the GNU General Public License, version 3 only: '
            '<a href="LICENSE">LICENSE</a>. <a href="LICENSING.md">LICENSING.md</a> lists the parts under other '
            'licences and <a href="LICENSES/">LICENSES/</a> holds their texts; the three have covered every version '
            'listed here since 0.16 (the licence texts have not changed; LICENSING.md has gained credits). The '
            'complete corresponding source of each version is the repository at that version\'s tag, linked beside '
            f'it. {esc(data["minimum"]["note"])} <a href="{esc(repo)}/releases">Older versions are on GitHub</a>.</p>\n')


def smk_name(version, key, one=False):
    return f"choralroot-{re.sub(r'[^A-Za-z0-9.-]', '-', version)}{SMK_MODELS[key][1]}{'-1core' if one else ''}.fwsc"


def check_smk_package(path, raw, key):
    """an SMK-37 test package: its model's identity (_9xx) and Felucca's loader, or the build stops"""
    product = product_of(raw)
    if not re.fullmatch(re.escape(SMK_MODELS[key][0]) + r"_9\d\d", product):
        raise SystemExit(f"{path}: identity {product!r} is not ChoralRoot's {SMK_MODELS[key][0]} package ({SMK_MODELS[key][0]}_9xx)")
    if b"FELUCCA-LOADER-1" not in raw:
        raise SystemExit(f"{path}: no Felucca loader in it; the site ships only fm1pkg_make.py packages")
    return product


def smk_collect(smk, require_all):
    """the SMK-37 test builds the site offers, newest first: [(entry, {key: {std, one: (raw, sha256)}})], the lines to
    print. Every listed version needs all three models' packages (and their single-core ones where listed), each with
    the sha256 releases-smk37.json gives it"""
    if not SMK_RELEASES.is_file():
        return None, [], [f"no SMK page: {SMK_RELEASES} missing"]
    data = json.loads(SMK_RELEASES.read_text(encoding="utf-8"))
    out, notes = [], []
    for r in data["releases"]:
        v, files, missing = r["version"], {}, []
        if set(r["packages"]) != set(SMK_MODELS):
            raise SystemExit(f"{SMK_RELEASES}: {v} lists {sorted(r['packages'])}, not the three models {sorted(SMK_MODELS)}")
        for key, want in r["packages"].items():
            got = {}
            for one, field in ((False, "sha256"), (True, "sha256_1core")):
                if not want.get(field):
                    continue
                name = smk_name(v, key, one)
                src = smk / name if smk else None
                if src is None or not src.is_file():
                    missing.append(name)
                    continue
                raw = src.read_bytes()
                digest = hashlib.sha256(raw).hexdigest()
                if digest != want[field]:
                    raise SystemExit(f"SMK {v}: {src} has sha256 {digest}, web/releases-smk37.json says {want[field]}: refused")
                got["one" if one else "std"] = (raw, digest, check_smk_package(src, raw, key))
            files[key] = got
        if missing:
            where = f"not in {smk}" if smk else "no SMK packages folder given"
            if require_all:
                raise SystemExit(f"SMK {v}: {', '.join(missing)} missing ({where}). Every version of "
                                 f"web/releases-smk37.json must be published: download them from the release {r['tag']}, "
                                 "or take the version out of web/releases-smk37.json")
            notes.append(f"skipped: SMK {v} ({len(missing)} package(s) {where}; --smk-packages DIR or CR_SMK_PACKAGES_DIR)")
            continue
        out.append((r, files))
    return data, out, notes


SMK_EMU_LINK = ('<p class="small"><b><a href="emu/">Try it in the browser first</a></b>: the SMK-37 build in an emulator '
                '(the panel with the mouse or the computer keyboard; no keyboard needed).</p>')


def smk_site(out, collected, emu=None):
    """webapp/smk37/index.html, webapp/smk37/emu/ and firmware/smk37/ (see the top) from smk_collect's answer; -> the
    line to print"""
    data, offered, notes = collected
    emu = Path(emu or os.environ.get("SMK_EMU_DIR") or HERE.parent / "build" / "emu-web-smk37")
    has_emu = (emu / "index.html").is_file() and (emu / "choralroot-smk37.wasm").is_file()
    for line in notes:
        print(line)
    fw, inst = out / "firmware" / "smk37", out / "webapp" / "smk37"
    for old in fw.glob("*.fwsc") if fw.is_dir() else []:
        old.unlink()
    if not offered:                                   # (a site built again into the same folder: nothing of an earlier
        for d in (fw, inst):                          # build's SMK pages stays, its firmware/smk37/index.html would link
            if d.is_dir():                            # a webapp/smk37/ that is not written)
                shutil.rmtree(d)
        return "no webapp/smk37/ (no SMK-37 test build offered)"
    fw.mkdir(parents=True, exist_ok=True)
    inst.mkdir(parents=True, exist_ok=True)
    rels, sums, rows = [], [], []
    for r, files in offered:
        models = {}
        for key, got in files.items():
            raw, digest, product = got["std"]
            m = {"product": product, "pkg": "../../firmware/smk37/" + smk_name(r["version"], key), "sha256": digest, "size": len(raw)}
            (fw / smk_name(r["version"], key)).write_bytes(raw)
            sums.append(f"{digest}  {smk_name(r['version'], key)}\n")
            row = f'<li>{html_escape(smk_name(r["version"], key))} · {len(raw)} bytes · {html_escape(product)}<br><code>sha256 {digest}</code>'
            if "one" in got:
                raw1, d1, p1 = got["one"]
                if p1 != product:
                    raise SystemExit(f"SMK {r['version']} {key}: the single-core package's identity {p1} is not {product}")
                m.update({"pkg_1core": "../../firmware/smk37/" + smk_name(r["version"], key, True), "sha256_1core": d1,
                          "size_1core": len(raw1)})
                (fw / smk_name(r["version"], key, True)).write_bytes(raw1)
                sums.append(f"{d1}  {smk_name(r['version'], key, True)}\n")
                row += f'<br>{html_escape(smk_name(r["version"], key, True))} · {len(raw1)} bytes · single core<br><code>sha256 {d1}</code>'
            models[key] = m
            rows.append(row + "</li>")
        rels.append({**{k: x for k, x in r.items() if k != "packages"}, "models": models})
    (fw / "SHA256SUMS").write_text("".join(sums), encoding="utf-8")
    (fw / "index.html").write_text(
        '<!doctype html><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1">'
        '<meta name="robots" content="noindex, nofollow"><title>ChoralRoot SMK-37 test builds</title>'
        '<h1>ChoralRoot for the SMK-37: test builds</h1><p>Community test builds, community-tested and not tested by '
        'the maintainer (<a href="https://github.com/Quixotic7/ChoralRootFM1/blob/main/SMK37.md">SMK37.md</a>). '
        'Install with the <a href="../../webapp/smk37/">SMK-37 test installer</a> or '
        '<code>python3 tools/fm1_install.py FILE</code>. <a href="SHA256SUMS">SHA256SUMS</a>; the licences: '
        '<a href="../LICENSE">LICENSE</a>, <a href="../LICENSING.md">LICENSING.md</a>, <a href="../LICENSES/">LICENSES/</a>; '
        'the source of each version is the repository at its tag.</p><ul>' + "".join(rows) + "</ul>\n", encoding="utf-8")
    html = (HERE / "smk37.html").read_text(encoding="utf-8")
    for mark in ("/*LIB*/", "/*META*/", "<!--EMU-->"):
        if html.count(mark) != 1:
            raise SystemExit(f"smk37.html must contain {mark} once; update make_site.py")
    lib = "\n".join(strip_module((HERE / n).read_text(encoding="utf-8")) for n in SMK_LIB)
    meta = json.dumps({"repo": data["repo"], "releases": rels}, ensure_ascii=False,
                      separators=(",", ":")).replace("</", "<\\/")
    page = html.replace("/*LIB*/", lib).replace("/*META*/", meta).replace("<!--EMU-->", SMK_EMU_LINK if has_emu else "")
    (inst / "index.html").write_text(page, encoding="utf-8")
    if has_emu:
        (inst / "emu").mkdir(exist_ok=True)
        emu_files = sorted(f.name for f in emu.iterdir() if f.is_file() and not f.name.startswith("."))
        for n in emu_files:
            shutil.copy(emu / n, inst / "emu" / n)
        emu_note = f"webapp/smk37/emu/ ({', '.join(emu_files)})"
    else:
        emu_note = "no webapp/smk37/emu/"
        print(f"warning: {emu}/index.html or choralroot-smk37.wasm missing: the SMK page is made without its emulator "
              "(sh tools/emu/web/build_web.sh builds it)", file=sys.stderr)
    return (f"webapp/smk37/index.html ({len(page)} B, " + ", ".join(r["version"] for r, _ in offered) +
            f"), firmware/smk37/ ({len(sums)} packages, SHA256SUMS, index.html), {emu_note}")


SMK_LIB = ("fm1pkg.js", "fm1ota.js", "fm1backup.js")


def main(pkg, version, out, emu=None, packages=None, require_all=False, smk_packages=None):
    pkg, out = Path(pkg), Path(out)
    emu = Path(emu or os.environ.get("EMU_DIR") or HERE.parent / "build" / "emu-web")
    lay = Path(os.environ.get("LAYOUT_DIR") or HERE.parent / "build" / "layout-web")
    has_lay = all((lay / n).is_file() for n in LAYOUT_FILES)
    if packages is None:
        default = HERE.parent / "build" / "scratch" / "packages"
        packages = os.environ.get("CR_PACKAGES_DIR") or (default if default.is_dir() else None)
    packages = Path(packages) if packages else None
    if smk_packages is None:
        default = HERE.parent / "build" / "release-smk37"
        smk_packages = os.environ.get("CR_SMK_PACKAGES_DIR") or (default if default.is_dir() else None)
    smk_packages = Path(smk_packages) if smk_packages else None
    data, offered, product, notes, one = collect(pkg, version, packages, require_all)
    smk = smk_collect(smk_packages, require_all)    # (its refusals, too, before anything is written)
    html = (HERE / "index_pkg.html").read_text(encoding="utf-8")
    lib = strip_module((HERE / "fm1pkg.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((HERE / "fm1ota.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((HERE / "fm1backup.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((HERE / "fm1dnsnd.js").read_text(encoding="utf-8")) + "\n" + \
        strip_module((HERE / "fm1sounds.js").read_text(encoding="utf-8"))
    shown = front(offered, version)                 # (VERSION, or the newest stable one when VERSION is a beta)
    name = pkg_name(shown)
    meta = json.dumps({"version": shown, "product": product, "pkg": "../../firmware/" + name, "repo": data["repo"],
                       "minimum": data["minimum"],
                       "releases": [{**{k: x for k, x in r.items() if k != "sha256_1core"},
                                     "pkg": "../../firmware/" + pkg_name(r["version"]), "size": len(raw),
                                     **({"pkg_1core": "../../firmware/" + pkg_name_1core(r["version"]),
                                         "sha256_1core": r["sha256_1core"], "size_1core": len(one[r["version"]])}
                                        if r["version"] in one else {})}
                                    for r, raw in offered]},
                      ensure_ascii=False, separators=(",", ":")).replace("</", "<\\/")
    for mark in ("/*LIB*/", "/*META*/"):
        if html.count(mark) != 1:
            raise SystemExit(f"index_pkg.html must contain {mark} once; update make_site.py")
    index = fill({"VERSION": shown, "PKG": name, "PKG_URL": "firmware/" + name, "PRODUCT": product,
                  "NEWS": news_html(data, offered, version), "LAYOUT": LAYOUT_LINK if has_lay else "",
                  "EMU_NOTE": emu_line(data, offered, emu) if (emu / "index.html").is_file() else ""})
    html = html.replace("/*LIB*/", lib).replace("/*META*/", meta)
    inst, fw = out / "webapp" / "installer", out / "firmware"
    for d in (inst, fw):
        d.mkdir(parents=True, exist_ok=True)
    for old in [*fw.glob("choralroot-*.fwsc"), *fw.glob("felucca-*.fwsc")]:   # only the versions offered now (and
                                                                                # their single-core packages)
        old.unlink()
    (inst / "index.html").write_text(html, encoding="utf-8")
    mgr = manager_page()
    (inst / "manager.html").write_text(mgr, encoding="utf-8")
    packs_out = inst / "packs"
    packs_out.mkdir(exist_ok=True)
    pack_files = sorted((HERE / "packs").glob("*.json"))
    for f in pack_files:
        shutil.copy(f, packs_out / f.name)
    for r, raw in offered:
        (fw / pkg_name(r["version"])).write_bytes(raw)
        if r["version"] in one:
            (fw / pkg_name_1core(r["version"])).write_bytes(one[r["version"]])
    (fw / "SHA256SUMS").write_text("".join(f"{r['sha256']}  {pkg_name(r['version'])}\n"
                                           + (f"{r['sha256_1core']}  {pkg_name_1core(r['version'])}\n" if r["version"] in one else "")
                                           for r, _ in offered), encoding="utf-8")
    (fw / "index.html").write_text(firmware_index(data, offered, product, one), encoding="utf-8")
    lic = HERE.parent / "LICENSES"                  # the packages hold JieLi SDK files (Apache-2.0): their
    (fw / "LICENSES").mkdir(exist_ok=True)          # licence travels next to them, with Felucca's own
    names = sorted(f.name for f in lic.glob("*.txt"))
    for n in names:
        shutil.copy(lic / n, fw / "LICENSES" / n)
    (fw / "LICENSES" / "index.html").write_text(    # the pages link this folder: Pages lists no folders
        '<!doctype html><meta charset="utf-8"><title>ChoralRoot FM-1 licences</title><h1>Licence texts</h1><ul>'
        + "".join(f'<li><a href="{n}">{n}</a></li>' for n in names)
        + '</ul><p><a href="../LICENSING.md">LICENSING.md</a> · <a href="../LICENSE">LICENSE (GPL-3.0)</a> · '
          '<a href="../">the packages they cover, each with its source</a></p>\n',
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
    if has_lay:
        (out / "webapp" / "layout").mkdir(parents=True, exist_ok=True)
        lay_html = layout_page()
        (out / "webapp" / "layout" / "index.html").write_text(lay_html, encoding="utf-8")
        for n in LAYOUT_FILES:
            shutil.copy(lay / n, out / "webapp" / "layout" / n)
        lay_note = f"webapp/layout/ (index.html {len(lay_html)} B, {', '.join(LAYOUT_FILES)})"
    else:
        lay_note = "no webapp/layout/"
        print(f"warning: {lay}/crlayout.js or crlayout.wasm missing: the site is made without the layout viewer "
              "(webapp/layout/) and the landing page does not link it; build it with sh tools/layout/build_layout.sh",
              file=sys.stderr)
    smk_note = smk_site(out, smk)
    for line in notes:
        print(line)
    print(f"site: {out}: {', '.join(site)}; webapp/installer/index.html ({len(html)} B); "
          f"webapp/installer/manager.html ({len(mgr)} B, {len(pack_files)} pack files); "
          + ", ".join(f"firmware/{pkg_name(r['version'])} ({len(raw)} B, {product})"
                      + (f" + {pkg_name_1core(r['version'])} ({len(one[r['version']])} B)" if r["version"] in one else "")
                      for r, raw in offered)
          + f", firmware/index.html, firmware/SHA256SUMS, firmware/LICENSE, firmware/LICENSING.md, "
          f"firmware/LICENSES/ ({len(names)} texts + index.html); {emu_note}; {lay_note}; {smk_note}")


if __name__ == "__main__":
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("pkg", help="the current version's package (build/choralroot.fwsc, or the release's file)")
    ap.add_argument("version", help="its version, e.g. 0.19")
    ap.add_argument("out", help="the folder the site is written to")
    ap.add_argument("emu", nargs="?", help="the browser emulator's folder (default: EMU_DIR, build/emu-web)")
    ap.add_argument("--packages", metavar="DIR", help="the folder with the other versions' choralroot-X.Y.fwsc "
                    "(default: CR_PACKAGES_DIR, else build/scratch/packages when it exists)")
    ap.add_argument("--smk-packages", metavar="DIR", help="the folder with the SMK-37 test packages of "
                    "web/releases-smk37.json (default: CR_SMK_PACKAGES_DIR, else build/release-smk37 when it exists)")
    ap.add_argument("--require-all", action="store_true", help="stop when a version of web/releases.json has no "
                    "package, or the current version is not its newest (the Pages job)")
    a = ap.parse_args()
    main(a.pkg, a.version, a.out, a.emu, a.packages, a.require_all, a.smk_packages)
