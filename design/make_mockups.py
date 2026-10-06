#!/usr/bin/env python3
"""Generate the ChoralRoot FM-1 interface mock-ups as an fm1-panel-design JSON.

  python3 design/make_mockups.py            # writes design/choralroot-fm1-mockups.json

Open the file in the ChoralRootFM1Designer (../ChoralRootFM1Designer/index.html, or
`?design=examples/choralroot-fm1-mockups.json` on its dev server) to view, edit and export it.
The control mapping and every state here are the normative interface sketch of PLAN.md §3-§6;
when this file and PLAN.md disagree, fix one of them explicitly.

Layout (2026-10-05, second pass): the Orchid's 2x4 chord block sits on the LEFT OF THE KEYBED —
chord types on the black keys F#3 G#3 A#3 C#4 (DIM MIN MAJ SUS), extensions on the white keys
F3 G3 A3 C4 (6 m7 M7 9), B3 unused — and the roots are the 18 keys from D4 to G5. The twelve
function buttons carry the Orchid dials' button-like functions.
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
# (extensions) on the white keys; B3 is left out so the two rows line up as on Orchid
CHORD_KEYS = {"F#3": "DIM", "G#3": "MIN", "A#3": "MAJ", "C#4": "SUS", "F3": "6", "G3": "m7", "A3": "M7", "C4": "9"}
CHORD_IDX = {key_of(n): lab for n, lab in CHORD_KEYS.items()}
ROOT_LO = key_of("D4")                                   # roots: D4 .. G5 (18 keys)
ROOTS = list(range(ROOT_LO, 27))
ROOT_WHITE = [k for k in ROOTS if not key_black(k)]      # 11 white root keys: D4 E4 F4 G4 A4 B4 C5 D5 E5 F5 G5
ROOT_BLACK = [k for k in ROOTS if key_black(k)]          # 7 black root keys

BUTTON_LABELS = {
    "FX": "FX", "SEL": "EDIT", "ENV": "BASS", "LFO": "LATCH", "EDIT": "KEY", "GLO": "OPT",
    "HOME": "HOME", "SAVE": "SAVE", "ARP": "PERF", "SEQ": "METRO", "PLAY": "LOOP", "REC": "REC",
    "OCT-": "OCT-", "OCT+": "OCT+",
}
ENCODER_LABELS = {
    "MASTER": "VOLUME", "SELECT": "BPM", "PRESETS": "SOUND", "ALGORITHM": "BASS SND",
    "KNOB1": "VOICING", "KNOB2": "BASS VOICE", "KNOB3": "PERFORM", "KNOB4": "FX",
}


KEY_COLS = {"Key": "yellow", "Rec": "red", "Loop": "red"}


def header(key="", right="", icon="none", bpm="", rec=False):
    """Orchid's top line: the key top-left (in the colour of what owns it), a status top-right, the battery"""
    h = {"icon": icon, "bpm": bpm, "mid": key, "right": right, "batt": 3, "usb": False}
    for word, col in KEY_COLS.items():
        if key.startswith(word):
            h["midCol"] = col
    if rec:
        h["rec"] = True
    return h


def scr(panel, key="", right="", icon="none", bpm="", rec=False, ring=None, ring_rec=False, foot=None, message=None, message_col=None, note=""):
    """one big thing per screen: a panel under Orchid's thin top line, an optional one-line footer"""
    o = {"header": header(key, right, icon, bpm, rec), "cards": None, "panel": panel,
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
          chord_block=True):
    """glow: every LED dim unless set otherwise (Felucca's idle glow); lit / blink: root key names;
    held: chord-block labels (DIM, M7, ...) lit; chord_block: print the block's labels on its keys"""
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
        s["keys"][key_of("B3")] = OFF                      # the unused key stays dark
    for n in lit:
        s["keys"][key_of(n)] = LIT
    for n in blink:
        s["keys"][key_of(n)] = BLINK
    for lab in held:
        s["keys"][[k for k, v in CHORD_IDX.items() if v == lab][0]] = LIT
    for b, v in (buttons or {}).items():
        s["buttons"][b] = v
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


IDLE_ENC = {"KNOB1": "0", "KNOB2": "0", "KNOB3": "off", "KNOB4": "25"}
S = []

