#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The site (web/make_site.py) built from the device package, then every internal link walked.

  python3 tests/site_test.py          (from the repo root, after ./build.sh; skips without build/choralroot.fwsc)

Builds build/site from build/choralroot.fwsc as version "0.1-test" and checks: the landing page at the root with
every placeholder filled, the installer with the identity and the version in its metadata and the library
inlined, the package and the licence files next to it, no webapp/editor written; then, for every .html in
build/site, every href / src that is not http(s):, mailto:, data: or a bare #fragment resolves to a file (a
folder needs index.html), every url() in a .css file too, and every #fragment points at an id on its page.
"""
import json
import re
import subprocess
import sys
import time
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]
PKG = ROOT / "build" / "choralroot.fwsc"
OUT = ROOT / "build" / "site"
VERSION = "0.1-test"
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


def walk():
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


def main():
    if not PKG.is_file():
        print(f"skip: {PKG.relative_to(ROOT)} missing (run ./build.sh first)")
        sys.exit(0)
    editor = OUT / "webapp" / "editor"
    had_editor = editor.exists()
    t0 = time.time() - 1
    r = subprocess.run([sys.executable, str(ROOT / "web" / "make_site.py"), str(PKG), VERSION, str(OUT)],
                       capture_output=True, text=True)
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
    meta = None
    for m in re.finditer(r"\{[^{}]*\"pkg\"[^{}]*\}", ih):
        try:
            meta = json.loads(m.group(0))
            break
        except ValueError:
            pass
    ok(meta is not None and meta.get("version") == VERSION and meta.get("product") == "FM-1_920"
       and meta.get("pkg") == f"../../firmware/{NAME}", "installer: META JSON (version, product, pkg)")

    fw = OUT / "firmware" / NAME
    ok(fw.is_file() and fw.stat().st_size == PKG.stat().st_size, f"firmware/{NAME} ({PKG.stat().st_size} B)")
    ok(len(list((OUT / "firmware").glob("choralroot-*.fwsc"))) == 1, "firmware/: one package")
    ok(all((OUT / "firmware" / f).is_file() for f in ("LICENSE", "LICENSING.md", "LICENSES/index.html")),
       "firmware/: LICENSE, LICENSING.md, LICENSES/index.html")
    if had_editor:
        new = [p for p in editor.rglob("*") if p.is_file() and p.stat().st_mtime >= t0]
        ok(not new, "webapp/editor: an old one left as it was, nothing written")
    else:
        ok(not editor.exists(), "webapp/editor: not written")

    pages, count, bad, frags = walk()
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
