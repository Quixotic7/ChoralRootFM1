# ChoralRoot FM-1 — build plan

**What:** a Telepathic Orchid-style chord instrument as a firmware for the M-VAVE FM-1: one hand plays
roots, the other shapes chords; voicing, Key Mode, performance modes, bass, looper — with the FM-1's
own sound engines and a screen, plus the three-channel MIDI output that the grid version
(`choralroot_fullsource/`) already gets right.

**Status (2026-10-06):** the device build (`build/choralroot.fwsc`, `FM-1_920`, `ChoralRoot 0.1`) is flashed
and playing. Done: the chord engine (`cr_engine.c`, host tests green), the screens (`cr_draw.c` / `cr_gfx.c`, the
mod language, the motion pass), the looper (`cr_loop.c`), settings persistence (`cr_settings.c`), MIDI in / out and
clock, the sound bank with user slots and naming, and the VA engine (`eng_va.c`, deep pages, `va_store.c`). Being
built now: the new interaction grammar (layers lock on hold, LOCK on B3; §3–§4) and the dense sound editor
(`cr_edit.c`, [`docs/EDITOR.md`](docs/EDITOR.md)), both approved in the mock-ups below. Open in M7: the bank by
ear, the sticker sheet, the installer page, the README / manual (§8).

| Deliverable | Where |
| --- | --- |
| this plan | `PLAN.md` |
| interface mock-ups (24 states, LEDs + screens) | `design/choralroot-fm1-mockups.json`, `design/choralroot-fm1-screens.png`, `design/choralroot-fm1-panels.png`; generator `design/make_mockups.py` |
| sound editor mock-ups (16 states) | `design/choralroot-fm1-sound-editor-mockups.json`, `design/choralroot-fm1-sound-editor-screens.png`, `design/choralroot-fm1-sound-editor-panels.png`; generator `design/make_editor_mockups.py` |
| the sound editor spec | `docs/EDITOR.md` |
| how it is built | `docs/INTEGRATION.md` (glue, status, performance), `docs/VA.md`, `docs/LOOPER.md`, `docs/SETTINGS.md`, `firmware/src/CR_ENGINE.md`, `firmware/src/CR_SCREENS.md` |
| the designer tool that renders the mock-ups | `../ChoralRootFM1Designer/` (`index.html`, `FORMAT.md`) |

## 1. Starting points

| Source | What we take from it |
| --- | --- |
| **Felucca** (`../Felucca`, GPL-3.0-only, hugelton) | the whole FM-1 platform: HAL (`firmware/hal/*.h`: key/LED matrix at 10 kHz, 7 encoders with detent decoding, LCD SPI, audio I²S, flash, USB), `lcd.c`/`gfx.c` (240×240 strip renderer, Inter Tight fonts, Fukiai icons, 8 palettes), 13 sound engines + voice allocator + FX (`engines.c`, `voice.c`, `fx.c`), USB MIDI in/out + TRS MIDI in + MIDI clock, flash settings/presets/projects, the OTA update loader and web installer, the host test harness (`tests/hostsim.c`, `ui_render.c` renders every screen to PNG on the build machine), the build (`tools/build.py`, JieLi toolchain in Docker) |
| **sloop-fm1** (`../sloop-fm1`, Felucca fork) | the interaction idiom we adopt: *hold a button, touch a key* — every function button is a layer while held, the 16 white keys and KNOB 1–4 change job, the screen shows tiles + dials; lock a layer with HOME; tap = pages (ChoralRoot locks a layer by the hold itself, §4). Also its splash, recovery and "saves retried" robustness work |
| **choralroot (grid iii)** (`../choralroot_fullsource`) | the musical engine, already Orchid-correct for MIDI: `d_cr_engine.lua` (chord tables incl. the firmware-verified secret chords, `KEYMAP` harmonic quantization, voicing rotation, Simple/Advanced/Free state machine, Extension Addition, strum/slop/arp/pattern/harp scheduler with BPM rephasing, bass, note-ownership registry, panic), `d_cr_loop.lua` (semantic event looper with layers/undo), `design.md` (the normative behaviour spec, §5–§17, §20) and `dev/test_choralroot.lua` (426 assertions to port as C tests) |
| **Orchid references** (`telepathic orchid reference/`) | the manual (dial gestures, Options menu, View modes, loop waiting room, Standard Chord Naming Framework), `ORCHID_FIRMWARE_REFERENCE.md` (chord/extension interval tables, secret chord contents), the UI screenshots (big chord name, "Key: C" badge, list menus, big-number dial screens, loop ring) |
| **OMX-LED-Designer** | the designer tool pattern (now adapted as `ChoralRootFM1Designer`) |

Licensing: a Felucca fork is **GPL-3.0-only**, and `ChoralRootFM1/LICENSE` is GPL-3.0 (Felucca's
`LICENSES/` folder for the font, icons and ported DSP travels with the fork). The designer stays MIT.

## 2. What changes from the grid version

| | grid iii choralroot | ChoralRoot FM-1 |
| --- | --- | --- |
| surface | 16×8 grid, 12 note columns × 8 velocity rows, 4-column control strip | 27-key keybed (no velocity): 8 chord keys + 18 root keys D4–G5; 12 + 2 buttons; 7 encoders + pot; 240×240 screen |
| chord buttons | grid keys | the left of the keybed: DIM MIN MAJ SUS on the black keys F#3 G#3 A#3 C#4, 6 m7 M7 9 on the white keys F3 G3 A3 C4 (B3 = LOCK, the latch toggle) — Orchid's 2×4 block, two rows |
| voicing | two grid keys with repeat | an endless encoder (KNOB 1), like Orchid's dial |
| sound | MIDI only | internal engines (Felucca) **and** MIDI out on 3 channels |
| menus / parameters | hold-to-reveal on the grid's top row | layers that lock open on a hold (pickers and meters), Options one setting per screen, a dense sound editor |
| display | LED levels | chord name, notes, keyboard strip, ring, Orchid's View modes |
| loop display | column fill while Loop is held | the ring progress indicator around the screen while recording; a corner dial in the top line while it plays |
| velocity | 8 rows | fixed (Options > Velocity); patterns keep their accents |

