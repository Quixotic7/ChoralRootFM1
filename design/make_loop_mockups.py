#!/usr/bin/env python3
"""Generate the loop-indicator mock-ups (fm1-panel-design JSON): what the screen shows while a loop PLAYS.

  python3 design/make_loop_mockups.py       # writes design/choralroot-fm1-loop-mockups.json

The problem (the user, 2026-10-07): while a loop plays, the firmware draws Orchid's progress ring round the edge of
EVERY screen (cr_ui.c cr_build_screen: cu_ring under every layer, page and view), so the chord name is pushed inside
a dotted circle, the pickers shrink into it, and the Options and the layers all carry it. The ring is right where the
loop is the subject (recording, the count-in, undo, the LOOP layer, the loop save dialog) and wrong everywhere else.

Decided (the user, 2026-10-07, from the first sheet of three alternatives): **B, the corner dial**: Orchid's ring
shrunk to a 16 px dial at the right end of the top line, next to "Loop 1" (dotted track, red progress from 12
o'clock, a thicker arc for one frame on the downbeat). Not shown in the sound editor and not on the Options pages
(the LOOP LED carries it there); the ring stays where the loop is the subject. This sheet is the dial on every screen
it touches. The companion of make_mockups.py (its helpers are imported); the designer draws the dial with its `loop`
screen key (FORMAT.md, style `dial`).
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_mockups import (state, scr, chord, bubbles, editor_screen, osc_stack, WARM_OSC, EDITOR_BUTTON_LABELS,  # noqa: E402
                          BUTTON_LABELS, ENCODER_LABELS, IDLE_ENC, LAYER_BTN_NOTE, LAYER_FOOT, LIT, BLINK, OFF,
                          ROOT_WHITE, ROOT_BLACK, key_of, C_CHORD, C_BASS, C_KEY)

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "choralroot-fm1-loop-mockups.json")

PCT = 0.62                                     # loop 1, 4 bars, bar 3 beat 2


def dial(screen, on=False, pct=PCT):
    screen["loop"] = {"style": "dial", "pct": pct, "on": on, "col": "red"}
    return screen


def gchord(**kw):
    return chord("G", cols={"root": C_CHORD}, bubbles=bubbles(["G4", "B4", "D5"]), **kw)


PLAY_BTN = {"PLAY": LIT}
PLAY_NOTE = {"PLAY": "lit: a loop exists; its green LED: playing"}
S = []

# 1 ---------------------------------------------------------------- today
S.append(state(
    "1 · today: the loop plays, every screen wears the ring",
    "As built: while loop 1 plays, Orchid's ring (the dotted circle, the red progress) is drawn round every screen, so "
    "the chord name sits inside it and every picker, layer and Options page shrinks into it. Kept here for comparison.",
    held=["MAJ"], lit=["G4", "B4", "D5"], buttons=PLAY_BTN, play_green=1, button_notes=PLAY_NOTE, encoders=IDLE_ENC,
    screen=scr(gchord(), key="Loop 1", ring=PCT, note="today: the ring round the chord, the name pushed inside it."),
))

# 2, 3 ------------------------------------------------------------- the dial on the chord view
S.append(state(
    "2 · the corner dial: the chord view",
    "Decided: a loop playing shows as a 16 px dial at the right end of the top line (the dotted track in grey, the red "
    "progress from 12 o'clock; here 62 %: bar 3 of 4), with 'Loop 1' top-left in red. The chord has the whole panel. "
    "Motion: the arc grows continuously; on each downbeat the arc thickens for one frame (state 3); at the cycle's wrap "
    "the arc empties in one sweep.",
    held=["MAJ"], lit=["G4", "B4", "D5"], buttons=PLAY_BTN, play_green=1, button_notes=PLAY_NOTE, encoders=IDLE_ENC,
    screen=dial(scr(gchord(), key="Loop 1", note="the dial top-right, 62 % round; the chord fills the panel.")),
))

S.append(state(
    "3 · the dial on a downbeat (bar 4)",
    "The downbeat of bar 4: the dial's arc is drawn thick for one frame (~100 ms), the loop's pulse without a "
    "metronome. With Options > Motion = Off the pulse is skipped and the arc just grows.",
    held=["MAJ"], lit=["G4", "B4", "D5"], buttons=PLAY_BTN, play_green=1, button_notes=PLAY_NOTE, encoders=IDLE_ENC,
    screen=dial(scr(gchord(), key="Loop 1", note="the downbeat frame: the arc thick, 75 % round."), on=True, pct=0.75),
))

# 4..6 ------------------------------------------------------------- the dial on the layers
perf_modes = ["STRUM", "STR 2", "SLOP", "ARP", "ARP 2", "PATT", "HARP"]
S.append(state(
    "4 · the dial over the perform layer",
    "A layer over a playing loop: the picker keeps its full size, its marks and its footer hints (today it is squeezed "
    "into the ring); the dial sits in the top line. The same for the KEY, FX, BASS and METRO layers and the engine "
    "picker.",
    keys=[OFF] * 27, lit=["G4"], buttons={"ARP": BLINK, "PLAY": LIT}, play_green=1,
    key_labels={ROOT_WHITE[i]: m for i, m in enumerate(perf_modes)},
    button_notes={"ARP": LAYER_BTN_NOTE},
    encoders={"KNOB1": ("RATE", "1/8"), "KNOB2": ("ORDER", "UP"), "KNOB3": ("RANGE", "1 OCT"), "KNOB4": ("GATE", "70%")},
    screen=dial(scr({'kind': 'picker', 'items': ['Strum', 'Strum 2 Octaves', 'Slop', 'Arpeggiate', 'Arp 2 Octaves', 'Pattern', 'Harp'], 'sel': 3, 'label': 'perform', 'col': 'white', 'value': '1/8'},
                    key="Loop 1", foot="a root: mode · " + LAYER_FOOT, note="the perform picker at full size; the dial in the top line.")),
    chord_block=False,
))

S.append(state(
    "5 · the dial over the key layer",
    "The key layer (the keyboard slides up, the tonic lit yellow) with the loop playing: 'Key: C' owns the top-left in "
    "yellow, so the loop shows as the dial alone at the right. Where the top-left is taken by Key Mode, the dial is "
    "the whole loop indicator.",
    lit=["C5"], buttons={"SEL": BLINK, "PLAY": LIT}, play_green=1,
    key_labels={k: n for k, n in ((key_of("C5"), "C"),)},
    button_notes={"SEL": LAYER_BTN_NOTE},
    encoders={"KNOB1": ("TONIC", "C"), "KNOB2": ("SCALE", "MAJ"), "KNOB3": ("TRANSPOSE", "+0"), "KNOB4": ("SINGLE NOTES", "FULL")},
    screen=dial(scr({"kind": "keyboard", "title": "select key", "titleSize": 26, "col": C_KEY, "lit": [{"k": "C5", "col": C_KEY}], "labels": {str(ROOT_WHITE[6]): "C"}},
                    key="Key: C", foot="MIN: minor · " + LAYER_FOOT, note="Key Mode owns the top-left; the dial alone says the loop plays.")),
))

S.append(state(
    "6 · the dial with Key Mode on the chord view",
    "Key Mode on and a loop playing: 'Key: C' top-left in yellow, the dial top-right in red. Both fit the top line; "
    "the chord keeps the panel.",
    held=["MIN"], lit=["E4", "G4", "B4"], buttons={"SEL": LIT, "PLAY": LIT}, play_green=1, encoders=IDLE_ENC,
    screen=dial(scr(chord("E", quality="m", cols={"root": C_CHORD}, bubbles=bubbles(["E4", "G4", "B4"])), key="Key: C",
                    note="Key: C and the dial share the top line.")),
))

# 7 ---------------------------------------------------------------- overdub armed
S.append(state(
    "7 · overdub armed while playing (REC lit)",
    "REC tapped while the loop plays: the overdub is armed (REC lit) and the next chord opens a layer. The top line "
    "says 'Dub 3.2' at the right in red, left of the dial; once the overdub records, the ring returns (state 9) "
    "because recording is the subject again.",
    held=["MAJ"], lit=["G4", "B4", "D5"], buttons={"PLAY": LIT, "REC": LIT}, play_green=1,
    button_notes={"REC": "lit: overdub armed; blinks once it records"}, encoders=IDLE_ENC,
    screen=dial(scr(gchord(), key="Loop 1", right="Dub 3.2", right_col="red", note="armed: 'Dub 3.2' left of the dial; no ring until the overdub records.")),
))

# 8 ---------------------------------------------------------------- the editor and Options: none
S.append(state(
    "8 · the sound editor and Options: no dial",
    "Decided: the editor's screens (no header bar; every pixel used) and the Options pages show nothing of the loop: "
    "LOOP's green LED says it plays, and the dial is back the moment they are left. Shown: the editor's OSC stack.",
    held=["MAJ"], lit=["D4", "F#4", "A4"], buttons={"EDIT": BLINK, "FX": LIT, "PLAY": LIT}, play_green=1,
    button_labels=EDITOR_BUTTON_LABELS,
    button_notes={"EDIT": "blinks: the editor is open", "PLAY": "FX group in the editor; its green LED still shows the loop playing"},
    encoders={"SELECT": ("OSC", "1"), "KNOB1": ("WAVE", "PWM"), "KNOB2": ("LEVEL", "63%"), "KNOB3": ("COARSE", "0"), "KNOB4": ("FINE", "0")},
    screen=editor_screen(osc_stack("WARM PAD", WARM_OSC, 0), note="the editor untouched; the loop is on the LED only (Options the same)."),
))

# 9, 10 ------------------------------------------------------------ where the ring stays
S.append(state(
    "9 · the ring stays where the loop is the subject: recording",
    "Unchanged: while recording or overdubbing (and during the count-in and the undo screen) the loop IS the subject, "
    "so the red ring runs round the edge as the Orchid's does, with 'Rec' and bar.beat in the top line. It leaves with "
    "the take; playback then shows the dial.",
    held=["MAJ"], lit=["F4", "A4", "C5"], buttons={"REC": BLINK, "PLAY": LIT}, encoders=IDLE_ENC,
    screen=scr(chord("F", cols={"root": C_CHORD}, bubbles=bubbles(["F4", "A4", "C5"])), key="Rec", right="2.3", ring=0.35, ring_rec=True,
               note="recording: the red ring, as today."),
))

S.append(state(
    "10 · the ring stays in the LOOP layer while playing",
    "Unchanged: LOOP held opens the loop layer with the Overdub / Pause / Undo / Clear picker inside the ring (OCT+ "
    "does it); the ring shows the cycle. SAVE held (the loop save / load / delete) keeps its ring too.",
    held=["MAJ"], lit=["G4", "B4", "D5"], buttons={"PLAY": BLINK, "REC": LIT, "OCT+": BLINK}, play_green=1,
    button_notes={"PLAY": LAYER_BTN_NOTE, "OCT+": "OK: does the picked action"}, encoders=IDLE_ENC,
    screen=scr({'kind': 'picker', 'items': ['Overdub', 'Pause', 'Undo', 'Clear'], 'sel': 0, 'label': 'loop 1', 'col': 'red'}, key="Loop 1", ring=PCT,
               note="the loop layer: the picker inside the ring, as today."),
))

for _s in S:                                   # the battery shows on the Options page only (255 = none; the designer
    _h = _s["screen"].get("header")              # would draw 255 as a full battery)
    if _h and _h.get("batt") == 255:
        _h["batt"] = False

assert len(S) <= 24

design = {
    "format": "fm1-panel-design", "version": 1, "device": "M-VAVE FM-1",
    "name": "ChoralRoot FM-1 loop indicator mockups",
    "palette": "MOD",
    "labels": {"buttons": BUTTON_LABELS, "encoders": ENCODER_LABELS},
    "notes": "What the screen shows while a loop plays. Today Orchid's progress ring is drawn round every screen (state 1). "
             "Decided (2026-10-07): the corner dial, a 16 px ring at the right end of the top line (states 2-7: the chord view, "
             "the downbeat pulse, the perform and key layers, Key Mode, the overdub armed); nothing in the sound editor or "
             "Options (8); the ring stays where the loop is the subject: recording, the count-in, undo, the LOOP layer and "
             "the loop save dialog (9, 10).",
    "states": S,
}

if __name__ == "__main__":
    with open(OUT, "w") as f:
        json.dump(design, f, indent=1)
    print("wrote", OUT, "with", len(S), "states")
