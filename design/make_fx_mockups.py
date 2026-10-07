#!/usr/bin/env python3
"""Generate the FX-layer mock-ups (fm1-panel-design JSON): a layer screen that SHOWS its knobs.

  python3 design/make_fx_mockups.py         # writes design/choralroot-fm1-fx-mockups.json

The problem (the user, 2026-10-07): the fx layer shows the effect picker alone (Reverb / Chorus / Delay / Drive, the
amount under it) while KNOB 1-3 carry parameters that differ per effect (Reverb: size, damp, type; Chorus: rate,
depth; Delay: time, feedback, colour; Drive: none) and are only seen as a popup once a knob turns. The same is true of
every layer (PERF: the mode's four parameters; BASS: behaviour, register, sound, level; KEY: tonic, scale,
transpose, single notes; LOOP: sync, quantize, count-in, level): a layer screen hides what its knobs do.

The proposal: a **layer grammar** in one new screen kind, `knobrow` (the designer's FORMAT.md): the picker keeps the
top of the panel (the choice big in the layer's colour, its neighbours peeking left and right, square marks), and
under it ONE ROW OF FOUR CELLS, one per knob, in the knob colours (blue, orange, white, green: the editor's rule, a
thing is the colour of the control that moves it) with a stripe bar and the value; the cell just turned sits on a hot
block. The effect's amount is on KNOB 4 and therefore green: the FX colour lands where the FX amount is. Effects with
fewer parameters leave their cells as a dim '–'. No popup is needed in a layer any more: the row IS the readout.

Alternatives drawn for comparison: the four tall stripe meters (the OSC mixer's `tall` kind) and the amount-first
screen (today's popup made permanent). Then the grammar on the other layers (PERF, BASS).
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_mockups import (state, scr, cell, editor_screen, BUTTON_LABELS, ENCODER_LABELS, LAYER_BTN_NOTE, LAYER_FOOT, LIT, BLINK,  # noqa: E402
                          OFF, ROOT_WHITE, ROOT_BLACK, C_FX, C_BASS)

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "choralroot-fm1-fx-mockups.json")

FX = ["Reverb", "Chorus", "Delay", "Drive"]                       # cr_ui.c CU_FX (the white roots D4 E4 F4 G4)
fx_labels = {ROOT_WHITE[i]: n.upper() for i, n in enumerate(FX)}
FX_FOOT = "a root: effect · " + LAYER_FOOT
FX_BTN = {"FX": BLINK}
FX_NOTE = {"FX": LAYER_BTN_NOTE}
S = []


def knobrow(items, sel, col, cells, hot=None, label="", value=""):
    return {"kind": "knobrow", "items": items, "sel": sel, "col": col, "label": label, "cells": cells, "hot": hot,
            "value": value}


# the bespoke glyphs (the designer's FORMAT.md, "cell glyphs"): a parameter is a small picture that changes with its
# value, in the spirit of Ableton's device pictograms and the reverb UIs in ../UI Inspiration: a room for the reverb's
# size (the far wall recedes), a moon phase for damping / colour (tone), echoes for the delay's time (spacing) and
# feedback (decay), an LFO wave for the chorus (density = rate, height = depth), a sine squaring off for drive, two
# squares for dry / wet. pct is the value; pct2 a second value the picture needs.
def g(label, value, glyph, pct=None, pct2=None, **kw):
    c = cell(label, value, glyph, pct, **kw)
    if pct2 is not None:
        c["pct2"] = round(pct2, 3)
    return c


REVERB_CELLS = [g("Size", "90", "room", 90 / 127), g("Damp", "60", "moon", 1 - 60 / 127), g("Type", "Room", "room", 0.5),
                g("Amount", "25", "mix", 0.25)]
REVERB_SPRING = [g("Size", "90", "room", 90 / 127), g("Damp", "60", "moon", 1 - 60 / 127), g("Type", "Spring", "spring"),
                 g("Amount", "25", "mix", 0.25)]
DELAY_CELLS = [g("Time", "1/8", "echoes", 0.45, 0.5), g("Feedback", "60", "echoes", 0.45, 0.5), g("Colour", "70", "moon", 70 / 127),
               g("Amount", "40", "mix", 0.4)]
CHORUS_CELLS = [g("Rate", "0.8 Hz", "lfo", 0.3, 0.5), g("Depth", "60", "lfo", 0.3, 60 / 127), None, g("Amount", "30", "mix", 0.3)]
DRIVE_CELLS = [None, None, None, g("Amount", "15", "clip", 0.15)]

# 1 ---------------------------------------------------------------- today
S.append(state(
    "1 · today: the fx layer is a picker; the knobs are a surprise",
    "As built (walk-through state 16): FX held opens the layer; the white roots pick the effect, KNOB 1-3 its "
    "parameters, KNOB 4 the amount. The screen shows the effect and the amount only; what KNOB 1-3 do (size / damp / "
    "type here) appears as a popup when a knob turns, so the layer never shows its own controls.",
    keys=[OFF] * 27, lit=["D4"], buttons=FX_BTN, key_labels=fx_labels, button_notes=FX_NOTE,
    encoders={"KNOB1": ("SIZE", "90"), "KNOB2": ("DAMP", "60"), "KNOB3": ("TYPE", "ROOM"), "KNOB4": ("AMOUNT", "25")},
    screen=scr({'kind': 'picker', 'items': FX, 'sel': 0, 'label': 'fx · KNOB 4 amount', 'col': C_FX, 'value': '25'},
               foot=FX_FOOT, note="today: the effect and its amount; KNOB 1-3 unseen."),
    chord_block=False,
))

# 2..5 ------------------------------------------------------------- the knob row
S.append(state(
    "2 · knob row: Reverb",
    "Proposal: the picker keeps the top of the panel (Reverb big in green, Chorus peeking at the right, the four "
    "square marks), and under it one row of four cells, one per knob, in the knob colours: SIZE (blue, KNOB 1), DAMP "
    "(orange, KNOB 2), TYPE (white, KNOB 3), AMOUNT (green, KNOB 4): the amount is green because KNOB 4 is, which is "
    "also the FX colour. Each cell: its name small, a stripe bar, the value bold; the row is the readout, no popup. "
    "Motion: a root or SELECT flips the effect name (the split-flap) and the row's labels and bars change with it, "
    "sliding in from the right; a turned bar fills stripe by stripe.",
    keys=[OFF] * 27, lit=["D4"], buttons=FX_BTN, key_labels=fx_labels, button_notes=FX_NOTE,
    encoders={"KNOB1": ("SIZE", "90"), "KNOB2": ("DAMP", "60"), "KNOB3": ("TYPE", "ROOM"), "KNOB4": ("AMOUNT", "25")},
    screen=scr(knobrow(FX, 0, C_FX, REVERB_CELLS, label="fx"), foot=FX_FOOT,
               note="the knob row: Reverb big in green over the four cells in the knob colours; the amount green on KNOB 4."),
    chord_block=False,
))

S.append(state(
    "3 · knob row: Delay, KNOB 2 just turned (hot)",
    "Delay selected (F4): TIME / FEEDBACK / COLOUR / AMOUNT. KNOB 2 has just moved: FEEDBACK's value sits on an orange "
    "block for 800 ms (the editor's hot cell), no popup over the screen.",
    keys=[OFF] * 27, lit=["F4"], buttons=FX_BTN, key_labels=fx_labels, button_notes=FX_NOTE,
    encoders={"KNOB1": ("TIME", "1/8"), "KNOB2": ("FEEDBACK", "60"), "KNOB3": ("COLOUR", "70"), "KNOB4": ("AMOUNT", "40")},
    screen=scr(knobrow(FX, 2, C_FX, DELAY_CELLS, hot=1, label="fx"), foot=FX_FOOT,
               note="Delay: the hot cell (FEEDBACK on an orange block) is the only trace of the turn."),
    chord_block=False,
))

S.append(state(
    "4 · knob row: Chorus (two parameters) and Drive (amount only)",
    "Effects with fewer parameters leave their cells as a dim '–' (Chorus: RATE, DEPTH, –, AMOUNT; Drive: –, –, –, "
    "AMOUNT): the row's shape stays, so the hand always finds the amount on KNOB 4. This state shows Chorus.",
    keys=[OFF] * 27, lit=["E4"], buttons=FX_BTN, key_labels=fx_labels, button_notes=FX_NOTE,
    encoders={"KNOB1": ("RATE", "0.8 Hz"), "KNOB2": ("DEPTH", "60"), "KNOB3": "", "KNOB4": ("AMOUNT", "30")},
    screen=scr(knobrow(FX, 1, C_FX, CHORUS_CELLS, label="fx"), foot=FX_FOOT,
               note="Chorus: two cells and a dim '–'; the amount stays on KNOB 4."),
    chord_block=False,
))

S.append(state(
    "5 · knob row: Drive, FX off",
    "Drive has no parameters: three '–' and the amount. With the effect switched off (FX tap) the amount cell reads "
    "'off' and its bar is empty; a KNOB 4 turn switches it on again (today's rule).",
    keys=[OFF] * 27, lit=["G4"], buttons={"FX": BLINK}, key_labels=fx_labels, button_notes=FX_NOTE,
    encoders={"KNOB1": "", "KNOB2": "", "KNOB3": "", "KNOB4": ("AMOUNT", "off")},
    screen=scr(knobrow(FX, 3, C_FX, [None, None, None, g("Amount", "off", "clip", 0.0)], label="fx"), foot=FX_FOOT,
               note="Drive, FX off: the amount cell reads off."),
    chord_block=False,
))

S.append(state(
    "5b · knob row: Reverb, Type = Spring",
    "TYPE is an enum (Room / Spring), so its cell is a picture of the type rather than a level: the room box for Room, "
    "a coil for Spring. The other cells keep their pictures.",
    keys=[OFF] * 27, lit=["D4"], buttons=FX_BTN, key_labels=fx_labels, button_notes=FX_NOTE,
    encoders={"KNOB1": ("SIZE", "90"), "KNOB2": ("DAMP", "60"), "KNOB3": ("TYPE", "SPRING"), "KNOB4": ("AMOUNT", "25")},
    screen=scr(knobrow(FX, 0, C_FX, REVERB_SPRING, hot=2, label="fx"), foot=FX_FOOT,
               note="Type = Spring: the coil; KNOB 3 just turned (hot)."),
    chord_block=False,
))

# 6 ---------------------------------------------------------------- the glyphs at two values
S.append(state(
    "6 · the glyphs change with the value (low row, high row)",
    "The pictures are the readout: the same four parameters at a low value (row A, on the knobs) and a high one (row B): "
    "the reverb's room deepens as SIZE grows, the moon fills as the damping lifts, the delay's echoes spread with TIME "
    "and linger with FEEDBACK, the chorus wave quickens with RATE and swells with DEPTH, the drive's sine squares off. "
    "(Drawn with the editor's two-row kind for the comparison only.)",
    keys=[OFF] * 27, buttons=FX_BTN, button_notes=FX_NOTE, encoders={},
    screen=editor_screen({"kind": "edit8", "title": "glyph study", "titleCol": C_FX, "right": "low · high",
                          "rows": [[g("Size", "20", "room", 0.15), g("Time", "1/16", "echoes", 0.15, 0.5), g("Feedback", "10", "echoes", 0.45, 0.1), g("Drive", "10", "clip", 0.1)],
                                   [g("Size", "120", "room", 0.95), g("Time", "1/2", "echoes", 0.95, 0.5), g("Feedback", "110", "echoes", 0.45, 0.95), g("Drive", "120", "clip", 0.95)]],
                          "active": 0}, note="the same glyphs low (top) and high (bottom)."),
    chord_block=False,
))

S.append(state(
    "7 · the glyphs change with the value, continued",
    "Damp and Colour as a moon (dark to full), the chorus Rate (one cycle to five) and Depth (flat to full height), "
    "and the dry / wet squares (the front square fills with the amount).",
    keys=[OFF] * 27, buttons=FX_BTN, button_notes=FX_NOTE, encoders={},
    screen=editor_screen({"kind": "edit8", "title": "glyph study", "titleCol": C_FX, "right": "low · high",
                          "rows": [[g("Damp", "110", "moon", 0.15), g("Rate", "0.1 Hz", "lfo", 0.05, 0.5), g("Depth", "10", "lfo", 0.3, 0.1), g("Amount", "10", "mix", 0.1)],
                                   [g("Damp", "10", "moon", 0.95), g("Rate", "6 Hz", "lfo", 0.95, 0.5), g("Depth", "120", "lfo", 0.3, 0.95), g("Amount", "90", "mix", 0.9)]],
                          "active": 0}, note="moon, LFO and mix glyphs low (top) and high (bottom)."),
    chord_block=False,
))

# 8, 9 ------------------------------------------------------------- the alternatives
S.append(state(
    "8 · alternative: four tall stripes",
    "The OSC mixer's tall meters instead of a row: the effect name as the layer's top line, four tall stripe meters in "
    "the knob colours filling from the bottom, the values at the foot. Bigger and more graphic, but a text value "
    "(TYPE = Room) has no height to fill, and the effect picker has no room left: the roots pick blind.",
    keys=[OFF] * 27, lit=["D4"], buttons=FX_BTN, key_labels=fx_labels, button_notes=FX_NOTE,
    encoders={"KNOB1": ("SIZE", "90"), "KNOB2": ("DAMP", "60"), "KNOB3": ("TYPE", "ROOM"), "KNOB4": ("AMOUNT", "25")},
    screen=editor_screen({"kind": "edit8", "title": "Reverb", "titleCol": C_FX, "right": "fx · 1 of 4", "tall": True,
                          "rows": [[cell("Size", "90", pct=90 / 127), cell("Damp", "60", pct=60 / 127), cell("Type", "Room", pct=0.0), cell("Amount", "25", pct=0.25)]],
                          "active": 0}, note="four tall stripes (drawn full height like the editor's mixer): graphic, but no picker, no footer and no height for a text value."),
    chord_block=False,
))

S.append(state(
    "9 · alternative: the amount first",
    "Today's popup made permanent: the amount huge in green over its stripe meter (what the hand rides on KNOB 4), the "
    "effect name as the label, and KNOB 1-3's values as one small line under it. Honest about what matters live, but "
    "the three parameters are a footnote and still need a popup to be edited.",
    keys=[OFF] * 27, lit=["D4"], buttons=FX_BTN, key_labels=fx_labels, button_notes=FX_NOTE,
    encoders={"KNOB1": ("SIZE", "90"), "KNOB2": ("DAMP", "60"), "KNOB3": ("TYPE", "ROOM"), "KNOB4": ("AMOUNT", "25")},
    screen=scr({"kind": "meter", "value": "25", "label": "reverb · size 90 · damp 60 · room", "col": C_FX, "pct": 0.25, "segments": 12},
               foot=FX_FOOT, note="amount first: the popup as the screen, the parameters as a line."),
    chord_block=False,
))

# 10, 11 ----------------------------------------------------------- the grammar on the other layers
perf_modes = ["Strum", "Strum 2 Octaves", "Slop", "Arpeggiate", "Arp 2 Octaves", "Pattern", "Harp"]
S.append(state(
    "10 · the same grammar: the perform layer",
    "The knob row is a layer grammar, not an FX screen: PERF held shows the mode big in white (Arpeggiate; Slop and "
    "Arp 2 Octaves peeking) over RATE / ORDER / RANGE / GATE in the knob colours. Today the mode's parameters are "
    "popups too.",
    keys=[OFF] * 27, lit=["G4"], buttons={"ARP": BLINK}, button_notes={"ARP": LAYER_BTN_NOTE},
    key_labels={ROOT_WHITE[i]: m for i, m in enumerate(["STRUM", "STR 2", "SLOP", "ARP", "ARP 2", "PATT", "HARP"])},
    encoders={"KNOB1": ("RATE", "1/8"), "KNOB2": ("ORDER", "UP"), "KNOB3": ("RANGE", "1 OCT"), "KNOB4": ("GATE", "70%")},
    screen=scr(knobrow(perf_modes, 3, "white", [g("Rate", "1/8", "echoes", 0.5, 1.0), g("Order", "Up", "arrow", 0.1), g("Range", "1 oct", "range", 0.33), g("Gate", "70%", "gate", 0.7)], label="perform"),
               foot="a root: mode · " + LAYER_FOOT, note="the perform layer in the same grammar: the mode over its four parameters."),
    chord_block=False,
))

S.append(state(
    "11 · the same grammar: the bass layer",
    "BASS held: the behaviour big in orange over REGISTER / SOUND / LEVEL and, on KNOB 1, the behaviour itself (the "
    "picker and KNOB 1 are the same control, so the first cell repeats the choice small). Today: the picker alone.",
    keys=[OFF] * 27, buttons={"ENV": BLINK}, button_notes={"ENV": LAYER_BTN_NOTE},
    encoders={"KNOB1": ("BEHAVIOUR", "CHORDS"), "KNOB2": ("REGISTER", "-1"), "KNOB3": ("SOUND", "03 SUB"), "KNOB4": ("LEVEL", "80")},
    screen=scr(knobrow(["Chords Only", "Unison Bass", "Bass Single Notes", "Solo"], 0, C_BASS,
                       [cell("Behaviour", "Chords"), g("Register", "-1", "shift", 1 / 3), cell("Sound", "03 SUB"), g("Level", "80", "bar", 0.8)], label="bass"),
               foot="a root: preview · OCT-: back · HOME", note="the bass layer: behaviour over register, sound and level."),
    chord_block=False,
))

for _s in S:                                   # the battery shows on the Options page only
    _h = _s["screen"].get("header")
    if _h and _h.get("batt") == 255:
        _h["batt"] = False

assert len(S) <= 24

design = {
    "format": "fm1-panel-design", "version": 1, "device": "M-VAVE FM-1",
    "name": "ChoralRoot FM-1 fx layer mockups",
    "palette": "MOD",
    "labels": {"buttons": BUTTON_LABELS, "encoders": ENCODER_LABELS},
    "notes": "The fx layer today shows the effect picker alone (state 1); every parameter cell carries a bespoke glyph that changes with its value (room, moon, echoes, lfo, clip, mix: states 2-7); KNOB 1-3's parameters differ per effect and appear only as "
             "popups (state 1). Proposal (2-5): a layer grammar, the `knobrow` kind: the picker on top (the choice big in the "
             "layer's colour, neighbours peeking, square marks), one row of four cells under it in the knob colours (blue "
             "orange white green) with stripe bars and values, the cell just turned on a hot block; missing parameters are a "
             "dim '–'; the FX amount lands on green KNOB 4. Alternatives (6, 7): four tall stripes, the amount first. The "
             "grammar on the perform and bass layers (8, 9). Recommended: the knob row, on every layer.",
    "states": S,
}

if __name__ == "__main__":
    with open(OUT, "w") as f:
        json.dump(design, f, indent=1)
    print("wrote", OUT, "with", len(S), "states")