# 1 ------------------------------------------------------------------------------- idle
S.append(state(
    "1 · idle (CHORD view)",
    "Power-on and idle. No instructions: the name over three racing stripes in the coralroot orchid's colours (red, orange, white) that slide sideways "
    "at the master BPM. The chord block is the left "
    "of the keybed (DIM MIN MAJ SUS on F#3 G#3 A#3 C#4, 6 m7 M7 9 on F3 G3 A3 C4, B3 unused), the roots are D4-G5. "
    "Every key and button glows dim.",
    encoders=IDLE_ENC,
    key_notes={key_of("F#3"): "chord types DIM / MIN / MAJ / SUS on the four black keys: momentary, as on Orchid",
               key_of("F3"): "extensions 6 / m7 / M7 / 9 on the white keys under them: momentary, stackable",
               key_of("B3"): "unused (keeps the two rows aligned as Orchid's block)",
               key_of("D4"): "roots D4 .. G5"},
    button_notes={"EDIT": "KEY: tap = Key Mode on/off · hold = key layer (root keys pick the tonic)",
                  "ARP": "PERF: tap = performance on/off · hold = perform layer (white root keys pick the mode)",
                  "FX": "FX: tap = the sound's main effect on/off · hold = fx layer (pick the effect, its parameters)",
                  "ENV": "BASS: tap = bass on/off · hold = bass layer (behaviour, register, sound, level)",
                  "LFO": "LATCH: Sticky keys / arp hold (a chord keeps sounding after release)",
                  "GLO": "OPT: tap = Options menu · held + a knob = that knob's second function (Orchid's press + turn)",
                  "PLAY": "LOOP: tap = play / stop · hold = loop layer (slots, sync, quantize)",
                  "REC": "REC: tap = record / overdub arm (red) · hold = undo the last layer",
                  "SEQ": "METRO: tap = metronome / beat on-off (Orchid's BPM press) · hold = beat picker",
                  "SEL": "EDIT: tap = the sound's edit pages (SELECT turns pages, KNOB 1-4 edit) · hold = engine picker · BASS held + EDIT = edit the bass sound",
                  "HOME": "HOME: back to the view from any page, menu or layer · tap again: next View (CHORD / KEYBOARD / NOTES / GEEK OUT / SCOPE)",
                  "SAVE": "SAVE: tap = save the sound (Orchid's Sound long press) · hold = save the loop",
                  "OCT-": "octave of the root keys (-2 .. +2); in menus: back / OK · both together: PANIC (all notes off, octave reset)"},
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

# 3 ------------------------------------------------------------------------------- voicing +2
S.append(state(
    "3 · VOICING turned +2 while holding",
    "KNOB 1 is always chord voicing (Orchid's Chord Voicing Dial): each click moves the lowest note up an "
    "octave (or the highest down). The lit keys follow the new register, the VOICING card goes hot (accent) "
    "for a moment, the notes line shows the inversion. Muscle memory: never reassigned.",
    held=["MAJ", "M7"], lit=["A4", "C#5", "D5", "F#5"],
    encoders=IDLE_ENC | {"KNOB1": "+2"},
    screen=scr(chord("D", sup="M7", cols={"root": C_CHORD, "sup": C_BASS}, bubbles=bubbles(["A4", "C#5", "D5", "F#5"], ext=["C#5"]), line="voicing +2", lineCol=C_VOICE), note="voicing turned: the notes line reshuffles (the lowest slides to the end), the blue voicing line appears and fades."),
))

# 4 ------------------------------------------------------------------------------- key mode
S.append(state(
    "4 · Key Mode on (C major), E4 pressed → Em",
    "KEY tapped: its LED is lit and the key shows in the header. No chord type is needed: E in C major is "
    "minor. Out-of-key roots quantize (C# → Csus). A held chord type still overrides. The root keys outside "
    "the scale go dark so the scale can be seen on the keybed.",
    keys=[DIM if (k < ROOT_LO or not key_black(k)) else OFF for k in range(27)],
    lit=["E4", "G4", "B4"], buttons={"EDIT": LIT},
    key_notes={key_of("D#4"): "out-of-scale roots dark; pressing one plays the harmonically quantized chord"},
    encoders=IDLE_ENC,
    screen=scr(chord("E", quality="m", cols={"root": C_CHORD}, bubbles=bubbles(["E4", "G4", "B4"])), key="Key: C", note="Key Mode: 'Key: C' top-left in yellow; Em from one finger."),
))

# 5 ------------------------------------------------------------------------------- key layer
tonic_labels = {k: name(k)[:-1] for k in ROOTS}
S.append(state(
    "5 · KEY held: the key layer",
    "Hold KEY: the root keys pick the tonic (any of them, its pitch class; hold MIN (G#3) too for a minor "
    "key — Orchid's quick key select), KNOB 1-4 become TONIC / SCALE / TRANSPOSE / SINGLE NOTES. Let go: "
    "back to playing, Key Mode enabled. Screen: the 12 pitch classes as tiles with the tonic on and the "
    "scale's notes marked.",
    lit=["C5"], buttons={"EDIT": BLINK},
    key_labels=tonic_labels,
    key_notes={key_of("C5"): "the current tonic, lit", key_of("G#3"): "hold MIN with the tonic key for a minor key"},
    button_notes={"EDIT": "held (blinks): the layer is up"},
    encoders={"KNOB1": ("TONIC", "C"), "KNOB2": ("SCALE", "MAJ"), "KNOB3": ("TRANSPOSE", "+0"), "KNOB4": ("SINGLE NOTES", "FULL")},
    screen=scr({"kind": "keyboard", "title": "select key", "titleSize": 26, "col": C_KEY, "lit": [{"k": "C5", "col": C_KEY}], "labels": {str(ROOT_WHITE[6]): "C"}}, key="Key: C", foot="a root: the key · MIN held: minor", note="KEY held: the keyboard slides up; the tonic key lights yellow and pulses."),
))

# 6 ------------------------------------------------------------------------------- perform layer
perf_modes = ["STRUM", "STR 2", "SLOP", "ARP", "ARP 2", "PATT", "HARP"]
perf_labels = {ROOT_WHITE[i]: m for i, m in enumerate(perf_modes)}
S.append(state(
    "6 · PERF held: the perform layer",
    "Hold PERF: the white root keys D4-C5 are the performance modes (press one: selected and enabled), "
    "KNOB 1-4 the selected mode's parameters — here ARP: RATE / ORDER / RANGE / GATE (STRUM: SPEED / "
    "DIRECTION / RANGE / -; SLOP adds AMOUNT; PATTERN: RATE / PATTERN 1-13 / RANGE / GATE; HARP: SPEED / "
    "DIRECTION / RANGE / GATE). The screen shows the modes as tiles and the parameters as cards.",
    keys=[OFF] * 27, lit=["G4"], buttons={"ARP": BLINK},
    key_labels=perf_labels,
    key_notes={ROOT_WHITE[0]: "white root keys D4 .. C5 pick the mode", ROOT_WHITE[3]: "ARP: the selected mode, lit"},
    encoders={"KNOB1": ("RATE", "1/8"), "KNOB2": ("ORDER", "UP"), "KNOB3": ("RANGE", "1 OCT"), "KNOB4": ("GATE", "70%")},
    screen=scr({'kind': 'picker', 'items': ['Strum', 'Strum 2 Octaves', 'Slop', 'Arpeggiate', 'Arp 2 Octaves', 'Pattern', 'Harp'], 'sel': 3, 'label': 'perform', 'col': 'white', 'value': '1/8'}, foot="a root key: the mode · KNOB 3: rate", note="PERF held: one mode at a time, huge; the next and previous peek above and below; a change flips the words like a split-flap board."),
    chord_block=False,
))

# 7 ------------------------------------------------------------------------------- arp playing
S.append(state(
    "7 · ARP on, Am7 held: arpeggio playing",
    "PERF lit: the arpeggiator walks the voiced chord at 1/8 of the master BPM. The chord notes are lit "
    "and the note sounding now blinks (the keys show what the engine plays, as Felucca does). KNOB 3 rides "
    "the mode's main parameter (arp RATE) without opening the layer; the PERF card names it. The footer "
    "strip shows the beat.",
    held=["MIN", "m7"], lit=["A4", "C5", "G5"], blink=["E5"], buttons={"ARP": LIT},
    encoders=IDLE_ENC | {"KNOB3": ("ARP RATE", "1/8")},
    screen=scr({"kind": "arp", "root": "A", "quality": "m", "sup": "7", "cols": {"root": C_CHORD, "sup": C_BASS}, "notes": bubbles(["A4", "C5", "E5", "G5"], ext=["G5"]), "pos": 2, "hopCol": C_CHORD, "line": "arp 1/8"}, right="Arp", note="arpeggio playing: the sounding note sits on a white block and hops along the line in time; the dotted arc shows the next hop."),
))

# 8 ------------------------------------------------------------------------------- loop layer
slot_labels = {ROOT_WHITE[i]: str(i + 1) for i in range(10)}
slot_labels[ROOT_BLACK[0]] = "CLEAR"
slot_labels[ROOT_BLACK[1]] = "UNDO"
S.append(state(
    "8 · LOOP held: the loop layer",
    "Hold LOOP: the white root keys D4-F5 are the ten loop slots (lit = holds a loop, blinking = the one "
    "selected); D#4 is CLEAR (hold 1 s), F#4 UNDO the last layer. KNOB 1-4: SYNC (FREE, 1, 2, 4, 8, 16 bars "
    "— Orchid's loop waiting room), QUANTIZE, COUNT-IN, LOOP LEVEL. The screen shows the ring with the "
    "length inside.",
    keys=[OFF] * 27, blink=["D4"], lit=["E4", "F4"], buttons={"PLAY": BLINK},
    key_labels=slot_labels,
    key_notes={ROOT_WHITE[0]: "slot 1 selected (blinks); slots 2-3 hold loops (lit)", ROOT_BLACK[0]: "CLEAR (hold) · UNDO on the first black root keys"},
    encoders={"KNOB1": ("SYNC", "4 BAR"), "KNOB2": ("QUANTIZE", "1/16"), "KNOB3": ("COUNT-IN", "ON"), "KNOB4": ("LOOP LEVEL", "100")},
    screen=scr({'kind': 'picker', 'items': ['Free', '1 bar', '2 bars', '4 bars', '8 bars', '16 bars'], 'sel': 3, 'label': 'loop length', 'col': 'red'}, key="Loop", ring=0, note="LOOP held with no loop: the length, one at a time, inside the empty ring; SELECT or a root key changes it with a slide."),
    chord_block=False,
))

# 9 ------------------------------------------------------------------------------- recording
S.append(state(
    "9 · REC tapped: recording a 4-bar loop (bar 2 of 4)",
    "REC tapped: after the one-bar count-in the loop records for 4 bars at the master BPM (free mode: until "
    "REC or LOOP is tapped again). REC's red LED blinks, the header shows REC and bar.beat, the Orchid ring "
    "progress indicator runs around the edge of the screen in red. The chord view keeps naming what is played.",
    held=["MAJ"], lit=["F4", "A4", "C5"], buttons={"REC": BLINK, "PLAY": LIT},
    encoders=IDLE_ENC,
    screen=scr(chord("F", cols={"root": C_CHORD}, bubbles=bubbles(["F4", "A4", "C5"])), key="Rec", right="2.3", ring=0.35, ring_rec=True, note="recording: the red ring runs round the edge; 'Rec' blinks with the beat."),
))

# 10 ------------------------------------------------------------------------------ playing + overdub
S.append(state(
    "10 · loop playing, overdub armed",
    "The loop plays (PLAY's green LED, like Felucca's transport); REC is armed for overdub (lit), so the "
    "next notes record on top as a new layer (hold REC: undo it). The ring runs in the accent colour; the "
    "loop's notes glow on the root keys as they sound while a live chord is lit. Voicing, performance and "
    "bass apply live at playback: the loop stores the chords, not the notes.",
    held=["MAJ"], lit=["G4", "B4", "D5"], buttons={"PLAY": LIT, "REC": LIT}, play_green=1,
    key_notes={key_of("G4"): "a chord played live over the loop"},
    encoders=IDLE_ENC,
    screen=scr({'kind': 'picker', 'items': ['Overdub', 'Pause', 'Undo', 'Clear'], 'sel': 0, 'label': 'loop 1', 'col': 'red'}, key="Loop 1", ring=0.62, note="LOOP held while a loop plays: Overdub / Pause / Undo / Clear one at a time inside the ring; OCT+ does it."),
))

# 11 ------------------------------------------------------------------------------ options menu
S.append(state(
    "11 · OPT tapped: the Options menu",
    "Tap OPT: Orchid's Options menu as a list. SELECT scrolls (its printed name), KNOB 1 changes the "
    "selected value, OCT+ enters / confirms (blinks), OCT- goes back (lit), HOME leaves — Felucca's dialog "
    "convention. Entries: Play Style, Extension Addition, Bass Behaviour, Secret Chords, Single Notes, "
    "Velocity, Quantization, MIDI Channels, MIDI Clock, View, Metronome, Palette, LEDs, Version, Update.",
    buttons={"GLO": LIT, "OCT-": LIT, "OCT+": BLINK},
    button_notes={"GLO": "OPT lit while the menu is open; tap again, HOME or OCT- to leave"},
    encoders={"SELECT": ("SCROLL", ""), "KNOB1": ("VALUE", ""), "KNOB2": "", "KNOB3": "", "KNOB4": ""},
    screen=scr({'kind': 'picker', 'items': ['Battery', 'View', 'Audio Output', 'MIDI Channels', 'MIDI In', 'MIDI Out', 'MIDI Clock', 'Play Style', 'Extension Addition', 'Single Notes', 'Secret Chords', 'Quantization', 'Metronome', 'Velocity Sense', 'Motion', 'Palette', 'LEDs', 'Version', 'Upgrade firmware'], 'sel': 7, 'label': 'options · KNOB 1 sets', 'col': 'white', 'value': 'Simple'}, note="OPT tapped: one setting per screen — its name big, its value under it; SELECT slides to the next setting, KNOB 1 changes the value."),
))

# 12 ------------------------------------------------------------------------------ secret chord
S.append(state(
    "12 · Secret chord: DIM + SUS + D4 → D5 (power)",
    "Secret Chords (Options: Simple / All): two chord types held together select the extra types proved in "
    "the Orchid firmware: DIM+SUS = power chord (D⁵), MAJ+SUS = augmented (D+), MIN+SUS = [0 3 5] (Dm add4). "
    "Extensions still stack. The screen uses Orchid's own labels.",
    held=["DIM", "SUS"], lit=["D4", "A4", "D5"],
    encoders=IDLE_ENC,
    screen=scr(chord("D", sup="5", cols={"root": C_CHORD, "sup": C_LOOP}, bubbles=[{"t": "D4", "col": C_CHORD}, {"t": "A4", "col": C_CHORD}, {"t": "D5", "col": C_LOOP, "mark": True}]), note="a secret chord: the extra tone in red; the name flashes once."),
))

# 13 ------------------------------------------------------------------------------ bass on
S.append(state(
    "13 · BASS on (03 SUB), bass voicing -1",
    "BASS tapped: the bass part plays the chord root through its own engine and MIDI channel 2. ALGORITHM "
    "browses the bass sounds (Orchid's Bass Dial turn), KNOB 2 is the bass register (octaves). The footer "
    "names the bass sound; the BASS card its voicing.",
    held=["MAJ"], lit=["E4", "G#4", "B4"], buttons={"ENV": LIT},
    key_notes={key_of("E4"): "E major; the bass plays E1 (below the keybed)"},
    encoders=IDLE_ENC | {"KNOB2": "-1", "ALGORITHM": "03 SUB"},
    screen=scr({"kind": "meter", "value": "03", "sub": "SUB", "label": "bass", "col": C_BASS, "pct": 3 / 12, "segments": 12}, note="ALGORITHM turned: the bass sound's number huge in orange over a stripe meter of the bank; the stripes fill one by one."),
))

# 14 ------------------------------------------------------------------------------ bass layer
S.append(state(
    "14 · BASS held: the bass layer",
    "Hold BASS: KNOB 1-4 become BEHAVIOUR (Chords Only / Unison / Single Notes / Solo — Orchid's Bass "
    "Dial hold menu), REGISTER, SOUND, LEVEL (Orchid's Volume-hold bass volume). The root keys preview "
    "the bass sound alone.",
    keys=[OFF] * 27, buttons={"ENV": BLINK},
    encoders={"KNOB1": ("BEHAVIOUR", "CHORDS"), "KNOB2": ("REGISTER", "-1"), "KNOB3": ("SOUND", "03 SUB"), "KNOB4": ("LEVEL", "80")},
    screen=scr({'kind': 'picker', 'items': ['Chords Only', 'Unison Bass', 'Bass Single Notes', 'Solo'], 'sel': 0, 'label': 'bass', 'col': 'orange'}, foot="KNOBS: register · sound · level", note="BASS held: the bass behaviour, one at a time, in orange."),
    chord_block=False,
))

# 15 ------------------------------------------------------------------------------ fx layer
fx_names = ["REV", "DELAY", "CHOR", "PHASE", "DRIVE", "TREM", "FILT", "ENSMB"]
fx_labels = {ROOT_WHITE[i]: n for i, n in enumerate(fx_names)}
S.append(state(
    "15 · FX held: the fx layer",
    "Tap FX: the sound's main effect on/off (Orchid's FX Dial default). Hold FX: the white root keys pick "
    "which effect KNOB 4 rides (Orchid's FX push + turn), KNOB 1-3 its parameters, KNOB 4 the amount. "
    "OPT + FX locks the FX settings across sound changes (Orchid's FX lock).",
    keys=[OFF] * 27, lit=["D4"], buttons={"FX": BLINK},
    key_labels=fx_labels,
    encoders={"KNOB1": ("SIZE", "60"), "KNOB2": ("DAMP", "40"), "KNOB3": ("TYPE", "ROOM"), "KNOB4": ("AMOUNT", "25")},
    screen=scr({'kind': 'picker', 'items': ['Reverb', 'Chorus', 'Delay', 'Phaser', 'Drive', 'Tremolo', 'Filter'], 'sel': 0, 'label': 'fx · KNOB 4 amount', 'col': 'green', 'value': '05'}, foot="a root key: the effect · OPT+FX: lock", note="FX held: the effect one at a time in green with its amount under it."),
    chord_block=False,
))

# 16 ------------------------------------------------------------------------------ geek out view
S.append(state(
    "16 · GEEK OUT view (HOME tapped again)",
    "HOME tapped on the view cycles Orchid's View modes: CHORD (default), KEYBOARD, NOTES, GEEK OUT and SCOPE (React). "
    "Geek Out shows the chord, every note, the status lines and the 27-key strip with the sounding notes.",
    held=["MIN", "m7"], lit=["D4", "F4", "A4", "C5"], buttons={"EDIT": LIT},
    encoders=IDLE_ENC | {"KNOB1": "+1"},
    screen=scr({"kind": "geek", "root": "D", "quality": "m", "sup": "7", "cols": {"root": C_CHORD, "sup": C_BASS}, "notes": ["D4", "F4", "A4", "C5"], "lit": [{"k": "D4", "col": C_CHORD}, {"k": "F4", "col": C_CHORD}, {"k": "A4", "col": C_CHORD}, {"k": "C5", "col": C_BASS}], "lines": ["voicing +1 · simple", "bass off · 120 bpm"]}, key="Key: D minor", right="Trans +0", note="Geek Out: the one dense view, by request."),
))

# 17 ------------------------------------------------------------------------------ sound browse popup
S.append(state(
    "17 · PRESETS turned: browsing sounds",
    "Turning PRESETS pops the sound list over the panel (as the stock firmware and Felucca do), the sound "
    "loads at once and the popup fades after a second. Sounds come from Felucca's engines (FM6, ANALOG, "
    "WHEEL, PHYS…) with ChoralRoot's own bank of chord-friendly presets; bass sounds are a separate bank "
    "on ALGORITHM. SAVE stores an edited sound to a user slot.",
    held=["MAJ"], lit=["E4", "G#4", "B4"],
    encoders=IDLE_ENC | {"PRESETS": "13 EP", "KNOB4": "40"},
    screen=scr({"kind": "meter", "value": "13", "sub": "EP", "label": "sound", "col": C_CHORD, "pct": 13 / 48, "segments": 16}, note="PRESETS turned: the sound's number huge, its name under it, the stripe meter is the place in the bank."),
))

# 18 ------------------------------------------------------------------------------ panic
S.append(state(
    "18 · OCT- + OCT+ together: PANIC",
    "Press OCT- and OCT+ together (a chord nothing else uses; the stock firmware resets the octave with it): "
    "every scheduler stops, every note gets its note-off, MIDI CC 123 goes out on the three channels, the octave "
    "resets, the LEDs flash once and the screen says so. Settings and loops are kept.",
    keys=[OFF] * 27, buttons={b: OFF for b in BTN_IDS} | {"OCT-": LIT, "OCT+": LIT},
    encoders=IDLE_ENC,
    screen=scr({"kind": "big", "value": "PANIC", "label": "all notes off", "block": "red", "size": 64}, note="panic: the whole screen goes red with black type, shakes once, and fades back."),
))

# 19 ------------------------------------------------------------------------------ fx amount popup
S.append(state(
    "19 · KNOB 4 turned: Reverb 05",
    "Any knob turn shows its value big for a second, then the view returns (Orchid's dial screens). Levels "
    "and amounts draw as an inverted fill rising with the value — Orchid's Bass Volume screen.",
    held=["MAJ"], lit=["D4", "F#4", "A4"],
    encoders=IDLE_ENC | {"KNOB4": "05"},
    screen=scr({"kind": "meter", "value": "05", "label": "reverb", "col": C_FX, "pct": 0.25, "segments": 12}, note="KNOB 4 turned: the amount in green over its stripe meter; back to the view a second later."),
))

# 20 ------------------------------------------------------------------------------ bpm popup
S.append(state(
    "20 · SELECT turned: 120 BPM",
    "SELECT is the tempo: turning it shows the BPM huge (Orchid's BPM dial screen), METRO taps the "
    "metronome on and off.",
    encoders=IDLE_ENC | {"SELECT": "120"},
    screen=scr({"kind": "meter", "value": "120", "label": "bpm", "col": C_TEMPO, "pct": (120 - 20) / 280, "segments": 14}, note="SELECT turned: the tempo huge; the stripes pulse at the beat."),
))

# 21 ------------------------------------------------------------------------------ sound edit page
S.append(state(
    "21 · EDIT tapped: the sound's pages (ENV)",
    "Any sound can be opened and edited on the device: EDIT opens the chord sound's pages (BASS held + EDIT: the "
    "bass sound's). SELECT turns the pages — ENGINE, the engine's own parameters (two pages), ENV, LFO, FILTER / "
    "MOD, FX sends, MIX — and KNOB 1-4 edit the four parameters on each, drawn as glyphs in the knobs' colours "
    "(blue, orange, cream, coral). HOME returns to the view. Deep FM6 patches still have the web editor.",
    held=["MAJ"], lit=["D4", "F#4", "A4"], buttons={"SEL": LIT},
    encoders={"SELECT": ("PAGE", "ENV"), "KNOB1": ("ATTACK", "0.20"), "KNOB2": ("DECAY", "0.30"), "KNOB3": ("SUSTAIN", "60"), "KNOB4": ("RELEASE", "0.80")},
    screen=scr({"kind": "params", "title": "13 EP", "page": "ENV 4/8", "col": C_CHORD,
                "cols": [{"label": "Attack", "value": "0.20", "glyph": "env", "env": [0.2, 0.3, 0.6, 0.8]}, {"label": "Decay", "value": "0.30", "glyph": "knob", "pct": 0.3},
                         {"label": "Sustain", "value": "60", "glyph": "bar", "pct": 0.6}, {"label": "Release", "value": "0.80", "glyph": "knob", "pct": 0.8}],
                "foot": "SELECT: page · HOME: done · SAVE: keep it"},
               note="EDIT: a page of four parameters in the knob colours; the glyph of the one being turned animates (the envelope redraws as the knob moves)."),
))

# 22 ------------------------------------------------------------------------------ engine picker
engine_labels = {ROOT_WHITE[i]: n for i, n in enumerate(["ANLG", "FM6", "PHASE", "LOFI", "SMPL", "VOICE", "TRIO", "WHEEL", "GRAIN", "PHYS", "NOISE"])}
S.append(state(
    "22 · EDIT held: the engine picker",
    "Hold EDIT: the white root keys are Felucca's engines (ANALOG, FM6, PHASE, LOFI, SAMPLE, VOICE, TRIO, WHEEL, "
    "GRAIN, PHYS, NOISE); press one and the sound switches engine, keeping its envelope and sends. KNOB 1 steps "
    "through the engine's factory presets, KNOB 2 inits the sound.",
    keys=[OFF] * 27, lit=["E4"], buttons={"SEL": BLINK},
    key_labels=engine_labels,
    encoders={"KNOB1": ("PRESET", "13 EP"), "KNOB2": ("INIT", ""), "KNOB3": "", "KNOB4": ""},
    screen=scr({'kind': 'picker', 'items': ['Analog', 'FM6', 'Phase', 'LoFi', 'Sample', 'Voice', 'Trio', 'Wheel', 'Grain', 'Phys', 'Noise'], 'sel': 1, 'label': 'engine · KNOB 1 its presets', 'col': 'white', 'value': '13 EP'}, foot="a root key: the engine · KNOB 1: its presets", note="EDIT held: the engine one at a time; the sound changes as the name flips."),
    chord_block=False,
))

# 23 ------------------------------------------------------------------------------ save a sound
name_labels = {ROOT_WHITE[i]: c for i, c in enumerate(list("ABCDEFGHIJK"))}
S.append(state(
    "23 · SAVE tapped: name and save the sound",
    "SAVE while a sound is edited: pick a user slot with KNOB 1, name it with the keys (Felucca's naming: the keys "
    "type letters, OCT- deletes, OCT+ confirms), and the sound lands in one of 32 user slots, listed after the "
    "factory bank on PRESETS. SAVE held: the loop's save / load / delete.",
    keys=[OFF] * 27, buttons={"SAVE": LIT, "OCT-": LIT, "OCT+": BLINK},
    key_labels=name_labels,
    encoders={"KNOB1": ("SLOT", "U03"), "KNOB2": "", "KNOB3": "", "KNOB4": ""},
    screen=scr({"kind": "text", "title": "save sound", "col": C_CHORD, "lines": [{"t": "U03", "px": 15, "col": "grey"}, {"t": "WARM EP_", "px": 36, "w": 700, "col": "white"}, "", {"t": "keys: letters · OCT-: delete", "col": "grey"}, {"t": "OCT+: save", "col": "grey"}]}, foot="SAVE again: cancel", note="SAVE: the slot and the name being typed; each letter slides in, the cursor blinks."),
    chord_block=False,
))

# 24 ------------------------------------------------------------------------------ the squeeze
S.append(state(
    "24 · a chord change, mid-squeeze (Dmaj7 → Em, frame 2 of 6)",
    "Orchid's trick, kept and played up: when the chord changes, the old name squeezes to a thin condensed column "
    "and the new one stretches out from it (about 120 ms). Long names stay condensed to fit the width, as on the "
    "Orchid's own screen. This frame shows the new name half-way out.",
    held=["MIN"], lit=["E4", "G4", "B4"], buttons={"EDIT": LIT},
    encoders=IDLE_ENC,
    screen=scr(chord("E", quality="m", cols={"root": C_CHORD}, squeeze=0.45, bubbles=bubbles(["E4", "G4", "B4"])), key="Key: C", note="one frame of the squeeze: the name at 45 % width on its way to full."),
))

design = {
    "format": "fm1-panel-design", "version": 1, "device": "M-VAVE FM-1",
    "name": "ChoralRoot FM-1 mockups",
    "palette": "MOD",
    "labels": {"buttons": BUTTON_LABELS, "encoders": ENCODER_LABELS},
    "notes": "ChoralRoot on the M-VAVE FM-1: a Telepathic Orchid-style chord instrument with Felucca's sound engines. "
             "The Orchid's 2x4 chord block is the left of the keybed: DIM MIN MAJ SUS on the black keys F#3 G#3 A#3 C#4, "
             "6 m7 M7 9 on the white keys F3 G3 A3 C4 (B3 unused); the 18 keys D4-G5 are the roots. The function buttons "
             "carry the Orchid dials' button functions — KEY, PERF, FX, BASS, LATCH, OPT / HOME, SAVE, METRO, LOOP, REC, VIEW "
             "(tap = the action, hold = a layer where the root keys and KNOB 1-4 change job and the screen shows how — "
             "Felucca's grammar). KNOB 1 is always chord voicing, KNOB 2 bass voicing, KNOB 3 the performance parameter, "
             "KNOB 4 the FX amount; PRESETS browses sounds, ALGORITHM the bass sounds, SELECT is the BPM, MASTER the volume. "
             "OCT-/OCT+ shift the root keys (and are back / OK in menus). States 1-18 walk through idle, chords, voicing, "
             "Key Mode and its layer, the perform layer, an arpeggio, the loop layer, recording, playback, the Options menu, "
             "a secret chord, bass and its layer, the fx layer, the Geek Out view, sound browsing, panic, two dial pop-ups, the sound edit pages, the engine picker and saving a sound. "
             "Screens follow Orchid's rule — one big thing per screen — in a 1960s mod language: black, white, roundel red and "
             "blue, pop yellow, orange and green; flat blocks, stripes and targets; heavy type that squeezes to fit as on the "
             "Orchid. Roles: white chords and sounds, blue voicing, orange bass and 7ths, red loop / REC / secret tones, yellow "
             "Key Mode, green FX. No menus and no circled numbers: a choice is one item at a time with its neighbours peeking "
             "(a picker), a knob value is a huge number over a stripe meter, the idle screen is the name over mod racing stripes. Every state notes "
             "its motion.",
    "states": S,
}

with open(OUT, "w") as f:
    json.dump(design, f, indent=1)
print("wrote", OUT, "with", len(S), "states")
