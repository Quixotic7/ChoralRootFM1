# ChoralRoot FM-1 — the sound editor

The dense sound editor, as approved in the mock-ups (2026-10-06). Normative sources:
`design/make_editor_mockups.py` → `design/choralroot-fm1-sound-editor-mockups.json` (16 states, rendered in
`design/choralroot-fm1-sound-editor-screens.png`) and the main walk-through's states 21–23
(`design/choralroot-fm1-mockups.json`: the editor's first view, the engine picker, the save dialog). The screen
kinds are specified in `../ChoralRootFM1Designer/FORMAT.md` ("Sound editor panels"). Firmware: `cr_edit.c` (the
editor, being built), drawing its parameters from `cr_pages.c` and the engines' deep pages (`eng_deep_t`, docs/VA.md).
`PLAN.md` §3–§6 has the rest of the instrument.

## 1. The rules it keeps

- **One big thing per screen** — here, one *section* per screen. The editor is the instrument's one dense place,
  so it takes the whole 240 × 240: **no header bar, no footer hints**.
- **Pickers, not lists.** No menu of pages to scroll: each group has its own button; a tap steps its screens,
  SELECT runs through the lanes.
  The engine picker and the save dialog are pickers, as everywhere else.
- **The mod look**: black ground, flat colour, heavy type; the four knob columns always in the knob colours —
  **blue, orange, white, green** (KNOB 1–4) — whatever they edit; everything not on the knobs grey. Shapes (the
  envelope, the filter curve, the waves) are drawn big and redraw as the knob turns.
- Taps step; SHIFT latches with a tap, so nothing needs two hands except chord ↔ bass (SHIFT + EDIT).

## 2. Entering and leaving

| Gesture | Where | Result |
| --- | --- | --- |
| **EDIT tap** | outside the editor | the editor on the **chord sound** (part 0), at its remembered group, screen and lane (first time: OSC, screen 1, lane 1). EDIT blinks |
| **BASS held + EDIT** | outside (BASS held, or its layer open) | the editor on the **bass sound** (part 1) |
| **SHIFT + EDIT** (GLO held, EDIT tapped) | in the editor | switch between the chord sound and the bass sound; each opens where it was last left |
| **EDIT tap** | in the editor | leave, back to the view |
| **HOME** | in the editor (no layer open) | leave, back to the view (variant A; see §12) |
| **EDIT held** | anywhere | the engine picker, a preview (§7); it closes back to where it came from |
| **PRESETS turned** | in the editor | the engine picker, previewing the next preset (§7); outside the editor PRESETS loads at once |

While the editor is open the root keys still play (audition, through the part being edited), OCT− / OCT+ still
shift their octave, OCT− + OCT+ is still panic, PERF works as outside (tap on/off, hold = the perform layer), and
MASTER is the volume. The chord block still works.

**Layers over the editor.** A layer opened while the editor is open (PERF held: the perform layer; EDIT held: the
engine picker) closes back **to the editor, as it was** (group, screen, lane):
OCT− or HOME closes the layer, not the editor. HOME with no layer open still leaves the editor.

## 3. Navigation: groups → screens → lanes

- A **group** is one of the seven sections (OSC FILT ENV LFO MOD FX MIX), each on its own button.
- A group has one or more **screens** (OSC: the oscillators, their "+" parameters, the mixer; ENV: ENV 1–4).
- A screen has one or more **lanes**: the four parameters on KNOB 1–4 at a time (a row, an oscillator, a matrix slot).
  The active lane is drawn in the knob colours with the bars under it.

| Gesture | Result |
| --- | --- |
| a group's button, from another group | that group, at its remembered screen and lane |
| the current group's button again | the group's next **screen** (wraps); the lane is kept where the new screen has it (OSC 3 stays OSC 3) |
| a group's button **held** | nothing (reserved) |
| **SELECT** forward | the next **lane**; past the screen's last lane, the next screen's first lane (and past the group's last screen, its first: it wraps) |
| **SELECT** back | the previous lane; before the first, the previous screen's last lane |
| KNOB 1–4 | the active lane's four cells |

So SELECT alone reaches everything in a group: on the VA's ENV, SELECT runs ENV 1 A → ENV 1 B → ENV 2 A → … → ENV 4 B
→ ENV 1 A.

### The button map

The function buttons become **groups**. Top row = the signal flow (OSC FILT ENV LFO), bottom row = the output
stage (MOD FX MIX).

| Printed | Outside | In the editor | Tap | Hold (past HOLD_MS) |
| --- | --- | --- | --- | --- |
| FX | FX | **OSC** | the group; again: the next screen | — (reserved) |
| SEL | KEY | **FILT** | as OSC | — |
| ENV | BASS | **ENV** | as OSC (the next envelope) | — ; **held + KNOB 1–4: the quick mapping** (§13) |
| LFO | LATCH | **LFO** | as OSC | — ; **held + KNOB 1–4: the quick mapping** (§13) |
| SEQ | SEQ | **MOD** | as OSC | — |
| PLAY | LOOP | **FX** | the group (one screen) | — |
| REC | REC | **MIX** | the group (one screen) | — |
| GLO | OPT | **SHIFT** | latch / unlatch SHIFT (§6) | momentary SHIFT while held; held + EDIT: chord ↔ bass sound |
| EDIT | EDIT | **EDIT** (blinks) | leave the editor | the engine picker (§7) |
| HOME | HOME | **HOME** | leave the editor (group, screen and lane remembered); in a layer: back to the editor | — |
| SAVE | SAVE | **SAVE** | the save dialog (§8) | the loop slots, as outside |
| ARP | PERF | **PERF** | performance on / off | the perform layer (closes back to the editor) |
| OCT− / OCT+ | octave | octave (audition) | — | both: panic; **OCT− held + KNOB 1–4: that parameter's modulation cleared** (§13) |

### The rotaries

| Rotary | In the editor |
| --- | --- |
| KNOB 1–4 | edit the four cells of the active lane. A value is **accelerated** (§6a: a slow detent the finest step, a quick one up to 5 % of the range), enums and named choices **one by one**; SHIFT on = **snap** (§6a). Ranges, defaults and value text are the platform's (`params.c`, `param_format`) or the deep page's (`eng_page_t` formats) |
| SELECT | the lane, across the screens (above) |
| PRESETS | the engine picker, previewing (§7) |
| MASTER | volume |
| ALGORITHM | the bass sound, as outside |

## 4. Groups per engine class

The screens are built from the engine's deep pages (`eng_deep_t`, docs/VA.md): its five sections (OSC FILTER ENV
LFO MOD; or the first four: an engine without a MOD section, FM TONE (engine 15, `eng_quad.c`, code name QUAD), has
the platform's MOD routes) and the page titles,
never the column names, so a column or a page the engine adds shows up by itself, unless the engine gives a **screen
plan** (`eng_deep_t.screens`, `eng_screen_t` in core.h: FM TONE's, below): then each of its groups is the plan's screens
in order, each one or two pages (rows A, B) under a band (`ENG_B_*`: none, the AHDSR, the filter, the filter with the
base-width window, the algorithm, two attack-decay-end envelopes), with its title (and the active row's letter: `SYN 1 ·
A`), each cell's label and style (`ENG_C_*`: the rule below, a big number, a ratio fraction, the harm / detune glyphs, a
number with a small bar, the text, a knob, a bar, a wave + phase span cell). The plan only adds: VA, FM6 and CZ-1 have
none and keep the title rules:

- a page titled with an instance number (`OSC 2`, `OSC 2+`, `ENV 3`, `LFO 1`) belongs to that instance; pages whose
  titles differ only in the number are one **kind** (`OSC n`, `OSC n+`; FM6's `OP n` and `SCALE n` are two kinds);
- a stack has a row per instance, up to eight (FM6's six operators); a page with no number and its `+` page (`LFO` /
  `LFO+`, FM6's `FUNC` / `FUNC+`) are one `edit8` screen of two lanes;
- OSC and LFO: one **stack** screen per kind (lane n = instance n); a page with no number whose labels end in a
  digit (`LFO SYN`: SYNC1..SYNC4) is a stack whose lane n is its column n; any other page with no number (`VOICE`)
  is a one-lane screen of its own. A column some lanes lack reads `–`. The headings are the columns' long labels
  (two kinds in one column: `Sync/Ring`);
- OSC then has the **mixer** (when the oscillator pages have a LEVEL column and there are at most four of them);
- FILT and ENV: an `edit8` screen per instance, its pages as lanes A and B (two a screen) under the wide band: the
  filter curve when the FILTER pages have a CUT column (else no band, the page's title on the right: FM6's ALGO), the
  AHDSR (a REL column of format `F_TIMEI` at its top, FM TONE's INF: the release drawn flat to the band's right edge,
  the cell reads "INF"), the **dx** band when the ENV page's columns are R1..R4 (FM6), or the **cz** band when the run of pages
  (one name and instance: `DCW 1`, `DCW 1+`, `DCW 1 B`, ..) has R1.. and SUS / END (CZ-1); FILT and ENV pages run by
  instance and name (`PITCH 1` and `DCW 1` are two runs);
- MOD: a stack of the slots, eight a screen, when its pages are matrix slots (SRC DST AMT); else (FM6's function
  pages) `edit8` screens as FILT's. **Then, on every engine, the EXPR screen** (0.22, docs/VELOCITY.md
  "Expression"): a stack of two lanes, VEL and AT, each Source · Dest · Amount, the sound's velocity and
  aftertouch routings (mod.c's matrix slots 3 and 4, saved with the sound; the destinations are the platform
  matrix's: PITCH CUT SHP AMP PAN the sends RATE VIB E1..E8). An unset slot reads its default, `AMP auto` /
  `CUT auto` (what Key Touch Dynamic / Velocity + Aftertouch does for a sound that routes nothing); KNOB 2 or 3
  turned on it takes the default (DST AMP amount 63; DST CUT amount by Aftertouch Sens) and then the turn, and
  DST `OFF` switches that default off for the sound. MIDI velocity and aftertouch reach the same sources. The
  amounts read as numbers, -64 .. +63 (not the percentage the platform's bipolar amounts show elsewhere), so a
  detent reads as one; they step as every value does (§ "Knob acceleration and OPT snap": one unit a slow detent,
  up to six a quick one).
- a cell's label, value names and range come from `deep->desc(t, page, col)` when the engine gives one (the VA's
  mode-dependent WAVE / SHAPE: MORPH, NOISE, COLOR, DENS), else from the page's column; the stack's heading over a
  WAVE column stays `Wave` whatever the modes.

### VA

| Group | Screen | Kind | Lanes | KNOB 1–4 | Top-right text |
| --- | --- | --- | --- | --- | --- |
| OSC | 1 | `stack`, 4 rows | OSC 1–4 | Wave · Level · Coarse · Fine (pages `OSC n`) | `OSC 2 · A` |
| OSC | 2 | `stack`, 4 rows | OSC 1–4 | whatever the `OSC n+` pages publish (Mode · Shape · Key trk · Sync/Ring today; `–` where an oscillator has none) | `OSC 2 · B` |
| OSC | 3 | `edit8` `tall`, one row | the mixer (one lane) | the LEVEL of OSC 1 · 2 · 3 · 4, four tall bars | `OSC · MIX` |
| FILT | 1 | `edit8`, wide filter curve | A, B | A: Cutoff · Reso · Ftype · Env amt (`FILTER`: CUT RES FTYPE FENV); B: the `FILTER+` page (Key trk · (blank) · Spread · Drive) | `FILTER` |
| ENV | 1–4 | `edit8`, wide AHDSR of that envelope | A, B | A: Attack · Decay · Sustain · Release (`ENV n`); B: Hold (· Velocity on ENV 1) (`ENV n+`) | `ENV 1 · amp`, `ENV 2 · filter`, `ENV 3 · free` |
| LFO | 1 | `stack`, 4 rows | LFO 1–4 | Rate · Wave · Depth · Fade | `LFO 1 · A` |
| LFO | 2 | `stack`, 4 rows | LFO 1–4 | Sync (`LFO SYN`, column n) | `LFO 1 · B` |
| LFO | 3 | `edit8`, one row | one (the `VOICE` page, the LFO section's last: a page of its own) | Uspread (USPREAD, the unison's stereo spread: docs/VA.md); KNOB 2–4 blank | `VOICE` |
| MOD | 1 | `stack`, 8 rows, text only | slots 1–8 | Source · Dest · Amount (bipolar; hundredths, `+0.30` `+12.00`: tenths below 6, whole numbers from 6, docs/VA.md "matrix") | `MOD 3` |
| FX | 1 | `edit8`, one row | one | Drive · Chorus · Delay · Reverb (the platform sends) | `FX` |
| MIX | 1 | `edit8`, two rows | A, B | A: Level · Pan · Voice · Glide; B: Transpose · Detune · Priority · Gl. mode (Glide: the format `F_GLIDE`, 3 ms .. 30 s, the editor protocol's fmt 20; issue 46) | `MIX` |

With Sync on, an LFO's Rate is the sync list (`deep->desc`: `8BARD` .. `1/8` .. `256T`, `4ms` `3ms` `2ms`; one item a
detent, SHIFT / OPT the straight ones: docs/VA.md). The VA's EDIT 1 / EDIT 2 macros are not shown:
the deep values are the truth and a deep edit writes the macro back. Unused matrix slots read `–` (the active one
shows its source, to turn).

### Engines without deep pages (WHEEL; until 0.18 also PHASE, LOFI, VOICE, TRIO, NOISE, PHYS)

| Group | Screen | Lanes | KNOB 1–4 | Top-right text |
| --- | --- | --- | --- | --- |
| OSC | `edit8`, one row | one | EDIT 1: `P_E0`–`P_E3` | the engine's `page_title[0]` (`BARS` on WHEEL) |
| FILT | `edit8`, one row | one | EDIT 2: `P_E4`–`P_E7` | `page_title[1]` (`FLT`, `PATCH`) |
| ENV | `edit8`, one row under the wide AHDSR | one | Attack · Decay · Sustain · Release (`P_ATK P_DEC P_SUS P_REL`) | `ENV` |
| LFO | `edit8`, one row | one | Rate · Wave · Vibrato · Wah (`P_LRATE P_LWAVE P_LD_PIT P_LD_FLT`) | `LFO` |
| MOD | `stack`, 4 rows | 4 routes | Source (fixed text) · Dest (fixed) · Amount (`P_ED_FLT P_ED_PIT P_ED_SHP P_LD_AMP`) | `MOD n` |
| FX, MIX | as the VA | | | |

A one-screen, one-lane group's tap and SELECT do nothing.

### Engines with their own envelopes (`engine_t.ownenv`)

- **VA**: its own ENV 1–4 (above); ENV 1 is the amplitude.
- **FM6**: its operator envelopes and pitch EG are its deep ENV pages (below), under the dx band.

### FM6 (docs/FM6.md)

| Group | Screen | Kind | Lanes | KNOB 1–4 | Top-right text |
| --- | --- | --- | --- | --- | --- |
| OSC | 1 | `stack`, 6 rows | OP 1–6 | Level · Coarse · Fine · Detune (`OP n`) | `OP 2 · A` |
| OSC | 2 | `stack`, 6 rows | OP 1–6 | Mode · Vel sens · AM sens · Rate scl (`OP n+`) | `OP 2 · B` |
| OSC | 3 | `stack`, 6 rows | OP 1–6 | Break pt · L depth · R depth · Curve (L/R in one, `-L/+E`) (`SCALE n`) | `SCALE 2 · C` |
| FILT | 1 | `edit8`, no band, one row | ALGO | Algo · Feedback · Transp · Osc sync | `ALGO` |
| ENV | 1–6 | `edit8`, wide **dx** envelope | A, B | A: Rate 1–4 (`ENV n`); B: Level 1–4 (`ENV n+`) | `ENV 1` |
| ENV | 7 | `edit8`, wide dx envelope (a centre line at 50) | A, B | the pitch EG's rates / levels | `PITCH EG` |
| LFO | 1 | `edit8`, two rows | A, B | A: Speed · Delay · PM depth · AM depth; B: Wave · Sync · PM sens | `LFO` |
| MOD | 1 | `edit8`, two rows | A, B | A: Bend · Porta · Engine; B: Step · Porta md · Gliss · DX vel (the function settings) | `FUNC` |
| MOD | 2 | `edit8`, two rows | A, B | A: Wheel · W.dst · Foot · F.dst; B: Breath · B.dst · Aftertch · A.dst | `CTRL` |

The **dx** band (`CR_W_DX`, cr_draw.c): from L4 to L1 at R1, to L2 at R2, to L3 at R3, held at L3, to L4 at R4; a
segment's width grows with its distance and the slowness of its rate; the segment the turned cell ends (R k / L k: k)
thick in its knob's colour, digits 1–4 under the segments. COARSE of a FIXED operator reads as its decade (1Hz ..
1kHz). FM6 has no matrix: the quick mapping says `not modulatable`. Tests: `tools/emu/scripts/cr_fm6.txt`.

### CZ-1 (docs/CZ1.md)

| Group | Screen | Kind | Lanes | KNOB 1–4 | Top-right text |
| --- | --- | --- | --- | --- | --- |
| OSC | 1 | `stack`, 2 rows | LINE 1, 2 | Wave · Wave · Window · Level (`LINE n`) | `LINE 1 · A` |
| OSC | 2 | `edit8`, one row | DETUNE | Sign · Oct · Note · Fine (line 2's detune) | `DETUNE` |
| OSC | 3 | the mixer | the two lines' LEVEL | | `OSC · MIX` |
| FILT | 1 | `edit8`, no band, two rows | A, B | A: DCW key 1 · DCW key 2 · DCW vel 1 · DCW vel 2; B: DCA key 1 · 2 · DCA vel 1 · 2 | `DCW` |
| ENV | 1–18 | `edit8`, wide **cz** envelope | A, B | per envelope (PITCH 1, DCW 1, DCA 1, PITCH 2, DCW 2, DCA 2) three screens: Rate 1–4 / Level 1–4; Rate 5–8 / Level 5–8; Sustain · End (PITCH: · Pitch vel) | `DCW 1 · 1-4`, `· 5-8`, `· END` |
| LFO | 1 | `edit8`, one row | VIB | Wave · Delay · Rate · Depth | `VIB` |
| MOD | 1 | `edit8`, one row | TONE | Line · Mod (OFF RING NOISE) · Oct | `TONE` |

The **cz** band (`CR_W_CZ`, cr_draw.c): the eight steps from 0 to L1 at R1 .. L k at R k, the END step to 0, a dashed
hold after the SUS step, nothing after END; the step of the turned cell (R k / L k; SUS / END: their step) thick in its
knob's colour, digits 1–8 (and S) under the steps. No matrix (`not modulatable`). Melodee's CZ TOOLS page (NAME, 1 > 2,
2 > 1, COMPARE) is not ported. Tests: `tools/emu/scripts/cr_cz.txt`.

### P5 (docs/PROPHET.md)

The owner's rows (0.22 in development; engine 16, Melodee's Prophet-5; the code's name is PROPHET), **from a design
file**: `design/layouts/designs/p5-rows.json`, laid out by the owner in the layout tool ("This layout now checks out, I
updated the label text in editor. We are good to build and get into the firmware.") and compiled into the build
(`tools/gen_layout.py` `BUILD` -> `build/gen/cr_layouts.h` `CR_LAYC`; docs/LAYOUT-TOOL.md "Step 4"). It is a **screen
plan** as FM TONE's (§4 below), with pages of its own: `cr_pages.c` `cp_deep` serves P5 seventeen pages, one a lane,
each column a copy of the built-in descriptor it binds, and `get` / `set` / `desc` go through to that column's place in
P5's own 19 pages (the program's byte, the blob, SysEx, `cr_backup` and the layout file's hash are the engine's,
unchanged). Eleven `edit8` screens of up to four cells a row, each cell the Prophet-5's control as its glyph (the p5
cell styles, §9 "A cell": the ticked knob, the LED button with its wave's icon, two LEDs, the lever) and its label as
the owner wrote it; a screen under a band has one lane, drawn at full size under the band; the others two lanes, no
band. SELECT walks the rows and the screens of a group, KNOB 1..4 the active row's cells (blue, orange, white, green);
an empty slot (–) does nothing. MOD's last screen is EXPR after VOICE (a stack, as on every engine), FX and MIX are the
platform's, as for every engine:

| Group | Screen | Band | Lanes | KNOB 1–4 | Top-right text |
| --- | --- | --- | --- | --- | --- |
| OSC | 1 | none | A, B | A: FREQ (knob) · SAW (button, saw icon) · PULSE (button, pulse icon) · P WIDTH; B: SYNC (button) · MIX A · MIX B · NOISE (the mixer) | `OSC A` |
| OSC | 2 | none | A, B | A: FREQ · SAW · TRI · PULSE (buttons with their icons); B: FINE · P WIDTH · LO FREQ · KEYB (buttons) | `OSC B` |
| FILT | 1 | the filter's curve | one | CUTOFF · RES · ENV AMT · REV (two LEDs: 1/2, 3) | `FILTER` |
| FILT | 2 | the envelope (ENV 2) | one | ATTACK · DECAY · SUSTAIN · RELEASE | `ENV 2 · filter` |
| FILT | 3 | none | one | VELOC (button) · – · KEY TRK (a lever: Off, Half, Full) · AFTER (button: the filter's aftertouch) | `ENV 2 +` |
| ENV | 1 | the envelope (ENV 1) | one | ATTACK · DECAY · SUSTAIN · RELEASE | `ENV 1 · amp` |
| ENV | 2 | none | one | VELOC · REL SW (buttons) · – · – | `ENV 1 +` |
| LFO | 1 | none | A, B | A: FREQ · SAW · TRI · SQUARE (buttons with their icons); B: INIT AMT · AFTER (button) · – · – | `LFO` |
| LFO | 2 | none | A, B | A: SRC MIX (the LFO's noise amount: the wheel's source mix) · FREQ A · FREQ B · PW A; B: PW B · FILTER (the wheel's destinations, buttons) · – · – | `WHEEL` |
| MOD | 1 | none | A, B | A: FILT ENV · OSC B (the two amounts) · FREQ A · PW A; B: FILTER (the destinations, buttons) · – · – · – | `POLYMOD` |
| MOD | 2 | none | A, B | A: UNISON (button) · VOICES · DETUNE · GLIDE; B: PRIORITY (Low, Low R, Last, Las R) · VINTAGE · – · – | `VOICE` |
| MOD | 3 | — | VEL, AT | the platform's stack: VEL / AT · Dest · Amount | `EXPR · VEL`, `EXPR · AT` |

A cell is one byte of the program, turned by the editor's own knob code (acceleration, OPT snap, SHIFT, the hot block
for 800 ms, the modulation marks); the switches read On / Off, the knobs the stored 0..127 (Voices 1..4: the engine's
four voices; Unison also switches the part's voice mode). No matrix of its own ("not modulatable"). The editor's trace
names the built-in page and column ("deep: part 0 page 5 FILTER col 0 CUT"). Before (0.22-beta1 and lay-5 .. lay-7):
the owner's panel `p5-panel.json` as free screens (`CR_K_FREE`, §9), which the unit no longer carries; the layout tool
still previews it. Tests: `tools/emu/scripts/cr_prophet.txt` (FILTER KNOB 1 the cutoff, LFO KNOB 1 FREQ and KNOB 2 the
saw's button, OSC A's row B KNOB 2 Mix A), `tests/cr_p5rows_test.c` (every cell, empty slots, SELECT, EXPR after
VOICE), `tests/cr_layout_test.c` (the tables), `tests/run_cr_draw.sh` (`tests/cr_p5rows_golden.txt`),
`tests/cr_prophet_test.c`, `web/test_layout.mjs` (the unit's frames against the emulator's, and against the design
loaded in the layout tool).

### FM TONE (docs/QUAD.md; the user-approved screens design/choralroot-fm1-quad-screens.png)

Built from FM TONE's screen plan (`eng_quad.c` `QUAD_SCREENS`), not from the title rules:

| Group | Screen | Kind | Lanes | KNOB 1–4 | Top-right text |
| --- | --- | --- | --- | --- | --- |
| OSC | 1 | `edit8`, the **algo** band (`CR_W_ALGO`: algorithm, feedback, mix) | A, B | A: Algo (1–8, the Digitone manual's eight: the band draws its diagram, a direct carrier's line dotted, an enveloped one solid) · Ratio C · Ratio A (big numbers) · Ratio B (the fraction B2/B1: the fast hand B2 on top); B: Harm (harm glyph, −26..+26) · Dtune (detune glyph) · Feedback (bar) · Mix (bipolar, −64..+63) (`SYN 1`, `SYN 1+`) | `SYN 1 · A` |
| OSC | 2 | `edit8`, one row | one | Offset C · Offset A · Offset B1 · Offset B2 (`SYN 2`) | `SYN 2` |
| FILT | 1 | `edit8`, the filter band | A, B | A: Attack · Decay · Sustain · Release (the filter envelope); B: Freq · Reso · Type (Off · LP12 · HP12 · LP24: the band flat for Off, twice the slope for LP24) · Env depth | `FILTER · B` |
| FILT | 2 | `edit8`, the filter band with the base-width window | A, B | A: Env delay · Key track; B: Base · Width | `FILTER 2 · A` |
| ENV | 1 | `edit8`, the **ade2** band (`CR_W_ADE2`: A's and B's curves, level bars; the turned cell's segment lit) | A, B | A: A Attack · A Decay · A End · A Level; B: the same for B | `ENV A/B` |
| ENV | 2 | `edit8`, no band | A, B | A: A Delay · A Trig · A Reset · Phase (PHRT, text: Off · All · C · A+B · A+B2); B: B Delay · B Trig · B Reset · Velocity | `ENV 2` |
| ENV | 3 | `edit8`, one row | one | A Key · B1 Key · B2 Key (the operators' key scaling, three cells) | `ENV 3` |
| ENV | 4 | `edit8`, the AHDSR band (FM TONE's own amp envelope) | A, B | A: Attack · Decay · Sustain · Release; B: Level · Pan · Drive (the part's P_PAN and P_DIST; the Drive reads 0..127 and is the sound's: saved and loaded with an FM TONE preset, not gated by FX off, an edit of the sound: docs/FX.md "FM TONE's Drive") | `AMP · A` |
| LFO | 1–3 | `edit8`, no band, one screen an LFO | A, B | A: Speed (a bipolar knob, −64..+63) · Mult (text: 1 .. 2k synced, F1 .. F2k at a fixed 120 BPM) · Fade (bipolar, −64..+63: below 0 fades in, above fades out) · Dest; B: **Wave · Phase** (one span cell: the wave drawn from its start phase, "Tri · 90", degrees; KNOB 1 the wave, KNOB 2 the phase) · Trig · Depth | `LFO 1 · A` |
| MOD | 1 | the platform's routes (as an engine without deep pages) | 4 | | `MOD n` |

The numbers are the values as stored (Attack "12", Reso "30"), as the mock-ups write them; Freq "2.1k"; on / off and
the enums title-cased ("On", "Free", "Harm"; "LP" stays). The ratios step one value a detent (Ratio A 0.25, Ratio B the
next pair). The cells after a span cell move a place left (`CR_CF_SPAN2`: the hot cell is the cell's index). No matrix:
the quick mapping says "not modulatable". Tests: `tools/emu/scripts/cr_quad.txt`.

### GRAIN (docs/GRAIN.md; 0.24-beta2)

A plan for OSC and ENV, the title rules for the rest (`eng_grain.c GRAIN_SCREENS`). OSC: `SRC` (lanes `SRC 1`, `SRC 2`:
TYPE TONE MOTION SEED) and `GRAIN` (lanes `GRAIN`: SIZE DENS POS SPRD; `GRAIN+`: MORPH RAND PITCH SCAN), both under
the **wave band** (`ENG_B_WAVE`, `cr_draw.c cr_wave_band`: the active source's envelope on SRC; on GRAIN the mix the
grains read, A's and B's envelopes weighted by MORPH in a colour from A's blue to B's orange (3y167), POS, SPRD and
the live grains in one colour). FILT: `FILTER` (CUT RES FTYPE DRIVE) and `FILTER+` (KTRK) under
the VA's filter curve. ENV: `ENV 1 · amp` (the track's ATK DEC SUS REL) and `ENV 2 · mod`, each under the AHDSR. LFO:
the stack `LFO 1`, `LFO 2` (RATE WAVE DEPTH FADE) and `LFO SYN` (SYNC1 SYNC2; a synced RATE reads the VA's list). MOD:
the matrix's stack of eight (SRC DST AMT, the VA's fine amounts), then EXPR. The quick mapping works (`grain_mod_dst`).
Tests: `tools/emu/scripts/cr_grain.txt` (eleven screenshots), `tests/cr_grain_test.c`.

ENGINE, the factory presets and INIT are **not groups**: they are the engine picker (§7).

## 5. Memory

- Each **part** (chord, bass) remembers its current group.
- Each **group** remembers its screen and lane.
- Leaving the editor (EDIT, HOME) and coming back, switching part with SHIFT + EDIT, or a layer opened and closed
  over the editor, returns to the same group, screen and lane. An engine with fewer screens or lanes brings them into
  range.
- In RAM only; the first entry after power-on is OSC, screen 1, lane 1. Traces: `edit: group OSC screen 2 lane 3 part 0`.

## 6. SHIFT

GLO (printed; OPT outside) is SHIFT in the editor:

- **tap** = toggle the **latched** SHIFT: its LED lit, the word `snap` small in the title line, KNOB 1–4 snap (§6a)
  until tapped again (or the editor is left);
- **held** past HOLD_MS (or with a knob turned meanwhile) = **momentary**: snap while held; released, back to the
  latched state (a hold never toggles it);
- held + EDIT = chord ↔ bass sound.

A **ratio pair** (a column of k names over k x k values, the value slow × k + fast: FM TONE's RATIO B, B1 × 19 +
B2, the Digitone's hands) takes SHIFT as **its slow hand a step** (FM TONE's B1: B2 kept, B1 held at its first and
last step) instead of a snap: a plain detent already moves the fast hand (B2) one step, carrying into B1 on the
wrap. The title line then reads `snap · B1` (`cr_screen_t.fine` 2: the active lane has a pair). The pair's cell is a
fraction, the fast hand **on top** (B2 over B1), each with two decimals (the cell's `pct` / `pct2` in quarters,
cr_draw.c formats them); the trace and the value text read "B2/B1" ("2.00/1.00": B2 2.00, B1 1.00).

While the editor is open the edit wins over OPT's outside knob functions (split point, metronome level, bass volume).

Until 0.22 SHIFT was **fine** (one unit a detent against the plain 5 %); acceleration makes the plain detent fine
when turned slowly, so SHIFT is the snap now.

## 6a. Knob acceleration and OPT snap (0.22)

The owner (2026-10-14): "a small turn does fine increments, a quick turn moves the parameter faster", and hold OPT
to "do the same thing the real Digitone does when holding Func, and snap to common values". One implementation for
every knob that edits a **value**: the sound editor of every engine (FM6, FM TONE, VA, CZ-1, P5, WHEEL, GRAIN and the
platform pages), its FX group (the sends), the FX layer, the BASS layer's level and BASS + ALGORITHM, the KEY layer's
Transpose, the PERF row and the main view's KNOB 3 / KNOB 4, Options > Velocity (`cr_pages.c` `kn_*`, `cr_ui.c`
`cu_kval`). **Not** values, one item a detent whatever the speed: PRESETS, ALGORITHM, SELECT, the pickers and lists,
every F_ENUM, on / off, a note name, a name list of at most 64 values (FM6's ALG and COARSE, FM TONE's RATIO A / C),
a ratio pair (RATIO B), FM TONE's ALGO, the FX layer's divisions / ratio / sync / mode, the PERF row's division,
direction, pattern, rotate, retrigger and hold, the VA's synced LFO Rate (va-1: the sync list, 42 items).

**A turn's first two detents are always the finest step** (2026-10-15; `kn_turn` `KN_FIRST`, the owner on the EXPR
amounts: "I cannot move it in small steps of 1"). The unit reads a knob once a main-loop pass, and two detents of a
hand turn that land in one editor redraw read as a quick turn (16 ms / 2, whatever the pass took) and stepped a
128-range value by six. So a nudge, or the first two clicks of any turn (after a rest of a second or a reversal), count
one, a spin accelerates from its third detent on (a flick of three in one pass: one, one, then the speed's step), and
detents that arrive together count their mean spacing over the pass they arrived in (two in a 38 ms redraw: 19 ms
apart, quick; two in a 100 ms pass: 50 ms, one unit each); `tools/emu/scripts/cr_expr_knob.txt` times detents like a hand (90 ms), like a spin (20 ms)
and two in one frame.

**Audited (va-1, 2026-10-14: the owner "acceleration works well some places but not others").** Every value knob of
the VA's editor (OSC, FILT, ENV, LFO, MOD, the ENV / LFO-held mapping's AMT) and of the common groups (FX: the sends;
MIX: Level, Pan, Glide, Transpose, Detune; the platform pages) steps through `cp_dstep` / `cu_send_step`, so all
accelerate. The places that did not feel right were the VA's: a synced LFO Rate was a 0..127 value read as 14
divisions (accelerated, so a slow detent often showed no change and a quick one skipped divisions): it is a list now;
FENV was −64..63 in % of 64: hundredths now. Still one step a detent outside the editor, for the owner to confirm:
the tempo (SELECT on the main view, 20..300 BPM, 1 BPM a detent), the PLAY menu's Level and the metronome's click
(0..100 in fixed 5s), the PERF row's Rotate (−12..12), the KEY layer's Transpose (by choice), the FX layer's values of
at most 21 steps (`hi − lo > 20`).

**Acceleration.** `cu_knob` times every detent of each knob (`kn_turn`, the UI clock `fm1_ms`); the gap since the same
knob's previous detent picks the step from ONE table (`KN_CURVE`), in 64ths of the quickest step, which is the range
/ 20 rounded (the detent of before 0.22: 6 of 0..127, 5 of 0..99, 10 of FM TONE's ±1.00, 6.35 of a hi-res 0..127.00):

| gap since the knob's last detent | step |
|---|---|
| ≥ 150 ms (and the first detent, after 1 s of rest, a reversal) | the finest: one stored step (0.01 of a hi-res value, 1 of an integer) |
| 110–149 ms | 1/64 of the quickest (at least the finest) |
| 80–109 ms | 2/64 |
| 60–79 ms | 4/64 |
| 45–59 ms | 8/64 |
| 34–44 ms | 16/64 |
| 25–33 ms | 32/64 |
| < 25 ms | the quickest: the range / 20 (several detents in one pass count their mean spacing over the pass: two in a 38 ms redraw are 19 ms apart, two in a 100 ms save 50) |

So a slow turn is fine, and a quick spin of about a turn (24 detents) crosses the range. Clamped: both ends are
reached exactly. The emulator's scripts set the timing exactly (their clock is the firmware's): `knob KNOB1 +1`,
`wait 300` is a slow detent, detents a frame (15 ms) apart quick ones, `knob KNOB1 +5` five in a frame (quick);
`knobs quick` makes every detent the quickest (the scripts written for the 5 % step, tools/emu/emu.c).

**OPT snap.** GLO (OPT) held while a value knob turns, and in the editor SHIFT latched too: the value goes to the next
**target** in the turn's direction, one a detent (`kn_snap`, `kn_grid`). The targets are the multiples of the grid
from the anchor, and both ends:

| the value | targets |
|---|---|
| hi-res (1/100 units: FM TONE's F_C100 / F_O100 / F_C100I / F_FRQ100, core.h `FMT_HI`; a value named by its number with decimals: FM TONE's OFS "-1.00" .. "+1.00") | whole numbers (`kn_whole`: 100 steps) |
| an LFO speed (a bipolar SPEED: FM TONE's LFO 1–3) | −64 −32 −16 −8, 8 16 32 64 (the ends past them), × 100 when hi-res |
| semitones (F_SEMI, an offset in "st": the VA's COARSE, the part's Transpose, FM6's TRNSP; the KEY layer's Transpose) | octaves (12, around the centre) |
| the DX7 break point (FM6's BREAK) | the Cs |
| 0..127 | 0 16 32 48 64 80 96 112 127 |
| centred −64..63 (F_BIPCT, F_OFS: Pan, the envelope / LFO amounts, MIX, FADE, the VA's FINE) | 0 then steps of 8 (−64 .. 56 63) |
| another range | the nice number (1 2 4 5 8 10 16 20 25 32 50 64 100 ..) nearest to 1/8 of it (centred: 1/16): 0..99 → 10s, 0..100 → 10s, −50..50 → 5s, 0..120 → 16s, 1..1000 → 100s, 1..200 → 25s, 50..90 → 5s, −16..16 → 2s |
| at most 17 values (FM6's FB, DETUNE, VSENS; CZ-1's levels and keys; ALGO 1..8; −8..8) | no snap: one step a detent |

A named choice never snaps (one item a detent, OPT or not), but for a list of tempo divisions (the VA's synced LFO
Rate, `kn_divs`: an F_ENUM `RATE` with a `1/4T` among its names): OPT steps the straight ones (a name ending in T or D
is skipped). The VA's FENV and its matrix's AMT are hi-res (`F_FIX2`): whole numbers. OPT's other jobs are as before: OPT + PRESETS / +
ALGORITHM change the chord / bass part's engine, OPT + KNOB 1 the split point and OPT + SELECT the click level (also
in a layer: KNOB 1 of a layer cannot snap), OPT + SAVE, OPT tapped the Options; in the editor SHIFT + EDIT the other
part and SHIFT + SELECT the lanes. Only OPT + a value knob changes meaning.

The hook for hi-res values is `kn_whole` (cr_pages.c), the one place that asks: a format in `FMT_HI` (FM TONE's
1/100 formats) is 100 steps a whole number; another way to declare a hi-res column goes there.

## 7. The engine picker: a preview (EDIT held, or PRESETS in the editor; main walk-through state 22)

On opening, the picker **snapshots** the part's sound: every parameter, the engine, the deep patch (`deep->blob_get`,
the VA's), the `edited` flag and the pool entry it came from (its place in the pool). Then, as before:

- the white root keys are the engines in the firmware's order (FM6, FM TONE, VA, CZ-1, WHEEL: D4..A4, five
  engines since 0.18, a root each; PHASE, LOFI, VOICE, TRIO, NOISE, PHYS (retired in 0.18), ANALOG (retired in 0.14), SAMPLE and DRUM are not in
  the all-synth firmware). A root switches the sound's engine, keeping its envelope and sends;
- KNOB 1 (and PRESETS) steps the engine's **pool**, the same list PRESETS turns outside the editor (docs/PRESETS.md: 00
  INIT, the factory presets, each replaced by the user's record bound to it, the user's added presets), loaded for
  preview; the meter is PRESETS' ("05 / FM PAD / FM6 · 05/25"), its bar **jumps** to the value (no fill animation,
  that popup only). There are no separate "templates": the factory presets are the pool's;
- KNOB 2 inits the sound (the pool's 00);
- **KNOB 4** switches the roots' job: `roots: engines` (they pick engines) or `roots: play` (they play the previewed
  sound; engines are then chosen with SELECT only). The footer shows it. **0.18: the roots always play there** (the owner:
  "I don't see any case in which I would want to scroll through presets and not be able to immediately play them");
  KNOB 4 has no job in the picker, the footer reads `SELECT: engine`, the byte is reserved. **0.22: KNOB 4 marks the
  previewed sound as a favourite** (right on, left off; the read-out pops with its star, the browser stays;
  docs/PRESETS.md "Favourites"; the footer is unchanged). Until 0.18: a setting (`pick_roots`,
  docs/SETTINGS.md), default engines.

Closing it:

| Gesture | Result |
| --- | --- |
| **OCT−** (or HOME) | **cancel**: the snapshot back (parameters, engine, patch, flags), back to the editor if it was open, else the view |
| **OCT+** or **EDIT tap** | **confirm**: what is loaded stays, as a fresh load (not edited) |
| another layer's hold, OPT, SAVE | confirm, as OCT+ |

Traces: `picker: open part 0 LUSH PAD crc 1a2b roots engines`, `picker: cancel part 0 -> LUSH PAD crc 1a2b`,
`picker: keep part 0 WARM PAD crc 3c4d`, `picker: roots play`.

## 8. Save (SAVE tap; main walk-through state 23)

The same dialog as outside (docs/PRESETS.md; preset sheet 3, 4, 6). First a two-item picker (KNOB 1 or SELECT):
**Overwrite** (the current preset; its name under it, `*` when edited) or **Save as new**; the default is Overwrite on a
user preset or an edited sound, Save as new otherwise. **SAVE again or OCT+ takes it**, **OCT− or HOME cancels**.
Overwrite saves at once and keeps the name: over a factory preset it writes a user record bound to it (the factory
one stays recoverable), over a user preset its slot, on INIT (which cannot be overwritten) it is Save as new.
Overwrite never renames (a new name: Save as new, then delete the old one). Save as new takes the first free user slot,
which is the engine's pool's next place ("FM6 · 27"), then the naming page: the white roots type the name (phone
style, prefilled with the current one), D#4 a space, **F#4 deletes** the letter under the cursor, SELECT the cursor and KNOB 1 the letter under it (0.22; until then KNOB 2 the last letter; docs/PRESETS.md "The naming page"), SAVE or
OCT+ saves. No free slot (32 user presets): "no free slot". **SAVE held 1 s in the dialog**: "reset to factory?" on an
overwritten factory preset, "delete?" on an added one, the place huge in red (OCT+ does it, OCT− keeps); after a reset
the factory preset is back at its place, after a delete the pool closes up. EDIT keeps blinking; the dialog returns to
the editor view. On the bass part it saves the bass sound (into the bass part's engine's pool, on ALGORITHM); on the
chord part the chord sound (PRESETS). A VA sound saves its patch with it (`va_store.c`), an FM6
sound its voice and function settings (`fm6_ustore.c`, docs/FM6.md), a CZ-1 sound its tone (`cz_ustore.c`, docs/CZ1.md).

## 9. Screens

### The top line (both kinds, y 0–24)

Left: the sound's name in 13 px bold, white; a trailing **`*`** once edited (deep edits too); on the bass part
` · BASS` and the whole title in **orange** (`PUNCH BASS · BASS`). Right, 11 px grey, right-aligned: the group and
what is on the knobs — `OSC 1 · A` (screen A, lane OSC 1), `OSC 2 · B`, `OSC · MIX`, `FILTER`, `ENV 1 · amp`,
`ENV 2 · filter`, `LFO 2 · B`, `MOD 3`, `FX`, `MIX`. With SHIFT on (latched or held), the word **`snap`** (9 px,
white) left of it (**`snap · B1`** on a lane with a ratio pair, §6). The editor's title line has no battery (it shows on the Options page only); the section text sits at the right end:
the header's 16 x 10 case and nub, 0–3 quarters filled, full on USB power (`cr_screen_t.batt`, 255 = none; the
right text moves left of it).

### A cell

Up to four cells a row, 60 px wide at x = 60·c, one per knob. A cell is a label, a value and an optional glyph
(`knob bar env wave saw square filter steps dots morph noise`, or an oscillator `wave` SAW SQR TRI SIN PWM NOIS
drawn with its shape). The VA's WAVE column follows the oscillator's MODE live (the label `desc` gives):
**WAVE** (BASIC) the named wave (TRI a triangle); **MORPH** the morphed wave itself, `CR_G_MORPH` at the 0..127
position: two cycles, 12 knots a cycle (the eighths, both sides of the jump at 1/2 and of the pulse's edge, ≤ 23
segments), each knot the linear crossfade of the two neighbouring shapes of docs/VA.md (sine 0, triangle 24, saw 48,
ramp 72, square 96, then the pulse narrowing from 50 % to ~5 % at 127), aligned as eng_va.c's `va_morph` and drawn
full height, so turning KNOB 1 morphs the glyph detent by detent; the value is the position's name (`SAW>RMP`, 9 px
when 11 px does not fit); **NOISE** `CR_G_NOISE`: WHITE dense jitter (20 segments), BROWN a smooth wandering line
(10), VINYL four sparse spikes on a faint line; a text cell draws its value with a small bar for its fill; `bipolar` cells (Env amt, Pan, Transpose, the
matrix Amount) draw a centre-zero bar. A cell whose parameter the matrix modulates (a slot with that destination, a
source and a non-zero amount) has a **mark**: a 4 px square at its top right in the source's colour (ENV yellow, LFO
red, VEL KEY RAND MODW blue; several sources: white); `cr_cell_t.flags` carries it (`CR_CF_MARK(colour)`), the
edit8 label then fits in 46 px. The **active** row is in the knob colours (blue, orange, white, green) with a
2 px bar in that colour under each cell; every other row is the palette's grey with no bar. The cell just turned is
**hot**: a filled block in its colour behind its value (the value in the ground colour); after a quick mapping the
block is the source's colour and the value the slot's amount with the source (`ENV2 +12`, `LFO1 +24`: `hot_col`). A
value too wide loses its space, then 2 px of size at a time (down to 9 px), then ellipsises.

**The Prophet-5's controls as a cell's glyph** (3y94, the final look 3y95; the cell styles `ENG_C_P5*`, cr_screen.h
`CR_G_P5*`, drawn by cr_draw.c `cr_p5glyph`; P5's rows, §4, and the layout tool's designs, docs/LAYOUT-TOOL.md "Cell
styles"). In a full-size cell (60 px wide, the 34 px glyph box: no band, or one lane under
a band) they are the owner's round-5 mock-up (`design/p5-glyph-mockups-5.html`), centred 22 px under the label's
baseline (the box's top + 12); the label stays on top, the value on the row under it ("64", "On", "Half", "1/2"), the
knob bars and the hot block as for any cell. On the active row the glyph is in the column's colour; on a dim row the
editor's greys (rim line, face surf, ticks dim, the bright tick and the pointer mid):

- **knob** (`CR_G_P5KNOB`, K6d): eleven 2 px ticks, 3 px long (r 14 .. 17), mid grey, over the 270 degrees; the tick
  nearest the value white and 1 px longer (it matches the dial to a real P5's printed scale); a 2 px rim (r 9 .. 11) in
  the row colour; the face the row colour at 40 % (cr_draw's `ux_mix` toward black, the mock-up's `tintOf`); a 3 px
  white pointer from 2 px off the centre to r 8; centred 6 px lower than the buttons (box top + 18, level with their
  LEDs: the owner, "the knob glyphs are vertically too high and not aligned with the button glyphs"); centre-zero
  parameters have their zero at 12 o'clock; no value inside;
- **LED button** (`CR_G_P5LED`, B1a): a 22 x 20 cap, its 2 px rim in the row colour, the face surf, the LED window (the
  top 8 px inside the rim) black; the LED a 6 px red disc in a 2 px dark-red halo when on, a 4 px dark-red dot
  off; `pct2` the one-cycle wave (saw, triangle, pulse: CR_P5I_*) as a 2 px line, 14 x 8, 4 px over the cap, **white
  when on, grey (dim) when off** (mid / line on a dim row); without a wave the cap stays where it is;
- **lever** (`CR_G_P5SWITCH`, S5): a 30 x 20 cap, the rim in the row colour, two LEDs 14 px apart counting the position
  (Off none, Half the left, Full both), no names;
- **two LEDs** (`CR_G_P5LED2`, R3, the filter's REV): the 30 x 20 cap, two red LEDs, the current value's lit, the two
  value names (8 px bold, the faces scale to it) over them, the current one white, the other dim; the names ride after
  `cr_cell_t.label`'s NUL, or after the value's when the label is too long ("Filter rev").

Two lanes under a band keep the first pass's small glyphs in their 22 px box (a 26 px dial; the caps alone, 20 x 16
and 34 x 16 with a red and a yellow LED; the lever's slot). Tests: tests/cr_draw_test.c `p5cell_screens` (goldens in
tests/cr_p5cell_golden.txt; the lit LED, the white tick, the wave white when on, the lever's LEDs), web/test_layout.mjs
section 6 on `design/layouts/designs/p5-rows.json`.

### `edit8` — up to eight parameters

Two rows of four cells (row A, row B), one of them active; an optional **wide** shape over them:

- the **envelope**: one AHDSR line, 3 px white, over a faint baseline, the sustain a flat run, the segment being
  turned thicker in its knob's colour, letters A (H) D S R under the baseline;
- the **filter**: the response curve, 3 px orange (KNOB 1 = cutoff), its resonance peak over a dashed 0 dB pass
  level, cutoff on a 9-octave log axis, `DRIVE n` top right when driven. FTYPE (0..127, KNOB 3) morphs it: the curve
  is |lp − hp·w² + j·bp·g·w/Q|² / |1 − w² + j·w/Q|² with the weights (lp, bp, hp) crossfaded linearly between
  LP (1,0,0) at 0, BP (0,1,0) at 32, HP (0,0,1) at 64, NOTCH (1,0,1) at 96 and back to LP (the SVF's outputs mixed,
  as the VA mixes them; g² = Q / 0.6 keeps BP at 0 dB at its peak), in 32-bit integer log2 maths at the same seven
  quarter-octave points (≤ 12 segments); top left the position's name (`LP`, `LP>BP`, `BP`, .. `NT>LP`);
- the **dx** envelope (FM6): the DX7's four rates and levels as a 4-segment line (FM6 above);
- the **cz** envelope (CZ-1): eight rates and levels, SUS and END as a step line (CZ-1 above);
- (FORMAT.md also has a **wave** band: two cycles across the screen, blue; not used by the drawn sections).

Layout: **with a wide shape** — title 0–24, the shape 24–120, row A 124–180, row B 184–240 (label 10 px, glyph 22
px, value 13 px bold); **one lane under the shape** (3y95: CZ-1's END screens, WHEEL's ENV, a design's one-lane
screen) — its row at full size, 124–224 (label 12 px at +16, the 34 px glyph box at +26, value 17 px bold at +82, the
bars at +98: the row without a shape moved down 94 px). **Without** — row A 30–130, row B 134–234 (label 12 px, glyph
34 px, value 17 px bold).

Used for: FILT, each ENV, FX, MIX; and every one-row group of the engines without deep pages.

**`tall`** (the oscillator mixer, OSC screen 3): one row of four cells over the whole panel: the label (`OSC n`, 12 px)
at y 46, a 24 px wide well 56–204 filled from the bottom in the knob colour, the value (17 px bold) at y 226, the knob
bars at 236.

### `stack` — the group's instances

Up to four column headings (10 px grey, y ≈ 36), then N = 1–8 equal rows sharing y 41–239, separated by 1 px lines.
A row label column at the left (11 px bold; white when active, else grey; as wide as the longest label, at most
44 px), the four cells share the rest; a cell's own label is not drawn (the heading names it).

- Rows ≥ 30 px (N ≤ 6: the 4 oscillators, the 4 LFOs, the 4 platform routes): a glyph cell draws its glyph with the
  value under it; a text cell its value (15 px bold, 13 px under 45 px rows) with a 3 px fill bar.
- Rows < 30 px (N = 7–8, ~24.75 px: the VA matrix): every cell text only, the value 12 px bold and a 2 px bar
  (centre-zero for the Amount).
- The active row's knob bar is 2 px at the bottom of each cell.

Used for: OSC (VA), LFO (VA), MOD.

### `free` — a compiled layout's free screen (the layout tool; no unit has one now)

`CR_K_FREE` (`cr_draw.c`, docs/LAYOUT-TOOL.md "The p5 style"), compiled only when a built design has free screens
(`CR_LAYOUTS_FREE`) and in the layout tool's preview: none does since P5's rows (3y97). The title line, then the
screen's placed elements from the compiled table (`cr_layout.c`), frames first: framed sections with their names in the border, knobs with a 0..10
scale and the value in the face (a named value too wide for it on the row under the label), LED buttons (a wave icon
or a name over the cap; two LEDs, red and yellow, for a parameter of two values), a lever for three positions. Every
lane's values are drawn; the active lane's elements in the knob colours by slot (blue, orange, white, green), the
others grey. A turned knob's face fills in its colour for 800 ms. A knob turn composes only the strips of the elements
that changed. The layout tool draws a design's free screens through this same code (`lay_load`).

Used for: the layout tool's free designs (`p5-panel.json`, P5's editor from 3y86 until 3y97).

## 10. Motion: none in the editor (2026-10-06, the user's feedback from the device)

The editor draws every change **at once**, in the UI frame after the detent or the tap: no slide-in of the cells
on a new screen or group, no sliding bars between lanes, no tween of a cell's glyph or of the wide band. The band
(the envelope, the filter curve) redraws with the value, the turned segment thick in the knob's colour; the hot
cell (the one just turned) stays CE_HOT_MS (800 ms). Options > Motion (full / calm / off) applies to the rest of the
instrument (the chord squeeze, the pickers, the meters, the stripes) and changes nothing in the editor.
`cr_screen_t` has no motion fields for the editor any more (`ed_dx`, `bar_dy` are gone).

### Responsiveness ("changing the cutoff has a large delay until it visually changes")

Measured with `tools/emu/scripts/cr_editor_lag.txt` (LUSH PAD, FILT, KNOB 1 20 detents in 1 s, then the OSC stack's
KNOB 2 10 detents) under `EMU_UI_LOG=0` (every UI frame: its host instructions, the strips drawn / blitted, the
bytes sent, the band's values); tools/emu/test_cr.sh checks it. Device time = host instructions / 258 µs; the LCD's
SPI ran at 12 MHz (lcd.c `LCD_BAUD` 4): 0.67 µs a byte, 12.8 ms a 240 × 40 strip; **since 0.22 at 20 MHz** (`LCD_BAUD`
2, the owner's eye on the unit 2026-10-14: 0.4 µs a byte, 7.7 ms a strip; the figures below are the 12 MHz build's).

| | before | after |
| --- | --- | --- |
| detents per frame | every detent taken at once (`fm1_enc_take`: the whole accumulated count; `cp_dstep` steps `s` × the step; the main loop scans the input between frames too): no change needed | the same |
| frames from a cutoff detent to the curve at its value | 10 (the 160 ms band tween; the next detent restarts it) | **1** (no tween) |
| a cutoff detent's frame | 6.5 M host instructions (~25 ms), all 6 strips composed, 2–3 blitted (38–58 KB, 26–38 ms of SPI), **every frame of the tween** | **3.3–3.5 M (~13 ms)**, 5 strips composed (the band 0–2, row A 3–4), 18–28 KB sent (12–19 ms of SPI, overlapping the next strip's drawing), once |
| an OSC stack detent (Level) | 2.5 M, 3 strips composed, 19 KB | **1.4 M**, 2 strips, 3–8 KB |
| frames between the detent and the screen settled (device) | ~10 frames of ~50–60 ms each | 1 frame of ~20 ms |

What made it fast:
1. **No tween** (cr_edit.c): the band's values and the cells are the parameter's, every frame.
2. **Only the strips of what changed are composed** (cr_draw.c `cr_ed_strips`): the editor's parts are hashed apart
   from the rest of the screen: each row's cells, the band's values (`wv`), the hot cell, the title line. A change
   of the band alone composes strips 0–2, a cell its row's strips (row A 3–4, row B 4–5, a stack row its 1–2), the
   title line strip 0; anything else (a new screen, a message) all six.
3. **cr_poly** (cr_gfx.c) tests a pixel's centre against each segment first: farther than the stroke's half width
   plus a sample's reach (8.5 Q4) it skips the 16 samples, well inside the stroke (not dashed) it fills the pixel at
   once; a row visits only the columns its segments' boxes reach. Exact: the mock-up renders are identical bit for
   bit. The filter band's frame: 6.5 -> 4.2 M before the other steps. A strip also skips the title line, the band and
   the rows it does not cross (`cr_in_strip`: not even measured).
4. **The blit** (cr_draw.c `cr_send`): each strip is hashed as 4 × 5 tiles of 60 × 8 px and only the box of the tiles
   that changed is sent; it is copied into one of two DMA buffers (cv_px's rows 40–119, unused by the 40-row strip
   canvas), so the next strip is composed while the SPI sends this one. Before, gfx.c `cv_begin` waited for the last
   strip's DMA (`lcd_sync`) before drawing the next: every blitted strip cost its 12.8 ms of SPI on top of the drawing.
   lcd.c itself (Felucca's) is unchanged: it already leaves a transfer running; raising its SPI clock (`LCD_BAUD` 2 =
   20 MHz) would cut the SPI time further but needs a check on the device. 2026-10-14: built, looked at on the owner's
   unit ("looks good"), the default since (lcd.c `LCD_BAUD` 2; `LCD_BAUD=4 ./build.sh` is the 12 MHz build).

## 11. LEDs

- **EDIT blinks** while the editor is open (and while the engine picker or the save dialog is open over it).
- **The current group's button is lit** (OSC = printed FX, FILT = SEL, ENV = ENV, LFO = LFO, MOD = SEQ,
  FX = PLAY, MIX = REC); the other group buttons dim.
- SHIFT (GLO) lit while latched or held; PERF as outside; OCT− / OCT+ as outside (lit / blinking in the dialogs and
  the picker).
- The root keys show what sounds (audition), as outside; the chord keys as outside (held, or latched with LOCK).
- **The printed key labels (OP1..OP6, PIT, GLO, MONO, POLY on the black keys) are not used** as editor indicators or
  shortcuts (decided 2026-10-06): the key LEDs always mean the chord keys' state, and the screen carries the rest.

## 12. Open questions

1. **Variant B: HOME = MIX.** Drawn is variant A: HOME and EDIT both leave, REC = MIX. Variant B: HOME is the MIX
   view (tap = MIX, tap again = row B) and only EDIT leaves; REC then stays REC (record while editing). For the user
   to choose.
2. **LFO screen 2**: decided: Sync alone, so screen 1 keeps Rate · Wave · Depth · Fade visible; a synced LFO's Rate
   reads as a division of the BPM.
3. **PRESETS in the editor**: decided (2026-10-06): the engine picker, previewing (§7). ALGORITHM still loads the
   bass at once.
4. **A group button held**: reserved (nothing). Could become "reset this parameter / lane to default".
5. **The functions the sections hide** (KEY, BASS, LATCH, SEQ, LOOP, REC, the FX on/off): not reachable while
   editing. Variant B gives REC back.
6. **OPT tap in the editor**: decided: SHIFT latch (§6); the Options stay outside the editor.
7. **Page memory across power-off**: RAM only for now; it could join the settings record if wanted.
8. **FX lane B**: none (the platform exposes no per-effect parameters on its pages); the fx layer (PLAN.md §3) keeps
   the effect parameters.

## 13. Quick modulation mapping (2026-10-06)

For an engine whose deep pages map their columns to the matrix (`eng_deep_t.mod_dst`: the VA):

| Gesture (in the editor) | Result |
| --- | --- |
| **ENV held + KNOB 1–4 turned** | the **ENV n last shown** (the ENV group's screen when it was last open; before that **ENV 2**) modulates the parameter under that knob: the matrix slot with that source and that destination, else the **first free slot** (SRC or DST `OFF`) gets them, and the turn steps its **AMT** (−64..63, accelerated as every value, §6a: a slow detent 0.1 below 6 and 1 from 6, a quick one 6.35 to the last value it passed, 6 from 0; SHIFT: the snap, whole numbers; the VA's AMT steps in tenths below 6 since 2026-10-15, GitHub issue 13). The hot cell shows `ENV2 +12` (`LFO1 +0.3` in tenths) on a block in the source's colour. Trace: `mod: ENV2 -> CUT +12 slot 1` |
| **LFO held + KNOB 1–4 turned** | the same with the **LFO n last shown** (its lane on the LFO stacks; before that **LFO 1**): `mod: LFO1 -> RES +6 slot 2` |
| no free slot | the message **`matrix full`** (red), nothing changes; trace `mod: LFO1 -> CUT matrix full` |
| a parameter that is no destination (a wave, an envelope time, a matrix column), or an engine without `mod_dst` | **`not modulatable`**; trace `mod: not modulatable` |
| **OCT− held + KNOB 1–4 turned** | every slot whose destination is that parameter is cleared (SRC, DST `OFF`, AMT 0): **`cleared`** (`no modulation` when there was none); trace `mod: clear CUT, 2 slots`. OCT−'s release then shifts no octave |
| ENV / LFO **tapped** | as before: the group, the next screen |
| ENV / LFO **held** with no knob turned | nothing (reserved), as before |

The turn may come at any time while the button is down (a turn makes the press a combo: its release is no tap).
The destination of a cell: `mod_dst(t, page, col)` for a deep column, `mod_dst(t, ENG_MOD_TRK, P_id)` for a
platform one (the VA: Level -> AMP, Pan -> PAN, Transpose / Detune -> PITCH on the MIX screens); its value is the MOD
pages' DST value, so the editor reads and writes the slots through the deep pages' `get` / `set` (MOD 1..8: SRC DST
AMT) and a source by its name in the SRC column (`ENV2`, `LFO1`). The slots stay visible and editable on the MOD
screens. Every cell whose parameter is modulated carries the mark of §9. The VA's destinations for this
(appended, docs/VA.md): DRIVE, SPRD (SPREAD), FENV, DEP1..DEP4 (the LFOs' DEPTH).

Tests: `tools/emu/scripts/cr_editor_map.txt` (tools/emu/test_cr.sh: the traces above, the marks' pixels yellow / red /
white, `cleared` with no octave step, the taps kept, the matrix filled to `matrix full`, WHEEL (A4) `not modulatable`;
shots `build/emu/test/cr_map_*.ppm`), tests/gen_cr_screens.py's two modulated states (`43_filter_modulated_*`,
`44_osc_stack_modulated_*` in build/cr_screens/).