## 3. Control mapping (normative; `design/make_mockups.py` draws it)

### The keybed

| Keys | ChoralRoot |
| --- | --- |
| F#3 G#3 A#3 C#4 (black) | **DIM MIN MAJ SUS** — Orchid's top row, momentary chord types |
| F3 G3 A3 C4 (white) | **6 m7 M7 9** — Orchid's bottom row, momentary, stackable extensions |
| B3 | **LOCK** — a mode toggle for the chord block: tap = on (B3 lit, `lock` top right on the screen), tap again = off (the chord keys momentary as usual). With LOCK on, the chord keys held are **latched when released**, so one hand plays the roots. Once no chord key is held, a **top-row key resets** the latch to that type alone (extensions cleared) and a **bottom-row key toggles** its extension in the latched set without touching the type. Example: hold MIN, release: minor; 6: m6; 9: m6/9; 9 again: m6; MAJ: major, extensions cleared. Latched keys are lit; panic clears the latch |
| D4 … G5 (18 keys) | **roots**: a single note alone, a chord with a type held (or latched); OCT−/OCT+ shift them by octaves (−2…+2) |

Two chord types held together = a Secret Chord when Options allows it (DIM+SUS power, MAJ+SUS
augmented, MIN+SUS [0 3 5]), as on the grid; with LOCK on, the pair latches like any held chord keys. In
layers the root keys become the layer's map (tonic, mode, slot, effect) while the chord keys keep their job (hold
MIN with a tonic = minor key).

### Buttons (the Orchid dials' button functions)

The rule for every layer button: **a hold past 300 ms opens its layer and it stays open** after release (the button
blinks); **OCT− or HOME closes it**; holding another layer button switches to that layer; a **tap**, inside the
layer or out, is always the button's on/off action.

