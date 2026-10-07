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
        "arp": "ARP", "geek": "GEEK", "text": "TEXT", "big": "BIG", "scope": "SCOPE", "edit8": "EDIT8",
        "stack": "STACK", "knobrow": "KNOBROW"}
ICON = {"none": "NONE", "play": "PLAY", "rec": "REC", "loop": "LOOP", "stop": "NONE"}
CELL_GLYPH = {"knob": "KNOB", "bar": "BAR", "wave": "WAVE", "saw": "SAW", "square": "SQUARE", "steps": "STEPS",
              "dots": "DOTS", "morph": "MORPH", "noise": "NOISE",
              # the parameter pictograms (FORMAT.md "cell glyphs")
              "room": "ROOM", "moon": "MOON", "echoes": "ECHOES", "lfo": "LFO", "clip": "CLIP", "spring": "SPRING",
              "mix": "MIX", "gate": "GATE", "range": "RANGE", "arrow": "ARROW", "shift": "SHIFT"}
FTYPE = {"LP": 0, "BP": 32, "HP": 64, "NOTCH": 96}      # the band's FTYPE position (0..127) of the designer's names


def ftype_pos(v):
    """the JSON's ftype ("LP" .. "NOTCH", or a position 0..127) -> the band's FTYPE position"""
    if isinstance(v, (int, float)):
        return int(v) & 127
    return FTYPE[str(v or "LP").upper()]
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
        f.append(f".footer = {cstr(foot['text'], 64)}")
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
    elif kind == "knobrow":
        # a layer screen: the picker's band (horizontal) over one row of the four knobs' cells (cell[0])
        items = [it if isinstance(it, str) else (it or {}).get("t", "") for it in (p.get("items") or [])]
        n, sel = len(items), max(0, min(len(items) - 1, p.get("sel") or 0))
        first = max(0, min(sel - 3, n - 8))
        f.append(".item = {" + ", ".join(cstr(t, 24) for t in items[first:first + 8]) + "}")
        f.append(f".n_items = {n}")
        f.append(f".item0 = {first}")
        f.append(f".sel = {sel}")
        f.append(".orient = 1")
        f.append(f".col = {col(p.get('col'), 'WHITE')}")
        if p.get("value") not in (None, ""):
            f.append(f".value = {cstr(p['value'], 24)}")
        if p.get("label"):
            f.append(f".label = {cstr(p['label'], 32)}")
        cells = ((p.get("cells") or []) + [None] * 4)[:4]
        f.append(".cell = {{" + ", ".join(cell_init(c) for c in cells) + "}}")
        f.append(".n_rows = 1")
        if p.get("hot") not in (None, ""):
            f.append(".hot_r = 1")
            f.append(f".hot_c = {int(p['hot'])}")
        if p.get("hotCol"):
            f.append(f".hot_col = {col(p['hotCol'])}")
        if n > 1:
            f.append(f".slide = {1 if sel > 0 else -1}")
            anim.append("CR_A_SLIDE")
    elif kind in ("edit8", "stack"):
        f += editor_fields(p, kind)
        if p.get("batt") is not None:                   # the editor's title line: the battery (the MIX screens)
            f = [x for x in f if not x.startswith(".batt =")] + [f".batt = {int(p['batt'])}"]
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


def q8c(v):
    """a cell's fill: Q8 of 255 (255 = full)"""
    return max(0, min(255, int(round(float(v) * 256))))


def cell_init(c):
    """an edit8 / stack / knobrow cell -> cr_cell_t {label, value, flags, glyph, pct, pct2}"""
    if not c:
        return "{\"\", \"\", 0, CR_G_NONE, 0, 0}"
    flags = ["CR_CF_ON"]
    if c.get("pct") is not None:
        flags.append("CR_CF_PCT")
    if c.get("bipolar"):
        flags.append("CR_CF_BIP")
    if c.get("mark"):                                   # (device: the matrix modulates it, the source's colour)
        flags.append(f"CR_CF_MARK(CR_COL_{c['mark'].upper()})")
    g = c.get("glyph")
    glyph = CELL_GLYPH[g] if g and g != "none" else "NONE"
    value = str(c.get("value") or "").replace("\u2013", "-")
    return (f"{{{cstr(c.get('label'), 10)}, {cstr(value, 9)}, {' | '.join(flags)}, CR_G_{glyph}, "
            f"{q8c(c.get('pct') if c.get('pct') is not None else 0.5)}, "
            f"{q8c(c.get('pct2') if c.get('pct2') is not None else 0.5)}}}")


