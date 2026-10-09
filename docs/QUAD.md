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

Since 2026-10-08 (the Digitone review, blob version 3) the voice follows the Digitone manual (OS 1.44: §11.3-11.12,
§9.5, Appendix A and C) wherever it says something; where it gives no number (DTUN's curve, FDBK's scale, the key
scaling law, the HARM partials) the curves are ours, listed under "Implementation".

```
   ALGO 1..8     the manual's eight routings of C, A, B1, B2 (Appendix A.3), two outputs X and Y; each output
                 carrier direct (full level) or enveloped (its envelope x level)
   ratios        C 0.25..16 (19 steps), A 0.25..16 (64 steps of 0.25), B = the pair B1 / B2 (19 steps each), one knob:
                 B2 the fast hand, B1 steps when B2 wraps (the manual's watch)
   HARM -26..+26 the 26-wave additive series (Appendix A.6), interpolated; - shapes C, + shapes A and B1, B2 never
   DTUN 0..127   A up, B2 down: 0..64 a few cents (to 6), 64..127 wide (to 50 cents)
   FDBK 0..120   the algorithm's feedback operator; 35 = a saw
   MIX -64..63   X alone .. Y alone
   ENV A, ENV B  ATK DEC END LEV each (A: operator A; B: B1 and B2 through the B LEV law), DELAY, TRIG, RESET
   PHRT          OFF ALL C A+B A+B2: the operators restarted at a note (OFF: they run on, even on a fresh voice)
   KEY A B1 B2   key scaling: less modulation the higher you play (neutral at C3 = MIDI 60)
   OFFSETS       C A B1 B2 -1.00..+1.00 added to the ratios
   then          DC blocker -> base-width filter -> multimode (OFF LP12 HP12 LP24) with its ADSR -> amp ADSR, LEVEL
   LFO 1..3      SPEED -64..63 x MULT (BPM-synced 1..2k, or the same at 120 BPM), FADE in / out, 40 destinations
```

The amp envelope is QUAD's own (`ownenv`, the Digitone's AMP page). Velocity scales the operator levels (VEL) and
the amplitude (half).

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

## Implementation (version 3: the Digitone review, 2026-10-08)

Files: `firmware/src/eng_quad.c` (the engine, `ENG_QUAD`, `QUAD_DEEP`), `firmware/src/quad_tables.h` (generated by
`tests/quad_ref.py`, committed), `tests/quad_ref.py` (the table generator and the reference model),
`tests/quad_goldens/*.json` (12 renders), `tests/cr_quad_test.c` (in `tests/run_cr_tests.sh`), `tests/run_quad_test.sh`.

**The algorithms** (`QUAD_ALGO`; the contract with the editor, the manual's Appendix A.3 diagram; "fb" the feedback
operator; *direct* = at full level whatever its envelope / LEV, *env* = x its envelope x level). A modulator's
output into its target = its wave x its envelope x its level (+ velocity, key scaling): the modulation index. An
operator that is both (B1 in 1 and 4, A in 5) has its modulation path scaled, its audio path not.

| algo | modulations | fb | X | Y |
| --- | --- | --- | --- | --- |
| 1 | A>C, B2>B1, B1>C | A | C | B1 (direct) |
| 2 | A>C, B2>B1 | B2 | C | B1 (direct) |
| 3 | A>C, A>B2, A>B1 | A | C + B2 (direct) | B1 (direct) |
| 4 | B2>B1, B1>A, A>C | B2 | C | B1 (direct) |
| 5 | B1>A, B2>A, A>C | B1 | C | A (direct) |
| 6 | A>C, A>B1, B2>C, B2>B1 | A | C | B1 (direct) |
| 7 | A>C, B2>B1 | A | C + A (env) | B1 (env) + B2 (env) |
| 8 | A>C | B1 | C + B2 (env) | B1 (env) |

C is always direct. Algorithm 8's B1 is enveloped (the manual draws its line to Y solid; corrected 2026-10-08):
its Y needs B LEV > 0 (43 = B1 full, B2 off). Per sample: four 32-bit phases; a modulator's output (Q15, at its level) is a
phase offset `o << 18` (full level = 2 cycles, an index of 12.6 rad); the loops are written out per algorithm and per
HARM case (none, - on C, + on A and B1). MIX: `gy = (MIX + 64) x 258`, `gx = 32766 - gy` (-64 = X alone, 63 = Y alone).

