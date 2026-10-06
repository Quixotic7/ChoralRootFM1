#!/usr/bin/env python3
"""Generate the ChoralRoot FM-1 sound editor mock-ups as an fm1-panel-design JSON.

  python3 design/make_editor_mockups.py     # writes design/choralroot-fm1-sound-editor-mockups.json

The companion of make_mockups.py (the main walk-through; its state 21 is this file's state 1): the same format, the
same `labels` sticker, the MOD palette, and its helpers (state, cell, the stack panel), imported from it. The VA engine
is mocked (docs/VA.md: oscillators, filter, envelopes, LFOs, matrix) plus the platform's FX sends and MIX. The engine
picker and the save dialog are the main walk-through's states 22 and 23.

The dense editor (second pass, 2026-10-06, after the user's review of the first):
- No header bar and no footer hints: the panel takes the whole screen. Its top line is the sound's name ('*' when
  edited, ' · BASS' for the bass part, in orange) and, at the right, the section and what is on the knobs.
- A section is ONE view. `edit8`: up to eight parameters in two rows of four, an optional wide shape over them (the
  envelope, the filter curve); the active row is on KNOB 1-4 (drawn in the knob colours with bars), the other grey.
  `stack`: the section's instances as rows (the four oscillators, the four LFOs, the eight matrix slots); the active row
  is on KNOB 1-4.
- EDIT tap enters (EDIT blinks while in the editor); EDIT tap or HOME leaves. Root keys still play (audition),
  OCT-/OCT+ still shift the octave, PERF works as outside. SHIFT + EDIT = chord sound <-> bass sound.
- Section buttons (printed -> editor): FX = OSC, SEL = FILT, ENV = ENV, LFO = LFO, SEQ = MOD, PLAY = FX, REC = MIX,
  GLO = SHIFT. A tap steps the active row (the next oscillator / envelope / LFO / slot; FILT and MIX swap row A <-> B);
  a hold is the A/B bank rule: it swaps the view to its B bank (OSC: shape / key track / sync-ring; ENV: hold /
  velocity; LFO: sync), hold again = back to A. SELECT turns what the tap steps.
- Page memory: every section remembers its active row and bank; the editor remembers the section per part.
"""
import json
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import make_mockups as mm  # noqa: E402  (builds the main walk-through in memory; writes nothing on import)
from make_mockups import (state, cell, wave_cell, editor_screen, stack_panel, osc_row_a, osc_stack,  # noqa: E402
                          WARM_OSC, OSC_COLS_B, EDITOR_BUTTON_LABELS, BUTTON_LABELS, ENCODER_LABELS, LIT, BLINK,
                          C_BASS)

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "choralroot-fm1-sound-editor-mockups.json")

# printed button -> the section it opens in the editor
SEC_BTN = {"OSC": "FX", "FILT": "SEL", "ENV": "ENV", "LFO": "LFO", "MOD": "SEQ", "FX": "PLAY", "MIX": "REC"}


# value -> 0..1 fills on the firmware's curves (docs/VA.md)
def t_pct(ms):            # F_TIME: 1 ms .. 10 s, exponential
    return max(0.0, min(1.0, math.log10(max(ms, 1)) / 4))


def cut_pct(hz):          # F_CUTOFF: 30 Hz .. 16 kHz
    return math.log(hz / 30) / math.log(16000 / 30)


def lfo_pct(hz):          # F_LFOHZ: 0.05 .. 40 Hz
    return math.log(hz / 0.05) / math.log(800)


def bip(v, lo=-64, hi=63):
    return (v - lo) / (hi - lo)


def ms(v):
    return f"{v / 1000:.2f} s" if v >= 1000 else f"{v} ms"


def r3(x):
    return round(x, 3)


AUDITION = dict(held=["MAJ"], lit=["D4", "F#4", "A4"])      # a held D major: the root keys still play
AUDITION_NOTE = {mm.key_of("D4"): "root keys audition the sound (D major held)"}

