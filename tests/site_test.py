#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The site (web/make_site.py) built from the device package, then every internal link walked.

  python3 tests/site_test.py          (from the repo root, after ./build.sh; skips without build/choralroot.fwsc)

Builds build/site from build/choralroot.fwsc as version "0.1-test" and checks: the landing page at the root with
every placeholder filled, the installer with the identity and the version in its metadata and the library
inlined, the packages and the licence files next to them, no webapp/editor written; then, for every .html in
build/site, every href / src that is not http(s):, mailto:, data: or a bare #fragment resolves to a file (a
folder needs index.html), every url() in a .css file too, and every #fragment points at an id on its page.

The versions (web/releases.json): the other versions' packages come from CR_PACKAGES_DIR, else
build/scratch/packages (python3 tools/fetch_packages.py fills it; the network is needed for that only). Each one
found must be published with the listed sha256 and offered by the installer; each one missing is reported as
"skipped" and must not be offered. make_site.py's refusals are run on made-up files (a wrong sha256, a missing
version with --require-all, a version that is not listed). When build/choralroot.fwsc is itself a listed release
(the build is reproducible) and every package is there, the site is also built as the Pages job builds it
(--require-all, into build/scratch/site-release) and checked the same way.
The SMK-37 installer (web/releases-smk37.json, SMK37.md): built from made-up SMK packages (build/choralroot.fwsc
under the three models' identities) and a made-up list (CR_SMK_RELEASES_JSON): webapp/smk37/index.html with the three
models' packages in its META, firmware/smk37/ with each package, SHA256SUMS and index.html, linked from the landing
page (indexable) and from no FM-1 installer or firmware page;
a wrong sha256, an FM-1 package in an SMK model's place and a missing package with --require-all refused before
anything is written; a local build without the packages makes no SMK page and no landing-page link. With
build/emu-web-smk37 (build_web.sh): the SMK-37 emulator at webapp/smk37/emu/, linked from the SMK page and from no FM-1
page.
"""
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
from html import escape as html_escape
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
PKG = ROOT / "build" / "choralroot.fwsc"
OUT = ROOT / "build" / "site"
VERSION = "0.1-test"
RELEASES = json.loads((ROOT / "web" / "releases.json").read_text(encoding="utf-8"))
PACKAGES = Path(os.environ.get("CR_PACKAGES_DIR") or ROOT / "build" / "scratch" / "packages")
SCRATCH = ROOT / "build" / "scratch"
NAME = f"choralroot-{VERSION}.fwsc"
failed = 0


def ok(cond, what):
    global failed
    print(f"{what:<64} {'ok' if cond else 'FAIL'}")
    failed += not cond
    return cond


class Page(HTMLParser):
    """the links (href / src) and the ids of one page"""

    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.links, self.ids = [], set()

    def handle_starttag(self, tag, attrs):
        for k, v in attrs:
            if k == "id" and v:
                self.ids.add(v)
            elif k == "name" and tag == "a" and v:
                self.ids.add(v)
            elif k in ("href", "src") and v is not None:
                self.links.append((tag, k, v.strip()))

    handle_startendtag = handle_starttag


def parse(path):
    p = Page()
    p.feed(path.read_text(encoding="utf-8", errors="replace"))
    return p


def external(url):
    return re.match(r"(?i)(https?:|mailto:|data:|javascript:)", url) is not None or url.startswith("//")


def target(page, url):
    """the file a relative url on page names (a folder: its index.html), and its fragment"""
    parts = urlsplit(url)
    frag = unquote(parts.fragment)
    if not parts.path:
        return page, frag
    if parts.path.startswith("/"):
        return None, frag                           # site-absolute paths break on a project Pages URL
    t = (page.parent / unquote(parts.path)).resolve()
    if parts.path.endswith("/") or t.is_dir():
        t = t / "index.html"
    return t, frag


def walk(skip=(), OUT=OUT):
    pages = sorted(OUT.rglob("*.html"))
    ids = {}
    bad, frags, count = [], [], 0

    def page_ids(p):
        if p not in ids:
            ids[p] = parse(p).ids if p.suffix == ".html" and p.is_file() else set()
        return ids[p]

    for page in pages:
        for tag, attr, url in parse(page).links:
            if not url or external(url):
                continue
            count += 1
            t, frag = target(page, url)
            rel = page.relative_to(OUT).as_posix()
            if t is not None and any(d in t.parents for d in skip):
                continue
            if t is None or not t.is_file() or OUT.resolve() not in t.parents:
                bad.append(f"{rel}: <{tag} {attr}=\"{url}\">")
                continue
            if frag and t.suffix == ".html" and frag not in page_ids(t):
                frags.append(f"{rel}: <{tag} {attr}=\"{url}\"> (no id \"{frag}\")")
    for css in sorted(OUT.rglob("*.css")):
        for url in re.findall(r"url\(\s*['\"]?([^'\")]+)['\"]?\s*\)", css.read_text(encoding="utf-8")):
            if external(url):
                continue
            count += 1
            t, _ = target(css, url)
            if t is None or not t.is_file():
                bad.append(f"{css.relative_to(OUT).as_posix()}: url({url})")
    return pages, count, bad, frags


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read_meta(page):
    """the installer's metadata: one line, `const meta = {...};`"""
    m = re.search(r"^const meta = (\{.*\});$", page, re.M)
    try:
        return json.loads(m.group(1).replace("<\\/", "</")) if m else None
    except ValueError:
        return None


def make(args, env=None):
    return subprocess.run([sys.executable, str(ROOT / "web" / "make_site.py"), *map(str, args)],
                          capture_output=True, text=True, env=env)


def no_smk_env(env=None):
    """the FM-1's --require-all builds without the SMK-37 list (smk_site() checks that part on its own list): an empty
    web/releases-smk37.json, so that no SMK package of the tree's list is required of them"""
    f = SCRATCH / "site-smk-none.json"
    f.parent.mkdir(parents=True, exist_ok=True)
    f.write_text(json.dumps({"about": "none", "repo": RELEASES["repo"], "releases": []}), encoding="utf-8")
    return {**(env or os.environ), "CR_SMK_RELEASES_JSON": str(f)}


def versions(out, meta, want, stdout):
    """the multi-version layout: exactly the wanted packages under firmware/, each with its sha256, each offered by
    the installer with a link that resolves, SHA256SUMS and firmware/index.html agreeing"""
    fw = out / "firmware"
    names = [f"choralroot-{w['version']}.fwsc" for w in want]
    # a version that lists sha256_1core publishes its single-core package beside it, unless make_site.py said it skipped
    # that file (a local build without it)
    ones = {w["version"]: f"choralroot-{w['version']}-1core.fwsc" for w in want if w.get("sha256_1core")}
    skipped1 = [v for v, n in ones.items() if f"skipped: {n} (" in stdout]
    ones = {v: n for v, n in ones.items() if v not in skipped1}
    ok(sorted(p.name for p in fw.glob("*.fwsc")) == sorted(names + list(ones.values())),
       f"firmware/: {len(names)} package(s): {', '.join(w['version'] for w in want)}" + (f" (+ {len(ones)} single-core)" if ones else ""))
    ok(all((fw / n).is_file() and sha(fw / n) == w["sha256"] for n, w in zip(names, want)) and
       all((fw / ones[w["version"]]).is_file() and sha(fw / ones[w["version"]]) == w["sha256_1core"] for w in want if w["version"] in ones),
       "firmware/: every package has its listed sha256 (a single-core one its sha256_1core)")
    rel = (meta or {}).get("releases", [])
    ok([r.get("version") for r in rel] == [w["version"] for w in want] and all(r.get("sha256") == w["sha256"] for r, w in zip(rel, want)),
       "installer: offers exactly these versions, in this order, with these sha256")
    inst = out / "webapp" / "installer"
    ok(all((inst / r["pkg"]).resolve() == (fw / n).resolve() and (inst / r["pkg"]).is_file() and r.get("size") == (fw / n).stat().st_size
           for r, n in zip(rel, names)), "installer: every version's package link resolves to its file")
    stable0 = next(r["version"] for r in RELEASES["releases"] if not r.get("beta"))   # (a beta: no going-back note)
    ok(all(r.get("bullets") and r.get("date") and r.get("tag") for r in rel if r["version"] != VERSION)
       and all("going_back" in r for r in rel if r.get("tag") and not r.get("beta") and r["version"] != stable0),
       "installer: every listed version carries its bullets, date and tag; the older ones their going-back note")
    sums = (fw / "SHA256SUMS").read_text(encoding="utf-8").splitlines() if (fw / "SHA256SUMS").is_file() else []
    ok(sums == [x for n, w in zip(names, want) for x in [f"{w['sha256']}  {n}"] +
                ([f"{w['sha256_1core']}  {ones[w['version']]}"] if w["version"] in ones else [])], "firmware/SHA256SUMS lists them")
    idx = (fw / "index.html").read_text(encoding="utf-8") if (fw / "index.html").is_file() else ""
    ok(all(f'href="{n}"' in idx and w["sha256"] in idx for n, w in zip(names, want))
       and all(f'/tree/{r["tag"]}"' in idx for r in rel if r.get("tag")) and all(f'href="{x}"' in idx for x in ("LICENSE", "LICENSING.md", "LICENSES/")),
       "firmware/index.html: every package, its sha256, its source at its tag, the licences")
    ok(not any(f"choralroot-{v}.fwsc" in idx or (fw / f"choralroot-{v}.fwsc").exists() for v in ("0.14", "0.15")), "firmware/: 0.14 and 0.15 are not published")
    ok(stdout.count("skipped: ") - stdout.count("skipped: SMK ") == len(RELEASES["releases"]) - len([w for w in want if w["version"] != VERSION]) + len(skipped1),
       "make_site.py: one \"skipped:\" line for each listed version without a package")


def refusals():
    """make_site.py must stop before it writes anything: a package that is not the listed one, a missing version
    or an unlisted current version when everything is required"""
    listed = RELEASES["releases"]
    neg, out = SCRATCH / "site-neg-packages", SCRATCH / "site-neg"
    for d in (neg, out):                            # (made by this test, in build/scratch)
        if d.exists():
            shutil.rmtree(d)
    neg.mkdir(parents=True)
    victim = listed[-1]["version"]
    bad = bytearray(PKG.read_bytes())
    bad[-1] ^= 0xFF
    (neg / f"choralroot-{victim}.fwsc").write_bytes(bad)
    r = make([PKG, VERSION, out, "--packages", neg])
    ok(r.returncode != 0 and f"version {victim}" in r.stderr and "refused" in r.stderr and not out.exists(),
       f"make_site.py refuses a choralroot-{victim}.fwsc whose sha256 is not the listed one")
    (neg / f"choralroot-{victim}.fwsc").unlink()
    r = make([PKG, VERSION, out, "--packages", neg, "--require-all"])
    ok(r.returncode != 0 and "not in web/releases.json" in r.stderr and not out.exists(),
       "make_site.py --require-all refuses a version that is not in releases.json")
    if len(listed) > 1 and sha(PKG) == listed[0]["sha256"]:
        r = make([PKG, listed[0]["version"], out, "--packages", neg, "--require-all"])
        ok(r.returncode != 0 and f"version {listed[1]['version']}: choralroot-{listed[1]['version']}.fwsc is missing" in r.stderr and not out.exists(),
           f"make_site.py --require-all refuses a build without choralroot-{listed[1]['version']}.fwsc, naming it")
        r = make([PKG, listed[1]["version"], out, "--packages", neg])
        ok(r.returncode != 0 and "is not the released" in r.stderr and not out.exists(),
           "make_site.py refuses a current package whose sha256 is not its version's")
    else:
        print("skipped: the --require-all refusal of a missing older package (build/choralroot.fwsc is not the newest listed release)")
    foreign = bytearray(PKG.read_bytes())
    foreign[0x2F] = (ord("X") + 1) & 0xFF            # the identity's first marker byte: XM-1_920
    (neg / "foreign.fwsc").write_bytes(foreign)
    r = make([neg / "foreign.fwsc", VERSION, out, "--packages", neg])
    ok(r.returncode != 0 and "FM-1_9xx" in r.stderr and not out.exists(), "make_site.py refuses a package whose identity is not FM-1_9xx")


def one_core():
    """a release with a single-core package (releases.json sha256_1core, docs/DUALCORE.md "Two packages"): published
    under firmware/ beside the standard one, in SHA256SUMS and firmware/index.html, offered by the installer with its
    sha256; a file whose sha256 is not the listed one is refused; a local build without the file offers the version
    without the choice. A made-up list (CR_RELEASES_JSON) whose newest is 9.9 = build/choralroot.fwsc"""
    pk, out, lst = SCRATCH / "site-1core-packages", SCRATCH / "site-1core", SCRATCH / "site-1core-releases.json"
    for d in (pk, out):
        if d.exists():
            shutil.rmtree(d)
    pk.mkdir(parents=True)
    one = bytearray(PKG.read_bytes())
    one[-1] ^= 0x5A                                 # (another body, the same identity and loader)
    (pk / "choralroot-9.9-1core.fwsc").write_bytes(one)
    top = {**RELEASES["releases"][0], "version": "9.9", "tag": "v9.9", "sha256": sha(PKG),
           "sha256_1core": hashlib.sha256(one).hexdigest()}
    top.pop("going_back", None)
    top.pop("beta", None)                           # (9.9 is a stable version, whatever the first entry is)
    lst.write_text(json.dumps({**RELEASES, "releases": [top] + RELEASES["releases"]}), encoding="utf-8")
    env = no_smk_env({**os.environ, "CR_RELEASES_JSON": str(lst)})
    run = lambda *a: subprocess.run([sys.executable, str(ROOT / "web" / "make_site.py"), *map(str, a)], capture_output=True, text=True, env=env)
    r = run(PKG, "9.9", out, "--packages", pk)
    if not ok(r.returncode == 0, "1-core: make_site.py builds a release with a single-core package"):
        print(r.stdout + r.stderr)
        return
    fw = out / "firmware"
    f1 = fw / "choralroot-9.9-1core.fwsc"
    meta = read_meta((out / "webapp" / "installer" / "index.html").read_text(encoding="utf-8")) or {}
    m0 = (meta.get("releases") or [{}])[0]
    sums = (fw / "SHA256SUMS").read_text(encoding="utf-8")
    idx = (fw / "index.html").read_text(encoding="utf-8")
    ok(f1.is_file() and sha(f1) == top["sha256_1core"] and f"{top['sha256_1core']}  choralroot-9.9-1core.fwsc" in sums.splitlines() and
       'href="choralroot-9.9-1core.fwsc"' in idx and top["sha256_1core"] in idx,
       "1-core: firmware/choralroot-9.9-1core.fwsc, its sha256 in SHA256SUMS and firmware/index.html")
    inst = out / "webapp" / "installer"
    ok(m0.get("sha256_1core") == top["sha256_1core"] and m0.get("size_1core") == len(one) and m0.get("pkg_1core") and
       (inst / m0["pkg_1core"]).resolve() == f1.resolve() and all("sha256_1core" not in x and "pkg_1core" not in x for x in meta["releases"][1:]),
       "1-core: the installer offers it (pkg_1core resolves, its sha256 and size); the older versions have none")
    bad = bytearray(one)
    bad[-2] ^= 0xFF
    (pk / "choralroot-9.9-1core.fwsc").write_bytes(bad)
    shutil.rmtree(out)
    r = run(PKG, "9.9", out, "--packages", pk)
    ok(r.returncode != 0 and "sha256_1core" in r.stderr and "refused" in r.stderr and not out.exists(),
       "1-core: a single-core file whose sha256 is not the listed one is refused (nothing written)")
    (pk / "choralroot-9.9-1core.fwsc").unlink()
    r = run(PKG, "9.9", out, "--packages", pk)
    meta = read_meta((out / "webapp" / "installer" / "index.html").read_text(encoding="utf-8")) if r.returncode == 0 else {}
    ok(r.returncode == 0 and "skipped: choralroot-9.9-1core.fwsc" in r.stdout and not (out / "firmware" / "choralroot-9.9-1core.fwsc").exists() and
       "pkg_1core" not in meta["releases"][0] and "sha256_1core" not in meta["releases"][0],
       "1-core: a local build without the file offers 9.9 without the choice (\"skipped:\")")
    for d in (pk, out):
        shutil.rmtree(d)
    lst.unlink()


def beta_site():
    """a beta published (releases.json "beta": true; BUILDING.md "A beta release"): the landing page's version line,
    Install sentence, Download button, command line and "New in", and the installer's own version (its META, whose
    version the beta disclaimer names), are the newest stable version's; the beta is only in the installer's list. With
    a browser emulator that runs the beta, one line under "Try it first" says so; one that runs a stable: no line.
    A made-up list (CR_RELEASES_JSON): 9.9-beta1 = build/choralroot.fwsc, 9.8 its stable, as the Pages job builds it"""
    pk, out, lst, emu = (SCRATCH / "site-beta-packages", SCRATCH / "site-beta", SCRATCH / "site-beta-releases.json",
                         SCRATCH / "site-beta-emu")
    for d in (pk, out, emu):
        if d.exists():
            shutil.rmtree(d)
    pk.mkdir(parents=True)
    emu.mkdir(parents=True)
    old = bytearray(PKG.read_bytes())
    old[-1] ^= 0x33                                 # (another body, the same identity and loader)
    (pk / "choralroot-9.8.fwsc").write_bytes(old)
    base = {k: x for k, x in RELEASES["releases"][-1].items() if k not in ("going_back", "beta", "sha256_1core")}
    beta = {**base, "version": "9.9-beta1", "beta": True, "tag": "v9.9-beta1", "sha256": sha(PKG),
            "bullets": ["Something to try out early, behind the opt-in."]}
    stable = {**base, "version": "9.8", "tag": "v9.8", "sha256": hashlib.sha256(old).hexdigest(),
              "bullets": ["The stable one: what a visitor gets without the opt-in."]}
    lst.write_text(json.dumps({**RELEASES, "releases": [beta, stable]}), encoding="utf-8")
    env = no_smk_env({**os.environ, "CR_RELEASES_JSON": str(lst)})
    run = lambda *a: subprocess.run([sys.executable, str(ROOT / "web" / "make_site.py"), *map(str, a)], capture_output=True, text=True, env=env)
    (emu / "index.html").write_text("<!doctype html><title>emu</title>\n", encoding="utf-8")
    (emu / "version.txt").write_text("ChoralRoot 9.9-beta1\n", encoding="utf-8")
    r = run(PKG, "9.9-beta1", out, emu, "--packages", pk, "--require-all")
    if not ok(r.returncode == 0, "beta: make_site.py --require-all builds the site for 9.9-beta1"):
        print(r.stdout + r.stderr)
        return
    html = (out / "index.html").read_text(encoding="utf-8")
    meta = read_meta((out / "webapp" / "installer" / "index.html").read_text(encoding="utf-8")) or {}
    body = html.split("-->", 2)[-1]                 # (past the page's head comments, which name the placeholders)
    ok("Version 9.8:" in body and "Install 9.8 directly" in body and 'href="firmware/choralroot-9.8.fwsc" download>Download '
       '<span class="pkg">choralroot-9.8.fwsc</span>' in body and 'fm1_install.py <span class="nw">choralroot-9.8.fwsc</span>' in body
       and "New in 9.8" in body and "Version 9.9-beta1" not in body and "Install 9.9-beta1" not in body
       and 'href="firmware/choralroot-9.9-beta1.fwsc" download>Download' not in body,
       "beta: the landing page's version, Install, Download, command line and New in are 9.8's")
    rel = meta.get("releases") or []
    ok(meta.get("version") == "9.8" and meta.get("pkg") == "../../firmware/choralroot-9.8.fwsc" and
       [x.get("version") for x in rel] == ["9.9-beta1", "9.8"] and rel[0].get("beta") is True and
       (out / "firmware" / "choralroot-9.9-beta1.fwsc").is_file() and (out / "firmware" / "choralroot-9.8.fwsc").is_file(),
       "beta: the installer's own version is 9.8 (its disclaimer); 9.9-beta1 is in its list, marked beta, its file published")
    ok('<p class="note note-beta">It runs ChoralRoot 9.9-beta1, a beta (the installer\'s default is 9.8).</p>' in body,
       "beta: the emulator runs the beta: one line under Try it first says so")
    (emu / "version.txt").write_text("ChoralRoot 9.8\n", encoding="utf-8")
    shutil.rmtree(out)
    r = run(PKG, "9.9-beta1", out, emu, "--packages", pk, "--require-all")
    ok(r.returncode == 0 and "note-beta" not in (out / "index.html").read_text(encoding="utf-8"),
       "beta: an emulator that runs a stable version: no such line")
    for d in (pk, out, emu):
        shutil.rmtree(d)
    lst.unlink()


def release_site(cur):
    """the site as the Pages job builds it: the newest release as the current version, every listed version required"""
    out = SCRATCH / "site-release"
    r = make([PKG, cur["version"], out, "--packages", PACKAGES, "--require-all"], env=no_smk_env())
    if not ok(r.returncode == 0, f"make_site.py --require-all builds the site for {cur['version']} (build/scratch/site-release)"):
        print(r.stdout + r.stderr)
        return
    page = (out / "webapp" / "installer" / "index.html").read_text(encoding="utf-8")
    versions(out, read_meta(page), RELEASES["releases"], r.stdout)
    html = (out / "index.html").read_text(encoding="utf-8")
    news = next((r for r in RELEASES["releases"] if not r.get("beta")), cur)   # (a beta is opt-in: the newest stable's news)
    ok(f'New in {news["version"]}' in html and all(escape_html(b) in html for b in news["bullets"]) and 'href="webapp/installer/"' in html,
       f"index.html: \"New in {news['version']}\" with its bullets, and the button to the installer")
    # a beta is opt-in (BUILDING.md "A beta release"): in the installer's list only, no link on the landing page
    link = lambda x: f'href="firmware/choralroot-{x["version"]}.fwsc"' in html
    ok(all(link(x) for x in RELEASES["releases"] if not x.get("beta"))
       and not any(link(x) for x in RELEASES["releases"] if x.get("beta")),
       "index.html: a download link for every stable version, none for a beta")
    emu = (ROOT / "build" / "emu-web" / "index.html").is_file()
    pages, count, bad, frags = walk(() if emu else ((out / "emu").resolve(),), out)
    for b in bad + frags:
        print(f"  missing: {b}")
    ok(not bad and not frags, f"links (the release site): {count} internal href / src / url() in {len(pages)} pages resolve")


def escape_html(s):
    """as web/make_site.py writes a bullet (html.escape: quotes too, so a bullet with an apostrophe, 0.22's, matches)"""
    return html_escape(s)


def as_product(raw, product):
    """a package's 20 identity markers rewritten (make_site.py product_of): the site test's SMK-37 stand-ins"""
    b = bytearray(raw)
    for i in range(20):
        b[i * 0x30 + 0x2F] = (ord(product[i]) + i + 1) & 0xFF if i < len(product) else 0x7D
    return bytes(b)


def smk_site():
    pk, out, lst = SCRATCH / "site-smk-packages", SCRATCH / "site-smk", SCRATCH / "site-smk-releases.json"
    for d in (pk, out):
        if d.exists():
            shutil.rmtree(d)
    pk.mkdir(parents=True)
    raw, v, names, entry = PKG.read_bytes(), "9.9-smk37", {}, {}
    for key, (model, sfx) in {"pro": ("SMK-37 Pro", ""), "elite": ("SMK-37 Elite", "-elite"), "mke": ("MKE-P37", "-mke")}.items():
        std, one = as_product(raw, model + "_920"), bytearray(as_product(raw, model + "_920"))
        one[-1] ^= 0x5A
        names[key] = (f"choralroot-{v}{sfx}.fwsc", f"choralroot-{v}{sfx}-1core.fwsc", model + "_920")
        (pk / names[key][0]).write_bytes(std)
        (pk / names[key][1]).write_bytes(one)
        entry[key] = {"sha256": hashlib.sha256(std).hexdigest(), "sha256_1core": hashlib.sha256(one).hexdigest()}
    rel = {"version": v, "base": "9.9", "date": "2026-10-16", "tag": "v" + v, "title": "test", "bullets": ["b"], "notes": ["n"],
           "packages": entry}
    lst.write_text(json.dumps({"about": "test", "repo": RELEASES["repo"], "releases": [rel]}), encoding="utf-8")
    env = {**os.environ, "CR_SMK_RELEASES_JSON": str(lst)}
    run = lambda *a: subprocess.run([sys.executable, str(ROOT / "web" / "make_site.py"), *map(str, a)], capture_output=True, text=True, env=env)
    r = run(PKG, VERSION, out, "--packages", PACKAGES, "--smk-packages", pk)
    if not ok(r.returncode == 0, "SMK: make_site.py builds the site with the SMK-37 installer"):
        print(r.stdout + r.stderr)
        return
    page = out / "webapp" / "smk37" / "index.html"
    ph = page.read_text(encoding="utf-8") if page.is_file() else ""
    meta = read_meta(ph) or {}
    models = ((meta.get("releases") or [{}])[0]).get("models", {})
    fw = out / "firmware" / "smk37"
    ok(page.is_file() and "/*LIB*/" not in ph and "/*META*/" not in ph and 'name="robots"' not in ph,
       "SMK: webapp/smk37/index.html, the libraries and META inlined, indexable")
    land = (out / "index.html").read_text(encoding="utf-8") if (out / "index.html").is_file() else ""
    ok(land.count('href="webapp/smk37/"') == 1 and "{{" not in land,
       "SMK: the landing page links the SMK-37 installer once (its {{SMK}} cell)")
    ok(set(models) == {"pro", "elite", "mke"} and all(
        models[k]["product"] == names[k][2] and models[k]["sha256"] == entry[k]["sha256"] and
        models[k]["sha256_1core"] == entry[k]["sha256_1core"] and (page.parent / models[k]["pkg"]).resolve() == (fw / names[k][0]).resolve() and
        (page.parent / models[k]["pkg_1core"]).resolve() == (fw / names[k][1]).resolve() for k in models),
       "SMK: META lists the three models, each package's identity, sha256s and paths (they resolve)")
    sums = (fw / "SHA256SUMS").read_text(encoding="utf-8").splitlines() if (fw / "SHA256SUMS").is_file() else []
    ok(all((fw / n).is_file() and sha(fw / n) == entry[k][f] and f"{entry[k][f]}  {n}" in sums
           for k in names for n, f in ((names[k][0], "sha256"), (names[k][1], "sha256_1core"))) and len(sums) == 6 and
       (fw / "index.html").is_file(), "SMK: firmware/smk37/: the six packages, SHA256SUMS, index.html")
    others = [out / "webapp" / "installer" / "index.html", out / "webapp" / "installer" / "manager.html",
              out / "firmware" / "index.html"]
    emu_src = ROOT / "build" / "emu-web-smk37"
    if (emu_src / "index.html").is_file():
        eh = (out / "webapp" / "smk37" / "emu" / "index.html")
        ok(eh.is_file() and (eh.parent / "choralroot-smk37.wasm").is_file() and 'href="emu/"' in ph and
           'name="robots"' not in eh.read_text(encoding="utf-8"),
           "SMK: webapp/smk37/emu/ (the SMK-37 emulator, indexable), linked from the SMK page")
        others.append(out / "emu" / "index.html") if (out / "emu" / "index.html").is_file() else None
    else:
        print(f"skip: {emu_src.relative_to(ROOT)} missing: the SMK emulator not checked (sh tools/emu/web/build_web.sh)")
    ok(all("smk37" not in p.read_text(encoding="utf-8") for p in others) and
       not any(n.startswith("choralroot-9.9-smk37") for n in os.listdir(out / "firmware")),
       "SMK: no FM-1 installer or firmware page links it, the FM-1's firmware/ holds no SMK package")
    shutil.rmtree(out)
    bad = bytearray((pk / names["elite"][0]).read_bytes())
    bad[-3] ^= 0xFF
    (pk / names["elite"][0]).write_bytes(bad)
    r = run(PKG, VERSION, out, "--packages", PACKAGES, "--smk-packages", pk)
    ok(r.returncode != 0 and "refused" in r.stderr and not out.exists(), "SMK: a package whose sha256 is not the listed one: refused, nothing written")
    fm1 = PKG.read_bytes()
    (pk / names["elite"][0]).write_bytes(fm1)
    entry["elite"]["sha256"] = hashlib.sha256(fm1).hexdigest()
    lst.write_text(json.dumps({"about": "test", "repo": RELEASES["repo"], "releases": [{**rel, "packages": entry}]}), encoding="utf-8")
    r = run(PKG, VERSION, out, "--packages", PACKAGES, "--smk-packages", pk)
    ok(r.returncode != 0 and "SMK-37 Elite_9xx" in r.stderr and not out.exists(), "SMK: an FM-1 package in the Elite's place: refused, nothing written")
    (pk / names["elite"][0]).unlink()
    top = {**RELEASES["releases"][0], "version": "9.9", "tag": "v9.9", "sha256": sha(PKG)}   # (the FM-1's list: 9.9 alone,
    for k in ("going_back", "beta", "sha256_1core"):                                        # this build, so that its
        top.pop(k, None)                                                                    # own checks pass)
    fl = SCRATCH / "site-smk-fm1.json"
    fl.write_text(json.dumps({**RELEASES, "releases": [top]}), encoding="utf-8")
    r = subprocess.run([sys.executable, str(ROOT / "web" / "make_site.py"), str(PKG), "9.9", str(out), "--packages", str(pk),
                        "--smk-packages", str(pk), "--require-all"], capture_output=True, text=True, env={**env, "CR_RELEASES_JSON": str(fl)})
    fl.unlink()
    ok(r.returncode != 0 and names["elite"][0] in r.stderr and not out.exists(), "SMK: --require-all refuses a missing package, naming it")
    r = run(PKG, VERSION, out, "--packages", PACKAGES, "--smk-packages", pk)
    ok(r.returncode == 0 and "skipped: SMK 9.9-smk37" in r.stdout and not (out / "webapp" / "smk37").exists() and
       not list((out / "firmware").glob("smk37/*.fwsc")) and "smk37" not in (out / "index.html").read_text(encoding="utf-8"),
       "SMK: a local build without them: skipped, no SMK page, no landing-page link")
    for d in (pk, out):
        shutil.rmtree(d)
    lst.unlink()


def main():
    if not PKG.is_file():
        print(f"skip: {PKG.relative_to(ROOT)} missing (run ./build.sh first)")
        sys.exit(0)
    editor = OUT / "webapp" / "editor"
    had_editor = editor.exists()
    t0 = time.time() - 1
    r = make([PKG, VERSION, OUT, "--packages", PACKAGES])
    if not ok(r.returncode == 0, "make_site.py builds build/site"):
        print(r.stdout + r.stderr)
        return 1

    index = OUT / "index.html"
    html = index.read_text(encoding="utf-8") if index.is_file() else ""
    ok(index.is_file(), "index.html (the landing page) at the root")
    ok("{{" not in html and "}}" not in html, "index.html: every placeholder filled")
    ok(NAME in html and f'href="firmware/{NAME}"' in html, f"index.html: links the package {NAME}")
    ok('href="webapp/installer/"' in html, "index.html: links webapp/installer/")
    ok("http-equiv=\"refresh\"" not in html, "index.html: no redirect")
    ok((OUT / "site.css").is_file() and (OUT / "InterTight.woff2").is_file() and (OUT / "OFL.txt").is_file(),
       "site.css, InterTight.woff2, OFL.txt at the root")

    inst = OUT / "webapp" / "installer" / "index.html"
    ih = inst.read_text(encoding="utf-8") if inst.is_file() else ""
    ok(inst.is_file(), "webapp/installer/index.html written")
    ok("FM-1_920" in ih, "installer: identity FM-1_920")
    ok("/*LIB*/" not in ih and "/*META*/" not in ih, "installer: /*LIB*/ and /*META*/ replaced")
    meta = read_meta(ih)
    ok(meta is not None and meta.get("version") == VERSION and meta.get("product") == "FM-1_920"
       and meta.get("pkg") == f"../../firmware/{NAME}", "installer: META JSON (version, product, pkg)")

    fw = OUT / "firmware" / NAME
    ok(fw.is_file() and fw.stat().st_size == PKG.stat().st_size, f"firmware/{NAME} ({PKG.stat().st_size} B)")
    ok(all((OUT / "firmware" / f).is_file() for f in ("LICENSE", "LICENSING.md", "LICENSES/index.html")),
       "firmware/: LICENSE, LICENSING.md, LICENSES/index.html")
    ok('id="new"' in html and "a local build" in html, "index.html: the version block (a local build: no notes)")
    here = [r for r in RELEASES["releases"] if (PACKAGES / f"choralroot-{r['version']}.fwsc").is_file()]
    for x in RELEASES["releases"]:
        if x not in here:
            print(f"skipped: choralroot-{x['version']}.fwsc not in {PACKAGES}: version {x['version']} is not "
                  "published by this local build (python3 tools/fetch_packages.py; the Pages job has them all)")
    versions(OUT, meta, [{"version": VERSION, "sha256": sha(PKG)}] + here, r.stdout)
    refusals()
    one_core()
    beta_site()
    smk_site()
    cur =next((r for r in RELEASES["releases"] if r["sha256"] == sha(PKG)), None)
    if cur is not None and cur is RELEASES["releases"][0] and len(here) == len(RELEASES["releases"]):
        release_site(cur)
    else:
        print("skipped: the site as the Pages job builds it (--require-all): build/choralroot.fwsc is not the newest "
              "listed release, or a listed package is missing")
    if had_editor:
        new = [p for p in editor.rglob("*") if p.is_file() and p.stat().st_mtime >= t0]
        ok(not new, "webapp/editor: an old one left as it was, nothing written")
    else:
        ok(not editor.exists(), "webapp/editor: not written")

    emu_src = ROOT / "build" / "emu-web"
    if (emu_src / "index.html").is_file():
        ok((OUT / "emu" / "index.html").is_file() and (OUT / "emu" / "choralroot.wasm").is_file(),
           "emu/: index.html and choralroot.wasm copied")
        ok('href="emu/"' in html, "index.html: links emu/ (Try it in the browser)")
    else:
        print(f"skip: {emu_src.relative_to(ROOT)} missing: emu/ and the links to it not checked "
              "(sh tools/emu/web/build_web.sh)")
    skip = () if (emu_src / "index.html").is_file() else ((OUT / "emu").resolve(),)
    pages, count, bad, frags = walk(skip)
    for b in bad:
        print(f"  missing: {b}")
    ok(not bad, f"links: {count} internal href / src / url() in {len(pages)} pages resolve")
    for f in frags:
        print(f"  fragment: {f}")
    ok(not frags, "links: every #fragment names an id on its page")
    landing = parse(index) if index.is_file() else Page()
    ok("playing" in landing.ids and any(u == "#playing" for _, _, u in landing.links),
       "index.html: Manual -> #playing")
    return 1 if failed else 0


if __name__ == "__main__":
    rc = main()
    print("site: ALL OK" if rc == 0 and not failed else f"site: {failed} FAILED")
    sys.exit(rc)
