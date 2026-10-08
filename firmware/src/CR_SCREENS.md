# ChoralRoot FM-1 screens (`cr_screen.h`, `cr_draw.c`, `cr_gfx.c`)

The renderer of the 24 approved mock-ups (`design/choralroot-fm1-screens.png`, drawn by the designer's
`renderScreen` in `../ChoralRootFM1Designer/index.html` from `design/choralroot-fm1-mockups.json`) on Felucca's
graphics layer (`gfx.c`). `tests/run_cr_draw.sh` renders every state on the host and lints the layout.

```
#include "gfx.c"
#include "cr_gfx.c"      /* shapes + scalable text on gfx.c's canvas; includes build/gen/cr_fonts.h */
#include "cr_draw.c"     /* includes cr_screen.h */
...
cr_screen_t s;  cr_screen_clear(&s);  /* fill it from the engine / UI state */
cr_draw(&s, ms_since_the_change);     /* every frame; cheap when nothing changed */
```

## The view-model (`cr_screen.h`)

`cr_screen_t` mirrors the designer's `screen` object, flattened: `kind` (`CR_K_STRIPES CHORD PICKER METER KEYBOARD
ARP GEEK TEXT BIG SCOPE EDIT8 STACK KNOBROW`), the top line (`header`, `icon` none/play/rec/loop, `mid`/`mid_col`, `right`/`right_col`,
`batt`), `footer` (one line; empty = none, the panel then runs to row 240), the ring (`ring_on`, `ring` Q8,
`ring_rec`, `ring_col`), the corner dial (`dial_on`, `dial` Q8, `dial_pulse`), `message`/`message_col`, and the panel fields of every kind (fixed char arrays; the chord
name as `cr_name_t {root, quality, sup, col_root, col_quality, col_sup}`; up to 8 notes `{t, col, mark}`; a picker
window of 8 items with `n_items`, `item0`, `sel`; 4 params columns; 27 keys as a `lit` bitmask + `lit_col[]` +
`key_label[]`). Colours are `cr_col_t`: ChoralRoot's seven + black (fixed RGB565 in `cr_draw.c`, not per palette:
white 246,243,234, red 224,58,47, blue 43,80,200, yellow 242,183,5, orange 240,122,26, green 47,179,122, grey
110,110,118, black 10,10,12) and the palette tokens (BG SURF TEXT THEME ACCENT MID DIM LINE REC); `CR_COL_NONE` takes
the designer's default for that field. Fractions are Q8 (`CR_ONE` = 256).

The producer must start from `cr_screen_clear()` each frame: the cache hashes the struct's bytes.
`tests/gen_cr_screens.py` is the reference translation from the designer's JSON (its defaults made explicit; a token
the designer resolves in its MOD palette becomes the named colour: THEME red, ACCENT yellow, TEXT white).

## Animations (pure functions of `(s, anim_ms)`)

`s->anim` says which are running; `anim_ms` is the time since the change that started them (the tween clock).
`cr_anim_busy(s, ms)` is 1 while frames are still to come. Past their durations they draw exactly the settled screen
(tested).

| flag | fields | motion |
| --- | --- | --- |
| `CR_A_SQUEEZE` | `from`, `name`, `squeeze` | 120 ms: `from` thins to an 8 % column (ease-in, 60 ms), `name` stretches out of it (ease-out, 60 ms); no `from`: stretch only. Multiplies the fit-to-224 px factor and the forced `squeeze` (state 24's frame) |
| `CR_A_SLIDE` | `slide` (sel came from sel − slide) | 160 ms ease-out: the old item leaves and the new one enters inside the item's box (a reel); vertical or horizontal |
| `CR_A_FILL` | `pct_from` → `pct` | one stripe every 30 ms |
| `CR_A_STRIPES` | `phase`, `period_ms` | the bands' phase advances a cycle per `period_ms` (40 px per cycle, as the designer's `phase`) |
| `CR_A_SWEEP` | — | the bands are swept off to the right, 240 ms each, 40 ms apart (the first chord) |

No flag, the producer's motion (`cr_ui.c` `cu_animate`, `cr_anim.c` `cr_spring`): a meter's number and a `big`
panel's value **spring in** through `size` (meter 92 → 104 px, big ¾ → its size, 240 ms) whenever the value changes:
the count-in's 4 3 2 1, undo's layers left, PANIC. `size` is in the hashed bytes, so the cache redraws each step.
A `big` panel with `ring_on` lifts its value, sub and label 22 px clear of the ring's band.

## Strips and the cache

The screen is drawn as **six strips of 240 × 40** (gfx.c's canvas holds 240 × 124, `CV_MAX`); each strip runs the
whole composition with `cv_oy = −top`, so layout code is in screen coordinates and everything is clipped by the
canvas (text and glyphs outside the strip's rows are skipped). Order as the designer: header, panel, footer, ring,
message.

- **Signature**: FNV of the struct + the animation frame (`cr_frame_t`: the squeeze, slide offset, stripes filled,
  shift, sweep) + the palette generation (`ux.gen`). Unchanged and not forced: `cr_draw` returns at once (nothing
  drawn, nothing blitted) — a settled screen costs one hash per frame.
- **Changed**: every strip is drawn, its pixels hashed, and only strips whose hash changed are blitted (the SPI
  transfer is the expensive part). A header change blits 1 strip; the ring appearing blits 6.
- **Only the ring's fraction moved** (the signature without `ring` unchanged: a playing loop on a still screen):
  only the strips holding band pixels between the old and the new tip are drawn (`cr_ring_strips`: the band's rows
  over those angles, widened by two pixels; usually one strip, two where a strip edge is crossed; the loop's restart
  clears the whole ring: all six). The other strips are neither drawn nor blitted.
- **Only the corner dial moved** (`dial`, `dial_pulse` hashed apart like `ring`): strip 0 alone is drawn.
- `cr_draw_invalidate()`: the next draw blits all six (after Felucca's UI drew, a palette change, power-up).

**The ring** (`cr_ring_draw`): its pixels never move, only its colour and fraction change, so the coverage of the
band is computed once (`cr_ring_build`, from `cr_ui_init`: ~27 ms on the device scale) with `cr_arc`'s own per-pixel
code (`cr_arc_px`) into the POOL: per row up to two runs of pixels, a byte a pixel (the solid band's and the dotted
circle's sample counts as one index of the 153 pairs d <= s), 4 232 pixels, 6.1 KB. A frame blends its rows' runs
from the table; the progress is decided per run from the angles of its end pixels (the angle is monotonic along a
row): a run well inside the sweep takes the band's coverage, one well outside nothing, and only pixels within
64 / 65536 turn of the start or the tip (more than any sample's angle differs from its pixel's) take `cr_arc_px`'s
16 samples. The pixels are exactly `cr_arc`'s (`tests/cr_draw_test.c` compares both at 258 fractions over a pattern,
and the partial draws with full draws). Host instructions a UI frame with a loop playing on the chord screen: 1.0 M
(0.58 M with no ring at all); before, 12.8 M with the arp (`tools/emu/perf.sh` c: now 2.4 M, 1.2 M with no ring).

**The corner dial** (`cr_dial`, from `cr_header`): the ring is drawn only where the loop is the subject (the
count-in, the undo screen, recording and overdubbing, set by `cr_ui.c`; calibration uses it as its progress); the
LOOP and SAVE layers have no ring since the layers sheet's state 6 (2026-10-07); while a loop
merely plays (`lstate` PLAYING, `lcap` NONE or OD_ARMED) the producer sets `dial_on` instead, on every screen with
a top line but the Options pages (the sound editor has none). The dial is Orchid's ring shrunk into the top line:
centre (229, 12), r 8, 3 px — the track as 10 dots of T_LINE (`cr_disc`, about 2 on 3 off; no dashed `cr_arc`, whose
per-sample angles cost more), the progress `cr_arc` red clockwise from 12 o'clock, 5 px wide for `dial_pulse` (the
downbeat's ~100 ms, skipped with Motion Off). The header's `right` text moves 22 px left while it shows; nothing
else moves (the panel keeps its height, pickers their marks and footers). Rows 2..22, so strip 0 only: a moving
dial redraws one strip (`perf.sh` c, a 1-bar loop at 200 BPM under the arp: 2.70 ms a UI frame on the device
scale, 2.95 ms with the ring it replaced).

## Type: faces, sizes, flash

Bold type (the mock-ups' 600/700/800) is **Inter Tight 700**, regular (500) is Felucca's own faces:

| face | where | flash |
| --- | --- | --- |
| `AF_CRX` 104 px / 700, 1 phase, Huffman, sparse: `space # + - . 0-9 ? A-G I J M N P T W Z b d i m s u` (37 glyphs) | the chord root (104 px), quality (60 px), the meters' numbers (104 px), PANIC (64 px), the arp's root (44 px) | **14 272 B** |
| `AF_CRB` 40 px / 700, 1 phase, Huffman, ASCII + Felucca's extras (· … etc.) | everything else bold, 7–40 px: superscripts, pickers (40 / 36 px), values (20 px), notes lines (16 / 13 px), titles (34 / 26 / 15 px), header (15 px), labels (11 px), key labels (7 px) | **16 099 B** |
| `AF_M` 15 px / 500, `AF_S` 12 px / 400 (existing) | regular text 13–15 px (M, native), 12 px (S, native), < 12 px (S resampled) | 0 |