EDITOR_BUTTON_NOTES = {
    "FX": "OSC: tap = the next oscillator on the knobs (1..4, wraps) · hold = bank B (shape, key track, sync / ring), hold again = bank A",
    "SEL": "FILT: tap = swap the knob row (A: type cutoff reso drive · B: key track, env amount)",
    "ENV": "ENV: tap = the next envelope (1 amp, 2 filter, 3-4 free) · hold = row B (hold; velocity on ENV 1), hold again = row A",
    "LFO": "LFO: tap = the next LFO on the knobs (1..4) · hold = bank B (sync, rate, wave, depth), hold again = bank A",
    "EDIT": "EDIT: blinks while the editor is open · tap = leave · hold = the engine picker · SHIFT + EDIT = chord / bass sound",
    "GLO": "SHIFT (OPT): held + a knob = fine steps (one unit) · SHIFT + EDIT = switch chord / bass sound",
    "HOME": "HOME: leave the editor (back to the view); every section's row and bank are remembered",
    "SAVE": "SAVE: the save dialog (slot + name), as outside the editor",
    "ARP": "PERF: as outside the editor (tap on / off, hold = the perform layer)",
    "SEQ": "MOD: tap = the next matrix slot on the knobs (1..8, wraps); SELECT the same",
    "PLAY": "FX: the sends (Drive, Chorus, Delay, Reverb)",
    "REC": "MIX: tap = swap the knob row (A: level pan voice glide · B: transpose detune priority glide mode)",
    "OCT-": "OCT- / OCT+: the root keys' octave, as outside (audition); both: panic",
}


def edit8(title, right, rows, active, hot=None, wide=None, title_col=None):
    p = {"kind": "edit8", "title": title, "right": right, "wide": wide, "rows": rows, "active": active, "hot": hot}
    if title_col:
        p["titleCol"] = title_col
    return p


def ed(name_, notes, sec, panel, knobs, select, part="chord", buttons=None, button_notes=None, key_notes=None,
       audition=None, screen_note=""):
    """an editor state: EDIT blinking, the section's button lit, the editor's button labels, the active row's four
    cells on KNOB 1-4 (their encoder notes = the parameter names), SELECT = what the section's tap steps"""
    enc = {"MASTER": ("VOLUME", ""), "SELECT": select}
    for i, c in enumerate(knobs):
        enc[f"KNOB{i + 1}"] = (c["label"].upper(), c["value"]) if c else ("", "")
    b = {"EDIT": BLINK}
    if sec:
        b[SEC_BTN[sec]] = LIT
    b.update(buttons or {})
    return state(
        name_, notes, **(audition if audition is not None else AUDITION), buttons=b,
        button_labels=EDITOR_BUTTON_LABELS, button_notes=button_notes, key_notes=key_notes,
        encoders=enc, screen=editor_screen(panel, screen_note),
    )


def knobs_of(panel):
    """the four cells on KNOB 1-4: the active row of an edit8 / stack (for a stack's B bank, its own cells)"""
    a = panel["active"]
    if panel["kind"] == "edit8":
        return panel["rows"][a]
    return [dict(c, label=panel["cols"][i] or c["label"]) if c else None for i, c in enumerate(panel["rows"][a]["cells"])]


# ------------------------------------------------------------------------------------------------ the data
T = "WARM PAD*"

# OSC bank B: Shape, Key trk, Sync (OSC 2) / Ring (OSC 4), the fourth blank; OSC 1 and 3 have no sync / ring
WARM_OSC_B = [
    [cell("Shape", "55%", "bar", 0.55), cell("Key trk", "on"), cell("Sync/Ring", "–"), None],
    [cell("Shape", "40%", "bar", 0.40), cell("Key trk", "on"), cell("Sync/Ring", "on"), None],
    [cell("Shape", "0%", "bar", 0.0), cell("Key trk", "on"), cell("Sync/Ring", "–"), None],
    [cell("Shape", "0%", "bar", 0.0), cell("Key trk", "on"), cell("Sync/Ring", "off"), None],
]