**Levels.** A: `(LEV A / 127)^2`. B: the **B LEV law** (manual §11.5.8; `quad_blev`, Q12) then squared per operator:
0..43 B1 0 -> 1 with B2 0; 43..85 B1 1 -> 0.1 while B2 0 -> 1; 85..127 B1 0.1 -> 1, B2 1 (at 0 / 43 / 64 / 85 / 127:
B1 0, 1, 0.55, 0.10, 1; B2 0, 0, 0.5, 1, 1). Then velocity (`32767 - VEL x (127 - velocity) x 2080 / 1024`), then
**key scaling** (A KEY, B1 KEY, B2 KEY 0..127, manual §9.5.1-3): x `2^(-(note - 60) / 12 x KEY / 127)` (KEY 127: half
the modulation an octave up, double an octave down, at most x 2 and full level; `quad_exp2`, a quadratic within 0.3 %),
then x the operator's envelope. Ramped per sample over the tick (three ramps: A, B1, B2).

**Ratios.** RATIO C and B1 / B2: 19 steps `0.25 0.50 0.75 1 2 3 .. 16` (index 0..18, 1.00 = 3). RATIO A: 64 steps
`0.25 .. 16.00` by 0.25. **RATIO B**: the patch keeps B1 (`QP_RB1`, value 3) and B2 (`QP_RB2`, value 71); the deep
column is one value 0..360 = `B1 x 19 + B2`: one detent steps B2 (the fast hand), past 16.00 B2 wraps to 0.25 and B1
steps (the manual's §11.3.4); 0 = 0.25/0.25 and 360 = 16/16 hold. The text is `B2/B1` (cr_edit.c's pair:
`names[v % 19]` first / on top, `names[v / 19]`); default 60 = 1.00/1.00. The macro `P_E3` is **B2's** step
(`QUAD_MAC`: macro k -> patch value; RATIO B's is `QP_RB2`). Offsets -1.00..+1.00 added to the ratio (clamped at 0).
**DTUN** (`QUAD_DT_UP` / `QUAD_DT_DN`): A up and B2 down by `6 x d / 64` cents up to 64, `6 + 44 x ((d - 64) / 63)^2`
above (16 cents at 96, 50 at 127); C and B1 untouched. Increments: `pitch inc x ratio (Q16) x detune (Q16)` in 64-bit.

**HARM.** 27 tables x 513 int16 (512 points and the wrap point; 27.1 KB `const`, XIP flash; was 15 x 1025, 30.8 KB):
0 the sine, 1..26 the manual's series, additive (sine phases, the fundamental at 1, each wave scaled to a peak of
32767). The recipes (harmonic: amplitude; `tests/quad_ref.py harm_recipes`):

| HARM | family | partials |
| --- | --- | --- |
| 1..7 | saw build-up | 1..n at 1/n, n = 2 3 4 6 8 11 16 |
| 8..13 | saw reduction | the 16-partial saw without partials 2..k+1, the rest x (1 - k/7), k = 1..6 |
| 14 | odd / even mix | odd 1..15 at 1/n, even at 0.35/n |
| 15..19 | square build-up | odd 1..n at 1/n, n = 3 5 7 11 17 |
| 20..23 | square reduction | the 17-partial square without 3..2k+1, the rest x (1 - k/5), k = 1..4 |
| 24..26 | bell | {1, 3 .5, 4 .35, 7 .25, 10 .15}, {1, 2 .3, 5 .5, 9 .35, 13 .2}, {1, 4 .6, 6 .45, 11 .35, 14 .25, 19 .15} |