Total new flash for fonts: **30 371 B** (generated as `build/gen/cr_fonts.h` by `tools/gen_aa_font.py --preset
choralroot`, run by `tools/build.py generate()`). Figures are proportional (as the mock-ups' "13", "120").

**Why option (a)**, resampling two big faces, not (b) a 52 px face pixel-doubled: the 52 px chord-charset face is
6 510 B, so (b) saves ~7.8 KB, but a 2× pixel-doubled glyph has 2 × 2 px steps and a two-pixel-wide anti-aliasing
ramp on every edge — at ~220 ppi the chord name (the one big thing on the screen) visibly loses the mock-up's crisp
104 px outline. With (a) the root is drawn 1:1 from a 104 px raster (unhinted 16× supersampled area coverage, as
Felucca's faces), and the same resampler that draws every other size does the squeeze. A 104 px nibble-packed face
would be > 64 KB; Huffman (the gfx.c coder) makes it 14 KB.

**The resampler** (`cr_gfx.c` `cr_glyph`): the glyph is decoded into an 8 KB buffer (the largest, CRX 'W', is
104 × 76), then each destination pixel takes the area-weighted sum of the source pixels under it (separable box
filter, overlaps in 1/256 px, scales in Q12, up to 16 source pixels per axis), quantised to the 16 coverage levels and
blended with gfx.c's coverage curve into whatever the canvas holds (so type on a block, a key or a stripe needs no
"under" colour). At 1:1 it is a straight copy at whole pixels. A squeeze is a horizontal scale only.

RAM (`.pool`): glyph buffer 8 192 B, column weights 9 216 B, two Huffman tables 1 032 B — 18.4 KB; the ring's
table (`cr_draw.c` `cr_ring`) 6.1 KB and the cache (`cr_dc`) 44 B.

## Shapes (`cr_gfx.c`)

All anti-aliased with 4 × 4 samples, blended into the canvas: `cr_frect` (fractional edges), `cr_disc`, `cr_arc`
(radius, width, start, sweep, round caps, dashes along the arc: the ring's dotted circle is width 5, dash 2 of 6 at
R 113), `cr_poly` (polyline, round joins and caps, dashes; joins blend once; per row only the segments whose box reaches
it, per pixel only those reaching its columns, a pixel none reaches skipped: a curve's cost is its own pixels, not its
box's), `cr_quad` (quadratic, as a 16-segment polyline: the arp's dotted hop, the filter glyph). `cr_arc_px` takes a
pixel whose centre is well inside or outside an undashed arc's sweep (by more than a sample's angle can differ) as
all in / out without the samples' angles. Both are pixel for pixel the exhaustive tests (INTEGRATION Performance). Integer `cr_sin`/`cr_cos` (quarter table), `cr_atan2`, `cr_isqrt`.
`cr_clip` limits these draws to a rectangle (the picker's slide window). gfx.c is unchanged.

## The MOD palette

Added to `tools/gen_ui_palettes.py` as the ninth palette: bg 10,10,12, surf 28,28,32, text 246,243,234, accent
242,183,5 as asked; **theme 248,96,48 instead of 224,58,47**: the red fails the generator's WCAG checks ink/sel
(3.44 < 4.5: background-coloured text on a selection row) and theme/surf (4.05 < 4.5), and 248,96,48 is the nearest
colour that passes all of them. ChoralRoot's screens never use the THEME token for red — they use the named red,
exact — so the mock-ups are unaffected; THEME serves Felucca's own pages when MOD is picked. `tests/theme_test.c` and
`tests/ui_test.c` now expect nine palettes (MOD last; saved palette ids unchanged).

## What differs from the mock-ups, and why

- **Weights**: one bold weight (700) for the mock-ups' 600/700/800: the quality, superscripts, header and labels
  (600) are a little heavier; the stripes' title (800) a little lighter. Regular 13–14 px text (picker labels, the
  voicing line, the arp line) uses Felucca's M at 15 px (crisper than resampling M); 10–11 px text (geek status
  lines, the params foot) is S resampled.