def editor_fields(p, kind):
    """the sound editor's panels (FORMAT.md "Sound editor panels"): the cells, the title line, the wide band"""
    f = []
    if kind == "edit8":
        rows = [r for r in (p.get("rows") or [])[:2]]
        cells = [[(r[i] if i < len(r) else None) for i in range(4)] for r in rows]
    else:
        rows = (p.get("rows") or [])[:8]
        cells = [[((r or {}).get("cells") or [None] * 4)[i] if i < len((r or {}).get("cells") or []) else None
                  for i in range(4)] for r in rows]
        f.append(".head = {" + ", ".join(cstr(h, 10) for h in ((p.get("cols") or []) + [""] * 4)[:4]) + "}")
        f.append(".rlabel = {" + ", ".join(cstr((r or {}).get("label"), 3) for r in rows) + "}")
    f.append(".cell = {" + ", ".join("{" + ", ".join(cell_init(c) for c in row) + "}" for row in cells) + "}")
    f.append(f".n_rows = {len(cells)}")
    f.append(f".active = {int(p.get('active') or 0)}")
    hot = p.get("hot")
    if isinstance(hot, list) and len(hot) == 2:
        f.append(f".hot_r = {int(hot[0]) + 1}")
        f.append(f".hot_c = {int(hot[1])}")
    if p.get("hotCol"):
        f.append(f".hot_col = {col(p['hotCol'])}")
    if p.get("title"):
        f.append(f".title = {cstr(p['title'], 24)}")
    f.append(f".title_col = {col(p.get('titleCol'), 'NONE')}")
    if p.get("right"):
        f.append(f".page = {cstr(p['right'], 16)}")
    if p.get("tall"):
        f.append(".tall = 1")
    if p.get("fine"):
        f.append(".fine = 1")
    w = p.get("wide") if kind == "edit8" else None
    if w and w.get("type") == "env":
        seg = w.get("seg")
        vals = [q8c(w.get(k, 0)) for k in ("a", "h", "d", "s", "r")] + [0 if seg is None else int(seg) + 1]
        f.append(".wide = CR_W_ENV")
        f.append(".wv = {" + ", ".join(str(v) for v in vals) + "}")
    elif w and w.get("type") == "filter":
        vals = [q8c(w.get("cut", 0.5)), q8c(w.get("res", 0)), ftype_pos(w.get("ftype", "LP")),
                q8c(w.get("drive", 0))]
        f.append(".wide = CR_W_FILTER")
        f.append(".wv = {" + ", ".join(str(v) for v in vals) + "}")
    return f


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

# .. then the sound editor's (design/choralroot-fm1-sound-editor-mockups.json, the normative spec): these states
EDITOR_SRC = ROOT / "design" / "choralroot-fm1-sound-editor-mockups.json"
EDITOR_PICK = [1, 3, 4, 6, 8, 9, 11, 15, 16]


# .. and the editor's device-only screens (docs/EDITOR.md; not in the mock-ups yet): OSC screen 3, the oscillator mixer
# (one lane: the four LEVELs as tall bars), SHIFT latched ("fine" in the title line)
EDITOR_DEVICE = [
    {"name": "Editor device · OSC mixer (fine)",
     "screen": {"panel": {"kind": "edit8", "title": "LUSH PAD*", "right": "OSC \u00b7 MIX", "tall": True, "fine": True,
                          "active": 0, "hot": [0, 1],
                          "rows": [[{"label": "OSC 1", "value": "100%", "glyph": "bar", "pct": 1.0},
                                    {"label": "OSC 2", "value": "49%", "glyph": "bar", "pct": 0.49},
                                    {"label": "OSC 3", "value": "31%", "glyph": "bar", "pct": 0.31},
                                    {"label": "OSC 4", "value": "0%", "glyph": "bar", "pct": 0.0}]]}}},
]


# the device's FILTER pages (va3: FTYPE 0..127 merges TYPE and MORPH): row A CUT RES FTYPE FENV, row B KTRK (blank)
# SPREAD DRIVE; the mock-ups' filter states are mapped onto that order
def filter_rows(rows, ftype):
    a, b = (rows + [[], []])[:2]
    a = (a + [None] * 4)[:4]
    b = (b + [None] * 4)[:4]
    ty, cut, res, drive = a
    ktrk, fenv = b[0], b[1]
    ft = dict(ty or {}, label="F.type", value=["LP", "LP>BP", "BP", "BP>HP", "HP", "HP>NT", "NOTCH", "NT>LP"][
        (ftype >> 5) * 2 + (1 if ftype & 31 else 0)], glyph="dots", pct=ftype / 127)
    return [[cut, res, ft, fenv], [ktrk, None, {"label": "Spread", "value": "0%", "glyph": "bar", "pct": 0.0}, drive]]


