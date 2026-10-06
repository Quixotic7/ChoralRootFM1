# ChoralRoot FM-1 — build plan

**What:** a Telepathic Orchid-style chord instrument as a firmware for the M-VAVE FM-1: one hand plays
roots, the other shapes chords; voicing, Key Mode, performance modes, bass, looper — with the FM-1's
own sound engines and a screen, plus the three-channel MIDI output that the grid version
(`choralroot_fullsource/`) already gets right.

**Status (2026-10-05):** plan + interface mock-ups (fifth pass: chord block on the keybed, mod-scene screens with pickers and meters, the Mac emulator planned). Nothing compiled yet.

| Deliverable | Where |
| --- | --- |
| this plan | `PLAN.md` |
| interface mock-ups (24 states, LEDs + screens) | `design/choralroot-fm1-mockups.json`, `design/choralroot-fm1-screens.png`, `design/choralroot-fm1-panels.png`; generator `design/make_mockups.py` |
| the designer tool that renders them | `../ChoralRootFM1Designer/` (`index.html`, `FORMAT.md`) |

## 1. Starting points

| Source | What we take from it |
| --- | --- |
| **Felucca** (`../Felucca`, GPL-3.0-only, hugelton) | the whole FM-1 platform: HAL (`firmware/hal/*.h`: key/LED matrix at 10 kHz, 7 encoders with detent decoding, LCD SPI, audio I²S, flash, USB), `lcd.c`/`gfx.c` (240×240 strip renderer, Inter Tight fonts, Fukiai icons, 8 palettes), 13 sound engines + voice allocator + FX (`engines.c`, `voice.c`, `fx.c`), USB MIDI in/out + TRS MIDI in + MIDI clock, flash settings/presets/projects, the OTA update loader and web installer, the host test harness (`tests/hostsim.c`, `ui_render.c` renders every screen to PNG on the build machine), the build (`tools/build.py`, JieLi toolchain in Docker) |
| **sloop-fm1** (`../sloop-fm1`, Felucca fork) | the interaction idiom we adopt: *hold a button, touch a key* — every function button is a layer while held, the 16 white keys and KNOB 1–4 change job, the screen shows tiles + dials; lock a layer with HOME; tap = pages. Also its splash, recovery and "saves retried" robustness work |
| **choralroot (grid iii)** (`../choralroot_fullsource`) | the musical engine, already Orchid-correct for MIDI: `d_cr_engine.lua` (chord tables incl. the firmware-verified secret chords, `KEYMAP` harmonic quantization, voicing rotation, Simple/Advanced/Free state machine, Extension Addition, strum/slop/arp/pattern/harp scheduler with BPM rephasing, bass, note-ownership registry, panic), `d_cr_loop.lua` (semantic event looper with layers/undo), `design.md` (the normative behaviour spec, §5–§17, §20) and `dev/test_choralroot.lua` (426 assertions to port as C tests) |
| **Orchid references** (`telepathic orchid reference/`) | the manual (dial gestures, Options menu, View modes, loop waiting room, Standard Chord Naming Framework), `ORCHID_FIRMWARE_REFERENCE.md` (chord/extension interval tables, secret chord contents), the UI screenshots (big chord name, "Key: C" badge, list menus, big-number dial screens, loop ring) |
| **OMX-LED-Designer** | the designer tool pattern (now adapted as `ChoralRootFM1Designer`) |

Licensing: a Felucca fork is **GPL-3.0-only**, and `ChoralRootFM1/LICENSE` is GPL-3.0 (Felucca's
`LICENSES/` folder for the font, icons and ported DSP travels with the fork). The designer stays MIT.

## 2. What changes from the grid version