def osc_b(title, rows, active, hot=None, title_col=None):
    return stack_panel(title, f"OSC {active + 1} · B", OSC_COLS_B, [(str(i + 1), r) for i, r in enumerate(rows)],
                       active, hot, title_col)


def filt_wide(cut_hz=643, res=0.08, ftype="LP", drive=0.0):
    return {"type": "filter", "cut": r3(cut_pct(cut_hz)), "res": res, "ftype": ftype, "drive": drive}


FILT_ROWS = [
    [cell("Type", "LP", "dots", 0.0), cell("Cutoff", "643 Hz", "bar", cut_pct(643)), cell("Reso", "8%", "bar", 0.08),
     cell("Drive", "0%", "bar", 0.0)],
    [cell("Key trk", "50%", "bar", 0.5), cell("Env amt", "+32", "bar", bip(32), bipolar=True), None, None],
]

ROLE = {1: "amp", 2: "filter", 3: "free", 4: "free"}


def env_view(n, a, h, d, s_pct, r, vel=None, active=0, hot=None):
    """one envelope: the wide AHDSR over row A (Attack Decay Sustain Release) and row B (Hold, Velocity on ENV 1);
    seg = the hot cell's segment (0 A, 1 H, 2 D, 3 S, 4 R), null when no cell is hot"""
    rows = [
        [cell("Attack", ms(a), "bar", t_pct(a)), cell("Decay", ms(d), "bar", t_pct(d)),
         cell("Sustain", f"{s_pct}%", "bar", s_pct / 100), cell("Release", ms(r), "bar", t_pct(r))],
        [cell("Hold", ms(h) if h else "0 ms", "bar", t_pct(h) if h else 0.0),
         cell("Velocity", f"{vel}%", "bar", vel / 100) if vel is not None else None, None, None],
    ]
    seg = None
    if hot:
        seg = [[0, 2, 3, 4], [1, None, None, None]][hot[0]][hot[1]]
    wide = {"type": "env", "a": r3(t_pct(a)), "h": r3(t_pct(h) if h else 0.0), "d": r3(t_pct(d)), "s": r3(s_pct / 100),
            "r": r3(t_pct(r)), "seg": seg}
    return edit8(T, f"ENV {n} · {ROLE[n]}", rows, active, hot, wide)


LFO_COLS_A = ["Rate", "Wave", "Depth", "Fade"]
LFO_COLS_B = ["Sync", "", "", ""]
# (rate text, rate pct, wave, depth %, fade ms, sync)
WARM_LFO = [("0.8 Hz", lfo_pct(0.8), "TRI", 35, 1200, False), ("1/8", 9 / 13, "SIN", 20, 0, True),
            ("0.2 Hz", lfo_pct(0.2), "SAW", 0, 0, False), ("3.0 Hz", lfo_pct(3.0), "S&H", 0, 0, False)]


def lfo_stack(active, bank="A", hot=None):
    rows = []
    for i, (rate, rp, wave, depth, fade, sync) in enumerate(WARM_LFO):
        rc = cell("Rate", rate, "knob", rp)
        wc = wave_cell(wave)
        dc = cell("Depth", f"{depth}%", "bar", depth / 100)
        if bank == "A":
            cells = [rc, wc, dc, cell("Fade", ms(fade) if fade else "0 ms", "bar", t_pct(fade) if fade else 0.0)]
        else:
            cells = [cell("Sync", "on" if sync else "off"), None, None, None]
        rows.append((str(i + 1), cells))
    return stack_panel(T, f"LFO {active + 1} · {bank}", LFO_COLS_A if bank == "A" else LFO_COLS_B, rows, active, hot)


MOD_COLS = ["Source", "Dest", "Amount", ""]
MOD_SLOTS = [("LFO1", "CUT", 24), ("LFO2", "PAN", 20), ("ENV3", "PIT2", -12)] + [None] * 5


