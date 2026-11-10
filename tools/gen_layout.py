#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
"""The layout generator (docs/LAYOUT-TOOL.md "Step 4, the firmware: what was built"): the designs the build ships,
compiled into the firmware's own tables:

  python3 tools/gen_layout.py [build/gen/cr_layouts.h] [--design ENGINE=path ..]   (a step of tools/build.py's generate
                                                                                    list; --design: other designs than
                                                                                    BUILD's, for the host tests)

What is built is this file's business, not a design's (rule 4): BUILD names one design an engine. A design is checked
against its engine's version 1 layout file (design/layouts/<engine>.json, which web/test_layout.mjs proves to be the
firmware's): the same params_hash, every stable id resolved through its pages to the built-in (page, col), or part:<id>
to (CR_LAY_PART, id). The rules are validateDesign's (web/fm1layout.js), in the same words, and stricter where the
firmware must be (validateDesign(d, cat, {build: true}) has these too, word for word: the designer's "not buildable"
reasons). Two forms, both what web/fm1layout.js packDesign packs for the preview (web/test_layout.mjs compares them
byte for byte through the module's lay_compiled):

- CLASSIC screens of OSC .. MOD: the screen plan (packDesign's "plan"): a page of its own per lane, each column a copy
  of the built-in descriptor it binds (core.h param_desc_t) with its built-in (page, col), the design's label and cell
  style; the screens (core.h eng_screen_t: one or two lanes, the band, the title); the sections. firmware/src/cr_pages.c
  cp_deep serves them as the engine's pages (get / set / desc / mod_dst through to the built-in (page, col)), so the
  editor (cr_edit.c) derives the screens from the plan. Edit8 screens of one or two lanes, a plan band, no part
  parameter on an engine page, no sketch glyph.
- FREE screens (cr_layout.h's records: an element 14 B, a screen 38 B, one string pool), drawn by cr_edit.c CE_S_FREE
  and cr_draw.c CR_K_FREE in the p5 style: the 4 px grid, the area under the title line, the budgets, a bound element
  in exactly one lane, a parameter once, a pair's halves touching, no band element. A built design with free screens
  sets CR_LAYOUTS_FREE 1 and the free-page editor and renderer are compiled into the unit; without, they are not (the
  layout tool's preview has them always, CR_LAYOUT_PREVIEW). A group is one kind or the other.

FX and MIX are the platform's, as today. Value names come from the layout file's descriptors. The header is written
only when its text changes."""
import json
import math
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BUILD = {"P5": "design/layouts/designs/p5-rows.json"}

# web/fm1layout.js, in its order
FORMAT, DESIGN_VERSION = "choralroot-layout", 2
GRID, SCREEN, HEADER_H = 4, 240, 28
MAX_LANES, MAX_ELEMENTS, LANE_SLOTS = 8, 24, 4
COLOURS = {"white": 1, "red": 2, "blue": 3, "yellow": 4, "orange": 5, "green": 6, "grey": 7, "black": 8, "bg": 9,
           "surf": 10, "text": 11, "theme": 12, "accent": 13, "mid": 14, "dim": 15, "line": 16}
GLYPHS = ["none", "knob", "bar", "wave", "saw", "square", "steps", "dots", "morph", "noise", "room", "moon", "echoes",
          "lfo", "clip", "mix", "gate", "range", "arrow", "shift", "harm", "detune", "lfowave", "ratio", "tail", "pre",
          "cut", "width", "spread", "sync", "ping"]
BANDS = ["none", "env", "filter", "dx", "cz", "algo", "ade2", "wave"]
PLAN_BAND = {"none": 0, "env": 1, "filter": 2, "algo": 4, "ade2": 5, "wave": 6}   # core.h ENG_B_* (fm1layout.js PLAN_BAND)
ENG_B = ["ENG_B_NONE", "ENG_B_ENV", "ENG_B_FILTER", "ENG_B_WINDOW", "ENG_B_ALGO", "ENG_B_ADE2"]
FMTS = ["int", "pct", "bipct", "time", "lfohz", "cutoff", "db", "semi", "enum", "bpm", "note", "onoff", "oct", "steps",
        "ofs", "fmnote", "fmfrq", "timei", "fix2", "inti", "glide"]              # core.h F_* (the catalog's "fmt", layout_web.c)
ELEMENT_TYPES = ["frame", "label", "knob", "switch", "led", "bar", "glyph", "band"]
BINDABLE = {"knob", "switch", "led", "bar", "glyph"}
LED_GLYPHS = ["saw", "square", "triangle", "sine", "noise"]
STYLES = ["flat", "p5"]
CLASSIC_KINDS = ["edit8", "stack"]
# a classic cell's look (core.h ENG_C_*, in its order; the catalog's cell "style"): the editor's rule .. and the
# Prophet-5's controls (the p5 cell glyphs, cr_screen.h CR_G_P5*)
CELL_STYLES = ["rule", "big", "ratio", "harm", "detune", "num", "val", "text", "knob", "bar", "waveph", "p5knob", "p5led",
               "p5led-saw", "p5led-tri", "p5led-sqr", "p5led2", "p5switch"]
# the sketch glyphs (web/fm1sketch.js SKETCH_NAMES, in its order): a cell style "sketch:<name>" (+ "-saw" "-tri" "-sqr"),
# drawn by the layout tool's page only: valid in a design, never built (docs/LAYOUT-TOOL.md "Sketch glyphs")
SKETCH_NAMES = ["k1", "k2", "k3", "k4", "k5", "k6", "k6a", "k6b", "k6c", "k6d", "k6e", "b1", "b2", "b3", "b4", "b5", "b1a",
                "b1b", "b1c", "b1d", "s1", "s2", "s3", "s4", "s5", "s6", "r1", "r2", "r3"]
SKETCH_RE = re.compile(r"^sketch:([a-z0-9]+)(?:-(saw|tri|sqr))?$")
FREE_GROUPS = {"OSC", "FILT", "ENV", "LFO", "MOD"}          # the engine's groups (0 .. 4); FX and MIX: the platform's
KNOB_P5 = (28, 44)                                          # the p5 knob's dial (KNOB_SIZES.p5)