| | grid iii choralroot | ChoralRoot FM-1 |
| --- | --- | --- |
| surface | 16×8 grid, 12 note columns × 8 velocity rows, 4-column control strip | 27-key keybed (no velocity): 8 chord keys + 18 root keys D4–G5; 12 + 2 buttons; 7 encoders + pot; 240×240 screen |
| chord buttons | grid keys | the left of the keybed: DIM MIN MAJ SUS on the black keys F#3 G#3 A#3 C#4, 6 m7 M7 9 on the white keys F3 G3 A3 C4 (B3 unused) — Orchid's 2×4 block, two rows |
| voicing | two grid keys with repeat | an endless encoder (KNOB 1), like Orchid's dial |
| sound | MIDI only | internal engines (Felucca) **and** MIDI out on 3 channels |
| menus / parameters | hold-to-reveal on the grid's top row | screen layers (tiles + 4 knob cards), an Options list |
| display | LED levels | chord name, notes, keyboard strip, ring, Orchid's View modes |
| loop display | column fill while Loop is held | the ring progress indicator around the screen |
| velocity | 8 rows | fixed (Options > Velocity); patterns keep their accents |

## 3. Control mapping (normative; `design/make_mockups.py` draws it)

### The keybed

| Keys | ChoralRoot |
| --- | --- |
| F#3 G#3 A#3 C#4 (black) | **DIM MIN MAJ SUS** — Orchid's top row, momentary chord types |
| F3 G3 A3 C4 (white) | **6 m7 M7 9** — Orchid's bottom row, momentary, stackable extensions |
| B3 | unused (dark); keeps the two rows aligned as Orchid's 2×4 block |
| D4 … G5 (18 keys) | **roots**: a single note alone, a chord with a type held; OCT−/OCT+ shift them by octaves (−2…+2) |

Two chord types held together = a Secret Chord when Options allows it (DIM+SUS power, MAJ+SUS
augmented, MIN+SUS [0 3 5]), as on the grid. In layers the root keys become the layer's map (tonic,
mode, slot, effect) while the chord keys keep their job (hold MIN with a tonic = minor key).

### Buttons (the Orchid dials' button functions)

