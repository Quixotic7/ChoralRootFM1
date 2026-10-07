# ChoralRoot FM-1 — FM6

FM6 is ChoralRoot's six-operator FM engine (engine 12). Since 0.13 it is **Melodee's** FM6 (Kerem Kilic's fork of
Felucca, <https://github.com/keremimo/melodee>, GPL-3.0): it renders DX7 voices sample for sample as Dexed renders
them, imports and exports DX7 SysEx, keeps a 32-voice bank, and plays up to 16 voices (ChoralRoot: 8, below). This
is phase 1 of 3 of the Melodee platform work (phase 2 brings the CZ-1 engine, docs/CZ1.md; phase 3 USB audio, docs/USB-AUDIO.md).

| file | what | from |
| --- | --- | --- |
| `firmware/src/fm6_core.c` | the synthesis: envelopes in the log domain with the DX7 attack curve, key level / rate scaling, velocity, the 32 algorithms, the LFO, pitch EG, bend, portamento, the controllers; MODERN (MSFA), MARK I (the DX7's log-sine tables) and OPL resolution; the function settings (`fm6_fn`) | Melodee, as it is |
| `firmware/src/eng_fm6.c` | the engine: the patch per part, the macros, Dexed's voice handling (`alloc`, `legato`, `mono_key`, `fm6_ghost`), the DC filter (`post`), the factory presets; ChoralRoot's deep pages and patch blob | Melodee + ChoralRoot's additions |
| `firmware/src/eng_fm6_rom.h` | Melodee's 16 factory voices (F9..F24) | Melodee |
| `tools/gen_fm6_patches.py` | Felucca's 8 factory voices (F1..F8) -> `build/gen/felucca_fm6.h` | Felucca (= Melodee's) |
| `tools/gen_tables.py` | the FM6 tables Dexed computes at start (sine, 2^x, frequency, MARK I / OPL, detune, LFO, portamento) | Melodee's FM6 part |
| `firmware/src/fm6_bank.c` | the bank B1..B32 (a DX7 cartridge), 7-bit packed, storage object `OBJ_FM6BANK`; Felucca 1.0's 27-slot bank and Melodee's earlier one are imported at boot | Melodee |
| `firmware/src/fm6_store.c` | DX7 SysEx in / out over USB-MIDI | Melodee, adapted (below) |
| `firmware/src/fm6_ustore.c` | the FM6 patch store: one blob per user slot | ChoralRoot (as `va_store.c`) |

The copyright headers are kept ("Modifications Copyright (C) 2026 Kerem Kilic (Ellic Studio)"); LICENSING.md lists
Melodee and, through it, MSFA (Apache-2.0) and Dexed (GPL-3.0-or-later).

## The voice model (core.h, voice.c, engines.c)

Melodee's, merged into ChoralRoot's platform (ChoralRoot's additions all kept: the steal fade and deferred start
`voice_fade_steal` / `vsq`, `render2` + `part_side`, the VA, the retired slots, `FELUCCA_*`):

- `NVOICE` 16 voice slots per part, `NPOLY` 8 (an engine without its own cap), `VBUDGET` 16 units shared by all
  parts: a voice of an engine capped above `NPOLY` (or with `units` saying so: FM6) takes **one** unit, any other
  **two**, so every other engine keeps the shared eight voices of before.
- `engine_t` hooks: `alloc` (the engine picks the POLY voice: Dexed's chooseNote), `legato` and `mono_key` (Dexed's
  MONO hand-over), `post` (the part after its voices: Dexed's DC filter), `cap`, `units`; `vmod_t.plog` (the voice's
  own pitch offset, Q24 octaves, without TUNE and bend); `voice_was`; `track_t` gains `foot`, `breath`, `porta`
  (CC 4, 2, 65; CC 5 sets the portamento time) and `bend_raw` (FM6 bends by its own range); `mono_stack[NVOICE]`.
- `engines.c` `eng_state[NPART]` in the pool: one union a part of PHYS's voice slots, WHEEL's part and voices and
  FM6's 16 voices and controllers, cleared at every engine switch (`eng_state_clear`). DRUM, GRAIN and SLICE keep their
  own state (not built by ChoralRoot). The VA's per-voice state is `[VA_POLY]` now (not `[NVOICE]`).
- `voice.c`: an engine with `alloc` takes its voice and steals it in place as Dexed does (FM6 starts a stolen voice
  without KEY SYNC: no click); every other engine keeps ChoralRoot's fade-and-defer. A voice that `done` ends renders
  one more block faded out only for FM6 (Dexed's); the others end at once, as before. `fx.c` saturates a part's
  sum where LEVEL or PAN would overflow (Melodee's; no effect below that).

The other engines render **bit for bit** as before: `tests/regress.c`'s goldens of ANALOG PHASE LOFI SAMPLE VOICE
TRIO WHEEL GRAIN PHYS DRUM NOISE SLICE VA, the voice modes, the sends and the slicer are unchanged; the 4-track mix
and the slicer song (their part 2 is FM6 PAD) change only through FM6 (with an ANALOG pad instead they are identical
to 44566cd's); `perf.sh (f)` (a VA chord and bass) writes the same WAV.

## Polyphony and CPU

`FM6_POLY` (eng_fm6.c, default Dexed's 16) is **8 in ChoralRoot** (`choralroot.c`, the emulator, `tests/regress.c`):
ChoralRoot's chords use at most 7 notes, and at 16 Dexed's long release tails of the chord changes cost too much.
Device estimates from `tools/emu/perf.sh` (avg / worst block of the 2.9 ms budget):

| scenario | before (Felucca's FM6, 6 voices) | Melodee's FM6 at 16 | at 8 (shipped) |
| --- | --- | --- | --- |
| (b) 6-note chord on FM PAD held 6 s | 24 % / 27 % | 25 % / 30 % | 25 % / 29-31 % |
| (c) FM PAD + SUB BASS + a loop + the arp at 200 BPM | 23 % / 35 % | 46 % / 59 % | 30 % / 39-42 % |
| (e) (c) with KNOB 1..4 turning on the editor | 26 % / 32 % | 48 % / 62 % | 32-33 % / 39-42 % |

A voice still takes one budget unit at 8 (`fm6_units`): 8 FM6 voices and the bass (2 units) leave 6 units.

## The patch, its blob and the user slots

Every part has its patch (`fm6_patch`: the 155-byte DX7 single voice) and its function settings (`fm6_fn`: bend up /
down / step, portamento mode / time / glissando, wheel, foot, breath and aftertouch range and target, Dexed's velocity
scaling, ENGINE MODERN / MARK I / OPL). In ChoralRoot **the function settings belong to the sound**: a factory sound
loads Dexed's defaults (MARK I, bend 3), a user sound its own; they travel in the blob.

The blob (`eng_deep_t.blob_size` 128: no cap was raised):

| bytes | content |
| --- | --- |
| 0..111 | the voice as the DX7's 128-byte packed record (VMEM), 7-bit packed (eight 7-bit bytes in seven, as the bank keeps its slots) |
| 112, 113 | `'F'`, 1: magic and version |
| 114..121 | the 16 function settings, bit-packed LSB first (4 4 4 1 7 1 7 3 7 3 7 3 7 3 1 2 bits: 64) |
| 122..127 | 0 |

A bad blob (magic, version, a value out of range, a non-zero tail) loads the init voice with Dexed's functions.

The FM6 patch store (`fm6_ustore.c`) keeps a blob per user slot, 1:1, as `va_store.c` does for the VA: SAVE of an FM6
sound stores the blob of the part it came from, another engine's sound over the slot or an erase clears it, loading
the slot gives the part its blob back (`fm6_user_pending`, `fm6_track_loaded`). 32 x 128 bytes do not fit one
storage object (3840 bytes of payload), so the store is two objects of 16 slots: `OBJ_PROJECT0 + 1` (A/B 0x99000 /
0x9A000) and `OBJ_PROJECT0 + 2` (0x9B000 / 0x9C000), 2064 bytes each (16-byte header: "FM6U", version 1, 16 slots,
the first slot, the blob size, the used mask), mirrored in the pool. The backup carries them as objects **10** and
**11** (`cr_backup.c`, `web/fm1backup.js`). A user slot saved before the store has no blob: it loads its PTCH slot with
Dexed's functions, and a PTCH of Felucca's numbering (B1..B27 = 8..34) moves to today's (B1..B27 = 24..50).

PTCH (EDIT 2's KNOB 4, the HOME knob) still loads a patch over the sound's: F1..F8 Felucca's, F9..F24 Melodee's,
B1..B32 the bank. The macros ALG FB MLVL MRAT MEG VMOD DTUN stay on top of the patch, neutral at 0.

## DX7 SysEx (fm6_store.c)

Accepted from USB-MIDI on any channel (the frame collected by `usb.c` `sysex_byte` -> `fm6_sx_byte`, served in the
UI frame by `fm6_service`):

| message | effect |
| --- | --- |
| `F0 43 0n 00 01 1B <155> <sum> F7` (a voice, VCED) | the FM6 part's patch (a new voice: its notes stop); the sound marked edited |
| `F0 43 0n 09 20 00 <4096> <sum> F7` (32 voices, VMEM) | the bank B1..B32, saved to flash ("FM6 BANK SAVED") |
| `F0 43 1n gg pp dd F7` (a voice parameter; 155: the operator switches) | the patch, the sounding notes follow |
| `F0 43 1n 08 pp dd F7` (a function parameter: 64 mono, 65 bend, 66 step, 68 glissando, 69 portamento time, 70..77 the controllers) | the part's function settings |
| `F0 43 2n 00 F7` / `F0 43 2n 09 F7` (dump requests) | the part's voice / the bank as a dump (device: `ota_wire_send`) |

The FM6 part: the part the sound editor shows if it plays FM6, else the chord part, else the bass part (the channel
does not choose: ChoralRoot's parts are not channels). The dumps out are built in the receive buffer (`fm6_rx`, the
frame just served), so FM6 needs one 4 KiB buffer, not Melodee's two. No STORE page (no web editor either): a bank
dump fills the bank, PTCH loads from it, SAVE keeps an edited voice in a user slot.

## The deep pages (the sound editor)

`FM6_DEEP` (39 pages, `section = {0, 18, 19, 33, 35}`); the editor builds its screens from the titles
(docs/EDITOR.md):

| section (editor group) | pages | KNOB 1..4 |
| --- | --- | --- |
| OSC (FX) | `OP 1`..`OP 6` | LEVEL (0..99) · COARSE (0.5, 1..31; FIXED: 1Hz..1kHz) · FINE (0..99) · DETUNE (-7..+7) |
| | `OP 1+`..`OP 6+` | MODE (RATIO / FIXED) · VSENS (0..7) · AMS (0..3) · RSCL (0..7) |
| | `SCALE 1`..`SCALE 6` | BREAK (A-1..C8) · LDEPTH · RDEPTH (0..99) · CURVE (left and right in one: L x 4 + R, `-L/+E`) |
| FILTER (SEL) | `ALGO` | ALG (1..32) · FB (0..7) · TRNSP (±24 st) · OSYNC (on / off) |
| ENV (ENV) | `ENV 1`..`ENV 6` + `ENV n+` | R1 R2 R3 R4 / L1 L2 L3 L4 (operator n's envelope: lanes A and B under the "dx" band) |
| | `PITCH EG` + `PITCH EG+` | R1..R4 / L1..L4 (levels round 50) |
| LFO (LFO) | `LFO` + `LFO+` | SPEED DELAY PMD AMD / WAVE (TRI SAWDN SAWUP SQR SIN S&H) SYNC PMS |
| MOD (SEQ) | `FUNC` + `FUNC+` | BEND (0..12 st, up and down) PORTA (0..127) ENGINE (MODRN MARK1 OPL) / STEP PMODE (PEDAL / ON) GLISS DXVEL |
| | `CTRL` + `CTRL+` | WHEEL W.DST FOOT F.DST / BRTH B.DST AFTER A.DST (range 0..99, target - P A PA E PE AE PAE) |

`OP n` is the patch's record 6 - n (the DX7 keeps OP6 first). A set is an edit of the patch (the sounding notes
follow, TRANSPOSE stops them) or of the function settings. `mod_dst` is -1 everywhere: FM6 has no matrix (the quick
mapping says "not modulatable"). The six operators are a stack of six rows; with more than four instances the OSC group
has no mixer screen (LEVEL is on the stack).

## Levels (the bank)

The eight FM6 rows keep their names and places (cr_bank.c): TINE EP F1, FM BELL F2, PIANO (F1 through the macros,
ChoralRoot's), FM PAD F5, FM ORGAN F7, MARIMBA F6, FM PLUCK F8, FM BASS F3 (Felucca's originals are F1..F8: 1:1).
Measured with `tests/va_levels.c fm6` (a 6-note chord at LEVEL 92, alone / with SUB BASS; the share of the hold under
the limiter, the peak and RMS after the master):

| row | before: alone / with SUB BASS | now: alone / with SUB BASS | trim |
| --- | --- | --- | --- |
| TINE EP | 0 % -8.1 / -30.5 dB, 0 % -7.9 / -19.4 dB | 0 % -8.1 / -30.5 dB, 0 % -7.9 / -19.4 dB | 0 |
| FM BELL | 0 % -15.5 / -39.1, 0 % -11.5 / -19.4 | 0 % -15.5 / -39.1, 0 % -11.5 / -19.4 | 0 |
| PIANO | 0 % -9.7 / -32.2, 0 % -8.9 / -19.3 | 0 % -9.8 / -32.2, 0 % -8.9 / -19.3 | 0 |
| FM PAD | 0 % -12.2 / -27.0, 0 % -7.7 / -17.8 | 0 % -12.2 / -27.0, 0 % -7.7 / -17.8 | 0 |
| FM ORGAN | 0 % -16.2 / -25.4, 0 % -12.3 / -21.1 | 0 % -16.2 / -25.4, 0 % -12.3 / -21.1 | 0 |
| MARIMBA | 0 % -34.0 / -58.8, 0 % -17.2 / -20.7 | 0 % -33.8 / -58.9, 0 % -17.2 / -20.7 | 0 |
| FM PLUCK | 0 % -29.1 / -52.9, 0 % -15.8 / -20.8 | 0 % -29.1 / -52.8, 0 % -15.8 / -20.8 | 0 |
| FM BASS (with TINE EP's chord) | 0 % -8.2 / -30.2 | 0 % -8.1 / -30.2 | 0 |

Both engines are MSFA's scaling, so the levels agree within 0.2 dB; no row reaches the limiter: the trims stay 0.

## What differs from Felucca's FM6

- Dexed's rendering (Felucca: msfa's core, CTL-sample blocks, its own voice handling): 64-sample Dexed blocks, MARK I
  as the default resolution, Dexed's voice choice, MONO hand-over, portamento, controllers, DC filter.
- 16 voices (ChoralRoot 8) at one budget unit (Felucca: 6 voices at one of the 8).
- F9..F24 (Melodee's voices) and B1..B32 (was B1..B27; Felucca's bank is imported, B n stays B n).
- The function settings per part (Felucca had none), in ChoralRoot the sound's.
- The platform bend (±2 st, RPN 0) does not move FM6: it bends by its own BEND range (`bend_raw`).
- DX7 SysEx in and out (Felucca: the web editor's FM6 protocol, which ChoralRoot does not answer).
- Deep pages and the user-slot blob (ChoralRoot's).

## Memory

`./build.sh`: .text 270312 -> 300788 B (XIP 47.0 -> 54.3 %); RAM 73264 -> 77460 B (74.5 -> 78.8 %: Melodee's FM6
tables in `.data`, 12 KB, the per-sample lookups out of the flash cache; Felucca's 5.9 KB of `fm6_note` and WHEEL's
part state left the RAM); POOL 294740 -> 303116 B (85.7 -> 88.1 %, +8376: the DX7 receive buffer 4104, the patch
store 2 x 2064, the bank 3472 -> 3612; `eng_state` takes no more than PHYS's slots took).

## Tests

- `tests/cr_fm6_test.c` (in `tests/run_cr_tests.sh`): F1..F24, the blob round trip of every factory patch, a bad
  blob, VCED in -> patch -> VCED identical, a parameter and a function change, a VMEM bulk dump into the bank (and
  flash), PTCH B n, the 39 pages (titles, sections, every column's min / max / default), the patch store (save, load,
  reboot, clear, a slot of before the store), the voice model.
- `tests/fm6_test.c` is Melodee's (algorithms against the DX7 diagrams, patch formats, pitch, levels, envelopes,
  modulation, DC, macros, voices, cost).
- `tests/regress.c`: the FM6 goldens re-made (F1..F8 changed, F9..F24 new), nothing else.
- `tools/emu/scripts/cr_fm6.txt` (`test_cr.sh`): TINE EP, the editor, OP 2's LEVEL changes the sound (its brightness
  in the WAV), the dx band drawn and its segment lit; `fm6_persist_*.txt` (`test_persist.sh`): an edited FM6 sound in
  U01 across a relaunch.

`tests/run_tests.sh` (Felucca's units, every flag at its default) runs to its end; what breaks there is Felucca's web
editor and backup around FM6, which ChoralRoot does not use: `tests/backup_test.c` (Felucca's `editor_backup.c` stages
an object in its 3584-byte project scratch: Melodee's 3612-byte bank does not fit), `tests/editor_test.c`'s three FM6
checks (Felucca's `editor_fm6.c` protocol: 8 factory names, B1..B27) and `web/test_web.mjs`'s mock tables (the editor's
FM6 presets, PTCH range and bank size are Felucca's). The `text spacing` failure is older than this work.
