#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
"""The mock-up states as C: design/choralroot-fm1-mockups.json -> tests/cr_screens_gen.h (run by tests/run_cr_draw.sh).

  gen_cr_screens.py [JSON] [OUT.h]

Each state's `screen` object becomes a cr_screen_t (firmware/src/cr_screen.h) the way the designer's renderScreen
(../ChoralRootFM1Designer/index.html) reads it: its defaults and fallbacks made explicit (a colour the designer takes
from a palette token becomes the named colour it is in the MOD palette: THEME red, ACCENT yellow, TEXT white),
its quirks kept (the geek view's name ignores `cols`; the text view's title is THEME). Only the device's kinds.

Each state also gets the animation that leads into it, for the mid-animation renders (tests/cr_draw_test.c draws
every state settled at anim_ms 0 with `anim` cleared, then with `anim` set at a mid value): a chord squeezes in from
the previous chord state's name, a picker slides in from the item before, a meter fills from empty, the idle stripes
run. CR_SCREEN_NAMES / CR_SCREEN_SLUGS name them (build/cr_screens/<nn>_<slug>.png).
"""
import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "design" / "choralroot-fm1-mockups.json"
OUT = Path(sys.argv[2]) if len(sys.argv) > 2 else ROOT / "tests" / "cr_screens_gen.h"

# the designer's MOD palette: names (and the CHORAL names mapped onto it) and tokens -> cr_col_t
NAMES = {"white": "WHITE", "cream": "WHITE", "red": "RED", "coral": "RED", "magenta": "RED", "blue": "BLUE",
         "teal": "BLUE", "navy": "BLUE", "yellow": "YELLOW", "orange": "ORANGE", "green": "GREEN", "mint": "GREEN",
         "grey": "GREY", "black": "BLACK",
         "theme": "RED", "accent": "YELLOW", "text": "WHITE", "mid": "MID", "dim": "DIM", "rec": "REC", "line": "LINE",
         "bg": "BG", "surf": "SURF"}
KIND = {"stripes": "STRIPES", "chord": "CHORD", "picker": "PICKER", "meter": "METER", "keyboard": "KEYBOARD",
        "arp": "ARP", "params": "PARAMS", "geek": "GEEK", "text": "TEXT", "big": "BIG", "scope": "SCOPE"}
