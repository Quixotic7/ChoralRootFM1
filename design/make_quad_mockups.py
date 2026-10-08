#!/usr/bin/env python3
"""Generate the QUAD (Digitone-style 4-op FM) engine mock-ups (fm1-panel-design JSON): its editor screens.

  python3 design/make_quad_mockups.py       # writes design/choralroot-fm1-quad-mockups.json

docs/QUAD.md is the plan; the page layout is the user's (2026-10-07): ratios as plain numbers (B as a fraction B1 over
B2), Algo a big number, glyphs for Harm and Dtune; the operator envelopes A and B on one screen under a two-envelope
band with level bars; the filter's two screens (multimode with its envelope; the base-width filter); the amp
envelope; two LFOs. The designer draws them with: `algo` and `ade2` wide bands, the `filter` band's `bw` window,
the `harm`, `detune`, `ratio` glyphs and `big` cells.
"""
import json
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from make_mockups import (state, scr, cell, editor_screen, EDITOR_BUTTON_LABELS, BUTTON_LABELS,  # noqa: E402
                          ENCODER_LABELS, IDLE_ENC, LIT, BLINK, C_CHORD)
from make_editor_mockups import edit8, filt_wide  # noqa: E402

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "choralroot-fm1-quad-mockups.json")


def g(label, value, glyph=None, pct=None, pct2=None, **kw):
    c = cell(label, value, glyph, pct, **kw)
    if pct2 is not None:
        c["pct2"] = round(pct2, 3)
    return c


def big(label, value):
    return g(label, value, big=True)


ED = {"EDIT": BLINK}
S = []

# 1 ---------------------------------------------------------------- OSC 1: SYN 1
S.append(state(
    "1 · QUAD · OSC 1: the SYN1 page under the algorithm diagram",
    "The Digitone's SYN1 page, one for one: row A Algo (a big number) · Ratio C · Ratio A · Ratio B (one knob over the "
    "pair, drawn as the fraction B1 over B2; it cycles B1 through its steps, then increments B2), row B Harm (its wave "
    "glyph) · Dtune (two beating waves) · Feedback (a bar) · Mix (bipolar, X to Y). Ratios are numbers, no bars: C and "
    "B step 0.25 below 1 and 1 above, A in 0.25 steps. The band draws the algorithm and redraws as Algo turns. These "
    "eight are the EDIT macros.",
    held=["MAJ"], lit=["D4", "F#4", "A4"], buttons=ED | {"FX": LIT}, button_labels=EDITOR_BUTTON_LABELS,
    encoders={"SELECT": ("SYN 1", "A"), "KNOB1": ("ALGO", "3"), "KNOB2": ("RATIO C", "1.00"), "KNOB3": ("RATIO A", "2.00"), "KNOB4": ("RATIO B", "0.50/1.00")},
    screen=editor_screen(edit8("GLASS EP", "SYN 1 · A", [
        [big("Algo", "3"), big("Ratio C", "1.00"), big("Ratio A", "2.00"), g("Ratio B", "0.50/1.00", "ratio")],
        [g("Harm", "+8", "harm", 0.5 + 8 / 52), g("Dtune", "12", "detune", 12 / 127), g("Feedback", "40", "bar", 40 / 127), g("Mix", "+12", bipolar=True, pct=0.5 + 12 / 126)]], 0,
        wide={"type": "algo", "algo": 3, "fdbk": 0.3, "mix": 0.6}),
        note="SYN1: the diagram; Algo and the ratios as numbers, B a fraction; harm and detune glyphs."),
))

# 2 ---------------------------------------------------------------- OSC 2: the ratio offsets
S.append(state(
    "2 · QUAD · OSC 2: the ratio offsets of C, A, B1, B2",
    "OSC's second screen is one row: the fine ratio offsets of the four operators (-1.00..+1.00), numbers.",
    held=["MAJ"], lit=["D4", "F#4", "A4"], buttons=ED | {"FX": LIT}, button_labels=EDITOR_BUTTON_LABELS,
    encoders={"SELECT": ("SYN 2", ""), "KNOB1": ("OFFSET C", "+0.00"), "KNOB2": ("OFFSET A", "+0.00"), "KNOB3": ("OFFSET B1", "+0.00"), "KNOB4": ("OFFSET B2", "+0.01")},
    screen=editor_screen(edit8("GLASS EP", "SYN 2", [[g("Offset C", "+0.00"), g("Offset A", "+0.00"), g("Offset B1", "+0.00"), g("Offset B2", "+0.01")]], 0),
                         note="the ratio offsets, one row of numbers."),
))