| Printed | ChoralRoot | Tap | Hold (opens and stays; OCT− / HOME closes) |
| --- | --- | --- | --- |
| SEL | **KEY** | Key Mode on/off (LED lit when on) | key layer, a **knob row** whose band is the keyboard (the tonic lit yellow): root keys = tonic (MIN held too = minor key), KNOB 1–4 = TONIC / SCALE / TRANSPOSE (shift glyph) / SINGLE NOTES as cells, no popups, the turned cell hot |
| ARP | **PERF** | performance on/off | perform layer: SELECT picks STRUM, STRUM 2, SLOP, ARP, ARP 2, PATTERN, HARP (picked = on); the root keys play the chord, so a mode is heard at once (their LEDs the chord's); KNOB 1–4 = the mode's parameters, a **knob row** (the mode on top, its four parameters as cells: rate / division echoes, direction arrow, range, gate, slop amount bar; no popup, the turned cell hot) |
| FX | **FX** | the sound's main effect on/off | fx layer, a **knob row**: SELECT picks the effect (the picker on top; the root keys play the chord), KNOB 1–3 its parameters, KNOB 4 the amount, all four shown as cells with their glyphs under it (no popup; the turned cell hot) |
| ENV | **BASS** | bass on/off | bass layer, a **knob row**: KNOB 1–4 = BEHAVIOUR (Chords Only / Unison / Single Notes / Solo; text) / REGISTER (shift glyph) / SOUND (text) / LEVEL (bar), no popups, the turned cell hot; root keys preview the bass; **BASS held + EDIT** = the bass sound's editor |
| LFO | **LATCH** | Sticky keys / arp hold on/off | — |
| GLO | **OPT** | Options menu open/close | held + a knob = that knob's second function (Orchid's press+turn): KNOB 1 split point, SELECT metronome level, ALGORITHM bass volume, **PRESETS the chord part's engine** (a horizontal picker while OPT is held; on release the part lands on its last place in that engine's pool, docs/PRESETS.md). In the editor: SHIFT |
| HOME | **HOME** | back to the view from any page, menu, layer or the editor; tapped again on the view: next View (CHORD / KEYBOARD / NOTES / GEEK OUT / SCOPE) | — |
| SAVE | **SAVE** | the save dialog (docs/PRESETS.md): **Overwrite** the current preset (saved at once, its name kept; a factory preset is overwritten by a user record bound to it) or **Save as new** (the pool's next place, then the name typed with the keys); OCT+ / SAVE takes it, OCT− cancels; SAVE held 1 s in the dialog: reset to factory / delete; the same dialog in the editor | save / load / delete loops (Orchid's Loop long press; a plain picker, no ring) |
| SEQ | **METRO** | metronome / beat on-off (Orchid's BPM press) | metronome layer, a **knob row**: the time signature (4/4 3/4 6/8) over CLICK (KNOB 1, bar) and three empty cells; no popup |
| PLAY | **LOOP** | play / stop the loop (green LED = playing) | loop layer, a **knob row** with no ring: white root keys = slots 1–10, D#4 = CLEAR (hold 1 s), F#4 = UNDO; the length picker over SYNC (range) / QUANTIZE (echoes) / COUNT-IN (gate) / LEVEL (bar), KNOB 1 = the picker, no popups; while playing, the Overdub / Pause / Undo / Clear picker (OCT+ does it), SYNC dim, the corner dial in the top line |
| REC | **REC** | record (count-in, then the sync length) / overdub arm; red LED blinks while recording | undo the last layer |
| EDIT | **EDIT** | the **sound editor** on the chord sound (EDIT blinks; EDIT tap or HOME leaves; [`docs/EDITOR.md`](docs/EDITOR.md)) | the engine picker (locked open like a layer) |
| OCT− / OCT+ | octave of the root keys | in layers, menus and dialogs: **back (closes the layer) / OK** (Felucca's convention; OCT+ blinks when it would do something) | **both together: PANIC** (all notes off on every stream, CC 123 on the three channels, octave reset, LOCK latch cleared) |

The printed SEL and EDIT swap jobs from earlier passes: SEL is KEY, EDIT is EDIT (the editor is on the button that
says so).

### Rotaries (no push switches on the FM-1)

| Printed | ChoralRoot | Turn | OPT held + turn |
| --- | --- | --- | --- |
| MASTER | VOLUME | the pot | — |
| SELECT | BPM | tempo 20–300 (in menus and pickers: scroll; in the editor: what the section's tap steps) | metronome volume |
| PRESETS | SOUND | the chord part's engine's **pool** (docs/PRESETS.md): 00 INIT, the factory presets, the user's; wraps, loads at once, popup "05 / FM PAD / FM6 · 05/26" | OPT held: the engine |
| ALGORITHM | BASS SOUND | the bass part's engine's pool, 00 = OFF before it (Orchid's Bass Dial turn); the bass engine: BASS held + EDIT, the editor's EDIT-held picker | bass volume |
| KNOB 1 | **VOICING** | Orchid's Chord Voicing: lowest note up an octave / highest down, one click = one note; **never reassigned** outside the layers and the editor | single-note split point |
| KNOB 2 | BASS VOICE | bass register, octaves | — |
| KNOB 3 | PERFORM | the selected mode's main parameter (strum speed, slop amount, arp rate, pattern number, harp speed) | — |
| KNOB 4 | FX | amount of the selected effect | — |

A knob turn shows its value big on the screen for a second (Orchid's dial screens, §5); nothing
is displayed permanently for the knobs. In a layer KNOB 1–4 are the layer's; in the editor they edit the active row
(OPT = SHIFT: one unit a detent).

## 4. Interaction grammar

- **Tap = use, hold = open (and it stays), OCT− / HOME = close.** The hold threshold is 300 ms (Felucca
  `HOLD_MS`; Options > Hold Time). A tap is the button's action, always — inside its own layer too. A hold of KEY / PERF / FX / BASS /
  LOOP / METRO opens the layer and **locks it**: it stays after release, the button blinks, until OCT− (back) or HOME
  closes it; holding another layer button switches straight to that layer. A key or knob touched while a button is
  still down makes it a combo (no tap action): OPT + knob, BASS + EDIT, OCT− + OCT+. There is no separate lock
  gesture any more (no HOME + hold, no OPT + FX / PERF, no OPT + KNOB 3).
- **Layers** (sloop's idiom, made to stay): the root keys and KNOB 1–4 are the layer's, the screen shows the layer's
  picker or meter, the footer shows keycap hints (`a root: the mode · OCT-: back · HOME: home`); the chord keys keep
  their job. **The knob row** (the layer grammar, 2026-10-07; all layers: FX, PERF, BASS, KEY, LOOP, METRO): the picker in the
  upper part of the panel and under it one row of four cells, one per knob, in the knob colours (blue, orange, white,
  green) with a glyph that pictures the value; a turned knob makes its cell hot for 800 ms instead of a popup.
- **The engine picker locks the same way**: EDIT held = the white root keys are the engines in the firmware's order
  (ANALOG, FM6, VA, PHASE, LOFI, SAMPLE, VOICE, TRIO, WHEEL, GRAIN, PHYS; NOISE on SELECT), a root switches the
  sound's engine keeping its envelope and sends, KNOB 1 steps the engine's factory presets, KNOB 2 inits the sound;
  it stays after release (EDIT blinks); EDIT tap, OCT− or HOME closes it (back to the editor view when opened from
  there).
- **Menus**: OPT tap opens the Options (one setting per screen); SELECT scrolls, KNOB 1 sets, OCT− back, OCT+
  enters, HOME or OPT leaves.
- **Popups** (stock FM-1 / Felucca): turning PRESETS or ALGORITHM shows the place in the engine's pool for a second; turning SELECT shows
  the BPM meter; a knob turn shows its meter briefly.
- **Panic**: OCT− + OCT+ pressed together — a chord nothing else uses, so it can never fire while OPT
  is held as a shift; LEDs flash, screen message, CC 123 on all three channels, octave reset, LOCK latch cleared,
  loops and settings kept.
- **Sound editing**: EDIT tap opens the dense sound editor on the chord sound (BASS held + EDIT: the bass sound;
  SHIFT + EDIT inside switches part). The function buttons become seven sections — OSC FILT ENV LFO on the top row
  (the signal flow), MOD FX MIX on the bottom (the output stage) — each one view on the full screen; a section tap
  steps the active row, a hold swaps to its B bank, KNOB 1–4 edit the active row, OPT is SHIFT (fine steps). EDIT
  tap or HOME leaves; every section remembers its row and bank. SAVE stores the result (docs/PRESETS.md): Overwrite
  (over the current preset, a factory one included: a user record bound to it, resettable) or Save as new (the
  engine's pool's next place, named with the keys); each user preset is one of the 32 user slots. The whole
  spec: [`docs/EDITOR.md`](docs/EDITOR.md). Felucca's web editor stays for deep FM6 patches, backup and restore.
- **MIDI**: USB MIDI in and out and TRS MIDI in (Felucca's `usb.c`, `midi_uart.c`; ChoralRoot's `cr_midi.c`). Out:
  the three streams on channels 1 / 2 / 3 (each on/off in Options), 24-PPQN clock out, start/stop with the loop.
  In: notes on the chord channel play the chord part directly (Orchid's behaviour: no chord
  generation), the bass channel the bass part, CC for the FX amounts, program change for sounds,
  clock in syncs the tempo (Options > MIDI Clock: OUT / IN / OFF), SysEx for the web editor and
  firmware updates.
- **Options** (mock-up state 12, one setting per screen): Play Style (Simple / Advanced / Free), Extension Addition,
  Secret Chords, Velocity, Bass Behaviour, Single Notes, Split Point, MIDI Perform / Bass / Raw Chord channels, Raw
  Chord Sound, MIDI Clock, View, Motion, LEDs, Hold Time, Version, Calibrate. The metronome and the loop's
  quantization live in their layers (METRO, LOOP); the palette is MOD.

## 5. Screen

Orchid's rule, kept: **one big thing per screen.** The language is **1960s mod** — the roundel, bold
stripes and flat colour blocks, heavy grotesk type, black and white with red, blue, yellow, orange and
green — clean and high-contrast, in the spirit of the references in `../UI Inspiration` and copied
from none of them. ChoralRoot's `cr_draw.c` / `cr_gfx.c` draw it on Felucca's `gfx.c` (240×240, Inter Tight; the
`MOD` palette added to its eight). Two rules from the user: **no menus** (no multiple-choice lists) and **no circled
numbers**.

### Colour

| colour | owns |
| --- | --- |
| **white** | the chord name and its triad notes, sounds, the Perform picker, tempo |
| **blue** | KNOB 1 voicing |
| **orange** | the bass (ALGORITHM, KNOB 2, BASS) and 7th extensions |
| **red** | the loop and REC (the ring), secret-chord tones, panic |
| **yellow** | Key Mode (`Key: C`, select-key) |
| **green** | FX (KNOB 4, the FX picker) |

A thing on screen is the colour of the control that moves it. In the editor the four knob columns are blue,
orange, white, green (KNOB 1–4) whatever they edit.

### Screens

- **Idle**: the name over three racing stripes in the coralroot orchid's colours (red, orange,
  white) that slide sideways at the BPM. The first chord sweeps the stripes off the screen and the chord name lands where the title
  was. No instructions anywhere.
- **Chord** (the default View): the name fills the screen in the Orchid Standard Chord Naming
  Framework, **squeezed horizontally to fit** as the Orchid's own screen does with long names; the
  extension superscript in its colour; the voiced notes as a plain line of text under it, extensions
  coloured with a block under them. `Key: C` top-left in yellow when Key Mode is on; `lock` top-right when LOCK is
  on; a status top-right.
- **The squeeze**: on a chord change the old name squeezes to a thin column and the new one stretches
  out from it (~120 ms) — the Orchid's squeeze, played up.
- **Layers: the knob row** (`knobrow`, design/choralroot-fm1-fx-screens.png): the layer's picker as a band (the
  choice big in the layer's colour, its neighbours left and right, the square marks) over one row of four knob cells
  in the knob colours (label, a 48 × 36 glyph, the value, a 2 px bar); a missing parameter is a dim dash; the cell
  just turned sits on a hot block (no popup). The glyphs are pictures of the parameter that change with it: a room
  (reverb size: the far wall recedes), a moon (damping / tone), echoes (delay time and feedback), an LFO wave (chorus
  rate and depth), a clipped sine (drive), a coil (spring reverb), dry / wet squares (the amount); gate, range, arrow
  and shift picture the other layers' values. Used by every layer: FX, PERF, BASS; KEY (the band is the keyboard with
  the tonic lit yellow, over Tonic / Scale / Transpose / Single); LOOP (stopped: the length picker, playing: Overdub /
  Pause / Undo / Clear, over Sync (dim while playing) / Quantize / Count-in / Level; no ring); METRO (the time
  signature over Click and three empty cells). SAVE held (save / load / delete a loop) stays a plain picker, no ring.
- **Pickers** replace every list: one choice at a time, huge, its neighbours peeking small and faded
  above and below (left and right inside the ring), square position marks, the value under it. SELECT
  or the root keys move it with a split-flap flip; OCT+ confirms, OCT− backs out. Used for Perform,
  FX, bass behaviour, the loop length and the loop's Overdub / Pause / Undo / Clear, the engine
  picker, and **Options: one setting per screen** (its name big, its value under it, KNOB 1 sets).
- **Meters** replace dials: a knob's value huge in its colour over a stripe meter of bold blocks that
  fill one by one (`03 SUB / bass`, `13 EP / sound`, `05 / reverb`; SELECT's `120 / bpm` the same way).
- **Perform in motion**: the chord's notes as text on a line; the sounding one sits on a colour block
  and hops along in time, a dotted arc to the next.
- **Select key**: KEY held shows the keyboard with the tonic lit yellow as the knob row's band.
- **The ring**: Orchid's progress ring as a dotted circle round the edge — red, where the loop is the subject
  (recording, overdubbing, the count-in, undo; calibration's progress in yellow). Everywhere else a playing loop is
  the 16 px corner dial in the top line, the LOOP and SAVE layers included (`docs/LOOPER.md`).
- **The sound editor** ([`docs/EDITOR.md`](docs/EDITOR.md)) is the one dense place, and it takes the whole screen:
  **no header bar and no footer**. Its top line is the sound's name (`*` once edited, ` · BASS` in orange for the
  bass part) and, at the right, the section and what is on the knobs (`OSC 2 · A`, `ENV 2 · filter`, `MOD 3`). Two
  screen kinds:
  - **`edit8`**: one section's up to eight parameters as two rows of four cells (one per knob), with a wide
    shape over them where there is one — the filter response, the AHDSR envelope — that redraws as its knob turns.
    The active row (on KNOB 1–4) is in the knob colours with a bar under each cell; the other row grey. Used for
    FILT, each ENV, FX, MIX (and the one-row pages of engines without deep pages).
  - **`stack`**: the section's instances as equal rows of four cells under column headings — the four
    oscillators (a wave glyph per row), the four LFOs, the eight matrix slots (text only, a bipolar amount bar). The
    active row is in the knob colours with the knob bars; the rest grey. Used for OSC, LFO, MOD.
- **Panic**: the whole screen goes red with black type.
- **Views** (Options > View, HOME taps): CHORD (default), KEYBOARD, NOTES, GEEK OUT (the dense status screen),
  SCOPE.
- A thin footer line of keycap hints appears only in layers (never in the editor).

### Motion (for fun's sake, never in the way)

Every screen change is animated, every knob move answers on screen; all short (100–250 ms), never
delaying sound or input. `cr_anim.c` (a fixed-point ease-out / spring on Felucca's frame tick)
implements it once:

- chord names **squeeze and stretch** on every change; the notes line **reshuffles** on a voicing
  click (the lowest slides to the end);
- pickers **flip like a split-flap board**; meters **fill stripe by stripe**; a meter's number
  **springs** in;
- the idle stripes **slide** at the BPM and are **swept off** by the first chord; the ring **draws itself**
  and **pulses** on the downbeat; the keyboard **slides up** for select-key;
- a secret chord **flashes** its name; panic **shakes** the red screen;
- in the editor: rows **swap with a slide** and the active bar **slides** between rows / instances; a bank swap
  **slides the columns sideways**; the wide envelope / filter **redraws** as its knob turns; the turned cell's
  value sits on a hot block in its colour.

Motion is a setting (Options > Motion: full / calm / off).

- The 24 mock-up states (`design/choralroot-fm1-screens.png`): idle stripes · Dmaj7 · LOCK on, Dm6/9 latched ·
  voicing +2 · Key Mode Em · key layer (select key) · perform layer (picker) · arp in motion · loop layer (loop length)
  · recording · loop playing, overdub picker · Options · secret chord · bass meter · bass layer (behaviour picker) ·
  fx layer (picker) · Geek Out · sound meter · panic · reverb meter (SELECT's BPM meter the same way) · the sound
  editor (OSC stack) · the engine picker · saving a sound · a chord change mid-squeeze. The editor's own 16 states
  are in `design/choralroot-fm1-sound-editor-screens.png`. Each state's note says how it moves.

## 6. LEDs

Felucca's rules: every key and button glows dim at rest (the panel is findable in the dark; Options >
LEDs can invert to the stock look); lit = held, sounding, active; blinking = an open layer, LOOP
recording, OCT+ when it would act. Specifically:

- root keys: the **voiced chord notes light where they sound** (Orchid's chord display, as the grid
  version does); in Key Mode the black root keys outside the scale go dark; a performance's current
  note blinks; loop notes glow dim; in layers the root keys show the layer's map (tonic, modes, slots,
  effects).
- chord keys: lit while held; **with LOCK on, the latched keys stay lit**; lit while latched in Advanced/Free
  (toggled extensions); dim otherwise. **B3 (LOCK) lit while LOCK is on**, dim when off.
- KEY lit = Key Mode on; PERF lit = performance on; FX / BASS / LATCH lit = on; METRO lit = metronome on;
  **a layer button blinks while its layer is open** (locked, after the hold); REC red blink = recording, red =
  overdub armed; LOOP orange = a loop exists, green = playing; OPT lit = menu open; OCT− lit / OCT+ blinking in
  menus and pickers.
- the editor: **EDIT blinks while the editor is open** (and while the engine picker is open), **the current
  section's button is lit** (OSC = printed FX, FILT = SEL, ENV, LFO, MOD = SEQ, FX = PLAY, MIX = REC); the root
  keys keep showing what sounds (audition).

## 7. Firmware architecture

Felucca forked into `ChoralRootFM1/firmware` (one compilation unit, header-only HAL, the same `tools/build.py`),
the platform kept, the instrument replaced. The files as they are (`docs/INTEGRATION.md` §1 has the include order):

```
firmware/src/
  choralroot.c      the compilation unit (replaces felucca.c): build options (FELUCCA_SLICE 0, FELUCCA_SLICER 0,
                    FELUCCA_VA 1; all-synth: FELUCCA_SEQ 0, FELUCCA_SAMPLE 0, FELUCCA_GRAIN 0, FELUCCA_DRUM 0,
                    FELUCCA_ICONS 0, FELUCCA_KEYCAPS 0; FM-1_920, "ChoralRoot 0.1"), Felucca's include order
  kept from Felucca (the platform):
    hal/, libc.c, lcd.c, gfx.c (no keycaps), panel.c, usb.c, midi_uart.c, storage.c, storage_hw.c, ota.c, ota_hw.c,
    console.c, main.c, settings_persist.c, upreset.c (32 user sounds)
    engines.c + eng_*.c, dsp.c, voice.c, mod.c, fx.c (+ perform.c), params.c, audio.c, fm6_*.c       the sound:
                    ANALOG, PHASE, LOFI, VOICE, TRIO, WHEEL, PHYS, NOISE, FM6, the VA and CZ-1 (14, Melodee's, docs/CZ1.md:
                    eng_cz.c, cz_*.c, Casio's 64 tones, 8 banks, tone SysEx, cz_ustore.c); the slots of DIGITAL (1),
                    SAMPLE (4), GRAIN (8) and DRUM (10) are retired placeholders (engines.c ENG_GONE: never offered;
                    a user sound on one loads as INIT on ANALOG)
    slicer.c        no-op stubs (SLICER off: its 32 KB POOL buffer freed; perform.c's buffer effects off with it);
                    the SLICE engine is off too
  ChoralRoot:
    cr_engine.c/.h  the musical engine (CR_ENGINE.md): chord tables incl. secret chords, KEYMAP quantization,
                    voicing, Simple/Advanced/Free, Extension Addition, latch, strum/slop/arp/pattern/harp scheduler,
                    bass, note ownership, panic
    cr_loop.c/.h    the semantic event looper (docs/LOOPER.md)
    cr_out.c        the three streams -> parts 0 (chord) / 1 (bass) and MIDI channels 1/2/3, clock out, the input
                    queue; MIDI in on the parts (midi_event: notes, pedal, bend, pressure, CCs) and what the kept
                    files call of the dropped sequencer (events_block: the engine switches, panic_req); includes
                    cr_midi.c
    cr_midi.c       MIDI in, the pure part: the clock-in tempo follower, the channel map
    cr_ui.c/.h      the input grammar (taps, locked layers, LOCK, pickers, Options, naming), the view-model
                    (cr_build_screen), the LEDs (cr_leds)
    cr_edit.c       the sound editor (docs/EDITOR.md): sections, rows, banks, page memory, its edit8 / stack
                    view-models  [being built]
    cr_pages.c      the parameter catalogue the editor draws from: the platform pages, the engines' EDIT 1/2,
                    deep pages, labels, glyphs, value text
    cr_bank.c       the pools (docs/PRESETS.md: one per engine, INIT, the factory presets each replaced by the
                    user's record bound to it, the added ones), the loud presets' trims, the save with the
                    binding (wraps upreset.c's 32 slots)
    cr_name.c       naming with the root keys (SAVE)
    cr_settings.c/.h  the settings record in Felucca's settings_persist.c (docs/SETTINGS.md)
    cr_screen.h, cr_draw.c, cr_gfx.c   the screens (CR_SCREENS.md): the view-model and its drawing, scalable text
                    and shapes, the strip cache
    cr_anim.c       tweens, springs, Options > Motion
    cr_shim.c       the names main.c and the kept files call of Felucca's dropped UI; the splash
    eng_va.c        the VA engine (docs/VA.md): 4 oscillators, SVF, 4 AHDSR, 4 LFOs, 8-slot matrix, deep pages
    va_store.c      the VA patch store, one patch per user slot (included by upreset.c)
tools/emu/          the Mac emulator (§7.1): the same firmware compiled for the host
tests/              host tests: cr_engine_test, cr_loop_test, cr_midi_test, cr_settings_test, cr_va_test,
                    cr_draw_test (+ Felucca's), regress.c golden renders and CPU
```

Dropped from the unit: Felucca's `ui*.c`, `editor*.c`, `project.c`, `favorites.c`, `icons.c`, the sequencer (`seq.c`,
`song_chain.c`, `chord.c`, `motion.c`, `midi_control.c`, `midi_clock.c`) and the sample-based engines (`eng_sample.c`
with its ADPCM sets, `eng_grain.c`, `eng_drum.c` + `drum_voice.c`); their files stay in the tree for Felucca's unit
and its tests (each behind its flag, default 1 there).

- **Timing**: Felucca runs input at 10 kHz (TIMER5) and audio in 128-frame blocks (2.9 ms). The
  ChoralRoot scheduler runs in the audio ISR (the input queue drained every 32-sample block), so strums and
  arps are sample-stable; the UI polls edges from the main loop.
- **Sound**: two Felucca *parts* — CHORD (POLY, the chord / performance notes) and BASS (MONO) — each
  with its own engine + preset; the raw-chord stream is MIDI-only by default (Orchid defaults it off).
  ChoralRoot keeps Felucca's engines, their factory presets and user slots, as one editable pool per engine
  (`cr_bank.c`, docs/PRESETS.md; FM6 gains PIANO as F25).
- **Polyphony**: Melodee's voice model: 16 budget units shared, an FM6 voice one (FM6, Melodee's Dexed-exact engine:
  8 a part here, `FM6_POLY`), any other two (the VA's `poly` is 8); docs/FM6.md. A 6-note chord + bass fits; chord
  changes steal the old chord's releasing voices, now with a one-block fade (§9).
- **MIDI**: USB + TRS; channels 1 performance / 2 bass / 3 chord (each on/off, Options); 24-PPQN clock
  out; MIDI in plays the parts (Orchid's behaviour) and can clock the tempo (`cr_midi.c`).
- **Persistence**: settings in Felucca's settings record (`cr_settings.c`, saved only when quiet); loops in ten
  flash slots (storage.c's commit protocol); user sounds in Felucca's 32 user preset slots, VA patches beside them
  (`va_store.c`).
- **Tests on the host**: `sh tests/run_cr_tests.sh`, `sh tools/emu/test_cr.sh` (headless scripts), `sh
  tools/emu/perf.sh` (the worst cases); `tests/run_cr_draw.sh` renders every ChoralRoot screen to PPM.

### 7.1 The Mac emulator

The whole firmware runs on the Mac so the design can be played and iterated without flashing. It
builds on what Felucca already proves: the engines, the UI and the sequencer compile for the host
in `tests/` (`hostsim.c` stubs the HAL; `ui_render.c` runs the real drawing code into a buffer;
`regress.c` renders audio). `tools/emu/` adds an **SDL2** app (`clang`, no Docker) that:

- draws the 240×240 LCD buffer scaled ×3, the keybed and the buttons with their LEDs, and the knobs,
  from the same `fm1_led[]` / `fm1_in` state the hardware uses;
- feeds the firmware's input state from the keyboard and mouse at the real 10 kHz scan cadence
  (the main loop and the audio block callback are called as on the device, 128-frame blocks);
- plays the audio through SDL audio at 44.1 kHz, and sends the three MIDI streams to a CoreMIDI
  virtual port ("ChoralRoot"), with MIDI in from any CoreMIDI source — so the DAW side of the
  design is testable too;
- saves "flash" to a file so settings, user sounds and loops persist between runs;
- screenshots to PNG (`S`) and records the screen to an image sequence (`R`) for the designer and the
  docs.

**Key map** (the user's): the two keyboard rows are the root keys, the function keys and the
number row are the chord block.

| Mac keys | FM-1 |
| --- | --- |
| `A S D F G H J K L ; ' ]` | white note keys C4 D4 E4 F4 G4 A4 B4 C5 D5 E5 F5 G5 (Ableton Live's layout) |
| `W E T Y U O P` | black note keys C#4 D#4 F#4 G#4 A#4 C#5 D#5 (above the whites, piano-wise; `R` `I` in the gaps unmapped; F#5 mouse only) |
| `F1 F2 F3 F4` | DIM MIN MAJ SUS (the black chord keys) |
| `2 3 4 5` | 6 m7 M7 9 (the white chord keys) · `Tab` = B3 LOCK (the chord mod key) |
| `F5`–`F10` / `7 8 9 0 - =` (the panel's two rows) | printed FX SEL ENV LFO EDIT GLO / HOME SAVE ARP SEQ PLAY REC = FX KEY BASS LATCH EDIT OPT / HOME SAVE PERF METRO LOOP REC (in the editor: OSC FILT ENV LFO EDIT SHIFT / HOME SAVE PERF MOD FX MIX) |
| `Z` `X` (also `Esc` `⏎`) | OCT− OCT+ · `End` both = panic |
| mouse wheel over a knob, or `↑ ↓` with a knob selected (`Page Down` / `Page Up` cycle SELECT, KNOB 1–4; MASTER, PRESETS, ALGORITHM by mouse; Shift + `↑ ↓`: fine steps in the editor (GLO held), OPT's second function outside it) | the eight rotaries |
| click | any button or key on the drawn panel |
| `` ` `` · PrintScreen (F13) · Insert · F12 | big LCD view · LCD screenshot · record frames · print the state |

The map lives in one table (`tools/emu/keymap.c`) and can be changed; no key may be mapped twice (checked at start-up, exit 2). The emulator is M0's second
deliverable, before any hardware milestone, so every screen and gesture is tried on the Mac first.

## 8. Milestones

| # | Milestone | Done when | State |
| --- | --- | --- | --- |
| M0 | **Repo + platform + emulator** — fork Felucca, GPL licence, build in Docker, install on the FM-1 via the web installer, splash + panic screen; the Mac emulator (§7.1) running the same firmware with the key map, audio and CoreMIDI | Felucca builds from `ChoralRootFM1/`, boots, keys make sound; the same build plays on the Mac from the keyboard | **done** — `choralroot.fwsc` builds and is flashed; the emulator runs it headless and windowed |
| M1 | **Chord engine in C** — ported from `d_cr_engine.lua`, host tests green (chord tables, secret chords, KEYMAP, voicing, Simple/Advanced/Free, Extension Addition, note ownership, panic) | the Lua suite's engine cases pass in C | **done** — `cr_engine.c`, `tests/cr_engine_test.c` green |
| M2 | **Play it** — keybed split (8 chord keys + 18 roots, OCT shift), buttons mapped (§3), roots → CHORD part, KNOB 1 voicing, PRESETS sound, MIDI ch 1/3 out, CHORD view with the big chord name, dial screens, key LEDs = chord notes | Dmaj7 on the speaker and on MIDI, voicing clicks, screen names it | **done** — on the device and the emulator |
| M3 | **Key Mode + layers** — KEY tap/hold, key layer, Options, VIEW, LATCH, play styles, secret chords scope, Single Notes | mock-up states 5, 6, 12, 17 behave as drawn | **done** (layers as hold-to-show; the lock-on-hold grammar is M7 work below) |
| M4 | **Performance** — PERF tap/hold, perform layer, KNOB 3, BPM on SELECT, MIDI clock out, 13 patterns | states 7, 8; arps hold tempo, parameters rephase live | **done** — in `cr_engine.c`'s scheduler |
| M5 | **Bass + FX + editing** — BASS button + layer, BASS part on ALGORITHM, KNOB 2 register, behaviours, FX button + layer, KNOB 4 amount; EDIT pages, engine picker, SAVE to user slots with naming; MIDI in to the parts, CC / program change, clock in; the VA engine | states 14, 15, 16, 22, 23 | **done** — the paged editor (to be replaced by the dense editor below), `cr_bank.c`, `cr_name.c`, `cr_midi.c`, `eng_va.c` + `va_store.c` |
| M6 | **Looper** — free + 1/2/4/8/16-bar sync with count-in, quantize, overdub/undo/clear, 10 slots, the ring, LOOP / REC transport, SAVE hold | states 9, 10, 11 | **done** — `cr_loop.c` (docs/LOOPER.md) |
| M7 | **Finish** — views (Keyboard, Notes, Geek Out, Scope), the motion pass, palettes, LED inversion, settings persistence, sound bank, Options complete, **the new interaction grammar** (§3–§4: layers lock on hold, OCT− / HOME close, LOCK on B3, SEL = KEY / EDIT = EDIT), **the dense sound editor** (`cr_edit.c`, docs/EDITOR.md), sticker sheet, web installer page, README/manual | a first public beta (`.fwsc` + installer) | **partly** — done: views, motion, the MOD palette, LEDs (Options > LEDs), settings, Options, calibration, splash, the performance pass. **Building now**: the grammar and the editor. **Open**: the bank chosen by ear on the device, the sticker sheet, the installer page, the README / manual |
| M8 | **Parity passes** — Orchid MIDI captures for the secret-chord combo map, chromatic quantization, Key Mode 7ths and factory patterns (`ORCHID_CAPTURE_RUNBOOK`), then update the tables | parity items in `ORCHID_PARITY_AUDIT.md` closed or documented | open |

Every milestone is played on the emulator before it is flashed.

## 9. Risks and open decisions

1. **Root range**: with the chord block on the keybed the roots span D4–G5 (18 keys, 1½ octaves).
   That is still more than Orchid's one octave, and OCT−/OCT+ move it; the chord notes of a low
   voicing can fall below D4 and then show on no key (the screen's notes line always has them).
2. **Toolchain**: JieLi `pi32v2` clang in a `linux/amd64` Docker container on macOS (`BUILDING.md`). Solved for
   M0; the risk left is the gitee boot files disappearing.
3. **No encoder push, no velocity**: Orchid's press / long-press / press+turn gestures become
   button taps, holds and OPT+turn (§3); velocity is fixed in Options (patterns keep their accents).
4. **Relabelling**: the eight chord keys, B3 and most buttons change meaning (SEL = KEY, ARP = PERF,
   ENV = BASS…), and in the editor they change again (FX = OSC, SEL = FILT, SEQ = MOD, PLAY = FX, REC = MIX). The
   footer hints name buttons by their ChoralRoot role; the editor has no footer, so the printable sticker sheet (M7)
   carries both layers.
5. **CPU and voices** (measured, `docs/INTEGRATION.md` Performance; host instructions scaled to the device, which
   the device's `cpu` console readout has to confirm): a 6-note chord held costs **12–20 % of the 2.9 ms block on
   average, 23–46 % in the worst block** across the bank (FX buses +1 %); TINE EP 20 / 33 %, FM PAD 24 / 34 %; the
   worst case, FM PAD + bass + a playing loop + an arp at 200 BPM, **24 / 42 %** (46 % worst on SCOPE); a VA chord +
   VA bass averages 33 %, worst blocks 46–52 %. ChoralRoot's own ISR work is ~1.4 %. No overruns, no shed voices,
   no holes or clicks after the fixes (flash erases only when quiet, the limiter's eased release, fade-steal on
   chord changes, the delay's crossfade). The voice budget stays 8: every chord change with 6-note chords steals
   the old chord's releasing voices (faded over one block). The UI frame's worst is now 14 ms (loop + arp) and
   34 ms (knobs on the editor), against a 15 ms frame: **the dense editor must be re-measured** (`perf.sh (e)`).
6. **Memory**: since the all-synth cut (2026-10-07: the sequencer, SAMPLE with its sets, GRAIN and DRUM out of the
   image) RAM is at **74.5 %** of 98304 B (was 92.4 %), POOL 85.7 % (was 95.9 %), XIP 47.0 % (was 84.8 %): about
   23 KB of RAM, 48 KB of POOL and 280 KB of flash free. The loop cap (the grid's 64 events) is set from the headroom
   of before (docs/LOOPER.md) and could grow now. Melodee's FM6 (2026-10-07) took RAM to 78.8 %, POOL to 88.1 %, XIP to
   54.3 % (docs/FM6.md, Memory).
7. **Still unknown Orchid behaviour** (not blocking, same as the grid): secret-chord combo → type map,
   full chromatic Key Mode quantization, Key Mode sevenths, factory pattern data. Shipped as the grid's
   labelled fallbacks until captured (M8).
8. **Bluetooth MIDI**: the stock firmware has BLE MIDI; Felucca does not. Out of scope.
9. **Editor variant**: HOME leaves the editor (variant A, drawn) or HOME is the MIX view and REC stays REC
   (variant B) — docs/EDITOR.md, open questions.

## 10. Next steps

1. **The grammar** (`cr_ui.c`, building): layers lock on a 300 ms hold and stay until OCT− / HOME or another layer
   button; taps always act; the engine picker locks the same way; LOCK on B3 with its latch rules; SEL = KEY,
   EDIT = EDIT; the HOME-lock, OPT + FX / PERF and OPT + KNOB 3 gestures removed; METRO's beat layer; the LEDs of §6.
   Check against mock-up states 1–24.
2. **The editor** (`cr_edit.c`, building): docs/EDITOR.md, checked against the 16 editor states; then `perf.sh (e)`
   for the UI frame and the RAM figure.
3. On the device: the `cpu` readout with FM6 and VA chords, a bass and a playing loop, to confirm the host
   estimates of §9.5; choose the factory bank by ear.
4. Docs and release (M7): the sticker sheet (both button layers and LOCK), the web installer page, the README /
   manual; then the first public beta.
5. The emulator: a Mac key for B3 (LOCK) in `tools/emu/keymap.c`.
6. M8 parity passes when an Orchid is at hand for the captures.
