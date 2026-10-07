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
engine picker; SAVE held: the loop slots, momentary) closes back **to the editor, as it was** (group, screen, lane):
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
| SEQ | METRO | **MOD** | as OSC | — |
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
| KNOB 1–4 | edit the four cells of the active lane. A detent = **5 % of the range**, enums **one by one**; SHIFT on = **one unit**. Ranges, defaults and value text are the platform's (`params.c`, `param_format`) or the deep page's (`eng_page_t` formats) |
| SELECT | the lane, across the screens (above) |
| PRESETS | the engine picker, previewing (§7) |
| MASTER | volume |
| ALGORITHM | the bass sound, as outside |

## 4. Groups per engine class

The screens are built from the engine's deep pages (`eng_deep_t`, docs/VA.md): its five sections (OSC FILTER ENV
LFO MOD) and the page titles, never the column names, so a column or a page the engine adds shows up by itself:

- a page titled with an instance number (`OSC 2`, `OSC 2+`, `ENV 3`, `LFO 1`) belongs to that instance; pages whose
  titles differ only in the number are one **kind** (`OSC n`, `OSC n+`);
- OSC and LFO: one **stack** screen per kind (lane n = instance n); a page with no number whose labels end in a
  digit (`LFO SYN`: SYNC1..SYNC4) is a stack whose lane n is its column n; any other page with no number (`VOICE`)
  is a one-lane screen of its own. A column some lanes lack reads `–`. The headings are the columns' long labels
  (two kinds in one column: `Sync/Ring`);
- OSC then has the **mixer** (when the oscillator pages have a LEVEL column);
- FILT and ENV: an `edit8` screen per instance, its pages as lanes A and B (two a screen) under the wide band;
- MOD: a stack of the slots, eight a screen.
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
| MOD | 1 | `stack`, 8 rows, text only | slots 1–8 | Source · Dest · Amount (bipolar) | `MOD 3` |
| FX | 1 | `edit8`, one row | one | Drive · Chorus · Delay · Reverb (the platform sends) | `FX` |
| MIX | 1 | `edit8`, two rows | A, B | A: Level · Pan · Voice · Glide; B: Transpose · Detune · Priority · Gl. mode | `MIX` |

With Sync on, an LFO's Rate reads as a division of the BPM (`1/8`). The VA's EDIT 1 / EDIT 2 macros are not shown:
the deep values are the truth and a deep edit writes the macro back. Unused matrix slots read `–` (the active one
shows its source, to turn).

### Engines without deep pages (ANALOG, PHASE, LOFI, SAMPLE, VOICE, TRIO, WHEEL, GRAIN, PHYS, NOISE)

| Group | Screen | Lanes | KNOB 1–4 | Top-right text |
| --- | --- | --- | --- | --- |
| OSC | `edit8`, one row | one | EDIT 1: `P_E0`–`P_E3` | the engine's `page_title[0]` (`OSC` on ANALOG, `OPS` on FM6) |
| FILT | `edit8`, one row | one | EDIT 2: `P_E4`–`P_E7` | `page_title[1]` (`FLT`, `PATCH`) |
| ENV | `edit8`, one row under the wide AHDSR | one | Attack · Decay · Sustain · Release (`P_ATK P_DEC P_SUS P_REL`) | `ENV` |
| LFO | `edit8`, one row | one | Rate · Wave · Vibrato · Wah (`P_LRATE P_LWAVE P_LD_PIT P_LD_FLT`) | `LFO` |
| MOD | `stack`, 4 rows | 4 routes | Source (fixed text) · Dest (fixed) · Amount (`P_ED_FLT P_ED_PIT P_ED_SHP P_LD_AMP`) | `MOD n` |
| FX, MIX | as the VA | | | |

A one-screen, one-lane group's tap and SELECT do nothing.

### Engines with their own envelopes (`engine_t.ownenv`)

- **VA**: its own ENV 1–4 (above); ENV 1 is the amplitude.
- **FM6** (ownenv, no deep pages): the operator envelopes are in the patch, so there is no platform ENV page. The
  ENV button shows the message **`FM6: own envelopes`** and the editor stays in its current group.

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

- **tap** = toggle the **latched** SHIFT: its LED lit, the word `fine` small in the title line, KNOB 1–4 one unit a
  detent until tapped again (or the editor is left);
- **held** past HOLD_MS (or with a knob turned meanwhile) = **momentary**: fine while held; released, back to the
  latched state (a hold never toggles it);
- held + EDIT = chord ↔ bass sound.

While the editor is open the edit wins over OPT's outside knob functions (split point, metronome level, bass volume).