# 3 ---------------------------------------------------------------- ENV 1: the operator envelopes A and B
S.append(state(
    "3 · QUAD · ENV 1: the operator envelopes A and B",
    "The operators' envelopes on one screen: row A is envelope A (Attack · Decay · End · Level), row B envelope B. The "
    "band draws both: A's curve at the left, B's at the right, each a rise to its Level and a fall to its End, then "
    "the hold, with a level bar at its side. The segment being turned is thick in its knob's colour.",
    held=["MAJ"], lit=["D4", "F#4", "A4"], buttons=ED | {"ENV": LIT}, button_labels=EDITOR_BUTTON_LABELS,
    encoders={"SELECT": ("ENV", "A"), "KNOB1": ("A ATTACK", "12"), "KNOB2": ("A DECAY", "60"), "KNOB3": ("A END", "20"), "KNOB4": ("A LEVEL", "90")},
    screen=editor_screen(edit8("GLASS EP", "ENV A/B", [
        [g("A Attack", "12", pct=0.1), g("A Decay", "60", pct=0.47), g("A End", "20", pct=0.16), g("A Level", "90", pct=0.71)],
        [g("B Attack", "0", pct=0.0), g("B Decay", "80", pct=0.63), g("B End", "0", pct=0.0), g("B Level", "70", pct=0.55)]], 0,
        wide={"type": "ade2", "a": {"a": 0.1, "d": 0.47, "end": 0.16, "lev": 0.71}, "b": {"a": 0.0, "d": 0.63, "end": 0.0, "lev": 0.55}, "seg": 1}),
        note="both operator envelopes with their level bars; A's decay thick (KNOB 2)."),
))

# 4 ---------------------------------------------------------------- ENV 2: delays, trigs, resets
S.append(state(
    "4 · QUAD · ENV 2: delays, trig modes, resets",
    "Row A: A Delay · A Trig · A Reset · Phase reset; row B: B Delay · B Trig · B Reset. Trig is the envelope's trigger "
    "mode (every note / legato holds), Reset whether it restarts from zero, Phase reset whether the operators restart "
    "their phase at note-on.",
    held=["MAJ"], lit=["D4", "F#4", "A4"], buttons=ED | {"ENV": LIT}, button_labels=EDITOR_BUTTON_LABELS,
    encoders={"SELECT": ("ENV", "2"), "KNOB1": ("A DELAY", "0"), "KNOB2": ("A TRIG", "ON"), "KNOB3": ("A RESET", "ON"), "KNOB4": ("PHASE", "RESET")},
    screen=editor_screen(edit8("GLASS EP", "ENV 2", [
        [g("A Delay", "0", pct=0.0), g("A Trig", "On"), g("A Reset", "On"), g("Phase", "Reset")],
        [g("B Delay", "0", pct=0.0), g("B Trig", "On"), g("B Reset", "Off"), None]], 0),
        note="the envelopes' delays, trig modes and resets; phase reset."),
))

# 5 ---------------------------------------------------------------- FILT 1: the multimode filter and its envelope
S.append(state(
    "5 · QUAD · FILT 1: the multimode filter and its envelope",
    "The filter page: row A the filter envelope (Attack · Decay · Sustain · Release), row B the filter (Freq · Reso · "
    "Type LP / HP / BP · Env depth), under the response band the VA's filter screen uses. Row B is on the knobs here.",
    held=["MAJ"], lit=["D4", "F#4", "A4"], buttons=ED | {"SEL": LIT}, button_labels=EDITOR_BUTTON_LABELS,
    encoders={"SELECT": ("FILTER", "B"), "KNOB1": ("FREQ", "2.1k"), "KNOB2": ("RESO", "30"), "KNOB3": ("TYPE", "LP"), "KNOB4": ("ENV DEPTH", "+24")},
    screen=editor_screen(edit8("GLASS EP", "FILTER · B", [
        [g("Attack", "10", pct=0.08), g("Decay", "50", pct=0.4), g("Sustain", "60", pct=0.47), g("Release", "40", pct=0.31)],
        [g("Freq", "2.1k", pct=0.6), g("Reso", "30", pct=0.24), g("Type", "LP"), g("Env depth", "+24", bipolar=True, pct=0.5 + 24 / 126)]], 1,
        wide=filt_wide(cut_hz=2100, res=0.24, ftype="LP")),
        note="the multimode filter with its own envelope; the response band."),
))

# 6 ---------------------------------------------------------------- FILT 2: env delay, key track, the base-width filter
S.append(state(
    "6 · QUAD · FILT 2: envelope delay, key track, the base-width filter",
    "The filter's second screen: row A Env delay · Key track; row B the base-width filter: Base (its low edge) and "
    "Width (how far above it the window reaches), drawn on the band as a lit window between two dashed edges over the "
    "multimode response.",
    held=["MAJ"], lit=["D4", "F#4", "A4"], buttons=ED | {"SEL": LIT}, button_labels=EDITOR_BUTTON_LABELS,
    encoders={"SELECT": ("FILTER 2", "B"), "KNOB1": ("BASE", "20"), "KNOB2": ("WIDTH", "90"), "KNOB3": "", "KNOB4": ""},
    screen=editor_screen(edit8("GLASS EP", "FILTER 2 · B", [
        [g("Env delay", "0", pct=0.0), g("Key track", "50", pct=0.39), None, None],
        [g("Base", "20", pct=0.16), g("Width", "90", pct=0.71), None, None]], 1,
        wide=filt_wide(cut_hz=2100, res=0.24, ftype="LP") | {"bw": {"base": 0.16, "width": 0.71}}),
        note="the base-width window over the response."),
))