def mod_stack(active, hot=None):
    rows = []
    for i, m in enumerate(MOD_SLOTS):
        if m:
            src, dst, amt = m
            cells = [cell("Source", src), cell("Dest", dst),
                     cell("Amount", f"{amt:+d}", pct=bip(amt), bipolar=True), None]
        else:
            cells = [cell("Source", "–"), None, None, None]          # an unused slot: nothing but the dash
        rows.append((str(i + 1), cells))
    return stack_panel(T, f"MOD {active + 1}", MOD_COLS, rows, active, hot)


FX_ROWS = [[cell("Drive", "0", "bar", 0.0), cell("Chorus", "40", "bar", 40 / 127), cell("Delay", "20", "bar", 20 / 127),
            cell("Reverb", "55", "bar", 55 / 127)],
           [None, None, None, None]]

MIX_ROWS = [[cell("Level", "92", "bar", 92 / 127), cell("Pan", "C", "bar", 0.5, bipolar=True),
             cell("Voice", "POLY"), cell("Glide", "0", "bar", 0.0)],
            [cell("Trans", "0", "bar", 0.5, bipolar=True), cell("Detune", "12", "bar", 12 / 127),
             cell("Priority", "LAST"), cell("Gl. mode", "OFF")]]

S = []


def add(name_, notes, sec, panel, select, **kw):
    S.append(ed(name_, notes, sec, panel, knobs_of(panel), select, **kw))


# 1 ------------------------------------------------------------------------------- OSC bank A, OSC 1
add("1 · EDIT tapped: the editor on WARM PAD, the four oscillators (OSC 1 on the knobs)",
    "EDIT tap opens the sound editor on the chord sound (WARM PAD, VA); EDIT blinks while it is open, EDIT or HOME "
    "leaves. No header bar and no footer: the top line is the sound's name and, at the right, the section and bank. OSC "
    "is one view: the four oscillators as rows (Wave Level Coarse Fine); the active row, OSC 1, is on KNOB 1-4 in the "
    "knob colours with the active bar beside it, the other rows grey. OSC tap or SELECT = the next oscillator on the "
    "knobs; OSC hold = bank B (shape, key track, sync / ring). The root keys still play (audition), OCT- / OCT+ shift.",
    "OSC", osc_stack("WARM PAD", WARM_OSC, 0), ("OSC", "1"),
    button_notes=EDITOR_BUTTON_NOTES, key_notes=AUDITION_NOTE,
    screen_note="the editor opens: the four rows drop in one after another, then the active bar slides onto row 1 and "
                "its cells take the knob colours.")

# 2 ------------------------------------------------------------------------------- OSC 2 active, KNOB 3 turning
add("2 · OSC tapped: OSC 2 on the knobs, KNOB 3 (Coarse) turning",
    "OSC tapped (or SELECT turned one detent): OSC 2 becomes the active row, its four cells on KNOB 1-4. KNOB 3 turns "
    "its Coarse (-12, an octave down); the cell being turned is hot. The title gains its '*': the sound is edited.",
    "OSC", osc_stack(T, WARM_OSC, 1, hot=[1, 2]), ("OSC", "2"),
    button_notes={"FX": "OSC tapped: the next oscillator"},
    screen_note="the active bar slides down from row 1 to row 2; row 1 fades to grey as row 2 takes the knob colours; "
                "the Coarse cell flashes hot while KNOB 3 turns.")

# 3 ------------------------------------------------------------------------------- OSC bank B
add("3 · OSC held: bank B (shape, key track, sync / ring), OSC 2 active",
    "OSC held: the oscillators' B bank, the same four rows with Shape / Key trk / Sync-Ring (the fourth column blank). "
    "OSC 2 stays the active row (its sync to OSC 1 is on); OSC 1 and 3 have no sync / ring ('–'), OSC 4 has the ring "
    "(off). Hold OSC again = bank A; a tap here still steps the oscillator.",
    "OSC", osc_b(T, WARM_OSC_B, 1), ("OSC", "2"),
    button_notes={"FX": "OSC held: bank B (hold again: bank A)"},
    screen_note="A -> B: the column heads and cells slide sideways row by row while the active bar stays on row 2; "
                "the top right reads 'OSC 2 · B'.")

