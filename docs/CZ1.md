# ChoralRoot FM-1 — CZ-1

CZ-1 is ChoralRoot's Casio CZ-1 engine (**engine 14**, `ENGI_CZ`). It is **Melodee's** CZ-1 (Kerem Kilic's fork of
Felucca, <https://github.com/keremimo/melodee>, GPL-3.0): native Casio tones (the 144-byte CZ-1 tone kept verbatim),
two lines with eight-step pitch / timbre (DCW) / volume (DCA) envelopes, the uPD933 phase functions and logarithmic
amplitude, Casio's 64 preset tones, eight 16-tone banks and Casio's tone SysEx over USB-MIDI. This is phase 2 of 3 of
the Melodee platform work (phase 1: FM6, docs/FM6.md; phase 3: USB audio, docs/USB-AUDIO.md).

| file | what | from |
| --- | --- | --- |
| `firmware/src/eng_cz.c` | the engine: the factory presets, BANK / PTCH, the done test, the USB SysEx collector; the phase family's eight-point envelope and note reset (Melodee's `eng_phase.c`: ChoralRoot's PHASE keeps Felucca's own envelopes, so they live here); ChoralRoot's deep pages, blob and load hook | Melodee + ChoralRoot's additions |
| `firmware/src/cz_native.c` | the tone renderer: DCO / DCW / DCA envelopes at the chip's rate law, the 11-bit phase functions, the windows, RING / NOISE, the vibrato, key follow and velocity | Melodee (Devin Acker's uPD933 model, BSD-3-Clause), as it is |
| `firmware/src/cz_patch.h` | the 144-byte tone (128 synthesis bytes + the 16-character LCD name), its check, the init tone | Melodee (`ENGI_CZ` is core.h's) |
| `firmware/src/cz_edit.h`, `cz_legacy.h` | the tone's panel values (decode, encode only the bytes an edit changes) | Melodee (its page-system glue and CZ TOOLS left out) |
| `firmware/src/cz_bank.c` | the eight banks A..H (16 tones each; A..D Casio's until saved, E..H empty) | Melodee (flash objects ChoralRoot's) |
| `firmware/src/cz_store.c` | Casio SysEx: tone send / receive, the handshake | Melodee, adapted (below) |
| `firmware/src/cz_ustore.c` | the tone store: one tone per user slot | ChoralRoot (as `fm6_ustore.c`) |
| `tools/gen_cz1_factory.py`, `tools/make_cz1_factory.py`, `assets/cz1-factory/` | Casio's 64 tones -> `build/gen/melodee_cz1.h` (`tools/build.py` generate()) | Melodee |

The copyright headers are kept ("Copyright (c) Devin Acker / Modifications Copyright (c) 2026 Kerem Kilic (Ellic
Studio)" on `cz_native.c`, Kerem Kilic's on `cz_store.c` and `cz_bank.c`, Leo Kuroshita's on the envelope from
`eng_phase.c`); LICENSING.md lists Melodee, the uPD933 model (`LICENSES/BSD-3-Clause-uPD933.txt`) and Casio's tone data.
Melodee's web-editor side (`editor_cz.c`, commands 75..77) is not ported: ChoralRoot has no web editor.

## The engine

`FELUCCA_CZ` (core.h, default 0: Felucca's unit; 1 in `choralroot.c`, the emulator and `tests/regress.c`). The engine
number is `13 + FELUCCA_SLICE + FELUCCA_VA` = **14** on ChoralRoot (after the VA's 13; Melodee numbers it 15):
`ENGINES[]` stays append-only, `ENGINE_ORDER` shows it **after the VA** since 0.14 (after PHASE before; the PRESETS
browse, the engine picker's roots: FM6, FM TONE, VA, CZ-1, WHEEL). Its per-voice state (`cz_part_t`: six envelopes, the vibrato, the noise, 8
voices) is a member of `engines.c`'s `eng_state` union (smaller than FM6's: the union does not grow); the tone of each
part (`cz_patch[NTRK]`, 4 x 144 bytes) is in the pool, and since 0.18 what the renderer builds from it once per tone
(`cz_defs_`, 2 x 412 bytes: "The render's cost on the unit" below).

EDIT 1 / 2 are Melodee's: **BANK** (A..H) and **PTCH** (0 = INIT, 1..16), TONE fixed at "native" (`P_E7` 2); HOME's
knobs are BANK, PTCH. Turning BANK / PTCH loads that bank slot's tone (`cz_bank_poll`, the UI frame); an empty slot
keeps the tone. **PRESETS** = INIT TONE (the init voice) and Casio's 64, A-1 BRASS 1 .. H-8 TYPHOON SOUND, named as
Melodee lists them (12 characters: FUNKY CLAV 1, SYN STRINGS, TYPHOON, ..); a factory preset loads Casio's tone
whatever the banks hold, with BANK / PTCH at its place in banks A..D (A-1 .. B-8 = BANK A PTCH 1..16, ..). Dry, as the
CZ-1 (no sends). The engine plays its own envelopes (`ownenv`: the platform's ADSR is not applied), POLY up to the 8
shared voices (2 budget units each), no `poly` cap (CPU below).

Velocity amounts (V.PIT, V.WAV, V.AMP) follow the note velocity. Casio's tones with no DCA sustain point (BELLS,
SITAR, JET ROAR) play their whole envelope regardless of the key, as on the CZ: SITAR rings ~60 s after the note-off,
BELLS ~21 s, JET ROAR ~28 s (`tests/regress.c` gives these three 90 s to end). None of them is a bank row.

## The blob and the tone store

`eng_deep_t.blob_size` 144: the tone as it is (128 synthesis bytes + the LCD name). core.h's blob cap is now
`ENG_BLOB_MAX` 160 (was 128): `cr_ui.c`'s picker snapshot and `cu_snd_crc` size their buffers by it; the VA's 110-byte
and FM6's 128-byte blobs and their store records are unchanged. A bad tone (`cz_patch_valid`) loads the init tone.

The tone store (`cz_ustore.c`) keeps a tone per user slot, 1:1, as `fm6_ustore.c` does: SAVE of a CZ-1 sound stores the
tone of the part it came from, another engine's sound over the slot or an erase clears it, loading the slot gives the
part its tone back (`cz_user_pending`, `cz_track_loaded` through `fm6_track_loaded` on every load path). A slot of no
tone loads its BANK / PTCH from the banks. 32 x 144 bytes do not fit one storage object (3840 bytes), so two objects of
16 slots, 2320 bytes each (16-byte header: "CZ1U", version 1, 16 slots, the first slot, the tone size, the used mask),
mirrored in the pool.

## Flash

| object | storage.c | A / B | backup id |
| --- | --- | --- | --- |
| the tone store, slots 1..16 | `OBJ_CZSTORE0` = `OBJ_PROJECT0 + 3` | 0x9D000 / 0x9E000 (the last project pair; 0x97000 VA store, 0x99000 / 0x9B000 FM6 store) | 12 |
| the tone store, slots 17..32 | `OBJ_CZSTORE1` | 0xA0000 / 0xA1000 | 13 |
| the CZ-1 bank A..H | `OBJ_CZBANK0 + k` | 0xA2000 + k x 0x2000 / + 0x1000 (to 0xB1FFF) | 14 + k |
| (FM TONE's patch store: engine 15, `eng_quad.c`, code name QUAD, docs/QUAD.md) | `OBJ_QUADSTORE` = `OBJ_CZBANK0 + 8` | 0xB2000 / 0xB3000 | 22 |

0xA0000..0xB1FFF is the user sample slot 1's flash, unused since the all-synth change (`FELUCCA_SAMPLE` 0; storage.c's
map); 0xB2000 / 0xB3000 hold FM TONE's patch store (0.14), 0xB4000 / 0xB5000 the session (0.18, docs/SETTINGS.md), 0xB6000..0xC7FFF stays free, 0xC8000.. holds the loops. SAFE
MODE's Flash Data erase covers 0xA0000..0xB3FFF too (`cr_ui.c` `cu_flash_erase`: 56 sectors with FM TONE's pair).