# firmware/src/cr_layout.h
PART, NONE, NOSTR, EMPTY = 0xFF, 0xFE, 0xFFFF, 0xFF
F_LEDS2, F_BELOW, F_BOLD, F_NAMES = 0x0008, 0x0040, 0x0800, 0x1000
ELEM_B, SCREEN_B, HEAD_B = 14, 38, 8


def jround(x):
    """Math.round"""
    return math.floor(x + 0.5)


def is_int(v):
    return isinstance(v, int) and not isinstance(v, bool)


# ------------------------------------------------------------------ the catalog: design/layouts/<engine>.json ---
def layout_file(name):
    """fm1layout.js layoutFileName"""
    return re.sub(r"^-|-$", "", re.sub(r"[^a-z0-9]+", "-", name.lower())) + ".json"


def load_catalog(name):
    return json.loads((ROOT / "design" / "layouts" / layout_file(name)).read_text(encoding="utf-8"))


def param_list(cat):
    """fm1layout.js paramList: every parameter a design can bind, by stable id -> {page, col, label, names, ..}"""
    today, out, seen = {}, {}, {}
    for g in cat.get("groups") or []:
        for s in g["screens"]:
            for lane in s["lanes"]:
                for c in lane["cells"]:
                    k = ("d", c["page"], c["col"]) if c.get("bind") == "deep" else \
                        ("t", c["param"]) if c.get("bind") == "track" else None
                    if k and k not in today:
                        today[k] = c

    def uniq(i):
        seen[i] = seen.get(i, 0) + 1
        return f"{i}#{seen[i]}" if seen[i] > 1 else i
    for p in cat.get("pages") or []:
        for col, c in enumerate(p["cols"]):
            if not c:
                continue
            t = today.get(("d", p["page"], col))
            out[uniq(f"{p['title']}:{c['short']}")] = dict(
                page=p["page"], col=col, part=False, label=t["label"] if t else c["short"][:1] + c["short"][1:].lower(),
                fmt=c["fmt"], min=c["min"], max=c["max"], names=c.get("names"))
    for k, c in today.items():
        if k[0] == "t":
            out[uniq(f"part:{c['param']}")] = dict(page=PART, col=c["param"], part=True, label=c["label"], fmt=c["fmt"],
                                                   min=c["min"], max=c["max"], names=c.get("names"))
    return out


def today_cells(cat, params):
    """today's screens of a group as the design's cells (designFromCatalog): name -> [[[id or None] x 4] per lane]"""
    by = {("t", p["col"]) if p["part"] else ("d", p["page"], p["col"]): i for i, p in params.items()}
    key = lambda c: ("d", c["page"], c["col"]) if c.get("bind") == "deep" else ("t", c["param"]) if c.get("bind") == "track" else None
    return {g["name"]: [[[by.get(key(c)) for c in lane["cells"]] for lane in s["lanes"]] for s in g["screens"]]
            for g in cat.get("groups") or []}


# --------------------------------------------------------------------------------------------- validation ---
def has_key(v):
    """an object anywhere with a "key" (validateDesign: rule 2, the keys always play)"""
    if isinstance(v, dict):
        return "key" in v or any(has_key(x) for x in v.values())
    return isinstance(v, list) and any(has_key(x) for x in v)


def latin1(s):
    try:
        b = s.encode("latin-1")
    except UnicodeEncodeError:
        return None
    return None if b"\0" in b else b


def validate(d, cat, built=True):
    """validateDesign's rules (the same words), and with built the firmware's (the generator's). -> [errors]"""
    errors = []
    err = errors.append
    if not isinstance(d, dict):
        return ["not a JSON object"]
    if d.get("format") != FORMAT:
        err(f"not a {FORMAT} file")
    if d.get("version") != DESIGN_VERSION:
        err(f"format version {d.get('version')}: a design is version {DESIGN_VERSION}")
    if d.get("kind") != "design":
        err('"kind" must be "design"')
    eng = d.get("engine") if isinstance(d.get("engine"), dict) else {}
    if eng.get("index") != cat["engine"]["index"]:
        err(f"engine {eng.get('name')}: this catalog is {cat['engine']['name']}")
    if d.get("params_hash") != cat["params_hash"]:
        err(f"made for other parameters (params_hash {d.get('params_hash')}, the firmware's {cat['params_hash']})")
    style = d.get("style")
    if style is not None and style not in STYLES:
        err(f'style "{style}" ({", ".join(STYLES)}; none = flat)')
    elif built and style != "p5":
        err(f'style "{style or "flat"}": the firmware draws the p5 style only (a built design is "style": "p5")')
    if not isinstance(d.get("header"), dict) or d["header"].get("kind") != "title-line":
        err('"header" must be {"kind": "title-line"}: the shared header is the editor\'s title line')
    if not isinstance(d.get("groups"), list):
        err('"groups" must be a list')
        return errors
    params = param_list(cat)
    gnames = [g["name"] for g in cat["groups"]]
    today = today_cells(cat, params)
    seen_g, used = set(), {}
    for gi, g in enumerate(d["groups"]):
        where = f"group {gi + 1} ({g.get('name') if isinstance(g, dict) else None})"
        if not isinstance(g, dict) or g.get("name") not in gnames:
            err(f"{where}: not a group of {cat['engine']['name']} ({' '.join(gnames)})")
            continue
        if g["name"] in seen_g:
            err(f"{where}: the group twice")
        seen_g.add(g["name"])
        if not isinstance(g.get("screens"), list):
            err(f'{where}: "screens" must be a list')
            continue
        for si, s in enumerate(g["screens"]):
            at = f"{g['name']} screen {si + 1}"
            if not isinstance(s, dict) or s.get("type") not in ("classic", "free"):
                err(f'{at}: "type" must be "classic" or "free"')
                continue
            if not isinstance(s.get("title"), str):
                err(f'{at}: "title" must be a text')
            elif built and (latin1(s["title"]) is None or len(s["title"]) > 15):
                err(f'{at}: the title "{s["title"]}" is longer than 15 characters or not Latin-1')
            if has_key(s):
                err(f"{at}: nothing may be bound to a key (rule 2: the keys always play)")
            if s["type"] == "classic":
                mixed = any(isinstance(x, dict) and x.get("type") == "free" for x in g["screens"])
                classic(s, at, g["name"], si, params, today, built, err, used, mixed)
            else:
                free(s, at, g["name"], params, built, err, used)
    if built:
        for n in gnames:
            if n in FREE_GROUPS and not any(isinstance(g, dict) and g.get("name") == n and g.get("screens") for g in d["groups"]):
                err(f"{n}: no screens (a built design lays out every group of OSC .. MOD)")
        for p, where in used.items():
            if len(where) > 1:
                err(f"{p} is bound twice in the design ({', '.join(where)}): a parameter once")
    return errors


