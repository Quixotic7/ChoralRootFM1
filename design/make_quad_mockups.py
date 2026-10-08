#!/usr/bin/env python3
"""Generate the QUAD (Digitone-style 4-op FM) engine mock-ups (fm1-panel-design JSON): its editor screens.

  python3 design/make_quad_mockups.py       # writes design/choralroot-fm1-quad-mockups.json

docs/QUAD.md is the plan. The screens use the editor's kinds (stack, edit8) plus three additions the designer gains
for this engine: the `algo` wide band (the four operators as boxes with their routing, the feedback loop, the X / Y
outputs; redrawn as ALGO turns), the `ade` wide band (attack to LEV, decay to END, held: the operator envelopes), and
the `harm` cell glyph (a sine growing harmonics: + the odd series toward a square, - all harmonics toward a saw).
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_mockups import (state, scr, cell, editor_screen, stack_panel, EDITOR_BUTTON_LABELS, BUTTON_LABELS,  # noqa: E402
                          ENCODER_LABELS, IDLE_ENC, LIT, BLINK, OFF, C_CHORD)
from make_editor_mockups import edit8  # noqa: E402

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "choralroot-fm1-quad-mockups.json")


def g(label, value, glyph=None, pct=None, pct2=None, **kw):
    c = cell(label, value, glyph, pct, **kw)
    if pct2 is not None:
        c["pct2"] = round(pct2, 3)
    return c


ED = {"EDIT": BLINK}
S = []

# 1 ---------------------------------------------------------------- OSC: the four operators
S.append(state(
    "1 · QUAD · OSC: the four operators",
    "The OSC group is a stack of the four operators C, A, B1, B2 (the Digitone's): Ratio (C fixed at 1.00, dim), "
    "Offset (a fine ratio offset), Level (A and B: the modulation ceiling; C: –), Harm (the carriers' waveshape: the "
    "harm glyph shows the sine growing harmonics). KNOB 1-4 edit the active row (A here); a tap on OSC steps the row.",
    held=["MAJ"], lit=["D4", "F#4", "A4"], buttons=ED | {"FX": LIT}, button_labels=EDITOR_BUTTON_LABELS,
    encoders={"SELECT": ("OP", "A"), "KNOB1": ("RATIO", "2.00"), "KNOB2": ("OFFSET", "+0.00"), "KNOB3": ("LEVEL", "80"), "KNOB4": ("HARM", "+8")},
    screen=editor_screen(stack_panel("GLASS EP", "OP A · A", ["Ratio", "Offset", "Level", "Harm"], [
        ("C", [g("Ratio", "1.00", dim=True), g("Offset", "+0.00"), None, g("Harm", "+8", "harm", 0.5 + 8 / 52)]),
        ("A", [g("Ratio", "2.00", pct=0.3), g("Offset", "+0.00", pct=0.5), g("Level", "80", "bar", 80 / 127), g("Harm", "+8", "harm", 0.5 + 8 / 52)]),
        ("B1", [g("Ratio", "0.50", pct=0.1), g("Offset", "+0.00", pct=0.5), g("Level", "60", "bar", 60 / 127), None]),
        ("B2", [g("Ratio", "0.50", pct=0.1), g("Offset", "+0.01", pct=0.51), g("Level", "60", "bar", 60 / 127), None]),
    ], 1), note="QUAD's OSC stack: C A B1 B2; the harm glyph on the carriers; the active row A on the knobs."),
))

# 2 ---------------------------------------------------------------- ALGO page with the diagram
S.append(state(
    "2 · QUAD · ALGO: the algorithm diagram over Algo · Feedback · Mix · Phase",
    "OSC's last screen: ALGO. The wide band draws the algorithm: the four operators as boxes, modulators above "
    "carriers with arrows, the feedback loop on its operator, the X and Y outputs at the bottom (the MIX cell's "
    "value tilts toward X or Y). Turning KNOB 1 redraws the diagram for algorithms 1..8.",
    held=["MAJ"], lit=["D4", "F#4", "A4"], buttons=ED | {"FX": LIT}, button_labels=EDITOR_BUTTON_LABELS,
    encoders={"SELECT": ("OSC", "ALGO"), "KNOB1": ("ALGO", "3"), "KNOB2": ("FEEDBACK", "40"), "KNOB3": ("MIX", "+12"), "KNOB4": ("PHASE", "RESET")},
    screen=editor_screen(edit8("GLASS EP", "ALGO", [[g("Algo", "3", pct=2 / 7), g("Feedback", "40", "bar", 40 / 127), g("Mix", "+12", bipolar=True, pct=0.5 + 12 / 126), g("Phase", "Reset")]], 0,
                               wide={"type": "algo", "algo": 3, "fdbk": 0.3, "mix": 0.6}),
                         note="the ALGO screen: the diagram of algorithm 3 redraws as KNOB 1 turns."),
))

# 3 ---------------------------------------------------------------- ENV A with the ADE band
S.append(state(
    "3 · QUAD · ENV A: the operator envelope (attack · decay · end · level)",
    "The ENV group: after the platform's amp ADSR, QUAD's two operator envelopes. ENV A is an edit8 under the ADE "
    "band: the modulation index rises to LEVEL over ATTACK, falls to END over DECAY, then holds; row B: Delay (the "
    "envelope starts late), Reset (restart on every note), Key trk, Vel. The band's segment being turned is thick in "
    "its knob's colour.",
    held=["MAJ"], lit=["D4", "F#4", "A4"], buttons=ED | {"ENV": LIT}, button_labels=EDITOR_BUTTON_LABELS,
    encoders={"SELECT": ("ENV", "A"), "KNOB1": ("ATTACK", "12"), "KNOB2": ("DECAY", "60"), "KNOB3": ("END", "20"), "KNOB4": ("LEVEL", "90")},
    screen=editor_screen(edit8("GLASS EP", "ENV A · mod", [
        [g("Attack", "12", pct=0.1), g("Decay", "60", pct=0.47), g("End", "20", pct=0.16), g("Level", "90", pct=0.71)],
        [g("Delay", "0", pct=0.0), g("Reset", "On"), g("Key trk", "40", pct=0.31), g("Vel", "50", pct=0.39)]], 0,
        wide={"type": "ade", "a": 0.1, "d": 0.47, "end": 0.16, "lev": 0.71, "seg": 1}),
        note="ENV A: the ADE band (rise to LEVEL, fall to END, hold), the Decay segment thick (KNOB 2)."),
))

# 4 ---------------------------------------------------------------- HOME knobs / the picker
S.append(state(
    "4 · PRESETS on QUAD: the pool",
    "QUAD in the preset model: PRESETS turns inside its pool (00 INIT, the factory presets, yours); the popup names "
    "it like every engine. OPT + PRESETS reaches QUAD between FM6 and VA in the engine order.",
    held=["MAJ"], lit=["D4", "F#4", "A4"], encoders=IDLE_ENC | {"PRESETS": ("SOUND", "03")},
    screen=scr({"kind": "meter", "value": "03", "sub": "GLASS EP", "label": "QUAD · 03/17", "col": C_CHORD, "pct": 3 / 17, "segments": 17},
               note="PRESETS on QUAD: 03 GLASS EP of 17."),
))

# 5 ---------------------------------------------------------------- the harm glyph study
S.append(state(
    "5 · the harm glyph: HARM from -26 to +26",
    "The carriers' waveshape control as a picture: HARM 0 is a sine; positive values add the odd series (toward a "
    "square), negative values add every harmonic (toward a saw). Four values on one row (the editor's two-row kind "
    "used for the study).",
    buttons=ED, button_labels=EDITOR_BUTTON_LABELS, encoders={},
    screen=editor_screen(edit8("glyph study", "harm", [
        [g("Harm", "-26", "harm", 0.0), g("Harm", "-10", "harm", 0.5 - 10 / 52), g("Harm", "+10", "harm", 0.5 + 10 / 52), g("Harm", "+26", "harm", 1.0)],
        [g("Algo", "1", pct=0.0), g("Algo", "4", pct=3 / 7), g("Algo", "6", pct=5 / 7), g("Algo", "8", pct=1.0)]], 0),
        note="harm at four values; (row B: algorithm numbers, their diagrams are state 2's band)."),
))

for _s in S:
    _h = _s["screen"].get("header")
    if _h and _h.get("batt") == 255:
        _h["batt"] = False

design = {
    "format": "fm1-panel-design", "version": 1, "device": "M-VAVE FM-1",
    "name": "ChoralRoot FM-1 QUAD engine mockups",
    "palette": "MOD",
    "labels": {"buttons": BUTTON_LABELS, "encoders": ENCODER_LABELS},
    "notes": "QUAD, a Digitone-style four-operator FM engine (docs/QUAD.md): the OSC stack of C A B1 B2 with the harm glyph (1), "
             "the ALGO screen with the algorithm diagram band (2), the operator envelopes under the ADE band (3), the preset "
             "pool (4), the harm glyph at four values (5).",
    "states": S,
}

if __name__ == "__main__":
    with open(OUT, "w") as f:
        json.dump(design, f, indent=1)
    print("wrote", OUT, "with", len(S), "states")
