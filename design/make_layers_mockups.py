#!/usr/bin/env python3
"""Generate the knob-row mock-ups for the remaining layers (fm1-panel-design JSON): KEY, LOOP, SEQ (the beat's preset page).

  python3 design/make_layers_mockups.py     # writes design/choralroot-fm1-layers-mockups.json

The layer grammar decided on 2026-10-07 (design/make_fx_mockups.py: the picker on top, one row of four knob cells
with bespoke glyphs under it, the cell just turned hot, no popups) is built for FX and being built for PERF and BASS.
The three layers left have a wrinkle each, so they get their own sheet:

  KEY    its screen today is the keyboard sliding up with the tonic lit (the one big thing). Two drafts: A keeps the
         keyboard and puts the knob row under it (the keyboard is the band); B is the plain knob row with the key
         name big and the twelve tonics as marks.
  LOOP   the ring stays in this layer (decided with the dial), so the knob row has to live inside it: drafted inside
         the ring (A) and, for comparison, without the ring (B). Stopped: the picker is the length (KNOB 1 = SYNC is
         the same control); playing: the picker is the action.
  SEQ    the SEQ button's hold layer, the metronome (the button was called METRO until 0.18; the screen's own
         title is still "metronome", as the firmware draws it): one knob (the click level); three cells are '–'.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_mockups import (state, scr, cell, BUTTON_LABELS, ENCODER_LABELS, LAYER_BTN_NOTE, LAYER_FOOT, LIT, BLINK,  # noqa: E402
                          OFF, ROOT_WHITE, ROOT_BLACK, ROOTS, key_of, name, C_KEY)

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "choralroot-fm1-layers-mockups.json")


def g(label, value, glyph=None, pct=None, pct2=None, **kw):
    c = cell(label, value, glyph, pct, **kw)
    if pct2 is not None:
        c["pct2"] = round(pct2, 3)
    return c


def knobrow(items, sel, col, cells, hot=None, label="", value="", **kw):
    p = {"kind": "knobrow", "items": items, "sel": sel, "col": col, "label": label, "cells": cells, "hot": hot,
         "value": value}
    p.update(kw)
    return p


S = []
KEY_CELLS = [g("Tonic", "C"), g("Scale", "Major"), g("Transpose", "+0", "shift", 0.5), g("Single", "Full")]
tonics = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
KEY_LABEL = "key · keys pick"                                     # (GLO in the layer: "key · keys play", the keys play)
KEY_FOOT = "MAJ MIN: scale · GLO: play · OCT-: back"              # cr_ui.c cu_layer_screen (2026-10-10: MAJ / MIN pick
# the scale in the layer; their LEDs at half, the selected scale's at full, the other chord keys dark: cr_ui.c cr_leds)
KEY_LEDS = [1] * 27                                              # the glow (make_mockups.py DIM) ..
for _n in ("F3", "F#3", "G3", "A3", "C4", "C#4"):                # .. but the six chord keys without a job in the layer
    KEY_LEDS[key_of(_n)] = OFF
KEY_NOTES = {key_of("A#3"): "MAJ: the scale Major (lit: the selected scale)", key_of("G#3"): "MIN: the scale Minor (half: not selected)"}

# 1, 2 --------------------------------------------------------------- KEY
S.append(state(
    "1 · KEY · A: the keyboard stays, the knob row under it",
    "KEY held: the keyboard slides up with the tonic lit yellow (today's screen) and the four cells sit under it: TONIC "
    "(text), SCALE (text), TRANSPOSE (the staff-and-square shift glyph, centre = 0), SINGLE NOTES (Full / Split). The "
    "root keys pick the tonic, KNOB 1 too. The keyboard is the band: the designer draws it with `band: keyboard`. "
    "MAJ and MIN pick the scale (2026-10-10): both glow, the selected scale's key lit, the other chord keys dark. "
    "GLO tapped here switches the keys to play (the label 'key · keys play', GLO lit, every key as outside the layer).",
    keys=KEY_LEDS, held=["MAJ"], key_notes=KEY_NOTES,
    lit=["C5"], buttons={"SEL": BLINK}, key_labels={k: name(k)[:-1] for k in ROOTS},
    button_notes={"SEL": LAYER_BTN_NOTE},
    encoders={"KNOB1": ("TONIC", "C"), "KNOB2": ("SCALE", "MAJ"), "KNOB3": ("TRANSPOSE", "+0"), "KNOB4": ("SINGLE NOTES", "FULL")},
    screen=scr(knobrow(tonics, 0, C_KEY, KEY_CELLS, label=KEY_LABEL, band="keyboard", lit=[{"k": "C5", "col": C_KEY}]),
               key="Key: C", foot=KEY_FOOT, note="A: the keyboard as the band, the tonic lit; the cells under it."),
))
S.append(state(
    "2 · KEY · B: the plain knob row (the key name big)",
    "The same layer as a plain knob row: the tonic big in yellow with its neighbours (B · C · C#), twelve marks, the "
    "scale as the value line ('major'), the cells under it. Loses the keyboard picture.",
    keys=KEY_LEDS, held=["MAJ"], key_notes=KEY_NOTES,
    lit=["C5"], buttons={"SEL": BLINK}, key_labels={k: name(k)[:-1] for k in ROOTS},
    button_notes={"SEL": LAYER_BTN_NOTE},
    encoders={"KNOB1": ("TONIC", "C"), "KNOB2": ("SCALE", "MAJ"), "KNOB3": ("TRANSPOSE", "+0"), "KNOB4": ("SINGLE NOTES", "FULL")},
    screen=scr(knobrow(tonics, 0, C_KEY, KEY_CELLS, label=KEY_LABEL, value="major"),
               key="Key: C", foot=KEY_FOOT, note="B: the key name big over the cells."),
))
S.append(state(
    "3 · KEY · A, TRANSPOSE just turned to +5",
    "KNOB 3 turned: the shift glyph's square climbs (centre = 0, top = +24), the value on a white hot block; no popup.",
    keys=KEY_LEDS, held=["MAJ"], key_notes=KEY_NOTES,
    lit=["C5"], buttons={"SEL": BLINK}, key_labels={k: name(k)[:-1] for k in ROOTS},
    button_notes={"SEL": LAYER_BTN_NOTE},
    encoders={"KNOB1": ("TONIC", "C"), "KNOB2": ("SCALE", "MAJ"), "KNOB3": ("TRANSPOSE", "+5"), "KNOB4": ("SINGLE NOTES", "FULL")},
    screen=scr(knobrow(tonics, 0, C_KEY, [g("Tonic", "C"), g("Scale", "Major"), g("Transpose", "+5", "shift", 29 / 48), g("Single", "Full")],
                       hot=2, label=KEY_LABEL, band="keyboard", lit=[{"k": "C5", "col": C_KEY}]),
               key="Key: C", foot=KEY_FOOT, note="A with a hot Transpose cell."),
))

# 4..6 --------------------------------------------------------------- LOOP
slot_labels = {ROOT_WHITE[i]: str(i + 1) for i in range(10)}
slot_labels[ROOT_BLACK[0]] = "CLEAR"
slot_labels[ROOT_BLACK[1]] = "UNDO"
LOOP_CELLS = [g("Sync", "4 bars", "range", 4 / 5), g("Quantize", "1/16", "echoes", 0.8, 1.0), g("Count-in", "On", "gate", 1.0), g("Level", "100", "bar", 1.0)]
S.append(state(
    "4 · LOOP · A: stopped, the knob row inside the ring",
    "LOOP held with no loop playing: the ring stays (decided with the dial), the length picker inside it ('4 bars', "
    "red) and the four cells under it, narrowed to the ring's inner width: SYNC (a span: Free .. 16 bars; it is the "
    "picker's own value, so the cell repeats it), QUANTIZE (pulses at the grid's density), COUNT-IN (a gate: on / "
    "off), LEVEL (a bar). Tight, but everything is on screen.",
    keys=[OFF] * 27, blink=["D4"], lit=["E4", "F4"], buttons={"PLAY": BLINK}, key_labels=slot_labels,
    button_notes={"PLAY": LAYER_BTN_NOTE},
    encoders={"KNOB1": ("SYNC", "4 BAR"), "KNOB2": ("QUANTIZE", "1/16"), "KNOB3": ("COUNT-IN", "ON"), "KNOB4": ("LOOP LEVEL", "100")},
    screen=scr(knobrow(["Free", "1 bar", "2 bars", "4 bars", "8 bars", "16 bars"], 3, "red", LOOP_CELLS, label="loop length"),
               key="Loop", ring=0, note="A: the picker and the cells inside the empty ring."),
    chord_block=False,
))
S.append(state(
    "5 · LOOP · A: playing, the action picker inside the ring",
    "The loop plays: the ring runs, the picker is Overdub / Pause / Undo / Clear (OCT+ does it), the cells show SYNC "
    "(fixed while playing: dim), QUANTIZE, COUNT-IN, LEVEL (the one knob you ride live: a bar).",
    held=["MAJ"], lit=["G4", "B4", "D5"], buttons={"PLAY": BLINK, "REC": LIT, "OCT+": BLINK}, play_green=1,
    button_notes={"PLAY": LAYER_BTN_NOTE, "OCT+": "OK: does the picked action"},
    encoders={"KNOB1": ("SYNC", "4 BAR"), "KNOB2": ("QUANTIZE", "1/16"), "KNOB3": ("COUNT-IN", "ON"), "KNOB4": ("LOOP LEVEL", "80")},
    screen=scr(knobrow(["Overdub", "Pause", "Undo", "Clear"], 0, "red",
                       [g("Sync", "4 bars", "range", 4 / 5, dim=True), g("Quantize", "1/16", "echoes", 0.8, 1.0), g("Count-in", "On", "gate", 1.0), g("Level", "80", "bar", 0.8)],
                       hot=3, label="loop 1"), key="Loop 1", ring=0.62, note="A, playing: the action picker, LEVEL hot after a turn."),
))
S.append(state(
    "6 · LOOP · B: stopped, no ring (for comparison)",
    "The same stopped layer without the ring: the row gets the full width. Against it: the ring is the loop's own "
    "picture and the user kept it for this layer; B is here only to compare the room.",
    keys=[OFF] * 27, blink=["D4"], lit=["E4", "F4"], buttons={"PLAY": BLINK}, key_labels=slot_labels,
    button_notes={"PLAY": LAYER_BTN_NOTE},
    encoders={"KNOB1": ("SYNC", "4 BAR"), "KNOB2": ("QUANTIZE", "1/16"), "KNOB3": ("COUNT-IN", "ON"), "KNOB4": ("LOOP LEVEL", "100")},
    screen=scr(knobrow(["Free", "1 bar", "2 bars", "4 bars", "8 bars", "16 bars"], 3, "red", LOOP_CELLS, label="loop length"),
               key="Loop", note="B: no ring, the full-width row."),
    chord_block=False,
))

# 7 ------------------------------------------------------------------ SEQ (the beat: Metronome / Drums)
S.append(state(
    "7 · SEQ · the beat's preset page (Metronome)",
    "SEQ held (the SEQ key's hold menu): the beat's preset page. Since the drums (the DRUMS plan, after 0.39) the device "
    "draws it as its own panel (CR_K_DRUM: the Metronome and Drums tiles under the page's name; the v3 sheet "
    "design/drums-mockups/drums-v3-overview.png, the drum pages Beat, Kick, Snare, Hats after it with Drums); this sheet "
    "keeps the knob row: Preset (KNOB 1), Click (KNOB 2, a bar), Meter (KNOB 3; D4 E4 F4 pick it too), and with Drums "
    "Kit (KNOB 4). GLO held + SEQ toggles the beat, and so does SEQ tapped in this layer (a SEQ tap anywhere else opens "
    "the loop editor). (OPT + SELECT sets the click's level from anywhere.)",
    keys=[OFF] * 27, buttons={"SEQ": BLINK}, button_notes={"SEQ": LAYER_BTN_NOTE},
    encoders={"KNOB1": ("PRESET", "Metro"), "KNOB2": ("CLICK", "70"), "KNOB3": ("METER", "4/4"), "KNOB4": ""},
    screen=scr(knobrow(["Metronome"], 0, "white", [g("Preset", "Metro"), g("Click", "70", "bar", 0.7), g("Meter", "4/4"), None]),
               key="", note="SEQ: the preset page; the device draws the Metronome / Drums tiles in the band (CR_K_DRUM)."),
    chord_block=False,
))

for _s in S:
    _h = _s["screen"].get("header")
    if _h and _h.get("batt") == 255:
        _h["batt"] = False

design = {
    "format": "fm1-panel-design", "version": 1, "device": "M-VAVE FM-1",
    "name": "ChoralRoot FM-1 layer knob-row mockups (KEY, LOOP, SEQ)",
    "palette": "MOD",
    "labels": {"buttons": BUTTON_LABELS, "encoders": ENCODER_LABELS},
    "notes": "The knob row on the three layers with a wrinkle: KEY (A: the keyboard kept as the band over the cells; B: the "
             "key name big), LOOP (A: inside the ring, stopped and playing; B: without the ring), SEQ, the metronome (one cell). "
             "Decided 2026-10-07 by the user: KEY A, LOOP B (no ring in the LOOP layer; the dial in the top line while playing, as everywhere), the metronome's layer (then METRO, SEQ since 0.18) as drawn.",
    "states": S,
}

if __name__ == "__main__":
    with open(OUT, "w") as f:
        json.dump(design, f, indent=1)
    print("wrote", OUT, "with", len(S), "states")