# 4 ------------------------------------------------------------------------------- FILTER row A
add("4 · FILT tapped: the filter, row A on the knobs, KNOB 2 (Cutoff) turning",
    "FILT (printed SEL): one view of up to eight parameters under the wide filter curve: row A Type Cutoff Reso Drive "
    "(on KNOB 1-4), row B Key trk and Env amt (grey). KNOB 2 turns the cutoff (643 Hz). OSC remembers bank B and OSC 2 "
    "for when it is pressed again.",
    "FILT", edit8(T, "FILTER", FILT_ROWS, 0, [0, 1], filt_wide()), ("ROW", "A"),
    button_notes={"SEL": "FILT: the filter (tap again: row B)"},
    screen_note="a section change: the view wipes in from the side of its button; the wide filter curve redraws as "
                "KNOB 2 turns (the knee slides with the cutoff, the peak grows with Reso).")

# 5 ------------------------------------------------------------------------------- FILTER row B
add("5 · FILT tapped again: row B on the knobs (key track, env amount)",
    "FILT tapped (or SELECT): the knobs move to row B, Key trk and Env amt (ENV 2's amount on the cutoff, bipolar); row "
    "A turns grey. KNOB 3 and 4 do nothing on this row.",
    "FILT", edit8(T, "FILTER", FILT_ROWS, 1, None, filt_wide()), ("ROW", "B"),
    screen_note="the rows swap with a slide: the active bar and the knob colours move from row A down to row B.")

# 6 ------------------------------------------------------------------------------- ENV 1 row A
add("6 · ENV tapped: ENV 1 (amp), KNOB 2 (Decay) turning",
    "ENV: one view per envelope, the wide AHDSR over row A Attack Decay Sustain Release (on the knobs) and row B Hold "
    "and Velocity (grey). KNOB 2 turns ENV 1's Decay: the decay segment of the wide envelope is lit.",
    "ENV", env_view(1, 1880, 0, 683, 91, 790, vel=60, active=0, hot=[0, 1]), ("ENV", "1"),
    screen_note="the wide envelope redraws as the knob turns; the segment being turned (decay) in the hot cell's "
                "colour, the rest in the line colour.")

# 7 ------------------------------------------------------------------------------- ENV 1 row B
add("7 · ENV held: ENV 1 row B (hold, velocity)",
    "ENV held: the knobs move to row B, Hold and Velocity (ENV 1's velocity to level); hold ENV again = row A. "
    "ENV 2-4 have Hold only.",
    "ENV", env_view(1, 1880, 0, 683, 91, 790, vel=60, active=1), ("ENV", "1"),
    button_notes={"ENV": "ENV held: row B (hold again: row A)"},
    screen_note="the rows swap with a slide; the wide envelope stays.")

# 8 ------------------------------------------------------------------------------- ENV 2 row A
add("8 · ENV tapped: ENV 2 (filter), row A",
    "ENV tapped: the next envelope, ENV 2 (the filter envelope: FILTER's Env amt), back on row A. A short pluck of an "
    "envelope; its row B has Hold only.",
    "ENV", env_view(2, 10, 0, 420, 20, 300, active=0), ("ENV", "2"),
    button_notes={"ENV": "ENV tapped: the next envelope"},
    screen_note="the next envelope: the wide shape morphs from ENV 1's slow swell to ENV 2's pluck; the top right "
                "reads 'ENV 2 · filter'.")

# 9 ------------------------------------------------------------------------------- LFO bank A
add("9 · LFO tapped: the four LFOs, LFO 1 on the knobs",
    "LFO: one view, the four LFOs as rows (Rate Wave Depth Fade); LFO 1 is on the knobs. LFO 2 is tempo-synced, so its "
    "Rate reads a division (1/8). LFO tap or SELECT = the next LFO; LFO hold = bank B (sync).",
    "LFO", lfo_stack(0), ("LFO", "1"),
    screen_note="each Wave glyph moves at its LFO's rate (LFO 2 on the beat).")