## The banks (cz_bank.c)

Melodee's: eight banks of 16 tones (`cz_bank_t`, 2332 bytes: magic "CZCB", version 1, a 16-character name, the used
mask, 16 tones), one RAM cache (the pool) of the bank in use. A bank never saved is its default: BANK A..D Casio's 64
(A-1 .. B-8, C-1 .. D-8, E-1 .. F-8, G-1 .. H-8, named "CZ-1 A1-B8" ..), E..H empty. Casio's 64 are in flash packed, a bank of 16
an LZ4 block (`build/gen/melodee_cz1.h CZ_FACTORY_LZ4`, 4660 B where the tones were 9216; `tools/gen_cz1_factory.py`
packs with `tools/lz4blk.py`, `cz_patch.h cz_lz4_block` unpacks a bank into the cache or a tone into a load's buffer,
HISTORY 3y128; on the FM-1 in the resource region since 3y293, `cr_res.h`: refused, a factory tone is not loaded); the tests read the tones as they are with `-DCZ_FACTORY_RAW`. As in Melodee a bank comes in and
goes out whole through the **backup** (Melodee's backup ids 9..16, ChoralRoot's 14..21): `cr_backup.c` lists a saved
bank, restores one (Melodee's check), and the parts on that bank reload its tones. A backup carries only saved banks:
restoring one without a bank brings the default back. There is no Casio bank dump (the CZ-1 has none); a tone goes in
and out by Casio SysEx (below).

## Casio SysEx (cz_store.c)

Melodee's, on any channel n; Casio's low nibble first, no checksum. The bytes reach `cz_sx_byte` from `usb.c`
`sysex_byte` (beside FM6's `fm6_sx_byte`), the UI frame serves them (`cz_service`, `cr_ui.c` beside `fm6_service`):

| message | effect |
| --- | --- |
| `F0 44 00 00 7n 30 <288 nibbles> F7` (a 144-byte CZ-1 tone dump) | the CZ-1 part's tone, verbatim; "CZ TONE LOADED", the sound marked edited |
| `F0 44 00 00 7n 30 <256 nibbles> F7` (a 128-byte CZ-101 / 1000 tone) | the same, with the CZ-1 line levels, velocity and DCW key-follow table filled in (the part's name kept) |
| `F0 44 00 00 7n 20 60 ..` / `21 60 ..` (receive request + the tone) | the handshake (`7n 30` back), then the tone in; the acknowledgement |
| `F0 44 00 00 7n 10 60 F7` / `11 60` (send request) | the handshake, then the part's tone as a 128- / 144-byte dump |

The CZ-1 part: the part the sound editor shows if it plays CZ-1, else the chord part, else the bass part (the channel
does not choose, as FM6). An invalid tone is refused ("CZ INVALID TONE"); no CZ-1 part: "CZ: NO CZ-1 PART". The frames
out go through `CZ_SEND` (the device: `ota_wire_send`).

## The deep pages (the sound editor)

`CZ_DEEP` (37 pages, `section = {0, 3, 5, 35, 36}`); every column is a panel value of `cz_edit.h` (`cz_dref`); a set
writes back only the tone bytes whose encoding changed (an imported tone keeps its exact rates and levels until that
value is turned), the name stays. SUS sounds only before END (turning it past END gives "-"), a new END drops a SUS at
or after it, END's own level is 0: the CZ's rules (`cz_ed_put`). `mod_dst` is -1 everywhere: no matrix.

| section (editor group) | pages | KNOB 1..4 |
| --- | --- | --- |
| OSC (FX) | `LINE 1`, `LINE 2` (a stack of two) | WAVE (SAW SQUARE PULSE NULL DBL SINE SAW-PULS MULTISIN PULSE2) · WAVE2 (OFF + the eight) · WINDOW (OFF SAW DN TRIANGLE TRAPEZ HALF SAW 2 SAW UP 2 SAW 6 2 SAW 7) · LEVEL (1..15); then the mixer (the two LEVELs) |
| | `DETUNE` | SIGN (+ / -) · OCT (0..3) · NOTE (0..11) · FINE (0..60) (line 2) |
| FILTER (SEL) | `DCW` + `DCW+` | W.KEY1 W.KEY2 (DCW key follow 0..9) · V.WAV1 V.WAV2 (DCW velocity 0..15) / A.KEY1 A.KEY2 (DCA key follow) · V.AMP1 V.AMP2 (DCA velocity) |
| ENV (ENV) | `PITCH n`, `DCW n`, `DCA n` (n = line 1, 2; in that order) | five pages each: `X n` R1..R4, `X n+` L1..L4, `X n B` R5..R8, `X n B+` L5..L8, `X n S` SUS (1..8, -) · END (1..8) (PITCH: · V.PIT 0..15); three screens an envelope under the **cz** band, top right `DCW 1 · 1-4`, `· 5-8`, `· END` |
| LFO (LFO) | `VIB` | WAVE (TRI SAW UP SAW DN SQR) · DELAY · RATE · DEPTH (0..99) |
| MOD (SEQ) | `TONE` | LINE (LINE1 LINE2 1+2 1+1) · MOD (OFF RING NOISE) · OCT (-1 0 +1) |

The ENV group's screens follow the pages' runs: `cr_edit.c` now groups the FILT / ENV pages by instance **and name**
(`PITCH 1` and `DCW 1` are two runs; the VA's and FM6's pages group as before). The **cz** band (`CR_W_CZ`, cr_draw.c;
`ce_is_cz`: a run whose pages hold R1.. and SUS / END): the eight steps from 0 to L1 at R1 .. L k at R k, the END step
to 0, a dashed hold after the SUS step's point, steps after END not drawn; a step's width grows with its distance and
the slowness of its rate; the step of the cell just turned (R k / L k; SUS / END: their step) thick in its knob's
colour, digits 1..8 (and S) under the steps. `cr_screen_t.wv` grew to 20 bytes (R1..R8, L1..L8, SUS, END, the lit step).

Melodee's **CZ TOOLS** page (NAME, LINE 1 > 2, 2 > 1, COMPARE) is **not ported**: ChoralRoot's editor pages hold
values, not actions, and the editor's own SAVE names the sound; COMPARE's pool copy is left out with it.

## The bank rows (cr_bank.c)

Five chord rows at the END of `CB_CHORD` (PRESETS 44..48; the other rows keep their places) and one bass at the end of
`CB_BASS` (ALGORITHM 13). Felucca's PHASE imitations (CZ STRINGS / CZ ORGAN / CZ BRASS) stay as they are. Measured with
`tests/va_levels.c cz` (a 6-note chord at LEVEL 92, velocity 100, alone / with SUB BASS; the share of the hold under
the limiter, the post-master peak and RMS; trims in 0.5 dB steps):

| row | Casio | trim 0: alone / with SUB BASS | trimmed: alone / with SUB BASS | trim |
| --- | --- | --- | --- | --- |
| CZ BRASS 1 | A-1 BRASS 1 | 56.1 % -3.9 / -14.8 dB, 79.4 % -4.1 / -14.5 | 0 % -7.6 / -19.2, 0 % -6.2 / -17.2 | -10 (-5 dB) |
| CZ PIANO | D-1 PIANO 1 | 0 % -22.7 / -53.0, 0 % -16.3 / -21.4 | (as trim 0) | 0 |
| CZ STRINGS 2 | A-5 STRINGS 2 | 1.9 % -5.6 / -15.8, 27.5 % -4.7 / -15.0 | 0 % -8.2 / -18.7, 0 % -6.5 / -16.9 | -6 (-3 dB) |
| CZ PIPE | E-3 PIPE ORGAN 1 | 79.0 % -3.7 / -15.0, 95.2 % -3.7 / -14.7 | 0 % -7.7 / -20.1, 0 % -6.8 / -17.7 | -12 (-6 dB) |
| CZ VIBES | F-2 VIBRAPHONE | 0 % -18.7 / -46.8, 0 % -15.3 / -21.4 | (as trim 0) | 0 |
| CZ BASS (with TINE EP's chord) | B-5 SYNTH.BASS | 0 % -8.6 / -30.2 | (as trim 0) | 0 |

**0.19: no trims.** The engine is 6 dB lower ("Level" below) and `CB_TRIM`'s three CZ-1 rows are gone. The same
measure after it, trim 0 (alone / with SUB BASS): CZ BRASS 1 0 % -8.5 / -20.2 dB, 0 % -6.9 / -17.8; CZ PIANO 0 %
-28.7 / -59.0, 0 % -17.5 / -21.4; CZ STRINGS 2 0 % -11.0 / -21.7, 0 % -8.6 / -18.6; CZ PIPE 0 % -7.7 / -20.1, 0 %
-6.9 / -17.8; CZ VIBES 0 % -24.7 / -52.8, 0 % -16.9 / -21.4; CZ BASS (TINE EP's chord) 0 % -8.4 / -30.4. No row
reaches the limiter; BRASS 1 is 1 dB and STRINGS 2 3 dB under what they were with their trims, PIPE ORGAN 1 where it
was. (`tests/va_levels.c cz`, through hostsim's master: Classic.)

The fifth chord row is VIBRAPHONE rather than BELLS: BELLS has no DCA sustain and rings ~21 s after every chord change
(the voices pile up), VIBRAPHONE decays as a mallet should. PIANO 1 decays too (its RMS over the 5.6 s hold is low).

## CPU and memory

`tools/emu/perf.sh` (g): a 6-note CZ STRINGS 2 chord + CZ BASS, held 3 s, a chord change, 3 s, released (device
estimate, avg / worst block of the 2.9 ms budget): **33 % / 43 %** (VA's (f): 30 % / 39 %; FM PAD (b): ~25 % / 30 %);
no block over 80 %. Under 60 %: CZ-1 keeps the shared 8 voices (no `poly` cap). `tests/va_levels.c cz`: ~34 % with
SUB BASS. The host cost (`tests/regress.c`, one part, 8 notes): 1757..2487 instructions a sample (JET ROAR the
heaviest; the VA's MORPH PAD 2697, FM6's DRAWBARS 1920). Since 0.18's render (below): 799..1264 (INIT TONE, JET ROAR), 44..57 %
less for every tone of eight sounding notes; `tests/cpu_baseline.txt` is rewritten (every other entry 32..44 lower: the
master and the mix; FM6's were stale by ~210 before, not by this).

`./build.sh` (against the same tree with `FELUCCA_CZ=0`): XIP 320348 -> 345472 B (55.1 -> 59.4 %: the 64 tones 9216 B
and the code); RAM 79512 -> 79640 B (80.9 -> 81.0 %); POOL 303160 -> 311300 B (88.1 -> 90.5 %, +8140: the tone store
2 x 2320, the bank cache 2332, the parts' tones 576, the SysEx buffers 296 + 295; `eng_state` does not grow). The blob
cap's +32 bytes in `cr_ui.c`'s picker snapshot are in both.

### The render's cost on the unit (0.18)

Measured on the unit (SysEx 78 PEAK `pk_part0_us`), an eight-note CZ-1 chord took 1699..1985 us of the 2902 us half:
with the 0.18 effects, late halves. Counted in `build/choralroot.dis` (instructions on the usual path over 205 a
microsecond, the unit's measured rate; jump-table bytes are not instructions), a voice of a two-line tone cost about
50 800 instructions a half (248 us): 265 a sample and 4 200 a block. It is now about 16 100 (79 us): 91 a sample and
1 100 a block. Eight voices: about 1 980 us -> 630 us. Every sample is the one it was (below). What changed, in
`cz_native.c`:

1. **The envelopes' definitions are built once per tone, not per voice and block.** `cz_native_defs` (six envelopes
   of eight points: a `memset`, a 64-bit multiply and a divide by 44100 a point, a divide by 96 for the DCA: about
   3 250 instructions a voice a block) wrote them onto the render's stack every time. The rates and targets are the
   tone's: they live in `engines.c cz_defs_[ENG_STATE_NPART]` (`cz_defs_t`, 412 B a part, 824 B of pool; not in
   `eng_state`, whose bytes another engine may have left when a part comes to CZ-1 without a clear) and are rebuilt
   when `eng_cz.c cz_pgen[part]` moved. Every writer of `cz_patch` bumps it after its write, as `fm6_pgen`:
   `cz_init`, `cz_tone_put` (a preset, a user slot, an engine change, INIT, the picker, the blob: the session's
   restore and the backup go through `blob_set`), `cz_dset` (the editor), `cz_bank.c cz_bank_poll` (BANK / PTCH),
   `cz_store.c cz_sx_import` (Casio SysEx). The DCA's key follow depends on the note: a voice's eight key-followed
   rates a line are in its own state (`cz_voice_t.krate`, in `eng_state`: free), built by `cz_native_krate` when the
   tone's generation or the voice's note is not the one they were built for (`kgen`, `knote`; `cz_note_on`'s `memset`
   leaves none). `cz_env_tick` takes the rates as an argument. 1+1' reads line 1's definitions for both lines
   instead of copying them.
2. **A line's samples run in a loop built for its wave and window.** `cz_native_wave` was a call a sample and line
   with a switch on the wave, a switch on the window and an interpolated cosine. Now each line renders its block on
   its own into a buffer on the stack (a line reads no other; ring modulation and the sum take both afterwards), a
   cycle at a time: `cz_native_line` picks, by the wave the cycle plays (a line has two, alternating) and the window,
   one of 43 loops (`cz_native_run`, built with the two as constants: 7 waves x 6 windows, and the silent wave), which
   runs until the phase wraps or the block ends. The carrier comes from `CZ_CARRIER[2048]` (`tools/gen_tables.py`:
   `(pd_cos((p << 5) + (p >> 6)) + 32767) >> 1` for the 2048 phases there are, in the same integers;
   `tests/cr_cz_test.c` compares every entry with the expression). Line 2 with NOISE, whose generator moves the
   increment every sample, keeps a sample's switches (`cz_native_noise`). A line's loop is 27..42 instructions a
   sample (34 for the plain cosine-phase waves) where the call and its loop were 110; the mix of the two lines 23.
   Flash: 10.3 KB (the table 4096 B, in flash as `SINE` is; `ram=True` in `gen_tables.py` would put it in RAM if
   the unit shows the flash cache in the way).
3. **The set-up does what the tone needs.** A line the tone does not play (LINE 1 or LINE 2 alone) still ticks its
   three envelopes: their state is what a LINE change under a held note finds, so skipping them would change that
   note; its two DCA curves, its velocity and key-follow scaling, its pitch and its phase function are no longer
   worked out. For a line that plays: the DCA curve (`cz_native_amp`) once when the envelope did not move in the
   block, the pivot's two divides only when the DCW depth changed (`cz_voice_t.pk`), no multiply by a velocity factor
   of 1, no divide by a key follow of 0, the ramp's divide by the block as the shift it is.

Not done: skipping a line whose DCA has ended. Ring modulation multiplies the lines and the sum halves them, so an
ended line still shapes the output through its DC filter's tail (`dc += raw - (dc >> 10)` runs on with the wave at
full scale: the DCA only scales after it); it is not silent sample for sample until that tail is, and the phase and
the cycle's toggle would have to be advanced in step for a later note on the same voice (`voice_was`).

A patch write from the main loop that the audio ISR interrupts was always audible for a block as a half-written tone;
it still is, with the definitions of the tone before it until the generation moves at the end of the write.

**Bit for bit.** `tests/regress.c`'s goldens of INIT TONE and the 64 (unchanged), the emulator's 52 WAVs
(`test_cr.sh`, `test_persist.sh`, `perf.sh` with and without Dual Core: the same SHA-256 before and after), and a
comparison of the two source trees built side by side: every tone at three velocities and eight notes with the
pitch, fine tune, cutoff, shape and amplitude moving every block, held and released; 600 random tones (LINE, octave,
both waves of both lines, the window, ring and noise, key follow, velocity amounts, detune, the vibrato's wave); the
whole mix with CZ-1 on both parts while every column of the 37 pages is turned under held notes, LINE and MOD are
changed under them, other tones and blobs are loaded, with a bend and TUNE: the same hash of every output sample,
of `cz_native_done` and of the voices' final state.

## Pops (0.19)

The owner (HANDOFF, "For 0.19" (b)): "CZ-1 can pop and dropout when holding chords without FX ... probably internal clipping." Two
causes were found in the host render. Neither is clipping: nothing in the voice, the part's sum or the mix wraps or
saturates. Both are steps the engine made in the sample: (A) a note struck again on a voice that still sounds, (B) the
DCW depth of a control block applied at the block's edge. A third cause, a half rendered late on the unit, cannot
show in a host render at all and is not ruled out: `tools/cz_pop_check.py` below.

### How they were looked for

`tests/cz_pops.c` (a tool, not a suite; `tools/cz_pops_report.py` sorts its lines): one factory sound on part 0 at
LEVEL 92 with its trim, the sends at 0, ChoralRoot's mix (`fx_smooth`, `voice_fade_steal`, MASTER 2047), with and
without the CZ-1 bass (SYNTH.BASS, MONO) on part 1. INIT TONE and the 64 (banks E..H are empty until the user fills
them: no other tone exists by default) x six scenarios x four chords (4, 5, 6 notes close, 6 notes over two octaves) x
four roots (C2, C4, C6, C7) x velocity 100 / 127 x bass off / on = **24 960 runs** of 3.6 to 5.2 s. The scenarios:
`hold` (3 s, released), `retrig` (the chord let go and struck again in one block, eight times), `retrig-gap` (struck
again 100 ms after its release), `change` (eight chords with common tones, changed in one block), `steal` (12 notes
40 ms apart, then three chords over the ringing tails), `arp` (the chord's notes up and down every 75 ms, each twice).
Looked at, in the dry mix before the master (`mix_l` / `mix_r`: linear) and in the output:

- **wraps**: the tool is built with `-fsanitize=signed-integer-overflow`; a wrap anywhere in the voice, the part or
  the mix stops the run. The peaks of every stage are recorded through a host-only hook at the end of
  `cz_native_render` (`CZ_PROBE`, not compiled into the firmware).
- **gain steps**: a line's gain (its DCA curve x the voice's ramp, Q15) at the start of a voice's block against the
  end of its block before. The engine ramps every gain across a block, so a difference is a step it made. Counted
  above 5 % of full scale.
- **jumps**: a sample-to-sample step of the dry mix above -35 dB that is at least four times the largest step the
  signal makes in the 600 samples around it (1.25 periods of the lowest note if longer): a saw's edge is not a pop.
- **holes**: 32 samples of zeros between sounding windows. (**Dips**, a fall of 12 dB that is back within 60 ms, are
  printed too but say little: six detuned notes beat.)

`tools/emu/wavclicks.py` (the third difference in 32-frame windows against its neighbourhood) was left as it is: it
finds the same places in the emulator's WAVs (below) but also reads a bright attack as a burst, so it cannot say what
a click is. The gain step can.

### What was found (main, 19b6453)

| | hold | retrig | retrig-gap | change | steal | arp |
| --- | --- | --- | --- | --- | --- | --- |
| runs | 4160 | 4160 | 4160 | 4160 | 4160 | 4160 |
| with a wrap | 0 | 0 | 0 | 0 | 0 | 0 |
| with a gain step > 5 % (tones) | 0 | 4000 (65) | 2056 (42) | 3629 (65) | 3882 (65) | 3148 (52) |
| with a jump in the dry mix | 165 | 758 | 434 | 624 | 442 | 891 |
| with a hole | 0 | 108 | 43 | 0 | 0 | 104 |

Peaks over all runs: a line 29 817 (Q15), a voice's two lines 51 950 (ring modulation: `mulq15(l0, l1) * 2`), a voice
into the part 152 188 (6.3 x `VOICE_FS`), the part after LEVEL 114 205 (the mix loop clamps at 524 287), the dry mix
149 467 (4.6 x full scale at MASTER's top: the master's limiter worked in 59 % of the runs, down to a gain of
0.29). By the code the bounds hold for any tone: a line is `((raw - dc) >> 1) * amp` with |raw - dc| <=
65 534 and amp <= 31 079, the ring product 31 079^2 * 2 / 32 768 = 58 954, and `voice_amp`'s 58 954 x 32 767 is 1.93e9,
under 2^31; eight such voices are 1.4 M, which the mix loop clamps to 884 000 before its multiply.

**(A) A note struck again cut its own voice (all 65 tones).** `cz_note_on` cleared the voice's state, so the six
envelopes started at level 0. For a free voice that is right. For a voice that still sounds (`voice_was`: the same
note again, MONO, the arp, a chord change's common tone: `voice_alloc` gives the note its own voice back, and
`voice_start` keeps its phases because "resetting them clicks") the DCA's gain fell from where it was to 0 between
two samples and the DCW's depth with it: a step of the voice's whole sample, then silence, then the attack again
(the "pop and drop-out"). The worst: INIT TONE, a gain of 31 078 of 32 767 to 0; in the mix a step of 42 485 on
STRINGS 1 (51 % of full scale at the output). Every chord change on the keys that shares a note with the chord
before it does this, and every re-strike of a sustained chord.

**(B) The DCW depth stepped at a block's edge (`hold`'s 165 runs: 164 at C2, 160 of them the CZ-1 bass under the
chord; every tone when struck again).** The
depth (`pd.dcw`, so the pivot and the two slopes of the phase function) was the envelope's value once per 32-sample
block. A DCW attack at the fastest rate moves it 218 of 1023 a block; at a block's edge the phase function changed
under the phase, and with it the sample. High notes hide it (a cycle is shorter than a block and has such slopes of
its own); a low note sits on one part of its cycle for many samples and steps: SYNTH.BASS at C2 steps 14 000 of the
dry mix 160 samples after every note-on (43 % of full scale at MASTER's top), HONKY-TONK's C2 chord 22 240. This is
the CZ-1 bass's click under a held chord.

Nothing else: no jump further than 1000 samples from a note-on in `hold`, no step at a voice's end (the DCA's last
block ramps to 0 before `cz_native_done`), none when a voice is given up (`voice_kill`: one block's fade, as every
engine).

### The fixes

- **(A)** `eng_cz.c cz_note_on`: a voice that sounds keeps its six envelopes' levels and its lines' last DCW pivot
  (`cz_voice_t.pk`); stage, gate, vibrato, noise and the key-followed rates restart as before. The envelopes go from
  where they are to step 1's level at step 1's rate. A free voice starts as it did (bit for bit).
- **(B)** `cz_native.c`: `cz_native_pd` gives a block the depth its line's last block ended at (`1024 - pk[0]`) and
  the step a sample to this block's (`dq`, `dstep`: Q16, exactly there after 32). A block whose depth did not change
  (a held note at its DCW sustain: the usual one) has `dstep` 0 and runs `cz_native_line`'s 43 loops as before, the
  same code. A block whose depth changed runs `cz_native_line_mv`: the same 43 loops with the depth of each sample,
  its pivot and its two slopes read from `CZ_RECIP[2048]` (`(1024 << 16) / x`, `tools/gen_tables.py`: the two divides
  `cz_native_pd` does once a block). `cz_native_noise` does the same in its one loop. The chip steps its envelopes at
  its sample rate (`cz_hw_rate`'s 40 kHz), so this is nearer to it than a block's step was.

After, the same matrix: **0 gain steps, 0 holes, 0 wraps, 0 jumps in `hold`**; 41 runs of 24 960 keep a jump, all at
C2 on AFRO PERC (19), SYN DRUMS 2 (13), SYNTH.BASS (4), CELLO (3), CONGA (2): two to six samples of a pulse's own
edge about 156 samples after a note-on, while the depth runs through its range at the fastest rate. They are in
main's renders too and are the tone, not a step of a block (then an inference from their place inside blocks and
their width; verified against the waves' definitions in the second round, below).

What changed in the sound. (A): a note struck again while it sounds no longer falls silent and attacks from nothing;
its level, its timbre and its pitch envelope move from where they are through the tone's steps. A sustained chord
struck again is 0.2 to 0.5 dB louder over the half second around the strike (the hole is gone), and a tone whose
first DCA step is its sustain level hardly re-attacks. This is a change of behaviour, not only the pop's removal;
a CZ's own envelopes are said to behave so (not checked on one). (B): the first few blocks of an attack, and any
block in which the DCW moves, differ in the samples where the depth used to be a block's and is now a sample's; level
and timbre are the same (the excerpts' RMS within 0.1 dB), the click is not there.

`../audio-0.19/cz-pops/` has the ten worst (before, a gap, after, in one WAV each) and `README.txt` with the sample
index and the size of each.

### Cost on the unit

Counted in `build/choralroot.dis` against a build of 19b6453, at 205 instructions a microsecond; a voice of a two-line
tone was about 16 100 instructions a half (79 us).

| | before | after |
| --- | --- | --- |
| `cz_native_line`, the 43 loops | 16 .. 42 a sample (median 35) | the same code (the listing differs in the carrier table's address only) |
| `cz_native_pd`, a block whose depth did not change | 21 | 33 |
| `cz_native_render`, a line's choice of loop and `n` to `cz_native_pd` | | about + 5 |
| **a voice a half, the depth at rest** | 16 100 (79 us) | + 8 x 17 = 136: **16 240 (79.2 us), + 0.8 %** |
| `cz_native_line_mv`, the 43 loops | | 16 .. 47 a sample (median 39): + 4.9 a sample on average |
| **a voice a half, both lines' depth moving in all four blocks** | 16 100 | + 8 x (157 + 23) = 1440: **17 540 (85.6 us), + 9 %** |
| `cz_note_on` | 59 instructions | 105 (once a note) |
| `cz_native_noise`'s loop (line 2 with NOISE) | 148 in the listing | 175 (one test a sample at rest) |

Eight voices: about 630 us -> 634 us at rest, 685 us with every depth moving. No divide was added to a loop. Flash:
XIP 458 228 -> 474 068 B (79.9 -> 82.7 %: the second set of loops 7.3 KB, the table 8.2 KB); RAM and POOL unchanged
(`cz_hw_pd_t` is on the render's stack). `tests/target_budget.txt`: `cz_native_render` 338 -> 360,
`cz_native_noise` 134 -> 161, `cz_native_line_mv` added (7333); `cz_native_line` 45 287 as before. The host
(`tests/regress.c`): JET ROAR 1264 -> 1288 instructions a sample. `tools/emu/perf.sh` (g): 142 789 / 228 163 host
instructions a block (avg / max) -> 143 787 / 235 538 (+0.7 % / +3.2 %; a second run of the same build: 143 749 /
238 881, the worst block moves that much between runs); its `wavclicks` line 5 clicks -> 4 (the
x440 burst at the chord's start is x59, the one at the chord change is gone; the three left are the bass's attack,
the same in both builds).

### What stayed bit for bit, and what did not

- Every other engine, the mix and the master: all 161 scripts of `tools/emu/scripts/` rendered with `--wav` through
  an emulator built from `git archive 19b6453` and through this tree's: **158 identical WAVs**. The three that differ
  (`cr_cz`, `cr_select`, `perf_g`) are the three that play CZ-1 notes.
- `tests/regress.c`: 132 of 197 goldens unchanged; **all 65 CZ-1 goldens changed** (their renders have a DCW attack,
  and 60 of them strike a sounding note again) and were rewritten. With both fixes switched off in a copy of this
  tree (`dstep` 0, the levels not kept) all 197 are main's: the two loops' set-up, the table and the hook change no
  sample.
- So a CZ-1 render is **not** bit-exact wherever its DCW depth moves, which is the condition that popped. A block
  whose depth is at rest runs the code it ran; its samples can still differ from main's by the DC filter's state
  (`dc`), which carries the attack's history and comes back to within one step of it.

### The second round (2026-10-12): the click that was left, the jumps at C2

**What was asked.** The output chain's round saw "CZ-1 BRASS 1 with the bass, struck again while it sounds: 2 clicks
to `wavclicks.py` in Clean, the same in f402608's, absent in Classic", and the first round left 41 runs with a jump
at C2 "believed to be the tones' own edges". Both were followed to the sample.

**The matrix grew** to eight scenarios, 33 280 runs: `restrike` (the emulator script's: held 1.2 s, struck again
20 ms after its release three times, two changes; `CZ_GAP=MS` sets the release) and `arp-up` (the unit's Arpeggiate
as it comes: up, sixteenths at 120 BPM, ten passes, each note struck again over its own tail). New in a run's line:
`click` (wavclicks' measure made two-sided, below), `half` (the hash of "Level"). `CZ_TRACE=A,B` prints every voice's
block between two samples, `CZ_WHY=J` what every line did at sample J against its wave's definition. The thresholds
follow the engine's scale, so the counts before and after the trim are of the same steps.

**The two clicks are not a step.** Reproduced to the millisecond in the emulator (BRASS 1, the bass, MASTER at its
top, the chord let go for 60 ms and struck again: `wavclicks` 2 clicks at 2.3039 and 3.0643 s in a 778a6ad build;
without the bass the same two, so they are the chord's) and in the host (`CZ_GAP=60 cz_pops one 15 1 6 ..`). At the
re-strike every line's gain and DCW depth is continuous (`CZ_TRACE`: the DCA goes 43 -> 130 -> 392 -> 1179 -> 3542
-> 10 632 -> 28 683 of 32 767 over six blocks from where the 60 ms release left it; the depth goes on from 716 of
1023, where its slower release left it, to 958). The window wavclicks flags is the **first audible window of that
attack** (third difference 42, against a median of about 1 in the 30 quiet windows before it); the windows after it
carry the tone's own saw edges at 150 to 215, every cycle, for as long as the note is held. So: BRASS 1's DCA lets
go in 60 ms and its DCW does not, the note comes back with its wave already bright instead of opening (a fresh
strike starts the DCW at 0 and takes 40 ms to get there, which is why the first strike is not flagged), and a
measure that compares a window with the median of its neighbourhood reads the first bright one after a quiet stretch
as a click. Classic shows the same event at 10 times its neighbourhood instead of 24: under the threshold of 12, not
absent. It is what fix (A) of the first round does by design (the envelopes go on from their level), and it is what
the chip does: in Devin Acker's uPD933 model (MAME `upd933.cpp`, the source of `cz_native.c`) a write of an
envelope's rate and target never touches its current level, which slews from where it is; only a chip reset clears
it. (Read from the model's source; not compared with a CZ-1.) Nothing was changed for it. **If the bright re-attack
is not wanted**, the place is `cz_note_on`: start the DCW and pitch envelopes from 0 when the line's DCA is silent
(a voice that makes no sound has no wave to step); a decision of taste, not built.

**A real step was found on the way: a sounding note struck again at another velocity.** The matrix strikes every
note of a run at one velocity, so it could not see it. The DCA curve of a level is scaled by the velocity
(`cz_native_amp`), and a voice that keeps its level over a note-on got the new velocity's gain on the block's first
sample: a step of the voice's whole sample. `CZ_VEL2=30 cz_pops one 15 4 1 2 60 127 0` (STRINGS 1, the chord struck
again every 300 ms at 127 and 30 in turn): a gain step of 9496 of 32 767, a jump of 5236 in the dry mix against
1069 nearby; 43 of the 65 tones step more than 5 % of full scale. On the unit the keys have one velocity, so it
comes from a loop's step velocities, MIDI in, Pattern's accents, MONO's legato raising a voice's velocity, or a
LEVEL / V.AMP column turned under a held note (the same arithmetic). **Fixed at the cause**: a line's gain at the
end of a block is kept (`cz_voice_t.ag`) and is where its next block starts; `cz_note_on` keeps it on a sounding
voice as it keeps the levels. The gain then moves to the new velocity's across one block (0.7 ms: what every other
gain of the engine does) and on through the envelope's steps. With the velocity and the tone's level bytes unchanged
the kept gain is the value the curve gave before, so nothing else moves: the same `half` for all 33 280 runs before
and after, all 167 emulator WAVs the same. After: 0 of the 65 with a gain step, STRINGS 1's jump gone.

**The jumps at C2 are the waves' own edges: verified.** `tools/cz_pops_why.py` renders every run with a jump again
with `CZ_WHY` at the jump's sample and sets, for the line with the largest step, the step its wave's definition
gives between the two samples' phase positions (`cz_native_wave` alone, no state, at that sample's depth and gain)
beside the step the line made. 79 runs of 33 280 have a jump (the first round's 41 in its six scenarios, 37 in the
two new ones, one more of BRASS 1 now that it has no trim); **in all 79 the definition gives the step** (within
15 %; e.g. AFRO PERC: the definition's -14 676, the line's -14 375):

| tone | runs | wave | DCW depth of 1023 | phase positions a sample | samples after the note-on | the step: definition / line |
| --- | --- | --- | --- | --- | --- | --- |
| AFRO PERC | 35 | 5 (sine pulse) | 812 .. 943 | 28 .. 42 | 147 .. 165 | 7240 .. 17 106 / 7241 .. 16 984 |
| SYN DRUMS 2 | 24 | 0 (saw), 5 | 208 .. 943 | 2 .. 35 | 148 .. 267 | 83 .. 13 526 / 77 .. 13 455 |
| SYNTH.BASS | 8 | 1 (square) | 1010 .. 1013 | 2 | 150 .. 277 | 6157 .. 6594 / 6137 .. 6568 |
| CONGA | 6 | 1 | 985 .. 997 | 4 .. 5 | 156 .. 159 | 3726 .. 4880 / 3715 .. 4877 |
| CELLO | 3 | 4 (double sine) | 996 | 8 | 164 | 8899 .. 9294 / 8902 .. 9298 |
| MALLET LOG | 2 | 1 | 1013 | 2 | 165 | 6877 .. 7402 / 6871 .. 7396 |
| BRASS 1 | 1 | 5 | 1014 | 3 | 61 | 3980 / 3934 |

They are phase functions squeezed by the depth: wave 5 at depth 936 runs its second half-cosine through 88 of the
2048 phase positions, which a voice moving 33 positions a sample crosses in three samples; the square at depth 1013
has 11 positions for its edge. They fall about 150 samples after a note-on because that is where a DCW attack at
the fastest rate reaches its top (218 of 1023 a block), and they count as jumps because nothing that steep is
within 600 samples of them yet. Six of them fall on a block's first sample: the definition gives those too (one
block in 32 would by chance). The chip's 11-bit phase functions make the same edges; not compared with a CZ-1. Left
as they are.

**The matrix, before (778a6ad) and after both changes**, the same tool and thresholds:

| | hold | retrig | retrig-gap | change | steal | arp | restrike | arp-up |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| runs | 4160 | 4160 | 4160 | 4160 | 4160 | 4160 | 4160 | 4160 |
| with a wrap, before / after | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| with a gain step > 5 % | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| with a jump (all the waves' own) | 0 / 0 | 4 / 4 | 0 / 0 | 4 / 4 | 8 / 8 | 25 / 26 | 6 / 6 | 31 / 31 |
| with a hole | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 | 0 / 0 |
| with a dip (beating; says little) | 255 / 241 | 428 / 427 | 803 / 751 | 329 / 329 | 472 / 447 | 1125 / 1099 | 1137 / 1115 | 418 / 405 |
| with a two-sided click | 45 / 44 | 455 / 447 | 434 / 431 | 415 / 405 | 204 / 205 | 383 / 384 | 432 / 420 | 300 / 300 |

(The one jump more in `arp` is BRASS 1 without its trim: 5 dB louder against the scaled floor.) The gain step row is
0 by construction since the fix (a block starts at the gain the last one ended at) and was 0 before at one velocity.
**The two-sided click** (`clicks()` in `tests/cz_pops.c`: 12 times above the median of the windows before it and of
those after it, each on its own, and 3 times above the largest window within 1.25 periods of the lowest note) still
fires in 2636 runs (2668 before): attacks that carry a burst of their own (AFRO PERC 213, CONGA
191, KOTO 160, SITAR 153, HARPSICHORD 102, RECORDER 78: its line 2's pitch envelope jumps 27 semitones and back in
four blocks, the chiff; 44 of them in `hold`, where nothing is struck again). It says where to look, not what is
there; wavclicks' own one-sided measure on the output fires in nearly half of all runs (16 011 before, 15 335 after).

Stage peaks over all runs, before -> after: a line 29 601 -> 29 601 (Q15), a voice's two lines 53 480 -> 53 480, a
voice into the part 156 672 -> 78 336 (exactly half), the part after LEVEL 116 543 -> 58 271, the dry mix 151 577 ->
75 787 (4.6 -> 2.3 x full scale at MASTER's top), the output 30 550 -> 24 084. **The master limiter** (hostsim's
master is Classic, 0.18's, as in the first round) worked in 54.9 % of the runs before (59.8 % of the first round's
six scenarios, where it was 59 %) and in **6.3 %** after (6.6 %), down to a gain of 0.28 before and 0.55 after.
Through Options > Output Clean (`CZ_CLEAN=1`, the same 33 280 runs): its limiter works in 56.8 % of the runs before and
**7.9 %** after, down to 0.26 and 0.52.

**Cost on the unit**, counted in `build/choralroot.dis` against a build of 778a6ad at 205 instructions a
microsecond. The trim: nothing (every CZ-1 function has the instruction count it had: `cz_native_render` 494; the
multiply's constant is another). The fix: `cz_native_render` 494 -> 517 instructions; on the usual path (the DCA at
rest, a kept gain) 9 instructions more a line and block (the kept gain's load, test and store), 72 a voice a half:
16 240 -> 16 312 (79.2 -> 79.6 us, +0.4 %; eight voices +2.8 us); in a block whose DCA moves one `cz_native_amp`
fewer (30 instructions and its two divides), about 28 less a line and block. `cz_note_on` 105 -> 115, once a note.
No instruction was added to a sample's loop. Flash: XIP 480 832 -> 480 720 B (the trims' rows went, the kept gain
came); RAM and POOL unchanged (`cz_voice_t` 156 -> 164 B inside `eng_state`, which FM6 sizes).
`tests/target_budget.txt`: `cz_native_render` 360 -> 382 (the static measure).

**What stayed bit for bit.** Against an emulator built from 778a6ad, all 166 scripts with `--wav`, in Clean and
held at Classic: 161 identical WAVs; the five that differ (`cr_cz`, `cr_cz_pop`, `cr_output_loud`, `cr_select`,
`perf_g`) are the five that play CZ-1 notes. `tests/regress.c`: the 132 goldens of the other engines unchanged; all
65 CZ-1 goldens rewritten for the trim, 50 of them again for the fix (its phrase strikes two sounding notes again
at another velocity, 80 then 105 and 120 then 110; with those two at their first velocities all 197 are the
trim's).

**Not tested**: nothing on the unit (no flash, no MIDI); no CZ-1 beside it; the velocity step through a loop or
MIDI in the emulator (its scripts have one velocity: the host test covers it); how the bright re-attack sounds to
the owner (`../audio-0.19/cz-trim/brass1-bass-restrike-60ms.wav`).

### Level

As measured in the first round (778a6ad): the same held 6-note chord (D4 F#4 A4 B4 C#5 E5, velocity 100, LEVEL 92,
the sends at 0, `CB_TRIM`'s three trims in),
`build/host/cz_pops level`: the part's peak after its level and the dry mix's RMS over 0.3 .. 2.9 s, 0 dB = 32 768
(full scale at MASTER's top). Sustained sounds only (RMS above -30 dB):

| engine | sounds | peak, median (range) | RMS, median (range) | velocity 127: peak / RMS |
| --- | --- | --- | --- | --- |
| FM6 (TINE EP, PAD, ORGAN) | 3 | -4.7 dB (-11.4 .. -1.8) | -20.4 dB (-21.5 .. -19.7) | -4.7 / -20.4 |
| VA (24 of 25) | 24 | -3.0 dB (-6.0 .. +3.3) | -15.6 dB (-29.4 .. -9.4) | -1.7 / -14.7 |
| FM TONE (13 of 16) | 13 | -3.8 dB (-10.8 .. +3.2) | -16.8 dB (-29.9 .. -5.3) | -2.1 / -16.5 |
| **CZ-1 (36 of 65)** | 36 | **+2.4 dB** (-5.4 .. +8.1) | **-12.3 dB** (-27.9 .. -2.4) | +3.0 / -11.8 |

CZ-1 is the outlier: 5.4 dB above the VA at the peak and 3.3 dB in RMS, 6.2 / 4.5 dB above FM TONE, 7.1 / 8.1 dB above
the FM6 rows; 40 of its 65 sounds peak above full scale into the mix (VA 3 of 25, FM TONE 2 of 16), and that is with
BRASS 1, STRINGS 2 and PIPE ORGAN 1 already trimmed by 5, 3 and 6 dB (+0.6, -0.5, -1.1 dB peak with their trims). The
loudest: INIT TONE +8.1 / -2.4, STRINGS 1 +4.7 / -6.2, PIPE ORGAN 2 +5.0 / -7.6, ORCHESTRA +5.7 / -8.1, STRINGS 4
+6.4 / -8.2. The reason was in the last lines of `cz_native_render`: a line is half scale (`(raw - dc) >> 1`) and
`dsp.c` says such an engine adds its voice `<< 1`; CZ-1 added it `* 4`, and a ring-modulated voice twice that again
at its peaks.

**0.19: the engine is 6 dB lower.** `cz_native.c CZ_VOICE_GAIN` 2: the three final sums of `cz_native_render` add
`voice_amp(..) * 2`. The factor is the last thing done to a voice's sample, so nothing upstream moves: a line, the
ring product (`mulq15(l0, l1) * 2`), NOISE, the DC filter, `voice_amp`'s ramp and `VOICE_FS`, the velocity and level
products are the integers they were, and every sample a voice adds to its part is **exactly half** of what it was
(`out_old == 2 * out_new`: no rounding, the old sum was a multiple of 4). Proven on the matrix below: a hash of every
sample a CZ-1 voice leaves in its part's sum (`tests/cz_pops.c cz_probe_out`; the earlier tree hashes each sample's
half and counts the samples without one) is the same for all 33 280 runs between a build of 778a6ad and this tree, no
sample without an exact half (`tools/cz_pops_report.py --halves`); and in `tests/cr_cz_test.c`, where twice every
sample of the part's render of INIT TONE and the 64 hashes to what 778a6ad's sample did. After the part's sum the
mix is not linear to the bit (`(b >> 2) * lvl >> 10`, the limiter): there the change is -6.0 .. -6.1 dB.

`CB_TRIM`'s rows for BRASS 1 (-5 dB), STRINGS 2 (-3 dB) and PIPE ORGAN 1 (-6 dB) are removed ("The bank rows" above:
0 % under the limiter without them). The same table after both, the same 36 sounds:

| engine | peak, median (range) | RMS, median (range) | velocity 127: peak / RMS | peak over full scale |
| --- | --- | --- | --- | --- |
| FM6, VA, FM TONE | as above (not a sample changed) | | | 0 of 5, 3 of 25, 2 of 16 |
| CZ-1 before (the three trims in) | +2.4 dB (-5.4 .. +8.1) | -12.3 dB (-27.9 .. -2.4) | +3.0 / -11.8 | 40 of 65 |
| **CZ-1 after (no trim)** | **-3.5 dB** (-11.4 .. +2.1) | **-18.0 dB** (-33.9 .. -8.4) | -2.8 / -17.3 | 3 of 65 |

CZ-1 now sits between the VA (-3.0 / -15.6) and FM TONE (-3.8 / -16.8), above the FM6 rows (-4.7 / -20.4). The
sounds that had no trim moved by -6.0 .. -6.1 dB in RMS and peak (the 54 that sound through the window); BRASS 1 by -1.0, STRINGS 2 by -3.0, PIPE ORGAN 1
by 0.0. Sounds more than 4 dB (RMS) from the engine's median of -18.0 dB, for the owner's ear (**no trim applied**):

| sound | RMS (from the median) | peak | a trim that would bring it within 3 dB |
| --- | --- | --- | --- |
| INIT TONE | -8.4 (+9.6) | +2.1 | -6.5 dB (-13 steps): one bare cosine a line, nothing to mask it |
| STRINGS 1 | -12.2 (+5.8) | -1.3 | -3 dB (-6) |
| BRASS 1 | -12.7 (+5.3) | -0.4 | -2.5 dB (-5): half of the trim it had |
| PIPE ORGAN 2 | -13.6 (+4.4) | -1.0 | -1.5 dB (-3) |
| (PIPE ORGAN 1, MOTORCYCLE | -14.0 (+4.0) | -1.1, -1.6 | at the line: none) |
| HARMONICA, ELEC.BASS 2, SYNTH.LEAD 2 | -22.1, -23.2, -25.9 (-4.1, -5.2, -7.9) | -2.2, -2.3, -0.6 | none: their peaks are already above the median's (narrow pulses) |
| SWEEP 2, VOICE 2, JET ROAR, ACCORDION | -22.9, -23.3, -25.1, -27.4 (-4.9 .. -9.4) | -9.6, -5.4, -10.9, -9.6 | +2, +2, +4, +6 dB (+4, +4, +8, +12 steps; `track_t.trim` is signed) |
| ELEC.PIANO, BELLS, SITAR | -30.6, -31.1, -33.9 | -6.2, -3.8, -11.4 | none: they decay over the measured 2.6 s |

What the lower scale changes besides the level, by the code: a user's CZ-1 sound is 6 dB quieter with every other
(intended: the engine is levelled, LEVEL is the part's and is not touched; the 144-byte tone and the eight banks
hold tone data only, no level that assumed the old scale); a user record bound to one of the three factory sounds
loses the trim with it (`cu_pool_load` reads `cb_trim`); the sends, the USB part pair and the part's meter follow the
part (6 dB lower); DIST on a CZ-1 part is driven 6 dB less (`track_dist` works on the part's sum before LEVEL); the
part's sum now clamps at 884 000 only above 11 voices' worth (it never did in a run). **The session** stored
`track_t.trim` and `css_part_put` wrote it back, so a unit last saved with BRASS 1, STRINGS 2 or PIPE ORGAN 1 on a
part would have come up with the old trim under the new scale (5, 3 or 6 dB too quiet) until the sound was picked
again. Since 2026-10-12 it does not: a part put back from the session takes its trim from `cb_trim` by the record's
pool entry, as `cu_pool_load` does (`cr_settings.h cr_session_part_trim`; docs/SETTINGS.md "The session", the
`part[2]` row), at power-on and at GLO + SAVE.

Listening files: `../audio-0.19/cz-trim/` (the same chord on FM6, VA, FM TONE and CZ-1 before and after, through
Output Clean at the power-on MASTER; `README.txt`).

### The unit: `tools/cz_pop_check.py`

A half rendered late (docs/FX.md "Late is not slow") is the other possible cause of what the owner heard, and the
unit's counters were never read after a CZ-1 session. `python3 tools/cz_pop_check.py` prints its plan and checks
that `fm1_install.py --info` reports a ChoralRoot (it refuses anything else); `--read` then reads SysEx 78 PEAK
and judges, for a session played by hand; `--play` selects CZ-1 STRINGS 2 and SYNTH.BASS, clears the counters,
plays 45 s over MIDI (eight notes held with the bass; six-note chords with common tones every 400 ms; eight notes in
turn every 60 ms: MIDI Perform and MIDI Bass on channels 1 and 2), reads and judges. `stale_halves` above 0 is late
audio (audible, and timing: its stage in `pk_late_stage`); `pk_late` above 0 alone is a late half the DAC did not
reach; `pk_part0_us` was predicted at about 630 us for eight voices (685 with every depth moving): above 1450 it is
far from the count, above 2000 a late half is near; all clean, and a pop heard in that stretch was computed.

**The count against the unit: 630 counted, 1066 read** (docs/HANDOFF.md item 21). A likely cause since 3y41, **a
hypothesis until it is measured**: the count is instructions at 205 a microsecond, and code that executes from the
flash stalls when the code cache misses. The P5 engine's sample loop lost a quarter of its part's time when it moved
to RAM with not an instruction changed (docs/FX.md "Code in RAM"). CZ-1's render is 16.4 KB of loops, one of 86
chosen for each line and block, and two tables in flash (`CZ_CARRIER` 4096 B every sample; `CZ_RECIP` 8192 B in the
`_mv` loops). `CR_RAM_CZ=1 ./build.sh` links the eleven functions of the render at RAM (16 390 B: it fits beside P5's
loop and nothing else); package E of the measurement plan there is that build. Read `pk_part0_us` with the same
eight-voice playing on A and on E.

## Tests

- `tests/cr_cz_test.c` (in `tests/run_cr_tests.sh`): the engine's number and place; Casio's 64 (valid, names, BANK /
  PTCH); every tone through the blob and back, a bad blob; every tone rendered (3 notes, velocity 127 / 1) within range
  and not silent; SysEx: a 144-byte dump in, the send request's dump out identical, a 128-byte dump, an invalid tone,
  no CZ-1 part; the banks (A..D Casio's, E..H empty, BANK / PTCH, a bank restored as backup object 18 and reloaded);
  the 37 pages (titles, sections, every column's min / max / default, the panel value it is, one byte for one R, the
  SUS / END rules, Casio's encodings); the tone store (save, load, reboot, the second object, clear, erase, a slot of no
  tone); the backup's objects 12..21.
  0.19: `CZ_RECIP` against its divide; the pops (a sounding note struck again steps no more than the held note and
  keeps its level; INIT TONE and the 64 let go and struck again at once and 60 ms later: no step at the new note's
  first sample; the 65 at C2: no step at a block's edge above twice the largest inside the blocks; `cz_native_pd`'s
  ramp). On main the three checks of the sound fail: 0 of the level kept, 62 of 130 re-strikes step (the worst
  34 552), 11 tones step at a block's edge (the worst 18 024).
  0.19, the second round: the level (twice every sample of the part's render of INIT TONE and the 64 is 778a6ad's
  sample: all 65, and by kind: one line 3, two lines 33, ring modulation 24, NOISE 5); the 65 struck again at another
  velocity while they sound (127 -> 30, 30 -> 127: no step at the new note's first sample or into the block after;
  on 778a6ad 25 of 130 step, the worst 11 296); a line's kept gain across a note-on.
- `tests/cz_pops.c`, `tools/cz_pops_report.py`, `tools/cz_pops_why.py`: the matrix of "Pops" (a tool: `cc` line at its
  top), its report (`--jumps`, `--halves`), every jump against its wave's definition.
- `tests/cr_backup_test.c`: 27 objects (12, 13, 14..21 listed; a saved bank and the store round trip).
- `tests/regress.c`: goldens and CPU baselines for INIT TONE and the 64 (additions only; nothing else changed).
- `tests/target_budget.txt`: `cz_native_render`, `cz_native_wave`, `cz_env_tick` added; the audio ISR's static count
  is 43986 (was 37094: 1880 -> 1882 instructions, the register allocation moved a loop head; its code is the same).
  0.18: `cz_native_wave` is inlined; `cz_native_line` (its 43 loops; the static measure weighs a loop in a loop four
  times: 45287) and `cz_native_noise` are watched instead. 0.19: `cz_native_line_mv` too ("Pops": Cost on the unit).
- `tools/emu/scripts/cr_cz.txt` (`test_cr.sh`): CZ BRASS 1, the editor's ENV screen 4 (DCW 1) under the cz band (shot),
  KNOB 1 on DCW 1's L1 (the `deep:` trace, step 1 lit), the chord darker after it (its HF share), the other groups;
  `cz_persist_*.txt` (`test_persist.sh`): an edited CZ-1 sound in U01 across a relaunch; `perf_g.txt` (`perf.sh`).
  0.19: `cr_cz_pop.txt` (`test_cr.sh`): STRINGS 2 with the CZ-1 bass, the chord struck again three times 20 ms after
  its release and changed twice in one frame; `wavclicks.py` from 3.4 s: 0 jumps, 0 holes, 0 clicks (main: 3 clicks,
  at the first re-strike and the first change). `cr_cz_brass.txt` (`test_cr.sh`): BRASS 1 with the CZ-1 bass in
  Output Clean, struck again 60 ms and 20 ms after its release (twice each), then the unit's Arpeggiate for 3 s;
  `wavclicks.py --both-sides` over the re-strikes: 0 jumps, 0 holes, 0 clicks (its one-sided measure: 2, the two
  60 ms re-attacks, in 778a6ad as now: "The second round"); the arp: no jump, no hole. This check passes on 778a6ad
  too: it guards, it does not show the velocity fix (the emulator's scripts strike at one velocity).
- `web/test_backup.mjs`: ids 12..21, their names.