def classic(s, at, gname, si, params, today, built, err, used, mixed=False):
    if s.get("kind") not in CLASSIC_KINDS:
        err(f'{at}: kind "{s.get("kind")}" ({", ".join(CLASSIC_KINDS)})')
    if s.get("band") not in BANDS:
        err(f'{at}: band "{s.get("band")}" ({", ".join(BANDS)})')
    lanes = s.get("lanes")
    plan = built and gname in FREE_GROUPS                      # (OSC .. MOD: the screen plan's rules)
    if plan:
        if s.get("kind") in CLASSIC_KINDS and isinstance(lanes, list) and lanes and (s["kind"] != "edit8" or len(lanes) > 2):
            err(f"{at}: a {s['kind']} of {len(lanes)} lanes: the screen plan has edit8 screens of one or two lanes")
        if s.get("band") in BANDS and s["band"] not in PLAN_BAND:
            err(f'{at}: band "{s["band"]}": not a band of the screen plan ({", ".join(PLAN_BAND)})')
        if mixed:
            err(f"{at}: a classic screen beside free screens in {gname}: the unit's editor shows the group's free screens")
    if not isinstance(lanes, list) or not lanes or len(lanes) > MAX_LANES:
        err(f"{at}: 1 .. {MAX_LANES} lanes")
        return
    ok = True
    for li, lane in enumerate(lanes):
        if not isinstance(lane, dict) or not isinstance(lane.get("cells"), list) or len(lane["cells"]) != LANE_SLOTS:
            err(f"{at} lane {li + 1}: four cells (KNOB 1..4; null = empty)")
            ok = False
            continue
        if plan and lane.get("title") is not None and (not isinstance(lane["title"], str) or latin1(lane["title"]) is None):
            err(f"{at} lane {li + 1}: its title is a Latin-1 text")
        for ci, c in enumerate(lane["cells"]):
            if c is None or c.get("fixed"):
                continue
            if c.get("param") not in params:
                err(f'{at} lane {li + 1} knob {ci + 1}: no parameter "{c.get("param")}"')
                ok = False
            else:
                used.setdefault(c["param"], []).append(at)
                if plan and params[c["param"]]["part"]:
                    err(f"{at} lane {li + 1} knob {ci + 1}: {c['param']}: a part parameter on an engine page")
            if c.get("label") is not None and not isinstance(c["label"], str):
                err(f"{at} lane {li + 1} knob {ci + 1}: a label is a text")
            elif plan and isinstance(c.get("label"), str) and latin1(c["label"]) is None:
                err(f"{at} lane {li + 1} knob {ci + 1}: its label is a Latin-1 text")
            st = c.get("style")
            if isinstance(st, str) and st.startswith("sketch:"):   # a sketch glyph: the page draws it, no build has it
                m = SKETCH_RE.match(st)
                if not m or m.group(1) not in SKETCH_NAMES:
                    err(f'{at} lane {li + 1} knob {ci + 1}: style "{st}" (the sketch glyphs: {", ".join(SKETCH_NAMES)}; '
                        f'a wave -saw -tri -sqr)')
                elif built:
                    err(f"{at} lane {li + 1} knob {ci + 1}: not buildable yet: a sketch glyph ({m.group(1)}); "
                        f"port it into the firmware to build")
            elif st is not None and st not in CELL_STYLES:
                err(f'{at} lane {li + 1} knob {ci + 1}: style "{st}" ({", ".join(CELL_STYLES)})')
    if built and ok and gname not in FREE_GROUPS:
        t = today.get(gname, [])
        mine = [[(c or {}).get("param") if not (c or {}).get("label") else "label" for c in lane["cells"]] for lane in lanes]
        if si >= len(t) or mine != t[si]:
            err(f"{at}: today's platform screen: its cells cannot be moved by a design yet")


