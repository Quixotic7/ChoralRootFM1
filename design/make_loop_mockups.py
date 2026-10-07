#!/usr/bin/env python3
"""Generate the loop-indicator mock-ups (fm1-panel-design JSON): what the screen shows while a loop PLAYS.

  python3 design/make_loop_mockups.py       # writes design/choralroot-fm1-loop-mockups.json

The problem (the user, 2026-10-07): while a loop plays, the firmware draws Orchid's progress ring round the edge of
EVERY screen (cr_ui.c cr_build_screen: cu_ring under every layer, page and view), so the chord name is pushed inside
a dotted circle, the pickers shrink into it, and the Options and the layers all carry it. The ring is right where the
loop is the subject (recording, the count-in, undo, the LOOP layer, the loop save dialog) and wrong everywhere else.

Three alternatives for "a loop is playing", drawn on the screens where it hurts most, plus the screens where the ring
stays. The companion of make_mockups.py (its helpers are imported); the designer draws the indicators with its `loop`
screen key (FORMAT.md): `bar` (a thin progress stripe), `dial` (a miniature ring in the top line), `mark` (a square
that fills on the downbeat).

  A  the beat stripe: a 3 px red stripe under the top line, filling left to right over the cycle, a gap at each bar;
     a white tick at the tip on the downbeat. Nothing else changes: the chord name, the pickers and Options keep the
     whole panel. The editor shows nothing (its screens use every pixel; LOOP's green LED is the indicator there).
  B  the corner dial: Orchid's ring shrunk to 16 px, at the right end of the top line next to "Loop 1".
  C  the pulse mark: no progress at all; "Loop 1" in red with a square that fills on each downbeat; the progress is
     only seen where the loop is the subject (the LOOP layer's ring).
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_mockups import (state, scr, chord, bubbles, editor_screen, osc_stack, WARM_OSC, EDITOR_BUTTON_LABELS,  # noqa: E402
                          BUTTON_LABELS, ENCODER_LABELS, IDLE_ENC, LAYER_BTN_NOTE, LAYER_FOOT, LIT, BLINK, OFF,
                          ROOT_WHITE, key_of, C_CHORD, C_BASS)

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "choralroot-fm1-loop-mockups.json")

# the moment every state shows: loop 1, 4 bars, bar 3 beat 2 (62 % of the cycle)
PCT, BARS, BAR = 0.62, 4, 3


def with_loop(screen, style, pos="top", on=False, pct=PCT):
    screen["loop"] = {"style": style, "pos": pos, "pct": pct, "bars": BARS, "bar": BAR, "on": on, "col": "red"}
    return screen


def gchord(**kw):
    return chord("G", cols={"root": C_CHORD}, bubbles=bubbles(["G4", "B4", "D5"]), **kw)


PLAY_BTN = {"PLAY": LIT}
PLAY_NOTE = {"PLAY": "lit: a loop exists; its green LED: playing"}
S = []

# 1 ---------------------------------------------------------------- today
S.append(state(
    "1 · today: the loop plays, the chord view wears the ring",
    "As built: while loop 1 plays, Orchid's ring (the dotted circle, the red progress) is drawn round every screen, so "
    "the chord name sits inside it and every picker, layer and Options page shrinks into it. The user's verdict: "
    "invasive. The states that follow are the alternatives.",
    held=["MAJ"], lit=["G4", "B4", "D5"], buttons=PLAY_BTN, play_green=1, button_notes=PLAY_NOTE, encoders=IDLE_ENC,
    screen=scr(gchord(), key="Loop 1", ring=PCT, note="today: the ring round the chord, the name pushed inside it."),
))

# 2..5 ------------------------------------------------------------- A: the beat stripe
S.append(state(
    "2 · A · the beat stripe: the chord view",
    "Proposal A. A loop playing shows as a 3 px red stripe under the top line, filling left to right over the cycle "
    "(here 62 %: bar 3 of 4), with a thin gap at each bar so the stripe reads as bar segments: the stripe meter "
    "language the knobs already use, turned into a clock. 'Loop 1' stays top-left in red. The chord name has the whole "
    "panel back. Motion: the stripe fills continuously; a white tick flashes at its tip on every downbeat; on the "
    "cycle's wrap the stripe sweeps off to the right and starts again (the idle stripes' sweep, in red).",
    held=["MAJ"], lit=["G4", "B4", "D5"], buttons=PLAY_BTN, play_green=1, button_notes=PLAY_NOTE, encoders=IDLE_ENC,
    screen=with_loop(scr(gchord(), key="Loop 1", note="A: the red stripe under the top line, 62 % filled with a gap at each bar; the chord fills the panel as when no loop plays."), "bar"),
))

S.append(state(
    "3 · A · the beat stripe at a downbeat (bar 4)",
    "The same stripe one bar later, on the downbeat of bar 4: the tip carries a white tick for one frame (~100 ms), "
    "the loop's pulse without a metronome. With Options > Motion = Off the tick is skipped and the stripe just fills.",
    held=["MAJ"], lit=["G4", "B4", "D5"], buttons=PLAY_BTN, play_green=1, button_notes=PLAY_NOTE, encoders=IDLE_ENC,
    screen=with_loop(scr(gchord(), key="Loop 1", note="A: the downbeat frame: a white tick at the stripe's tip, 75 % filled."), "bar", on=True, pct=0.75),
))

perf_modes = ["STRUM", "STR 2", "SLOP", "ARP", "ARP 2", "PATT", "HARP"]
S.append(state(
    "4 · A · the perform layer open while the loop plays",
    "A layer over a playing loop: the picker keeps its full size and its footer hints (today it is squeezed into the "
    "ring and loses its position marks); the stripe is the only trace of the loop. The same for the KEY, FX, BASS and "
    "METRO layers and the engine picker.",
    keys=[OFF] * 27, lit=["G4"], buttons={"ARP": BLINK, "PLAY": LIT}, play_green=1,
    key_labels={ROOT_WHITE[i]: m for i, m in enumerate(perf_modes)},
    button_notes={"ARP": LAYER_BTN_NOTE},
    encoders={"KNOB1": ("RATE", "1/8"), "KNOB2": ("ORDER", "UP"), "KNOB3": ("RANGE", "1 OCT"), "KNOB4": ("GATE", "70%")},
    screen=with_loop(scr({'kind': 'picker', 'items': ['Strum', 'Strum 2 Octaves', 'Slop', 'Arpeggiate', 'Arp 2 Octaves', 'Pattern', 'Harp'], 'sel': 3, 'label': 'perform', 'col': 'white', 'value': '1/8'},
                         key="Loop 1", foot="a root: mode · " + LAYER_FOOT, note="A: the perform picker at full size, its marks and footer back; the stripe under the top line is the loop."), "bar"),
    chord_block=False,
))

S.append(state(
    "5 · A · Options while the loop plays",
    "Options with a loop playing: one setting per screen, as drawn in the walk-through; the stripe under the top line. "
    "(Today Options is drawn inside the ring.)",
    buttons={"GLO": LIT, "OCT-": LIT, "OCT+": BLINK, "PLAY": LIT}, play_green=1,
    encoders={"SELECT": ("SCROLL", ""), "KNOB1": ("VALUE", ""), "KNOB2": "", "KNOB3": "", "KNOB4": ""},
    screen=with_loop(scr({'kind': 'picker', 'items': ['Hold Time', 'USB Record', 'USB Level', 'Version', 'Calibrate'], 'sel': 1, 'label': 'options · KNOB 1 sets', 'col': 'white', 'value': 'On'},
                         key="Loop 1", batt=True, note="A: Options at full size; the stripe is the loop."), "bar"),
))

# 6 ---------------------------------------------------------------- A in the editor: nothing
S.append(state(
    "6 · A · the sound editor while the loop plays: no indicator",
    "The editor's screens use every pixel (no header bar, no footer; the knob bars sit on the bottom row), so no "
    "stripe is drawn there: LOOP's green LED says the loop plays, 'Loop 1' is back the moment the editor is left. "
    "(Today the ring is drawn over the editor's rows too.)",
    held=["MAJ"], lit=["D4", "F#4", "A4"], buttons={"EDIT": BLINK, "FX": LIT, "PLAY": LIT}, play_green=1,
    button_labels=EDITOR_BUTTON_LABELS,
    button_notes={"EDIT": "blinks: the editor is open", "PLAY": "FX group in the editor; its green LED still shows the loop playing"},
    encoders={"SELECT": ("OSC", "1"), "KNOB1": ("WAVE", "PWM"), "KNOB2": ("LEVEL", "63%"), "KNOB3": ("COARSE", "0"), "KNOB4": ("FINE", "0")},
    screen=editor_screen(osc_stack("WARM PAD", WARM_OSC, 0), note="A: the editor untouched; the loop is on the LED only."),
))

# 7 ---------------------------------------------------------------- A variant: the stripe at the bottom edge
S.append(state(
    "7 · A' · the stripe along the bottom edge instead",
    "The same stripe placed along the bottom edge (4 px). Weighed against the top placement: it collides with the "
    "layers' footer hints and the editor's knob bars, and the eye reads the top line for status anyway; drawn here so "
    "the two can be compared on the sheet.",
    held=["MAJ"], lit=["G4", "B4", "D5"], buttons=PLAY_BTN, play_green=1, button_notes=PLAY_NOTE, encoders=IDLE_ENC,
    screen=with_loop(scr(gchord(), key="Loop 1", note="A': the stripe at the bottom edge."), "bar", pos="bottom"),
))

# 8 ---------------------------------------------------------------- B: the corner dial
S.append(state(
    "8 · B · the corner dial",
    "Proposal B. Orchid's ring shrunk to a 16 px dial at the right end of the top line (dotted track, red progress from "
    "12 o'clock), next to 'Loop 1'. Keeps the ring's idea without taking the panel; small enough to ignore, big enough "
    "to glance at. Motion: the arc grows continuously; a pulse of the dial's size on the downbeat.",
    held=["MAJ"], lit=["G4", "B4", "D5"], buttons=PLAY_BTN, play_green=1, button_notes=PLAY_NOTE, encoders=IDLE_ENC,
    screen=with_loop(scr(gchord(), key="Loop 1", note="B: a 16 px ring in the top-right corner, 62 % round."), "dial"),
))

# 9 ---------------------------------------------------------------- C: the pulse mark
S.append(state(
    "9 · C · the pulse mark (no progress)",
    "Proposal C. No progress on the views at all: 'Loop 1' top-left in red and an 8 px square at the right end of the "
    "top line that fills on every downbeat and empties over the beat (the LOOP LED's pulse on the screen). The cycle "
    "position is shown only where the loop is the subject (the LOOP layer's ring, state 11). The quietest option; the "
    "player feels the bar, the screen does not count it.",
    held=["MAJ"], lit=["G4", "B4", "D5"], buttons=PLAY_BTN, play_green=1, button_notes=PLAY_NOTE, encoders=IDLE_ENC,
    screen=with_loop(scr(gchord(), key="Loop 1", note="C: the square top-right filled on the downbeat frame; outlined between beats."), "mark", on=True),
))

# 10, 11 ----------------------------------------------------------- where the ring stays
S.append(state(
    "10 · the ring stays where the loop is the subject: recording",
    "Unchanged in every proposal: while recording or overdubbing (and during the count-in and the undo screen) the "
    "loop IS the subject, so the red ring runs round the edge as the Orchid's does, with 'Rec' and bar.beat in the top "
    "line. It leaves with the take; playback then shows the chosen indicator.",
    held=["MAJ"], lit=["F4", "A4", "C5"], buttons={"REC": BLINK, "PLAY": LIT}, encoders=IDLE_ENC,
    screen=scr(chord("F", cols={"root": C_CHORD}, bubbles=bubbles(["F4", "A4", "C5"])), key="Rec", right="2.3", ring=0.35, ring_rec=True,
               note="recording: the red ring, as today."),
))

S.append(state(
    "11 · the ring stays in the LOOP layer while playing",
    "Unchanged: LOOP held opens the loop layer with the Overdub / Pause / Undo / Clear picker inside the ring (OCT+ "
    "does it); the ring shows the cycle. SAVE held (the loop save / load / delete) keeps its ring too. Everywhere else "
    "the playing loop is the stripe (A), the dial (B) or the mark (C).",
    held=["MAJ"], lit=["G4", "B4", "D5"], buttons={"PLAY": BLINK, "REC": LIT, "OCT+": BLINK}, play_green=1,
    button_notes={"PLAY": LAYER_BTN_NOTE, "OCT+": "OK: does the picked action"}, encoders=IDLE_ENC,
    screen=scr({'kind': 'picker', 'items': ['Overdub', 'Pause', 'Undo', 'Clear'], 'sel': 0, 'label': 'loop 1', 'col': 'red'}, key="Loop 1", ring=PCT,
               note="the loop layer: the picker inside the ring, as today."),
))

# 12 --------------------------------------------------------------- A with the overdub armed
S.append(state(
    "12 · A · overdub armed while playing (REC lit)",
    "REC tapped while the loop plays: the overdub is armed (REC lit) and the next chord opens a layer. In proposal A "
    "the stripe stays and the top line says 'Dub 3.2' at the right in red (today's text), so arming is visible without "
    "the ring; once the overdub records, the ring returns (state 10) because recording is the subject again.",
    held=["MAJ"], lit=["G4", "B4", "D5"], buttons={"PLAY": LIT, "REC": LIT}, play_green=1,
    button_notes={"REC": "lit: overdub armed; blinks once it records"}, encoders=IDLE_ENC,
    screen=with_loop(scr(gchord(), key="Loop 1", right="Dub 3.2", right_col="red", note="A: armed: the stripe and 'Dub 3.2' top-right in red; no ring until the overdub records."), "bar"),
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
             "Three alternatives: A the beat stripe (states 2-7, 12: a 3 px red stripe under the top line filling over the "
             "cycle, bar gaps, a white tick on the downbeat; nothing in the editor), B the corner dial (state 8: a 16 px ring "
             "in the top line), C the pulse mark (state 9: a square that fills on the downbeat, no progress). In all of them the "
             "ring stays where the loop is the subject: recording, the count-in, undo, the LOOP layer and the loop save dialog "
             "(states 10, 11). Recommended: A.",
    "states": S,
}

if __name__ == "__main__":
    with open(OUT, "w") as f:
        json.dump(design, f, indent=1)
    print("wrote", OUT, "with", len(S), "states")