HARM h (with the LFO's share, in 1/256 steps): tables |h| and |h| + 1 crossfaded (Q15), one index a sample;
negative h shapes C, positive A and B1 (modulators included), B2 never.

**FDBK** 0..120 on the algorithm's feedback operator: `(y[n-1] + y[n-2]) x fbq << 2`, `fbq = (f x 4237 + f^2 x 537
/ 64) / 32` (beta = 2 pi fbq / 16384 on the average: 1.9 rad at 35, measured h2 / h1 0.46, h3 / h1 0.29 = a saw;
7.5 rad at 120, noise). The feedback operator may be HARM-shaped (A, B1): its feedback then turns chaotic sooner.

**Filters** (the manual's order): the operators' sum -> the DC blocker (8 Hz, `QUAD_DC_K`) -> the base-width filter
(a one-pole high-pass at BASE, off at 0, then a one-pole low-pass at BASE + WIDTH, off at >= 127; 30 Hz x
533^(v/127)) -> the multimode: TYPE `OFF` (passes), `LP12`, `HP12` (dsp.c's trapezoidal SVF, its products rounded:
truncation's bias, integrated, was DC), `LP24` (a plain LP12 stage, damping 2, then the resonant one), the soft knee,
FREQ + the filter ADSR x DEPTH + key track; then the amp ADSR x LEVEL^2.

**Release INF** (the manual §11.7 / §11.8: REL 0–126, INF): the filter and amp REL keep 0..127 (int8 patch bytes) and
**127 = INF**; 126 is the longest timed release. In `quad_env_tick` a release at REL 127 holds its level (stage 5) and
decays again as soon as REL (modulated) leaves 127 (an LFO on "F REL" / "AMP R" reaching 127 holds it too: accepted).
A held voice sounds on after the note-off (`quad_done` 0) until the platform ends it: a steal for the budget or of the
part's own voice (voice.c, the oldest first: a held voice is an ordinary sounding one), an engine switch, or
`trk_all_off` (panic, MIDI all notes off, a sound load), which asks `engine_t.endless` (`quad_endless`: AMP REL 127 in
the patch, or a release already held) and fades such a voice in one block instead of releasing it. The text: the
format `F_TIMEI` (core.h, after `F_FMFRQ`: params.c prints F_TIME for 0..126, "INF" at 127; the editor protocol's fmt
17; QUAD's cells `QC_TI`), "INF" in every cell style (`cr_edit.c ce_ptext`); the AMP band draws the release flat to its
right edge (`wv[6]`). No factory preset uses 127 (none had it): the goldens do not change.

**Envelopes.** Operator A and B: DELAY, ATK (linear), DEC (exponential to END), held (attack-decay-end); TRIG / RESET
as before (unchanged by the review). **PHRT** (`QP_PHRT`, value 23) OFF ALL C A+B A+B2: at every note-on (a fresh voice
too) the set's phases go to 0, the others run on from where the voice left them (OFF: none reset).

**LFOs** (three, per voice, control rate). Rate `f = SPEED x MULT / 128 x BPM / 240 Hz` (`QUAD_LFO_K`, 64-bit; SPEED x
MULT = 128 is a bar: SPEED 32 x 4 = 0.5 Hz at 120 BPM): MULT 0..11 `1 2 4 .. 512 1k 2k` at the part's tempo
(`song.g[G_BPM]`, as the VA's LFO SYNC: the tempo, tap or MIDI clock), 12..23 `F1 F2 .. F2k` the same at 120 BPM.
SPEED -64..63 (negative runs backwards). FADE -64..63: negative fades in, positive fades out, over the time of
`2 |FADE| - 1` (`ENV_LIN`); 0 none. SPH on RAND is the slew: a one-pole on the random steps (127 ~ a period, 0 none;
the start phase is 0). TRIG FREE TRIG HOLD ONE HALF, waves TRI SINE SQR SAW RAMP EXP RAND, DEPTH -64..63 as before.
**DEST** (`N_QUAD_DEST`, 40, <= 5 characters): `NONE PITCH "P AB2" ALGO "RAT C" "RAT A" "RAT B" "OFS C" "OFS A" OFSB1
OFSB2 HARM DTUN FDBK MIX "A LEV" "B LEV" "A ATK" "A DEC" "A END" "B ATK" "B DEC" "B END" "A DLY" "B DLY" FREQ RESO
FENV BASE WIDTH "F ATK" "F DEC" "F SUS" "F REL" "AMP A" "AMP D" "AMP S" "AMP R" LEVEL PAN`. Full depth: PITCH / P AB2
+-1 octave (all operators / A and B2), ALGO +-7, RATIO C +-18 steps, A +-63, B +-180 pair steps, offsets +-1.00, HARM
+-26, DTUN / FDBK / MIX / levels / times / FREQ / RESO / BASE / WIDTH / LEVEL +-127 (FDBK 120), FENV +-127, PAN +-64.

**DRIVE and PAN are not in the patch**: AMP+'s PAN and DRIVE columns read and write the part's `P_PAN` and `P_DIST`.

**The patch and the blob** (`QUAD_BLOB` 80 bytes): `'Q'` (0x51), **version 3**, a byte per value (value - min), zeros
to 80 (73 values + 5 zero bytes). Values (index name min..max (init)): 0 ALGO 1..8 (1); 1 RATIO C 0..18 (3); 2 RATIO A
0..63 (3); 3 RATIO B1 0..18 (3); 4 HARM -26..26 (0); 5 DTUNE 0..127 (0); 6 FDBK 0..120 (0); 7 MIX -64..63 (0); 8..11
OFS C A B1 B2 -100..100 (0); 12..15 A ATK DEC END LEV (0 60 64 48); 16..19 B ATK DEC END LEV (0 60 0 0); 20 A DLY (0);
21 A TRIG 0..1 (1); 22 A RESET 0..1 (1); 23 PHRT 0..4 (1 ALL); 24 B DLY (0); 25 B TRIG (1); 26 B RESET (1); 27 VEL (64);
28 A KEY (0); 29 B1 KEY (0); 30..33 filter ATK DEC SUS REL (0 64 0 40; REL 127 = INF); 34 FREQ (127); 35 RESO (0); 36 TYPE 0..3 OFF
LP12 HP12 LP24 (1); 37 DEPTH -64..63 (0); 38 F DELAY (0); 39 F KTRK (0); 40 BASE (0); 41 WIDTH (127); 42..45 amp ATK
DEC SUS REL (0 64 127 40; REL 127 = INF); 46 LEVEL (100); 47..54, 55..62, 63..70 LFO 1..3: SPEED -64..63 (16), MULT 0..23 (3), FADE
-64..63 (0), DEST 0..39 (0), WAVE 0..6 (0), PHASE (0), TRIG 0..4 (0), DEPTH -64..63 (0); 71 RATIO B2 0..18 (3); 72 B2
KEY (0). (Unlisted ranges 0..127.) Macros P_E0..P_E7 = values 0 1 2 **71** 4 5 6 7.
**Versions 1 and 2 are read** (`quad_blob_ok` with their ranges, `quad_range_v2`; `quad_unpack` converts): version 1's
RATIO B (BR x 19 + B1) as B1, the nearest B2 and the rest in OFS B2 (as before), then version 2 -> 3
(`quad_v2_to_v3`): FDBK to the same beta (64 -> 29, 127 -> 101); TYPE LP -> LP12, HP -> HP12, BP -> LP12; HARM
(+-26 = +-7 positions of the old odd / all tables, on the carriers) -> - the saw build-up 1..7, + the square build-up
15..19 (both on C: v3's + side shapes A and B1); PHRT on -> ALL, off -> OFF; B2 KEY = B KEY (note: v2's key track
raised the modulation up the keyboard, v3's KEY lowers it); LFO SPEED 64 -> 63, FADE 0..127 (a fade-in time) -> -1..-64
(the same time), MULT index kept (the BPM set: f is now 1.25 x v2's at 120 BPM), DEST by name. ALGO keeps its number
(the routings changed: a v2 patch does not sound as it did). A version-1 RATIO B macro in a project (`P_E3` 19..113)
is still taken by `quad_track_loaded`. Blobs are written as version 3; the QUAD store's own header stays version 1.

**The pages as implemented** (`eng_page_t`, 4 columns each; `section = {0, 3, 7, 14, 0xFF}`: OSC, FILTER, ENV, LFO,
**no MOD**):

| # | title | columns | | # | title | columns |
| --- | --- | --- | --- | --- | --- | --- |
| 0 | SYN 1 | ALGO, RATIO C, RATIO A, RATIO B | | 10 | ENV 2+ | B DLY, B TRIG, B RESET, VEL |
| 1 | SYN 1+ | HARM, DTUNE, FDBK, MIX | | 11 | ENV 3 | A KEY, B1 KEY, B2 KEY, - |
| 2 | SYN 2 | OFS C, OFS A, OFS B1, OFS B2 | | 12 | AMP | ATK, DEC, SUS, REL |
| 3 | FILTER | ATK, DEC, SUS, REL | | 13 | AMP+ | LEVEL, PAN (P_PAN), DRIVE (P_DIST), - |
| 4 | FILTER+ | FREQ, RESO, TYPE, DEPTH | | 14 | LFO 1 | SPEED, MULT, FADE, DEST |
| 5 | FILT 2 | DELAY, KTRK, -, - | | 15 | LFO 1+ | WAVE, PHASE (RAND: slew), TRIG, DEPTH |
| 6 | FILT 2+ | BASE, WIDTH, -, - | | 16, 17 | LFO 2, LFO 2+ | as LFO 1 |
| 7 | ENV A | A ATK, A DEC, A END, A LEV | | 18, 19 | LFO 3, LFO 3+ | as LFO 1 |
| 8 | ENV B | B ATK, B DEC, B END, B LEV | | | | |
| 9 | ENV 2 | A DLY, A TRIG, A RESET, PHRT | | | | |

Formats: ALGO `F_INT`; RATIO C / A / B and the offsets `F_INT` with a name list (`N_QUAD_RCB`, RATIO B a pair over
361 values); HARM, MIX, SPEED, FADE, the depths, PAN `F_OFS`; MULT, TYPE, PHRT, WAVE, TRIG, DEST `F_ENUM`; TRIG / RESET
`F_ONOFF`; times `F_TIME` (the filter and amp REL `F_TIMEI`: 127 = INF); FREQ `F_CUTOFF`; RESO, SUS, KEY, KTRK, VEL, DRIVE `F_PCT`; FDBK, DTUNE, BASE, WIDTH, END, LEV,
LEVEL, PHASE `F_INT`. The editor's screens: ENV 3 is one row of three cells "A Key", "B1 Key", "B2 Key".

**Presets** (retuned 2026-10-08 for the new routings and laws; a 6-note chord at LEVEL 92 peaks at 30-71 % FS; measured
against the version-2 sounds by RMS, zero-crossing rate and the energy above ~2 kHz on a 3-note chord, 1 s):

| preset | algo | the voice |
| --- | --- | --- |
| EP | 2 | X C under A 1:1 (body), Y B1 1.00 direct under B2 14.00 (the tine, short), MIX -30, KEY 40/30/30 |
| BELL | 2 | X C under A 3.50, Y B1 2.00 under B2 6.00, MIX -10 (was algorithm 4: the same two pairs) |
| BASS (MONO) | 1 | C under A 1:1 with FDBK 24, X only, LP12 |
| PLUCK | 5 | B1 3.00 and B2 3.00 into A 2.00 into C, X only |
| BRASS | 1 | C under a slow 1:1 A with FDBK 28, a little B1, X only, LP12 |
| GLASS PAD | 7 | X C under A 2.00 + A, Y B1 under B2 + B2, HARM +8 (A, B1), PHRT OFF |
| HOLLOW | 2 | C a soft square (HARM -16) under A 2.00, Y B1 under a little B2 |
| SQUARE LEAD (MONO) | 1 | C a square (HARM -19) under a little A 2.00, X only |
| METAL | 5 | B1 7.00 (FDBK 20) and B2 10.50 into A 1.50 into C, DTUN 90, MIX -40 |
| WOBBLE (MONO) | 1 | BASS through LP24, LFOs on A LEV and FREQ |
| CLAV | 2 | X C (HARM -15) under A 3.00, Y B1 1.00 under B2 4.00, HP12 |
| STRINGS | 8 | X C a saw (HARM -7) + B2 detuned (DTUN 100), Y B1 with FDBK 35 (a second saw), LP12, an LFO on DTUN |
| MARIMBA | 1 | C under A 4.00 and B1 10.00 (short), X only, KEY 50/50/0 |
| DRONE | 6 | C 0.50 and B1 under A (FDBK 15) and B2 1.50, three slow LFOs, PHRT OFF |
| FEEDBACK | 8 | Y B1 (enveloped, B LEV 43) at FDBK 42, X C under A, MIX +30 |
| NOISE-ISH | 7 | A at FDBK 120 into C, X = C + A, HP12 70 reso 40 (was BP) |

Approximations (the user judges by ear): **GLASS PAD** (algorithm 7 now puts A into C: brighter than v2's, about 2.7 x
the zero crossings), **DRONE** (algorithm 6 now sends A and B2 into C as well: brighter), **NOISE-ISH** (BP is gone:
HP12 instead), **FEEDBACK** and **STRINGS** (v2's routings have no exact counterpart: Y is now an enveloped feedback
operator / a second saw), **BASS** (C under one fed-back A instead of v2's B chain; a little darker). EP, BELL, PLUCK,
BRASS, HOLLOW, SQUARE LEAD, METAL, WOBBLE, CLAV, MARIMBA keep v2's level and brightness within ~30 %.

**Tests** (`cr_quad_test`, 21 166 checks, 0 failed, built with `-fsanitize=signed-integer-overflow`): the blob round
trip (2000 random patches), bad blobs -> init; version 1 blobs (RATIO B and every other value converted) and macros;
**version 2**: 500 random patches converted into range, each conversion (TYPE, HARM, PHRT, KEY, FDBK, SPEED, FADE, MULT,
DEST), v2's ranges and padding; the pages against the ranges, the contract's enums and ranges, ENV 3's three cells;
RATIO B's walk 0..360 (B2 first, the carry into B1, the ends, the texts "B2/B1", the macro = B2); RATIO B heard (BELL,
algorithm 2, Y: B1 1.00 -> 2.00 an octave up, live within a note, B2 the timbre; B LEV 0 leaves the direct B1 as loud);
the routing table against the contract (modulators, fb, X, Y, enveloped); **the routings heard**: every algorithm, X
and Y, a ratio offset on each operator changes the output exactly when the operator reaches it; sidebands (A 1:1 -> 2:1
removes C's 2nd harmonic); direct carriers ignore LEV and envelope (algorithms 2, 5), enveloped ones follow (7, 8: B1 by ENV B and the B LEV law); the
B LEV law at 0 / 21 / 43 / 64 / 85 / 106 / 127 and its shape, in a voice; HARM per operator (- C only, + A and B1 only,
A as a modulator, B2 never; the 26 waves distinct, the fundamental kept, the halfway crossfade); DTUN (A up 50 cents at
127, B2 down, C and B1 the same); FDBK on each algorithm's operator, FDBK 35 a saw (h2 / h1 0.46, h3 / h1 0.29); key
scaling (A KEY 127 a quarter two octaves up, B1 KEY 64 half, B2 KEY 0 flat, neutral at C3); LFO rates at 120 / 90
/ 200 BPM and the fixed set within 2 % (SPEED 32 x 4 = one bar = 0.5 Hz at 120), backwards, SPEED 0, the start phase,
ONE / HALF, the waves; FADE in / out; PHRT OFF / ALL / C / A+B / A+B2 on a fresh voice; against the model (each
algorithm with a filter type of its own, EP / BASS / GLASS PAD / WOBBLE: SNR 45-73 dB, block RMS within 0.11 dB); the
envelopes; LP12 / HP12 / LP24 / OFF and the base-width window (before the multimode); 192 extreme patches (all 40
destinations, FADE, RAND slew, every filter type): no int32 wrap, the voices end; every preset's chord < 0.9 FS; no DC.
(The model's FDBK in the algorithm goldens is 14: a HARM-shaped feedback operator is chaotic enough above ~25 that the
float model and the integer engine part ways.)

**CPU** (`tests/regress.c`, host instructions a sample, 8 voices): the 16 presets average 1491 (v2: 1510); the heaviest
WOBBLE 1880 (LP24; v2's WOBBLE 1684), STRINGS 1630 (v2 1891). HARM's tables are 27.1 KB in XIP flash (v2: 30.8 KB);
HARM + reads two tables for A and B1 (two operators a sample instead of v2's carriers).
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

**2026-10-08, the Digitone review** (blob version 3; "Implementation" above): the routings, direct / enveloped
carriers, the B LEV law, HARM as the 26-wave series on C (-) or A and B1 (+), DTUN on A and B2, FDBK 0..120 (35 = saw),
key scaling A / B1 / B2 (B2 KEY new), MIX -64..63, RATIO B with B2 as the fast hand (macro = B2, text B2/B1), the
filter OFF / LP12 / HP12 / LP24 after the base-width filter, the LFOs (tempo-synced MULT, fade in / out, RAND slew,
40 destinations), PHRT as an enum; versions 1 and 2 read; the 16 presets retuned. Release INF: done (2026-10-08, the
filter and amp REL at 127; "Filters" above).
regress: the 16 FM TONE goldens and CPU entries change (to be re-recorded), 0 health failures.

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
