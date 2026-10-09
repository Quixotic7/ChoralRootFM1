#!/usr/bin/env python3
"""Generate the looper's PLAY menu and record-mode mock-ups (fm1-panel-design JSON).

  python3 design/make_looper_mockups.py     # writes design/choralroot-fm1-looper-mockups.json

docs/LOOPER-MODES.md (2026-10-09, proposal): five record modes (Overwrite, Advance, Overdub, Replace, Step) and one
PLAY menu (LOOP held, locked open as every layer) under the top line: the slot strip (PRESETS), the mode picker
(ALGORITHM), the middle (SELECT: the length while idle; the record timeline while recording or playing; the step
grid in Step), the knob row (Quantize, Count-in, Level, Keys). GLO (OPT) in the menu toggles Keys: Play (the roots
and the chord block play, so the menu stays open while you play into a take) or Loops (today's slot shortcuts).

The designer draws the menu with its `loopmenu` panel (FORMAT.md: `slots`, `mode`, `mid` = a text, a `timeline` or
a `steps` object, `cells`, `foot`) and the corner dial with the `loop` screen key (make_loop_mockups.py's `dial`).
Layout on the 240 x 240 screen: the top line 0-24 (the slot's name, the dial and its right text), the slot strip
28-54, the mode band 56-92, the middle 92-168 (92-154 with a foot), the knob row 168-240 (154-226), the foot 226-240.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_mockups import (state, scr, BUTTON_LABELS, ENCODER_LABELS, LAYER_BTN_NOTE, LIT, BLINK, DIM, OFF,  # noqa: E402
                          ROOT_WHITE, ROOT_BLACK, LOCK_KEY, key_of)
from make_layers_mockups import g  # noqa: E402
from make_loop_mockups import dial  # noqa: E402

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "choralroot-fm1-looper-mockups.json")

MODES = ["Overwrite", "Advance", "Overdub", "Replace", "Step"]
USED = [1, 3, 4]                                        # slots 1, 3 and 4 hold loops
MODE_COL = "white"                                      # the mode picker's big item (the menu itself is red)

# the loop in slot 3 (4 bars): layer 1 a chord on each half bar, layer 2 an overdub (positions in bars from the start)
LAYER1 = [0.0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 3.5]
LAYER2 = [0.25, 0.75, 1.75, 2.25, 2.375, 3.25, 3.75]


def cells(keys="Play", hot=None):
    """the knob row: QUANTIZE (pulses), COUNT-IN (a gate), LEVEL (a bar), KEYS (Play / Loops, big text)"""
    return [g("Quantize", "1/16", "echoes", 0.8, 1.0), g("Count-in", "On", "gate", 1.0), g("Level", "100", "bar", 1.0),
            g("Keys", keys, big=True)]


def menu(slot, mode, mid, keys="Play", used=USED, hot=None, jump=None, foot=None):
    """the PLAY menu panel: the slot strip, the mode picker, the middle, the knob row (and an optional foot)"""
    s = {"kind": "slots", "n": 10, "sel": slot, "used": list(used), "col": "red"}
    if jump:
        s["jump"] = jump
    p = {"kind": "loopmenu", "col": "red", "slots": s, "mode": {"items": MODES, "sel": MODES.index(mode), "col": MODE_COL},
         "mid": mid, "cells": cells(keys), "hot": hot}
    if foot:
        p["foot"] = foot
    return p


def length(t, sub=""):
    p = {"t": t, "col": "red"}
    if sub:
        p["sub"] = sub
    return p


def timeline(pos, bars=4, lanes=(), new=None, free=False, elapsed=None, erase=None):
    t = {"kind": "timeline", "bars": bars, "beats": 4, "pos": pos, "lanes": [list(l) for l in lanes], "free": free}
    if new is not None:
        t["new"] = list(new)
    if elapsed:
        t["elapsed"] = elapsed
    if erase:
        t["erase"] = list(erase)
    return t


def steps(filled, cursor, name, right, bars=4):
    return {"kind": "steps", "bars": bars, "per": 16, "filled": list(filled), "cursor": cursor, "name": name, "right": right, "h": 34}


def enc(slot, mode, length_, keys="Play", select=("LENGTH", None)):
    return {"PRESETS": ("LOOP SLOT", str(slot)), "ALGORITHM": ("REC MODE", mode.upper()),
            "SELECT": (select[0], select[1] or length_.upper()), "KNOB1": ("QUANTIZE", "1/16"), "KNOB2": ("COUNT-IN", "ON"),
            "KNOB3": ("LOOP LEVEL", "100"), "KNOB4": ("KEYS", keys.upper())}


MENU_NOTE = {"PLAY": LAYER_BTN_NOTE,
             "GLO": "OPT tapped in the menu: Keys Play / Loops (saved; KNOB 4 turns the same cell)"}

# Keys = Loops: today's shortcuts (white roots D4..F5 = slots 1..10, D#4 held 1 s = clear, F#4 = undo)
SLOT_LABELS = {ROOT_WHITE[i]: str(i + 1) for i in range(10)}
SLOT_LABELS[ROOT_BLACK[0]] = "CLEAR"
SLOT_LABELS[ROOT_BLACK[1]] = "UNDO"


def slot_keys(sel, used=USED):
    """the key LEDs with Keys = Loops: the selected slot blinks, the slots holding a loop lit, CLEAR / UNDO dim"""
    k = [OFF] * 27
    for i in used:
        k[ROOT_WHITE[i - 1]] = LIT
    k[ROOT_WHITE[sel - 1]] = BLINK
    k[ROOT_BLACK[0]] = k[ROOT_BLACK[1]] = DIM
    return k


S = []

# 1 ------------------------------------------------------------------ the menu, idle
S.append(state(
    "1 · PLAY menu: idle, slot 3, Overwrite, 4 bars (Keys = Play)",
    "LOOP held past 300 ms: the PLAY menu opens and stays (LOOP blinks; OCT- or HOME closes it, a LOOP tap still plays / "
    "stops). One red screen under the top line: the SLOT STRIP (PRESETS): ten cells, filled = holds a loop (1, 3, 4), "
    "the selected one red and taller, its name 'Loop 3' in the top line; the MODE picker (ALGORITHM): Overwrite big, "
    "Advance peeking (Overwrite · Advance · Overdub · Replace · Step); the MIDDLE (SELECT): the length, '4 bars'; the "
    "KNOB ROW: QUANTIZE (pulses), COUNT-IN (a gate), LEVEL (a bar), KEYS. Keys = Play (the default, a saved setting): the "
    "roots and the chord block play as on the chord view, so the menu can stay open while you play into a take; the "
    "keys keep the chord view's LEDs and labels (no slot labels).",
    buttons={"PLAY": BLINK}, button_notes=MENU_NOTE, encoders=enc(3, "Overwrite", "4 bars"),
    key_notes={key_of("D4"): "Keys = Play: the roots and the chord block play as usual with the menu open"},
    screen=scr(menu(3, "Overwrite", length("4 bars")), key="Loop 3",
               note="the PLAY menu: the slot strip, the mode, the length, the four knobs. PRESETS slides the red cell along "
                    "the strip (the top line's name follows), ALGORITHM flips the mode like every picker, SELECT squeezes "
                    "the next length in."),
))

# 2 ------------------------------------------------------------------ Keys = Loops
S.append(state(
    "2 · GLO tapped in the menu: Keys = Loops (the slot shortcuts)",
    "GLO (OPT) tapped with the menu open toggles KEYS (KNOB 4 turns the same cell; the setting is saved, settings v8 "
    "`loop_keys`): Loops = today's shortcuts on the keys: the white roots D4..F5 are the slots 1..10 (3 blinks: "
    "selected; 1 and 4 lit: they hold loops), D#4 held 1 s clears, F#4 undoes. The chord block is off while Loops is on. "
    "GLO lit while Keys = Loops; the Keys cell says 'Loops' on its hot block for a moment.",
    keys=slot_keys(3), buttons={"PLAY": BLINK, "GLO": LIT}, button_notes=MENU_NOTE,
    key_labels=SLOT_LABELS, chord_block=False, encoders=enc(3, "Overwrite", "4 bars", keys="Loops"),
    key_notes={ROOT_WHITE[2]: "slot 3 selected (blinks); 1 and 4 hold loops (lit)",
               ROOT_BLACK[0]: "CLEAR (held 1 s) · UNDO on F#4"},
    screen=scr(menu(3, "Overwrite", length("4 bars"), keys="Loops", hot=3), key="Loop 3",
               note="Keys = Loops: the screen is unchanged but for the Keys cell (hot); the keys show the slot map."),
))

# 3 ------------------------------------------------------------------ ALGORITHM: the record mode
S.append(state(
    "3 · ALGORITHM turned: the record mode on Overdub",
    "ALGORITHM turns the record mode (a saved setting, settings v8 `loop_mode`, default Overwrite): Overdub big, Advance "
    "and Replace peeking. It applies to the next REC, in and out of the menu; it is also the top line's right text "
    "while armed ('Rec · Overdub', states 5, 10), so the mode is known without opening the menu. The old Overdub / "
    "Pause / Undo / Clear picker is gone: Pause = LOOP tap, Undo = REC held, Clear = D#4 held with Keys = Loops (or "
    "the SAVE layer's Delete).",
    buttons={"PLAY": BLINK}, button_notes=MENU_NOTE, encoders=enc(3, "Overdub", "4 bars"),
    screen=scr(menu(3, "Overdub", length("4 bars")), key="Loop 3",
               note="the mode flips like the pickers (the split-flap slide); its marks step with it."),
))

# 4 ------------------------------------------------------------------ PRESETS: an empty slot
S.append(state(
    "4 · PRESETS turned: slot 5 (empty)",
    "PRESETS turns the slot: slot 5 selected (an empty cell with the red outline, '5' big), 'Loop 5' in the top line and "
    "'empty' at its right. Leaving slot 3 saved it (a slot is its pattern, docs/LOOPER.md); stopped, the new slot "
    "loads at once (playing: at the end of the cycle). The middle keeps the length a take here will get.",
    buttons={"PLAY": BLINK}, button_notes=MENU_NOTE, encoders=enc(5, "Overwrite", "4 bars"),
    screen=scr(menu(5, "Overwrite", length("4 bars")), key="Loop 5", right="empty", right_col="grey",
               note="the red cell slides from 3 to 5; an empty slot is an outline, 'empty' fades in at the top right."),
))

# 5 ------------------------------------------------------------------ Overwrite armed (Free)
S.append(state(
    "5 · Overwrite, REC tapped (Free): armed",
    "REC tapped in Overwrite with the length Free: armed (REC lit), the first chord starts a fresh take. Slot 3's loop is "
    "still there: it is cleared when the take starts, not on arming (REC again before the first chord cancels and the "
    "loop is back). The top line says 'Rec · Overwrite' at the right, the dial its armed look (the grey track, the REC "
    "dot lit like the REC LED); the middle still says 'Free' (nothing recorded yet). Synced lengths start the count-in "
    "instead (its own screen with the ring).",
    buttons={"PLAY": BLINK, "REC": LIT}, button_notes=MENU_NOTE | {"REC": "lit: armed; blinks once it records"},
    encoders=enc(3, "Overwrite", "Free"),
    screen=dial(scr(menu(3, "Overwrite", length("Free", "the first chord starts the take")), key="Loop 3",
                    right="Rec · Overwrite", right_col="red",
                    note="armed: 'Rec · Overwrite' left of the dial, the REC dot lit; the middle waits for the first chord."),
                pct=0, rec="armed"),
))

# 6 ------------------------------------------------------------------ recording the first take (Free)
S.append(state(
    "6 · recording the first take (Free), bar 2",
    "The first chord started the take (REC blinks): the middle becomes the RECORD TIMELINE. Free: the bars are drawn as "
    "they pass (bar 1 done, bar 2 up to the playhead; from the fifth bar the current one stays at the right edge and "
    "the earlier ones shift left), the take's chords as marks as they are played (three in bar 1), the elapsed time "
    "under it. The dial records (the track red, the arc sweeping once a bar, the dot blinking); 'Rec 2.4' at the "
    "right. Keys = Play: the take is played with the menu open.",
    buttons={"PLAY": BLINK, "REC": BLINK}, button_notes=MENU_NOTE | {"REC": "blinks: recording"},
    encoders=enc(3, "Overwrite", "Free"),
    screen=dial(scr(menu(3, "Overwrite", timeline(1.75, bars=2, new=[0.0, 0.5, 0.75], free=True, elapsed="0:07")),
                    key="Loop 3", right="Rec 2.4", right_col="red",
                    note="recording Free: bar 2 grows with the playhead; each chord drops a mark where it starts."),
                pct=0.75, rec="rec"),
))

# 7 ------------------------------------------------------------------ playing
S.append(state(
    "7 · playing: 4 bars, two layers, bar 3",
    "Slot 3 plays (LOOP's green LED): the timeline shows the four bar boxes with their beat ticks, a lane per layer (the "
    "first take on top, dimmer; the overdub under it, full red), the playhead in bar 3; '3.2' (bar.beat) at the top "
    "right, the dial running. The mode is Overdub here, ready for state 8.",
    buttons={"PLAY": BLINK}, play_green=1, button_notes=MENU_NOTE, encoders=enc(3, "Overdub", "4 bars"),
    screen=dial(scr(menu(3, "Overdub", timeline(2.3, lanes=[LAYER1, LAYER2])), key="Loop 3", right="3.2",
                    note="playing: the playhead sweeps the four bars and jumps back at the wrap; the marks stay."),
                pct=2.3 / 4),
))

# 8 ------------------------------------------------------------------ overdubbing
S.append(state(
    "8 · Overdub: overdubbing (Am held)",
    "REC in Overdub while it plays: armed, the first chord opened a layer (REC blinks): the new layer is a third lane, "
    "full red, its marks appearing as they are played (the two older lanes a step dimmer); 'Dub 3.2' at the right, the "
    "dial's REC dot blinking over the playing arc. REC ends the layer, LOOP ends it and stops.",
    held=["MIN"], lit=["A4", "C5", "E5"], buttons={"PLAY": BLINK, "REC": BLINK}, play_green=1,
    button_notes=MENU_NOTE | {"REC": "blinks: overdubbing"}, encoders=enc(3, "Overdub", "4 bars"),
    key_notes={key_of("A4"): "Am played into the overdub with the menu open (Keys = Play)"},
    screen=dial(scr(menu(3, "Overdub", timeline(2.3, lanes=[LAYER1, LAYER2], new=[2.0, 2.25])), key="Loop 3",
                    right="Dub 3.2", right_col="red",
                    note="overdubbing: a third lane, its marks dropping in as they are played."),
                pct=2.3 / 4, rec="od"),
))

# 9 ------------------------------------------------------------------ replacing
S.append(state(
    "9 · Replace: a chord held over bar 3 (G held)",
    "Replace while it plays: REC arms, and while a gesture is held the loop's events that start inside the held span "
    "are erased (struck: grey with a red cross, on the span's dark-red band) and an event sounding when the gesture "
    "started is cut there; the gesture is recorded as a new layer (the new lane's mark). 'Rep 3.2' at the right, the "
    "dial's REC dot blinking. REC ends it; Undo (REC held) brings the erased events back with the layer (they are kept "
    "hidden until then, inside the 512-event cap).",
    held=["MAJ"], lit=["G4", "B4", "D5"], buttons={"PLAY": BLINK, "REC": BLINK}, play_green=1,
    button_notes=MENU_NOTE | {"REC": "blinks: replacing"}, encoders=enc(3, "Replace", "4 bars"),
    key_notes={key_of("G4"): "G held since 3.1: what it covers is replaced"},
    screen=dial(scr(menu(3, "Replace", timeline(2.4, lanes=[LAYER1, LAYER2], new=[2.1], erase=[2.1, 2.4])),
                    key="Loop 3", right="Rep 3.2", right_col="red",
                    note="replacing: the held span widens with the playhead; each mark it reaches is struck."),
                pct=2.4 / 4, rec="od"),
))

# 10 ----------------------------------------------------------------- Advance: the jump
S.append(state(
    "10 · Advance: REC tapped on slot 3 jumps to slot 4",
    "Advance: REC records into the NEXT slot (10 wraps to 1). Slot 3 is saved by the slot rule (leaving a slot saves "
    "it) and the selection jumps: the strip's hop arrow from 3 to 4, 4 red and tall, 'Loop 4' in the top line, armed "
    "('Rec · Advance', the REC dot). Then as Overwrite: the first chord starts the take (Free), the take replaces "
    "slot 4, and the new slot stays selected and plays. Slot 4 already holds a loop here: as the doc stands, Advance "
    "overwrites it when the take starts (to decide: skip to the next empty slot instead?).",
    buttons={"PLAY": BLINK, "REC": LIT}, button_notes=MENU_NOTE | {"REC": "lit: armed into slot 4"},
    encoders=enc(4, "Advance", "Free"),
    screen=dial(scr(menu(3, "Advance", length("Free", "slot 3 saved · next: slot 4"), jump=4), key="Loop 4",
                    right="Rec · Advance", right_col="red",
                    note="the red cell hops from 3 to 4 along the arrow; the name in the top line flips to Loop 4."),
                pct=0, rec="armed"),
))

# 11, 12 ------------------------------------------------------------- Step
STEP_FOOT = "OCT-: back · OCT+: rest · REC: done"
STEP_NOTES = {"PLAY": LAYER_BTN_NOTE, "REC": "lit: step entry (the dial's REC dot steady)",
              "OCT-": "back one step (the steps' chords stay)", "OCT+": "a rest: the cursor advances"}
S.append(state(
    "11 · Step: entering Dm7 at step 7 (LOCK held)",
    "Step: REC enters step entry (the loop stops): the middle is the STEP GRID, one row per bar (four at most; more "
    "page), sixteen steps a bar (the Quantize grid; none = 1/16), the steps holding a chord filled (1-6), the cursor a "
    "blinking red outline on step 7; the cursor step's chord under it ('Dm7') and 'step 7 · bar 1' at the right. A "
    "chord gesture writes the chord at the cursor (one step long); LOCK (B3) held keeps the keys latched so a chord is "
    "built from several presses (here MIN, m7 and the root D4); when every key is released the cursor advances. "
    "OCT+ = a rest, OCT- = back one step. REC leaves step entry, LOOP leaves it and plays.",
    held=["MIN", "m7"], lit=["D4", "F4", "A4", "C5"], lock=True,
    buttons={"PLAY": BLINK, "REC": LIT, "OCT-": LIT, "OCT+": LIT}, button_notes=STEP_NOTES,
    encoders=enc(3, "Step", "4 bars", select=("STEP BARS", "4")),
    key_notes={LOCK_KEY: "LOCK held: the keys stay latched until all are released (a chord from several presses)",
               key_of("D4"): "the root: MIN + m7 + D4 = Dm7, written at the cursor"},
    screen=dial(scr(menu(3, "Step", steps(range(1, 7), 7, "Dm7", "step 7 · bar 1"), foot=STEP_FOOT), key="Loop 3",
                    right="Rec · Step", right_col="red",
                    note="step entry: the cursor blinks on step 7, the chord's name changes as the keys go down."),
                pct=0, rec="armed"),
))
S.append(state(
    "12 · Step: every key released, the cursor on step 8",
    "Every key released: Dm7 is written into step 7 (filled) and the cursor moves on to step 8 (empty: '–'). The next "
    "chord goes there; OCT+ leaves it a rest. Free: the length grows as the steps pass the last bar (REC rounds it up "
    "to the last bar used); synced (4 bars here): the cursor wraps.",
    buttons={"PLAY": BLINK, "REC": LIT, "OCT-": LIT, "OCT+": LIT}, button_notes=STEP_NOTES,
    encoders=enc(3, "Step", "4 bars", select=("STEP BARS", "4")),
    screen=dial(scr(menu(3, "Step", steps(range(1, 8), 8, "–", "step 8 · bar 1"), foot=STEP_FOOT), key="Loop 3",
                    right="Rec · Step", right_col="red",
                    note="released: step 7 fills, the cursor slides to step 8."),
                pct=0, rec="armed"),
))

for _s in S:                                   # the battery shows on the Options page only
    _h = _s["screen"].get("header")
    if _h and _h.get("batt") == 255:
        _h["batt"] = False

assert len(S) <= 24, "the designer loads at most 24 states (index.html MAX_STATES)"

design = {
    "format": "fm1-panel-design", "version": 1, "device": "M-VAVE FM-1",
    "name": "ChoralRoot FM-1 looper mockups",
    "palette": "MOD",
    "labels": {"buttons": BUTTON_LABELS, "encoders": ENCODER_LABELS},
    "notes": "The looper's PLAY menu and record modes (docs/LOOPER-MODES.md, proposal 2026-10-09). LOOP held: one red screen "
             "under the top line: the slot strip (PRESETS), the record mode (ALGORITHM: Overwrite, Advance, Overdub, Replace, "
             "Step), the middle (SELECT: the length; the record timeline while recording or playing; the step grid in Step), "
             "the knob row (Quantize, Count-in, Level, Keys). GLO in the menu toggles Keys: Play (the keys play, the menu can "
             "stay open while you record) or Loops (the slot shortcuts). States: 1-4 the menu (Keys Play / Loops, the mode, "
             "an empty slot), 5-6 Overwrite armed and recording Free, 7 playing, 8 overdubbing, 9 replacing, 10 Advance's "
             "jump, 11-12 step entry.",
    "states": S,
}

if __name__ == "__main__":
    with open(OUT, "w") as f:
        json.dump(design, f, indent=1)
    print("wrote", OUT, "with", len(S), "states")