# 10 ------------------------------------------------------------------------------ LFO bank B
add("10 · LFO held: bank B (sync), LFO 2 active",
    "LFO held: bank B, Sync alone: KNOB 1 is the tempo sync on / off; with sync on, bank A's Rate reads as a "
    "division of the BPM (LFO 2 at 1/8). Hold LFO again = bank A.",
    "LFO", lfo_stack(1, "B"), ("LFO", "2"),
    button_notes={"LFO": "LFO held: bank B (hold again: bank A)"},
    screen_note="A -> B: the Sync column slides in from the left and Fade slides out; a synced LFO's wave glyph pulses "
                "with the beat.")

# 11 ------------------------------------------------------------------------------ MOD slot 1
add("11 · MOD tapped: the matrix, slot 1 on the knobs",
    "MOD (printed SEQ): the whole matrix in one view, eight rows Source Dest Amount (the amount a bipolar bar around "
    "zero). Three slots are used: LFO1 -> CUT +24, LFO2 -> PAN +20, ENV3 -> PIT2 -12; the rest '–'. Slot 1 is on the "
    "knobs. MOD tap or SELECT = the next slot.",
    "MOD", mod_stack(0), ("SLOT", "1"),
    screen_note="the eight rows text only; the active bar on slot 1.")

# 12 ------------------------------------------------------------------------------ MOD slot 3
add("12 · MOD tapped twice: slot 3 on the knobs, KNOB 3 (Amount) turning",
    "Two more MOD taps: slot 3, ENV3 -> PIT2 -12 (OSC 2's pitch dips with ENV 3). KNOB 3 turns its amount.",
    "MOD", mod_stack(2, hot=[2, 2]), ("SLOT", "3"),
    button_notes={"SEQ": "MOD tapped: the next slot"},
    screen_note="the active bar slides down two rows; the amount bar grows left of zero as KNOB 3 turns.")

# 13 ------------------------------------------------------------------------------ FX sends
add("13 · FX tapped: the sends",
    "FX (printed PLAY): the platform sends, Drive Chorus Delay Reverb, on row A (cr_pages.c FX). The platform exposes "
    "no per-effect parameters on its pages, so row B is empty.",
    "FX", edit8(T, "FX", FX_ROWS, 0), ("", ""),
    screen_note="no wide shape: the four bars fill the view.")

# 14 ------------------------------------------------------------------------------ MIX row A
add("14 · MIX tapped: row A (level, pan, voice, glide)",
    "MIX (printed REC): one view of the part's mix and tuning (the old TUNE page merged into it): row A Level Pan Voice "
    "Glide on the knobs, row B Transpose Detune Priority Glide mode (grey). MIX tap or SELECT swaps the rows.",
    "MIX", edit8(T, "MIX", MIX_ROWS, 0), ("ROW", "A"),
    screen_note="eight parameters at once, row A in the knob colours.")

# 15 ------------------------------------------------------------------------------ MIX row B
add("15 · MIX tapped again: row B (transpose, detune, priority, glide mode)",
    "MIX tapped: the knobs move to row B, Transpose Detune Priority Glide mode (cr_pages.c has the MIX row only: row B "
    "is a new platform page).",
    "MIX", edit8(T, "MIX", MIX_ROWS, 1), ("ROW", "B"),
    screen_note="the rows swap with a slide.")

# 16 ------------------------------------------------------------------------------ the bass sound
PUNCH_OSC = [osc_row_a("SQR", 79, "-12", "0"), osc_row_a("SAW", 60, "-12", "+5"), osc_row_a("SIN", 50, "-24", "0"),
             osc_row_a("NOIS", 0, "0", "0")]
