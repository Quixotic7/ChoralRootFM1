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
   LFO 1..3      SPEED -64..63 x MULT (BPM-synced 1..2k, or the same at 120 BPM), FADE in / out, 58 destinations (the Digitone's 57 + none)
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

**HARM.** 27 tables x 513 int16 (512 points and the wrap point): in flash as 27 x 257 (the first half of each cycle and
its end point, 13.5 KB `const`; was 27 x 513, 27.1 KB, and 15 x 1025, 30.8 KB before that): every wave is a sum of
sines, so f(512 - i) = -f(i) exactly (`tests/quad_ref.py` asserts it point by point; HISTORY 3y126). The render reads
the halves in flash with a mirror (`eng_quad.c quad_hw`, HISTORY 3y150): in the second half of the cycle the two points
around the phase are the first half's pair at 511 - i, reversed and negated, so the slope is the same and only the
start changes sign (branchless, bit for bit the whole cycle's values: `cr_quad_test` checks every point at ten
fractions and six crossfades). From 3y126 to 3y150 the whole cycles were built at power-on into POOL (`quad_init`,
`quad_harm`, 27.7 KB) and read there; the owner chose the mirror's CPU over the POOL ("I'd like to keep things open
for potentially more engines or FX"). The waves:
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
17; QUAD's cells `QC_TI`; since 2026-10-14 `F_INTI`, a number: "Times"), "INF" in every cell style (`cr_edit.c ce_ptext`); the AMP band draws the release flat to its
right edge (`wv[6]`). No factory preset uses 127 (none had it): the goldens do not change.

**Envelopes.** Operator A and B: DELAY, ATK (linear), DEC (exponential to END), held (attack-decay-end; the times the
Digitone's since 2026-10-14: "Times"); TRIG / RESET
as before (unchanged by the review). **PHRT** (`QP_PHRT`, value 23) OFF ALL C A+B A+B2: at every note-on (a fresh voice
too) the set's phases go to 0, the others run on from where the voice left them (OFF: none reset). Since 2026-10-16
("Pops" below) a voice that still sounds when it is struck again restarts only the operators of the set that no one
hears (`quad_phrt_mask`): a carrier while the voice is under -40 dB (an enveloped carrier at its envelope x level), a
modulator while its level is under -66 dB; the others run on.

**LFOs** (three, per voice, control rate). Rate `f = SPEED x MULT / 128 x BPM / 240 Hz` (`QUAD_LFO_K`, 64-bit; SPEED x
MULT = 128 is a bar: SPEED 32 x 4 = 0.5 Hz at 120 BPM): MULT 0..11 `1 2 4 .. 512 1k 2k` at the part's tempo
(`song.g[G_BPM]`, as the VA's LFO SYNC: the tempo, tap or MIDI clock), 12..23 `F1 F2 .. F2k` the same at 120 BPM.
SPEED -64..63 (negative runs backwards). FADE -64..63: negative fades in, positive fades out, a linear ramp over the
Digitone's fade time of |FADE| ("Times": `QUAD_T_FADE`, 0.047 s at 16 .. 199 s at 64; the fade-out assumed the same);
0 none. SPH on RAND is the slew: a one-pole on the random steps (127 ~ a period, 0 none;
the start phase is 0). TRIG FREE TRIG HOLD ONE HALF, waves TRI SINE SQR SAW RAMP EXP RAND, DEPTH -64..63 as before.
**DEST** (`N_QUAD_DEST`, 58, <= 5 characters): every destination of the Digitone's LFOs (2026-10-14, the owner:
"Update FM Tone's LFO destinations to match."). The **stored numbers** (`QD_*`, the blob's DEST byte): 0..39 as
before (`NONE PITCH "P AB2" ALGO "RAT C" "RAT A" "RAT B" "OFS C" "OFS A" OFSB1 OFSB2 HARM DTUN FDBK MIX "A LEV" "B LEV"
"A ATK" "A DEC" "A END" "B ATK" "B DEC" "B END" "A DLY" "B DLY" FREQ RESO FENV BASE WIDTH "F ATK" "F DEC" "F SUS"
"F REL" "AMP A" "AMP D" "AMP S" "AMP R" LEVEL PAN`), then **appended** 40..57: `L1SPD L1MUL L1FAD L1WAV L1SPH L1MOD
L1DEP "R ALL" ABLEV ABATK ABDEC ABEND ABDLY DRIVE REV DEL CHO "F DLY"`. The **editor's order** (`QUAD_DEST_ORDER`;
`cr_pages.c cp_dstep` steps a detent along it) is the Digitone's (its destination numbers ascending): NONE, LFO 1's
SPEED MULT FADE WAVE SPH MODE DEPTH, ALGO, RATIO C A B, HARM DTUN FDBK MIX, A ATK DEC END LEV, B ATK DEC END LEV, A DLY,
B DLY, PITCH, P AB2, RATIO ALL, the AB macros LEV ATK DEC END DLY, FREQ RESO FENV, F ATK DEC SUS REL, BASE WIDTH, AMP
A D S R, DRIVE, PAN, LEVEL (the Digitone's VOLUME), the REV DEL CHO sends, OFS C A B1 B2, F DLY (the filter envelope's
ENV DELAY). The numbers were kept (not renumbered) so every saved patch keeps its meaning with no blob version change:
a version-3 blob stays version 3, its DEST range 0..57; only the display order is new.
Full depth: PITCH / P AB2 +-1 octave (all operators / A and B2), ALGO +-7, RATIO C +-18 steps, A +-63, B +-180 pair
steps, offsets +-1.00, HARM +-26, DTUN / FDBK / MIX / levels / times / FREQ / RESO / BASE / WIDTH / LEVEL +-127 (FDBK
120), FENV +-127, PAN +-64. The appended ones: **LFO 1's own parameters** (an LFO 2 or 3 destination; LFO 1 on its own
parameter does nothing: LFO 2 and 3 are read first, then LFO 1 with the moved values): SPEED +-127 (clamped -64..63; on
a FREE LFO 1 the voice keeps its phase's lead over the part's, `lof`), MULT +-11 steps within its set (BPM or fixed),
FADE +-127, WAVE +-6 steps, SPH +-127 (the phase read moves; on RAND the slew), MODE +-4 steps (read each tick, so a
moving MODE switches FREE / TRIG / HOLD / ONE / HALF as it goes), DEPTH +-127; the stepped three (MULT WAVE MODE) are
implemented as steps, as the Digitone allows them. **RATIO ALL**: C +-18, A +-63, B1 and B2 +-18 steps each (with the
own RATIO destinations). **AB LEV / ATK / DEC / END / DLY**: A's and B's field together (the sum with A's and B's own
destination: exactly two LFOs on the two). **F DLY**: the filter envelope's DELAY (as A DLY: a stored 0 starts without
a delay stage). **DRIVE, REV, DEL, CHO**: the part's `P_DIST`, `P_REV`, `P_DLY`, `P_CHOR` +-127 (clamped 0..127), from the
latest note's voice as PAN (`quad_fx_off`; `fx.c track_dist` / `mix_part_out` read them through `quad_drive` /
`quad_sends` once a block; the sends then ramp over the block as any send change). Cost when unused: nothing on the
per-sample paths; per voice and block the 18 more `dst[]` zeroed, one OR of seven terms (LFO 1's), the AB / RATIO ALL
sums (an add each), and four offsets at the latest voice.

**DRIVE and PAN are not in the patch**: AMP+'s PAN and DRIVE columns read and write the part's `P_PAN` and `P_DIST`.
The DRIVE is the sound's all the same (2026-10-12, the owner: "Digitone presets should save and load the drive level
with them and also always enable drive if this is greater than 0 on FM TONE and FX is off"): a factory preset's is the
drive of its `FX()` row in `QUAD_PRESETS` (10 on WOBBLE, 0 on the other fifteen: our own values, there is no Digitone
conversion in the table), a user sound's is its record's `P_DIST` field (`upreset.c`, where a save always wrote the
part's drive), and ChoralRoot loads it, saves it and does not close it with FX off (`cr_ui.c cu_drive_load`,
[FX.md](FX.md) "FM TONE's Drive"). The 80-byte blob and the store are unchanged.

**The patch and the blob** (`QUAD_BLOB` 80 bytes): `'Q'` (0x51), **version 3**, a byte per value (value - min), zeros
to 80 (73 values + 5 zero bytes). Values (index name min..max (init)): 0 ALGO 1..8 (1); 1 RATIO C 0..18 (3); 2 RATIO A
0..63 (3); 3 RATIO B1 0..18 (3); 4 HARM -26..26 (0); 5 DTUNE 0..127 (0); 6 FDBK 0..120 (0); 7 MIX -64..63 (0); 8..11
OFS C A B1 B2 -100..100 (0); 12..15 A ATK DEC END LEV (0 32 127 0; until 2026-10-14 0 60 64 48); 16..19 B ATK DEC END LEV (0 60 0 0); 20 A DLY (0);
21 A TRIG 0..1 (1); 22 A RESET 0..1 (1); 23 PHRT 0..4 (1 ALL); 24 B DLY (0); 25 B TRIG (1); 26 B RESET (1); 27 VEL (64);
28 A KEY (0); 29 B1 KEY (0); 30..33 filter ATK DEC SUS REL (0 64 0 40; REL 127 = INF); 34 FREQ (127); 35 RESO (0); 36 TYPE 0..3 OFF
LP12 HP12 LP24 (1); 37 DEPTH -64..63 (0); 38 F DELAY (0); 39 F KTRK (0); 40 BASE (0); 41 WIDTH (127); 42..45 amp ATK
DEC SUS REL (0 64 127 40; REL 127 = INF); 46 LEVEL (100); 47..54, 55..62, 63..70 LFO 1..3: SPEED -64..63 (16), MULT 0..23 (3), FADE
-64..63 (0), DEST 0..57 (0; 40..57 since 2026-10-14), WAVE 0..6 (0), PHASE (0), TRIG 0..4 (0), DEPTH -64..63 (0); 71 RATIO B2 0..18 (3); 72 B2
KEY (0). (Unlisted ranges 0..127.) Macros P_E0..P_E7 = values 0 1 2 **71** 4 5 6 7.
**Versions 1 and 2 are read** (`quad_blob_ok` with their ranges, `quad_range_v2`; `quad_unpack` converts): version 1's
RATIO B (BR x 19 + B1) as B1, the nearest B2 and the rest in OFS B2 (as before), then version 2 -> 3
(`quad_v2_to_v3`): FDBK to the same beta (64 -> 29, 127 -> 101); TYPE LP -> LP12, HP -> HP12, BP -> LP12; HARM
(+-26 = +-7 positions of the old odd / all tables, on the carriers) -> - the saw build-up 1..7, + the square build-up
15..19 (both on C: v3's + side shapes A and B1); PHRT on -> ALL, off -> OFF; B2 KEY = B KEY (note: v2's key track
raised the modulation up the keyboard, v3's KEY lowers it); LFO SPEED 64 -> 63, FADE 0..127 (a fade-in time) -> -1..-64
(the same time on the old curve; "Times"), MULT index kept (the BPM set: f is now 1.25 x v2's at 120 BPM), DEST by name. ALGO keeps its number
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

Formats (since 2026-10-14, "Ranges" below): ALGO `F_INT`; RATIO C / A / B and the offsets `F_INT` with a name list
(`N_QUAD_RCB`, RATIO B a pair over 361 values); the hi-res values (HARM, DTUNE, FREQ, RESO, the filter and LFO DEPTHs,
BASE, AMP REL, LEVEL, SPEED) `F_FIX2`; MIX, FADE, PAN `F_OFS`; MULT, TYPE, PHRT, WAVE, TRIG, DEST `F_ENUM`; TRIG /
RESET `F_ONOFF`; times `F_INT` since 2026-10-14 (the filter REL `F_INTI`: 127 = INF; numbers 0..127 as the
Digitone shows them, "Times"; `F_TIME` / `F_TIMEI` before); AMP SUS, DRIVE `F_PCT`; FDBK, WIDTH, END, LEV,
PHASE, VEL, the KEYs, filter SUS, KTRK `F_INT` (plain 0..127 numbers, as the Digitone shows them). The editor's
screens: ENV 3 is one row of three cells "A Key", "B1 Key", "B2 Key".

### Ranges (version 5: the Digitone's, 2026-10-14, with the owner's corrections)

The owner (2026-10-14): "FM-Tone on FM-1 has many parameters in different ranges than the real Digitone which uses 0
to 127 in a bunch of places or -64 to 63.99 for centered parameters like pan. Also a number of parameters like cutoff
support decimal places to two positions like 23.04. Would be good if the FM-Tone's engine parameter ranges and values
matched and also make it easier to compare parameters between both devices." So FM TONE shows and stores the
Digitone's numbers. The corrections the same day (version 5): "Cutoff should not go from 0 to 127.99, should go to 0
to 127. Amp release should be integer 0-126, then INF, same with Amp Decay. Level is also integers from 0 to 127. No
parameters should be a max of 127.99." Twelve values are hi-res (`eng_quad.c quad_hires`): stored in hundredths, shown with two
decimals by the format `F_FIX2` (core.h, the editor protocol's fmt 18; params.c: 4201 = "42.01", a range below 0
signed: "+41.34", "-64.00"; at most 6 characters). Their finest step is 1 (0.01), their whole-number step 100: the
knob code steps an `F_FIX2` value that way (the knob-acceleration branch; before it, the editor's 5 % steps).

| Value | Page | Range (shown) | Stored | Default | Was (version 3) |
| --- | --- | --- | --- | --- | --- |
| HARM | SYN 1+ | -26.00 .. +26.00 | -2600 .. 2600 | 0.00 | -26 .. 26 |
| DTUNE | SYN 1+ | 0.00 .. 127.00 | 0 .. 12700 | 0.00 | 0 .. 127 |
| FDBK | SYN 1+ | 0 .. 127 | 0 .. 127 | 0 | 0 .. 120 |
| FREQ | FILTER+ | 0.00 .. 127.00 (the Digitone's number; no Hz) | 0 .. 12700 | 127.00 | 0 .. 127 (Hz) |
| RESO | FILTER+ | 0.00 .. 127.00 | 0 .. 12700 | 0.00 | 0 .. 127 (%) |
| DEPTH (filter env) | FILTER+ | -64.00 .. +63.99 | -6400 .. 6399 | 0.00 | -64 .. 63 |
| BASE | FILT 2+ | 0.00 .. 127.00 | 0 .. 12700 | 0.00 | 0 .. 127 |
| AMP DEC | AMP | 0 .. 126, INF (127: no decay, the level held) | 0 .. 127 | 64 | 0 .. 127 (a time) |
| AMP REL | AMP | 0 .. 126, INF (127) (version 4: 0.00 .. 127.99) | 0 .. 127 | 40 | 0 .. 126, 127 INF (a time) |
| LEVEL | AMP+ | 0 .. 127 (version 4: 0.00 .. 127.99) | 0 .. 127 | 110 | 0 .. 127, 100 |
| SPEED (LFO 1..3) | LFO n | -64.00 .. +63.99 | -6400 .. 6399 | +16.00 | -64 .. 63 |
| DEPTH (LFO 1..3) | LFO n+ | -64.00 .. +63.99 | -6400 .. 6399 | 0.00 | -64 .. 63 |
| VEL, A / B1 / B2 KEY, filter SUS, KTRK | | 0 .. 127 (a number, not %) | as before | | 0 .. 127 (%) |

Unchanged: the ratios and their offsets (+-1.00), the waves, modes and enums, MIX -64..63, PAN and DRIVE (the part's
parameters), FM TONE's own values (LFO 3, the key / velocity tracking, the filter envelope's delay and key track),
WIDTH, the envelope times other than AMP DEC / REL. AMP DEC and REL are the format `F_INTI` (core.h, the editor
protocol's fmt 19: a number, its max "INF"); the knob steps them 1 slow, 6 quick, OPT snaps on the 16 grid (0 16 ..
112 127: the top is INF). **AMP DEC INF** (the Digitone's DEC 127): the amp envelope holds the level it reached at
the attack's end (1) and never moves to SUS; the gate's end releases by REL as always (`quad_env_tick` stage 3; the
filter envelope's DEC has no INF, its REL INF is unchanged). LEVEL's law: amplitude x 2 LEVEL^2 (Q15, from the integer).
The voice goes into the part at the engine's output scale, `>> 12` (6 dB under the `>> 11` before 2026-10-16: "Level"
below).

Storage: blob version 5 (96 bytes; HARM DTUN FREQ RESO filter DEPTH BASE and the LFOs' SPEED / DEPTH u16le, the rest a
byte: 87 used). Version 4 blobs (3y61's: AMP REL and LEVEL u16 hundredths, the tops 127.99; the owner's unit has them)
and versions 1..3 convert on load (`quad_unpack`, `quad_v5_of_v4`): FREQ DTUN RESO BASE clamped to 127.00; AMP REL
127.00 and up -> 127 (INF), below it rounded to the nearest whole number but at most 126 (126.85 -> 126); LEVEL
rounded (110.40 -> 110). Factory presets, pool slots, sessions, backups, sound files (web/fm1sounds.js, the importer
web/fm1dnsnd.js `quadToV4` = `quadToV5`) and tools/fm1_install.py (versions 1..5 accepted) all go through it.

The macros P_E4 HARM and P_E5 DTUNE hold the coarse part (`quad_mac_val`: a record's macro bytes, the editor
protocol's v14, the matrix, motion and a CC are 7-bit; a macro move sets the coarse value with fine 0,
`quad_mac_patch`). ChoralRoot has no HOME or main-view knob on the engine's macros (`engine_t.knob` is Felucca's HOME
row, ui.c, not in ChoralRoot's build): a player turns HARM and DTUNE on the sound editor's SYN 1+ cells, the deep
values, two decimals (0.01 slow, the range / 20 quick, OPT whole numbers). The two decimals survive a save and a load
because the patch blob is the authority (a user slot, a pool slot, the session: the blob's hundredths are loaded, the
macro is derived from them, `quad_macros_out`); only a CC, the matrix, motion or the web editor's macro write moves
them in whole numbers (fine 0).

The engine uses every hundredth: FREQ as Q8 (x 256 / 100) into the multimode filter, whose coefficients
(`quad_svf_coef`) interpolate g in Q20 and carry a2 to 1/32 of its Q13 step (FREQ 42.00 and 42.01 render differently;
dsp.c's `tsvf_coef_k` could not resolve 0.05 % at low cutoffs); RESO (k = 8192 - RESO x 3 / 5), the envelope DEPTH
(eF x DEPTH / 6400), BASE (`quad_bwk`: QUAD_BW_K interpolated), HARM (Q8 = x 256 / 100), DTUNE (QUAD_DT_UP / DN
interpolated), the LFO rate (SPEED x MULT x BPM x QUAD_LFO_K / 6553600, exact in 64 bits), the LFO depth (/ 6400),
AMP REL (version 4: ENV_EXP interpolated between its steps; 127.00 and up INF; version 5: a whole number on the
Digitone's tables, "Times"), LEVEL (squared: LEVEL^2 / 5000 = 2 x (LEVEL /
100)^2 as before). FDBK 0..127 runs the 0..120 feedback curve ((FDBK x 120 + 63) / 127). Modulation scales for the
hi-res destinations are x 100. The added cost on the device (the listing, 0.22 build): `quad_render` 5234 -> 5413
instructions, the multimode filter's sample loop +2 multiplies (LP12 61 -> 69 instructions a sample), a few
divisions a block; the host figure (`tests/run_quad_test.sh`) average 18.7 % -> 19.6 %, the worst case 30.4 % ->
31.1 %.

**Storage.** `quad_patch` is int16 (`QP_NP` = 73 values a part). Blob version 4 (`QUAD_BLOB` 96 bytes): 'Q', 4, each
value as value - min, the hi-res ones u16 little-endian, the rest a byte (89 bytes), zeros. Versions 1..3 (80 bytes,
`QUAD_BLOB_V3`: the factory presets' tables, the legacy QUAD store, a pool slot or a session of before, a backup's
object 22) are converted on load, best effort (`quad_unpack`: the hi-res values x 100, FDBK x 127 / 120 rounded; 1 and
2 through version 3 first). The pool slot of an older FM TONE sound keeps its 80 bytes until saved again
(`up_patch_get` reads it zero-padded); a session record of 80 bytes is taken (`cr_settings.c`). The 16 factory presets
are their version-3 tables converted (`quad_preset_patch`; to be replaced). An older firmware (version 3) refuses a
version-4 blob in `quad_blob_ok` (its version test): the sound loads as the init patch, the slot's bytes untouched.

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
WOBBLE 1880 (LP24; v2's WOBBLE 1684), STRINGS 1630 (v2 1891). HARM's tables are 13.5 KB in XIP flash since 3y126 (27.1 before, v2: 30.8 KB), no POOL
since 3y150; HARM + reads two tables for A and B1 (two operators a sample instead of v2's carriers). The mirror read
(3y150) costs the presets that use HARM, measured the same day before and after: GLASS PAD (HARM +8) 1593 -> 1742
(+9.4 %), DRONE (an LFO across HARM 0) 1575 -> 1718 (+9.1 %), HOLLOW +3.9 %, CLAV +2.9 %, STRINGS +2.0 %, SQUARE LEAD
+0.5 % (HARM - reads one table, for C); the presets without HARM -4 .. 0 % (their loops' source is the same). On the
unit's listing the per-sample loop of a HARM + algorithm is 133 .. 155 instructions (was 112 .. 130), HARM - 109 .. 129
(97 .. 115), no HARM 81 .. 103 (83 .. 98; the eight together 748 for 749): the compiler spills differently across the
one function, so algorithm 5's no-HARM loop (METAL) grew 96 -> 103 (six more stack accesses) while the others moved
-3 .. +2.
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

### Times (the Digitone's, measured, 2026-10-14)

The owner (2026-10-14): "make all the FM-Tone times match the Digitone, a wrong time will have a big effect on the
sound". Until then the four envelopes ran on Felucca's two curves (`ENV_LIN` attacks, `ENV_EXP` decays, 1 ms .. 10 s:
`tools/gen_tables.py`), the DELAYs on `ENV_LIN`, the LFO FADE over `ENV_LIN[2 |FADE| - 1]`. The Digitone's were
measured on the owner's unit (OS 1.44, two runs recorded losslessly over USB at 48 kHz: `tools/digitone/`, its
README): each envelope type has its own curve, and FM TONE now has a table per type, and the Digitone's shapes where
the measurement shows them (the operator attack).

**Where the numbers are.** `tools/digitone/digitone_times.json`: the Digitone's readings (derived numbers only, no
audio): per envelope the measure, the setup (the patch, in Digitone values), per value the seconds, each run's own
numbers and, for the filter and the operators, the centroid track itself (log2 Hz in time, thinned), extrapolated
or not. `tools/digitone/quad_calib.json`: our measurement of FM TONE through the same analysis (the calibration, the
verification of every measured value). `tools/digitone/make_quad_times.py` makes `firmware/src/quad_times.h` of the
two (regenerating after another measurement: the README). tests/run_cr_tests.sh runs `tools/digitone/test_quad_times.py`
(the header not stale; the amp's times, the operator attack's t10 / t50 / t90, the filter's and the operator decay's
readings, the delay, the fade).

**The tables** (`quad_times.h`, in flash: Q24 a control tick of 32 samples, 0.7256 ms; one look-up a tick and envelope,
as before): an attack's step 2^24 x tick / T (T the full rise; at most 2^24: one tick); the operators' attack is
**linear in dB** from `QUAD_OP_FLOOR` (-38 dB) to the top, its entry the growth a tick, 2^24 x ((1 / floor)^(tick / T)
- 1) (`x += x x k >> 24`, from at least the floor: a restart without RESET goes on from where it is); a decay's share
of the way 2^24 x (1 - 10^(-3 tick / T60)) (`x += (sus - x) x k >> 24`, 64-bit; a release's step rounded up, so the
long ones reach the end at 2^12). The times (engine seconds):

| value | 0 | 1 | 8 | 16 | 32 | 48 | 64 | 80 | 96 | 112 | 120 | 126 | 127 |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| AMP ATK (full rise) | 3.17ms | 3.18ms | 3.24ms | 9.11ms | 47.8ms | 128ms | 261ms | 451ms | 708ms | 1.49s | 8.93s | 26.8s | 29.8s |
| AMP DEC / REL (T60) | 5.45ms | 6.15ms | 39.5ms | 194ms | 1.02s | 2.74s | 5.54s | 9.61s | 15.1s | 22.1s | 26.2s | 29.7s | INF |
| filter ATK (full rise) | 0.726ms | 1.08ms | 26.1ms | 350ms | 1.36s | 4.06s | 8.36s | 15.4s | 22.9s | 48.3s | 290s | 870s | 936s |
| filter DEC / REL (T60) | 53.5ms | 56.7ms | 84.7ms | 137ms | 408ms | 752ms | 1.78s | 4.21s | 8.02s | 15.4s | 22.4s | 30.1s | 31.6s (REL: INF) |
| operator ATK (-38 dB to the top) | 0.726ms | 1.01ms | 13.7ms | 131ms | 674ms | 1.81s | 3.66s | 6.35s | 9.95s | 14.5s | 17.2s | 19.5s | 20s |
| operator DEC (T60) | 9.21ms | 10.8ms | 34.8ms | 114ms | 554ms | 1.54s | 3.12s | 5.34s | 8.48s | 12.5s | 14.8s | 16.8s | 17.2s |

`QUAD_T_AATK` the amp attack (linear), `QUAD_T_ADEC` the amp decay and release (one exponential curve: the Digitone's
are equal within 0.3 % and dB-linear, 0.01..0.1 dB rms; DEC 127 and REL 127 stay INF), `QUAD_T_FATK` the filter attack
(linear), `QUAD_T_FDEC` the filter decay and release (exponential; REL 127 INF), `QUAD_T_OATK` / `QUAD_T_ODEC` both
operator envelopes (A's and B's: the Digitone's are the same within a few %). Between the measured values: PCHIP in
log time. **The amp attack from 112 up** is a grid: every measured full rise is a multiple of one unit (0.4962 s,
fitted 0.496135, the points within 0.014 %): 112 = 3, 114 = 4, 116 = 6, 118 = 10, 120 = 18, 122 = 30, 124 = 42,
125 = 48, 126 = 54, 127 = 60 units; between them the multiples are interpolated (113 = 3.5, 115 = 5, 117 = 8, 119 =
14, 121 = 24, 123 = 36). The shortest times are the tick's: an attack one tick (0.73 ms), a decay at once.

**The amp** is measured on the amplitude (dt_analyse.py: the full linear rise (t90 - t10) / 0.8 at 0 8 16 24 32 48 64 80
96 100 104 108 112 114 116 118 120 122 124 125 126 127; the T60 from the dB slope at 4 8 16 24 32 48 64 80 96 104 112
120 126 for the decay, 0 8 24 32 48 64 80 96 112 120 126 for the release, merged). FM TONE's are calibrated through
the same analysis all the same, which matters only at the shortest (its smear, ~3 ms): ATK 0..8 are ~3.2 ms, the decay
/ release 0 a 5 ms T60 (the release 0's reading; the Digitone's DEC 0 sounds nothing with SUS 0: not measurable). Its
shapes are the Digitone's: the attack linear (the level at half the rise 0.490 on both from ATK 32 up; below, both
readings are smeared), the decay and release dB-linear (FM TONE's slope rms <= 0.074 dB on the decay, <= 0.09 dB on the
release to 96 and 0.16..0.31 dB at 112..126; the Digitone's 0.001..0.14).

**The filter and the operators** were measured **through a proxy**: the spectral centroid of the Digitone's output (the
setups: the filter LP4, FREQ 20, RESO 40, ENV +63, ALGO 1, A LEV 96 (held), FDBK 70; operator A on ALGO 1 at A LEV 110;
operator B on ALGO 6, B2 > C, at B LEV 110). These are readings, not envelope times: FM TONE is **calibrated through the
same proxy, on the whole track**. `make_quad_times.py --calibrate` renders the firmware's own code on the host
(`tools/digitone/quad_render.c`) with the same patches (C4, velocity 100), resamples to 48 kHz and reads it with
dt_analyse.py's own functions; per measured value (run 2: run 1 sent HARM +1.00, and FM TONE's HARM +1.00 does not
sound as the Digitone's did) the engine time is the one whose centroid track (as the share of its sweep, in time) is
nearest the Digitone's over the Digitone's window (a 4 s render scaled in time, then a render at the time found).
**The shapes** (the whole tracks, FM TONE's best fit against the Digitone's, rms of the share of the sweep):
- the operator attack: the Digitone's blooms late (t10 / t50 / t90 = 0.48 / 0.84 / 1 of t90 at every value); a linear
  attack (FM TONE's until then: 0.10 / 0.54 / 1) misses it by 0.21 rms. Linear in dB from a floor fits: -24 dB 0.12,
  -30 dB 0.07, -36 dB 0.028, **-37 .. -39 dB 0.026**, -42 dB 0.036, -48 dB 0.058. FM TONE's operator attacks are now
  linear in dB from -38 dB (`QUAD_OP_FLOOR`);
- the filter decay: the curves between exponential (FM TONE's) and linear (a decay toward a target below SUS, stopped
  at SUS) fit no better than the exponential on the part above the fundamental (0.080 of the open-to-fundamental
  span; linear 0.114); what differs is the close: FM TONE's FREQ 20.00 is 80 Hz, and below the fundamental (261 Hz)
  its filter closes later. FM TONE's static map (the centroid against the cutoff, a 60 s sweep) puts the Digitone's
  FREQ 20 at about FM TONE's FREQ 0.00 (30 Hz): there **both filter tracks match whole** with FM TONE's curves (the
  decay exponential, the attack linear; rms 0.09..0.15 and 0.04..0.08 of the sweep), and the filter release (run 1)
  agrees with the decay's table (t90 -3 % at 64, -11 % at 112). The filter tables are calibrated with that patch
  (FREQ 0.00 in the mirror). **The cutoff scale is not changed**: one point (FREQ 20 is ~1.4 octaves lower on the
  Digitone) does not give a scale; a measurement of the cutoff itself (a resonant LP's peak at FREQ 0, 16, 32 .. 127)
  would;
- the operator decay: exponential, as FM TONE's (whole track 0.02..0.05).

**Residuals** (FM TONE's reading at the table's time / the Digitone's - 1, %; `quad_calib.json`):

| envelope (run 2) | t10 | t50 | t90 |
| --- | --- | --- | --- |
| operator attack 16 .. 126 | -2 .. -4 | +1 .. +2 | -1 .. 0 |
| operator decay 16 .. 126 (0) | +39 .. +134 (the Digitone's t10 is very early: 18 ms at 64) | +3 .. +10 (+2) | -6 .. -13 (+13) |
| filter attack 16, 32 | -17, -52 | +4, -16 | +37, +1 |
| filter attack 48 .. 112 (unfinished on the Digitone) | -19 .. -40 | -8 .. -17 (80: +4) | +20 .. +36 against dt_analyse's estimate (t50 x 3.04) |
| filter decay 16 .. 126 (0) | (the onset: not a reading) | -31 .. +12 (+174) | -12 .. +29 (+15) |
| amp attack (full rise), amp decay / release (T60) | | within 1 % (ATK 0: +12 %, one tick of 2.7 ms) | |
| A DELAY at 120 and 80 BPM (t50) | | 0 | |
| LFO FADE at 120 and 80 BPM (full fade) | | within 4 % | |

Cross-checks (run 1): the filter release t90 0: +51 %, 64: -3 %, 112: -11 %; operator B (ALGO 6, B2 > C) decay 0: -1 %,
64 and 112: +18 %, attack 112: +4 % (an estimate).

**Above the measured values.** The filter attack from 113: the Digitone's filter attack readings are about 30x its
amp attack's full rise at every measured value, and its sweep at 120 had reached 3 % after 30 s (126: nothing): the
table follows the amp attack's curve x the ratio at 112 (120: 4.8 min, 126: 14.5 min; the 120 capture's partial track
alone fits 4.4 min). The operator attack 127, the decays' 127: the last segment's slope in log time (AMP DEC / REL 127:
INF, the entry unused).

**DELAY** (A's, B's, the filter's ENV DELAY): v / 32 beats at the part's tempo (`song.g[G_BPM]`, as the LFOs': the
tempo, tap or MIDI clock; followed while it runs): A DELAY measured exactly (120 BPM: 16 -> 0.25 s, 64 -> 1.00 s, 112
-> 1.75 s; at 80 BPM x 1.5). **B's and the filter's are assumed the same** (not measured). The progress counts 1/32
beats in Q24: BPM x `QUAD_DLY_K` (6493) a tick.

**LFO FADE**: a linear ramp of the depth over the full fade, the same at 80 and 120 BPM and at any LFO rate: -16 0.047
s, -24 0.199, -32 0.778, -40 3.11, -48 12.43; -56 49.7 and -64 199 extrapolated by the analysis (its 20 s gates ended
at 38 % and 10 %). x4 per 8 steps: 2^(|FADE| / 4) x c (c = 3.04 ms, fitted to 24 .. 48); -4 and -8 read as FADE 0
does (the measurement's floor, ~0.04 s): the law's 6 and 12 ms. **The fade-out (+) is assumed to take the same times**
(not measured). `QUAD_T_FADE[|FADE|]` = 32767 x 2^16 / the ticks of the full fade; the voice's `ticks` are 32 bits (199
s = 274,000 ticks; the two bytes were padding).

**The amplitude ramp.** The amp's per-sample ramp over a tick is Q23 (it was Q15 >> 5, which floored a slow decay's
step to -1 a sample: the tail saw-toothed up to 31 LSB below its level, and a 1 s T60 read 2.2 % short; now 0.5 %).

**The cells** show the times as numbers 0..127, as the Digitone does (`QC_T` `F_INT`, the filter REL `F_INTI`;
F_TIME's 1 ms .. 10 s text was the old curve's). The `.dnsnd` import takes the times 1:1 ([SOUNDS.md](SOUNDS.md)). The import also reads Digitone
SysEx dumps (`.syx`: a sound, the pool or a project's) and version-4 records (OS 1.42) since 3y200; a DRIVE above 0
gets a note: the FM-1's drive comes after FM TONE's filter, the Digitone's before it (SOUNDS.md ".dnsnd import").
Since 3y201 no Digitone sound is refused for its record version: a version it does not know (a newer one, or 2 / 3) is
read with the newest layout, version 5's (the parameters in their slots, the OCTAVE found from the end marker), with a
note naming the version and saying the fields the FM-1 does not know were skipped; a record cut short gives what it
holds, FM TONE's defaults for the rest. Only a file that is not a Digitone sound (or is a Digitone II one) is refused.
**Digitone sounds imported before this change should be imported again**: their times were converted to FM TONE's
old curve.

**INIT** is the Digitone's init sound (the owner's init-sound export, decoded by web/fm1dnsnd.js, its values 1:1; the
page columns' defaults, quad_ref.py's, the importer's base): MIX -64 (was 0), A DEC 32 (60), A END 127 (64), A LEV 0
(48), B DEC 32 (60), B END 127 (0), the filter REL 64 (40), AMP ATK 8 (0), DEC 32 (64), SUS 96 (127), REL 24 (40), LFO 1
and 2 SPEED 32.00 (16.00), MULT 32 (8); LFO 3 (the Digitone has two) as LFO 1 and 2. The rest was the Digitone's
already (ALGO 1, the ratios 1.00, LP12 at FREQ 127.00, LEVEL 110, the filter ATK 0 DEC 64 SUS 0 ..).

**The factory presets** were set on the old curve: each time (ATK DEC REL of ENV A, ENV B, AMP, the filter, and
`preset_t.env`) is re-expressed as the value whose new time is nearest the old one (the full time of an attack, a
decay's T60: EP's AMP DEC 96 -> 39: 1.6 s T60, REL 62 -> 14: 0.14 s; BRASS's A ATK 50 -> 11: 39 ms), so they keep their
designed times (an operator attack now with the Digitone's late bloom). Saved FM TONE sounds play their values on the
new tables (the blob is unchanged, version 5: no conversion of older patches, the owner: not needed).

**CPU**: one look-up a tick and envelope as before; a decay's multiply 64-bit (was `mulq16`'s two 32-bit), the
operator attack's and the fade's one; the amp ramp one shift a sample. Host (`tests/run_quad_test.sh`): the presets'
average 1136 -> 1150 instructions a sample (19.6 % of the device's block), the worst case 1815 -> 1857 (31.6 %).

## Level (2026-10-16)

The owner, 2026-10-15: "The new Digitone Engine is quite a bit louder than the other engines." Measured with
`tests/va_levels.c loud` and `dnsnd` (docs/AUDIO-QUALITY.md "FM TONE's level"): BS.1770 integrated loudness (K-weighted,
gated) of every sound as a player hears it at power-on (Output Clean, MASTER as at power-on, the part at LEVEL 104,
the sends 0, FM TONE's Drive its sound's, velocity 100), three phrases: a 6-note chord held 6 s, four 4-note chords
struck a second apart, the chords' top notes alone (one voice, so no limiter).

- **The 16 factory presets were not louder**: their medians (-18.8 / -20.0 / -25.8 LUFS: held, phrase, notes) sat
  between the VA's (-16.2 / -17.7 / -23.9) and FM6's, CZ-1's and P5's (-28.6 .. -24.5 / -23.5 .. -21.2 / -29.0 ..
  -27.6). They were voiced at AMP LEVEL 62 .. 90 on the squared law (LEVEL 90 is 6 dB under 127).
- **The sounds at the Digitone's own levels were**: INIT (LEVEL 110, the Digitone's VOL default) a single voice -16.1
  LUFS against -22.1 .. -29.0 for the other engines' medians; the 256 Digitone factory sounds through the `.dnsnd`
  import (VOL 100 .. 127, as their own) -17.2 / -17.6 / -20.9, 4 to 9 dB over the other engines' medians and 1.6 to 5 dB
  over FM TONE's own presets. A user sound made from INIT, or imported, is what the owner heard.
- **Fixed at the engine's output scale**, not with trims (the whole engine was hot, not a few sounds) and not at the
  master: `quad_render` adds the voice `>> 12` where it added `>> 11`, every voice 6 dB lower into its part, and the 16
  presets' AMP LEVEL x 1.41 (EP 72 -> 102, BELL 80 -> 113, PLUCK 72 -> 102, BRASS 76 -> 107, GLASS PAD 88 -> 124,
  HOLLOW 76 -> 107, METAL 70 -> 99, CLAV 90 -> 127, STRINGS 80 -> 113, MARIMBA 78 -> 110, DRONE 74 -> 105, FEEDBACK
  72 -> 102, NOISE-ISH 62 -> 88): 13 of them within 0.1 dB of where they were, and into the part's DRIVE as before.
  BASS, SQUARE LEAD and WOBBLE were at 110 / 110 / 115 and stop at 127: 3.5 dB quieter (WOBBLE 3.3 to 3.5 with its
  drive), still in the range of the VA's basses; SQUARE LEAD was the loudest FM TONE sound and still is.
- **Before the drive, not after it.** The Digitone's order is FM -> overdrive -> base-width -> multimode -> amp
  (the manual, OS 1.44: Appendix A.1 and §11.9.5 DRV, "the signal entering the filter"; VOLUME after the drive);
  ChoralRoot's is FM -> filters -> amp (LEVEL) -> the part's DIST, the platform's insert every engine feeds, its
  make-up set for their levels. A scale after the DIST (tried:
  `fx.c mix_part`) kept a driven import's tone but made every strongly driven FM TONE sound 6 dB quieter than the same
  DIST on any other engine, and turning Drive up on a factory preset made it quieter (`test_cr.sh`'s "FM TONE's Drive
  with FX off is heard": EP at Drive 127 92 % of its RMS at Drive 0, the check wants over 120 %). Before it, FM TONE
  reaches the DIST as the other engines do: a driven import is less saturated than in 0.22 and its level falls by
  2 .. 4 dB instead of 6 (the 64 with Drive 40 .. 127: -17.4 / -17.8 / -19.2 -> -19.6 / -19.9 / -22.6).
- **After**: the 256 imports -21.2 / -21.6 / -26.7 (Drive 0: -22.1 / -22.2 / -27.4), INIT's voice -22.1; the other
  engines bit for bit (every VA, FM6, CZ-1 and P5 line of the two runs the same; 297 of the 313 goldens unchanged, the
  16 FM TONE ones rewritten). The import (web/fm1dnsnd.js) is unchanged: a sound keeps its VOL as LEVEL.

## Pops (2026-10-16)

The owner: "various sound quality issues like pops and clicks on the voices". Looked for as CZ-1's were (docs/CZ1.md
"Pops"): `tests/quad_pops.c` renders one FM TONE sound on part 0 (LEVEL 104, its Drive, Output Clean, ChoralRoot's
fade steal) through eight scenarios (hold, retrig, retrig-gap, change, steal, arp, restrike, arp-up) on four chords at
C2, C4, C6 and velocity 100 / 127 (3264 runs for the 16 presets and INIT; `list`: the 256 imports, chord 1 at C4), and
looks in the dry mix for **jumps** (a sample step 4 x the largest the tone makes around it: cz_pops.c's measure),
**clicks** (a two-sided burst of the third difference), **holes**, and in the engine's state at each block's events for a
step the render plays at its first sample: an operator restarted under the voice's level (**phase**), the amplitude
set back (**amp**), an operator envelope restarted from 0 (**oplev**, RESET), the filters cleared (**filt**).
`QUAD_TRACE=A,B` prints every voice's state at each block between two samples, `QUAD_LIST=FILE` plays an import.

**What was found (0.22, a11e0ca):**

| | jumps | clicks | phase | oplev | amp, filt, holes |
| --- | --- | --- | --- | --- | --- |
| 16 presets + INIT, 3264 runs | 92 (INIT 68, HOLLOW 24) | 410 | 1560 | 1502 | 0 |
| 256 imports, 2048 runs | 78 (29 sounds) | 267 | 1379 | 1101 | 0, 0, 1 |

1. **PHRT on a voice that still sounds** (the pops). A note struck again while its voice sounds (the same chord again, a
   chord change's common tone, an arp's note over its own tail, a MONO line) set the phases of PHRT's operators
   (ALL by default) to 0 under the voice's level; the amp envelope goes on from its level (QUAD's design), so the wave
   stepped. INIT, C2 chord struck again at 300 ms (`quad_pops one 16 1 1 36 100`): at sample 13216 the dry mix goes
   -10212 -> -435, 13 x the largest step of the tone around it; voice 0 at amplitude 16944 (half scale), its phases
   `8dbead80` -> 0. A sine-like carrier shows it most (INIT, HOLLOW); a bright one hides it under its own edges.
   **Fixed** (`quad_phrt_mask`): such a voice restarts only the operators no one hears (above). Restarting a silent
   one is kept on purpose: a decayed modulator that comes back at an arbitrary phase as its envelope leaps up in a
   tick bursts (PLUCK and BASS showed it with the first try, which kept every phase of a voice over -60 dB).
2. **A filter switched in or out within a note** (imports). The multimode is skipped while an LP is wide open with no
   resonance; an envelope or an LFO bringing it down switched it in with its states at 0: the output fell to 0 at
   once (A067 EXPLORING, LP24 with its envelope: sample 512; A210 PREACHY: 3361). The base-width high-pass is off at
   BASE 0: an LFO taking BASE to 0 let the low band back at once (A126 WAVETABELS: 106112, a block's edge). **Fixed**:
   a filter switched in starts from the state its input implies (the LP's at the input, the band's at 0; the
   high-pass from rest) and a switch either way crossfades over its block (`quad_voice_t.fon`); a sound whose filters
   never switch renders bit for bit as before (checked: the same dry mix in every run of the 16 presets but DRONE).
3. **MIX by an LFO** stepped at every block's edge (a RAMP or SQR on it a full step: A087 HEXAGON). **Fixed**: MIX is
   ramped over the block as the operator levels are (`quad_voice_t.mixp`); a MIX that does not move gives the same
   samples. DRONE (an LFO on MIX) is the one preset whose renders change for it.

**After:** presets + INIT 0 jumps, 247 clicks, 0 phase; imports 3 jumps (A132 S-MACHINE: its HARM-shaped feedback
operator, chaotic by design: the tone), 125 clicks, 0 phase. **What the clicks left are**: the sounds' own
attacks (MARIMBA's mallet as loud on a fresh note from silence as on a retrigger, PLUCK and BASS at C2: a bright first
cycle after an attack at ATK 0), BRASS's feedback cusp (one a period, 452 samples apart at G2, while its A envelope
rises), and the platform's fade steal (voice.c: a stolen voice fades over one block, the kink of 0.7 ms: x 12 .. 31
on INIT's sine, the same on every engine). **RESET** (oplev) restarting a sounding voice's operator envelope from 0 is
the Digitone's and is kept: with it off in every run the click count did not move (256 / 261).

`quad_pops check` (in `tests/run_cr_tests.sh`): every preset and INIT, every scenario, chords 0 and 2, C2 and C4,
velocity 127 (544 runs, 12 s): fails on a wrap (the sanitizer), a jump, a hole, or a phase / amp / filt step. On
a11e0ca: 258 of the 544 fail. `cr_quad_test t_switch`: the multimode switched in by its envelope, the high-pass and
the low-pass switched by an LFO, MIX by a square LFO: the largest sample step within a block of each switch against
the largest anywhere else in the note, 0.86 .. 0.99 (without the fix 39, 38, 8.7 and 5.1).

**Cost on the unit** (`tools/fx_loops.py build/choralroot.dis quad_render` against a11e0ca's build, ~205 instructions
a microsecond): the 24 operator loops 6.2 instructions a sample more on average (+0 .. +12: MIX's ramp), the DC and
base-width loop 40 -> 39 when no pole switches (a block that switches runs a loop of its own, 63), the SVF's three
loops 101 / 78 / 69 -> 100 / 75 / 70, the crossfade 14 in a switching block only: about 3.7 us a voice a half, 30 us
for 8 voices of the 2902 us half (+1.0 %). XIP and RAM: see HISTORY 3y120; POOL the same (the two fields fit the voice's
padding).

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