# 7 ---------------------------------------------------------------- AMP: the master envelope
S.append(state(
    "7 · QUAD · AMP: the master envelope",
    "The amp page: Attack · Decay · Sustain · Release under the AHDSR band (QUAD's own amp envelope, as the Digitone's "
    "AMP page), row B Level · Pan · Drive (the platform's DIST).",
    held=["MAJ"], lit=["D4", "F#4", "A4"], buttons=ED | {"ENV": LIT}, button_labels=EDITOR_BUTTON_LABELS,
    encoders={"SELECT": ("AMP", "A"), "KNOB1": ("ATTACK", "5"), "KNOB2": ("DECAY", "40"), "KNOB3": ("SUSTAIN", "80"), "KNOB4": ("RELEASE", "35")},
    screen=editor_screen(edit8("GLASS EP", "AMP · A", [
        [g("Attack", "5", pct=0.04), g("Decay", "40", pct=0.31), g("Sustain", "80", pct=0.63), g("Release", "35", pct=0.28)],
        [g("Level", "100", pct=0.79), g("Pan", "0", bipolar=True, pct=0.5), g("Drive", "0", pct=0.0), None]], 0,
        wide={"type": "env", "a": 0.04, "h": 0.0, "d": 0.31, "s": 0.63, "r": 0.28, "seg": 0}),
        note="the amp envelope under the AHDSR band."),
))

# 8 ---------------------------------------------------------------- LFO 1
S.append(state(
    "8 · QUAD · LFO 1 (and LFO 2 the same)",
    "Two LFOs, a screen each: Speed · Mult · Fade · Dest (an enum over QUAD's parameters: Harm, Dtune, Feedback, Mix, "
    "the ratios, the filter's Freq and Reso, Level, Pan) / Wave · Phase · Mode (free / trig / hold) · Depth. The lfo "
    "glyph: density = speed, height = depth.",
    held=["MAJ"], lit=["D4", "F#4", "A4"], buttons=ED | {"LFO": LIT}, button_labels=EDITOR_BUTTON_LABELS,
    encoders={"SELECT": ("LFO", "1"), "KNOB1": ("SPEED", "24"), "KNOB2": ("MULT", "x2"), "KNOB3": ("FADE", "0"), "KNOB4": ("DEST", "HARM")},
    screen=editor_screen(edit8("GLASS EP", "LFO 1 · A", [
        [g("Speed", "24", "lfo", 0.3, 0.6), g("Mult", "x2"), g("Fade", "0", pct=0.0), g("Dest", "Harm")],
        [g("Wave", "Tri", "wave"), g("Phase", "0", pct=0.0), g("Mode", "Free"), g("Depth", "+30", bipolar=True, pct=0.5 + 30 / 126)]], 0),
        note="LFO 1: speed with the lfo glyph, the destination as an enum."),
))

# 9 ---------------------------------------------------------------- PRESETS
S.append(state(
    "9 · PRESETS on QUAD: the pool",
    "QUAD in the preset model: PRESETS turns inside its pool (00 INIT, the factory presets, yours); OPT + PRESETS "
    "reaches QUAD between FM6 and VA.",
    held=["MAJ"], lit=["D4", "F#4", "A4"], encoders=IDLE_ENC | {"PRESETS": ("SOUND", "03")},
    screen=scr({"kind": "meter", "value": "03", "sub": "GLASS EP", "label": "QUAD · 03/17", "col": C_CHORD, "pct": 3 / 17, "segments": 17},
               note="PRESETS on QUAD."),
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
    "notes": "QUAD, a Digitone-style four-operator FM engine (docs/QUAD.md), the user's page layout: SYN1 under the algorithm "
             "diagram with numbers for the ratios (B a fraction) and glyphs for Harm and Dtune (1), the ratio offsets (2), the "
             "operator envelopes A and B under a two-envelope band (3), their delays / trigs / resets (4), the multimode filter "
             "with its envelope (5), the base-width filter (6), the amp envelope (7), the LFOs (8), the preset pool (9).",
    "states": S,
}

if __name__ == "__main__":
    with open(OUT, "w") as f:
        json.dump(design, f, indent=1)
    print("wrote", OUT, "with", len(S), "states")
