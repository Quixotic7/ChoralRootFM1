#!/usr/bin/env python3
"""Generate the preset-model mock-ups (fm1-panel-design JSON): presets per engine, one editable pool each.

  python3 design/make_preset_mockups.py     # writes design/choralroot-fm1-preset-mockups.json

The user's model (2026-10-07), replacing the curated 48-sound bank + 32 user slots:
  * PRESETS turns through the presets of the CURRENT ENGINE only (its pool: the engine's factory presets plus the
    ones the user saved into it); OPT + PRESETS changes the engine (outside the editor).
  * One pool per engine: the user edits and saves over any factory preset; no separate factory / user lists.
  * SAVE is two choices: overwrite the current preset, or save as new (the pool's next free place), then the name.
  * SAVE held 1 s in the dialog: delete a user-added preset / reset an overwritten factory preset to the factory one.
Screens: the sound popup (PRESETS turned) names the engine and the place in its pool; the engine change is a picker;
the save dialog is a two-item picker; the naming screen is today's; a reset confirmation is today's red question.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_mockups import (state, scr, chord, bubbles, save_state, BUTTON_LABELS, ENCODER_LABELS, IDLE_ENC, LIT, BLINK,  # noqa: E402
                          OFF, C_CHORD, C_BASS)

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "choralroot-fm1-preset-mockups.json")
ENGINES = ["ANALOG", "FM6", "VA", "PHASE", "CZ-1", "LOFI", "VOICE", "TRIO", "WHEEL", "PHYS", "NOISE"]
S = []

# 1 ---------------------------------------------------------------- PRESETS turned: inside the engine's pool
S.append(state(
    "1 · PRESETS turned: the next preset of the current engine",
    "PRESETS steps through the presets of the engine the chord part plays, and nothing else: here FM6, preset 05 of "
    "26 (00 INIT, 01..24 the factory presets, then the 2 the user saved; INIT cannot be overwritten). The popup: the number big, the name, 'FM6 · 05/26' as the label, the stripe "
    "meter over the pool. A preset the user overwrote carries a small square mark after its name (state 5). It loads "
    "at once, as today.",
    held=["MAJ"], lit=["D4", "F#4", "A4"], encoders=IDLE_ENC | {"PRESETS": ("SOUND", "05")},
    screen=scr({"kind": "meter", "value": "05", "sub": "FM PAD", "label": "FM6 · 05/26", "col": C_CHORD, "pct": 5 / 26, "segments": 26},
               note="PRESETS: the preset number big, its name, the engine and the place in its pool."),
))

# 2 ---------------------------------------------------------------- OPT + PRESETS: the engine
S.append(state(
    "2 · OPT held + PRESETS turned: change the engine",
    "OPT (GLO) held while PRESETS turns steps the engine, as a picker: the engine big in white, the neighbours peeking "
    "(the firmware's order: ANALOG FM6 VA PHASE CZ-1 LOFI VOICE TRIO WHEEL PHYS NOISE), the pool's size under it. "
    "The part switches to that engine's first preset (or the one it was on last in that pool: remembered per engine) "
    "when OPT is released. The editor's EDIT-held picker stays for the editor; this is the quick way outside it.",
    held=["MAJ"], lit=["D4", "F#4", "A4"], buttons={"GLO": LIT}, encoders=IDLE_ENC | {"PRESETS": ("ENGINE", "VA")},
    screen=scr({"kind": "picker", "items": ENGINES, "sel": 2, "label": "engine · OPT + PRESETS", "col": "white", "value": "25 presets", "orient": "h"},
               note="OPT + PRESETS: the engine picker, horizontal, the pool size as the value."),
))

# 3 ---------------------------------------------------------------- SAVE: overwrite or new
S.append(state(
    "3 · SAVE tapped: overwrite the current preset, or save as new",
    "SAVE tap (on the view or in the editor): two choices, one at a time in the picker: 'Overwrite' (the current "
    "preset, named under it: FM PAD, factory or not) and 'Save as new' (the pool's next free place, U27 here). OCT+ "
    "takes the choice: Overwrite saves at once and keeps the name; Save as new opens the naming screen (state 4). "
    "The default choice is Overwrite when the sound is a user preset or an edited one, Save as new otherwise. OCT- "
    "cancels. SAVE held (loops) is unchanged.",
    held=["MAJ"], lit=["D4", "F#4", "A4"], buttons={"SAVE": LIT, "OCT-": LIT, "OCT+": BLINK},
    button_notes={"SAVE": "tap: the save dialog; held: the loops' save / load / delete", "OCT+": "takes the choice"},
    encoders=IDLE_ENC | {"KNOB1": ("CHOICE", "OVERWRITE")},
    screen=scr({"kind": "picker", "items": ["Overwrite", "Save as new"], "sel": 0, "label": "save · FM6", "col": "white", "value": "FM PAD *"},
               foot="OCT+: save · OCT-: cancel", note="the save dialog: Overwrite (the current preset's name under it) or Save as new."),
))

# 4 ---------------------------------------------------------------- the naming screen (today's)
S.append(save_state(
    "4 · Save as new: the naming screen",
    "After 'Save as new': today's naming screen, prefilled with the current name (grey) and the new place (FM6 · 27); "
    "the white roots type, D#4 space, F#4 deletes, KNOB 2 the last letter, SAVE again or OCT+ saves, OCT- cancels.",
    "FM6 · 27", "WARM EP_",
))

# 5 ---------------------------------------------------------------- an overwritten factory preset
S.append(state(
    "5 · PRESETS on a factory preset the user overwrote",
    "The pool is one list: factory preset 02 FM BELL was overwritten by the user; it shows with a small square mark "
    "after the name. Turning PRESETS passes through it like any other. There is no separate user bank.",
    held=["MAJ"], lit=["D4", "F#4", "A4"], encoders=IDLE_ENC | {"PRESETS": ("SOUND", "02")},
    screen=scr({"kind": "meter", "value": "02", "sub": "MY BELL ▪", "label": "FM6 · 02/26", "col": C_CHORD, "pct": 2 / 26, "segments": 26},
               note="an overwritten factory preset: the user's name with the square mark."),
))

# 6 ---------------------------------------------------------------- reset to factory / delete
S.append(state(
    "6 · SAVE held 1 s in the dialog: reset to factory (or delete)",
    "SAVE held 1 s inside the save dialog: on an overwritten factory preset the question is 'reset to factory?' (the "
    "number huge in red, the factory name under it); on a preset the user added, 'delete?'. OCT+ does it, OCT- keeps.",
    held=["MAJ"], lit=["D4", "F#4", "A4"], buttons={"SAVE": LIT, "OCT-": LIT, "OCT+": BLINK},
    encoders=IDLE_ENC,
    screen=scr({"kind": "big", "value": "02", "label": "reset to factory?", "sub": "FM BELL", "col": "red", "size": 96},
               foot="OCT+: reset · OCT-: keep", note="the red question, as today's delete."),
))

for _s in S:
    _h = _s["screen"].get("header")
    if _h and _h.get("batt") == 255:
        _h["batt"] = False

design = {
    "format": "fm1-panel-design", "version": 1, "device": "M-VAVE FM-1",
    "name": "ChoralRoot FM-1 preset model mockups",
    "palette": "MOD",
    "labels": {"buttons": BUTTON_LABELS, "encoders": ENCODER_LABELS},
    "notes": "Presets per engine, one editable pool each: PRESETS turns inside the current engine's pool (1), OPT + PRESETS "
             "changes the engine (2), SAVE offers Overwrite or Save as new (3) then the name (4), an overwritten factory "
             "preset carries a mark (5), SAVE held in the dialog resets it to factory or deletes a user preset (6).",
    "states": S,
}

if __name__ == "__main__":
    with open(OUT, "w") as f:
        json.dump(design, f, indent=1)
    print("wrote", OUT, "with", len(S), "states")