## 7. The engine picker: a preview (EDIT held, or PRESETS in the editor; main walk-through state 22)

On opening, the picker **snapshots** the part's sound: every parameter, the engine, the deep patch (`deep->blob_get`,
the VA's), the `edited` flag and the user slot link. Then, as before:

- the white root keys are the engines in the firmware's order (ANALOG, FM6, VA, PHASE, LOFI, SAMPLE, VOICE, TRIO,
  WHEEL, GRAIN, PHYS; NOISE on SELECT). A root switches the sound's engine, keeping its envelope and sends;
- KNOB 1 (and PRESETS) steps the engine's factory presets, loaded for preview. The preset meter's bar **jumps** to the
  value (no fill animation, that popup only);
- KNOB 2 inits the sound;
- **KNOB 4** switches the roots' job: `roots: engines` (they pick engines) or `roots: play` (they play the previewed
  sound; engines are then chosen with SELECT only). The footer shows it. The choice is a setting (`pick_roots`,
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

The same dialog as outside: KNOB 1 picks the user slot (U01–U32; a used one shows its name), the white roots type the
name (phone style), D#4 a space, **F#4 deletes** the last letter, KNOB 2 the last letter. **SAVE again or OCT+ saves**;
**OCT− or HOME cancels**. SAVE held 1 s on a used slot asks "delete?" (OCT+ yes, OCT− no). EDIT keeps blinking; the
dialog returns to the editor view. On the bass part it saves the bass sound (listed on ALGORITHM); on the chord part
the chord sound (listed after the factory bank on PRESETS). A VA sound saves its patch with it (`va_store.c`).

## 9. Screens

### The top line (both kinds, y 0–24)

Left: the sound's name in 13 px bold, white; a trailing **`*`** once edited (deep edits too); on the bass part
` · BASS` and the whole title in **orange** (`PUNCH BASS · BASS`). Right, 11 px grey, right-aligned: the group and
what is on the knobs — `OSC 1 · A` (screen A, lane OSC 1), `OSC 2 · B`, `OSC · MIX`, `FILTER`, `ENV 1 · amp`,
`ENV 2 · filter`, `LFO 2 · B`, `MOD 3`, `FX`, `MIX`. With SHIFT on (latched or held), the word **`fine`** (9 px,
white) left of it. The editor's title line has no battery (it shows on the Options page only); the section text sits at the right end:
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
- (FORMAT.md also has a **wave** band: two cycles across the screen, blue; not used by the drawn sections).

Layout: **with a wide shape** — title 0–24, the shape 24–120, row A 124–180, row B 184–240 (label 10 px, glyph 22
px, value 13 px bold). **Without** — row A 30–130, row B 134–234 (label 12 px, glyph 34 px, value 17 px bold).

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
SPI runs at 12 MHz (lcd.c `LCD_BAUD` 4): 0.67 µs a byte, 12.8 ms a 240 × 40 strip.

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
   20 MHz) would cut the SPI time further but needs a check on the device.

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
5. **The functions the sections hide** (KEY, BASS, LATCH, METRO, LOOP, REC, the FX on/off): not reachable while
   editing. Variant B gives REC back.
6. **OPT tap in the editor**: decided: SHIFT latch (§6); the Options stay outside the editor.
7. **Page memory across power-off**: RAM only for now; it could join the settings record if wanted.
8. **FX lane B**: none (the platform exposes no per-effect parameters on its pages); the fx layer (PLAN.md §3) keeps
   the effect parameters.

## 13. Quick modulation mapping (2026-10-06)

For an engine whose deep pages map their columns to the matrix (`eng_deep_t.mod_dst`: the VA):

| Gesture (in the editor) | Result |
| --- | --- |
| **ENV held + KNOB 1–4 turned** | the **ENV n last shown** (the ENV group's screen when it was last open; before that **ENV 2**) modulates the parameter under that knob: the matrix slot with that source and that destination, else the **first free slot** (SRC or DST `OFF`) gets them, and the turn steps its **AMT** (−64..63, the coarse / fine steps of §3: 6 a detent, SHIFT one). The hot cell shows `ENV2 +12` on a block in the source's colour. Trace: `mod: ENV2 -> CUT +12 slot 1` |
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
white, `cleared` with no octave step, the taps kept, the matrix filled to `matrix full`, ANALOG `not modulatable`;
shots `build/emu/test/cr_map_*.ppm`), tests/gen_cr_screens.py's two modulated states (`43_filter_modulated_*`,
`44_osc_stack_modulated_*` in build/cr_screens/).