add("16 · SHIFT + EDIT: the bass sound (PUNCH BASS), the oscillators",
    "SHIFT + EDIT switches the editor between the chord sound and the bass sound (BASS held + EDIT from outside opens "
    "the bass directly). The title reads 'PUNCH BASS · BASS' in orange; the bass remembers its own section, rows and "
    "banks (it opens where it was last left: OSC, OSC 1). The root keys audition the bass.",
    "OSC", osc_stack("PUNCH BASS · BASS", PUNCH_OSC, 0, title_col=C_BASS), ("OSC", "1"),
    buttons={"GLO": LIT},
    button_notes={"GLO": "SHIFT held + EDIT tapped: chord <-> bass", "EDIT": "blinks: the editor (now the bass sound)"},
    audition=dict(held=["MAJ"], lit=["D4"]), key_notes={mm.key_of("D4"): "the bass auditions its root"},
    screen_note="chord -> bass: the view slides down and comes back with the title in orange.")

assert len(S) <= 24, "the designer loads at most 24 states"

design = {
    "format": "fm1-panel-design", "version": 1, "device": "M-VAVE FM-1",
    "name": "ChoralRoot FM-1 sound editor",
    "palette": "MOD",
    "labels": {"buttons": BUTTON_LABELS, "encoders": ENCODER_LABELS},
    "notes": "The ChoralRoot sound editor, dense version (the main walk-through's state 21 opens it; its state 21 is "
             "state 1 here). Enter: EDIT tap (EDIT blinks while in the editor); exit: EDIT tap or HOME. Root keys still "
             "play (audition), OCT-/OCT+ shift the octave, PERF works as outside. The function buttons become sections "
             "(state buttonLabels): printed FX = OSC, SEL = FILT, ENV = ENV, LFO = LFO, GLO = SHIFT, HOME = HOME (exit), "
             "SAVE = SAVE, ARP = PERF, SEQ = MOD, PLAY = FX, REC = MIX: the top row reads as the signal flow OSC FILT ENV "
             "LFO, the bottom row MOD FX MIX as the output stage. "
             "THE DENSE GRAMMAR: no header bar and no footer hints; each section is ONE view. 'edit8' shows up to eight "
             "parameters in two rows of four (FILTER, each ENV, FX, MIX), with a wide shape over them where there is one "
             "(the filter curve, the AHDSR, which redraw as a knob turns); 'stack' shows the section's instances as rows "
             "(the 4 oscillators, the 4 LFOs, the 8 matrix slots). The active row is on KNOB 1-4, drawn in the knob "
             "colours with bars and an active bar beside it; the other row(s) grey. The top line: the sound's name ('*' "
             "when edited, ' · BASS' in orange for the bass part) and at the right the section and what is on the knobs "
             "('OSC 2 · A', 'ENV 2 · filter', 'MOD 3'). "
             "Taps step, holds swap: a section tap moves the active row (OSC / LFO / MOD: the next instance; ENV: the next "
             "envelope; FILT / MIX: row A <-> B); SELECT does the same as the tap. THE A/B BANK RULE: a section hold "
             "swaps its view to bank B and back (OSC B: Shape, Key trk, Sync/Ring; ENV: row B Hold, Velocity; LFO B: "
             "Sync alone; a synced Rate reads as a division). KNOB 1-4 edit (a detent = 5 % of the range, enums one by one; SHIFT held = one "
             "unit). SHIFT + EDIT switches between the chord sound and the bass sound. EDIT held = the engine picker and "
             "SAVE = the save dialog, as in the main walk-through (its states 22, 23). "
             "Motion: rows swap with a slide; the active bar slides between rows / instances; a bank swap slides the "
             "columns sideways; the wide envelope / filter redraws as its knob turns. "
             "Page memory: every section remembers its active row and bank; leaving the editor and coming back returns "
             "to the same section, row and bank, per part (chord and bass each have their own). "
             "VARIANT B (for the user to choose): HOME is the MIX view (tap = MIX, tap again = row B) and only EDIT leaves "
             "the editor; REC then stays REC (record while editing). Variant A (drawn here): HOME and EDIT both leave, "
             "REC = MIX.",
    "states": S,
}

if __name__ == "__main__":
    with open(OUT, "w") as f:
        json.dump(design, f, indent=1)
    print("wrote", OUT, "with", len(S), "states")
