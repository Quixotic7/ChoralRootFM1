# QUAD: a Digitone-style four-operator FM engine (plan, 2026-10-07)

The user's ask: a new FM engine modelled on Elektron's Digitone. This is the plan and the screens; nothing is built.
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
number**; Harm and Dtune have their own glyphs (the harm wave; two beating waves for detune).

| group | screen | KNOB 1..4 (row A / row B) | band / glyphs |
| --- | --- | --- | --- |
| OSC | 1 `SYN 1` | Algo · Ratio C · Ratio A · Ratio B / Harm · Dtune · Feedback · Mix | the algorithm diagram; Algo big number; ratios as numbers (B a fraction); harm and detune glyphs; Feedback a bar; Mix bipolar |
| | 2 `SYN 2` | Offset C · Offset A · Offset B1 · Offset B2 | one row: the fine ratio offsets, numbers |
| ENV (the operators) | 1 `ENV A/B` | A: Attack · Decay · End · Level / B: Attack · Decay · End · Level | a two-envelope band: A's and B's rise-fall-hold curves side by side, each with its level bar |
| | 2 `ENV 2` | A Delay · A Trig · A Reset · Phase reset / B Delay · B Trig · B Reset · – | no band |
| FILT | 1 `FILTER` | Attack · Decay · Sustain · Release (the filter envelope) / Freq · Reso · Type · Env depth | the filter response band (multimode: LP HP BP, as the VA's) |
| | 2 `FILTER 2` | Env delay · Key track · – · – / Base · Width · – · – | the base-width filter drawn as a window (two edges) on the response band |
| AMP ("master envelope") | `AMP` | Attack · Decay · Sustain · Release / Level · Pan · Drive · – | the AHDSR band (QUAD is `ownenv`: its own amp envelope, as the Digitone's AMP page; velocity to level) |
| LFO | 1 `LFO 1`, 2 `LFO 2` | Speed · Mult · Fade · Dest / Wave · Phase · Mode · Depth | the lfo glyph (density = speed, height = depth); Dest an enum over QUAD's parameters (the Digitone's list: the SYN and FILT pages' values, amp level, pan) |
| MOD | the platform's routes | | as every engine (QUAD's two LFOs are its own; the platform's matrix still reaches the P_E macros) |
| FX, MIX | as every engine | | |

So QUAD implements, beside the FM core: a **multimode SVF** per voice (the VA's `dsp.c` primitives) with its ADSR
filter envelope (depth, delay, key track), a **base-width filter** (a high-pass at BASE and a low-pass at BASE +
WIDTH, both one-pole, per voice), its **amp envelope** (`ownenv`), and **two LFOs** with destinations (at control
rate, as the VA's). The eight P_E macros are the SYN 1 page's eight values; HOME's knobs: Ratio A, Harm, Feedback,
Mix. `mod_dst`: none (the platform's matrix reaches the macros only).

## The patch and the blob

About 70 values (the SYN pages, the two operator envelopes, the filter section, the amp envelope, two LFOs): ALGO (3 bits), RATIO A, RATIO B (7 bits each, a table of the Digitone's ratio steps), HARM (6 + sign),
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
| CPU, 8 voices | 16-24 % of the 2.9 ms block (4 ops x table lookups, 2 operator envelopes, an SVF + a base-width filter per voice, the amp envelope, 2 LFOs; FM6 at 8 voices: 25-30 %) |
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
