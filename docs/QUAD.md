# QUAD: a Digitone-style four-operator FM engine (plan, 2026-10-07)

The user's ask: a new FM engine modelled on Elektron's Digitone. This is the plan and the screens. **Building since 2026-10-07** (the user approved the UI design): milestone 1 (the core) and the draw primitives, then the wiring (milestone 2, done 2026-10-08: "Status" at the end).
Mock-ups: `design/choralroot-fm1-quad-screens.png` (`design/make_quad_mockups.py`). The name on the device is
**QUAD** (four operators; "Digitone" is Elektron's name and "DIGITAL" is Felucca's retired engine 1).

## Why it fits

- Four operators in eight fixed algorithms are cheaper than FM6's six-operator Dexed core (the heaviest engine we
  run at 25-30 % of the block for a 6-note chord): a first estimate is 12-18 % for QUAD, which leaves room for the
  bass and the loop.
- The Digitone's idea is few knobs with a wide reach: two ratios (A, B), a harmonics control that reshapes the
  carriers, detune, feedback, a mix between two outputs, and two operator envelopes. That is exactly what the knob
  row and the editor's dense screens are for, and its macros fit the eight P_E slots the platform gives an engine.
- The platform already supplies what the Digitone has after the FM core: the multimode filter (the part's CUT / RES
  pages), the amp envelope (the ADSR, or the engine's own), two LFOs' worth of modulation (the LFO page and the
  matrix), overdrive (DIST), chorus / delay / reverb sends. QUAD implements only the FM core and its two envelopes.

## The voice (what we model; our own code, no Elektron code)

```
          ALGO 1..8 (the Digitone's eight routings of C, A, B1, B2; X and Y are the two outputs)
   ratios:  C fixed at 1.00 (its RATIO page offset only), A 0.25..16, B 0.25..16 (B1 and B2 share B, B2 = B x BR)
   HARM  -26..+26   the carriers' waveshape: 0 = sine; + adds the odd series (toward a square-ish wave),
                    - adds all harmonics (toward a saw-ish wave); implemented as a wavetable morph of 7 shapes a side
   DTUN  0..127     B1 / B2 detuned against each other (and A against C a little), cents
   FDBK  0..127     the feedback operator's self-modulation (which operator feeds back is the algorithm's)
   MIX   -63..+63   output X alone .. both .. Y alone
   ENV A: ATK DEC END LEV   the modulation index of operator A over time (attack to LEV, decay to END, held)
   ENV B: ATK DEC END LEV   the same for B1 and B2 (one envelope, as the Digitone)
   A / B DELAY 0..127       the envelopes start late (the Digitone's A/B DLY)
   TRIG  A RESET / B RESET  (0/1 each) whether an envelope restarts on every note (else free-running on legato)
   PHASE RESET  on/off      operators restart their phase at note-on (click-free off)
   KEY TRACK  A / B 0..127  the modulation index follows the key (brighter up the keyboard, or not)
   RATIO OFFSETS  C A B1 B2 -1.00..+1.00 fine ratio offsets (the Digitone's "ratio offset" page)
   LEVEL  A B               operator output levels (the modulation depth ceiling the envelopes scale)
```

The amp envelope is QUAD's own (`ownenv`, the Digitone's AMP page; decided by the user's page layout: unlike FM6 and CZ-1 the amp envelope is
the platform's, as the Digitone's AMP page is separate from its FM core), so the ENV group in the editor shows the
platform ADSR as for ANALOG, and QUAD's own two envelopes are deep pages under OSC / its own section (below).
Velocity scales the operator envelopes' LEV (a VEL amount, default 50 %).

Rendering: 44.1 kHz, per sample, four sine (or harmonic-table) lookups with linear interpolation on 2^12 tables,
phase accumulators 32-bit, the algorithm as a small switch over eight routing tables (each operator's modulators as
a bit set, the feedback operator and the X / Y outputs), the envelopes at control rate (every 32 samples) with
linear ramps per sample. Feedback as in the DX7 family (the average of the last two outputs). Harmonics as a morph
between 15 pre-computed 2^12 tables (sine, then 7 odd-series steps one side, 7 all-series steps the other) chosen by
HARM, two tables crossfaded. Budget target: 8 voices <= 18 % of the block; the first measurement decides `poly`.

## The eight P_E macros (EDIT 1 / EDIT 2) and HOME's knobs

The Digitone's SYN1 page, one for one (the user, 2026-10-07):

| EDIT 1 | ALGO | RATIO C | RATIO A | RATIO B |
| --- | --- | --- | --- | --- |
| **EDIT 2** | **HARM** | **DTUN** | **FDBK** | **MIX** |

RATIO C is a real ratio (0.25..16, as the Digitone's), not fixed. HOME's four knobs: RATIO A, HARM, FDBK, MIX. These are macros into the patch as the VA's are (a deep edit writes the
macro back).