EDITOR_DEVICE += [
    {"name": "Editor device · OSC modes (MORPH, NOISE)",
     "screen": {"panel": {"kind": "stack", "title": "MORPH PAD*", "right": "OSC 1 \u00b7 A",
                          "cols": ["Wave", "Level", "Coarse", "Fine"], "active": 0, "hot": [0, 0],
                          "rows": [{"label": "1", "cells": [{"value": "SIN>TRI", "glyph": "morph", "pct": 12 / 127},
                                                            {"value": "66%", "glyph": "bar", "pct": 0.52},
                                                            {"value": "0"}, {"value": "-6"}]},
                                   {"label": "2", "cells": [{"value": "SAW>RMP", "glyph": "morph", "pct": 60 / 127},
                                                            {"value": "60%", "glyph": "bar", "pct": 0.47},
                                                            {"value": "0"}, {"value": "+6"}]},
                                   {"label": "3", "cells": [{"value": "SQR>PLS", "glyph": "morph", "pct": 112 / 127},
                                                            {"value": "30%", "glyph": "bar", "pct": 0.24},
                                                            {"value": "-12"}, {"value": "0"}]},
                                   {"label": "4", "cells": [{"value": "VINYL", "glyph": "noise", "pct": 1.0},
                                                            {"value": "50%", "glyph": "bar", "pct": 0.39},
                                                            {"value": "0"}, {"value": "0"}]}]}}},
    {"name": "Editor device · OSC noises (WHITE, BROWN) and TRI",
     "screen": {"panel": {"kind": "stack", "title": "NOISE TEST", "right": "OSC 2 \u00b7 A",
                          "cols": ["Wave", "Level", "Coarse", "Fine"], "active": 1,
                          "rows": [{"label": "1", "cells": [{"value": "TRI", "glyph": "morph", "pct": 24 / 127},
                                                            {"value": "80%", "glyph": "bar", "pct": 0.8},
                                                            {"value": "0"}, {"value": "0"}]},
                                   {"label": "2", "cells": [{"value": "WHITE", "glyph": "noise", "pct": 0.0},
                                                            {"value": "40%", "glyph": "bar", "pct": 0.4},
                                                            {"value": "0"}, {"value": "0"}]},
                                   {"label": "3", "cells": [{"value": "BROWN", "glyph": "noise", "pct": 0.5},
                                                            {"value": "40%", "glyph": "bar", "pct": 0.4},
                                                            {"value": "0"}, {"value": "0"}]},
                                   {"label": "4", "cells": [{"value": "RMP>SQR", "glyph": "morph", "pct": 84 / 127},
                                                            {"value": "0%", "glyph": "bar", "pct": 0.0},
                                                            {"value": "0"}, {"value": "0"}]}]}}},
    {"name": "Editor device · FILTER morphing LP>BP",
     "screen": {"panel": {"kind": "edit8", "title": "WARM PAD*", "right": "FILTER", "active": 0, "hot": [0, 2],
                          "wide": {"type": "filter", "cut": 0.488, "res": 0.3, "ftype": 16, "drive": 0.0},
                          "rows": filter_rows([[None, {"label": "Cutoff", "value": "643 Hz", "glyph": "bar", "pct": 0.488},
                                                {"label": "Reso", "value": "30%", "glyph": "bar", "pct": 0.3},
                                                {"label": "Drive", "value": "0%", "glyph": "bar", "pct": 0.0}],
                                               [{"label": "Key trk", "value": "50%", "glyph": "bar", "pct": 0.5},
                                                {"label": "Env amt", "value": "+32", "glyph": "bar", "pct": 0.756,
                                                 "bipolar": True}]], 16)}}},
]