def free(s, at, gname, params, built, err, used):
    if built and gname not in FREE_GROUPS:
        err(f"{at}: a free screen in {gname}: FX and MIX are the platform's")
    els, lanes = s.get("elements"), s.get("lanes")
    if not isinstance(els, list):
        err(f'{at}: "elements" must be a list')
        return
    if not isinstance(lanes, list):
        err(f'{at}: "lanes" must be a list')
        return
    ids = {}
    for ei, e in enumerate(els):
        et = f"{at} element {ei + 1} ({e.get('id') if isinstance(e, dict) else None})"
        if not isinstance(e, dict) or not isinstance(e.get("id"), str) or not e["id"]:
            err(f'{et}: an "id" text')
            continue
        if e["id"] in ids:
            err(f"{et}: the id twice")
        ids[e["id"]] = e
        ty = e.get("type")
        if ty not in ELEMENT_TYPES:
            err(f'{et}: type "{ty}" ({", ".join(ELEMENT_TYPES)})')
            continue
        box = [e.get(k) for k in "xywh"]
        for k, v in zip("xywh", box):
            if not is_int(v) or v < 0:
                err(f"{et}: {k} a whole number >= 0")
        boxok = all(is_int(v) for v in box)
        if boxok:
            x, y, w, h = box
            if built and any(v % GRID for v in box):
                err(f"{et}: not on the {GRID} px grid")
            if built and (y < HEADER_H or x + w > SCREEN or y + h > SCREEN or not w or not h):
                err(f"{et}: outside the design area (under the header, rows {HEADER_H} .. 239)")
        p = None
        if ty in BINDABLE and e.get("param") is not None:
            p = params.get(e["param"])
            if p is None:
                err(f'{et}: no parameter "{e["param"]}"')
            else:
                used.setdefault(e["param"], []).append(f"{at} {e['id']}")
        elif built and ty in BINDABLE and ty != "glyph":
            err(f"{et}: a {ty} bound to nothing")
        if ty == "switch" and e.get("positions") not in (2, 3):
            err(f"{et}: a switch has 2 or 3 positions")
        if ty == "led" and "labelAt" in e and e["labelAt"] not in ("above", "below"):
            err(f'{et}: "labelAt" is "above" or "below"')
        if ty == "frame" and "r" in e and not (is_int(e["r"]) and 0 <= e["r"] <= 12):
            err(f'{et}: a frame\'s corner radius "r" is 0 .. 12')
        if ty == "led" and "leds" in e and e["leds"] not in (1, 2):
            err(f'{et}: "leds" is 1 or 2')
        if ty == "led" and e.get("leds") == 2:
            if p and p["max"] - p["min"] != 1:
                err(f"{et}: two LEDs show a parameter of two values ({e['param']}: {p['min']} .. {p['max']})")
            if e.get("glyph"):
                err(f"{et}: two LEDs have their names above them, no glyph")
        if ty == "led" and "pair" in e:
            if e["pair"] not in ("left", "right"):
                err(f'{et}: "pair" is "left" or "right"')
            elif built and boxok:
                other = "right" if e["pair"] == "left" else "left"
                if not any(isinstance(o, dict) and o.get("type") == "led" and o.get("pair") == other and
                           o.get("y") == e.get("y") and o.get("h") == e.get("h") and
                           (e["x"] + e["w"] == o.get("x") if e["pair"] == "left" else
                            is_int(o.get("x")) and is_int(o.get("w")) and o["x"] + o["w"] == e.get("x")) for o in els):
                    err(f"{et}: a {e['pair']} half with no {other} half touching it (same y and h): its cap stands alone")
        if ty == "led" and e.get("glyph") and e["glyph"] not in LED_GLYPHS:
            err(f'{et}: led glyph "{e["glyph"]}" ({", ".join(LED_GLYPHS)})')
        if ty == "glyph" and e.get("glyph") not in GLYPHS:
            err(f'{et}: glyph "{e.get("glyph")}"')
        if ty == "band":
            if e.get("band") not in BANDS:
                err(f'{et}: band "{e.get("band")}"')
            if built:
                err(f"{et}: a band element: not built (the bands are the classic screens')")
        if "colour" in e and e["colour"] != "auto" and e["colour"] not in COLOURS:
            err(f'{et}: colour "{e["colour"]}" is not a palette token')
        if built:
            if ty == "knob" and "size" in e and not (is_int(e["size"]) and KNOB_P5[0] <= e["size"] <= KNOB_P5[1]):
                err(f"{et}: a knob's dial is {KNOB_P5[0]} .. {KNOB_P5[1]}")
            if ty == "label" and "size" in e and not (is_int(e["size"]) and 6 <= e["size"] <= 32):
                err(f"{et}: a label's size is 6 .. 32 px")
            if ty == "label" and e.get("align", "left") not in ("left", "centre", "right"):
                err(f'{et}: "align" is "left", "centre" or "right"')
            if "names" in e and not (isinstance(e["names"], list) and all(isinstance(n, str) and n for n in e["names"])):
                err(f'{et}: "names" is a list of texts')
            for k in ("label", "text"):
                if e.get(k) is not None and (not isinstance(e[k], str) or latin1(e[k]) is None):
                    err(f"{et}: its {k} is a Latin-1 text")
    if built:
        if len(els) > MAX_ELEMENTS:
            err(f"{at}: {len(els)} elements: at most {MAX_ELEMENTS}")
        if not lanes or len(lanes) > MAX_LANES:
            err(f"{at}: {len(lanes)} lanes: 1 .. {MAX_LANES}")
    seen = {}
    for li, lane in enumerate(lanes):
        if not isinstance(lane, list) or (built and len(lane) != LANE_SLOTS):
            err(f"{at} lane {li + 1}: a list of element ids" + (" (four: KNOB 1..4; null = empty)" if built else ""))
            continue
        for i in lane:
            if i is None:
                continue
            if i not in ids:
                err(f'{at} lane {li + 1}: no element "{i}"')
            elif built:
                e = ids[i]
                if not (e.get("type") in BINDABLE and e.get("param") is not None):
                    err(f"{at} lane {li + 1}: {i} binds no parameter")
                if i in seen:
                    err(f"{at}: {i} is in two lanes")
                seen[i] = li
    if built:
        for i, e in ids.items():
            if e.get("type") in BINDABLE and e.get("param") is not None and i not in seen:
                err(f"{at}: {i} ({e['param']}) is in no lane: no knob reaches it")


def warnings(d):
    """the design checks that warn and do not refuse (web/fm1layout.js checkScreen's, the same words, after the screen):
    "order", a free lane whose bound slots' x goes back (KNOB 1..4 should follow the screen left to right) -> [texts]"""
    out = []
    for g in d.get("groups") or []:
        for si, s in enumerate(g.get("screens") or [] if isinstance(g, dict) else []):
            if not isinstance(s, dict) or s.get("type") != "free":
                continue
            ids = {e.get("id"): e for e in s.get("elements") or [] if isinstance(e, dict)}
            for li, lane in enumerate(s.get("lanes") or []):
                xs = [ids[i]["x"] for i in (lane if isinstance(lane, list) else []) if i is not None and i in ids and is_int(ids[i].get("x"))]
                if any(b < a for a, b in zip(xs, xs[1:])):
                    out.append(f"{g.get('name')} screen {si + 1}: lane {li + 1}: its slots are not left to right "
                               "(KNOB 1..4 should follow the screen)")
    return out