ICON = {"none": "NONE", "play": "PLAY", "rec": "REC", "loop": "LOOP", "stop": "NONE"}
GLYPH = ["knob", "bar", "env", "wave", "saw", "square", "filter", "steps", "dots"]
NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
KEY_NAMES = [NOTE_NAMES[(53 + k) % 12] + str((53 + k) // 12 - 1) for k in range(27)]   # F3 .. G5


def col(name, fallback="NONE"):
    if not name:
        return f"CR_COL_{fallback}"
    n = str(name).lower()
    if n not in NAMES:
        raise SystemExit(f"gen_cr_screens: colour {name!r} has no cr_col_t")
    return f"CR_COL_{NAMES[n]}"


def cstr(s, size):
    """a C string literal of at most size - 1 bytes (Latin-1: the middle dot, the ellipsis as 0x85; octal escapes)"""
    s = str(s or "").replace("…", "\x85")
    b = bytes(ord(c) if ord(c) < 256 else ord("?") for c in s)
    if len(b) > size - 1:
        raise SystemExit(f"gen_cr_screens: {s!r} is longer than {size - 1} bytes")
    out = ""
    for c in b:
        if c == 34 or c == 92:
            out += "\\" + chr(c)
        elif 32 <= c < 127:
            out += chr(c)
        else:
            out += f"\\{c:03o}"
    return f'"{out}"'


def q8(v):
    return max(0, min(65535, int(round(float(v) * 256))))


def key_index(v):
    if isinstance(v, int) or str(v).isdigit():
        return int(v)
    return KEY_NAMES.index(str(v))


def name_init(p, cols, default_sup="RED"):
    cols = cols or {}
    root = col(cols.get("root"), "WHITE")
    return (f"{{{cstr(p.get('root'), 4)}, {cstr(p.get('quality'), 6)}, {cstr(p.get('sup'), 8)}, "
            f"{root}, {col(cols.get('quality')) if cols.get('quality') else root}, {col(cols.get('sup'), default_sup)}}}")


def notes_init(items):
    out = []
    for it in items[:8]:
        b = {"t": it} if isinstance(it, str) else it
        out.append(f"{{{cstr(b.get('t'), 6)}, {col(b.get('col'), 'WHITE')}, {1 if b.get('mark') else 0}}}")
    return out


def screen(sc, prev_name):
    """-> (list of designated initializer lines, the chord name it shows or None)"""
    f = []
    h = sc.get("header")
    if h is not None:
        f.append(".header = 1")
        f.append(f".icon = CR_ICON_{ICON[h.get('icon', 'none')]}")
        f.append(f".batt = {h.get('batt', 3) if h.get('batt') not in (None, False) else 255}")
        if h.get("mid"):
            f.append(f".mid = {cstr(h['mid'], 24)}")
        if h.get("right"):
            f.append(f".right = {cstr(h['right'], 16)}")
        f.append(f".mid_col = {col(h.get('midCol'), 'WHITE')}")
        bare = h.get("icon", "none") == "none" and not h.get("bpm")
        f.append(f".right_col = {col(h.get('rightCol'), 'WHITE' if bare else 'MID')}")
    else:
        f.append(".batt = 255")
    foot = sc.get("footer")
    if foot and foot.get("text"):
        f.append(f".footer = {cstr(foot['text'], 48)}")
    if sc.get("ring") is not None and sc.get("ring") is not False:     # (0: the track only)
        f.append(".ring_on = 1")
        f.append(f".ring = {q8(sc['ring'])}")
        if sc.get("ringRec"):
            f.append(".ring_rec = 1")
        f.append(f".ring_col = {col(sc.get('ringCol'), 'RED')}")
    if sc.get("message"):
        f.append(f".message = {cstr(sc['message'], 24)}")
        f.append(f".message_col = {col(sc.get('messageCol'), 'WHITE')}")
    p = sc.get("panel") or {}
    kind = p.get("kind", "text")
    if kind not in KIND:
        raise SystemExit(f"gen_cr_screens: panel kind {kind!r} is not on the device")
    f.append(f".kind = CR_K_{KIND[kind]}")
    anim, shown = [], None

    if kind == "stripes":
        bands = p.get("bands") or ["red", "white", "blue"]
        f.append(".bands = {" + ", ".join(col(b, "RED") for b in bands[:4]) + "}")
        f.append(f".n_bands = {min(4, len(bands))}")
        f.append(f".band = {p.get('band') or 16}")
        f.append(f".gap = {p.get('gap') or 8}")
        f.append(f".phase = {q8(p.get('phase') or 0)}")
        if p.get("skew"):
            f.append(f".skew = {int(round(p['skew'] * 256))}")
        if p.get("title"):
            f.append(f".title = {cstr(p['title'], 24)}")
        f.append(f".title_px = {p.get('titleSize') or 34}")
        f.append(f".title_col = {col(p.get('titleCol'), 'WHITE')}")
        if p.get("titleY") is not None:
            f.append(f".title_y = {int(p['titleY'])}")
        if p.get("sub"):
            f.append(f".foot = {cstr(p['sub'], 48)}")
        f.append(".period_ms = 2000")                      # one 4/4 bar at 120 BPM
        anim.append("CR_A_STRIPES")
    elif kind == "chord":
        cols = p.get("cols") or {}
        f.append(f".name = {name_init(p, cols)}")
        shown = (p.get("root"), p.get("quality"), p.get("sup"), json.dumps(cols, sort_keys=True))
        if p.get("squeeze"):
            f.append(f".squeeze = {q8(p['squeeze'])}")
        if p.get("block"):
            f.append(f".block = {col(p['block'], 'RED')}")
        if p.get("line"):
            f.append(f".line = {cstr(p['line'], 32)}")
        f.append(f".line_col = {col(p.get('lineCol'), 'MID')}")
        if p.get("bubbles"):
            n = notes_init(p["bubbles"])
            f.append(".note = {" + ", ".join(n) + "}")
            f.append(f".n_notes = {len(n)}")
        if prev_name and prev_name[:3] != shown[:3]:        # the squeeze from the chord before
            pr, pq, ps, pc = prev_name
            f.append(f".from = {name_init({'root': pr, 'quality': pq, 'sup': ps}, json.loads(pc))}")
            anim.append("CR_A_SQUEEZE")
        elif not prev_name:
            anim.append("CR_A_SQUEEZE")                   # out of nothing (the idle stripes swept off)
    elif kind == "picker":
        items = [it if isinstance(it, str) else (it or {}).get("t", "") for it in (p.get("items") or [])]
        n, sel = len(items), max(0, min(len(items) - 1, p.get("sel") or 0))
        first = max(0, min(sel - 3, n - 8))
        f.append(".item = {" + ", ".join(cstr(t, 24) for t in items[first:first + 8]) + "}")
        f.append(f".n_items = {n}")
        f.append(f".item0 = {first}")
        f.append(f".sel = {sel}")
        if p.get("orient") == "h":
            f.append(".orient = 1")
        f.append(f".col = {col(p.get('col'), 'WHITE')}")
        val = p.get("value")
        if val is None and sel < len(p.get("items") or []) and isinstance(p["items"][sel], dict):
            val = p["items"][sel].get("v")
        if val not in (None, ""):
            f.append(f".value = {cstr(val, 24)}")
        if p.get("label"):
            f.append(f".label = {cstr(p['label'], 32)}")
        if p.get("title"):
            f.append(f".title = {cstr(p['title'], 24)}")
        f.append(f".title_col = {col(p.get('titleCol'), 'MID')}")
        if p.get("size"):
            f.append(f".size = {p['size']}")
        if n > 1:
            f.append(f".slide = {1 if sel > 0 else -1}")
            anim.append("CR_A_SLIDE")
    elif kind == "meter":
        f.append(f".value = {cstr(p.get('value'), 24)}")
        if p.get("sub"):
            f.append(f".sub = {cstr(p['sub'], 12)}")
        if p.get("label"):
            f.append(f".label = {cstr(p['label'], 32)}")
        if p.get("title"):
            f.append(f".title = {cstr(p['title'], 24)}")
        f.append(f".col = {col(p.get('col'), 'RED')}")
        f.append(f".pct = {q8(p.get('pct') or 0)}")
        f.append(f".segments = {p.get('segments') or 12}")
        f.append(f".thick = {p.get('thick') or 14}")
        if p.get("size"):
            f.append(f".size = {p['size']}")
        anim.append("CR_A_FILL")                          # pct_from 0: from empty
    elif kind in ("keyboard", "geek"):
        if kind == "keyboard":
            if p.get("root"):
                f.append(f".name = {name_init(p, p.get('cols'))}")
            if p.get("title"):
                f.append(f".title = {cstr(p['title'], 24)}")
            f.append(f".title_px = {p.get('titleSize') or 15}")
            f.append(f".col = {col(p.get('col'), 'RED')}")
        else:
            f.append(f".name = {name_init(p, None)}")      # (the designer's geek view ignores cols: TEXT, THEME sup)
            notes = p.get("notes") or []
            notes = notes if isinstance(notes, list) else str(notes).split()
            f.append(".note = {" + ", ".join(notes_init([{"t": t, "col": "red" if i == len(notes) - 1 else "white"}
                                                          for i, t in enumerate(notes)])) + "}")
            f.append(f".n_notes = {min(8, len(notes))}")
            lines = p.get("lines") or []
            f.append(".lines = {" + ", ".join(f"{{{cstr(t, 32)}, 10, CR_COL_MID, 0, 0}}" for t in lines[:6]) + "}")
            f.append(f".n_lines = {min(6, len(lines))}")
        lit, cols = 0, ["CR_COL_NONE"] * 27
        for v in p.get("lit") or []:
            o = v if isinstance(v, dict) else {"k": v}
            k = key_index(o["k"])
            lit |= 1 << k
            if o.get("col"):
                cols[k] = col(o["col"])
        f.append(f".lit = 0x{lit:07x}u")
        f.append(".lit_col = {" + ", ".join(cols) + "}")
        labels = p.get("labels") or {}
        if labels:
            f.append(".key_label = {" + ", ".join(f"[{int(k)}] = {cstr(t, 3)}" for k, t in sorted(labels.items(),
                                                                                                key=lambda kv: int(kv[0]))) + "}")
    elif kind == "arp":
        f.append(f".name = {name_init(p, p.get('cols'))}")
        n = notes_init(p.get("notes") or [])
        f.append(".note = {" + ", ".join(n) + "}")
        f.append(f".n_notes = {len(n)}")
        f.append(f".pos = {p.get('pos', -1)}")
        f.append(f".hop_col = {col(p.get('hopCol'), 'MID')}")
        if p.get("line"):
            f.append(f".line = {cstr(p['line'], 32)}")
        f.append(f".line_col = {col(p.get('lineCol'), 'MID')}")
        if p.get("size"):
            f.append(f".size = {p['size']}")
    elif kind == "params":
        kc = ["blue", "orange", "white", "red"]
        cs = []
        for i, c in enumerate((p.get("cols") or [])[:4]):
            env = c.get("env") or [0.2, 0.3, 0.6, 0.3]
            cs.append(f"{{{cstr(c.get('label'), 12)}, {cstr(c.get('value'), 8)}, {col(c.get('col') or kc[i])}, "
                      f"CR_G_{(c.get('glyph') or 'knob').upper()}, {c.get('cycles') or 2}, {c.get('n') or 8}, "
                      f"{q8(c.get('pct', 0.5))}, {{{', '.join(str(min(255, q8(e))) for e in env)}}}, "
                      f"{cstr(c.get('src'), 6)}, {cstr(c.get('dst'), 6)}}}")
        f.append(".par = {" + ", ".join(cs) + "}")
        if p.get("title"):
            f.append(f".title = {cstr(p['title'], 24)}")
        f.append(f".col = {col(p.get('col'), 'WHITE')}")
        if p.get("page"):
            f.append(f".page = {cstr(p['page'], 16)}")
        if p.get("sections"):                              # (device: [how many, the current one])
            f.append(f".n_sect = {p['sections'][0]}")
            f.append(f".sect = {p['sections'][1]}")
        if p.get("slide"):                                 # (device: a page turned, the columns dealt in)
            f.append(f".slide = {p['slide']}")
            anim.append("CR_A_SLIDE")
        if p.get("foot"):
            f.append(f".foot = {cstr(p['foot'], 48)}")
    elif kind == "text":
        if p.get("title"):
            f.append(f".title = {cstr(p['title'], 24)}")
        f.append(".title_col = CR_COL_RED")                 # (the designer draws the title in THEME)
        ls = []
        for l in (p.get("lines") or [])[:6]:
            o = {"t": l} if isinstance(l, str) else l
            ls.append(f"{{{cstr(o.get('t'), 32)}, {o.get('px') or 12}, {col(o.get('col'), 'WHITE')}, "
                      f"{1 if (o.get('w') or 500) >= 600 else 0}, {1 if o.get('center') else 0}}}")
        f.append(".lines = {" + ", ".join(ls) + "}")
        f.append(f".n_lines = {len(ls)}")
    elif kind == "scope":
        f.append(f".col = {col(p.get('col'), 'WHITE')}")
        f.append(".wave = {" + ", ".join(str(int(v)) for v in scope_wave(p.get("freqs", []))) + "}")
    elif kind == "big":
        f.append(f".value = {cstr(p.get('value'), 24)}")
        if p.get("label"):
            f.append(f".label = {cstr(p['label'], 32)}")
        if p.get("sub"):
            f.append(f".sub = {cstr(p['sub'], 12)}")
        if p.get("title"):
            f.append(f".title = {cstr(p['title'], 24)}")
        if p.get("block"):
            f.append(f".block = {col(p['block'], 'RED')}")
        f.append(f".col = {col(p.get('col'), 'WHITE')}")
        f.append(f".size = {p.get('size') or 118}")
    if kind not in ("arp",):
        f.append(".pos = -1")
    f.append(".anim = " + (" | ".join(anim) if anim else "0"))
    return f, shown


def scope_wave(freqs):
    """the firmware's cr_ui.c cu_scope on a synthetic chord (sines at freqs Hz, the scope's 22.05 kHz): 240 samples
    from the steepest rising zero crossing, auto-scaled to -127..127 (no freqs: silence, a flat line)"""
    import math
    fs, n = 22050.0, 512
    x = [sum(math.sin(2 * math.pi * f * i / fs + k) for k, f in enumerate(freqs)) * 6000 for i in range(n)]
    x = [int(v) for v in x]
    peak = max([2048] + [abs(v) for v in x])
    trig, best = 0, 0
    for i in range(1, n - 240):
        if x[i - 1] < 0 <= x[i] and x[i] - x[i - 1] > best:
            best, trig = x[i] - x[i - 1], i
    return [int(x[trig + i] * 127 / peak) for i in range(240)]


# the VA's deep pages (cr_pages.c cp_dcolumn): 31 pages between EDIT 2 and ENV, nine sections
VA_SECT = 9
NONE_COL = {"label": "", "value": ""}
VA_FOOT = "SELECT: page \u00b7 OPT+SELECT: section"

# device-only states (no mock-up yet; not in the JSON): rendered and linted after the mock-up states
DEVICE_STATES = [
    {"name": "Device · Scope view (D major held)",
     "screen": {"header": {"mid": "D", "batt": False}, "panel": {"kind": "scope", "freqs": [293.66, 369.99, 440.0]}}},
    {"name": "Device · Scope view (silence)",
     "screen": {"header": {"mid": "", "batt": False}, "panel": {"kind": "scope"}}},
    {"name": "Device · Calibration step (press FX)",
     "screen": {"ring": 0, "ringCol": "yellow",
                "panel": {"kind": "big", "value": "FX", "sub": "1/21", "label": "press", "size": 40}}},
    {"name": "Device · Calibration step (turn KNOB 1)",
     "screen": {"ring": 17 / 21, "ringCol": "yellow",
                "panel": {"kind": "big", "value": "KNOB 1", "sub": "18/21", "label": "turn right", "size": 40}}},
    {"name": "Device · Calibration done",
     "screen": {"ring": 1, "ringCol": "yellow",
                "panel": {"kind": "big", "value": "done", "col": "green", "sub": "OCT+ keeps", "label": "OCT- discards",
                          "size": 40}}},
]

# .. then the deep pages (an engine's own, core.h eng_deep_t)
VA_STATES = [
    {"name": "Device · VA deep page OSC 1 (dealt in)",
     "screen": {"header": {"mid": "", "batt": 3}, "panel": {
         "kind": "params", "title": "VA BRASS*", "page": "OSC 1 \u00b7 4/39", "col": "white", "sections": [VA_SECT, 1],
         "slide": 1, "foot": VA_FOOT, "cols": [
             {"label": "Wave", "value": "SAW", "glyph": "saw", "cycles": 2, "pct": 0.5},
             {"label": "Level", "value": "80%", "glyph": "bar", "pct": 0.8},
             {"label": "Coarse", "value": "+12", "glyph": "knob", "pct": 0.75},
             {"label": "Fine", "value": "-7", "glyph": "knob", "pct": 0.43}]}}},
    {"name": "Device · VA deep page FILTER",
     "screen": {"header": {"mid": "", "batt": 3}, "panel": {
         "kind": "params", "title": "VA BRASS", "page": "FILTER \u00b7 12/39", "col": "white", "sections": [VA_SECT, 2],
         "foot": VA_FOOT, "cols": [
             {"label": "Type", "value": "BP", "glyph": "ftype", "n": 1},
             {"label": "Cutoff", "value": "2.4kHz", "glyph": "filter", "pct": 0.62},
             {"label": "Reso", "value": "35%", "glyph": "knob", "pct": 0.35},
             {"label": "Drive", "value": "20%", "glyph": "bar", "pct": 0.2}]}}},
    {"name": "Device · VA deep page ENV 2",
     "screen": {"header": {"mid": "", "batt": 3}, "panel": {
         "kind": "params", "title": "VA BRASS", "page": "ENV 2 \u00b7 16/39", "col": "white", "sections": [VA_SECT, 3],
         "foot": VA_FOOT, "cols": [
             {"label": "Attack", "value": "120ms", "glyph": "env", "env": [0.45, 0.35, 0.55, 0.6]},
             {"label": "Decay", "value": "300ms", "glyph": "knob", "pct": 0.35},
             {"label": "Sustain", "value": "55%", "glyph": "bar", "pct": 0.55},
             {"label": "Release", "value": "800ms", "glyph": "knob", "pct": 0.6}]}}},
    {"name": "Device · VA deep page MOD 3 (bass)",
     "screen": {"header": {"mid": "", "batt": 3}, "panel": {
         "kind": "params", "title": "SUB BASS", "page": "MOD 3 \u00b7 33/39", "col": "orange", "sections": [VA_SECT, 5],
         "foot": VA_FOOT, "cols": [
             {"label": "Source", "value": "LFO1", "glyph": "dots", "pct": 0.25},
             {"label": "Dest", "value": "CUT", "glyph": "dots", "pct": 0.4},
             {"label": "Amount", "value": "-40%", "glyph": "mod", "pct": 0.3, "src": "LFO1", "dst": "CUT"},
             NONE_COL]}}},
    {"name": "Device · VA deep page OSC 2 (triangle, noise)",
     "screen": {"header": {"mid": "", "batt": 3}, "panel": {
         "kind": "params", "title": "VA BRASS", "page": "OSC 2 \u00b7 5/39", "col": "white", "sections": [VA_SECT, 1],
         "foot": VA_FOOT, "cols": [
             {"label": "Wave", "value": "TRI", "glyph": "tri", "cycles": 2, "pct": 0.5},
             {"label": "Level", "value": "60%", "glyph": "bar", "pct": 0.6},
             {"label": "Wave", "value": "NOISE", "glyph": "noise", "pct": 0.5},
             {"label": "Wave", "value": "PULSE", "glyph": "square", "cycles": 2, "pct": 0.25}]}}},
]


def slug(name):
    name = name.split("·", 1)[-1]
    words = re.findall(r"[a-z0-9]+", name.lower())
    s = ""
    for w in words:
        if len(s) + len(w) + 1 > 30:
            break
        s = f"{s}_{w}" if s else w
    return s or "state"


def main():
    d = json.loads(SRC.read_text())
    d["states"] = list(d["states"]) + DEVICE_STATES + VA_STATES
    if d.get("palette") not in (None, "MOD"):
        print(f"gen_cr_screens: note: the design's palette is {d.get('palette')}; the device draws MOD")
    out = ["/* generated by tests/gen_cr_screens.py from design/choralroot-fm1-mockups.json: the mock-up states as",
           " * cr_screen_t (firmware/src/cr_screen.h); `anim` is the animation that leads into the state */",
           "#pragma once", "", f"#define CR_NSCREENS {len(d['states'])}u", "",
           "static const cr_screen_t CR_SCREENS[CR_NSCREENS] = {"]
    names, slugs, prev = [], [], None
    for i, st in enumerate(d["states"]):
        fields, shown = screen(st.get("screen") or {}, prev)
        if shown:
            prev = shown
        out.append(f"    {{   /* {i + 1}: {st.get('name', '')} */".replace("*/", "* /", st.get('name', '').count("*/")))
        out += [f"        {x}," for x in fields]
        out.append("    },")
        names.append(st.get("name", ""))
        slugs.append(f"{i + 1:02d}_{slug(st.get('name', ''))}")
    out.append("};")
    out.append("static const char *const CR_SCREEN_NAMES[CR_NSCREENS] = {")
    out += [f"    {cstr(n.replace(chr(0x2192), '->').replace(chr(0xb7), '-'), 120)}," for n in names]
    out.append("};")
    out.append("static const char *const CR_SCREEN_SLUGS[CR_NSCREENS] = {")
    out += [f'    "{s}",' for s in slugs]
    out += ["};", ""]
    OUT.write_text("\n".join(out))
    print(f"gen_cr_screens: {len(d['states'])} states -> {OUT}")


if __name__ == "__main__":
    main()