- **Hairlines are crisp**: the battery outline, the bars glyph's ticks, and the arp's line
  are whole-pixel lines, where the canvas mock-up draws 1 px strokes on half pixels (blurred over two rows).
- **Rounded corners**: keys (r 1.5) and the battery (r 2) are square-cornered; the arp block, bar glyph and message
  box use gfx.c's `cv_rrect`.
- **The stripes' phase** is the designer's (a 40 px shift of 360 px bands): with skew 0 the bands span the screen at
  every phase, so the slide only shows with `skew` or the sweep (as in the designer).
- **The picker's change** is a slide (a reel inside the item's box), not a split-flap flip.
- The chord squeeze is the designer's horizontal scale, applied to the glyph rasters (no hinting at any width).
- **`CR_K_SCOPE`** (device-only, no mock-up yet): `wave[240]` (int8, ±127 = the panel's half height less 6 px), one
  sample a column. Drawn as a 3 px line, a column at a time: the span the polyline covers within a pixel either side
  (the neighbours' samples) ± 1.5 px as one fractional rectangle (`cr_frect`), only the columns crossing the strip's
  rows; a 1 px grey centre line. `cr_poly` is not used (48 points, every segment tested per sample: far too slow
  for 240 points). The samples are in the hashed struct: a still trace costs one hash, a moving one redraws and
  blits only the strips whose pixels changed. States 25-29 of `tests/gen_cr_screens.py` (`DEVICE_STATES`, not in the
  designer's JSON): the scope sounding and silent, three calibration steps (`CR_K_BIG` 40 px, the ring as progress).
- **`CR_K_EDIT8`, `CR_K_STACK`: the sound editor** (`cr_edit.c` fills them; FORMAT.md "Sound editor panels",
  design/choralroot-fm1-sound-editor-mockups.json; states 30-38 of `tests/gen_cr_screens.py` are that file's states
  1 3 4 6 8 9 11 15 16, the FILTER state mapped onto the device's column order CUT RES FTYPE FENV / KTRK - SPREAD
  DRIVE and the MIX ones given the battery; 39-42 device-only: the mixer, the OSC modes (MORPH / NOISE / TRI glyphs)
  and the FILTER morphing at FTYPE 16). Full screen (`header` 0): the title line (`title` in `title_col`, `page` right), then edit8:
  an optional wide band (QUAD's `CR_W_ALGO`, `CR_W_ADE2` and the filter's base-width window: below; `wide` `CR_W_ENV`: `wv` a h d s r + the lit segment, the AHDSR as one 3 px polyline, its
  steep runs cut into pieces <= 26 px tall so cr_poly's boxes stay small, the lit segment 4 px in the hot cell's
  colour, A H D S R under it; `CR_W_FILTER`: `wv` cut res ftype (0..127) drive, the 2nd-order response of
  the LP / BP / HP mix crossfaded at the FTYPE position (0 LP, 32 BP, 64 HP, 96 NOTCH = LP + HP, back to LP;
  the type word its name, `cr_ftype_name`) sampled at 7 quarter-octave offsets from the cutoff in 32-bit log2 arithmetic, the edges extrapolated, clamped to the band
  with the crossings: <= 12 segments, over a dashed 0 dB line) over two rows of four `cell`s (each `cr_cell_t` carries its label, value, `CR_CF_*` flags, glyph, pct, pct2, wave: 30 bytes); stack: `head` column headings at y 36 over
  `n_rows` rows sharing 41..239 with `rlabel`s (N >= 7: text only). The `active` row in the knob colours (blue
  orange white green) with 2 px bars, the others DIM; `hot_r` (row + 1) / `hot_c`: the value on a block of its
  colour. Glyphs (`CR_G_KNOB BAR WAVE SAW SQUARE STEPS DOTS`, a bipolar BAR centre-zero) are the designer's
  drawGlyph scaled by h / 64; `CR_G_MORPH` (pct = the VA's morph position) two cycles of the crossfaded wave,
  <= 23 segments; `CR_G_NOISE` (pct thirds: WHITE BROWN VINYL) jitter / a wandering line / spikes on a faint line.
  `batt` (not 255): the header's battery at the title line's right end (the MIX screens). No motion (docs/EDITOR.md §10): `hot_r` / `hot_c` and `hot_col` (the block's colour while
  mapping: the modulation source's), a cell's mark (`CR_CF_MARK(colour)` in its flags: a 4 px square at its top
  right). A value too wide loses its space first ("790ms"), then 2 px of size at a time (to 9 px), then ellipsises.
  The cache hashes the editor's parts apart (each row's cells, `wv`, the hot cell, the title line) and composes only
  their strips (`cr_ed_strips`); a cutoff detent: <= 4 M host instructions, measured by tools/emu/test_cr.sh
  (`cr_editor_lag.txt`, `EMU_UI_LOG=0`). The old params page (`CR_K_PARAMS`) is retired.
- **`CR_K_KNOBROW`: a layer screen** (FORMAT.md `knobrow`; design/choralroot-fm1-fx-mockups.json, the user-approved
  sheet design/choralroot-fm1-fx-screens.png; `cr_ui.c` fills it for every layer: `cu_fx_cells`, `cu_perf_cells`, `cu_bass_cells`, `cu_key_cells`,
  `cu_loop_cells`, METRO's Click inline, the hot cell `cu_hot_row`; design/choralroot-fm1-layers-mockups.json states
  1, 3, 6, 7 for KEY, LOOP and METRO). `kr_band` 1 (KEY): the band is `cr_keyboard`'s strip (224 px, <= 56 px tall,
  centred under `label`; `lit` / `lit_col` / `key_label` as `CR_K_KEYBOARD`) instead of the picker. `CR_CF_DIM`: the
  cell all in the grey (LOOP's Sync while playing: it cannot change), never hot. The panel's lowest 72 px are one
  row of four knob cells (`cell[0][0..3]`; y 126–198 with a footer, 168–240 without), the rest a horizontal picker
  band drawn by the picker's own pieces (`cr_pick_item` with its slide, `cr_pick_sides`, `cr_pick_marks`): the item
  34 px bold in `col` squeezed to 170 px, its neighbours 13 px dim, the marks under it, `label` 11 px dim top left,
  `value` 13 px bold under the marks. A cell is edit8's active cell made taller: the label 10 px (+11), a 48 × 36 glyph
  box (+15), the value 13 px bold (+66), the 2 px knob-colour bar (+70); with `CR_CF_PCT` and no glyph the bar glyph;
  not `CR_CF_ON`: a dim dash. `hot_r` 1 / `hot_c`: the value on a block of its colour (`hot_col`). The band slides
  (`CR_A_SLIDE`) as a picker's; the row has no motion. **Cache**: the cells and the hot cell are hashed apart as the
  editor's (`cr_ed_strips`, `cr_ed_row_strips` row 0 = the row's strips): a knob turn or the hot block coming or going
  composes strips 3–4 only (with a footer), host ~1.0 M instructions a frame while KNOB 1–4 turn every 30 ms
  (`tools/emu/perf.sh` h; a turn's frame <= 3.0 M, ~11.5 ms on the device); a new effect redraws all six.
- **The pictograms** (`CR_G_ROOM MOON ECHOES LFO CLIP SPRING MIX GATE RANGE ARROW SHIFT`, FORMAT.md "cell glyphs";
  `cr_cell_t.pct2` their second value, Q8 of 255): drawn at true size in the glyph box (2 px strokes 2 px inside it,
  not scaled by h / 64 as the params glyphs): room the box (`cr_srect`) and its far wall shrinking 70 → 22 % with four
  corner lines (`cr_poly`); moon a lit part (the right half less / plus an ellipse, `cr_moon_fill`: 4 corner samples a
  pixel, 16 where they disagree) inside a 2 px rim (`cr_arc`); echoes 3 px bars decaying by 0.2 + 0.75 pct2, spaced
  by pct, on a 1 px baseline; lfo 1–5 cycles (<= 44 segments), depth by pct2; clip one cycle at gain 1–10, dashed clip
  lines past 0.1; spring 13 zigzags; mix an outlined square behind one filled to pct; gate, range, arrow, shift as
  FORMAT.md. tests/cr_draw_test.c checks each one's ink stays in its box and changes with pct.
- **QUAD's primitives** (docs/QUAD.md; design/choralroot-fm1-quad-mockups.json, the user-approved sheet
  design/choralroot-fm1-quad-screens.png; the designer's drawWide `algo` / `ade2` / the filter's `bw`, drawPicto `harm`
  `detune` `lfowave`, cellRow's `big`, `ratio`, `span`, the bipolar knob). States 63–71 of `tests/gen_cr_screens.py`
  are that file's states 1–8 and 10 (ENV 3's three cells) (`QUAD_PICK`); `tests/cr_draw_test.c` renders all eight
  algorithms besides (`algo_1` .. `algo_8`, linted).
  - `CR_W_ALGO`: `wv` = algo (0: 1..8), fdbk (1: 0..255), mix (2: 0..255, 128 = X and Y alike). "ALGO" 10 px MID and
    the number 24 px at the left; the diagram is the **Digitone manual's** (Appendix A.3's figure; `CR_ALGOS`, the
    designer's `ALGOS` the same table: per algorithm up to four modulation arrows, the feedback operator, the X / Y
    carriers and the one each label sits under, the **direct** carriers, the carriers whose line leaves their box's
    right side, 3 or 4 rows, each box's row and column). `cr_algo_geom` places them: rows at y 13 / 37 / 61 (four rows:
    11 / 30 / 49 / 68, algorithm 4's stack), 32 px columns, a side line 15 px right of its box's centre, the diagram
    centred on x 137. The 1.5 px MID arrows box edge to box edge with a filled head (4.5 × 5 px, shorter in a short gap:
    `cr_tri`); each carrier's 1.5 px line to its label (the output mix favours in TEXT, the other DIM, both TEXT at
    128): **dotted for a direct carrier** (full level, unaffected by its operator envelope; `cr_poly` dashes 1.5 of
    3.5 px), **solid for an enveloped one**; the label's own carrier straight down (or from its side, then down) to the
    label's top, a second carrier down to the label's middle and across to its side (algorithms 3, 7, 8); the 14 px
    boxes (BG filled, 1.5 px outline, the letter 9 px); the feedback loop the manual's square bracket over its box's top
    left (up 3 px, left past the edge, down, a head back in), 1..3 px with fdbk (clamped to the band's top). The eight:
    1 A(fb)>C, B2>B1>C, X C, Y B1 · 2 A>C, B2(fb)>B1, X C, Y B1 · 3 A(fb)>C, B2, B1, X C + B2, Y B1 · 4 B2(fb)>B1>A>C,
    X C, Y B1 (side) · 5 B1(fb)>A, B2>A, A>C, X C, Y A (side) · 6 A(fb)>C, B1, B2>C, B1, X C, Y B1 · 7 A(fb)>C, B2>B1,
    X C + A, Y B1 + B2 (A, B1, B2 enveloped) · 8 A>C, B1(fb), X C + B2, Y B1 (B2, B1 enveloped); C always direct. Bounded:
    <= 4 arrows, 4 lines, 4 boxes, one loop.
  - `CR_W_ADE2`: `wv` = A's attack decay end level (0..3), B's (4..7) (0..255), the lit segment (8: 1..8 = A's attack
    decay end level, B's; 0 none). A in x 8..112, B in 128..232: each a 3 px TEXT polyline (rise to its level over 40 %
    of its room at attack 1, fall to its end over 40 % at decay 1, held; steep runs cut <= 26 px as the env band), a
    4 px level bar at its right (a LINE track filled to the level: the lit colour when a segment of that half is lit,
    else MID), its letter 10 px top left; the lit attack / decay / hold 4 px in the knob colour of (seg - 1) % 4 with
    dots at its ends (a lit level: the bar and letter only); a 1 px LINE divider at x 119.5.
  - `CR_W_FILTER`'s type for FM TONE: `wv` 7 = 0 the VA's (the position's name), 1 OFF (a flat line at the pass
    level, "OFF"), 2 a 12 dB slope (the name + "12": "LP12" "HP12"), 3 24 dB ("LP24": the response's dB doubled, two
    12 dB stages); the editor sets it from the TYPE column's name (cr_edit.c `ce_band_plan`).
  - `CR_W_FILTER`'s base-width window: `wv` 4 = shown (0 none: a cleared struct draws no window), 5 = base, 6 = width
    (Q8 of 255 of the band; the window base .. base + width, clipped at 255). The designer's lit region is the orange at
    12 % alpha; with no alpha here it is that composite as a **solid dim fill** (`ux_mix(T_BG, orange, 12)`, a dark
    brown, recomputed per palette) under the dashed 0 dB line and the curve — at 240 px a solid fill reads as the
    mock-up does, where a sparse dot pattern shimmered against the dashed lines. Its edges dashed 1.5 px (3 on 3 off)
    orange, `B` / `W` 9 px under them (pushed apart when closer than 12 px).
  - Glyphs: `CR_G_HARM` (pct: one cycle of sin + harmonics 2..7 at 1/k, the odd ones by 2 pct − 1, all by 1 − 2 pct,
    normalised: 40 segments), `CR_G_DETUNE` (pct: 16 segments, the joints jittered by 0.45 H pct from the designer's
    sin(i 78.233 + 1.7) table, every third segment dropped past 0.3, every other past 0.7), `CR_G_LFOWAVE` (`cr_cell_t.wave`
    = `CR_LW_TRI SINE SQUARE SAW RAMP EXP RANDOM`, pct = the start phase: one cycle, two in a span-2 cell, from pct of a
    cycle on; the points are the wave's breakpoints, both sides of each jump, and for sine / exp 16 samples a cycle:
    <= 37; a 1.5 px tick at the left at the start value), `CR_G_RATIO` (not a picture: the value "0.50/1.00" as a
    fraction, both numbers 13 px bold over / under a 1.5 px divider w − 14 wide, centred in the glyph-and-value space; a
    value without '/' draws as a big cell). Harm, detune, lfowave are 2 px strokes at true size, as the pictograms.
  - Cell flags (`flags` is 16 bits now; the mark's colour stays in bits 4..7): `CR_CF_BIG` (the value 7 px larger,
    20 px under a band / 24 px without, centred where glyph and value would be, no glyph, no bar; too wide: 2 px smaller
    at a time to 7 px, never ellipsised), `CR_CF_SPAN2` (edit8 and stack: the cell takes two columns from a column
    0..2, its label fits 116 px, its glyph box doubles, its bar spans both columns in its first column's colour; **the
    cells after it in the row shift a column right**, so cell index != column: `hot_c` is the cell's index, as the
    designer's), `CR_CF_BIPOLAR` (= `CR_CF_BIP`; on a `CR_G_KNOB`: the arc from 12 o'clock to the value, a 1.6 px mark
    over 12 o'clock). `cr_cell_t` is 30 bytes (label 14 for "Wave · Phase", value 10 for "0.50/1.00", flags 16 bits,
    the `wave` byte): + 7 bytes a cell, + 224 bytes a `cr_screen_t` (three in the firmware: `cu_scr` in the POOL, two
    statics).
  - Cache: the bands are hashed with the editor's `wv` as the others (a new algorithm composes strips 0–3 only, the
    partial draw equal to a full one: tested). `tests/cr_draw_test.c` checks the four boxes of every algorithm where
    `cr_algo_geom` puts them, the eight diagrams differing, no boxes / loops / side lines / labels overlapping or off
    the band, a direct line dotted and an enveloped one solid (algorithm 7), the loop thickening with fdbk, ade2's two curves and bars filled to their levels, the
    window's edges and fill (none when off), the fraction, the three glyphs' ink and change with pct, lfowave's seven
    waves, phase and two cycles, a big cell's missing bar and larger value, a span cell over two columns with the next
    cells shifted, the bipolar knob's arc flipping across 0.5. `gen_cr_screens.py` refuses a row whose spans pass the
    fourth column. Not done: span on the knob row (`CR_K_KNOBROW` draws cell[0][k] at column k), the single-envelope
    `ade` band.
- Not implemented (not on the device): the designer kinds `tiles list scope dial roundel splash loop notes`, `big`
  with `pct` (the inverted fill), knob cards, keycap footers, `bubbleStyle: "disc"`, the chord panel's `key`/`trans`.

## Test (`tests/run_cr_draw.sh`)

Builds `tests/cr_draw_test.c` (gfx.c + cr_gfx.c + cr_draw.c on a host framebuffer, as tests/ui_test.c), renders every
state settled (anim cleared, `anim_ms` 0) and mid-animation (80 ms), writes `build/cr_screens/<nn>_<slug>[_mid].png`,
`build/cr_screens/sheet.png` (the designer's sheet layout, 2× nearest) and `build/cr_screens/compare.png` (mock-up |
device | mid-animation per state). Lint (gfx.c `GFX_HOOK_TEXT`, boxes in screen rows, de-duplicated across strips):
no text off the screen, no two texts overlapping, no text on the ring's band. Checks: glyphs fit the buffer, the CRX
charset, the cache (0 / 1 / 6 strips), animations pure and settling. States 45–53 are design/choralroot-fm1-fx-mockups.json's 2 3 4 5 5b (knob rows), 6 7 (the
glyph studies, edit8) and 10 11 (the perform and bass layers); states 54–57 the layers sheet's 1 3 (KEY, the keyboard band), 6 (LOOP, no
ring), 7 (METRO); states 58–62 the preset sheet's 1 2 3 5 6; states 63–71 QUAD's 1–8, 10 (compare.png cuts them from
design/choralroot-fm1-quad-screens.png); state 16 is the main sheet's fx layer, a knob row. The pictograms and the knob row's hot
block and strips are checked too. Report: `build/cr_screens/report.txt`.
