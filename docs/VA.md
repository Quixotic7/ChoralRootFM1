# VA: the four-oscillator virtual analog (`firmware/src/eng_va.c`)

ChoralRoot's deep-editing synth: engine 13 (`ENGINES[]`, `ENGI_VA = 13 + FELUCCA_SLICE`; built with `FELUCCA_VA`,
1 in `choralroot.c`, the emulator (`tools/emu/emu_firmware.h`) and `tests/regress.c`, 0 for Felucca). It is fixed-point
code written for this project on `dsp.c`'s primitives (the polyBLEP oscillators and the trapezoidal SVF), with no code
taken from other synths.

## The voice

| block | what |
| --- | --- |
| 4 oscillators | MODE `BASIC MORPH NOISE` (below). BASIC: WAVE `SAW SQR TRI SIN PWM NOIS`, LEVEL 0..127 (square law), COARSE -24..24 st, FINE -64..63 ct, SHAPE 0..127, KTRK on/off (off: fixed at C4 + COARSE/FINE). SHAPE: SAW blends saw -> triangle; SQR sets the pulse width 50 % -> ~5 %; PWM sweeps the width around 50 % by SHAPE with its own ~0.8 Hz triangle (each voice in its own phase); SIN folds (up to 4x, a triangle fold); TRI and NOIS ignore it. OSC 2 SYNC: a hard sync to OSC 1 (OSC 2 restarts at OSC 1's wrap, scaled to its own phase; a ~27 Hz DC blocker runs on the mix while SYNC is on). OSC 4 RING: OSC 4 x OSC 3 (OSC 3 still plays at its own level). An oscillator at level 0 (and not a sync / ring source) costs nothing; its phase still runs. |
| MORPH | MODE MORPH: SHAPE is one continuous position, sine (0) -> triangle (24) -> saw (48) -> ramp (72, the saw reversed) -> square (96) -> a pulse narrowing to ~5 % (127, BASIC SQR's width at SHAPE 127). Between two shapes a linear crossfade of the band-limited waves (polyBLEP saw, ramp, square). The waves are aligned on the saw's fundamental (SAW and SQR are BASIC's own, bit for bit; the sine is BASIC SIN half a cycle on, the triangle BASIC TRI three quarters on), so no crossfade cancels the fundamental. The matrix's SHPn, the part's ENV / LFO -> SHP move the position: an LFO on SHP1 sweeps the wave. WAVE does nothing in MORPH; its column shows the position's name (`SIN`, `SIN>TRI`, .. `PULSE`, through `eng_deep_t.desc`). A fresh voice starts at its position (BASIC ramps SHAPE from 0 over the first tick, as before). |
| NOISE | MODE NOISE: WAVE is the noise type (`desc`: column `NTYPE`; `set` clamps it to 0..2, a stored value above 2 plays WHITE). WHITE: as BASIC NOIS. BROWN: white through a leaky integrator (a one-pole low-pass, SHAPE "COLOR" = its corner 30 Hz..3 kHz; KTRK on moves it with the key), made up to about half the white's RMS. VINYL: sparse crackle (clicks of random size and sign, gone in ~0.2 ms; SHAPE "DENS" = their density, ~1..200 a second per voice) over a low hiss (white low-passed at ~2.5 kHz, about -30 dB), and while a VINYL oscillator sounds (LEVEL > 0) the voice's pitch wows +-6 ct (a slow random walk per part, a new target every 0.3..0.75 s). Switching an oscillator to NOISE sets its KTRK off (key tracking only matters to BROWN). |
| mixer | MIX (macro, 0..127): 64 = both pairs at their levels, below fades OSC 3+4 out, above fades OSC 1+2 out. DTN (macro, 0..127): spreads the fine tunings -3, +3, -1, +1 thirds of up to 25 ct. |
| filter | the SVF: TYPE `LP BP HP NOTCH` (BP normalized to unity at its peak), CUT 0..127 (30 Hz..16 kHz), RES 0..127, DRIVE 0..127 (a pre-filter tanh at 1x..4x), KTRK 0..127 (127 = 100 %, around C4), FENV -64..63 from ENV 2 (63 = +126 cutoff steps). The part's ENV / LFO -> FLT and the platform matrix's CUT still add. MORPH 0..127 (FILTER+): 0 = TYPE as it is; above, the output moves along the cycle LP -> BP -> HP -> NOTCH -> LP, 32 steps a type, starting from TYPE (from LP: 32 = BP, 64 = HP, 96 = NOTCH; NOTCH = LP + HP), crossfading the SVF's outputs (as weights of the input, the band and the low-pass: at 32, 64, 96 exactly the discrete type). The matrix's FMORPH adds (+-126 at a full source). SPREAD 0..127 (FILTER+): a second SVF per voice; the left one's cutoff goes down, the right one's up, +-1 octave at 127; the voice renders mid (L + R) / 2 and side (R - L) / 2 (see "Stereo"). |
| VOICE | USPREAD 0..127: with P_VOICE UNISON, voice i of the 8 is panned to (2i - 7) / 7 x USPREAD / 127 (its side = its mid x that): the unison fans out left / right in the order of its detune. Nothing in POLY / MONO / LEGATO. |
| envelopes | 4 AHDSR at control rate (every 32 samples), ATK HOLD DEC SUS REL 0..127 on Felucca's tables (`ENV_LIN` / `ENV_EXP`: 1 ms..10 s, the same feel as the ADSR). ENV 1 is the amplitude (`engine_t.ownenv` / `done`, as FM6): the voice ends when ENV 1's release is done. VEL 0..127 (ENV 1+): velocity -> ENV 1's level, 0 = none. ENV 2 drives FENV. ENV 3 and 4 are free for the matrix. A retrigger restarts every envelope's attack from its current level. |
| LFOs | 4 per part at control rate: RATE 0..127 (0.05..40 Hz, Felucca's table) or, with SYNC, a division of the tempo (`song.g[G_BPM]`; RATE x 14 / 128: `8BAR 4BAR 2BAR 1BAR 1/2 1/4. 1/4 1/4T 1/8. 1/8 1/8T 1/16 16T 1/32`), WAVE `SIN TRI SAW SQR S&H`, DEPTH 0..127, FADE 0..127 (per voice, from its note-on). A fresh phrase (no key held on the part) restarts them. The matrix sets their destinations. |
| matrix | 8 slots SRC -> DST by AMT -64..63, evaluated once per control tick per voice. SRC: `OFF ENV1..ENV4 LFO1..LFO4 VEL KEY RAND MODW` (KEY: +-64 st around C4 = +-1; RAND: per note; MODW: CC1). DST: `OFF PITCH PIT1..PIT4 LVL1..LVL4 SHP1..SHP4 CUT RES AMP PAN RATE1..RATE4`. At a full source and AMT 63: PITCH / PITn +-12 st, LVLn / SHPn / RES +-126, CUT +-63 steps, AMP as mod.c's (AMT > 0 the source opens it, < 0 closes it), PAN +-63 (the part's pan: `fx.c mix_part`), RATEn +-126. Every sum is clamped to its range. PAN and RATE are per part: they take the latest note's voice. Pitch, level and shape move per sample as linear ramps over the tick; the filter's coefficients and the rest are set once per tick. |

`poly` is 8 (the 6-note chord + bass budget holds: see CPU). UNISON plays its voices at 2/5 each (as FM6).

### Stereo (`engine_t.render2`)

The VA is the platform's one engine with a stereo voice: `engine_t.render2(t, v, out, side, n, m)` renders the mid
into `out` as `render` would and the side into `side`, returning 1 if it added to it. `voice.c track_render` calls it
instead of `render` (clearing `part_side`; `part_side_on` = a voice added to it), `fx.c mix_part` then plays the part
as L = mid - side, R = mid + side, after LEVEL and with the pan law (and the matrix's PAN) on top. DIST works on the
mid; the SLICER (P_SLCR on, or a repeat ringing) plays the part mono; the FX layer's mute fades both; the sends (mono
buses) take the mid. With SPREAD 0 and no USPREAD nothing reaches `side` and the part is mono bit for bit as before:
the golden renders of the 20 version-1 presets did not change. The decision: the full stereo path (two SVFs per
voice), not the cheap alternate-voice variant: the worst VA case on the emulator, a 6-note chord of MORPH PAD (two
MORPH oscillators, SPREAD 40) + PUNCH BASS, is 38 % average / 54 % worst block of the device's half (WIDE STRINGS,
four saws + SPREAD 110: 36 % / 52 %; perf (f), ENSEMBLE STR: 31 % / 43-45 %), under the 60 % limit.

## Pages (`eng_deep_t`, the UI contract)

32 pages; `section[] = {0, 8, 10, 18, 24, 0xFF}` (OSC, FILTER, ENV, LFO, MOD). Blank columns have `label == 0`.
`eng_deep_t.desc(t, page, col)` (optional, 0 = the table's column) gives a mode-dependent label / value names: VA's
WAVE on OSC n (MORPH: `MORPH` with the position's name; NOISE: `NTYPE` `WHITE BROWN VINYL`, max 2) and SHAPE on OSC n+
(MORPH: `MORPH`; NOISE: `COLOR` for BROWN, `DENS` for VINYL). The stored value and `set` are the table's.
ENV uses 8 pages (each envelope's HOLD on its `+` page; VEL on `ENV 1+`). The `LFO SYNC` page is titled `LFO SYN`
(7 characters). SRC and DST include `OFF` at value 0.

| # | title | KNOB 1 | KNOB 2 | KNOB 3 | KNOB 4 |
| --- | --- | --- | --- | --- | --- |
| 0 | `OSC 1` | WAVE | LEVEL | COARSE | FINE |
| 1 | `OSC 1+` | MODE | SHAPE | KTRK | - |
| 2 | `OSC 2` | WAVE | LEVEL | COARSE | FINE |
| 3 | `OSC 2+` | MODE | SHAPE | KTRK | SYNC |
| 4 | `OSC 3` | WAVE | LEVEL | COARSE | FINE |
| 5 | `OSC 3+` | MODE | SHAPE | KTRK | - |
| 6 | `OSC 4` | WAVE | LEVEL | COARSE | FINE |
| 7 | `OSC 4+` | MODE | SHAPE | KTRK | RING |
| 8 | `FILTER` | TYPE | CUT | RES | DRIVE |
| 9 | `FILTER+` | KTRK | FENV | MORPH | SPREAD |
| 10 | `ENV 1` | ATK | DEC | SUS | REL |
| 11 | `ENV 1+` | HOLD | VEL | - | - |
| 12 | `ENV 2` | ATK | DEC | SUS | REL |
| 13 | `ENV 2+` | HOLD | - | - | - |
| 14 | `ENV 3` | ATK | DEC | SUS | REL |
| 15 | `ENV 3+` | HOLD | - | - | - |
| 16 | `ENV 4` | ATK | DEC | SUS | REL |
| 17 | `ENV 4+` | HOLD | - | - | - |
| 18 | `LFO 1` | RATE | WAVE | DEPTH | FADE |
| 19 | `LFO 2` | RATE | WAVE | DEPTH | FADE |
| 20 | `LFO 3` | RATE | WAVE | DEPTH | FADE |
| 21 | `LFO 4` | RATE | WAVE | DEPTH | FADE |
| 22 | `LFO SYN` | SYNC1 | SYNC2 | SYNC3 | SYNC4 |
| 23 | `VOICE` | USPREAD | - | - | - |
| 24..31 | `MOD 1`..`MOD 8` | SRC | DST | AMT | - |

DST gained `FMORPH` at its end (value 22). Formats: WAVE / MODE / TYPE / SRC / DST `F_ENUM` (MODE `BASIC MORPH NOISE`);
LEVEL SHAPE RES DRIVE KTRK(filter) SUS VEL DEPTH MORPH SPREAD USPREAD `F_PCT`; COARSE `F_SEMI`;
FINE `F_INT` "ct"; KTRK(osc) SYNC RING SYNCn `F_ONOFF`; CUT `F_CUTOFF`; FENV AMT `F_BIPCT`; ATK HOLD DEC REL FADE
`F_TIME`; RATE `F_LFOHZ` (with SYNC on, the value picks the division above: `N_VA_DIV` in eng_va.c names them).

`get` / `set` run in the main loop and `set` clamps. EDIT 1 / EDIT 2 (P_E0..P_E7) are macros that write into the patch:
`CUT RES FENV DRIVE | MIX DTN ATK REL` (ATK and REL are ENV 1's). A deep `set` of a macro's value also writes the
macro back to P_E. A knob, the editor, motion or the platform matrix moving a P_E reaches the patch at the next audio
block (`va_block`, `va_mlast`). HOME's knobs (`knob[]`): CUT RES ATK REL. The part's ENV page (P_ATK..P_REL) does
nothing for VA, as for FM6.

## The patch and the blob

The patch has 106 signed values per part (`va_patch[2][106]`, in the pool): version 1's 99 in their places
(OSC 1..4 x {WAVE LEVEL COARSE FINE SHAPE KTRK} 0..23, SYNC2 24, RING4 25, TYPE CUT RES DRIVE KTRK FENV 26..31,
ENV 1..4 x {ATK HOLD DEC SUS REL} 32..51, VEL 52, LFO 1..4 x {RATE WAVE DEPTH FADE SYNC} 53..72, MOD 1..8 x {SRC DST
AMT} 73..96, MIX 97, DTN 98), then version 2's: MODE of OSC 1..4 99..102, FILTER MORPH 103, SPREAD 104, USPREAD 105.
The blob (version 2) is 110 bytes: `'V'`, 2, then each value minus its minimum (all 7-bit), then 2 zero bytes. A
version-1 blob (`'V'`, 1, 99 values, zero padding to 104 bytes) is still taken: its values keep their places and the
new ones take their init values (MODE BASIC, MORPH / SPREAD / USPREAD 0: the sound as it was); `va_pack` always
writes version 2. `blob_set(0)` or a bad blob (magic, version, a value out of
range, padding not 0, an erased 0xFF record) loads the init patch: OSC 1 SAW at 100, the others silent, LP 100,
ENV 1 0/0/64/127/40, VEL 64, LFOs 70 Hz-index SIN at full depth, the matrix off, MIX 64. `blob_preset(k)` loads the
init patch with preset k's edits (`VA_PRESET_EDITS`, (index, value) pairs).

**Sound loads.** Every load path calls `eng_fm6.c fm6_track_loaded`, which calls `va_track_loaded` (one guarded line):
a cr_ui.c preset load, `cu_load_user` / `up_load`, INIT, and an engine switch. A user slot of VA, marked by
`upreset.c up_values` (`va_user_pending`), loads its stored patch. A track whose P_E equal preset `t->preset`'s macros
loads that preset's patch. Anything else (INIT, a slot with no stored patch) loads the init patch with the track's
macros on it.

**The store** (`firmware/src/va_store.c`, ChoralRoot only). It holds 32 patches, slot k <-> patch k, in one storage.c
object (`OBJ_VASTORE = OBJ_PROJECT0`: A/B at 0x97000 / 0x98000, sectors ChoralRoot's projects never use). Its header
is 16 bytes plus 32 x 110 = 3536 bytes (store version 2), so the payload ends 3792 bytes into its sector and the tail
stays erased. It is mirrored in the pool and uses storage.c's commit protocol unchanged. A version-1 store (104-byte
blobs, 3344 bytes) is converted in the mirror at boot (`va_store_v1`: in place, the last slot first; a bad patch is
dropped) and written as version 2 by the next save (trace: `va: store version 1 imported (N patches)`). Hooks in `upreset.c`:

- `up_put` after a successful store: for a VA record, the patch of the part it was saved from goes to patch k. The part
  is a VA part whose parameters equal the record's (P_VOICE aside), the selected part first. For another engine's
  record, patch k is cleared.
- `up_put(k, 0)` (delete) clears patch k.
- `up_rename` keeps the stored patch.
- `up_boot` reads the store.

Saving writes the user bank, then the store (two flash erases, explicit saves only). With `CR_TRACE` (the emulator)
it prints `va: save slot N part P patch crc XXXX` and `va: load slot N patch crc XXXX` (CRC-32 of the blob, low
16 bits).

## Presets

19 chord sounds (bank rows `PRESETS` 25..43, after the 24 existing ones) and 4 basses (`ALGORITHM` 9..12; `mono`).
The bank trim column is 0 for all of them: the levels are set in the presets.

LUSH PAD, WARM PAD, GLASS PAD, SLOW STRINGS, ENSEMBLE STR, SYNTH BRASS (bank name "VA BRASS": ANALOG's BRASS is
"SYNTH BRASS" in the bank), SOFT BRASS, POLY KEYS, PWM KEYS, CLAV, SOFT LEAD, HOLLOW (sync), BELLS (ring), SWEEP PAD
(ENV 2 + LFO -> CUT), SOFT AAH (the "choir-ish": two detuned saws + an octave triangle through a resonant BP with a
slow LFO on the cutoff), ORGANISH (a sine stack 1, 2, 3, 4 x), DEEP SUB, PUNCH BASS, RUBBER BASS, SYNC BASS; then
version 2's (preset indices 20..22, bank rows 41..43): MORPH PAD (two MORPH oscillators at SHAPE 44 / 56 swept by two
slow LFOs on SHP1 / SHP2 in opposite directions, a sine an octave down, SPREAD 40), VINYL KEYS (a triangle + a sine an
octave up, keys-like, with OSC 4 NOISE VINYL at LEVEL 50, DENS 40: the crackle and the wow under it), WIDE STRINGS
(ENSEMBLE STR's four saws with SPREAD 110). VINYL KEYS' crackle is clicks by design: `wavclicks.py` counts them.

### Levels (the limiter)

The method is docs/INTEGRATION.md Defaults: a 6-note chord held, both parts at LEVEL 92, MASTER at power-on, the
share of 0.3..5.9 s under the limiter's gain. Measured on the host with `tests/va_levels.c`, which uses ChoralRoot's
mix (`fx_smooth`, `voice_fade_steal`) and the chord D4 F#4 A4 B4 C#5 E5 + SUB BASS D2. This harness is stricter than
the emulator figures in INTEGRATION: its references read STRINGS 0.3 / 21.9 % against the doc's 1.6 / 13.3 %. Limited
share, chord alone / with SUB BASS:

| preset | limited | peak / RMS with the bass |
| --- | --- | --- |
| LUSH PAD | 0 / 3.9 % | -4.7 / -15.9 dBFS |
| WARM PAD | 0 / 0.3 % | -5.7 / -16.3 |
| GLASS PAD | 0 / 0 % | -7.2 / -17.3 |
| SLOW STRINGS | 0 / 6.6 % | -4.7 / -15.6 |
| ENSEMBLE STR | 0 / 0.8 % | -5.7 / -16.1 |
| SYNTH BRASS | 0 / 0 % | -6.7 / -17.0 |
| SOFT BRASS | 0 / 0 % | -7.6 / -17.5 |
| POLY KEYS | 0 / 0 % | -8.5 / -18.4 |
| PWM KEYS | 0 / 0 % | -8.1 / -17.8 |
| CLAV | 0 / 0 % | -17.4 / -20.8 (decays) |
| SOFT LEAD | 0 / 0 % | -7.0 / -17.6 |
| HOLLOW | 0 / 0 % | -7.8 / -17.6 |
| BELLS | 0 / 0 % | -10.0 / -19.6 (decays) |
| SWEEP PAD | 0 / 3.9 % | -4.9 / -15.7 |
| SOFT AAH | 0 / 0 % | -7.0 / -17.4 |
| ORGANISH | 0 / 0.4 % | -5.6 / -16.5 |
| MORPH PAD | 0 / 1.5 % | -5.4 / -16.0 |
| VINYL KEYS | 0 / 0 % | -7.8 / -17.5 |
| WIDE STRINGS | 0 / 0.6 % | -5.7 / -16.7 |
| basses, with TINE EP's chord: DEEP SUB / PUNCH / RUBBER / SYNC | 0 % | -7.9 / -6.4 / -6.4 / -8.1 dBFS peak |

## CPU

Device estimate = host instructions / 100 x 1.7 % of the 2.9 ms half (docs/INTEGRATION.md Performance).

- A 6-note chord + SUB BASS, per preset (`tests/va_levels.c`, host instructions a sample -> device estimate):

| preset | instr | device | preset | instr | device |
| --- | --- | --- | --- | --- | --- |
| LUSH PAD | 1680 | 28.6 % | BELLS | 1715 | 29.1 % |
| WARM PAD | 1461 | 24.8 % | SWEEP PAD | 1714 | 29.1 % |
| GLASS PAD | 1574 | 26.8 % | SOFT AAH | 1608 | 27.3 % |
| SLOW STRINGS | 1703 | 29.0 % | ORGANISH | 1646 | 28.0 % |
| ENSEMBLE STR | 1850 | 31.4 % | MORPH PAD | 2269 | 38.6 % |
| SYNTH BRASS | 1500 | 25.5 % | VINYL KEYS | 1608 | 27.3 % |
| SOFT BRASS | 1539 | 26.2 % | WIDE STRINGS | 2191 | 37.3 % |
| POLY KEYS | 1543 | 26.2 % | basses (with TINE EP's chord) | 1440-1503 | 24.5-25.5 % |
| PWM KEYS | 1356 | 23.1 % | ref FM6 PAD | 1594 | 27.1 % |
| CLAV | 1537 | 26.1 % | ref ANALOG STRINGS | 1092 | 18.6 % |
| SOFT LEAD | 1538 | 26.2 % | | | |
| HOLLOW | 1822 | 31.0 % | | | |

  What the version-2 features cost (8 voices, `cr_va_test`-style driver, host instructions a sample): SPREAD (the second
  SVF, the side) about +440 (+55 a voice), a MORPH oscillator about +7 a voice over a BASIC saw; MORPH PAD 2278 (1847
  without its SPREAD, 1742 with BASIC oscillators), WIDE STRINGS 2151 (ENSEMBLE STR 1747). FILTER MORPH alone costs
  the general filter loop (weights instead of the type's switch), a few instructions a voice.
- Emulator `perf.sh (f)` (ENSEMBLE STR chord + PUNCH BASS, a chord change): average 31 %, worst block 43-45 %. The
  same script on MORPH PAD (PRESETS +40): 38 % / 54 %; on WIDE STRINGS (+42): 36 % / 52 %. The worst blocks fall at
  random times through the hold, the host's per-block noise also seen in scenarios (a)-(e). `poly` stays 8.
- `tests/regress.c` (8 notes, one part): `cpu/VA/*` in `tests/cpu_baseline.txt`.
- `tests/target_budget.txt`: `va_render` (now the stereo body, `render2`; `va_render_mono` is `render`'s wrapper),
  `va_block`. The static cost grew with the new loops (MORPH, BROWN, VINYL, the general filter loop): 72014 against
  32669; they are alternatives, one voice runs one of each.

## Memory

- RAM: about 240 bytes of .bss (`va_user_pending`, the store's read hook, the PAN offsets; `voice.c part_side`, 128
  bytes, the stereo side of the part being mixed; `cr_out.c`'s transposition per part, 8 bytes). The patches, the
  macros' last values, the LFOs and the per-voice state (2 x 8 x 112 bytes) are in the pool.
- POOL: about 6 KB (the store mirror is 3536 bytes, the voice state 1.8 KB). After version 2: RAM 92.3 %, POOL spare
  14420 bytes.
- Flash: the presets are (index, value) edit lists.

## Tests

| test | what |
| --- | --- |
| `tests/cr_va_test.c` (`sh tests/run_cr_tests.sh`) | blob round trip of 2000 random patches; bad blobs -> init; the page table against the patch ranges; set clamps; macros both ways; `va_track_loaded`; every preset's blob and macros; the envelopes reach sustain, end, and keep ATK / HOLD times; the LFO SYNC divisions at 60 / 120 / 180 BPM (1 %); 1.4 s of a 6-note chord per preset (no int32 wrap, peak < 0.9 FS at LEVEL 92, voices free after the release); the matrix at its extremes on every wave and filter type (no overflow) |
| `tests/cr_va_test.c`, version 2 | a version-1 blob (1000 random ones) and a version-1 store (`va_store_v1`, empty and bad slots) import with every old value, the new ones at init; the page table (MODE first on OSC n+, FILTER+ MORPH SPREAD, VOICE USPREAD, MOD 1 at 24); `desc` and `set`'s NOISE rules (WAVE clamped, KTRK off once); MORPH at SIN / TRI / RAMP / PULSE = the BASIC wave aligned (+-1 LSB, 50000 phases), at SAW / SQR = BASIC SAW / SQR through the whole voice bit for bit; MORPH continuous (no step > 64 between neighbouring positions up to the square; the pulse's width moves only its edges); BROWN's HF energy < 1/50 of white's (SHAPE 0) and its level, VINYL sparse (~4 clicks / s at DENS 40, 0.06 % loud samples) and denser at 127; FILTER MORPH 32 / 64 / 96 from each TYPE = the discrete types bit for bit; SPREAD 0: render2 = the mono render bit for bit, no side, every preset; SPREAD 127: R brighter than L; USPREAD voice 0 hard left, voice 7 right, none in POLY; the matrix extremes with the modes, FMORPH, SPREAD, USPREAD (no overflow, voices end) |
| `tests/cr_trans_test.c` (`run_cr_tests.sh`) | TRANSPOSE through `cr_out.c`'s stream callbacks: +12 / -5 / clamps at 0 and 127, MIDI out as played, a change while held keeps the held notes and their note-offs end them, the parts' own values, panic, a re-route while held |
| `tests/regress.c` | golden renders + health + CPU of the 23 presets (`preset/VA/*`, `cpu/VA/*`); the 20 version-1 renders unchanged |
| `tools/emu/scripts/va_chord.txt` (`test_cr.sh`) | LUSH PAD from the bank, a held chord, a chord change: sound, screenshots, silence after the tails |
| `tools/emu/test_persist.sh` (`va_persist_set.txt` / `va_persist_check.txt`) | a VA sound edited on a deep page, saved to U01, the emulator restarted on the same flash file, U01 loaded: the same patch CRC |
| `tools/emu/perf.sh (f)` | 6-note VA chord + VA bass: CPU, clicks |
| `tests/va_levels.c` | the limiter table and the CPU above (a tool, not pass / fail) |

Clicks (`tools/emu/wavclicks.py`): every VA preset with a held chord and two chord changes, and the basses, shows no
jump and no hole. The only clicks are the attacks from silence of the sounds with ATK 0..10 (POLY KEYS, PWM KEYS, SOFT
LEAD, HOLLOW, PUNCH BASS, SYNC BASS) and CLAV's new chords after its own decay (the same thing). These are the
"attack from silence" of perf.sh (a). Version 2: perf (f) and its MORPH PAD / WIDE STRINGS variants show only PUNCH
BASS's attacks (1.14 s; on MORPH PAD also its retrigger at the chord change, 4.14 s, at the x12 threshold: the chord
alone shows 0 clicks).