# ------------------------------------------------------------------------------------------------ records ---
class Pool:
    """the string pool: each run once (a label, or a label and its value names up to an empty string)"""

    def __init__(self):
        self.data, self.at = bytearray(), {}

    def add(self, *strings):
        if any(latin1(s) is None for s in strings):
            raise ValueError([f"a text that is not Latin-1: {strings}"])
        run = b"".join(latin1(s) + b"\0" for s in strings)
        if run not in self.at:
            if len(self.data) + len(run) >= NOSTR:
                raise ValueError("the string pool is over 64 KB")
            self.at[run] = len(self.data)
            self.data += run
        return self.at[run]


def switch_name(e, p, n, i):
    """fm1layout.js switchName"""
    v = p["min"] + jround(i * (p["max"] - p["min"]) / (n - 1)) if p else i
    names = e.get("names") or []
    if i < len(names) and names[i]:
        return str(names[i])
    if p and p.get("names"):
        k = v - (p["min"] if p["fmt"] == "enum" else 0)
        if 0 <= k < len(p["names"]) and p["names"][k]:
            return str(p["names"][k])
    return ["Off", "On"][i] if n == 2 else ["Lo", "Mid", "Hi"][i]


def element(e, p, pool, offset=0):
    """one element's record (cr_layout.h cr_lay_elem_t), its texts resolved as the p5 look draws them; offset: the
    built-in pages' place in a classic layout's table (packFree's offset), 0 when the engine's own are served"""
    ty = e["type"]
    page, col = ((PART if p["part"] else p["page"] + offset), p["col"]) if p else (NONE, 0)
    size = r = flags = 0
    colour = COLOURS.get(e.get("colour"), 0)
    lab = e["label"] if e.get("label") is not None else p["label"] if p else ""
    names = []
    if ty == "frame":
        lab, r = (e.get("text") or "").upper(), e.get("r", 3)      # (p5: the title in capitals; r 3 by default)
    elif ty == "label":
        lab, size = e.get("text") or "", e.get("size") or 11
        flags |= {"centre": 1, "right": 2}.get(e.get("align"), 0) << 7
        flags |= 0 if e.get("bold") is False else F_BOLD
    elif ty == "knob":
        size = e.get("size") or 36
        names = list(p["names"]) if p and p.get("names") else []
    elif ty == "switch":
        n = e["positions"]
        flags |= n << 9
        names = [switch_name(e, p, n, i) for i in range(n)]
    elif ty == "led":
        two = e.get("leds") == 2
        icon = not two and e.get("glyph")
        if icon:
            flags |= LED_GLYPHS.index(icon) + 1
            lab = e.get("label") or ""                             # (an icon takes the label's place above)
        if two:
            flags |= F_LEDS2
            names = [switch_name(e, p, 2, i).upper() for i in range(2)]
        flags |= {"left": 1, "right": 2}.get(e.get("pair"), 0) << 4
        if (e["labelAt"] == "below") if "labelAt" in e else two:
            flags |= F_BELOW
    elif ty == "glyph":
        size, lab = GLYPHS.index(e["glyph"]), ""
    if names:
        if not all(names):
            raise ValueError([f"{e['id']}: an empty value name ({e.get('param')}): it would end the names"])
        flags |= F_NAMES
        label = pool.add(lab, *names, "")
    else:
        label = pool.add(lab) if lab else NOSTR
    return [ty, e["x"], e["y"], e["w"], e["h"], page, col, size, r, colour, flags, label]


def copy_cols(cat_page):
    """a built-in page's columns as a classic layout's: [{src, label, style} | None] (the design's own label none)"""
    return [{"src": (cat_page["page"], i), "label": "", "style": 0} if c else None for i, c in enumerate(cat_page["cols"])]


def pack_classic(d, cat, has_free):
    """the design's classic screens of OSC .. MOD as the screen plan: web/fm1layout.js packDesign's "plan", the same
    pages, sections and screens (and, with free screens, the copy of the built-in pages after the plan's own, fenced
    off by a section, that their elements address). -> {pages: [{title, cols}], section: [8], screens: [{sec, pg, band,
    title}], fence} or None (no classic screen in OSC .. MOD: the engine's own pages are served)"""
    if not any(g["name"] in FREE_GROUPS and any(s["type"] == "classic" for s in g["screens"]) for g in d["groups"]):
        return None
    params = param_list(cat)
    pages, screens, sec = [], [], {}

    def col_of(c):
        if not c or c.get("fixed"):
            return None
        p = params.get(c.get("param"))
        if not p or p["part"]:
            return None
        st = c.get("style") or "rule"
        return {"src": (p["page"], p["col"]), "label": c.get("label") or "",
                "style": CELL_STYLES.index(st) if st in CELL_STYLES else 0}
    for gno in range(5):
        t = next((x for x in cat["groups"] if x["group"] == gno), None)
        if not t or not any(s["pages"] for s in t["screens"]):
            continue
        dg = next((g for g in d["groups"] if g["name"] == t["name"]), None)
        sec[gno] = len(pages)
        cl = [s for s in dg["screens"] if s["type"] == "classic"] if dg else []
        if not cl:
            pages.append({"title": t["name"], "cols": [None] * 4})      # (a group of free screens only)
        for s in cl:
            pg = []
            for lane in s["lanes"][:2]:
                pg.append(len(pages))
                pages.append({"title": (lane.get("title") or s.get("title") or t["name"])[:15],
                              "cols": [col_of(c) for c in lane["cells"]]})
            screens.append({"sec": gno, "pg": (pg[0], pg[1] if len(pg) > 1 else 255), "band": PLAN_BAND.get(s["band"], 0),
                            "title": (s.get("title") or "")[:15]})
    fence = None
    if has_free:
        fence = len(pages)
        pages += [{"title": p["title"], "cols": copy_cols(p)} for p in cat["pages"]]
    secs = [sec[g] for g in range(5) if g in sec]
    section = [secs[i] if i < len(secs) else fence if i == len(secs) and fence is not None and len(secs) == 5 else 255
               for i in range(8)]
    return {"pages": pages, "section": section, "screens": screens, "fence": fence}


