#!/usr/bin/env python3
"""Generate the ChoralRoot FM-1 interface mock-ups as an fm1-panel-design JSON.

  python3 design/make_mockups.py            # writes design/choralroot-fm1-mockups.json

Open the file in the ChoralRootFM1Designer (../ChoralRootFM1Designer/index.html, or
`?design=examples/choralroot-fm1-mockups.json` on its dev server) to view, edit and export it.
The control mapping and every state here are the normative interface sketch of PLAN.md §3-§6;
when this file and PLAN.md disagree, fix one of them explicitly.

Layout (2026-10-05, second pass): the Orchid's 2x4 chord block sits on the LEFT OF THE KEYBED —
chord types on the black keys F#3 G#3 A#3 C#4 (DIM MIN MAJ SUS), extensions on the white keys
F3 G3 A3 C4 (6 m7 M7 9) — and the roots are the 18 keys from D4 to G5. The twelve function buttons
carry the Orchid dials' button-like functions.

Third pass (2026-10-06, the user's proposal, decided): B3 = LOCK (a mode toggle for the chord block:
on, released chord keys stay latched; a top-row key resets the type, a bottom-row key toggles its extension); printed SEL = KEY and printed EDIT = EDIT (swapped); a layer button held past 300 ms
opens its layer AND LOCKS it (OCT- or HOME closes it, its LED blinks while open); EDIT tap = the
sound editor, drawn in full by design/make_editor_mockups.py (choralroot-fm1-sound-editor-mockups.json),
which imports the helpers below (state, scr, the editor page, the engine picker and the save dialog).
The designer loads at most 24 states (its MAX_STATES), so the walk-through stays at 24.
"""
import json
import os

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "choralroot-fm1-mockups.json")

NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]
BTN_IDS = ["FX", "SEL", "ENV", "LFO", "EDIT", "GLO", "HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC", "OCT-", "OCT+"]
ENC_IDS = ["MASTER", "SELECT", "PRESETS", "ALGORITHM", "KNOB1", "KNOB2", "KNOB3", "KNOB4"]
OFF, DIM, LIT, BLINK = 0, 1, 2, 3


def key_black(k):
    return (0x54A >> ((k + 5) % 12)) & 1


