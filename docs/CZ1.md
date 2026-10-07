# ChoralRoot FM-1 — CZ-1

CZ-1 is ChoralRoot's Casio CZ-1 engine (**engine 14**, `ENGI_CZ`). It is **Melodee's** CZ-1 (Kerem Kilic's fork of
Felucca, <https://github.com/keremimo/melodee>, GPL-3.0): native Casio tones (the 144-byte CZ-1 tone kept verbatim),
two lines with eight-step pitch / timbre (DCW) / volume (DCA) envelopes, the uPD933 phase functions and logarithmic
amplitude, Casio's 64 preset tones, eight 16-tone banks and Casio's tone SysEx over USB-MIDI. This is phase 2 of 3 of
the Melodee platform work (phase 1: FM6, docs/FM6.md; phase 3: USB audio).

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
`ENGINES[]` stays append-only, `ENGINE_ORDER` shows it **after PHASE** (the PRESETS browse, the engine picker's roots:
ANALOG, FM6, VA, PHASE, CZ-1, LOFI, ..). Its per-voice state (`cz_part_t`: six envelopes, the vibrato, the noise, 8
voices) is a member of `engines.c`'s `eng_state` union (smaller than FM6's: the union does not grow); the tone of each
part (`cz_patch[NTRK]`, 4 x 144 bytes) is in the pool.

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

0xA0000..0xB1FFF is the user sample slot 1's flash, unused since the all-synth change (`FELUCCA_SAMPLE` 0; storage.c's
map); 0xB2000..0xC7FFF stays free, 0xC8000.. holds the loops. SAFE MODE's Flash Data erase covers 0xA0000..0xB1FFF too
(`cr_ui.c` `cu_flash_erase`: 54 sectors).

## The banks (cz_bank.c)

Melodee's: eight banks of 16 tones (`cz_bank_t`, 2332 bytes: magic "CZCB", version 1, a 16-character name, the used
mask, 16 tones), one RAM cache (the pool) of the bank in use. A bank never saved is its default: BANK A..D Casio's 64
(A-1 .. B-8, C-1 .. D-8, E-1 .. F-8, G-1 .. H-8, named "CZ-1 A1-B8" ..), E..H empty. As in Melodee a bank comes in and
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

The fifth chord row is VIBRAPHONE rather than BELLS: BELLS has no DCA sustain and rings ~21 s after every chord change
(the voices pile up), VIBRAPHONE decays as a mallet should. PIANO 1 decays too (its RMS over the 5.6 s hold is low).

## CPU and memory

`tools/emu/perf.sh` (g): a 6-note CZ STRINGS 2 chord + CZ BASS, held 3 s, a chord change, 3 s, released (device
estimate, avg / worst block of the 2.9 ms budget): **33 % / 43 %** (VA's (f): 30 % / 39 %; FM PAD (b): ~25 % / 30 %);
no block over 80 %. Under 60 %: CZ-1 keeps the shared 8 voices (no `poly` cap). `tests/va_levels.c cz`: ~34 % with
SUB BASS. The host cost (`tests/regress.c`, one part, 8 notes): 1757..2487 instructions a sample (JET ROAR the
heaviest; the VA's MORPH PAD 2697, FM6's DRAWBARS 1920).

`./build.sh` (against the same tree with `FELUCCA_CZ=0`): XIP 320348 -> 345472 B (55.1 -> 59.4 %: the 64 tones 9216 B
and the code); RAM 79512 -> 79640 B (80.9 -> 81.0 %); POOL 303160 -> 311300 B (88.1 -> 90.5 %, +8140: the tone store
2 x 2320, the bank cache 2332, the parts' tones 576, the SysEx buffers 296 + 295; `eng_state` does not grow). The blob
cap's +32 bytes in `cr_ui.c`'s picker snapshot are in both.

## Tests

- `tests/cr_cz_test.c` (in `tests/run_cr_tests.sh`): the engine's number and place; Casio's 64 (valid, names, BANK /
  PTCH); every tone through the blob and back, a bad blob; every tone rendered (3 notes, velocity 127 / 1) within range
  and not silent; SysEx: a 144-byte dump in, the send request's dump out identical, a 128-byte dump, an invalid tone,
  no CZ-1 part; the banks (A..D Casio's, E..H empty, BANK / PTCH, a bank restored as backup object 18 and reloaded);
  the 37 pages (titles, sections, every column's min / max / default, the panel value it is, one byte for one R, the
  SUS / END rules, Casio's encodings); the tone store (save, load, reboot, the second object, clear, erase, a slot of no
  tone); the backup's objects 12..21.
- `tests/cr_backup_test.c`: 27 objects (12, 13, 14..21 listed; a saved bank and the store round trip).
- `tests/regress.c`: goldens and CPU baselines for INIT TONE and the 64 (additions only; nothing else changed).
- `tests/target_budget.txt`: `cz_native_render`, `cz_native_wave`, `cz_env_tick` added; the audio ISR's static count
  is 43986 (was 37094: 1880 -> 1882 instructions, the register allocation moved a loop head; its code is the same).
- `tools/emu/scripts/cr_cz.txt` (`test_cr.sh`): CZ BRASS 1, the editor's ENV screen 4 (DCW 1) under the cz band (shot),
  KNOB 1 on DCW 1's L1 (the `deep:` trace, step 1 lit), the chord darker after it (its HF share), the other groups;
  `cz_persist_*.txt` (`test_persist.sh`): an edited CZ-1 sound in U01 across a relaunch; `perf_g.txt` (`perf.sh`).
- `web/test_backup.mjs`: ids 12..21, their names.