def classic_bytes(cl, engine):
    """a classic layout as packDesign's 'L' 3 form (its classic half): 'L' 3 engine npages, per page tlen title and four
    times srcpage srccol style llen label, section[8], nscreens, per screen sec pg0 pg1 band tlen title"""
    b = bytearray([76, 3, engine, len(cl["pages"])])

    def text(s):
        u = bytes(ord(ch) if ord(ch) < 256 else 63 for ch in (s or "")[:15])
        b.append(len(u))
        b.extend(u)
    for p in cl["pages"]:
        text(p["title"])
        for c in p["cols"]:
            if c:
                b.extend([c["src"][0], c["src"][1], c["style"]])
                text(c["label"])
            else:
                b.extend([255, 0, 0])
                text("")
    b.extend(cl["section"])
    b.append(len(cl["screens"]))
    for s in cl["screens"]:
        b.extend([s["sec"], s["pg"][0], s["pg"][1], s["band"]])
        text(s["title"])
    return bytes(b)


def free_bytes(c):
    """a compiled design's free screens as packDesign's free half (packFree): nfs, nel, npool (2 B each, little-endian),
    the screens (cr_lay_screen_t), the elements (cr_lay_elem_t), the pool"""
    ne, pool = sum(len(s["elements"]) for s in c["screens"]), bytes(c["pool"].data)
    b = bytearray([len(c["screens"]), ne & 255, ne >> 8, len(pool) & 255, len(pool) >> 8])
    first = 0
    for s in c["screens"]:
        b += bytes([s["group"], first, len(s["elements"]), len(s["lanes"]), s["title"] & 255, s["title"] >> 8])
        for lane in range(MAX_LANES):
            b += bytes((s["lanes"][lane] + [EMPTY] * 4)[:4] if lane < len(s["lanes"]) else [EMPTY] * 4)
        first += len(s["elements"])
    for s in c["screens"]:
        for ty, x, y, w, h, page, col, size, r, colour, flags, label in s["elements"]:
            b += bytes([ELEMENT_TYPES.index(ty), x, y, w, h, page, col, size, r, colour, flags & 255, flags >> 8,
                        label & 255, label >> 8])
    return bytes(b + pool)


def compile_design(d, cat):
    """a design -> {screens: [{group, title, elements: [record], lanes, ids}] (the free screens), pool, classic (the
    plan, pack_classic) or None}; ValueError(errors) when refused"""
    errors = validate(d, cat)
    if errors:
        raise ValueError(errors)
    params, pool, screens = param_list(cat), Pool(), []
    gno = {g["name"]: g["group"] for g in cat["groups"]}
    has_free = any(g["name"] in FREE_GROUPS and any(s["type"] == "free" for s in g["screens"]) for g in d["groups"])
    cl = pack_classic(d, cat, has_free)
    offset = cl["fence"] if cl else 0
    for g in d["groups"]:
        if g["name"] not in FREE_GROUPS:
            continue                                               # FX, MIX: the platform's, checked as today's
        for s in g["screens"]:
            if s["type"] != "free":
                continue
            els = sorted(s["elements"], key=lambda e: 0 if e["type"] == "frame" else 1)    # frames first (stable)
            ix = {e["id"]: i for i, e in enumerate(els)}
            recs = [element(e, params.get(e.get("param")) if e["type"] in BINDABLE else None, pool, offset) for e in els]
            lanes = [[EMPTY if i is None else ix[i] for i in lane] for lane in s["lanes"]]
            screens.append(dict(group=gno[g["name"]], title=pool.add(s["title"]), elements=recs, lanes=lanes,
                                ids=[e["id"] for e in els], name=s["title"]))
    if sum(len(s["elements"]) for s in screens) > 255 or len(screens) > 255:
        raise ValueError(["more than 255 elements or screens in an engine (cr_lay_screen_t.first is a byte)"])
    if cl and (len(cl["pages"]) > 255 or len(cl["screens"]) > 32):
        raise ValueError(["a classic layout of more than 255 pages or 32 screens"])
    return dict(screens=screens, pool=pool, classic=cl)


# the classic tables' size on the unit (arm-none-eabi: param_desc_t 20 B, eng_page_t 84, eng_screen_t 52, cr_layc_t 20;
# the source table 8 B a page; the strings counted apart)
PAGE_B, SRC_B, PLAN_SCREEN_B, LAYC_B = 84, 8, 52, 20


def cost(c):
    ne, ns = sum(len(s["elements"]) for s in c["screens"]), len(c["screens"])
    cl = c.get("classic")
    out = dict(screens=ns, elements=ne, lanes=sum(len(s["lanes"]) for s in c["screens"]), pool=len(c["pool"].data),
               bytes=ne * ELEM_B + ns * SCREEN_B + HEAD_B if ns else 0)
    if cl:
        strs = {p["title"] for p in cl["pages"]} | {s["title"] for s in cl["screens"] if s["title"]} | \
            {x["label"] for p in cl["pages"] for x in p["cols"] if x and x["label"]}
        out.update(cpages=len(cl["pages"]), cscreens=len(cl["screens"]),
                   cbytes=len(cl["pages"]) * (PAGE_B + SRC_B) + len(cl["screens"]) * PLAN_SCREEN_B + LAYC_B,
                   cstrings=sum(len(latin1(x) or b"") + 1 for x in strs))
    return out


# ------------------------------------------------------------------------------------------------------ C ---
def c_str(b):
    """bytes as a C literal: printable ASCII, the rest octal (no trigraphs, no hex run-ons)"""
    return '"' + "".join(chr(x) if 32 <= x < 127 and chr(x) not in '"\\?' else f"\\{x:03o}" for x in b) + '"'