def name(k):
    m = 53 + k
    return NOTE_NAMES[m % 12] + str(m // 12 - 1)


def key_of(n):
    """key index of a note name like 'C4' (F3..G5)"""
    for k in range(27):
        if name(k) == n:
            return k
    raise KeyError(n)


# ----------------------------------------------------------------- the control mapping (PLAN.md §3)
# the chord block on the keybed: Orchid's top row (chord types) on the black keys, its bottom row
# (extensions) on the white keys; B3, between the rows' ends, is LOCK (a mode toggle that latches the block)
CHORD_KEYS = {"F#3": "DIM", "G#3": "MIN", "A#3": "MAJ", "C#4": "SUS", "F3": "6", "G3": "m7", "A3": "M7", "C4": "9"}
CHORD_IDX = {key_of(n): lab for n, lab in CHORD_KEYS.items()}
LOCK_KEY = key_of("B3")
ROOT_LO = key_of("D4")                                   # roots: D4 .. G5 (18 keys)
ROOTS = list(range(ROOT_LO, 27))
ROOT_WHITE = [k for k in ROOTS if not key_black(k)]      # 11 white root keys: D4 E4 F4 G4 A4 B4 C5 D5 E5 F5 G5
ROOT_BLACK = [k for k in ROOTS if key_black(k)]          # 7 black root keys

BUTTON_LABELS = {
    "FX": "FX", "SEL": "KEY", "ENV": "BASS", "LFO": "LATCH", "EDIT": "EDIT", "GLO": "OPT",
    "HOME": "HOME", "SAVE": "SAVE", "ARP": "PERF", "SEQ": "METRO", "PLAY": "LOOP", "REC": "REC",
    "OCT-": "OCT-", "OCT+": "OCT+",
}
ENCODER_LABELS = {
    "MASTER": "VOLUME", "SELECT": "BPM", "PRESETS": "SOUND", "ALGORITHM": "BASS SND",
    "KNOB1": "VOICING", "KNOB2": "BASS VOICE", "KNOB3": "PERFORM", "KNOB4": "FX",
}
# the sound editor's button map (state buttonLabels inside the editor): the top row reads as the signal flow
# OSC FILT ENV LFO, the bottom row MOD FX MIX as the output stage; OPT is SHIFT (fine steps)
EDITOR_BUTTON_LABELS = {
    "FX": "OSC", "SEL": "FILT", "ENV": "ENV", "LFO": "LFO", "EDIT": "EDIT", "GLO": "SHIFT",
    "HOME": "HOME", "SAVE": "SAVE", "ARP": "PERF", "SEQ": "MOD", "PLAY": "FX", "REC": "MIX",
}


KEY_COLS = {"Key": "yellow", "Rec": "red", "Loop": "red"}


def header(key="", right="", icon="none", bpm="", rec=False, right_col=None, mid_col=None):
    """Orchid's top line: the key top-left (in the colour of what owns it), a status top-right, the battery"""
    h = {"icon": icon, "bpm": bpm, "mid": key, "right": right, "batt": 3, "usb": False}
    for word, col in KEY_COLS.items():
        if key.startswith(word):
            h["midCol"] = col
    if mid_col:
        h["midCol"] = mid_col
    if right_col:
        h["rightCol"] = right_col
    if rec:
        h["rec"] = True
    return h


def scr(panel, key="", right="", icon="none", bpm="", rec=False, ring=None, ring_rec=False, foot=None, message=None, message_col=None, note="", right_col=None):
    """one big thing per screen: a panel under Orchid's thin top line, an optional one-line footer"""
    o = {"header": header(key, right, icon, bpm, rec, right_col), "cards": None, "panel": panel,
         "footer": {"text": foot} if foot else None, "note": note}
    if ring is not None:
        o["ring"] = ring
        if ring_rec:
            o["ringRec"] = True
    if message:
        o["message"] = message
        if message_col:
            o["messageCol"] = message_col
    return o


def chord(root="", quality="", sup="", notes="", line="", hint="", **kw):
    p = {"kind": "chord", "root": root, "quality": quality, "sup": sup, "notes": notes, "line": line, "hint": hint}
    p.update(kw)
    return p


def big(value, label, sub="", pct=None, size=None):
    p = {"kind": "big", "value": value, "label": label, "sub": sub}
    if pct is not None:
        p["pct"] = pct
    if size:
        p["size"] = size
    return p


def biglist(items, sel, title=""):
    return {"kind": "list", "big": True, "title": title, "items": items, "sel": sel}


# colour roles (PLAN.md §5): what each knob and button owns on the screen
C_CHORD, C_VOICE, C_BASS, C_LOOP, C_KEY, C_FX, C_TEMPO = "white", "blue", "orange", "red", "yellow", "green", "white"


def bubbles(notes, ext=(), col=C_CHORD, extcol=C_BASS):
    """note bubbles in voiced order; the notes in `ext` (7ths, 9ths) take the extension colour"""
    return [{"t": n, "col": extcol if n in ext else col, "mark": n in ext} for n in notes]


def state(name_, notes="", keys=None, lit=(), blink=(), held=(), buttons=None, play_green=0,
          key_labels=None, key_notes=None, button_notes=None, encoders=None, screen=None, glow=True,
          chord_block=True, lock=False, button_labels=None):
    """glow: every LED dim unless set otherwise (Felucca's idle glow); lit / blink: root key names;
    held: chord-block labels (DIM, M7, ...) lit (held or latched); chord_block: print the block's labels (and
    LOCK on B3) on its keys; lock: B3 lit (LOCK mode on); button_labels: this state's overrides"""
    s = {
        "name": name_, "notes": notes, "export": True,
        "keys": [DIM if glow else OFF] * 27,
        "keyLabels": {}, "keyNotes": {},
        "buttons": {b: (DIM if glow else OFF) for b in BTN_IDS}, "playGreen": play_green,
        "buttonLabels": {}, "buttonNotes": {},
        "encoders": {e: {"note": "", "value": ""} for e in ENC_IDS},
        "screen": screen or {},
    }
    if keys is not None:
        s["keys"] = list(keys)
    if chord_block:
        for k, lab in CHORD_IDX.items():
            s["keyLabels"][str(k)] = lab
        s["keyLabels"][str(LOCK_KEY)] = "LOCK"
    if lock:
        s["keys"][LOCK_KEY] = LIT
    for n in lit:
        s["keys"][key_of(n)] = LIT
    for n in blink:
        s["keys"][key_of(n)] = BLINK
    for lab in held:
        s["keys"][[k for k, v in CHORD_IDX.items() if v == lab][0]] = LIT
    for b, v in (buttons or {}).items():
        s["buttons"][b] = v
    for b, v in (button_labels or {}).items():
        s["buttonLabels"][b] = v
    for k, v in (key_labels or {}).items():
        s["keyLabels"][str(k)] = v
    for k, v in (key_notes or {}).items():
        s["keyNotes"][str(k)] = v
    for b, v in (button_notes or {}).items():
        s["buttonNotes"][b] = v
    for e, v in (encoders or {}).items():
        if isinstance(v, str):
            s["encoders"][e] = {"note": "", "value": v}
        else:
            s["encoders"][e] = {"note": v[0], "value": v[1]}
    return s


# ------------------------------------------------- shared with make_editor_mockups.py (the sound editor)
def pc(label, value, glyph="knob", pct=None, **kw):
    """one params column (KNOB n): label, value, glyph (knob bar env wave saw square filter steps dots), its fill"""
    c = {"label": label, "value": value, "glyph": glyph}
    if pct is not None:
        c["pct"] = round(pct, 3)
    c.update(kw)
    return c


BLANK = None        # an empty column: the designer skips a null column (it has no glyph "none": that draws a knob)


# the dense editor (2026-10-06, second pass): `edit8` (up to eight parameters, the active row of four on KNOB 1-4,
# an optional wide shape) and `stack` (every oscillator / LFO / matrix slot as a row, the active one on the knobs);
# both without the header bar and the footer
WAVE_GLYPH = {"SAW": "saw", "SQR": "square", "PWM": "square", "TRI": "wave", "SIN": "wave", "NOIS": "dots",
              "S&H": "steps"}


def cell(label, value, glyph=None, pct=None, **kw):
    """one edit8 / stack cell: label, value, an optional glyph and fill (a cell without a glyph is text only)"""
    c = {"label": label, "value": value}
    if glyph:
        c["glyph"] = glyph
    if pct is not None:
        c["pct"] = round(pct, 3)
    c.update(kw)
    return c


def wave_cell(value, pct=0.5, label="Wave"):
    """a Wave cell: its glyph follows the value (SAW saw, SQR / PWM square, TRI / SIN wave, NOIS dots, S&H steps)"""
    return cell(label, value, WAVE_GLYPH[value], pct)


def editor_screen(panel, note=""):
    """a dense editor screen: the panel alone (no header bar, no knob cards, no footer)"""
    return {"header": None, "cards": None, "panel": panel, "footer": None, "note": note}


def stack_panel(title, right, cols, rows, active, hot=None, title_col=None):
    p = {"kind": "stack", "title": title, "right": right, "cols": cols,
         "rows": [{"label": lab, "cells": cells} for lab, cells in rows], "active": active, "hot": hot}
    if title_col:
        p["titleCol"] = title_col
    return p


OSC_COLS_A = ["Wave", "Level", "Coarse", "Fine"]
OSC_COLS_B = ["Shape", "Key trk", "Sync/Ring", ""]


def osc_row_a(wave, level, coarse, fine, shape=0.5):
    return [wave_cell(wave, shape), cell("Level", f"{level}%", "bar", level / 100), cell("Coarse", coarse),
            cell("Fine", fine)]


# WARM PAD (VA): the four oscillators (main state 21, editor state 1)
WARM_OSC = [osc_row_a("PWM", 63, "0", "0", 0.55), osc_row_a("SAW", 48, "-12", "+7"),
            osc_row_a("SIN", 22, "+12", "-5"), osc_row_a("SAW", 0, "0", "0")]


def osc_stack(title, rows, active, hot=None, title_col=None):
    """OSC bank A: the four oscillators, Wave Level Coarse Fine; the active row is on KNOB 1-4"""
    return stack_panel(title, f"OSC {active + 1} · A", OSC_COLS_A, [(str(i + 1), r) for i, r in enumerate(rows)],
                       active, hot, title_col)


# the engine picker (EDIT held): the melodic engines in engines.c ENGINE_ORDER; the white root keys take the
# first eleven, SELECT reaches all twelve (NOISE)
ENGINES = ["Analog", "FM6", "VA", "Phase", "LoFi", "Sample", "Voice", "Trio", "Wheel", "Grain", "Phys", "Noise"]
ENGINE_KEYS = ["ANLG", "FM6", "VA", "PHASE", "LOFI", "SMPL", "VOICE", "TRIO", "WHEEL", "GRAIN", "PHYS"]


def engine_picker_state(name_, notes, sel, preset, part="chord", buttons=None, button_labels=None):
    """EDIT held: the engine picker of the part being edited (white root keys = engines, KNOB 1 its presets)"""
    return state(
        name_, notes,
        keys=[OFF] * 27, lit=[name(ROOT_WHITE[sel])], buttons={"EDIT": BLINK} | (buttons or {}),
        key_labels={ROOT_WHITE[i]: n for i, n in enumerate(ENGINE_KEYS)},
        key_notes={ROOT_WHITE[0]: "white root keys D4 .. G5: the engines (NOISE: SELECT)"},
        button_notes={"EDIT": "blinks: the picker is open; EDIT tap, OCT- or HOME closes it"},
        button_labels=button_labels,
        encoders={"SELECT": ("ENGINE", ENGINES[sel]), "KNOB1": ("PRESET", preset), "KNOB2": ("INIT", ""), "KNOB3": "", "KNOB4": ""},
        screen=scr({'kind': 'picker', 'items': ENGINES, 'sel': sel, 'label': 'engine · KNOB 1 its presets',
                    'col': C_BASS if part == "bass" else C_CHORD, 'value': preset},
                   foot="a root: engine · " + LAYER_FOOT,
                   note="EDIT held: the engine one at a time; the sound changes as the name flips."),
        chord_block=False,
    )


def save_state(name_, notes, slot, typed, buttons=None, button_labels=None):
    """SAVE tapped: the slot and the name typed on the keys (the same dialog in and out of the editor)"""
    return state(
        name_, notes,
        keys=[OFF] * 27, buttons={"SAVE": LIT, "OCT-": LIT, "OCT+": BLINK} | (buttons or {}),
        key_labels={ROOT_WHITE[i]: c for i, c in enumerate(list("ABCDEFGHIJK"))},
        button_labels=button_labels,
        encoders={"KNOB1": ("SLOT", slot), "KNOB2": "", "KNOB3": "", "KNOB4": ""},
        screen=scr({"kind": "text", "title": "save sound", "col": C_CHORD,
                    "lines": [{"t": slot, "px": 15, "col": "grey"}, {"t": typed, "px": 36, "w": 700, "col": "white"}, "",
                              {"t": "keys: letters · OCT-: delete", "col": "grey"}, {"t": "OCT+: save", "col": "grey"}]},
                   foot="SAVE again: cancel", note="SAVE: the slot and the name being typed; each letter slides in, the cursor blinks."),
        chord_block=False,
    )


IDLE_ENC = {"KNOB1": "0", "KNOB2": "0", "KNOB3": "off", "KNOB4": "25"}
LAYER_FOOT = "OCT-: back · HOME: home"
LAYER_BTN_NOTE = "blinks: its layer is open (locked since the 300 ms hold); OCT- or HOME closes it, a tap still toggles it"
S = []

# 1 ------------------------------------------------------------------------------- idle
S.append(state(
    "1 · idle (CHORD view)",
    "Power-on and idle. No instructions: the name over three racing stripes in the coralroot orchid's colours (red, orange, white) that slide sideways "
    "at the master BPM. The chord block is the left of the keybed (DIM MIN MAJ SUS on F#3 G#3 A#3 C#4, 6 m7 M7 9 on F3 G3 A3 C4, "
    "LOCK on B3), the roots are D4-G5. Every key and button glows dim. Buttons: tap = the action; hold past 300 ms = its layer, "
    "which stays open after release (the button blinks) until OCT- or HOME closes it or another layer button is held.",
    encoders=IDLE_ENC,
    key_notes={key_of("F#3"): "chord types DIM / MIN / MAJ / SUS on the four black keys: momentary, as on Orchid",
               key_of("F3"): "extensions 6 / m7 / M7 / 9 on the white keys under them: momentary, stackable",
               LOCK_KEY: "LOCK: a mode toggle for the chord block (tap on, lit; tap off = momentary keys as usual). On: chord keys "
                         "stay latched when released; with none held, a top-row key (DIM MIN MAJ SUS) resets the latch to that type "
                         "alone and a bottom-row key (6 m7 M7 9) toggles its extension. Hold MIN, release: minor; 6: m6; 9: m6/9; "
                         "9 again: m6; MAJ: major, extensions cleared",
               key_of("D4"): "roots D4 .. G5"},
    button_notes={"SEL": "KEY: tap = Key Mode on/off · hold = the key layer, locked open (root keys pick the tonic)",
                  "ARP": "PERF: tap = performance on/off · hold = the perform layer, locked open (white root keys pick the mode)",
                  "FX": "FX: tap = the sound's main effect on/off · hold = the fx layer, locked open (pick the effect, its parameters)",
                  "ENV": "BASS: tap = bass on/off · hold = the bass layer, locked open (behaviour, register, sound, level) · BASS held + EDIT = the bass sound's editor",
                  "LFO": "LATCH: Sticky keys / arp hold (a chord keeps sounding after release)",
                  "GLO": "OPT: tap = Options menu · held + a knob = its second function (KNOB 1 split point, SELECT metronome level, ALGORITHM bass volume)",
                  "PLAY": "LOOP: tap = play / stop · hold = the loop layer, locked open (slots, sync, quantize)",
                  "REC": "REC: tap = record / overdub arm (red) · hold = undo the last layer",
                  "SEQ": "METRO: tap = metronome / beat on-off (Orchid's BPM press) · hold = the beat layer, locked open (beat, time signature)",
                  "EDIT": "EDIT: tap = the sound editor (EDIT blinks; EDIT or HOME leaves) · hold = engine picker · BASS held + EDIT = the bass sound's editor",
                  "HOME": "HOME: back to the view from any page, menu, layer or the editor · tap again: next View (CHORD / KEYBOARD / NOTES / GEEK OUT / SCOPE)",
                  "SAVE": "SAVE: tap = save the sound (Orchid's Sound long press) · hold = save the loop",
                  "OCT-": "octave of the root keys (-2 .. +2); in a layer or menu: back (OCT+ = OK) · both together: PANIC (all notes off, octave reset)"},
    screen=scr({"kind": "stripes", "bands": ["red", "orange", "white"], "band": 18, "gap": 8, "phase": 0.3, "title": "choralroot"}, note="idle: the name over three racing stripes in the coralroot orchid's colours (red, orange, white) that slide sideways at the BPM; the first chord sweeps the stripes off the screen and the chord name lands where the title was."),
))

# 2 ------------------------------------------------------------------------------- Dmaj7
S.append(state(
    "2 · MAJ + M7 held, D4 pressed → Dmaj7",
    "Simple play style: hold MAJ (A#3) and M7 (A3) before the root. The held modifiers are lit; the four "
    "chord notes light on the root keys where they sound. The screen names the chord in the Orchid "
    "Standard Framework (root big, extensions superscript) and lists the voiced notes.",
    held=["MAJ", "M7"], lit=["D4", "F#4", "A4", "C#5"],
    key_notes={key_of("D4"): "the root pressed", key_of("C#5"): "chord notes light where they sound"},
    encoders=IDLE_ENC,
    screen=scr(chord("D", sup="M7", cols={"root": C_CHORD, "sup": C_BASS}, bubbles=bubbles(["D4", "F#4", "A4", "C#5"], ext=["C#5"])), note="Dmaj7: the name squeezes in from a thin condensed state to its full width (Orchid's squeeze); the notes line under it, the 7th in orange with a block under it."),
))

# 3 ------------------------------------------------------------------------------- LOCK
S.append(state(
    "3 · LOCK on: MIN + 6 + 9 latched, D4 → Dm6/9",
    "LOCK (B3) is a mode toggle for the chord block: tap = on (B3 lit), tap again = off (the chord keys momentary as "
    "usual). With LOCK on, the chord keys held are latched when released, so one hand plays the roots. Once no chord key "
    "is held, a top-row key (DIM MIN MAJ SUS) RESETS the latch to that type alone (the extensions cleared) and a "
    "bottom-row key (6 m7 M7 9) TOGGLES that extension in the latched set without touching the type. Example: hold MIN, "
    "release: minor latched; press 6: m6; press 9: m6/9; press 9 again: m6; press MAJ: major, extensions cleared. Here: "
    "MIN, 6 and 9 latched, nothing held, D4 played → Dm6/9. The latched keys are lit.",
    held=["MIN", "6", "9"], lit=["D4", "F4", "A4", "B4", "E5"], lock=True,
    key_notes={LOCK_KEY: "LOCK on (lit)", key_of("G#3"): "MIN, 6 and 9 latched: lit, not held",
               key_of("F3"): "a bottom-row key toggles its extension; a top-row key resets the type",
               key_of("D4"): "one finger: the root"},
    encoders=IDLE_ENC,
    screen=scr(chord("D", quality="m", sup="6/9", cols={"root": C_CHORD, "sup": C_BASS}, bubbles=bubbles(["D4", "F4", "A4", "B4", "E5"], ext=["B4", "E5"])),
               right="lock", right_col="white",
               note="LOCK on: the chord as usual with a small white 'lock' top-right; it drops in when LOCK goes on and slides out when it goes off."),
))

# 4 ------------------------------------------------------------------------------- voicing +2
S.append(state(
    "4 · VOICING turned +2 while holding",
    "KNOB 1 is always chord voicing (Orchid's Chord Voicing Dial): each click moves the lowest note up an "
    "octave (or the highest down). The lit keys follow the new register, the VOICING card goes hot (accent) "
    "for a moment, the notes line shows the inversion. Muscle memory: never reassigned.",
    held=["MAJ", "M7"], lit=["A4", "C#5", "D5", "F#5"],
    encoders=IDLE_ENC | {"KNOB1": "+2"},
    screen=scr(chord("D", sup="M7", cols={"root": C_CHORD, "sup": C_BASS}, bubbles=bubbles(["A4", "C#5", "D5", "F#5"], ext=["C#5"]), line="voicing +2", lineCol=C_VOICE), note="voicing turned: the notes line reshuffles (the lowest slides to the end), the blue voicing line appears and fades."),
))

# 5 ------------------------------------------------------------------------------- key mode
S.append(state(
    "5 · Key Mode on (C major), E4 pressed → Em",
    "KEY (printed SEL) tapped: its LED is lit and the key shows in the header. No chord type is needed: E in C major is "
    "minor. Out-of-key roots quantize (C# → Csus). A held chord type still overrides. The root keys outside "
    "the scale go dark so the scale can be seen on the keybed.",
    keys=[DIM if (k < ROOT_LO or not key_black(k)) else OFF for k in range(27)],
    lit=["E4", "G4", "B4"], buttons={"SEL": LIT},
    key_notes={key_of("D#4"): "out-of-scale roots dark; pressing one plays the harmonically quantized chord"},
    encoders=IDLE_ENC,
    screen=scr(chord("E", quality="m", cols={"root": C_CHORD}, bubbles=bubbles(["E4", "G4", "B4"])), key="Key: C", note="Key Mode: 'Key: C' top-left in yellow; Em from one finger."),
))

# 6 ------------------------------------------------------------------------------- key layer
tonic_labels = {k: name(k)[:-1] for k in ROOTS}
S.append(state(
    "6 · KEY held: the key layer (locked open)",
    "Hold KEY past 300 ms: the key layer opens and stays open after release (KEY blinks). The root keys pick the tonic "
    "(any of them, its pitch class; hold MIN (G#3) too for a minor key — Orchid's quick key select), KNOB 1-4 become "
    "TONIC / SCALE / TRANSPOSE / SINGLE NOTES. A KEY tap inside still toggles Key Mode. OCT- or HOME closes the layer "
    "(Key Mode enabled); holding another layer button switches to that layer. Screen: the keyboard with the tonic lit.",
    lit=["C5"], buttons={"SEL": BLINK},
    key_labels=tonic_labels,
    key_notes={key_of("C5"): "the current tonic, lit", key_of("G#3"): "hold MIN with the tonic key for a minor key"},
    button_notes={"SEL": LAYER_BTN_NOTE},
    encoders={"KNOB1": ("TONIC", "C"), "KNOB2": ("SCALE", "MAJ"), "KNOB3": ("TRANSPOSE", "+0"), "KNOB4": ("SINGLE NOTES", "FULL")},
    screen=scr({"kind": "keyboard", "title": "select key", "titleSize": 26, "col": C_KEY, "lit": [{"k": "C5", "col": C_KEY}], "labels": {str(ROOT_WHITE[6]): "C"}}, key="Key: C", foot="MIN: minor · " + LAYER_FOOT, note="the key layer: the keyboard slides up; the tonic key lights yellow and pulses."),
))

# 7 ------------------------------------------------------------------------------- perform layer
perf_modes = ["STRUM", "STR 2", "SLOP", "ARP", "ARP 2", "PATT", "HARP"]
perf_labels = {ROOT_WHITE[i]: m for i, m in enumerate(perf_modes)}
S.append(state(
    "7 · PERF held: the perform layer (locked open)",
    "Hold PERF past 300 ms: the perform layer opens and stays (PERF blinks). The white root keys D4-C5 are the performance "
    "modes (press one: selected and enabled), KNOB 1-4 the selected mode's parameters — here ARP: RATE / ORDER / RANGE / "
    "GATE (STRUM: SPEED / DIRECTION / RANGE / -; SLOP adds AMOUNT; PATTERN: RATE / PATTERN 1-13 / RANGE / GATE; HARP: "
    "SPEED / DIRECTION / RANGE / GATE). A PERF tap toggles the performance; OCT- or HOME closes the layer.",
    keys=[OFF] * 27, lit=["G4"], buttons={"ARP": BLINK},
    key_labels=perf_labels,
    key_notes={ROOT_WHITE[0]: "white root keys D4 .. C5 pick the mode", ROOT_WHITE[3]: "ARP: the selected mode, lit"},
    button_notes={"ARP": LAYER_BTN_NOTE},
    encoders={"KNOB1": ("RATE", "1/8"), "KNOB2": ("ORDER", "UP"), "KNOB3": ("RANGE", "1 OCT"), "KNOB4": ("GATE", "70%")},
    screen=scr({'kind': 'picker', 'items': ['Strum', 'Strum 2 Octaves', 'Slop', 'Arpeggiate', 'Arp 2 Octaves', 'Pattern', 'Harp'], 'sel': 3, 'label': 'perform', 'col': 'white', 'value': '1/8'}, foot="a root: mode · " + LAYER_FOOT, note="the perform layer: one mode at a time, huge; the next and previous peek above and below; a change flips the words like a split-flap board."),
    chord_block=False,
))

# 8 ------------------------------------------------------------------------------- arp playing
S.append(state(
    "8 · ARP on, Am7 held: arpeggio playing",
    "PERF lit: the arpeggiator walks the voiced chord at 1/8 of the master BPM. The chord notes are lit "
    "and the note sounding now blinks (the keys show what the engine plays, as Felucca does). KNOB 3 rides "
    "the mode's main parameter (arp RATE) without opening the layer; the PERF card names it. The footer "
    "strip shows the beat.",
    held=["MIN", "m7"], lit=["A4", "C5", "G5"], blink=["E5"], buttons={"ARP": LIT},
    encoders=IDLE_ENC | {"KNOB3": ("ARP RATE", "1/8")},
    screen=scr({"kind": "arp", "root": "A", "quality": "m", "sup": "7", "cols": {"root": C_CHORD, "sup": C_BASS}, "notes": bubbles(["A4", "C5", "E5", "G5"], ext=["G5"]), "pos": 2, "hopCol": C_CHORD, "line": "arp 1/8"}, right="Arp", note="arpeggio playing: the sounding note sits on a white block and hops along the line in time; the dotted arc shows the next hop."),
))

# 9 ------------------------------------------------------------------------------- loop layer
slot_labels = {ROOT_WHITE[i]: str(i + 1) for i in range(10)}
slot_labels[ROOT_BLACK[0]] = "CLEAR"
slot_labels[ROOT_BLACK[1]] = "UNDO"
S.append(state(
    "9 · LOOP held: the loop layer (locked open)",
    "Hold LOOP past 300 ms: the loop layer opens and stays (LOOP blinks). The white root keys D4-F5 are the ten loop slots "
    "(lit = holds a loop, blinking = the one selected); D#4 is CLEAR (hold 1 s), F#4 UNDO the last layer. KNOB 1-4: SYNC "
    "(FREE, 1, 2, 4, 8, 16 bars — Orchid's loop waiting room), QUANTIZE, COUNT-IN, LOOP LEVEL. A LOOP tap still plays / "
    "stops; OCT- or HOME closes the layer. The screen shows the ring with the length inside.",
    keys=[OFF] * 27, blink=["D4"], lit=["E4", "F4"], buttons={"PLAY": BLINK},
    key_labels=slot_labels,
    key_notes={ROOT_WHITE[0]: "slot 1 selected (blinks); slots 2-3 hold loops (lit)", ROOT_BLACK[0]: "CLEAR (hold) · UNDO on the first black root keys"},
    button_notes={"PLAY": LAYER_BTN_NOTE},
    encoders={"KNOB1": ("SYNC", "4 BAR"), "KNOB2": ("QUANTIZE", "1/16"), "KNOB3": ("COUNT-IN", "ON"), "KNOB4": ("LOOP LEVEL", "100")},
    screen=scr({'kind': 'picker', 'items': ['Free', '1 bar', '2 bars', '4 bars', '8 bars', '16 bars'], 'sel': 3, 'label': 'loop length', 'col': 'red'}, key="Loop", ring=0, note="the loop layer with no loop: the length, one at a time, inside the empty ring; SELECT or a root key changes it with a slide."),
    chord_block=False,
))

# 10 ------------------------------------------------------------------------------ recording
S.append(state(
    "10 · REC tapped: recording a 4-bar loop (bar 2 of 4)",
    "REC tapped: after the one-bar count-in the loop records for 4 bars at the master BPM (free mode: until "
    "REC or LOOP is tapped again). REC's red LED blinks, the header shows REC and bar.beat, the Orchid ring "
    "progress indicator runs around the edge of the screen in red. The chord view keeps naming what is played.",
    held=["MAJ"], lit=["F4", "A4", "C5"], buttons={"REC": BLINK, "PLAY": LIT},
    encoders=IDLE_ENC,
    screen=scr(chord("F", cols={"root": C_CHORD}, bubbles=bubbles(["F4", "A4", "C5"])), key="Rec", right="2.3", ring=0.35, ring_rec=True, note="recording: the red ring runs round the edge; 'Rec' blinks with the beat."),
))

# 11 ------------------------------------------------------------------------------ playing + overdub
S.append(state(
    "11 · loop playing, overdub armed, loop layer open",
    "The loop plays (PLAY's green LED, like Felucca's transport); REC is armed for overdub (lit), so the "
    "next notes record on top as a new layer (hold REC: undo it). With the loop layer open (LOOP blinks) the picker "
    "offers Overdub / Pause / Undo / Clear and OCT+ does it. The loop's notes glow on the root keys as they sound while "
    "a live chord is lit. Voicing, performance and bass apply live at playback: the loop stores the chords, not the notes.",
    held=["MAJ"], lit=["G4", "B4", "D5"], buttons={"PLAY": BLINK, "REC": LIT, "OCT+": BLINK}, play_green=1,
    key_notes={key_of("G4"): "a chord played live over the loop"},
    button_notes={"PLAY": LAYER_BTN_NOTE, "OCT+": "OK: does the picked action"},
    encoders=IDLE_ENC,
    screen=scr({'kind': 'picker', 'items': ['Overdub', 'Pause', 'Undo', 'Clear'], 'sel': 0, 'label': 'loop 1', 'col': 'red'}, key="Loop 1", ring=0.62, note="the loop layer while a loop plays: Overdub / Pause / Undo / Clear one at a time inside the ring; OCT+ does it."),
))

# 12 ------------------------------------------------------------------------------ options menu
S.append(state(
    "12 · OPT tapped: the Options menu",
    "Tap OPT: Orchid's Options menu, one setting per screen. SELECT scrolls (its printed name), KNOB 1 changes the "
    "selected value, OCT+ enters / confirms (blinks), OCT- goes back (lit), HOME leaves — Felucca's dialog "
    "convention. Entries: Play Style, Extension Addition, Bass Behaviour, Secret Chords, Single Notes, "
    "Velocity, Quantization, MIDI Channels, MIDI Clock, View, Metronome, Palette, LEDs, Version, Update.",
    buttons={"GLO": LIT, "OCT-": LIT, "OCT+": BLINK},
    button_notes={"GLO": "OPT lit while the menu is open; tap again, HOME or OCT- to leave"},
    encoders={"SELECT": ("SCROLL", ""), "KNOB1": ("VALUE", ""), "KNOB2": "", "KNOB3": "", "KNOB4": ""},
    screen=scr({'kind': 'picker', 'items': ['Battery', 'View', 'Audio Output', 'MIDI Channels', 'MIDI In', 'MIDI Out', 'MIDI Clock', 'Play Style', 'Extension Addition', 'Single Notes', 'Secret Chords', 'Quantization', 'Metronome', 'Velocity Sense', 'Motion', 'Palette', 'LEDs', 'Version', 'Upgrade firmware'], 'sel': 7, 'label': 'options · KNOB 1 sets', 'col': 'white', 'value': 'Simple'}, note="OPT tapped: one setting per screen — its name big, its value under it; SELECT slides to the next setting, KNOB 1 changes the value."),
))

# 13 ------------------------------------------------------------------------------ secret chord
S.append(state(
    "13 · Secret chord: DIM + SUS + D4 → D5 (power)",
    "Secret Chords (Options: Simple / All): two chord types held together select the extra types proved in "
    "the Orchid firmware: DIM+SUS = power chord (D⁵), MAJ+SUS = augmented (D+), MIN+SUS = [0 3 5] (Dm add4). "
    "Extensions still stack (with LOCK on, the pair latches like any held chord keys). The screen uses Orchid's own labels.",
    held=["DIM", "SUS"], lit=["D4", "A4", "D5"],
    encoders=IDLE_ENC,
    screen=scr(chord("D", sup="5", cols={"root": C_CHORD, "sup": C_LOOP}, bubbles=[{"t": "D4", "col": C_CHORD}, {"t": "A4", "col": C_CHORD}, {"t": "D5", "col": C_LOOP, "mark": True}]), note="a secret chord: the extra tone in red; the name flashes once."),
))

# 14 ------------------------------------------------------------------------------ bass on
S.append(state(
    "14 · BASS on (03 SUB), bass voicing -1",
    "BASS tapped: the bass part plays the chord root through its own engine and MIDI channel 2. ALGORITHM "
    "browses the bass sounds (Orchid's Bass Dial turn; OPT + ALGORITHM: bass volume), KNOB 2 is the bass register "
    "(octaves). The footer names the bass sound; the BASS card its voicing.",
    held=["MAJ"], lit=["E4", "G#4", "B4"], buttons={"ENV": LIT},
    key_notes={key_of("E4"): "E major; the bass plays E1 (below the keybed)"},
    encoders=IDLE_ENC | {"KNOB2": "-1", "ALGORITHM": "03 SUB"},
    screen=scr({"kind": "meter", "value": "03", "sub": "SUB", "label": "bass", "col": C_BASS, "pct": 3 / 12, "segments": 12}, note="ALGORITHM turned: the bass sound's number huge in orange over a stripe meter of the bank; the stripes fill one by one."),
))

# 15 ------------------------------------------------------------------------------ bass layer
S.append(state(
    "15 · BASS held: the bass layer (locked open)",
    "Hold BASS past 300 ms: the bass layer opens and stays (BASS blinks). KNOB 1-4 become BEHAVIOUR (Chords Only / Unison / "
    "Single Notes / Solo — Orchid's Bass Dial hold menu), REGISTER, SOUND, LEVEL. The root keys preview the bass sound "
    "alone. A BASS tap toggles the bass; EDIT while BASS is held opens the bass sound in the editor; OCT- or HOME closes "
    "the layer.",
    keys=[OFF] * 27, buttons={"ENV": BLINK},
    button_notes={"ENV": LAYER_BTN_NOTE},
    encoders={"KNOB1": ("BEHAVIOUR", "CHORDS"), "KNOB2": ("REGISTER", "-1"), "KNOB3": ("SOUND", "03 SUB"), "KNOB4": ("LEVEL", "80")},
    screen=scr({'kind': 'picker', 'items': ['Chords Only', 'Unison Bass', 'Bass Single Notes', 'Solo'], 'sel': 0, 'label': 'bass', 'col': 'orange'}, foot="KNOB 3: sound · " + LAYER_FOOT, note="the bass layer: the bass behaviour, one at a time, in orange."),
    chord_block=False,
))

# 16 ------------------------------------------------------------------------------ fx layer
fx_names = ["REV", "DELAY", "CHOR", "PHASE", "DRIVE", "TREM", "FILT", "ENSMB"]
fx_labels = {ROOT_WHITE[i]: n for i, n in enumerate(fx_names)}
S.append(state(
    "16 · FX held: the fx layer (locked open)",
    "Tap FX: the sound's main effect on/off (Orchid's FX Dial default). Hold FX past 300 ms: the fx layer opens and stays "
    "(FX blinks); the white root keys pick which effect KNOB 4 rides (Orchid's FX push + turn), KNOB 1-3 its parameters, "
    "KNOB 4 the amount. OCT- or HOME closes it.",
    keys=[OFF] * 27, lit=["D4"], buttons={"FX": BLINK},
    key_labels=fx_labels,
    button_notes={"FX": LAYER_BTN_NOTE},
    encoders={"KNOB1": ("SIZE", "60"), "KNOB2": ("DAMP", "40"), "KNOB3": ("TYPE", "ROOM"), "KNOB4": ("AMOUNT", "25")},
    screen=scr({'kind': 'picker', 'items': ['Reverb', 'Chorus', 'Delay', 'Phaser', 'Drive', 'Tremolo', 'Filter'], 'sel': 0, 'label': 'fx · KNOB 4 amount', 'col': 'green', 'value': '05'}, foot="a root: effect · " + LAYER_FOOT, note="the fx layer: the effect one at a time in green with its amount under it."),
    chord_block=False,
))

# 17 ------------------------------------------------------------------------------ geek out view
S.append(state(
    "17 · GEEK OUT view (HOME tapped again)",
    "HOME tapped on the view cycles Orchid's View modes: CHORD (default), KEYBOARD, NOTES, GEEK OUT and SCOPE (React). "
    "Geek Out shows the chord, every note, the status lines and the 27-key strip with the sounding notes.",
    held=["MIN", "m7"], lit=["D4", "F4", "A4", "C5"], buttons={"SEL": LIT},
    encoders=IDLE_ENC | {"KNOB1": "+1"},
    screen=scr({"kind": "geek", "root": "D", "quality": "m", "sup": "7", "cols": {"root": C_CHORD, "sup": C_BASS}, "notes": ["D4", "F4", "A4", "C5"], "lit": [{"k": "D4", "col": C_CHORD}, {"k": "F4", "col": C_CHORD}, {"k": "A4", "col": C_CHORD}, {"k": "C5", "col": C_BASS}], "lines": ["voicing +1 · simple", "bass off · 120 bpm"]}, key="Key: D minor", right="Trans +0", note="Geek Out: the one dense view, by request."),
))

# 18 ------------------------------------------------------------------------------ sound browse popup
S.append(state(
    "18 · PRESETS turned: browsing sounds",
    "Turning PRESETS pops the sound list over the panel (as the stock firmware and Felucca do), the sound "
    "loads at once and the popup fades after a second. Sounds come from Felucca's engines (FM6, ANALOG, "
    "WHEEL, PHYS…) and the VA, with ChoralRoot's own bank of chord-friendly presets; bass sounds are a separate bank "
    "on ALGORITHM. SAVE stores an edited sound to a user slot.",
    held=["MAJ"], lit=["E4", "G#4", "B4"],
    encoders=IDLE_ENC | {"PRESETS": "13 EP", "KNOB4": "40"},
    screen=scr({"kind": "meter", "value": "13", "sub": "EP", "label": "sound", "col": C_CHORD, "pct": 13 / 48, "segments": 16}, note="PRESETS turned: the sound's number huge, its name under it, the stripe meter is the place in the bank."),
))

# 19 ------------------------------------------------------------------------------ panic
S.append(state(
    "19 · OCT- + OCT+ together: PANIC",
    "Press OCT- and OCT+ together (a chord nothing else uses; the stock firmware resets the octave with it): "
    "every scheduler stops, every note gets its note-off, MIDI CC 123 goes out on the three channels, the octave "
    "resets, the LEDs flash once and the screen says so. Settings and loops are kept (the LOCK latch is cleared).",
    keys=[OFF] * 27, buttons={b: OFF for b in BTN_IDS} | {"OCT-": LIT, "OCT+": LIT},
    encoders=IDLE_ENC,
    screen=scr({"kind": "big", "value": "PANIC", "label": "all notes off", "block": "red", "size": 64}, note="panic: the whole screen goes red with black type, shakes once, and fades back."),
))

# 20 ------------------------------------------------------------------------------ fx amount popup (and the BPM)
S.append(state(
    "20 · KNOB 4 turned: Reverb 05 (SELECT: the BPM the same way)",
    "Any knob turn shows its value big for a second, then the view returns (Orchid's dial screens). Levels "
    "and amounts draw as an inverted fill rising with the value — Orchid's Bass Volume screen. SELECT is the tempo and "
    "shows the BPM the same way (`120 / bpm` in white, the stripes pulsing at the beat; OPT + SELECT: the metronome "
    "level); METRO taps the metronome on and off.",
    held=["MAJ"], lit=["D4", "F#4", "A4"],
    encoders=IDLE_ENC | {"KNOB4": "05", "SELECT": ("BPM", "120")},
    screen=scr({"kind": "meter", "value": "05", "label": "reverb", "col": C_FX, "pct": 0.25, "segments": 12}, note="KNOB 4 turned: the amount in green over its stripe meter; back to the view a second later. SELECT turned: '120 / bpm' in white, same meter."),
))

# 21 ------------------------------------------------------------------------------ the sound editor (pointer)
S.append(state(
    "21 · EDIT tapped: the sound editor (see the editor file)",
    "EDIT tap opens the sound editor on the current chord sound (BASS held + EDIT: the bass sound); EDIT blinks while it "
    "is open, EDIT or HOME leaves. The function buttons become sections — OSC FILT ENV LFO on the top row (the signal "
    "flow), MOD FX MIX on the bottom (the output stage), OPT = SHIFT (fine steps; SHIFT + EDIT: chord / bass). The "
    "editor is dense: no header bar and no footer, a section is one view of up to eight parameters (or a stack of its "
    "oscillators / LFOs / matrix slots), and the row on KNOB 1-4 is drawn in the knob colours with a bar, the rest grey. "
    "A section tap steps the active row (the next oscillator here), a hold swaps to its B bank; SELECT does what the tap "
    "does; root keys audition. This is the first view (WARM PAD, VA, the four oscillators, OSC 1 on the knobs); for "
    "the whole editor see choralroot-fm1-sound-editor-mockups.json (design/make_editor_mockups.py).",
    held=["MAJ"], lit=["D4", "F#4", "A4"], buttons={"EDIT": BLINK, "FX": LIT},
    button_labels=EDITOR_BUTTON_LABELS,
    button_notes={"EDIT": "blinks: the editor is open; tap again (or HOME) to leave",
                  "FX": "OSC: tap = the next oscillator on the knobs · hold = bank B (shape, key track, sync / ring)"},
    encoders={"SELECT": ("OSC", "1"), "KNOB1": ("WAVE", "PWM"), "KNOB2": ("LEVEL", "63%"), "KNOB3": ("COARSE", "0"),
              "KNOB4": ("FINE", "0")},
    screen=editor_screen(osc_stack("WARM PAD", WARM_OSC, 0),
                         note="the editor: the four oscillators as rows, OSC 1 on the knobs in the knob colours with "
                              "the active bar beside it, the other rows grey; the editor opens with the rows dropping "
                              "in one after another. See choralroot-fm1-sound-editor-mockups.json for every section."),
))

# 22 ------------------------------------------------------------------------------ engine picker
S.append(engine_picker_state(
    "22 · EDIT held: the engine picker",
    "Hold EDIT: the white root keys are the engines in the firmware's order (ANALOG, FM6, VA, PHASE, LOFI, SAMPLE, VOICE, "
    "TRIO, WHEEL, GRAIN, PHYS; NOISE on SELECT); press one and the sound switches engine, keeping its envelope and sends. "
    "KNOB 1 steps through the engine's factory presets, KNOB 2 inits the sound. The picker stays open after release "
    "like a layer (EDIT blinks); OCT- or HOME closes it. EDIT held inside the editor opens the same picker (it closes back to the editor view it came from).",
    1, "13 EP",
))

# 23 ------------------------------------------------------------------------------ save a sound
S.append(save_state(
    "23 · SAVE tapped: name and save the sound",
    "SAVE tapped (on the view or in the editor: the same dialog; EDIT keeps blinking in the editor): pick a user slot with "
    "KNOB 1, name it with the keys (Felucca's naming: the keys type letters, OCT- deletes, OCT+ confirms), and the sound "
    "lands in one of 32 user slots, listed after the factory bank on PRESETS. SAVE held: the loop's save / load / delete.",
    "U03", "WARM EP_",
))

# 24 ------------------------------------------------------------------------------ the squeeze
S.append(state(
    "24 · a chord change, mid-squeeze (Dmaj7 → Em, frame 2 of 6)",
    "Orchid's trick, kept and played up: when the chord changes, the old name squeezes to a thin condensed column "
    "and the new one stretches out from it (about 120 ms). Long names stay condensed to fit the width, as on the "
    "Orchid's own screen. This frame shows the new name half-way out.",
    held=["MIN"], lit=["E4", "G4", "B4"], buttons={"SEL": LIT},
    encoders=IDLE_ENC,
    screen=scr(chord("E", quality="m", cols={"root": C_CHORD}, squeeze=0.45, bubbles=bubbles(["E4", "G4", "B4"])), key="Key: C", note="one frame of the squeeze: the name at 45 % width on its way to full."),
))

assert len(S) <= 24, "the designer loads at most 24 states (index.html MAX_STATES)"

design = {
    "format": "fm1-panel-design", "version": 1, "device": "M-VAVE FM-1",
    "name": "ChoralRoot FM-1 mockups",
    "palette": "MOD",
    "labels": {"buttons": BUTTON_LABELS, "encoders": ENCODER_LABELS},
    "notes": "ChoralRoot on the M-VAVE FM-1: a Telepathic Orchid-style chord instrument with Felucca's sound engines and the VA. "
             "The Orchid's 2x4 chord block is the left of the keybed: DIM MIN MAJ SUS on the black keys F#3 G#3 A#3 C#4, "
             "6 m7 M7 9 on the white keys F3 G3 A3 C4, and B3 = LOCK, a mode toggle for the block (tap on, lit; tap off: the "
             "chord keys momentary as usual). LOCK on: chord keys stay latched when released (one hand plays the roots); "
             "with none held, a top-row key resets the latch to that type alone and a bottom-row key toggles its extension "
             "(hold MIN, release: minor; 6: m6; 9: m6/9; 9 again: m6; MAJ: major, extensions cleared); latched keys are "
             "lit. The 18 keys D4-G5 are the roots. The function buttons carry the Orchid dials' button functions — "
             "KEY (printed SEL), EDIT (printed EDIT), PERF, FX, BASS, LATCH, OPT, HOME, SAVE, METRO, LOOP, REC. Grammar: "
             "tap = the action; KEY / PERF / FX / BASS / LOOP / METRO held past 300 ms open their layer AND LOCK it (it stays "
             "after release, the button blinks; the root keys and KNOB 1-4 change job and the screen shows how); OCT- (back) "
             "or HOME closes it, holding another layer button switches to it, a tap inside still does the on/off action. "
             "EDIT tap = the sound editor (its own file: choralroot-fm1-sound-editor-mockups.json), EDIT held = the engine "
             "picker, BASS held + EDIT = the bass sound's editor. OPT is the knob shift: OPT + KNOB 1 the split point, OPT + "
             "SELECT the metronome level, OPT + ALGORITHM the bass volume. KNOB 1 is always chord voicing, KNOB 2 bass voicing, "
             "KNOB 3 the performance parameter, KNOB 4 the FX amount; PRESETS browses sounds, ALGORITHM the bass sounds, SELECT "
             "is the BPM, MASTER the volume. OCT-/OCT+ shift the root keys (and are back / OK in layers and menus; both: panic). "
             "States 1-24 walk through idle, a chord, LOCK, voicing, Key Mode and its layer, the perform layer, an arpeggio, the "
             "loop layer, recording, playback, the Options menu, a secret chord, bass and its layer, the fx layer, the Geek Out "
             "view, sound browsing, panic, a dial pop-up (KNOB 4; SELECT's BPM the same), the sound editor, the engine picker, "
             "saving a sound and the squeeze (the designer holds 24 states, so the BPM pop-up is folded into state 20). "
             "Screens follow Orchid's rule — one big thing per screen — in a 1960s mod language: black, white, roundel red and "
             "blue, pop yellow, orange and green; flat blocks, stripes and targets; heavy type that squeezes to fit as on the "
             "Orchid. Roles: white chords and sounds, blue voicing, orange bass and 7ths, red loop / REC / secret tones, yellow "
             "Key Mode, green FX. No menus and no circled numbers: a choice is one item at a time with its neighbours peeking "
             "(a picker), a knob value is a huge number over a stripe meter, the idle screen is the name over mod racing stripes. Every state notes "
             "its motion.",
    "states": S,
}

if __name__ == "__main__":
    with open(OUT, "w") as f:
        json.dump(design, f, indent=1)
    print("wrote", OUT, "with", len(S), "states")
