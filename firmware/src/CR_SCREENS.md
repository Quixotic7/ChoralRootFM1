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
ARP GEEK TEXT BIG SCOPE EDIT8 STACK`), the top line (`header`, `icon` none/play/rec/loop, `mid`/`mid_col`, `right`/`right_col`,
`batt`), `footer` (one line; empty = none, the panel then runs to row 240), the ring (`ring_on`, `ring` Q8,
`ring_rec`, `ring_col`), `message`/`message_col`, and the panel fields of every kind (fixed char arrays; the chord
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
  an optional wide band (`wide` `CR_W_ENV`: `wv` a h d s r + the lit segment, the AHDSR as one 3 px polyline, its
  steep runs cut into pieces <= 26 px tall so cr_poly's boxes stay small, the lit segment 4 px in the hot cell's
  colour, A H D S R under it; `CR_W_FILTER`: `wv` cut res ftype (0..127) drive, the 2nd-order response of
  the LP / BP / HP mix crossfaded at the FTYPE position (0 LP, 32 BP, 64 HP, 96 NOTCH = LP + HP, back to LP;
  the type word its name, `cr_ftype_name`) sampled at 7 quarter-octave offsets from the cutoff in 32-bit log2 arithmetic, the edges extrapolated, clamped to the band
  with the crossings: <= 12 segments, over a dashed 0 dB line) over two rows of four `cell`s (each `cr_cell_t` carries its label, value, `CR_CF_*` flags, glyph, pct); stack: `head` column headings at y 36 over
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
- Not implemented (not on the device): the designer kinds `tiles list scope dial roundel splash loop notes`, `big`
  with `pct` (the inverted fill), knob cards, keycap footers, `bubbleStyle: "disc"`, the chord panel's `key`/`trans`.

## Test (`tests/run_cr_draw.sh`)

Builds `tests/cr_draw_test.c` (gfx.c + cr_gfx.c + cr_draw.c on a host framebuffer, as tests/ui_test.c), renders every
state settled (anim cleared, `anim_ms` 0) and mid-animation (80 ms), writes `build/cr_screens/<nn>_<slug>[_mid].png`,
`build/cr_screens/sheet.png` (the designer's sheet layout, 2× nearest) and `build/cr_screens/compare.png` (mock-up |
device | mid-animation per state). Lint (gfx.c `GFX_HOOK_TEXT`, boxes in screen rows, de-duplicated across strips):
no text off the screen, no two texts overlapping, no text on the ring's band. Checks: glyphs fit the buffer, the CRX
charset, the cache (0 / 1 / 6 strips), animations pure and settling. Report: `build/cr_screens/report.txt`.