def c_text(s):
    """a text as a C string literal (Latin-1), or 0 for none"""
    return c_str(latin1(s[:15]) if latin1(s[:15]) is not None else bytes(ord(ch) if ord(ch) < 256 else 63 for ch in s[:15])) \
        if s else "0"


def note(s):
    """a text for a C comment"""
    return s.encode("ascii", "replace").decode().replace("*/", "* /")


def classic_c(built, L):
    """the compiled classic layouts as C (core.h's types: cr_layouts.h is read after core.h)"""
    lays = [(name, index, path, c["classic"]) for name, index, path, c in built if c.get("classic")]
    L += ["", "/* ---- the classic layouts: an engine's editor as a screen plan over pages of its own (firmware/src/cr_pages.c",
          " * cp_deep serves them as the engine's pages; get / set / desc / mod_dst go through to the built-in (page, col)) */",
          f"#define CR_LAYC_N {len(lays)}u"]
    if not lays:
        return
    names, pages, srcs, scrs, heads = {}, [], [], [], []
    page0 = screen0 = 0

    def names_of(v):
        if not v:
            return "0"
        k = tuple(v)
        if k not in names:
            names[k] = f"CR_LAYC_NAMES{len(names)}"
        return names[k]
    for name, index, path, cl in lays:
        cat = load_catalog(name)
        bp = {p["page"]: p for p in cat["pages"]}
        for i, p in enumerate(cl["pages"]):
            cols, what = [], []
            for c in p["cols"]:
                if not c:
                    cols.append("{0, 0, 0, 0, 0, 0, 0}")
                    what.append("-")
                    continue
                src = bp[c["src"][0]]
                dc = src["cols"][c["src"][1]]
                cols.append(f'{{{c_str(latin1(dc["short"]))}, F_{dc["fmt"].upper()}, {dc["min"]}, {dc["max"]}, {dc["def"]}, '
                            f'{names_of(dc.get("names"))}, {c_text(dc.get("unit") or "")}}}')
                what.append(f'{src["title"]}:{dc["short"]}')
            pages.append(f"    {{{c_text(p['title'])}, {{{', '.join(cols)}}}}},   /* {name} {i}: {note(' '.join(what))} */")
            srcs.append("    {" + ", ".join(f"{{{c['src'][0]}, {c['src'][1]}}}" if c else "{255, 0}" for c in p["cols"]) + "},")
        for s in cl["screens"]:
            rows = [cl["pages"][g] if g != 255 else None for g in s["pg"]]
            cell = "{" + ", ".join("{" + ", ".join(CELL_C[(x or {}).get("style", 0)] for x in (r["cols"] if r else [None] * 4)) + "}"
                                   for r in rows) + "}"
            lab = "{" + ", ".join("{" + ", ".join(c_text((x or {}).get("label", "")) for x in (r["cols"] if r else [None] * 4)) + "}"
                                  for r in rows) + "}"
            scrs.append(f"    {{{s['sec']}, {{{s['pg'][0]}, {s['pg'][1]}}}, {ENG_B[s['band']]}, 0, {c_text(s['title'])},\n"
                        f"     {cell},\n     {lab}}},   /* {name} {note(s['title'])} */")
        heads.append(f"    {{{index}, {len(cl['pages'])}, {page0}, {len(cl['screens'])}, {screen0}, "
                     f"{{{', '.join(str(x) for x in cl['section'])}}}, {c_str(path.encode())}}},   /* {name} */")
        page0 += len(cl["pages"])
        screen0 += len(cl["screens"])
    L += [f"#define CR_LAYC_NPAGES {page0}u", f"#define CR_LAYC_NSCREENS {screen0}u"]
    for v, n in names.items():
        L.append(f"static const char *const {n}[] = {{{', '.join(c_str(latin1(x)) for x in v)}, 0}};")
    L += ["/* a page: its title (the lane's), its four columns: copies of the built-in descriptors (label fmt min max def",
          " * names unit; label 0: an empty column) */",
          f"static const eng_page_t CR_LAYC_PAGES[{page0}] = {{", *pages, "};",
          "/* each column's built-in (page, col) (255: an empty column) */",
          f"static const uint8_t CR_LAYC_SRC[{page0}][4][2] = {{", *srcs, "};",
          "/* the screens: the group, rows A and B (255: one row), the band, the letter, the title, the cells' styles and",
          " * labels (0: the editor's) */",
          f"static const eng_screen_t CR_LAYC_SCREENS[{screen0}] = {{", *scrs, "};",
          "/* engine, pages, the first page, screens, the first screen, the sections, the design */",
          f"static const cr_layc_t CR_LAYC[{len(lays)}] = {{", *heads, "};"]


CELL_C = ["ENG_C_RULE", "ENG_C_BIG", "ENG_C_RATIO", "ENG_C_HARM", "ENG_C_DETUNE", "ENG_C_NUM", "ENG_C_VAL", "ENG_C_TEXT",
          "ENG_C_KNOB", "ENG_C_BAR", "ENG_C_WAVEPH", "ENG_C_P5KNOB", "ENG_C_P5LED", "ENG_C_P5LED_SAW", "ENG_C_P5LED_TRI",
          "ENG_C_P5LED_SQR", "ENG_C_P5LED2", "ENG_C_P5SWITCH"]      # core.h ENG_C_*, CELL_STYLES' order