# the quick modulation mapping (docs/EDITOR.md): ENV held + KNOB 1 on FILTER, the hot Cutoff showing the amount with
# its source in the source's colour; the marks (ENV yellow, LFO red, several white) on the modulated cells; the OSC
# stack with LFO 1 on OSC 1's Level and Coarse
EDITOR_DEVICE += [
    {"name": "Editor device · FILTER modulated (ENV2 +12, marks)",
     "screen": {"panel": {"kind": "edit8", "title": "LUSH PAD*", "right": "FILTER", "active": 0, "hot": [0, 0],
                          "hotCol": "YELLOW",
                          "wide": {"type": "filter", "cut": 0.55, "res": 0.11, "ftype": 0, "drive": 0.0},
                          "rows": [[{"label": "Cutoff", "value": "ENV2 +12", "glyph": "bar", "pct": 0.55,
                                     "mark": "white"},
                                    {"label": "Reso", "value": "11%", "glyph": "bar", "pct": 0.11, "mark": "red"},
                                    {"label": "F.type", "value": "LP", "glyph": "dots", "pct": 0.0},
                                    {"label": "Env amt", "value": "+10", "glyph": "bar", "pct": 0.58, "bipolar": True,
                                     "mark": "yellow"}],
                                   [{"label": "Key trk", "value": "50%", "glyph": "bar", "pct": 0.5}, None,
                                    {"label": "Spread", "value": "0%", "glyph": "bar", "pct": 0.0},
                                    {"label": "Drive", "value": "0%", "glyph": "bar", "pct": 0.0, "mark": "blue"}]]}}},
    {"name": "Editor device · OSC stack modulated (marks)",
     "screen": {"panel": {"kind": "stack", "title": "LUSH PAD*", "right": "OSC 1 \u00b7 A",
                          "cols": ["Wave", "Level", "Coarse", "Fine"], "active": 0, "hot": [0, 1], "hotCol": "RED",
                          "rows": [{"label": "1", "cells": [{"value": "SAW", "glyph": "saw", "pct": 0.5},
                                                            {"value": "LFO1 +24", "glyph": "bar", "pct": 0.49,
                                                             "mark": "red"},
                                                            {"value": "0", "mark": "red"}, {"value": "-7"}]},
                                   {"label": "2", "cells": [{"value": "SAW", "glyph": "saw", "pct": 0.5},
                                                            {"value": "49%", "glyph": "bar", "pct": 0.49},
                                                            {"value": "0", "mark": "yellow"}, {"value": "+7"}]},
                                   {"label": "3", "cells": [{"value": "SAW", "glyph": "saw", "pct": 0.5},
                                                            {"value": "31%", "glyph": "bar", "pct": 0.31},
                                                            {"value": "+12"}, {"value": "+3"}]},
                                   {"label": "4", "cells": [{"value": "SAW", "glyph": "saw", "pct": 0.5},
                                                            {"value": "0%", "glyph": "bar", "pct": 0.0},
                                                            {"value": "0"}, {"value": "0"}]}]}}},
]


def editor_states():
    e = json.loads(EDITOR_SRC.read_text())["states"]
    out = []
    for k in EDITOR_PICK:
        st = json.loads(json.dumps(e[k - 1]))
        st["name"] = "Editor " + st.get("name", "")
        p = st["screen"]["panel"]
        w = p.get("wide") or {}
        if w.get("type") == "filter":
            p["rows"] = filter_rows(p.get("rows") or [], ftype_pos(w.get("ftype", "LP")))
            if p.get("hot") == [0, 1]:                  # (Cutoff turning: now KNOB 1)
                p["hot"] = [0, 0]
        if p.get("right") == "MIX":                     # the MIX screens: the battery in the title line
            p["batt"] = 3
        out.append(st)
    return out + EDITOR_DEVICE


# .. and the fx layer's (design/choralroot-fm1-fx-mockups.json, the user-approved spec of the knob row and its glyphs):
# the knob rows (2 Reverb, 3 Delay hot, 4 Chorus, 5 Drive off, 5b Spring) and the glyph studies (6, 7: edit8)
FX_SRC = ROOT / "design" / "choralroot-fm1-fx-mockups.json"
FX_PICK = ["2", "3", "4", "5", "5b", "6", "7"]


def fx_states():
    e = json.loads(FX_SRC.read_text())["states"]
    out = []
    for k in FX_PICK:
        st = next(x for x in e if str(x.get("name", "")).split(" ", 1)[0] == k)
        st = json.loads(json.dumps(st))
        st["name"] = "FX " + st.get("name", "")
        out.append(st)
    return out


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
    d["states"] = list(d["states"]) + DEVICE_STATES + editor_states() + fx_states()
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