| Printed | ChoralRoot | Tap | Hold |
| --- | --- | --- | --- |
| EDIT | **KEY** | Key Mode on/off (LED lit when on) | key layer: root keys = tonic, KNOB 1–4 = TONIC / SCALE / TRANSPOSE / SINGLE NOTES |
| ARP | **PERF** | performance on/off | perform layer: white root keys D4–C5 = STRUM, STRUM 2, SLOP, ARP, ARP 2, PATTERN, HARP; KNOB 1–4 = the mode's parameters |
| FX | **FX** | the sound's main effect on/off | fx layer: white root keys pick the effect, KNOB 1–3 its parameters, KNOB 4 the amount; OPT+FX = FX lock |
| ENV | **BASS** | bass on/off | bass layer: KNOB 1–4 = BEHAVIOUR (Chords Only / Unison / Single Notes / Solo) / REGISTER / SOUND / LEVEL |
| LFO | **LATCH** | Sticky keys / arp hold on/off | — |
| GLO | **OPT** | Options menu open/close | held + a knob = that knob's second function (Orchid's press+turn) |
| HOME | **HOME** | back to the view from any page, menu or layer; tapped again on the view: next View (CHORD / KEYBOARD / NOTES / GEEK OUT / SCOPE) | hold + a layer button = lock the layer open (sloop) |
| SAVE | **SAVE** | save the edited sound to one of 32 user slots, named with the keys (Orchid's Sound long press) | save / load / delete loops (Orchid's Loop long press) |
| SEQ | **METRO** | metronome / beat on-off (Orchid's BPM press) | beat and time-signature picker |
| PLAY | **LOOP** | play / stop the loop (green LED = playing) | loop layer: white root keys = slots 1–10, D#4 = CLEAR (hold), F#4 = UNDO; KNOB 1–4 = SYNC / QUANTIZE / COUNT-IN / LEVEL |
| REC | **REC** | record (count-in, then the sync length) / overdub arm; red LED blinks while recording | undo the last layer |
| SEL | **EDIT** | the sound's edit pages (SELECT turns pages, KNOB 1–4 edit; BASS held + EDIT = the bass sound) | engine picker: white root keys = engines |
| OCT− / OCT+ | octave of the root keys | in menus and dialogs: **back / OK** (Felucca's convention; OCT+ blinks when it would do something) | **both together: PANIC** (all notes off on every stream, CC 123 on the three channels, octave reset) |

### Rotaries (no push switches on the FM-1)

| Printed | ChoralRoot | Turn | OPT held + turn |
| --- | --- | --- | --- |
| MASTER | VOLUME | the pot | — |
| SELECT | BPM | tempo 20–300 (in menus: scroll) | metronome volume |
| PRESETS | SOUND | browse the chord/perform sound (popup list, loads at once) | — |
| ALGORITHM | BASS SOUND | browse the bass sounds (Orchid's Bass Dial turn) | bass volume |
| KNOB 1 | **VOICING** | Orchid's Chord Voicing: lowest note up an octave / highest down, one click = one note; **never reassigned** | single-note split point |
| KNOB 2 | BASS VOICE | bass register, octaves | — |
| KNOB 3 | PERFORM | the selected mode's main parameter (strum speed, slop amount, arp rate, pattern number, harp speed) | performance lock on/off |
| KNOB 4 | FX | amount of the selected effect (Orchid's FX Dial default) | — |

A knob turn shows its value big on the screen for a second (Orchid's dial screens, §5); nothing
is displayed permanently for the knobs.

## 4. Interaction grammar

- **Tap = use, hold = reveal, release = return** (grid design.md §25) carried over; hold threshold
  300 ms (Felucca `HOLD_MS`), a key or knob touched during the hold makes it a combo (no tap action).
- **Layers** (sloop's idiom): while KEY / PERF / FX / BASS / LOOP is held, the root keys and KNOB 1–4 are the layer's,
  the header reads `[KEY] HOLD`, the panel shows the map as tiles, the cards show the layer's four
  knobs, the footer shows keycap hints. Hold + tap HOME locks a layer open (sloop's lock).
- **Menus**: OPT tap opens the Options list; SELECT scrolls, KNOB 1 sets, OCT− back, OCT+ enters.
- **Popups** (stock FM-1 / Felucca): turning PRESETS or ALGORITHM shows the list for a second; turning
  SELECT highlights the BPM; a knob turn makes its card "hot" (accent) briefly.
- **Panic**: OCT− + OCT+ pressed together — a chord nothing else uses, so it can never fire while OPT
  is held as a shift; LEDs flash, screen message, CC 123 on all three channels, octave reset, loops kept.
- **Sound editing**: EDIT opens the current chord sound's pages (BASS held + EDIT: the bass sound's):
  ENGINE, the engine's own parameters (two pages), ENV, LFO, FILTER / MOD, FX sends, MIX — Felucca's
  page model, four parameters per page on KNOB 1–4 (a detent: 5% of the range, enums one by one; OPT held: one step), SELECT turning pages (the stock FM-1's SELECT), OPT + SELECT jumping to the next section (an engine with deep pages, the VA, adds its own between EDIT 2 and ENV),
  HOME back to the view. EDIT held is the engine picker on the white root keys (ANALOG, FM6, PHASE,
  LOFI, SAMPLE, VOICE, TRIO, WHEEL, GRAIN, PHYS, NOISE). SAVE stores the result in one of 32 user slots,
  named with the keys, listed after the factory bank on PRESETS; user bass sounds the same on
  ALGORITHM. Felucca's web editor stays for deep FM6 patches, backup and restore.
- **MIDI**: USB MIDI in and out and TRS MIDI in (Felucca's `usb.c`, `midi_uart.c`). Out: the three
  streams on channels 1 / 2 / 3 (each on/off in Options), 24-PPQN clock out, start/stop with the loop.
  In: notes on the chord channel play the chord part directly (Orchid's behaviour: no chord
  generation), the bass channel the bass part, CC for the FX amounts, program change for sounds,
  clock in syncs the tempo (Options > MIDI Clock: OUT / IN / OFF), SysEx for the web editor and
  firmware updates.
- **Play styles** Simple / Advanced / Free, Extension Addition, Secret Chords scope, Bass Behaviour,
  Velocity, Loop Quantization, MIDI channels, MIDI clock, View, Metronome, Palette, LEDs, Version,
  Update firmware: all in the Options list (mock-up state 11).

## 5. Screen

Orchid's rule, kept: **one big thing per screen.** The language is **1960s mod** — the roundel, bold
stripes and flat colour blocks, heavy grotesk type, black and white with red, blue, yellow, orange and
green — clean and high-contrast, in the spirit of the references in `../UI Inspiration` and copied
from none of them. Felucca's `gfx.c` draws it (240×240, Inter Tight; the `MOD` palette added to its
eight). Two rules from the user: **no menus** (no multiple-choice lists) and **no circled numbers**.

### Colour

| colour | owns |
| --- | --- |
| **white** | the chord name and its triad notes, sounds, the Perform picker, tempo |
| **blue** | KNOB 1 voicing |
| **orange** | the bass (ALGORITHM, KNOB 2, BASS) and 7th extensions |
| **red** | the loop and REC (the ring), secret-chord tones, panic |
| **yellow** | Key Mode (`Key: C`, select-key) |
| **green** | FX (KNOB 4, the FX picker) |

A thing on screen is the colour of the control that moves it.

### Screens

- **Idle**: the name over three racing stripes in the coralroot orchid's colours (red, orange,
  white) that slide sideways at the BPM. The first chord sweeps the stripes off the screen and the chord name lands where the title
  was. No instructions anywhere.
- **Chord** (the default View): the name fills the screen in the Orchid Standard Chord Naming
  Framework, **squeezed horizontally to fit** as the Orchid's own screen does with long names; the
  extension superscript in its colour; the voiced notes as a plain line of text under it, extensions
  coloured with a block under them. `Key: C` top-left in yellow when Key Mode is on; a status top-right.
- **The squeeze**: on a chord change the old name squeezes to a thin column and the new one stretches
  out from it (~120 ms) — the Orchid's squeeze, played up.
- **Pickers** replace every list: one choice at a time, huge, its neighbours peeking small and faded
  above and below (left and right inside the ring), square position marks, the value under it. SELECT
  or the root keys move it with a split-flap flip; OCT+ confirms, OCT− backs out. Used for Perform,
  FX, bass behaviour, the loop length and the loop's Overdub / Pause / Undo / Clear, the engine
  picker, and **Options: one setting per screen** (its name big, its value under it, KNOB 1 sets).
- **Meters** replace dials: a knob's value huge in its colour over a stripe meter of bold blocks that
  fill one by one (`03 SUB / bass`, `13 EP / sound`, `05 / reverb`, `120 / bpm`).
- **Perform in motion**: the chord's notes as text on a line; the sounding one sits on a colour block
  and hops along in time, a dotted arc to the next.
- **Select key**: KEY held slides up the keyboard with the tonic lit yellow.
- **The ring**: Orchid's progress ring as a dotted circle round the edge — red while recording and
  playing (the loop's colour); the loop pickers sit inside it.
- **Sound edit pages**: four columns for KNOB 1–4 in the knob colours (blue, orange, white, green),
  each a glyph (an envelope that redraws as the knob turns, a wave, a filter curve, a bar, an arc), a
  label and a value; the page name and `n/8` top right.
- **Panic**: the whole screen goes red with black type.
- **Views** (Options > View): CHORD (default), KEYBOARD, NOTES, GEEK OUT (the one dense screen), SCOPE.
- A thin footer line appears only in layers.

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
- a secret chord **flashes** its name; panic **shakes** the red screen.

Motion is a setting (Options > Motion: full / calm / off).

- The 24 mock-up states: idle stripes · Dmaj7 · voicing +2 · Key Mode Em · select key · Perform picker ·
  arp in motion · loop length · recording · loop playing picker · Options · secret chord · bass meter ·
  bass picker · FX picker · Geek Out · sound meter · panic · reverb meter · BPM meter · the ENV edit
  page · the engine picker · saving a sound · a chord change mid-squeeze
  (`design/choralroot-fm1-screens.png`). Each state's note says how it moves.

## 6. LEDs

Felucca's rules: every key and button glows dim at rest (the panel is findable in the dark; Options >
LEDs can invert to the stock look); lit = held, sounding, active; blinking = a layer button held, LOOP
recording, OCT+ when it would act. Specifically:

- root keys: the **voiced chord notes light where they sound** (Orchid's chord display, as the grid
  version does); in Key Mode the black root keys outside the scale go dark; a performance's current
  note blinks; loop notes glow dim; in layers the root keys show the layer's map (tonic, modes, slots,
  effects).
- chord keys: lit while held, lit while latched in Advanced/Free (toggled extensions), dim otherwise;
  B3 always dark.
- KEY lit = Key Mode on; PERF lit = performance on; FX / BASS / LATCH lit = on; a held layer button
  blinks; REC red blink = recording, red = overdub armed; LOOP orange = a loop exists, green = playing;
  OPT lit = menu open; OCT− lit / OCT+ blinking in menus.

## 7. Firmware architecture

Fork Felucca at its current `main` into `ChoralRootFM1/firmware` (one compilation unit, header-only
HAL, same `tools/build.py`), keep the platform, replace the instrument:

```
firmware/src/
  felucca.c            -> choralroot.c   build options, include order (as Felucca)
  hal/, lcd.c, gfx.c, icons.c, libc.c, usb.c, midi_uart.c, midi_clock.c, storage*.c, ota*.c, console.c, main.c   kept
  engines.c, eng_*.c, dsp.c, voice.c, mod.c, fx.c, params.c, audio.c                                       kept (sound)
  seq.c, song_chain.c, motion.c, perform.c (FX layer), slicer.c, ui_*.c, project.c, upreset.c, editor*.c  stripped or rewritten
  cr_chord.c        chord tables (dim/min/maj/sus + aug/pow/min4, extensions 9/10/11/14), chord_base(), KEYMAP quantization, voicing rotation
  cr_voice.c        note ownership registry per stream, play styles (Simple/Advanced/Free), Extension Addition, Sticky/Hold latch, panic
  cr_perform.c      strum / slop / arp / pattern / harp scheduler on the master clock (ticks = audio blocks), BPM rephasing, 13 patterns with accents
  cr_bass.c         bass stream, behaviours, register
  cr_loop.c         semantic event looper: free / 1-16 bar sync, count-in, quantize, layers + undo, 10 flash slots
  cr_out.c          the three streams -> internal parts (CHORD/PERFORM part, BASS part) and MIDI channels 1/2/3, MIDI clock out
  cr_ui.c, cr_draw.c, cr_layer.c, cr_menu.c, cr_leds.c    the UI of §4-§6 on Felucca's gfx
  cr_anim.c         the tween helper: eased / spring values on the frame tick, the squeeze, the flips (§5 Motion)
tools/emu/          the Mac emulator (§7.1): SDL2 window, the firmware compiled for the host
  cr_settings.c     Options persistence (Felucca's settings record), sound locks
```

- **Timing**: Felucca runs input at 10 kHz (TIMER5) and audio in 128-frame blocks (2.9 ms). The
  ChoralRoot scheduler runs in the audio ISR at block rate, as Felucca's sequencer does, so strums and
  arps are sample-stable; the UI polls edges from the main loop.
- **Sound**: two Felucca *parts* — CHORD (POLY, the chord / performance notes) and BASS (MONO) — each
  with its own engine + preset; the raw-chord stream is MIDI-only by default (Orchid defaults it off).
  ChoralRoot ships its own preset bank (chord-friendly pads/keys/plucks on FM6, ANALOG, WHEEL, PHYS;
  12 basses) and keeps Felucca's engines and user slots.
- **Polyphony**: Felucca's budget is 8 voices shared. A 5-note chord + bass fits; HARP over 3 octaves
  and two sounding chords will steal. Measure CPU with `tests/regress.c`'s cost files in M2 and decide
  whether `NVOICE` can grow for lighter engines.
- **MIDI**: USB + TRS; channels 1 performance / 2 bass / 3 chord (each on/off, Options); 24-PPQN clock
  out; MIDI in plays the CHORD part (Orchid's behaviour) and can clock the tempo (Felucca's
  `midi_clock.c`).
- **Persistence**: settings + chord pads in Felucca's settings record; loops as compact flash files
  (the grid's slot format, §14.4 of design.md); user sounds in Felucca's user preset slots.
- **Tests on the host**: port `dev/test_choralroot.lua` to C against the stubs in `tests/hostsim.c`;
  keep Felucca's `ui_render.c` so every ChoralRoot screen renders to PNG and is linted for clipping
  (compare against `design/choralroot-fm1-screens.png`).

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
| `A S D F G H J K L ; '` | white root keys D4 E4 F4 G4 A4 B4 C5 D5 E5 F5 G5 |
| `W E T Y I O [` | black root keys D#4 F#4 G#4 A#4 C#5 D#5 F#5 (above the whites, piano-wise) |
| `F1 F2 F3 F4` | DIM MIN MAJ SUS (the black chord keys) |
| `2 3 4 5` | 6 m7 M7 9 (the white chord keys) |
| `Z X C V B N` / `, . / ⇧ ⏎ ⌫` | FX EDIT BASS LATCH KEY OPT / HOME SAVE PERF METRO LOOP REC |
| `←` `→` | OCT− OCT+ (both: panic) · `Esc` panic |
| mouse wheel over a knob, or `↑ ↓` with a knob selected by `1`–`8` | the eight rotaries |
| click | any button or key on the drawn panel |

The map lives in one table (`tools/emu/keymap.c`) and can be changed. The emulator is M0's second
deliverable, before any hardware milestone, so every screen and gesture is tried on the Mac first.

## 8. Milestones

| # | Milestone | Done when |
| --- | --- | --- |
| M0 | **Repo + platform + emulator** — fork Felucca, GPL licence, build in Docker, install on the FM-1 via the web installer, splash + panic screen; the Mac emulator (§7.1) running the same firmware with the key map, audio and CoreMIDI | Felucca builds from `ChoralRootFM1/`, boots, keys make sound; the same build plays on the Mac from the keyboard |
| M1 | **Chord engine in C** — `cr_chord.c` / `cr_voice.c` ported from `d_cr_engine.lua`, host tests green (chord tables, secret chords, KEYMAP, voicing, Simple/Advanced/Free, Extension Addition, note ownership, panic) | the Lua suite's engine cases pass in C |
| M2 | **Play it** — keybed split (8 chord keys + 18 roots, OCT shift), buttons mapped (§3), roots → CHORD part, KNOB 1 voicing, PRESETS sound, MIDI ch 1/3 out, CHORD view with the big chord name, dial screens, key LEDs = chord notes | Dmaj7 on the speaker and on MIDI, voicing clicks, screen names it |
| M3 | **Key Mode + layers** — KEY tap/hold, key layer, Options list, VIEW, LATCH, play styles, secret chords scope, Single Notes | mock-up states 4, 5, 11, 16 behave as drawn |
| M4 | **Performance** — `cr_perform.c`, PERF tap/hold, perform layer, KNOB 3, BPM on SELECT, MIDI clock out, 13 patterns | states 6, 7; arps hold tempo, parameters rephase live |
| M5 | **Bass + FX + editing** — BASS button + layer, BASS part on ALGORITHM, KNOB 2 register, behaviours, FX button + layer, KNOB 4 amount, locks; EDIT pages, engine picker, SAVE to user slots with naming; MIDI in to the parts, CC / program change, clock in | states 13, 14, 15, 21, 22, 23 |
| M6 | **Looper** — `cr_loop.c`: free + 1/2/4/8/16-bar sync with count-in, quantize, overdub/undo/clear, 10 slots, the ring, LOOP / REC transport, SAVE hold | states 8, 9, 10 |
| M7 | **Finish** — views (Keyboard, Notes, Geek Out, Scope), the motion pass (`cr_anim.c`: ribbon, bounces, hops, sweeps; Options > Motion), palettes, LED inversion, settings persistence, sound bank, Options complete, sticker sheet, web installer page, README/manual | a first public beta (`.fwsc` + installer) |
| M8 | **Parity passes** — Orchid MIDI captures for the secret-chord combo map, chromatic quantization, Key Mode 7ths and factory patterns (`ORCHID_CAPTURE_RUNBOOK`), then update the tables | parity items in `ORCHID_PARITY_AUDIT.md` closed or documented |

M0–M2 are the critical path; M3–M6 are independent of each other once M2 is in. Every milestone is played on the emulator before it is flashed.

## 9. Risks and open decisions

1. **Root range**: with the chord block on the keybed the roots span D4–G5 (18 keys, 1½ octaves).
   That is still more than Orchid's one octave, and OCT−/OCT+ move it; the chord notes of a low
   voicing can fall below D4 and then show on no key (the screen's notes line always has them).
2. **Toolchain**: JieLi `pi32v2` clang runs in a `linux/amd64` Docker container on macOS; the SDK's
   three boot files come from gitee. Felucca's `BUILDING.md` covers it; budget a day for M0.
3. **No encoder push, no velocity**: Orchid's press / long-press / press+turn gestures become
   button taps, holds and OPT+turn (§3); velocity is fixed in Options (patterns keep their accents).
   Alternative for velocity: OCT−/OCT+ held = soft / hard, as sloop's drum ghosts (rejected for now:
   OCT is the octave).
4. **Relabelling**: the eight chord keys and most buttons change meaning (EDIT = KEY, ARP = PERF,
   ENV = BASS…). The screen's footer hints name the buttons by their ChoralRoot role; a printable
   sticker sheet for the keys and buttons is part of M7.
5. **CPU and voices**: FM6 (Dexed) is the heaviest engine; chords of six notes through it plus a bass
   may exceed the block budget. Measure in M2; prefer ANALOG/WHEEL/PHYS presets for the factory bank
   if needed.
6. **Loop memory**: the grid caps loops at 64 events for RAM; the FM-1 has far more RAM (Felucca keeps
   64-step patterns × 4 tracks + samples), so the cap can rise — set it from the measured heap in M6.
7. **Still unknown Orchid behaviour** (not blocking, same as the grid): secret-chord combo → type map,
   full chromatic Key Mode quantization, Key Mode sevenths, factory pattern data. Shipped as the grid's
   labelled fallbacks until captured (M8).
8. **Bluetooth MIDI**: the stock firmware has BLE MIDI; Felucca does not. Out of scope.

## 10. Next steps

1. Confirm the control map in §3 against the mock-ups (open `ChoralRootFM1Designer/index.html`,
   load `design/choralroot-fm1-mockups.json`; edit the generator `design/make_mockups.py` for any
   change so the JSON stays reproducible).
2. M0: fork Felucca into `firmware/`, get `./build.sh` and the installer working on the device, and the Mac emulator running the same build.
3. M1: port `d_cr_engine.lua` to `cr_chord.c` / `cr_voice.c` with the test suite — the one part that
   needs no hardware.