def header(built):
    """built: [(engine name, engine index, design path, compiled)] -> the text of build/gen/cr_layouts.h"""
    pool = Pool()
    free = [b for b in built if b[3]["screens"]]
    L = ["/* generated by tools/gen_layout.py from " + ", ".join(path for _, _, path, _ in built) + ":",
         " * the compiled layouts (firmware/src/cr_layout.h, docs/LAYOUT-TOOL.md \"Step 4, the firmware: what was built\").",
         " * Read after core.h (the classic layouts are its eng_page_t / eng_screen_t) */",
         "#pragma once", '#include "cr_layout.h"', "",
         "/* a built design has free screens: the free-page editor and renderer are compiled in (cr_edit.c CE_S_FREE,",
         " * cr_draw.c CR_K_FREE); the layout tool's preview has them always (CR_LAYOUT_PREVIEW) */",
         f"#define CR_LAYOUTS_FREE {1 if free else 0}",
         "#if CR_LAYOUTS_FREE || defined(CR_LAYOUT_PREVIEW)"]
    lays, scrs, els = [], [], []
    for name, index, path, c in free:
        base = c["pool"]
        remap = {}
        for run, off in base.at.items():                           # one pool for every engine
            if run not in pool.at:
                pool.at[run] = len(pool.data)
                pool.data += run
            remap[off] = pool.at[run]
        lays.append(f"    {{{index}, {len(c['screens'])}, {len(scrs)}, 0, {len(els)}, "
                    f"{sum(len(s['elements']) for s in c['screens'])}}},   /* {name}: {path} */")
        first = 0
        for s in c["screens"]:
            lanes = ", ".join("{" + ", ".join(str(x) for x in (lane + [EMPTY] * 4)[:4]) + "}" for lane in s["lanes"])
            lanes += ", {255, 255, 255, 255}" * (MAX_LANES - len(s["lanes"]))
            scrs.append(f"    {{{s['group']}, {first}, {len(s['elements'])}, {len(s['lanes'])}, {remap[s['title']]}, "
                        f"{{{lanes}}}}},   /* {name} {note(s['name'])} */")
            for rec, i in zip(s["elements"], s["ids"]):
                ty, x, y, w, h, page, col, size, r, colour, flags, label = rec
                els.append(f"    {{CR_E_{ty.upper()}, {x}, {y}, {w}, {h}, {page}, "
                           f"{col}, {size}, {r}, {colour}, 0x{flags:04x}, {remap.get(label, NOSTR)}}},   /* {note(i)} */")
            first += len(s["elements"])
    none = "   /* (none: a placeholder) */"
    L += [f"#define CR_LAY_NENGINES {len(lays)}u", f"#define CR_LAY_NSCREENS {len(scrs)}u",
          f"#define CR_LAY_NELEMS {len(els)}u", f"#define CR_LAY_POOL_SIZE {len(pool.data) or 1}u",
          "/* engine, screens, the first screen, 0, the first element, elements */",
          f"static const cr_layout_t CR_LAYOUTS[{len(lays) or 1}] = {{", *(lays or ["    {255, 0, 0, 0, 0, 0}," + none]), "};",
          "/* group, the first element, elements, lanes, the title, the lanes' elements (255: empty) */",
          f"static const cr_lay_screen_t CR_LAY_SCREENS[{len(scrs) or 1}] = {{", *(scrs or ["    {0, 0, 0, 0, 0, {{0}}}," + none]), "};",
          "/* type, x y w h, page col (254: none, 255: a part parameter), size, r, colour, flags, the label */",
          f"static const cr_lay_elem_t CR_LAY_ELEMS[{len(els) or 1}] = {{", *(els or ["    {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0}," + none]), "};",
          "/* the strings, each run once, NUL-terminated (the last one's NUL is the literal's own) */",
          "static const char CR_LAY_POOL[] ="]
    runs = sorted(pool.at.items(), key=lambda kv: kv[1])
    for k, (run, off) in enumerate(runs):
        L.append(f"    {c_str(run[:-1] if k == len(runs) - 1 else run)}   /* {off} */")
    if not runs:
        L.append('    ""')
    L[-1] += ";"
    L += ["_Static_assert(sizeof CR_LAY_POOL == CR_LAY_POOL_SIZE, \"the pool's size\");", "#endif"]
    classic_c(built, L)
    return "\n".join(L) + "\n"


def compile_build(build):
    """{engine: design path} -> [(engine name, engine index, design path, compiled)]; SystemExit when one is refused"""
    built = []
    for name, path in build.items():
        cat = load_catalog(name)
        d = json.loads((ROOT / path).read_text(encoding="utf-8"))
        try:
            c = compile_design(d, cat)
        except ValueError as e:
            for t in e.args[0]:
                print(f"gen_layout: {path}: {t}", file=sys.stderr)
            raise SystemExit(f"gen_layout: {path} refused ({len(e.args[0])} errors)")
        for t in warnings(d):
            print(f"gen_layout: {path}: warning: {t}", file=sys.stderr)
        n = cost(c)
        if c["screens"]:
            print(f"gen_layout: {name} {Path(path).name}: {n['screens']} free screens, {n['elements']} elements, {n['lanes']} "
                  f"lanes: {n['elements']} x {ELEM_B} + {n['screens']} x {SCREEN_B} + pool {n['pool']} + {HEAD_B} = "
                  f"{n['bytes'] + n['pool']} B of flash")
        if c["classic"]:
            print(f"gen_layout: {name} {Path(path).name}: a screen plan of {n['cscreens']} screens over {n['cpages']} pages: "
                  f"{n['cpages']} x {PAGE_B + SRC_B} + {n['cscreens']} x {PLAN_SCREEN_B} + {LAYC_B} + strings about "
                  f"{n['cstrings']} = about {n['cbytes'] + n['cstrings']} B of flash")
        built.append((name, cat["engine"]["index"], path, c))
    return built


def main(out, build=None):
    text = header(compile_build(build or BUILD))
    o = Path(out)
    o.parent.mkdir(parents=True, exist_ok=True)
    if not o.exists() or o.read_text(encoding="latin-1") != text:
        tmp = o.with_name(f"{o.name}.{os.getpid()}.tmp")
        tmp.write_text(text, encoding="latin-1")
        tmp.replace(o)                                             # (the web builds and the tests run it side by side)


if __name__ == "__main__":
    args, build, out = sys.argv[1:], {}, None
    while args:
        a = args.pop(0)
        if a == "--design" and args:
            k, _, v = args.pop(0).partition("=")
            build[k] = v
        else:
            out = a
    main(out or ROOT / "build" / "gen" / "cr_layouts.h", build or None)