## The deep pages (`eng_deep_t`), the editor's groups (the user's layout, 2026-10-07)

Values are shown as the Digitone shows them: **ratios are numbers, no bars** (Ratio C and B step 0.25 below 1 and 1
above 1, Ratio A 0.25 steps; **Ratio B is one knob over the pair B1 / B2**: it cycles B1 through its steps, then
increments B2 and B1 starts again, drawn as a fraction B1 over B2 with a horizontal divider); **Algo is a big
number**; Harm and Dtune have their own glyphs (the harm wave; detune a flat line that breaks up as it is turned).

| group | screen | KNOB 1..4 (row A / row B) | band / glyphs |
| --- | --- | --- | --- |
| OSC | 1 `SYN 1` | Algo · Ratio C · Ratio A · Ratio B / Harm · Dtune · Feedback · Mix | the algorithm diagram; Algo big number; ratios as numbers (B a fraction); harm and detune glyphs; Feedback a bar; Mix bipolar |
| | 2 `SYN 2` | Offset C · Offset A · Offset B1 · Offset B2 | one row: the fine ratio offsets, numbers |
| ENV (the operators) | 1 `ENV A/B` | A: Attack · Decay · End · Level / B: Attack · Decay · End · Level | a two-envelope band: A's and B's rise-fall-hold curves side by side, each with its level bar |
| | 2 `ENV 2` | A Delay · A Trig · A Reset · Phase reset / B Delay · B Trig · B Reset · – | no band |
| FILT | 1 `FILTER` | Attack · Decay · Sustain · Release (the filter envelope) / Freq · Reso · Type · Env depth | the filter response band (multimode: LP HP BP, as the VA's) |
| | 2 `FILTER 2` | Env delay · Key track · – · – / Base · Width · – · – | the base-width filter drawn as a window (two edges) on the response band |
| AMP ("master envelope") | `AMP` | Attack · Decay · Sustain · Release / Level · Pan · Drive · – | the AHDSR band (QUAD is `ownenv`: its own amp envelope, as the Digitone's AMP page; velocity to level) |
| LFO | `LFO 1`, `LFO 2`, `LFO 3` (one screen each, as the Digitone's three) | Speed · Multiplier · Fade · Dest / Wave + Start phase (one double cell on KNOB 1 and 2) · Trig mode · Depth | Speed is a **bipolar knob** -64..+64 with a centre detent (negative runs the LFO backwards), Multiplier the fixed series 1 2 4 8 16 32 64 128 256 512 1k 2k, Dest an enum over QUAD's parameters (the Digitone's list: the SYN and FILT pages' values, amp level, pan); **Wave and Start phase read as one parameter**: a double-width cell draws the chosen wave (tri, sine, square, saw, ramp, exp, random) shifted by the phase, so turning the phase knob slides the shape; Depth bipolar |
| MOD | the platform's routes | | as every engine (QUAD's two LFOs are its own; the platform's matrix still reaches the P_E macros) |
| FX, MIX | as every engine | | |

So QUAD implements, beside the FM core: a **multimode SVF** per voice (the VA's `dsp.c` primitives) with its ADSR
filter envelope (depth, delay, key track), a **base-width filter** (a high-pass at BASE and a low-pass at BASE +
WIDTH, both one-pole, per voice), its **amp envelope** (`ownenv`), and **two LFOs** with destinations (at control
rate, as the VA's). The eight P_E macros are the SYN 1 page's eight values; HOME's knobs: Ratio A, Harm, Feedback,
Mix. `mod_dst`: none (the platform's matrix reaches the macros only).

## The patch and the blob

About 70 values (the SYN pages, the two operator envelopes, the filter section, the amp envelope, three LFOs): ALGO (3 bits), RATIO A, RATIO B (7 bits each, a table of the Digitone's ratio steps), HARM (6 + sign),
DTUN, FDBK, MIX, LEVEL A, LEVEL B, 2 x (ATK DEC END LEV DELAY RESET KEYTRK), VEL, PHASE RESET, 4 ratio offsets, BR
(B2's ratio multiplier). Blob: `'Q'`, version 1, then one byte a value (all 7-bit-clean), ~80 bytes. A **patch store**
per user slot as `va_store.c` (one object: 16 + 32 x 80 = 2576 bytes in POOL; backup object id 22; the flash pair:
the free project sector pair 0x97000's neighbours are taken: use the free user-sample-slot-2 flash 0xB2000 / 0xB3000).

## Factory presets (the pool, after INIT)

About 16 at first, chosen by ear on the device later; starting points named for what they do: EP, BELL, BASS, PLUCK,
BRASS, GLASS PAD, HOLLOW, SQUARE LEAD, METAL, WOBBLE, CLAV, STRINGS, MARIMBA, DRONE, FEEDBACK, NOISE-ISH. Each a
list of (value index, value) edits over the init patch, as the VA's `VA_PRESET_EDITS`.

## Costs (estimates, to be measured)

| | estimate |
| --- | --- |
| CPU, 8 voices | 16-24 % of the 2.9 ms block (4 ops x table lookups, 2 operator envelopes, an SVF + a base-width filter per voice, the amp envelope, 3 LFOs; FM6 at 8 voices: 25-30 %) |
| RAM | per-voice state ~56 B x 8 = 448 B in `eng_state` (the union does not grow: FM6's member is larger); the part's patch 2 x 48 B |
| POOL | the patch store 1552 B (POOL is at 92.3 %: 26 KB spare) |
| flash | the 15 harmonic tables 15 x 4096 x 2 B = 120 KB? **too much**: use 2^10 tables (30 KB) or compute the odd / all series as a sum of 8 sines at init into RAM? No RAM for that. Decision: 2^10-point tables, 15 of them, 30 KB of flash (XIP at 60.6 %: fine), interpolated |
| code | ~12 KB |

## Milestones

1. **Core + host test** (`firmware/src/eng_quad.c`, `tests/cr_quad_test.c`): the 8 algorithms against a reference
   rendering (a Python model in `tests/quad_ref.py` producing goldens from the same equations), the envelopes'
   timing, the harmonic tables, no int32 wrap at full feedback, voices end, the blob round trip, every preset renders.
   `tests/regress.c` goldens and CPU baselines.
2. **The platform**: `ENGINES[15]`, `ENGINE_ORDER` after FM6 (ANALOG, FM6, QUAD, VA, PHASE, CZ-1, ...), `FELUCCA_QUAD`
   flag (1 in choralroot.c, the emulator, regress; 0 for Felucca), `eng_state` member, the patch store `quad_store.c`
   beside `va_store.c` with its backup id, the installer tools' knowledge of it (docs/SOUNDS.md: patch kind `quad`).
3. **The editor**: the deep pages, the `ALGO` band and the `ADE` band in `cr_draw.c`, the `harm` glyph, the engine
   in the picker, the pool (presets), the knob-row cells where the engine's parameters show up (the picker's KNOB 1).
4. **Presets by ear** on the device, `perf.sh` scenario (i): a 6-note QUAD chord + bass + loop.

## Open questions for the user

- The name: QUAD (proposed), or something else?
- How close to the Digitone's parameter ranges and ratio tables do you want it (exact ratio steps and the HARM
  curve are from listening and public manuals, not Elektron's code)?
- Should QUAD's own two envelopes also be able to be the amplitude (an `ownenv` option, like the Digitone's A env
  on a carrier in some algorithms), or keep the platform ADSR as the amp always (simpler, proposed)?

## Implementation (milestone 1, 2026-10-08)

Files: `firmware/src/eng_quad.c` (the engine, `ENG_QUAD`, `QUAD_DEEP`), `firmware/src/quad_tables.h` (generated by
`tests/quad_ref.py`, committed), `tests/quad_ref.py` (the table generator and the reference model),
`tests/quad_goldens/*.json` (12 renders), `tests/cr_quad_test.c` (in `tests/run_cr_tests.sh`), `tests/run_quad_test.sh`
(regenerate, test, CPU). Not wired yet (milestone 2): see "For the wiring" below.

**The voice.** Per sample: four 32-bit phases; modulators are `sine_i` (Felucca's SINE, RAM); carriers (the X / Y
operators of the algorithm) read the HARM tables when HARM != 0. A modulator's output (Q15, at its level) is a phase
offset of `o << 18` (full level = 2 cycles, index ~12.6 rad). Feedback: the feedback operator's own wave (before its
level), `(y[n-1] + y[n-2]) x FDBK^2 x 2 << 1` (FDBK 127 = one cycle of the average: saw-like near 64, noise above
~90). Levels: operator A = ENV A x LEV^2 (C is always at full level: the amp envelope shapes it), B1 and B2 = ENV B x B
LEV^2; ramped per sample (so a B carrier on Y is only as loud as ENV B x B LEV^2: BELL's Y alone is 16.6 dB under its
X, the init patch's B LEV 0 silences Y; at MIX +36 X still covers Y: the 2026-10-08 report "no change when B1 / B2
change" was this, the edit reaches the voice live). MIX: `X x (63 - MIX) / 126 + Y x (63 + MIX) / 126` (equal sum: algorithms 1-3, X = Y = C,
do not change level with MIX). Then a DC blocker (a one-pole high-pass at ~8 Hz, `QUAD_DC_K` 75 Q16, Q12 state): the
operators are not DC-free. The feedback's average lags 1.5 samples, which skews the feedback operator's saw (on a plain
sine its mean is -12 % of its RMS at FDBK 60, -61 % at 100; a float model of the same loop gives the same), and at
near-unison ratios a modulator's phase offset (that mean, or DTUNE's drift of 0.06..0.5 Hz) is `J1(I) sin(offset)` at
0 Hz in the carrier (BRASS held: +57 % of its RMS). The Digitone and the DX7 AC-couple their outputs; QUAD does it per
voice, before the SVF and its knee. Then the SVF (dsp.c's, LP / HP / BP as the VA's switch, bypassed when LP, FREQ 127 and no
resonance), the soft knee, the base-width filter, the amp. The routings mirror the designer's `ALGOS` (`QUAD_ALGO` in
eng_quad.c; the test checks it against a transcription of `index.html`):

| algo | modulations | feedback | X | Y |
| --- | --- | --- | --- | --- |
| 1 | A>C, B1>A, B2>B1 | B2 | C | C |
| 2 | A>C, B1>C, B2>B1 | B2 | C | C |
| 3 | A>C, B1>A, B2>A | B2 | C | C |
| 4 | A>C, B2>B1 | B2 | C | B1 |
| 5 | A>C, B1>A | B2 | C | B2 |
| 6 | A>C, A>B1, B2>B1 | B2 | C | B1 |
| 7 | B2>B1 | A | C + A | B1 |
| 8 | none | B2 | C + A | B1 + B2 |

**Ratio tables.** RATIO C and B1: 19 steps `0.25 0.50 0.75 1 2 3 .. 16` (index 0..18, 1.00 = 3). RATIO A: 64 steps
`0.25 .. 16.00` by 0.25 (index = ratio x 4 - 1, 1.00 = 3). **RATIO B (the pair, version 2, 2026-10-08)**: B1 and B2 each a C/B
step (patch values `QP_RB1`, `QP_RB2`, 0..18 each); the deep column is one value 0..360 = `B2 x 19 + B1` (`QUAD_NRB`
361, B2-major): one detent steps B1, and past B1's last step (16) the next detent carries into B2 (B1 back to 0.25,
B2 one step up); turning down the same backwards; 0 = 0.25/0.25 and 360 = 16.00/16.00 hold (no wrap). Default 60 =
1.00/1.00; e.g. 3 x 19 + 1 = 58 = 0.50/1.00. `quad_get` / `quad_set` join and split the pair; the macro `P_E3`
(`ENG_QUAD.edit[3]`, `preset_t.e[3]`) is B1's step only (0..18: the user preset record keeps a macro in one byte,
-64..191), so a knob, the matrix or motion on `P_E3` moves B1 and leaves B2. **SHIFT (GLO held or latched) on RATIO B
steps B2** (+-19 in the deep value, B1 kept; B2 stops at 0.25 and 16.00; the title line reads `fine · B2`). The texts:
the column's name list is `N_QUAD_RCB` (19 names over 361 values: cr_edit.c's *ratio pair*, `ce_pair`: B1 =
names[v % 19], B2 = names[v / 19], no engine hook); the cell carries B1 and B2 in quarters (`cr_cell_t.pct` /
`pct2`) and cr_draw.c writes each `%u.%02u` ("16.00" over "16.00"); the trace line "4.00/16.00" (until 2026-10-08:
361 generated names `N_QUAD_RB`, ~5 KB flash, and the 10-byte cell text cut "16.00/1.00" to "16.00/1.0").
Version 1 (until 2026-10-08) had 114 pairs `BR x 19 + B1`, B2 = B1 x BR, BR in `0.5 1 1.5 2 3 4` (`QUAD_BR_Q16`,
kept for the loader below). Offsets: -100..+100 =
-1.00..+1.00 added to the ratio (clamped at 0). DTUNE: B1 -, B2 + `DTUNE x 7.5 / 65536` (+-25 cents at 127, B1 / B2
50 cents apart), A + a quarter of that (+6 cents); a factor on the ratio. Increments: `pitch inc x ratio (Q16) x
detune (Q16)` in 64-bit, per tick.

**HARM.** 15 tables x 1025 int16 (30.8 KB, `const`: XIP flash): 0 sine; 1..7 the odd series `sum sin(nx) / n`, n odd up
to 3, 5 .. 15 (toward a square); 8..14 every harmonic up to 3, 5 .. 15 (toward a saw); each scaled to a peak of 32767.
HARM h: position |h| x 7 / 26 between table floor and floor + 1 (the + side for h > 0, the - side below), crossfaded
(Q15); +-26 is table 7 / 14 alone. The two tables share one index per sample.

**Envelopes.** Operator A and B: DELAY (`ENV_LIN` time), ATK (linear to 1 in `TIME_MS_X10`), DEC (exponential to
END / 127, 99 % in the DEC time), then held at END whatever the gate (attack-decay-end). LEV: 0..127, squared; VEL:
`32767 - VEL x (127 - velocity) x 2080 / 1024` on both LEVs; KTRK: `1 + (note - 60) x KTRK / (48 x 127)` (127: x2 four
octaves up), clamped to full. TRIG on: a retrigger (or a MONO / LEGATO legato move, `engine_t.legato`) restarts the
envelope, off: it holds; RESET on: the restart from 0, off: from where it is. PHASE RESET on: a retrigger zeroes the
phases (a fresh voice always starts at 0). Filter envelope: an ADSR with DELAY (the VA's envelope code); DEPTH -64..63
x the envelope = +-126 cutoff steps; KTRK 127 = 1 octave an octave (the VA's). Amp: an ADSR, ends the voice
(`done`), velocity at a fixed half (`32767 - (127 - vel) x 129`), LEVEL squared (`LEVEL^2 x 2`, Q15), UNISON x 2/5.

**Base-width filter.** BASE / WIDTH 0..127 on CUTOFF_HZ's scale (30 Hz x 533^(v/127)); a one-pole high-pass at BASE
(off at BASE 0) then a one-pole low-pass at BASE + WIDTH (off at >= 127); Q16 coefficients `QUAD_BW_K`, Q8 states,
64-bit products.

**LFOs** (three, per voice, control rate, read then advanced). Rate: `f = SPEED x MULT / 320 Hz` (SPEED 32 x1 = 0.1
Hz; SPEED 16 x8 = 0.4 Hz; 64 x 2k = 409.6 Hz), the increment a tick `SPEED x MULT x 9739`; negative SPEED runs
backwards, 0 stops. MULT 0..11 = x1 .. x2k. TRIG: FREE (the part's phase), TRIG (the voice's, from START PHASE at the
note), HOLD (the part's phase at the note, frozen), ONE (one cycle, then stops), HALF (half a cycle). START PHASE x
2^25. WAVE: TRI (0 at phase 0, rising), SINE, SQR, SAW (bipolar, rising through 0), RAMP (unipolar, falling), EXP
(unipolar, `(1 - x)^4`), RAND (a new value each cycle). FADE: a fade-in over the FADE time (as the VA's). DEPTH
-64..63: `wave x DEPTH / 64`. DEST (summed over the three, at full depth): HARM +-26, DTUNE +-127, FDBK +-127, MIX
+-126, RATIO A +-32 steps, RATIO B +-18 steps (B1 and B2 the same steps), FREQ +-127 cutoff steps, RESO / LEVEL / A LEV / B LEV
+-127, PAN +-64 (the part's: `quad_pan` for fx.c, from the latest note's voice, as `va_pan`).

**DRIVE and PAN are not in the patch**: AMP+'s PAN and DRIVE columns read and write the part's `P_PAN` and `P_DIST`
(the platform's DIST insert is QUAD's drive), on any part. A sound's PAN and DRIVE travel as the track's parameters
(a preset's `FX()` DIST send, a user slot's values), not in the blob.

**The blob** (`QUAD_BLOB` 80 bytes): `'Q'` (0x51), version 2, then a byte per value (value - min, 0..200: the offsets
and SPEED are not 7-bit clean), zeros to 80 (72 values + 6 zero bytes). A bad magic, version, out-of-range byte or
non-zero padding -> the init patch. **Version 1** (71 values, byte 3 RATIO B 0..113 = `BR x 19 + B1`, padding from
byte 73) is still read (`quad_blob_ok`, so a v1 patch store stays valid; `quad_unpack` converts): B1 = its B1, B2 = the
grid step nearest B1 x BR (the lower one when halfway), the rest added to OFS B2 (rounded to 1/100, clamped at +-1.00:
exact for the half steps, e.g. 7 x 1.5 = 10 + 0.50; 0.25 x 0.5 = 0.25 - 0.13, 16 x 4 = 16 + 1.00 are not). Blobs are
written as version 2. A version-1 macro (`P_E3` 19..113 in a project or a record from before) is taken the same way
by `quad_track_loaded` (B1 to `P_E3`, so a factory preset still matches). Values (index name min..max (init)):
0 ALGO 1..8 (1); 1 RATIO C 0..18 (3); 2 RATIO A 0..63 (3); 3 RATIO B1 0..18 (3); 4 HARM -26..26 (0); 5 DTUNE 0..127
(0); 6 FDBK 0..127 (0); 7 MIX -63..63 (0) [0..7 = P_E0..P_E7]; 8..11 OFS C A B1 B2 -100..100 (0); 12..15 A ATK DEC END
LEV (0 60 64 48); 16..19 B ATK DEC END LEV (0 60 0 0); 20 A DLY (0); 21 A TRIG 0..1 (1); 22 A RESET 0..1 (1); 23 PHASE
RESET 0..1 (1); 24 B DLY (0); 25 B TRIG (1); 26 B RESET (1); 27 VEL (64); 28 A KTRK (0); 29 B KTRK (0); 30..33 filter
ATK DEC SUS REL (0 64 0 40); 34 FREQ (127); 35 RESO (0); 36 TYPE 0..2 LP HP BP (0); 37 DEPTH -64..63 (0); 38 F DELAY
(0); 39 F KTRK (0); 40 BASE (0); 41 WIDTH (127); 42..45 amp ATK DEC SUS REL (0 64 127 40); 46 LEVEL (100); 47..54,
55..62, 63..70 LFO 1..3: SPEED -64..64 (16), MULT 0..11 (3), FADE (0), DEST 0..12 (0), WAVE 0..6 (0), PHASE (0), TRIG
0..4 (0), DEPTH -64..63 (0); 71 RATIO B2 0..18 (3) (version 2). (Unlisted ranges 0..127.)

**The pages as implemented** (`eng_page_t`, 4 columns each; a mock-up screen = row A + row B = two pages;
`section = {0, 3, 7, 14, 0xFF}`: OSC, FILTER, ENV, LFO, **no MOD**):

| # | title | columns | | # | title | columns |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | SYN 1 | ALGO, RATIO C, RATIO A, RATIO B | | 10 | ENV 2+ | B DLY, B TRIG, B RESET, VEL |
| 1 | SYN 1+ | HARM, DTUNE, FDBK, MIX | | 11 | ENV 3 | A KTRK, B KTRK, -, - |
| 2 | SYN 2 | OFS C, OFS A, OFS B1, OFS B2 | | 12 | AMP | ATK, DEC, SUS, REL |
| 3 | FILTER | ATK, DEC, SUS, REL | | 13 | AMP+ | LEVEL, PAN (P_PAN), DRIVE (P_DIST), - |
| 4 | FILTER+ | FREQ, RESO, TYPE, DEPTH | | 14 | LFO 1 | SPEED, MULT, FADE, DEST |
| 5 | FILT 2 | DELAY, KTRK, -, - | | 15 | LFO 1+ | WAVE, PHASE, TRIG, DEPTH |
| 6 | FILT 2+ | BASE, WIDTH, -, - | | 16, 17 | LFO 2, LFO 2+ | as LFO 1 |
| 7 | ENV A | A ATK, A DEC, A END, A LEV | | 18, 19 | LFO 3, LFO 3+ | as LFO 1 |
| 8 | ENV B | B ATK, B DEC, B END, B LEV | | | | |
| 9 | ENV 2 | A DLY, A TRIG, A RESET, PHASE | | | | |

Deviations from the plan's table: FILTER comes before ENV (the sections ascend: OSC FILTER ENV LFO); ENV 2+'s empty
column holds VEL and a one-row **ENV 3** (A KTRK, B KTRK) was added: the brief's key tracks and VEL had no place on the
user's layout. Value formats (existing `F_*` kinds only): ALGO `F_INT` ("3"); RATIO C / A / B and the offsets `F_INT`
with a 0-terminated name list (`N_QUAD_RCB` "2.00" (RATIO B: the same 19 names over its 361 values, a pair), `N_QUAD_RA`,
`N_QUAD_OFS` "+0.01"; params.c names value v with names[v - min] (a list shorter than the range: spread evenly, RATIO
B's B2); the editor draws RATIO B's pair itself, B1 over B2); HARM, MIX, SPEED, the depths, PAN `F_OFS` ("+8"; 0 at the middle of a
symmetric range); MULT `F_ENUM` "x16"; TYPE, WAVE, TRIG, DEST `F_ENUM`; TRIG / RESET / PHASE `F_ONOFF`; times `F_TIME`;
FREQ `F_CUTOFF`; RESO, SUS, KTRK, VEL, DRIVE `F_PCT`; BASE, WIDTH, END, LEV, LEVEL, START PHASE plain `F_INT`.

**Presets**: EP, BELL, BASS (MONO), PLUCK, BRASS, GLASS PAD, HOLLOW, SQUARE LEAD (MONO), METAL, WOBBLE (MONO), CLAV,
STRINGS, MARIMBA, DRONE, FEEDBACK, NOISE-ISH: edit lists over the init patch, starting points by design (to be tuned by
ear). A 6-note chord (D4 F#4 A4 B4 C#5 E5, one note D2 for the MONO ones) at LEVEL 92 peaks at 30-72 % FS. Version 2
re-expressed each preset's pair as B1 / B2 steps (`QRB(b1, b2)`, x4 as `QRC`); two were not on the grid (B2 = B1 x 1.5):
METAL 7/10.5 and DRONE 1/1.5 are now 7/10 and 1/1 with OFS B2 +0.50 (the same Q16 increments: regress's 16 renders
unchanged).

**Tests** (`cr_quad_test`, 22 783 checks, 0 failed (2026-10-08: RATIO B's walk 0..360, the carry, the ends, the texts; version-1 blobs and macros; RATIO B heard: BELL algorithm 4, MIX +63, B LEV 30, the editor's set 1.00/1.00 -> 2.00/1.00 doubles Y's zero-crossing pitch on the next note and live within the note, B2 changes the samples, B LEV 0 silences Y); built with `-fsanitize=signed-integer-overflow`): blob round trip of
2000 random patches, every bad blob -> init; the pages against the ranges (every value on exactly one column), the
sections, the macros = SYN 1 = `ENG_QUAD.edit`; set clamps, PAN / DRIVE to the part; the macros both ways; the 16
presets (blob, macros = `preset_t.e`, `env` = the amp ADSR, `quad_track_loaded`); the ratio tables and the pair's names;
the routings against the designer's ALGOS; **against the model**: each algorithm and EP / BASS / GLASS PAD / WOBBLE,
two notes for 0.3 s (released at 0.2 s): SNR over the first 2048 samples 53.6-68.4 dB (> 36 required) **and** every
10 ms block's RMS within 0.22 dB (1.5 required); envelope times (ATK, DEC to 99 %, DELAY within 2 ms / 10 %; LEV; TRIG,
RESET, PHASE RESET, legato); LP / HP / BP and the base-width window (> 12 / > 9 dB outside, < 1-3 dB inside); LFO rates
at six settings within 2 % (FREE and TRIG), backwards, SPEED 0, the start phase, ONE / HALF stopping, the seven waves;
192 extreme patches (every algorithm, FDBK 127, levels 127, HARM +-26, MIX -63 / 0 / 63, three LFOs at full depth, RESO
127, each filter type, notes 12..127): no int32 wrap, the voices end; every preset's chord: peak < 0.9 FS, not silent,
voices gone after the release.

**CPU** (`tests/run_quad_test.sh`, the test's driver, 8 voices held, host instructions a sample -> device % of the 2.9
ms half = instructions x 128 / 259 / 2902): the presets 811..1398 (13.8-23.8 %), average 995 (16.9 %), worst STRINGS
(algorithm 8: four HARM carriers between two tables, plus the SVF); VA LUSH PAD on the same driver 1478 (25.2 %): QUAD is
0.68 x the VA. A worst-case patch (algorithm 8, HARM between tables, BP, both base-width poles, three LFOs) 1538 (26.2 %).
Risk on the device: the HARM tables are in XIP flash (no RAM for 30.8 KB); per-sample reads from two 2 KB tables a
carrier may miss the flash cache. Measure on the device (perf.sh) before raising `poly` or adding a fifth carrier read.

**For the wiring (milestone 2).** core.h: `FELUCCA_QUAD` (0 by default, 1 in choralroot.c, the emulator,
tests/regress.c), `NENGINES += FELUCCA_QUAD`, `ENGI_QUAD` = 15 (after CZ-1, 14; eng_quad.c defaults it to 15u when core.h
does not define it). engines.c: `#include "eng_quad.c"` (it includes `quad_tables.h`), `ENGINES[15] = &ENG_QUAD`,
`ENGINE_ORDER` ANALOG, FM6, QUAD, VA ..; no `eng_state` member (QUAD's state is its own pool arrays: `quad_patch` 2 x 72
B, `quad_mlast` 32 B, `quad_lfo` 48 B, `quad_vs` 2 x 8 x 124 B = 1.98 KB, all `.pool`). Call `quad_track_loaded(t)` from
`fm6_track_loaded` beside `va_track_loaded`; fx.c `mix_part`: `pan = quad_pan(t, pan)` beside `va_pan`. The patch store
(`quad_store.c` beside va_store.c): records of `QUAD_BLOB` = 80 bytes (16 + 32 x 80 = 2576 B), set `quad_store_read` and
`quad_user_pending` as the VA's. cr_edit.c's `ce_deep` requires five ascending sections: QUAD has four (no MOD: 0xFF),
so it must accept a missing MOD section; the LFO screens are "LFO n" + "LFO n+" pairs (one edit8 screen per LFO in the
mock-up, where ce_scr's OSC / LFO stacks would put the three "LFO n" in one stack); SYN 1 / SYN 1+ likewise one edit8
screen. tests/regress.c enumerates `ENGINES[]`: with `FELUCCA_QUAD 1` there, its `preset/QUAD/*` goldens and `cpu/QUAD/*`
baselines are new entries to record (`tests/golden.txt`, `tests/cpu_baseline.txt`) once QUAD is registered.

## Status (milestone 2: wired, 2026-10-08)

**2026-10-08, RATIO B as the full grid** (the user: B1 steps through 0.25 .. 16, then B2 steps, both directions): the
pair is B2 x 19 + B1 (361), the blob version 2 with B2 at value 71, version 1 read and converted (above, "Ratio tables",
"The blob"); the presets' sounds unchanged; the QUAD store's own header stays version 1 (its layout did not change).

QUAD is engine 15 of ChoralRoot (`FELUCCA_QUAD` 1 in `choralroot.c`, the emulator and `tests/regress.c`; 0 by default:
Felucca's unit builds as before). Where:

- **Registration**: `core.h` (`FELUCCA_QUAD`, `NENGINES` += 1, `ENGI_QUAD` = 13 + SLICE + VA + CZ = 15; it needs
  `FELUCCA_CZ`: engines.c errors without it), `engines.c` (the include, `ENGINES[15]`, `ENGINE_ORDER` ANALOG FM6 **QUAD**
  VA PHASE CZ-1 ..: the third white root of the engine picker, OPT + PRESETS +1 from FM6), `eng_fm6.c fm6_track_loaded`
  calls `quad_track_loaded`, `fx.c mix_part` `quad_pan`. `tools/build.py` passes `FELUCCA_QUAD` from the environment.
- **The settings record** has no room for a twelfth engine's pool places (docs/SETTINGS.md): `cr_settings.c crs_slot`
  keeps the eleven v6 slots for the other engines (a 0.13 record means what it meant), the chord part's QUAD place in
  the retired byte `rsv_usb` (0: none yet), the bass part's from `bass_sound` when the bass plays QUAD. NOISE, now the
  twelfth melodic engine, has no white root in the picker (eleven roots D4..G5); KNOB 1 still reaches it.
- **The patch store** `quad_store.c` (included by `upreset.c` after `cz_ustore.c`): one storage object `OBJ_QUADSTORE`
  (= `OBJ_CZBANK0 + 8`) on the A/B pair 0xB2000 / 0xB3000 (the user sample slot 2's flash), 16-byte header "QUDS"
  version 1, 32 slots, blob 80, the used mask + 32 x 80 = 2576 bytes, mirrored in the pool; `quad_store_saved /
  _loading / _boot / _get / _put / _valid` as the VA's; backup object **22** (`cr_backup.c`: LIST / GET / PUT, a PUT
  validated by `quad_store_valid`); SAFE MODE's Flash Data erases the pair (56 sectors). Clients: `web/fm1backup.js`
  `CR_BACKUP_IDS`, `tools/fm1_install.py` `CR_IDS` ("QUAD patches").
- **The Sounds tools**: patch kind `quad` (80 bytes, 'Q' 1; the firmware writes 'Q' 2 since 2026-10-08 and reads both:
  the clients' version check must take 2 as well) in `web/fm1sounds.js` and `tools/fm1_install.py` (object 22 read when
  listed); no .syx for QUAD (docs/SOUNDS.md). `tests/sound_templates.c` prints QUAD's factory names (pinned
  in both clients) and a `quad` template and `quad_blob` fixture (EP).
- **The editor**: QUAD's screen plan (`QUAD_SCREENS`, `eng_deep_t.screens`; docs/EDITOR.md §4 "QUAD"): the mock-ups'
  eight screens (OSC SYN 1 under the algorithm, SYN 2; FILT FILTER, FILTER 2 with the window; ENV A/B, ENV 2, ENV 3,
  AMP; LFO 1..3 a screen each, Wave · Phase one span cell); MOD is the platform's routes (QUAD has no MOD section; the
  quick mapping says "not modulatable"). Ratios step one value a detent (RATIO B: one pair, B1 first, then the carry into
  B2; SHIFT: B2 a step, B1 kept). The engine's display name is "FM TONE" (`ENG_QUAD.name`; identifiers, files, the patch kind and the blob stay
  QUAD).
- **Tests**: `tools/emu/scripts/cr_quad.txt` (test_cr.sh "QUAD"), `cr_quad_ratiob.txt` (RATIO B heard: BELL on Y,
  B1 2.00 -> 4.00 an octave up in the WAV, `tools/emu/wavpitch.py`; SHIFT steps B2; the fraction uncut), `quad_persist_set/check.txt` (test_persist.sh), perf.sh
  scenario (i); `tests/cr_backup_test.c` (object 22, a QUAD sound through PUT), `tests/golden.txt` and
  `tests/cpu_baseline.txt` (the 16 presets), `tests/target_budget.py` lists `quad_render` / `quad_block` (their budget
  lines: the next device build, `BUDGET_UPDATE=1`).
- **DC**: `preset/QUAD/04_BRASS` failed regress's health check (DC -752, limit 400; FEEDBACK 389, EP -93): fixed in
  the engine by the DC blocker after the operators (above; regress DC now within +-17 on all 16, cr_quad_test checks
  every preset held and a plain feedback operator over FDBK 0..127). The presets are to be tuned by ear anyway
  (milestone 4). A QUAD voice takes two budget units (the default; FM6 takes one): perf (i) gives up 70 voices to the budget
  (budget fades, no clicks). Device build 2026-10-08: XIP 71.1 %, RAM 81.3 %, POOL 93.8 % (docs/INTEGRATION.md); `quad_render` costs 6089
  in the target budget (the VA's `va_render` for comparison: see `tests/target_budget.txt`); the draw code's HARM and LFOWAVE
  glyphs lost two 64-bit divisions the pi32v2 linker has no `__divdi3` for.
