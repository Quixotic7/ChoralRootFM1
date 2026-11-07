# ChoralRoot FM-1 — integration design

How the three independent parts become one instrument on Felucca's platform:

| part | files | owner of the truth |
| --- | --- | --- |
| the musical engine | `firmware/src/cr_engine.[ch]` | chords, voicing, Key Mode, play styles, performance scheduler, bass, latch, panic; emits note events on three streams |
| the screens | `firmware/src/cr_screen.h`, `cr_draw.c`, `cr_gfx.c` | draws a `cr_screen_t` view-model; knows nothing of the engine |
| the platform | Felucca's `hal/`, `gfx.c`, `engines.c`, `voice.c`, `fx.c`, `audio.c`, `usb.c`, `midi_uart.c`, `storage*.c`, `ota*.c`, `main.c` | sound, MIDI, flash, update, the main loop |
| the glue (this document) | `firmware/src/cr_ui.c` (input grammar + view-model), `cr_out.c` (streams → parts and MIDI), `cr_settings.c`, `cr_anim.c`, `choralroot.c` (the compilation unit) | the instrument |

`PLAN.md` §3–§6 is the behaviour; this is the mechanism.

## 1. The compilation unit

`firmware/src/choralroot.c` replaces `felucca.c` as the single compilation unit, in Felucca's
include order, keeping: the HAL, `libc.c`, `lcd.c`, `gfx.c`, `core.h`, `engines.c`, `params.c`,
`mod.c`, `voice.c`, `slicer.c`, `fx.c` (without `perform.c` since 0.18: `FELUCCA_PERFORM 0`), `usb.c`, `midi_uart.c`, `audio.c`, `panel.c`,
`storage*`, `upreset.c`, `ota*`, `console.c`, `main.c`; dropping Felucca's instrument: `seq.c` with
`song_chain.c`, `motion.c`, `chord.c`, `midi_control.c`, `midi_clock.c` (`FELUCCA_SEQ 0`), `ui*.c`, `icons.c`
(`FELUCCA_ICONS 0`), `project.c`, `editor*.c`, `favorites.c`, `ui_name.c` (naming is re-done small in `cr_ui.c`; of
`editor*.c`'s SysEx, `cr_backup.c` answers the backup subset), and the sample-based engines SAMPLE, GRAIN and DRUM
(`FELUCCA_SAMPLE`, `FELUCCA_GRAIN`, `FELUCCA_DRUM` 0: ChoralRoot is all-synth). Where a kept file references a
dropped one, `cr_out.c` / `cr_shim.c` / `cr_ui.c` / `engines.c` provide names of the same meaning, so kept files are
not edited:
- `fx.c` → `seq.c`'s `events_block` (every block): `cr_out.c`'s runs `voice.c`'s `engine_block` per part (the engine
  switches) and the releases a sound load asks for (`panic_req`, bit per part), and recovers from a MIDI in overflow;
- `cr_out.c`'s own MIDI in → `midi_control.c`'s `midi_event`: `cr_out.c`'s (section 2);
- `upreset.c` → `transport_req` (0), `trk_index`, `drum_track`, `motion_base_value` / `motion_guard` /
  `motion_unguard` (no motion lanes: the sounding value is the saved one), `step_lanes` / `step_accents` (DRUM's,
  `engines.c`: no grid); `cr_bank.c` → `chain_busy` (0 without the song chain);
- `main.c` → `chain_defaults(&chain_config)` (a macro that drops its argument), `panic_req`, `ui_input/ui_leds/ui_draw`
  (`cr_shim.c`); `audio.c` → `kb_out_tick` (its key-latency stamp: written, nothing reads it);
- `voice.c` → DRUM's `drum_reuse` (a stub: no part plays engine 10) and `ENG_DRUM` (the retired slot), `params.c` →
  SAMPLE's alias table (`SMP_ALL_NAMES`, `SMP_SET_ORIG`, `SMP_NSETS` 0: no alias).
A retired engine keeps its number (the stores and the protocol hold numbers: `ENGINES[]` is append-only): its slot is
`engines.c`'s `ENG_GONE` (no DSP, no presets, `eng_ok` false: the pickers and PRESETS never offer it). A user sound
saved on engine 4, 8 or 10 (a Felucca slot, a restored bank) loads as the INIT sound on the VA under its name, with
the message "engine retired: init sound" (one saved on PHASE, LOFI, VOICE, TRIO or NOISE, retired in 0.18: "0.18:
PHASE, LOFI, VOICE, TRIO, NOISE retired" below) (and the trace line `load: slot N engine E retired -> INIT on VA`). ANALOG
(engine 0) is retired too since 0.14 (`FELUCCA_ANALOG` 0; its source left the tree in 3y232): its code, presets and pool
are out of the image; a user sound saved on ANALOG (a 0.13 slot) loads on the VA with its macros carried by name (CUT RES DRIVE MIX
DTN, the filter envelope to FENV, ATK / REL; `cr_ui.c cu_analog_va`, the VA's init patch under them), an added preset
of the VA's pool (`cr_bank.c cb_rec_engine`; its binding to an ANALOG factory preset is dropped), with the message
"ANALOG retired: on the VA" (trace `load: slot N engine 0 (ANALOG) retired -> VA, its macros`).
**3y232: Felucca's tree goes.** Felucca's own unit (`felucca.c`), its sequencer, UI, editor, projects and the
retired engines' sources (38 files) and the DIGITAL -> FM6 converter (`fm4_convert.c`: a DIGITAL record now loads as
the VA's init sound, as the other retired engines' do) left the tree, with the host tests that compiled them. `core.h`'s
defaults are ChoralRoot's (the retired engines' and the sequencer's switches 0, setting one 1 an error; the VA, CZ-1
and FM TONE in every build, so the sound pool is too, `UP_POOL` 1); `tests/hostsim.c` builds in that configuration and
gives the host tests stand-ins for what `cr_out.c` provides (`HOST_CR_OUT`: the emulator and `tests/cr_trans_test.c`
take `cr_out.c` itself); `tests/regress.c`'s goldens are ChoralRoot's engines' (279, the 259 of before unchanged).

**Melodee's FM6 and voice model (2026-10-07, docs/FM6.md).** FM6 is Melodee's (Kerem Kilic's fork of Felucca): Dexed's
rendering, DX7 SysEx, the 32-voice bank (`eng_fm6.c`, `fm6_core.c`, `eng_fm6_rom.h`, `fm6_bank.c`, `fm6_store.c`, the FM6
tables of `tools/gen_tables.py`), with the platform it needs merged into the kept files: `core.h` (`NVOICE` 16 slots a
part, `NPOLY` 8, `VBUDGET` 16 units: an FM6 voice one, any other two, so the others keep their eight; the `engine_t`
hooks `alloc legato mono_key post cap units`, `vmod_t.plog`, `voice_was`, `track_t`'s `foot breath porta bend_raw`, the
`F_OFS F_FMNOTE F_FMFRQ` kinds), `voice.c` (the budget in units, the hooks; ChoralRoot's fade-and-defer steal, `render2`
and `part_side` kept, an engine with `alloc` steals in place as Dexed does), `engines.c` (`eng_state[ENG_STATE_NPART]`: PHYS's,
WHEEL's and FM6's runtime state in one pool union a part, cleared at an engine switch; the chord and the bass part only
with `FELUCCA_SEQ 0`, 2 x 12888 B), `fx.c` (Melodee's saturation of
a loud part; the limiter, trim, PAN and stereo lines kept), `mod.c` (CC 2 / 4 / 5 / 65), `params.c` (the new kinds),
`usb.c` (DX7 frames to `fm6_sx_byte`, SysEx bytes sent as single-byte packets), `midi_control.c` / `cr_out.c`
(`bend_raw`). The other engines render bit for bit as before. `FM6_POLY` 8 in this unit (Melodee: 16). ChoralRoot's
additions: FM6's deep pages and patch blob, the FM6 patch store `fm6_ustore.c` (one blob per user slot, storage objects
`OBJ_PROJECT0 + 1, + 2`, backup ids 10 and 11), included by `upreset.c` as `va_store.c` is; `fm6_store.c` after
`cr_ui.c`. Felucca's own unit (`felucca.c`) is no longer kept building against this platform (its web editor's FM6
protocol, `editor_fm6.c`, and its backup of the FM6 bank expect Felucca's layouts); `tests/run_tests.sh` tests them
as they behave on this platform and passes (docs/FM6.md, "Tests").

**Melodee's CZ-1 (2026-10-07, docs/CZ1.md).** Engine 14 (`ENGI_CZ`, `FELUCCA_CZ` 1 in this unit, default 0): native
Casio CZ-1 tones (`eng_cz.c`, `cz_native.c`, `cz_patch.h`, `cz_edit.h`, `cz_legacy.h`), Casio's 64 tones
(`tools/gen_cz1_factory.py` -> `build/gen/melodee_cz1.h`), the eight banks (`cz_bank.c`, included by `upreset.c`), Casio
tone SysEx (`cz_store.c` after `cr_ui.c`; `usb.c` hands the bytes to `cz_sx_byte`). Kept files touched: `core.h`
(`FELUCCA_CZ`, `NENGINES`, `ENGI_CZ`, the blob cap `ENG_BLOB_MAX` 160), `engines.c` (the include, `eng_state.cz`,
`ENGINES[14]`, `ENGINE_ORDER` after PHASE), `storage.c` (`OBJ_CZSTORE1`, `OBJ_CZBANK0..+7` on the user sample slot 1's
flash 0xA0000..0xB1FFF), `upreset.c` (the store's hooks beside FM6's), `usb.c` (one line), `eng_fm6.c`
(`fm6_track_loaded` calls `cz_track_loaded`, `fm6_init` `cz_init`). ChoralRoot's additions: the deep pages and blob,
the tone store `cz_ustore.c` (`OBJ_PROJECT0 + 3` and `OBJ_CZSTORE1`, backup ids 12 and 13; the banks 14..21), the cz
band (`cr_draw.c`, `cr_edit.c`), five chord rows and a bass in `cr_bank.c`. The other engines render bit for bit as
before (`tests/regress.c`: goldens added, none changed).

**FM TONE (engine 15, `eng_quad.c`, code name QUAD; 2026-10-08, docs/QUAD.md).** Engine 15 (`ENGI_QUAD`, `FELUCCA_QUAD` 1 in this unit and the emulator, default 0;
it needs `FELUCCA_CZ`): a Digitone-style four-operator FM voice (`eng_quad.c`, `quad_tables.h`), its state its own pool
arrays (no `eng_state` member). Kept files touched: `core.h` (`FELUCCA_QUAD`, `NENGINES`, `ENGI_QUAD`; the editor's
`eng_screen_t` and `eng_deep_t.screens`), `engines.c` (the include, `ENGINES[15]`, `ENGINE_ORDER` after FM6),
`eng_fm6.c` (`fm6_track_loaded` calls `quad_track_loaded`), `fx.c` (`quad_pan`), `storage.c` (`OBJ_QUADSTORE` on
0xB2000 / 0xB3000), `upreset.c` (the store's hooks beside the CZ-1's), `tools/build.py` (the flag). ChoralRoot's
additions: the patch store `quad_store.c` (backup id 22), the screen plan in `cr_edit.c` (a MOD-less engine accepted:
MOD = the platform's routes), `cr_settings.c crs_slot` (the settings record's eleven engine slots kept, FM TONE's chord
place in `rsv_usb` until ANALOG left: since settings version 7 FM TONE has ANALOG's slot 0, docs/SETTINGS.md), SAFE MODE's erase of the new pair. Memory (`./build.sh` 2026-10-08, with the DC blocker): XIP 413760 B of 581564 (71.1 %), RAM 79900 B of 98304
(81.3 %), POOL 322608 B of 344064 (93.8 %: + 2576 B the store mirror + ~2.2 KB `quad_patch`, `quad_mlast`, `quad_lfo`,
`quad_vs`), NOINIT 216 B; the target budget (`tests/target_budget.txt`) `quad_render` 6089, `quad_block` 43.
The same build recorded `fm1_timer5_irq` 50160 (was 32275): the USB audio service (`ua_service`, a6d3553) runs in that
ISR and the budget had not been re-recorded since; not QUAD's doing. The other
engines render bit for bit as before (`tests/golden.txt`: 16 QUAD renders added, none changed).

**0.14: FM TONE, ANALOG retired, the engine order (2026-10-08).** QUAD shows as **FM TONE** everywhere the user sees
it (the engine picker, the popups "FM TONE · 03/16", the save page "FM TONE · 17", the Sounds page / CLI, backup
object 22 "FM TONE patches"); the code name, files, flags and the patch kind `quad` stay. ANALOG retired (above;
`ENGINES[0]` is `ENG_GONE`, `cr_bank.c`'s ANALOG trim row dropped, `tests/target_budget.txt`'s `analog_render` an
optional entry). `ENGINE_ORDER`: FM6, FM TONE, VA, CZ-1, PHASE, LOFI, VOICE, TRIO, WHEEL, PHYS, NOISE: eleven engines, a
white root each in the pickers (D4..G5); six since 0.18, below. The bass at power-on (and for a settings record whose bass was ANALOG's) is VA
DEEP SUB. perf.sh (c) / (i) now run on the VA bass DEEP SUB (ALGORITHM +18): (c) ISR 30 / 41 % (device est. avg / max),
(i) 29 / 42 %, no holes, no clicks, no erase under sound.

**0.18: PHASE, LOFI, VOICE, TRIO, NOISE retired (2026-10-11).** The owner's decision: the five are Felucca's legacy,
not ChoralRoot's identity, and their flash is wanted elsewhere; PHYS stays for now. Switched off as ANALOG was, a flag
each, default 1 in `core.h` (then: Felucca's unit and its host suite kept every engine), 0 in `choralroot.c`, the
emulator and ChoralRoot's host units; `tools/build.py` passed them from the environment. (Since 3y232 their sources are
gone and the flags are 0 everywhere: the table below is what they cost then.)

| flag | engine | source (kept in the tree) | XIP it costs (switched back on alone) |
| --- | --- | --- | --- |
| `FELUCCA_PHASE` | 2 PHASE | `eng_phase.c` | 1632 B |
| `FELUCCA_LOFI` | 3 LOFI | `eng_lofi.c` | 2400 B |
| `FELUCCA_VOICE` | 5 VOICE | `eng_formant.c` | 2836 B |
| `FELUCCA_TRIO` | 6 TRIO | `eng_trio.c` | 3648 B |
| `FELUCCA_NOISE` | 11 NOISE | `eng_noise.c` | 2580 B |

Measured with `./build.sh` (before: a0dd291): XIP 446552 -> 433728 B of 573440 (77.9 -> 75.6 %: -12824 B, the five's
13096 B less the stand-in's code), RAM 84916 -> 84896 B (VOICE's breath-noise seed and alignment), POOL 295200 B
unchanged: the five kept no state of their own (the voices' common `voice_t`; none of them is in `eng_state`'s
union, whose size is PHYS's: 3 x 4296 B a part). `engines.c`: the includes, `ENGINES[2 3 5 6 11]` = `ENG_GONE`,
`ENGINE_ORDER` (shown: **FM6, FM TONE, VA, CZ-1, WHEEL, PHYS**, `NENG_SHOWN` 6, a white root each in the pickers,
D4..B4), `ENG_RETIRED` (with `ENG_RETIRED_018`, the five's mask), `TRK_DEF` (part 3: FM6 where LOFI PULSE LD was; no
ChoralRoot part), and what the kept files call of them: `pd_cos` (PHASE's, `cz_native.c`'s carrier) and `ENG_FORMANT`
(`fx.c c1_split_ok`: the retired slot). `cr_bank.c`: PHASE's and VOICE's `CB_TRIM` rows go with them. Their factory
presets (24) are in the engine files: out of the image; `web/packs/factory-{phase,lofi,voice,trio,noise}.json` are
removed (`tools/make_packs.py`: six packs), `tests/target_budget.py`'s entries of their renders are optional. They had
no editor plan of their own (the platform's one screen a group, which WHEEL and PHYS still use).
**What a unit with sounds on the five gets** (the owner did not say: built so, flagged in the release notes): nothing
is deleted or rewritten in flash. A user record of one keeps its slot, name and engine number; `cr_bank.c
cb_rec_engine` puts it in the VA's pool as an added preset (its binding to a factory preset of its own engine is
dropped: it replaces no VA preset), and `cr_ui.c cu_load_user` loads the VA's init sound for it, a stand-in, with what
carries over honestly: the record's sends (DIST CHO DLY REV) and its envelope's ATK / REL on the VA's ATK / REL macros
(`cu_analog_va`'s mapping of the same two; the engines' own eight macros have no VA meaning and are not mapped; LEVEL
and PAN are the part's, as for every sound). The message is `PHASE retired: stand-in` (23 characters: `cu.msg.text`
holds no more; trace `load: slot N engine 2 (PHASE) retired -> INIT on VA, its sends, ATK, REL`). SAMPLE's, GRAIN's
and DRUM's records load the plain init sound as before. A settings record whose `chord_sound` / `bass_sound` names one
of the five reads as `CRS_SOUND_DEFAULT` (`cr_settings.c crs_sound`: not a pool today): FM6 TINE EP, the bass VA DEEP
SUB behind BASS, as a fresh unit. The record's `pool_pos` slots stay fixed per engine (`CRS_NENG` 11, `CRS_SLOT_ENG`:
3 PHASE, 5 LOFI, 6 VOICE, 7 TRIO, 10 NOISE are reserved: no rank asks for them, `crs_capture` keeps their bytes; no
version change). SysEx 76 SELECT of one answers rc 1. The clients: `ENGINE_NAMES` keeps the names; `FACTORY_PRESETS`
(from `tests/sound_templates.c`, the engines with `eng_ok`) loses the five, so `pool_of` / `poolOf` gives the VA's;
`--sounds` prints `retired` where it prints `patch` (ANALOG's too), the installer page's table `PHASE · retired`, the
device manager shows six tabs and such a record as a row of the VA's with `PHASE · retired`. A backup made on 0.17
restores without error (`tests/cr_backup_test.c` "0.17 backup"). Tests: `tests/cr_fm6_test.c` (the five's records,
the six engines shown), `tools/emu/test_retired.sh` (run by test_cr.sh: a flash written by a build with the five
switched back on, a user sound on PHASE, the chord part parked on NOISE; today's build starts on TINE EP and shows
the stand-in; the first build reads the PHASE sound again), `web/test_sounds.mjs`, `web/test_manager.mjs`,
`tests/install_test.py`. The emulator scripts that named the five: `cr_engine.txt` and `cr_editor_map.txt` (A4, the
fifth white root: WHEEL now), `cr_editor_steps.txt` / `cr_editor_ref.txt` (the platform editor on WHEEL SOFT FLUTE;
TRIO CHIP CHOIR before).

**0.18: PHYS retired (2026-10-13).** The owner: "PHYS isn't the greatest engine and has built in chord modes which
conflict with the purpose of ChoralRoot so I think we should get rid of it, much of these sounds can be done with FM
too." The same mechanism as the five: `FELUCCA_PHYS`, default 1 in `core.h` (Felucca's unit, `tests/regress.c`'s PHYS
goldens, `phys_test`, `phys_ref`, `upreset_test`'s PHYS DRUM records keep the engine), 0 in `choralroot.c`,
`tools/emu/emu_firmware.h`, `tests/hostsim.c` (`FELUCCA_SEQ 0`) and `tests/cr_cpu1_test.c`; `tools/build.py` passes it
from the environment (`FELUCCA_PHYS=1 ./build.sh` builds it back in). `eng_phys.c`, `phys_dsp.c` and `phys_symp.c`
stay in the tree, compiled out. `engines.c`: the include, `ENGINES[9]` = `ENG_GONE`, `ENGINE_ORDER` (shown: **FM6, FM
TONE, VA, CZ-1, WHEEL**, `NENG_SHOWN` 5, the white roots D4..A4), `ENG_RETIRED_018` (bit 9 with the five's), and
`eng_state`'s union without `phys_slot_t phys[PHYS_POLY]` (`phys_slots` goes with it). What the kept files called of
it: `fx.c c1_split_ok` (PHYS on part 0 kept a block on one core: every block splits now) and `upreset.c up_values`
(`phys_legacy`, a version-1 record's DUST), both under `#if FELUCCA_PHYS`. Nothing else in ChoralRoot's unit named it
(`project.c` is Felucca's).

Measured with `./build.sh` (before: d013fda): XIP 463496 -> 454148 B of 573440 (80.8 -> 79.2 %: **-9348 B**), POOL
326608 -> 314600 B of 344064 (94.9 -> 91.4 %: **-12008 B**: the union a part was PHYS's three slots, 3 x 4296 = 12888
B, and is FM6's `fm6_part_t`, 6884 B, now: 2 x 6004 B), RAM 80520 -> 80532 B (+12 B of `.data`, 81.9 %: nothing of
PHYS's was in RAM). `tests/target_budget.txt` loses `phys_render` and the six `px_*` lines (optional entries of
`target_budget.py`, as the five's).

**What a unit with PHYS in use gets: exactly what the five's rules give** (no new code path but the name in the
message). A user record of PHYS (engine 9) keeps its slot, name and bytes; it is an added preset of the VA's pool
(`cb_rec_engine`: `CB_RETIRED_POOL`; a binding to a PHYS factory preset is dropped) and `cu_load_user` loads the VA's
init sound for it with the record's ATK / REL on the VA's macros of that name, `PHYS retired: stand-in` (trace `load:
slot N engine 9 (PHYS) retired -> INIT on VA, its ATK, REL`; the sends are the part's since the sends became one value
a part, so a load no longer touches them). A settings record whose `chord_sound` / `bass_sound` names PHYS reads as
`CRS_SOUND_DEFAULT` (`crs_sound`: `cu_engine_melodic` is 0 for a retired engine): FM6 TINE EP, the bass VA DEEP SUB.
The session's part records (`css_part_put`) refuse an engine that is not melodic and the part keeps what the settings
gave it, so a unit switched off with a part on PHYS comes up on the same defaults, the rest of the session applied
(Bass on, the voicing, the FX). `pool_pos` slot 9 (PHYS's) is reserved, its bytes kept. A loop record holds events,
no sound: nothing to do. A backup is the same objects (banks, settings): it restores without error with the same
result. SysEx 76 SELECT of engine 9 answers rc 1. Going back to 0.17 (or a build with `FELUCCA_PHYS=1`): the records
were not rewritten, PHYS plays them again; the settings and the session name PHYS until the next global save.
The clients: `FACTORY_PRESETS` (from `tests/sound_templates.c`) loses engine 9, so `pool_of` / `poolOf` gives the
VA's and `engine_retired` / `engineRetired` is true; `--sounds` and the installer page print `PHYS` and `retired`, the
device manager has five tabs and five packs (`web/packs/factory-phys.json` removed, `tools/make_packs.py` `ORDER`).
Tests: `tests/cr_fm6_test.c` (a PHYS record with the five's, the five engines shown, the message),
`tests/cr_backup_test.c` ("0.17 backup": a PHYS record bound to PHYS 09, the settings with both parts on PHYS, SELECT,
the session's part records), `tools/emu/test_retired.sh` (the pre-cut build has PHYS back too; "the PHYS runs": both
parts parked on PHYS, a user sound on it, the settings and the session saved by a build with PHYS; today's build
starts on TINE EP / DEEP SUB with Bass still on and shows the stand-in; the first build finds both parts on PHYS
again), `web/test_sounds.mjs`, `web/test_manager.mjs`, `tests/install_test.py`. `tools/emu/scripts/perf_g.txt`: the
bass part's picker step was `SELECT +3` from the VA, which landed on PHYS (above, "The audio ISR lighter"); it is `+1`
now, the CZ-1 as its title says, so (g)'s figures are not comparable with those before. The remaining engines' sound
is untouched: `tests/run_tests.sh`'s goldens and CPU baseline (Felucca's unit, every engine) are unchanged files and
pass, and the emulator's WAVs of test_cr.sh, test_persist.sh and perf.sh (ChoralRoot's unit) are byte for byte those
of the commit before, but for the scripts that changed.

**0.24-beta2: GRAIN (engine 8) shown again** (HISTORY 3y133 .. 3y139, docs/GRAIN.md): a new engine in Felucca's slot,
`FELUCCA_GRAIN 1` in `choralroot.c`, `ENGINE_ORDER` (shown: **FM6, FM TONE, VA, CZ-1, P5, WHEEL, GRAIN**,
`NENG_SHOWN` 7). Its user records are pool slots with a 64-byte patch (`upool.c`); a build without it (0.24-beta1, 0.23)
treats them as the retired engines' above: an added preset of the VA's pool, the VA's init sound (`engine retired:
init sound`), the record and its patch untouched, GRAIN again on a build that has it.

Build flags stay Felucca's; `FELUCCA_SLICE=0`, `FELUCCA_SLICER=0`, `FELUCCA_FM4=0`, `FELUCCA_UAC=1` (since 0.14 Melodee's
USB audio recording: ChoralRoot In, docs/USB-AUDIO.md), `FELUCCA_UART=1`,
and the all-synth set `FELUCCA_SEQ=0`, `FELUCCA_SAMPLE=0`, `FELUCCA_GRAIN=0` (1 again from 0.24-beta2: its own GRAIN, no SAMPLE), `FELUCCA_DRUM=0`, `FELUCCA_ICONS=0`,
`FELUCCA_KEYCAPS=0` (BUILDING.md).
The package identity becomes `FM-1_920` and the version string `ChoralRoot X.Y`. **The version has one source**:
`FELUCCA_VERSION` in `firmware/src/choralroot.c` (the dev tree reads as the next tag, `ChoralRoot 0.17`;
`./build.sh --release X.Y` passes `-DFELUCCA_VERSION="ChoralRoot X.Y"`); `cr_ui.h`'s `CR_VERSION` is that macro (until
2026-10-12 a literal of its own, `"ChoralRoot 0.1"`, which Options > Version showed on every build). Shown from it:
the splash (`cr_ui.c cu_stripes`, `cr_shim.c cr_splash`), Options > Version (`opt_text`; longer than its 23
characters, as the single-core beta's 28, without the leading `ChoralRoot `: `cu_version_text`), the identity over SysEx
(`cr_backup.c CRB_INFO`: `fm1_install.py --info`, the installer, the device manager), the console (`console.c`).
The emulators build with the same string (`tools/emu/build.sh`, `tools/emu/web/build_web.sh`: choralroot.c's define,
or `CR_VERSION=X.Y`; `cr_ui.c` refuses to compile without it; `emu_firmware.h` falls back to `"EMU"` for the host
tests that build that unit, and `tests/cr_backup_test.c` reads it back from INFO). GEEK OUT
shows no version. **Options > Reset Settings** (`O_RESET`, the last entry outside SAFE MODE, 2026-10-12): OCT+ twice
(`cu_reset_ask`, as Flash Data's `cu_erase_ask`; both are dropped by another entry and, new, by the Options closing:
`cr_build_screen`), `cu_reset` -> `cr_settings.c cr_settings_reset` (docs/SETTINGS.md "Defaults, and Options > Reset
Settings": what is reset, what is kept, the session's end). Test: `cr_version.txt` in test_cr.sh (the dump's `version:` line: the Options entry's text = the
INFO reply's string = choralroot.c's define; no other `"ChoralRoot <digit>` literal in the firmware or the
emulators). The fixtures that say `ChoralRoot 0.1` (`tests/install_test.py`, `web/test_installer.mjs`) are fake
units' identities for the installers' tests, not the firmware's version.

## 2. Parts and streams (`cr_out.c`)

Felucca has four `track_t` parts sharing a budget of 8 voices (since Melodee's voice model, 16 units: an FM6 voice
takes one, any other two; FM6 plays up to 8 voices a part here, the other engines their 8 shared; with Options > Dual
Core On, the default of a new unit since 0.22, the bass part has a budget of its own: docs/DUALCORE.md). ChoralRoot uses two:

| part | engine role | fed by |
| --- | --- | --- |
| part 0 **CHORD** | the chord / performance sound (PRESETS) | stream MAIN |
| part 1 **BASS** | the bass sound (ALGORITHM); `P_VOICE = V_MONO` | stream BASS |

Stream RAW is MIDI-only by default (Options > MIDI Channels can route it to part 0 too, for the
"sustained pad under an arp" case).

`cr_out_t` callbacks from the engine run **in the audio ISR** (the engine ticks there, §4):

```
note_on(s, note, vel):  if part_of(s) >= 0: trk_note_on(&trk[part], note, vel)   (voice.c)
                        if midi_en[s]: midi_out_event(0x09 | (0x90|ch[s]) << 8 | note << 16 | vel << 24)
note_off(s, note):      trk_note_off(...); midi 0x08 ...
all_off(s):             every voice of the part released (voice.c's release-all), CC 123 on ch[s]
```

MIDI out goes through Felucca's `midi_out_q` (USB) exactly as Felucca's `seq.c` key_on / key_off did.
Channels default 1 / 2 / 3 (Orchid); each stream has `enabled` and `channel` in settings.
Clock out: 24 PPQN from the engine's tempo on the audio clock (`cr_out.c`, the same sample counter the loop uses);
clock in: `cr_midi.c`'s follower (Felucca's `midi_clock.c` is not in the unit).

MIDI in is ChoralRoot's own (`cr_out.c` `cr_midi_in`, pure parts in `cr_midi.c`): the audio ISR drains usb.c's
`midi_in_q` (USB and TRS) at the top of each block, before `fx.c` calls `events_block` (`cr_out.c`'s: on an overflow
it drops the queue and releases every part). Only the CHORD and BASS channels of Options (MIDI Perform / MIDI Bass;
Off: ignored) are heard:
- notes, pedal, bend, pressure and the other CCs go to `cr_out.c`'s `midi_event(status, part, d1, d2)`, what
  ChoralRoot kept of Felucca's `midi_control.c`: note on / off → `trk_note_on` / `trk_note_off` on the part (a
  repeated note-on restarts it), CC 64 sustain (a released note held until the pedal is up), pitch bend → `voice.c`'s
  `midi_bend_target` (±2 semitones; RPN 0 sets 0..24 semitones and cents), CC 1 / CC 11 / channel pressure →
  `mod.c`'s MODW / EXPR / AT sources (`mod_midi`), CC 120 all sound off, CC 121 reset controllers (the bend range
  kept), CC 123 all notes off (the pedal honoured). Felucca's chords (CHRD), arpeggiator and live recording were the
  sequencer's and are gone; a MIDI note and the engine's stream share the part's voices as before.
- CC 7 → the part's LEVEL, CC 91 / 93 / 94 → its reverb / chorus / delay send (part 0's are the FX layer's "Send" cells), program
  change → the chord sound (its position in the chord part's engine's pool, 0 = INIT) or the bass sound (ALGORITHM's
  position: 0 = off, then the bass part's engine's pool); out of range ignored. The ISR posts these to a small ring (`cr_min_q`) and the UI frame
  (`cu_midi_poll`) applies them with the knob's meter popup.
- Options > MIDI Clock = **In**: 0xF8 feeds the follower (24 pulses averaged, a window restarts on a gap > 150 ms,
  BPM 20..300, a change under 0.8 BPM ignored so the ms stamps never reset the clock) → `cr_set_tempo` in the ISR
  (arps and patterns rephase); the UI mirrors it into the BPM shown. A SELECT turn while In shows its meter and
  tempo, but the next pulse re-asserts the clock's. 0xFA starts the loop from its start (restarts it if playing),
  0xFB plays it if stopped, 0xFC stops it (the LP_PLAY path; not during a take). Nothing is sent out while In.
  Out / Off: clock, start and stop in are ignored.
No chord generation from MIDI in (Orchid's behaviour).

## 3. Input grammar (`cr_ui.c`)

One scan per UI frame (15 ms) over `fm1_in` + `fm1_input_edges()` + `fm1_enc_take()` as
`ui_input.c` does, feeding a small state machine. The firmware key index `k` (0..26, F3..G5):

```
chord keys:   k ∈ {1,3,5,8} → DIM MIN MAJ SUS;  k ∈ {0,2,4,7} → 6 m7 M7 9   (CRE_MOD down / up → cr_mod)
LOCK:         k = 6 (B3) → LOCK on / off (a tap; lit while on; since 0.18 kept with its latch by the global save: the session, docs/SETTINGS.md)
root keys:    k ≥ 9 → cr_key(c, 53 + k + 12*octave, vel, down)   (vel from Options > Velocity)
layer open:   k ≥ 9 → the layer's map instead (tonic / mode / slot / effect / engine)
```

**LOCK** (B3) is a mode toggle for the chord block, done in the UI as a virtual hold (`cu_mod_press` /
`cu_mod_release`, `cu.mlatch`): a latched chord key stays posted down to the engine (CRE_MOD down, no up), so
every play style (Simple / Advanced / Free) sees it held. LOCK off: the chord keys momentary. LOCK on: the chord
keys held are latched when released (their LEDs stay lit: the engine's `mods_active`); with no chord key held, a
top-row key (DIM MIN MAJ SUS) **resets** the latch to that type alone (the others posted up, the extensions
cleared) and a bottom-row key (6 m7 M7 9) **toggles** its extension (off: posted up at once, its release does
nothing). Hold MIN, release: Dm on D4; 6: Dm6; 9: Dm6/9; 9 again: Dm6; MAJ: D. **The LOCK layer** (`L_LOCK`, the
owner, 2026-10-10): B3 held alone past HOLD_MS (`cu.b3_pend`: B3's tap is now its release, or the next key pressed
while it is down, so the order played is kept; a hold toggles nothing) opens a knob row under "Lock" (label
"lock · on / off", B3 blinking, footer "B: on/off · OCT-: back · HOME: home"; the roots and the chord keys play
in it, SELECT stays the tempo; OCT− / OCT+ / HOME / B3 held again close it) with two saved styles (settings v13,
bits 6 / 7 of `loop_rec`), both acting only while LOCK is on (`cu_mod_press`): **KNOB 1 "Type key" Clears / Keeps**
(`cs.lock_keep`: a type key pressed alone replaces the latched type and, in Keeps, leaves the latched extensions)
and **KNOB 2 "Ext keys" Toggle / Pushed** (`cs.lock_push`: an extension key pressed with no chord key held starts
a new set of that key alone, the type untouched; keys pressed while it is held join, as chord keys always did. The
owner's one exception: the set is exactly {K} and K is pushed again alone: K goes off, decided at its release
(`cu.mpend`: K stays down and sounding until then; another extension key pressed meanwhile joins and both stay;
{7, 9} then 7 alone is {7}, not empty). Keeps + Pushed: a type key changes the type only, an extension key the
extensions only. The defaults (Clears, Toggle) are the behaviour above. `cr_lock_menu.txt`, `persist_lock_*.txt`,
`tests/cr_settings_test.c t_key_keys_v13`. **The engine under LOCK** (`cr_engine.h cr_mod_lock / cr_mod_ex /
cr_mod_touch`, 2026-10-12; `CRE_MODLOCK`, `CRE_MOD` with b = quiet, `CRE_MODTOUCH`): the keys posted down are the
latch's set and the play styles differ only in who follows it. Until then Advanced / Free ran their own extension
toggle (`crx_modifier_press`: `v->sx ^= bit`, kept after the key's up) on top of the latch: with a root held a
latched extension's up left it in the voice and the next down toggled it OFF (latch on, sound off: "pressed
twice"), and Free's re-sounded chord, which ends when no chord key is down, never ended (a latched key is never up)
and took `lp.sx | bit`, every extension pushed since the last root. Now, with lock on: `cr_mod_ex` only edits
`mods_down`, then `crx_lock_sync` gives every chord voice the snapshot a new root would get (Advanced / Free; a type
key's down that changed the type re-strikes, the rest by Extension Addition; one sync per push: the UI posts a
replaced set as quiet ups, then the down); `cr_mod_touch` (the chord keys physically held, after a press's events,
before a release's) starts Free's voice on a press with no voice (the last root, the set's spec: a lone repeat
changes the set only at its release, so it sounds the old set) and ends it when the last key is let go. LOCK off
(`cu_lock_tap`): `CRE_MODLOCK` 0 first, so the ups are plain releases and a held chord keeps its spec as it always
did. Lock off, `cr_mod` is what it was (`s_advanced`, `s_free`). Tests: `tests/cr_engine_test.c s_lock`,
`cr_lock_push.txt` (x 3 play styles x Clears / Keeps) and `cr_lock_tog.txt` (x 3) in test_cr.sh, whose dump line
`mods:` shows the engine's keys down, the LEDs' set and each voice's own spec. **The top row's lone repeat** (the owner, 2026-10-11; always on with LOCK,
no setting): the latch holds exactly one type and that type key is pressed again with no other chord key held: it goes
off at its release (`cu.tpend`, as `cu.mpend`: on and sounding until then; any other chord key pressed meanwhile
cancels it and both latch; a root played meanwhile does not), posted up: no type, the roots play single notes (the
engine: an extension without a type is a single note, `cr_chord_base`; Key Mode with Key plays Chords still gives
the scale's chord with them). The latched extensions are NOT cleared: they stay down and lit, and a type key pressed
when no type is latched joins them, Clears or Keeps (Clears clears them only when a type key replaces another type).
The screen shows the single note's name (`D`, `D4`); `cr_lock_type.txt`. LOCK off and PANIC post the
latched keys up (`cu_unlatch`). The top line shows a small white `lock` (after the perform mode, the octave and
Latch). Traces: `lock: on` / `off` / `latched MIN 6 9`, and `chord: Dm6 9` as the sounding chord changes.

Buttons (`panel.btn[]` ids): **tap** = release < HOLD_MS (300 ms, Felucca's `settings_hold`) with no key, knob or
other button touched meanwhile: always the button's action, also while its own layer is open. **Hold** = past
HOLD_MS, or a key / knob / button touched meanwhile (a combo, no tap): a layer button opens its layer **and locks
it** (`cu.lock`): it stays open after release, its LED blinks; **OCT− (back) or HOME closes it** (OCT− in a layer
is never the octave), **its own button held again past HOLD_MS closes it** (alone: with a key or a knob touched
meanwhile it is a combo in the layer, which stays), holding another layer button switches to that layer.
**OCT+ confirms where a layer has something to confirm, and closes it where it has not** (the owner's rule,
2026-10-10; until then PERF's and FX's OCT+ were on / off and the PLAY menu's did nothing): the table below.
The lockable layers: KEY, PERF, FX, BASS, LOOP, SEQ and the engine picker (EDIT held: a preview, OCT− cancels,
OCT+ / EDIT / EDIT held again keep).

| layer | OCT− | OCT+ | HOME | a tap of its button | its button held again |
| --- | --- | --- | --- | --- | --- |
| KEY | close | close | close | Key Mode on/off, the layer stays | close (was: stays) |
| PERF | close | **close** (was: perform on/off) | close | perform on/off, the layer stays | close (was: stays) |
| FX | close | **close** (was: the effects on/off) | close | the effects on/off, the layer stays | close (was: stays) |
| BASS | close | close | close | bass on/off, the layer stays | close (was: stays) |
| SEQ (the metronome's) | close | close | close | the click on/off, the layer stays | close (was: stays) |
| LOOP (the PLAY menu) | close; step entry: back a step | **close** (was: nothing); step entry: a rest | close | play / stop, the menu stays | close (was: stays) |
| the view menu (HOME held) | close | close ("ok": the change is live) | close | (HOME tap: close) | close (was: stays) |
| the engine picker (EDIT held; since 0.18 its roots and chord keys always play, SELECT steps the engines, KNOB 4 has no job) | cancel, close | keep, close | cancel, close | keep, close | keep, close (was: stays) |
| the LOCK layer (the key B3 held) | close | close | close | LOCK on/off, the layer stays | close |
| the loop editor (EDIT tapped in the PLAY menu; 0.18) | close, back to the PLAY menu | close, back to the PLAY menu | close, back to the PLAY menu | (EDIT tap: close) | EDIT held: close (no engine picker from there) |
| SAVE held (0.18: no layer, the global save; until 0.17 the loop slots, momentary) | — | — | — | — | the global save, once, at the hold time |
| Options (OPT tap; not a layer) | close | close; Calibrate: start; SAFE MODE's Flash Data: erase (twice) | close | (OPT tap: close) | — |
| the save dialog, naming | cancel | take the choice / save; "delete?": yes | cancel | (SAVE tap: as OCT+) | SAVE 1 s: delete / reset |

 A layer opened from the sound editor closes back to the editor (OCT− / HOME). SAVE held is no layer since 0.18 (the global save, `cu_gsave`; GLO + SAVE held 1 s: the revert, `cu_revert`); OPT is the knob shift;
a button pressed during another's hold is that hold's combo (its release does nothing).

| printed | role | tap | hold (locked open unless noted) |
| --- | --- | --- | --- |
| SEL | KEY | Key Mode on/off | the knob row with the keyboard as its band: roots = tonic (Key Mode on; the scale kept unless MAJ / MIN is held with it); **MAJ / MIN = the scale**, at the press, LOCK on or off (`cu_key_scale`: the chord block is the layer's here, `cu.kpick` keeps those presses from the engine and from LOCK's latch, so the latched chord is what it was when the layer closes; the other six chord keys do nothing, B3 stays LOCK). That is **keys: pick**, the default; **an OPT (GLO) tap in the layer** switches to **keys: play** and back (`cs.key_play`, settings v13: bit 5 of `loop_rec`; the PLAY menu's Keys is the precedent): the roots and the chord block then play exactly as outside the layer (`cu_layer_keys`, `cu_key_pick`) and their LEDs are their own state again, OPT lit; the label reads "key · keys pick" / "key · keys play", the footer "MAJ MIN: scale · GLO: play · OCT-: back" / "GLO: pick · OCT-: back · HOME: home"; OPT never opens the Options from this layer, OPT held + a knob keeps its second jobs (a combo, no toggle: + KNOB 1 the split point, which belongs to the layer's Single cell); `cr_key_play.txt`, `persist_kkeys_*.txt`; KNOB 1–4 Tonic Scale Transpose Single as cells, the turned cell hot |
| ARP | PERF | performance on/off (LED lit while on: no message; the top line says the mode; on the Chord view an arpeggio or a pattern playing shows the arp screen, section 5; the other views never change) | the knob row: SELECT = mode (picking one turns it on), the roots play the chord; a PERF tap in it = on/off (the label "perform · on" / "· off"), OCT+ closes; KNOB 1–4 = the mode's params as cells; no popups, the turned cell hot 800 ms; **the band is the arp while notes are stepped** (section 5, "the PERF layer's band"), the picker for 2 s after the layer opens and after every SELECT detent |
| FX | FX | main effect on/off (every part's sends: `fx.c fx_off`) | the knob row: SELECT = the page (Sends, Reverb 1, Reverb 2, Chorus, Delay 1, Delay 2, Drive: 0.18, docs/FX.md), the roots play; a FX tap in it = on/off (label "fx · on · 1/2" / "· off"), OCT+ closes; KNOB 1–4 the page's cells, Sends' four and KNOB 4 on an effect's first page the chord part's sends (on: FX on); no popups, the turned cell hot 800 ms |
| ENV | BASS | bass on/off | the knob row: KNOB 1–4 BEHAVIOUR REGISTER SOUND LEVEL as cells (no popups, the turned cell hot); BASS held + EDIT = the bass sound's editor, + SAVE its saving |
| LFO | LATCH | latch on/off (Sticky Keys; also holds a performed arp / pattern after the keys are released: Hold defaults off in every mode, an arp stops when the keys are up; Hold is STRUM's KNOB 4) | — |
| GLO | OPT | Options (picker pages); in the PLAY menu: its keys play / loops (no Options from there); in the KEY layer: its keys pick / play | + SAVE, both held 1 s: the revert (docs/SETTINGS.md "Manual and automatic"); shift (momentary): OPT + KNOB 1 split point, + SELECT metronome level, + PRESETS the chord part's engine, + ALGORITHM the bass part's engine (0.19: both wherever the knob's plain job is the part's sound, `cu_epk_ok`: the home pages, the Options, the layers KEY / PERF / FX / BASS / SEQ / LOCK / the view menu, and + ALGORITHM in the sound editor; 0.18: the home pages only. Released: the part lands on the place it last had in that engine's pool, `cu_epk_step` / `cu_epk_commit`. Not where the knob has a job of the place's own: the PLAY menu (the slot, the record mode), the loop editor (+ PRESETS 8 steps, + ALGORITHM the step's voicing Follow / 0: docs/LOOPER.md), + PRESETS in the sound editor (its picker); nor in the editor's picker and on the save dialog, which hold one sound: there OPT changes nothing about the two knobs. The table: docs/PRESETS.md). Until 2026-10-12 + ALGORITHM was the bass level: that is BASS held + ALGORITHM now (`cu_bass_level_hold`: the popup and no layer when none is open and BASS is not past the hold time; the BASS layer's Level cell once the layer is open); the other knobs keep their job |
| EDIT | EDIT | the sound editor (again: leave; in the engine picker: keep its sound, close it) | the engine picker (a preview: OCT− cancels, OCT+ keeps): SELECT the engine, KNOB 1 its presets, KNOB 2 init; the roots and the chord keys play (0.18: until then the white roots chose the engine unless KNOB 4 set them to play) |
| HOME | HOME | close the layer / page / menu (the view unchanged); on the view: next View **unless the view is locked** | **600 ms: the view menu** (`L_VIEW`, a picker: Chord · Keyboard · Notes · Geek Out · Scope by SELECT / the white roots, the last line "Lock: On / Off": KNOB 1 or the sixth white root; OCT+ ok, OCT− / HOME back; Settings v9 `view_lock`, the same view as Options > View) |
| SAVE | SAVE | save sound (naming; in it: save, as OCT+) | **the global save** (0.18, `cu_gsave`: at the hold time, alone: the settings block, the session and the loop in RAM if they changed; "saved"; while a loop plays or a capture is open: "saves at stop"). GLO + SAVE held 1 s: **the revert** ("reverted"). Until 0.17: save / load / delete loops |
| SEQ | SEQ | the loop editor (0.18; docs/LOOPER.md; the metronome on/off until then: now the PLAY menu's page 2, GLO + SEQ, and SEQ in its own layer) | the knob row: the time signature over Click (KNOB 1, hot; no popup) and three empty cells |
| PLAY | LOOP | play / stop | the knob row, no ring: slots on white roots, D#4 CLEAR (hold), F#4 UNDO; KNOB 1 the length picker, 2–4 Quantize Count-in Level cells (hot, no popups); playing: the action picker, OCT+ does it, Sync dim, the dial |
| REC | REC | record / overdub arm | undo the last layer (not a layer) |
| OCT−/OCT+ | | octave −2..+2; in a layer or picker: back / confirm (nothing to confirm: close) | **both: PANIC** (the LOCK latch cleared) |

Layer footers read "… · OCT-: back · HOME: home" (PERF / FX: "SELECT: mode · ARP: on/off · OCT-: back" / "SELECT: effect · FX: on/off · OCT-: back": the printed names); the emulator's log has `layer: open N (locked)` / `layer: close N`.

Knobs (`fm1_enc_take(panel.enc[role])`, one step per detent): KNOB 1 → `cr_voicing_step`,
KNOB 2 → `cr_bass_voicing_step`, KNOB 3 → the current perform mode's main parameter, KNOB 4 →
the selected effect's amount, PRESETS → sound ±1 (loads at once, as Felucca's preset_step),
ALGORITHM → bass sound ±1 (position 0 = OFF), SELECT → tempo ±1 (in a picker: move; on an
EDIT page: next page). Every knob turn also sets `ui.popup = {knob, until_ms}` so the view shows
the knob's meter/dial for 900 ms (§5) — except in the FX layer, a **knob row** (`CR_K_KNOBROW`, `cu_fx_cells`): the
effect's picker over KNOB 1–4's cells. Since 0.18 (docs/FX.md) the layer has eight pages (`CU_FXP`: `cs.fx_sel` the
effect, `cs.fx_page` its page, `CU_FXPG_SENDS` or `CU_FXPG_FILTER`; SELECT steps them, the label carries "1/2"; the
eighth, Filter Low cut High cut Slope Filter, is 0.22's master filter, docs/FX.md "The master filter"): Sends Drive Chorus
Delay Reverb (the chord part's four, as the editor's FX group: `cu_send`), Reverb 1 Size Damp Decay Send, Reverb 2
Pre-delay Mod Low cut Width, Chorus Rate Depth Width Send, Delay 1 Time Spread Feedback Send, Delay 2 Sync Mode Tone Wow,
Drive – – – Send. A cell is a global parameter (`cr_fx.c xfp[]`, or the chorus's `song.g`), or "Send" =
the chord part's send in per cent (`cu_send_text`, as the Sends page and the editor read it; "off" with FX off); Time and Spread are the synced or the
free parameter by Sync (`cu_fx_id`). Values with their units (`cu_fx_val`: "250 ms", "1/8.", "3:4", "x0.75",
"2.4 s", "Inf", "127 Hz", "Tempo", "Ping-pong", "0.80 Hz", else 0..127). Glyphs: room (Size), moon (Damp inverted,
Tone), tail (Decay), pre (Pre-delay), lfo (Mod, Wow, the chorus's Rate and Depth), cut (Low cut), width, echoes (Time
and Feedback: the time's position, feedback / 127), spread (two rows), sync (notes / a clock), ping (Mode), mix (a
send; Drive: clip). A turn changes the value as before and marks its cell hot for 800 ms (`cu_hot` {layer, knob, until},
cleared when a layer opens or another item is picked). The PERF layer (`cu_perf_cells`) and the BASS layer
(`cu_bass_cells`) are knob rows the same way: PERF's cells are the mode's `CU_PERF_KNOB` parameters, labelled as the
popups named them (Rate Division Direction Range Gate Swing Pattern Amount Hold), glyphs echoes (rate, division;
pct2 1.0), arrow (direction), range, gate, bar (slop amount, pattern), else text (the table in cr_ui.c); BASS's are
Behaviour (text: Chords / Unison / Single / Solo), Register (shift, -2..4 from `cr_snap.bass_voicing`), Sound (the
popup's number and name, "off"), Level (bar). Traces `perf: cells ...`, `perf: knob Arp division 8`, `bass: cells
...`, `bass: knob 4 level 84`; traces `fx: effect Delay`, `fx: cells Time Feedback Colour Amount`,
`fx: knob 2 Reverb damp 64`. KEY (`cu_key_cells`, `kr_band` 1: the keyboard strip as the band, the tonic lit yellow):
Tonic (text), Scale (Major / Minor), Transpose (shift, (v + 24) / 48, "+5"), Single (Full / Split Oct / Split Bass;
`cr_cell_t.value` is 12 bytes for it). **SELECT turns the KEY layer's page** (`cu.kpage`, page 1 whenever the layer
opens; no tempo there; the label ends "· 1/2" / "· 2/2", the footer starts "SELECT: page"): page 2 is Key plays
(Chords / Notes, KNOB 1) and Keyboard (Chords / Full, KNOB 2), KNOB 3 / 4 free (a dash); below. LOOP
(`cu_loop_cells`): Sync (range, index / 5, the length; `CR_CF_DIM` while playing), Quantize (echoes, index / 6, pct2
1.0), Count-in (gate 1.0 / 0.1), Level (bar). SEQ's (the metronome's): Click (bar, level / 100). Traces `key: cells ...`, `key: knob 3
transpose 5`, `loop: cells ...`, `loop: knob 2 quantize 1/4`, `metro: knob 1 click 60`. OPT + SELECT outside the
SEQ key's hold menu (metronome) still shows the click-level popup.

Pickers (`cr_picker_t { items, n, sel, on_change }`): SELECT or a root key moves `sel` (the white
roots index into the list), OCT+ confirms (`on_change` is already live for settings, so OCT+ only
closes), OCT− / HOME leave. Options is a picker of settings whose KNOB 1 edits the value of the
current one, **and the one picker that takes no key** (0.19, the owner 2026-10-12): SELECT alone moves
`cu.opt_sel`, the keys play as on the view (`cu_key_press` has no Options case, `cr_ui_pump` hands them
to the engine between strips, `cr_leds` draws the playing state), KNOB 2..4 / PRESETS / ALGORITHM fall
through to their view jobs as before; its footer reads "SELECT: move · OCT-: back · HOME". Until then
the white roots jumped to the first eleven entries and no key sounded. Test: `cr_opt_keys.txt`.

## 4. Timing

- The engine ticks in the audio ISR once per 128-frame block (2.9 ms) with `now_ms` from the
  sample counter (`frames * 1000 / 44100`), before `voice.c` renders, so a note scheduled for this
  block sounds in it; the UI thread never calls the engine's note paths. Key/button events from
  the main loop are posted to a small lock-free ring (`cr_evq`) drained at the top of the ISR tick
  (Felucca's `fm1_in` is already volatile and ISR-safe; `seq.c`'s `keyboard_block` is the model).
- UI frame every 15 ms (Felucca's loop): read input → post events → build the `cr_screen_t`
  from a snapshot of engine state (`cr_snapshot()` copies the few fields under `fm1__lock`) →
  `cr_draw(&screen, fm1_ms)` → `cr_leds()`.
- `cr_anim.c`: `cr_tween(from, to, t0, dur, now)` ease-out and a spring, integer; the screen
  builder uses it for the squeeze (`squeeze` = tween when the chord name changed), the picker
  slide, the meter fill, the stripes phase (BPM-locked). Options > Motion: full / calm (dur ÷ 2,
  no spring) / off (instant).

## 5. The view-model each frame (`cr_ui.c: cr_build_screen`)

Priority, top down, first match wins:

1. a message (panic, "SAVED", errors) → `big` block / `message`
2. a knob popup (within 900 ms of a turn) → `meter` in the knob's colour (`PLAN.md` §5)
3. an open layer → its screen: KEY, PERF, FX, BASS, LOOP, SEQ the `knobrow` (no popup over it: their knob turns never set one; KEY's band the keyboard); SAVE (loops) and the engine picker pickers; EDIT the `params` page
4. Options → the settings picker
5. the View: CHORD (`chord` with squeeze + notes line, `Key:` in the top line, `Rec`/loop status in the top line; the ring only for the count-in, the undo screen, recording / overdubbing and calibration; the corner dial (`dial_on`) while the loop merely plays — on every screen with a top line but Options, the LOOP and SAVE layers included), KEYBOARD, NOTES, GEEK OUT, SCOPE (Felucca's scope buffer)
   - **the arp screen** (`arp`, `cr_draw.c cr_p_arp`: the chord's name, its notes on a line, the sounding note on a
     block hopping along with a dotted arc to the next, the rate line "arp 1/8" / "pattern alberti") is the **Chord
     view's content while an arpeggio plays**, and nothing more (`cr_ui.c cu_arp_on`; b25168b had removed it from
     every view; back 2026-10-10 under this rule). All of: the view is Chord (Keyboard, Notes, Geek Out and Scope are never
     replaced); Perform is on in a **clocked mode** (Arpeggiate, Arp 2 Octaves, Pattern; Strum, Slop and Harp run once
     over the chord and keep the chord view); the shown chord's voice is being stepped (`cr_snap.perf_step` =
     `cr_engine.c cr_perform_stepping`: the displayed voice's clock is on, from the press, before the first step of a
     Retrig-off arp, to the voice's end). Options > Motion does not gate it (until 2026-10-10 Off kept the plain chord
     view: the owner's unit had Motion Off and he saw no arp screen; the hop is the arpeggio's state, one frame a
     step, not a tween, so it shows with Full, Calm and Off alike; Off only drops the stripes' sweep). It ends with the voice: the keys up without
     LATCH / Hold, Perform off, PANIC, the loop that played it stopped (a playing loop's chords arpeggiate with no key
     held: the screen follows them; LATCH or Strum-style Hold keeps it). `CU_ARP_LINGER_MS` (150 ms): the screen
     stays that long after the voice ended, the hop gone (`pos` -1), so a chord re-fingered does not flash the big
     chord view between two chords; Perform off does not wait. The steps above it (messages, the knob
     meter, layers, pages, Options, the PLAY menu, the editor) and the stripes / the black screen keep their
     precedence; the top line is the view's (the mode, `Key: C`, the loop's text and dial). Transitions:
     `cu_animate` sweeps the idle stripes off for either screen; chord <-> arp is a plain redraw (no squeeze: a chord
     name that did not change is drawn as it stands when the arp stops; a changed one squeezes from the old); a chord
     change inside the arp screen redraws the name and the notes at once. The draw cache: `pos` is hashed apart
     (`cr_draw.c cr_arp_strips`), a hop composes only the strips from the arc to the notes' line (3 of 6, not the
     name's): tests/cr_draw_test.c "arp: ...". Tests: `cr_arp_view.txt` (test_cr.sh "the arp screen": every case
     above from the frames between its dumps), `cr_arp_unit.txt` (the owner's settings record of 2026-10-10 seeded into
     a flash file by `tools/emu/seed_settings.py`: Key Mode, one root alone, Arpeggiate, Motion Off), `cr_arp_hold.txt`, tests/cr_engine_test.c "stepping: ...".
   - **the PERF layer's band** (the owner, 2026-10-10; `cr_ui.c cu_arp_layer`, `cr_draw.c cr_kr_arp`, `kr_band` 2):
     the perform layer's top half is the arp while notes are stepped, by the arp screen's own rule (`cu_arp_run`:
     Perform on in a clocked mode, the shown chord's voice stepping, the 150 ms linger; Motion does not gate it)
     whatever the view, the knob row under it untouched. **The picker comes first**: for `CU_PERF_PICK_MS` (2 s)
     after the layer opens and after every detent of SELECT (`cu.perf_t0`; a detent at the list's end counts) the
     band is the mode picker, so a mode switched under a latched arp is read; KNOB 1–4 do not bring it back (their
     cell turns hot under the arp). The band (98 px over the row, a footer under it): `label` top left carries the
     mode ("Arpeggiate · on"), the chord's name 20 px (baseline row 60), the notes 13 px on a line 26 px over the
     row with the rule under them, the sounding one 15 px on its block, the dotted arc; no rate line (the Division
     cell says it). A hop composes strips 1 and 2 only (rows 60..114; the row starts at 126: `cr_arp_strips`),
     3.7 ms device estimate a hop (4.1 the worst; the Chord view's hop: 3.6), the frames between draw nothing, as
     the layer's picker does. Tests: `cr_perf_arp.txt` (test_cr.sh "the perform layer's band"), tests/cr_draw_test.c
     "knob row arp band: ...". No mock-up: the designer's knobrow has no such band.
6. the splash and the screensaver → `stripes` (the ChoralRoot logo): at power-on (the splash, CR_SPLASH_MS with the
   version, the intro slide once) until the first input; then only as a screensaver after **3 minutes without any
   input** (`CR_IDLE_MS` 180 000, fixed: keys, buttons, knobs and MIDI channel messages in are activity, `cu.last_input`;
   a loop playing alone or a chord still ringing is not). Any input brings the view back at once. Before 2026-10-09 the
   stripes came back 3 s after the last chord stopped sounding (and after every PANIC), which read as the logo popping
   up all the time. Test: `cr_panic.txt` (4 s: no stripes; 3 min: the stripes; a key: gone).
7. the screen off: after **10 minutes without input** (`CR_SCREEN_OFF_MS` 600 000; both timeouts are constants in
   `cr_ui.c`, no setting) the screen goes black: **one all-black frame** written to the panel (`cr_draw.c
   cr_draw_black`, a 240 x 240 `lcd_fill`), then nothing is drawn (no LCD writes: the bus is quiet). **The backlight
   stays on and the panel stays on**: "off" is black pixels, nothing more. The UI keeps ticking: the LEDs, the looper,
   the arp, the metronome and MIDI go on (none of them is input: a long loop playing alone lets the screen go black,
   the LEDs show it). The panel is a TFT (IPS), not an OLED: the "burn" is image retention, which a black frame ends
   (the backlight's heat stays). Any key, button, knob or MIDI channel message in wakes it: the whole current view
   redrawn at once (the strip cache invalidated: all six strips whole, no sweep off the logo); the press or turn that
   wakes it also acts as usual (a key plays, a knob changes its value: nothing swallowed). The same in SAFE MODE and
   on the Options pages. A full-screen fill from `main.c` (the crash screen, UBOOT, an update) draws on the lit panel
   as always. **SysEx 74 SCREEN** (`cr_backup.c`, web/EDITOR_PROTOCOL.md; `tools/fm1_install.py --screen off|on`)
   blacks it by the same path until `SCREEN 1` or a key, button or knob on the unit; MIDI in does not wake a screen
   switched off this way (a test driving the unit over MIDI keeps it black).

   **Why not the backlight: PA2 is not the backlight alone.** 0.14 and 0.15 switched the screen off for real: DISPOFF
   (ST7789 0x28) and the backlight off, PA2 driven high (`lcd.c lcd_power`, `fm1_lcd_backlight`; both gone). That was
   verified on the host only. On the unit (2026-10-10) the FM-1 then went deaf: no key made a sound, no key, button
   or knob woke the screen, and only a `74 SCREEN 1` from the computer brought the screen back, and the keys with it.
   Independently, the boot stub, which runs before `fm1_lcd_hw_init` has pulled PA2 low, cannot see a held key
   through the 595 chain, while the app reading the same pins the same way after its LCD init can. So: **PA2 low =
   the backlight and the key matrix / 595 chain (keys, buttons, knobs, LEDs) alive; PA2 high = both dead** (very
   likely one enable of the supply that feeds the panel, the 595s and the LEDs). `hal/fm1_lcd_hw.h` drives PA2 low
   once, in `fm1_lcd_hw_init`, and has no way to raise it; nothing else writes that bit (`hal/fm1_input.h`, the
   only other writer of PA's OUT and DIR, sets and clears its own bits 1, 3, 4, 9, 10 with read-modify-writes: at
   its init, then from the timer ISR alone, so no main-loop write of PA is left to race it and no stale PA2 = 1 can
   come back). DISPOFF is not sent either: what this panel shows with the display off and the
   backlight on is not known; black pixels are. The emulator carries the fact (`tools/emu/emu_hooks.h` `emu_hal.pa2`:
   high, the panel is black, no LED lights, no key, button or knob is read, and the dump's `pa2:` line says so), so a
   screen off that touches PA2 fails `test_cr.sh` as the 0.14 one does.

   Tests: `cr_screen_off.txt` in `tools/emu/test_cr.sh` (3 min the logo; 10 min every pixel black, PA2 low, no LCD
   write for a minute, the LEDs lit; a key wakes it and plays, a knob wakes it and turns, a loop alone does not keep
   it on, MIDI in wakes it; SCREEN 0: MIDI in plays and does not wake it, a key does; SCREEN 1; no pixel of the black
   frame left after a wake), `cr_pa2.txt` (the emulator's PA2 model), `tests/cr_backup_test.c` (SCREEN: one write,
   all black, the wake redraws all six strips whole), `tests/cr_draw_test.c` (`cr_draw_black`),
   `tests/install_test.py`.

### Key plays, Keyboard, Split Bass (0.17, the KEY layer's page 2)

- **Key plays** (`cs.key_notes`, `CRE_KEYPLAYS` -> `cr_set_key_plays`): Chords (default) / Notes. Notes: Key Mode on
  resolves no chord (`crx_build_base` skips the KEYMAP) and `cr_key_ex` drops a press whose pitch class is not in
  `cr_scale_mask` before anything else: no voice, no note on any stream, no gesture (so no loop event), `phys` not set
  (its release finds nothing), a latched chord not replaced. Chord keys as with Key Mode off.
- **Snap** (0.22, GitHub issue 18; `cs.snap`, `CRE_KEYSNAP` -> `cr_set_key_snap`, `cr_keysnap_t`): Off (default) /
  Scale / Chord, KNOB 3 of the page (grey, `CR_CF_DIM`, unless Key plays Notes: it applies only where that guard
  does). With it the guard takes `cr_snap_note` instead of dropping the press: the nearest note of
  the target (Scale: `cr_scale_mask`; Chord: the base notes of every used voice with more than one, each taken back
  by its own captured transpose, the scale's when none), d = 1 .. 11 down before up (ties go down; a chord tone the
  key already is plays as itself). The raw key keeps its place in `phys` (latching's "every key lifted",
  `crx_rec_commit`), the voice is the played note's (`crx_find_voice` by it: a second key onto a sounding note
  retriggers it, as a second press of that note would), `snapn[raw]` = played + 1 (`snapn_n` of them) tells the
  release which note to end whatever changed meanwhile; a note another held key plays, as itself (`phys`) or snapped
  onto it (`crx_snapped_to`), stays until the last of them is up; `crx_latch_sync` counts a snapped key as held. The
  streams, the gesture (the looper), the bass and the performance modes see the played note. The LEDs: `cr_leds`
  glows the roots out of the scale faint (`nf`) while Snap applies, Glow only. `tests/cr_engine_test.c s_snap`,
  `tools/emu/scripts/cr_key_snap.txt`.
- **Keyboard** (`cs.kbd_full`, `CRE_KEYBOARD` -> `cr_set_keyboard(full, split_note)`): Chords / Full. Full: every
  key 0..26 posts `CRE_KEY` 53 + k + 12 x octave (`cu_key_press`; `cu.key_note[k]` set says "pressed as a note" to
  the release and to `cu_mods_phys`); B3 never pends (no LOCK tap, no LOCK layer); the engine marks the voice
  `CR_Z_FULL`: spec 0 / 0 whatever `mods_down` holds (LOCK's latch stays posted down and is back when the mode is
  left), no voicing rotation for a note, no Key chord. **Since 0.22 Key plays rules there as on the Chords keyboard**
  (the owner, 2026-10-15, after Snap): with Key plays Chords `crx_build_base` resolves the KEYMAP for a `CR_Z_FULL`
  voice (the key's chord, the alternate chord off the scale; `crx_voic`: such a chord takes `c->voicing` and the
  live re-voice, a note still nothing), with Key plays Notes the guard and Snap, as on the Chords keyboard; until
  then the Full keyboard was notes with the guard whatever Key plays said (`cr_key_ex`'s guard read
  `key_notes || full`). KNOB 1 on the view
  steps `cu.octave` (popup "octave"). The KEY layer on pick still owns the keys (`cu_key_pick`), B3 inert there.
  Step entry's "every key up" ignores the latch in this mode (and a root pressed after one was let go is the next
  step whatever is latched: docs/LOOPER.md "Step entry").
- **Single** has three values (`cr_single_t`): Full, Split Oct (`CR_SINGLE_SPLIT`, as before), **Split Bass**
  (`CR_SINGLE_SPLIT_BASS`): a key below the split is a `CR_Z_BASS` voice: a single note on the BASS stream alone at
  the played pitch + transpose + 12 x the bass register, whatever Bass on / off says; every other voice plays the
  chord streams alone and gets no bass (`crx_bass_wanted`, `crx_chord_streams_ok`: Bass on / off and the Behaviour
  are kept and ignored meanwhile; the header's right text says "Split Bass"). **The split**: in the Chords layout the
  pitch class `split_pc` for both (Split Oct exactly as before); on the Full keyboard a key, F4 (`CU_SPLIT_FULL`,
  key 12: F3..E4 left, F4..G5 right), fixed, posted as the MIDI note it plays (`cu_post_kbd`, again at every octave
  change): Split Oct there lowers the left side an octave (`CR_Z_LOW`).
- **In a loop (0.18).** The gesture of a `CR_Z_BASS` voice carries `CR_GX_BASS` in its `ext` (`crx_gesture`); the
  looper stores it in the event (`crl_ev_t.qx`: a single note's nibble, `CRL_X_BASS`) and plays it with
  `cr_loop_event(.., ext = CR_GX_BASS, ..)`: a loop voice with `zone = CR_Z_BASS`, so the same three functions route
  it (`crx_chord_streams_ok` 0, `crx_bass_ok` = the BASS stream on, `crx_bass_pitch` = root + 12 x the bass register
  of now; no voicing rotation). The side is the one in force when the key went down, never the one at playback;
  until 0.17 such a note was recorded as a single-note gesture and came back on the chord part. The record:
  docs/LOOPER.md "What is recorded" and "Slots and the record" (flags byte 7, `CRL_RF_PART`; the size and version
  unchanged). MIDI out: the bass side, live and looped, goes to the BASS stream's channel. Tests:
  `tests/cr_loop_test.c s_bass`, `cr_loop_bass.txt`, `loop_bass_persist_check.txt`.
- Settings v14: bits 1 / 2 of `key_on`, `single` = 2 (docs/SETTINGS.md). Tests: `tests/cr_engine_test.c s_keyplays`,
  `cr_key_page.txt`, `cr_fullkbd.txt`, `persist_page2_*.txt`, `tests/cr_draw_test.c` (the two pages).

## 6. LEDs (`cr_leds()`)

**The hardware's levels** (`hal/fm1_input.h`): lit (the column's whole tick, ~95 us of each 1.1 ms frame), and two
pulses per frame riding on the 595 shift of the next column: `fm1_led_dim` (`FM1_LED_DIM_NS` 3.2 us, ~1/30 of lit:
the glow) and, new in 0.17, `fm1_led_dim2` (`FM1_LED_DIM2_NS` 9.6 us, ~1/10 of lit: the middle level; a pulse longer
than the whole shift is waited out, the tick's one wait) and, under the glow, `fm1_led_faint` (`FM1_LED_FAINT_NS`
1.3 us, ~1/70 of lit: the idle chord keys; it grows from the floor, the two writes of the lines back to back, one
bit of the shift at a time, and a target between two reachable widths is dithered frame by frame; console `inp`:
`faint_pulse_ns`, `faint_bits`: 0 bits with a pulse over the target = the floor is longer than the target). Four
levels: faint < dim < dim2 < lit; an LED in several bitmaps is the brightest. The three widths are tuned there. The
emulator: `emu_hal.led_mid` / `led_faint`, `expect led K on|mid|dim|faint|off`, the dump's `leds mid:`, `leds faint:`
and `key leds:` lines (per key 0 off 1 faint 2 dim 3 mid 4 lit).

Who uses which level (`cr_leds`, Options > LEDs = Glow): **faint** (`nf`): the eight chord keys and B3 when idle
(not held, not latched, LOCK off); **dim** (`nd`): every idle button, a note of the scale with Key Mode on, the KEY
layer's MAJ / MIN "half", a layer's map on the roots (PLAY's / SAVE's slots, the list layers' entries, the KEY layer's
tonic picker with Key Mode off; not the Options, which have none since 0.19), a playing loop's notes (`ld`); **mid** (`nm`): the tonic's
keys; **lit** (`nl`, `own`): what sounds, is held, latched, on or selected. Key Mode off: the roots have no idle
level at all (dark; the Full keyboard: all 27). Stock: unchanged: `nf`, `nd` and `nm` are all "idle" = lit, and with
Key Mode off every root is still put in `nd` there (lit, a sounding one dark).

As `ui_input.c: ui_leds` builds `fm1_led` / `fm1_led_dim` per column: every key and button dim
(the glow; the chord keys and B3: faint; the roots with Key Mode off: dark) unless Options > LEDs = STOCK; chord keys lit while held/latched (`cr_mod_active`; LOCK's latched keys
are posted down, so they are lit); B3 lit while LOCK is on (dim off); **in the KEY layer only** (the owner's
exception to "the chord keys' LEDs are their own state", 2026-10-10) the chord keys are its scale keys: MAJ and MIN
at half, the selected scale's at full, the six without a job dark. Glow: half = `fm1_led_dim` (the glow pulse),
full = `fm1_led`. Stock has no half level (it lights the idle keys and darkens the active ones; a layer's selected
key is lit through `own`): the selected scale's key lit, the other and the six dark (`cr_key_scale.txt`); with the
layer's keys on play (below, OPT lit) the chord keys and the roots show their own state as on the view;
**a sounding note lights every key that plays of its note name** (0.17, the owner: "if the chord has a G in it, the
two G's should light up (don't light up the chord keys)"; until then a note lit the one root key of its pitch, so a
chord voiced, transposed or shifted out of D4..G5 lit nothing). `cr_leds` builds a 12-bit mask `pcs` of the note names
that sound from what the snapshot already holds, no new field: `cr_snap.ci.notes` (the shown chord: the player's last
one, as voiced, with the KEY layer's Transpose: the engine's notes, so what sounds, not the key under the finger;
only while `ci.sounding` and not a loop voice's, `ldisp`), on the Full keyboard also every held key's note
(`cu.key_note` + Transpose; Key Mode: not a key out of the scale). Every key that plays (`key0`..: the 18 roots; the
Full keyboard: all 27) whose name (`CU_KEY_PC`) is in `pcs` is lit (`nl`); a chord key or B3 in the Chords layout
never is. The performed note (`perf_note`) blinks by name: its bit leaves `pcs` in the blink's dark half, so both
keys of the stepping note blink and the rest of the chord stays lit. A playing loop's notes (`lnote`, 16 bytes
scanned, zero bytes skipped) give `lpcs` the same way: the keys of those names glow dim (`ld`) where the player's are
not lit. Key Mode's scale lights lie under it (`nd` / `nm`: `fm1_led_dim2` is `nm & ~nl`). Stock inverts the same keys.
Unchanged: which notes count (the shown chord only: an older chord still held with it does not light; the bass
part's own note never lit and does not; a part's TRANSPOSE on the editor's MIX page is not in the lights, as it is
not in the note names on screen or in the MIDI out), the layers with a map on the roots, the on-screen keyboard of
the Keyboard view (exact pitches). Tests: `tools/emu/scripts/cr_note_leds.txt` (D major in both octaves and no chord
key, KNOB 1 +8 / -8, Transpose +5, OCT+ twice, Key Mode, the arp's blink), `cr_fullkbd.txt` (the Full keyboard),
`cr_key_leds.txt` (Stock), `cr_loop_glow.txt` (the loop's glow),
**Key Mode's lights** (0.17: `fm1_led_dim` < `fm1_led_dim2` < `fm1_led`): Key Mode off:
every key that plays is dark until it sounds (it glowed dim until 2026-10-12); on: the scale's notes dim, the tonic's keys at the middle level (`nm` ->
`fm1_led_dim2`), the keys out of the scale dark (until 0.17 only the black ones went dark: the loop skipped
`cu_black(k)` keys alone, so every white key glowed whatever the key), a sounding note lit over it. Where: the view,
the editor, and the layers whose roots play (PERF, FX, LOCK, PLAY with Keys = Play, the engine picker (always, since
0.18), KEY with keys: play), and the KEY layer on pick (its roots pick a tonic: the same picture; Key Mode off
there: all dim). Not in the layers with their own map on the roots (PLAY's and SAVE's slots, BASS / SEQ / the view
menu / the engine picker's lists, naming; the Options had one until 0.19: they show the playing state now). Stock (two levels): the scale's keys lit, the tonic as the
rest, the others dark, a sounding one dark. The Full keyboard: all 27 keys carry that picture and every held key
that plays is lit; the chord block's LEDs are not drawn. `cr_key_leds.txt`, the dump's `key leds:` line.
layer maps on the roots while a layer is open; KEY,
PERF, FX, BASS, LATCH, SEQ lit when on; **the open layer's button blinks** (locked after its hold; SAVE while
held); EDIT blinks while the editor is open; REC blink while recording, lit when armed; PLAY lit when a loop
exists, its green LED when playing; OPT lit in Options; OCT− lit / OCT+ blinking in layers and pickers.

## 7. Settings and sounds (`cr_settings.c`)

Felucca's settings record (`settings_persist.c`) grows a ChoralRoot block: play style, extension
addition, secret scope, key mode + tonic + scale, transpose, single notes, velocity, bass
behaviour + register + sound, perform mode + params, latch, tempo, loop sync/quant/count-in/level,
MIDI channels/enables/clock mode, view, motion, palette (MOD default), LEDs. Since 0.18 saving is manual
(docs/SETTINGS.md "Manual and automatic"): what the Options menu alone sets is saved on change, when the unit is quiet
and never while a loop plays (Felucca's `settings_poll`); everything else by the global save (SAVE held) alone. Sounds (docs/PRESETS.md, 2026-10-07): **one editable pool per
engine** (`cr_bank.c`): 00 INIT, the engine's factory presets (each replaced by the user's record bound to it: an
overwritten factory preset, resettable), then the presets the user added; PRESETS turns the chord part's engine's
pool, ALGORITHM the bass part's (00 OFF before it), OPT + PRESETS changes the chord part's engine (each part remembers
its place per engine: settings v6), SAVE offers Overwrite / Save as new. The user presets are the 32 slots of
`upreset.c`; a record's binding is two of its pattern bytes (`note[15] = 0xA6`, `flags[15]` = factory index + 1 or 0;
docs/SOUNDS.md). Names typed with the root keys as Felucca's `ui_name.c`. The curated 48-row bank of before is gone;
its trims stay as a table by (engine, preset), its PIANO is FM6's F25.

- **VA** (engine 13, `eng_va.c`, `FELUCCA_VA`; docs/VA.md): a four-oscillator virtual analog with deep pages
  (`eng_deep_t`: OSC / FILTER / ENV / LFO / MOD, 32 pages; oscillator MODE BASIC / MORPH / NOISE, FILTER MORPH and
  a stereo SPREAD, UNISON's USPREAD), its own patch per part (P_E0..P_E7 are macros into it, blob version 2), 19
  presets in its pool (docs/PRESETS.md; before: 19 chord sounds and 4 basses of the curated bank), levels set in the
  presets (trim 0). A user slot saved from a VA sound keeps its patch in `va_store.c` (one per slot, on the unused
  project sectors 0x97000 / 0x98000; a version-1 store is imported at boot). The VA is the one engine with a stereo
  voice path (`engine_t.render2`: a mid + side per part, `fx.c mix_part`).
- **TRANSPOSE** (MIX page, P_TRANS -24..24, both parts): applied in `cr_out.c` where the streams enter the parts
  (clamped to 0..127); the MIDI out carries the notes as played. A change applies from the next chord after the part's
  notes are released (each note-off ends the note its note-on started); `tests/cr_trans_test.c`, trace `transpose:`.

## 8. The looper (`cr_loop.c`, M6)

Semantic events (`design.md` §14): root, resolved quality, extension mask, velocity, on/off
time in ticks of the master clock; layers with undo; free or 1/2/4/8/16-bar sync with a one-bar
count-in; quantize on commit; playback through `cr_key`-equivalent internal calls so voicing,
performance and bass apply live; ten flash slots (`storage.c`). The ring position = `(now −
loop_start) / loop_len`.

## 9. Order of work

1. `cr_out.c` + `choralroot.c` + the shims: Felucca builds on the host with the engine driving
   part 0 from `cr_engine` fed by the emulator's keys (no UI yet: the chord sounds).
2. `cr_ui.c` grammar + `cr_build_screen` + `cr_leds`: the CHORD view, layers, pickers, popups.
3. settings, sounds, EDIT pages (Felucca's `PAGES` tables for the parameters), naming.
4. the looper. 5. the device build and the installer identity.

## Status (2026-10-05: steps 1-4 of section 9, on the emulator)

`sh tools/emu/test_cr.sh` (headless, deterministic) passes; `sh tools/emu/test.sh` runs it too. Screens:
`build/emu/test/cr_*.ppm` (`cr_dmaj7.ppm` matches mock-up state 2). 2026-10-06, BASS tap (`cr_bass_both.txt`): the bass
meter popup (mock-up 14), "Bass" in orange top right while it is on ("Bass Solo" when Bass Behaviour is Solo, which
silences the chord part by design and is stored in the settings; status order: Bass Solo, the perform mode, Bass,
Oct, Latch, lock); both parts sound with the bass on (Chords Only), a bass sound change keeps a held chord, BASS off
leaves part 0 alone.

2026-10-07, CZ-1 (docs/CZ1.md): Melodee's CZ-1 engine (14) with Casio's 64 tones, the eight banks, Casio tone SysEx,
its deep pages under the cz band and the tone store; `cr_cz.txt`, `cz_persist_*.txt`, `perf.sh (g)` and
`tests/cr_cz_test.c` pass.

### Wired

- **Boot guard and SAFE MODE (2026-10-07, `firmware/src/cr_bootguard.h`, docs/INSTALL-COMPAT.md)**: the guard reads
  the reset reason; a power-on clears it, only watchdog / soft (crash) resets within 30 s of a boot count. Two in a
  row: SAFE MODE (no flash object loaded or saved: factory sounds, default settings, no loops; no USB audio function, the
  serial console presented instead (`usb.c` `usb_cdc_on`, docs/USB-AUDIO.md);
  the installer, backup reads and the update gesture (OCT- + OCT+ + HOME 5 s; until 0.17 the two OCT keys) still work; a yellow SAFE MODE screen with the crash's boot stage,
  "Safe mode" on the top line, Options > Safe Mode and Flash Data: erase and reboot, OCT+ twice). SAFE MODE crashing
  twice more: UBOOT, as before. Every boot step sets `felucca_dbg.stage` (console `boot`, GEEK OUT's third line).
  Tests: `tests/cr_bootguard_test.c`; `tools/emu/scripts/cr_safe*.txt` (`--boot-fail`, `--reset-reason`,
  `--boot-stage`; shots `build/emu/test/cr_safe_*.ppm`).

- **USB audio (0.14, docs/USB-AUDIO.md)**: Melodee's UAC1 recording function in `usb.c` / `usb_audio.c` /
  `usb_audio_stream.c` / `usb_audio_desc.h`: "ChoralRoot In" (six channels: the master before the click, the CHORD
  and BASS parts' dry stereo at -6 dB: `fx.c` `ua_stage`, `choralroot.c`'s `mix_block` shim). Melodee's playback
  function ("ChoralRoot Out" in the dev builds) is removed. The endpoint is served from TIMER5 (`main.c`
  `ua_service`, nested in the render); `audio.c` copies a block with the IRQs off. Options > USB Record Off leaves
  the recording out and presents the serial console instead (the FM-1 replugs itself, `usb_replug`; both need EP2
  IN). Options > USB Level = Fixed (Felucca 1.0.5): USB at the full level, MASTER
  scales the DAC after (`fx.c` `usb_fixed_dac`). The emulator has no USB (`FELUCCA_UAC` 0 there): the settings only.
  **48 kHz (0.19)**, SLOOP 2.5's after Felucca 1.1.5, for six channels: the format lists 44100 and 48000 Hz and the
  host's `SET_CUR` on the endpoint picks (`ua_rate_set`); at 48 kHz `audio.c` resamples each staged block x 160 /
  147 before the ring (`ua_block48`, `usb_audio_stream.c` `ua_resample`, SLOOP's 12-tap polyphase table
  `uac_fir.h`), about 185 us a half counted from the listing; 44.1 kHz is bit for bit as before. `bcdDevice` 3.23,
  `wMaxPacketSize` 688; SysEx 79 USB has the rate and the 48 kHz counters. Nothing in `fx.c` changed.
  **Opt-in (2026-10-12): Options > USB Rate**, `44.1 kHz` by default: `ua_off`'s `UA_OFF_48` bit, with which
  `ua_cfg_build` writes 0.18's configuration (one rate, 640, 208 bytes) and `get_desc` answers `DEV_DESC44`
  (`bcdDevice` 3.22), `SET_CUR` leaves the rate at 44100 and `ua.r48` is never set (so `audio.c` is never asked to
  resample). A change replugs through `ua_off_set` / `ua_off_apply` as USB Record does, and `ua_off_apply` now
  waits while SysEx comes in (`usb.sx_ms`, `UA_REPLUG_QUIET` 1.5 s). `tests/cr_usbrate_test.c`.
  Felucca 1.0.5's BASS+ fix (#42) came with it: `spk_bass`'s low-pass has 4 poles.

- **Install guard (2026-10-07, docs/INSTALL-COMPAT.md)**: both installers classify the running firmware (identity + INFO) and refuse Sloop, the Felucca 0.x betas and unknown ones (`--force` / an "I understand the risk" box); the loader is not the cause; the cause is still open (the emulator boots fine on Sloop and 0.9 data).

- **FM6 (2026-10-07, docs/FM6.md)**: Melodee's engine and voice model; the deep pages (OP n, OP n+, SCALE n, ALGO,
  ENV n, PITCH EG, LFO, FUNC, CTRL) in the editor with the dx envelope band; the patch and its function settings in a
  user slot (`fm6_ustore.c`); DX7 SysEx in / out, bank dumps to B1..B32; the bank's FM6 rows on F1..F8 (levels as
  before, trims 0); `FM6_POLY` 8 (perf.sh (c) 30 / 39-42 %, at 16: 46 / 59 %). Tests: `tests/cr_fm6_test.c`,
  `tools/emu/scripts/cr_fm6.txt`, `fm6_persist_*.txt`, the FM6 goldens re-made.

- **The unit** (`choralroot.c`): Felucca's order and build options, `FELUCCA_ID "FM-1_920"`, `FELUCCA_VERSION
  "ChoralRoot 0.1"`, `FELUCCA_SLICE=0`. It passes `cc -fsyntax-only -w -Ibuild/gen -Ifirmware/src -Ifirmware/hal
  firmware/src/choralroot.c` (on macOS `__attribute__` is defined away: Mach-O has no `.noinit` / `.pool`), and with
  the real attributes for `--target=armv7-none-eabi -ffreestanding`; lowered to LLVM IR for that target it leaves the
  same two external symbols as `felucca.c` (`isr_alnk0`, `isr_timer5`, from the `.S` files).
- **The shims** (no kept file edited): `cr_param_t` is in both `cr_engine.h` (the perform parameter enum) and
  `cr_screen.h` (a params column): the unit includes the engine with `#define cr_param_t cr_eparam_t`. The engine
  ticks through a `mix_block` shim (`fx.c`'s is renamed `fx_mix_block`; ChoralRoot's calls `cr_audio_block` then it),
  so `audio.c` is unchanged. `cr_shim.c` gives `main.c` the names of the dropped UI (`ui`, `ui_input` / `ui_leds` /
  `ui_draw` -> `cr_ui_*`, `felucca_init`'s sound calls, `persist_boot`, `settings_save` / `settings_poll`,
  `panel_setup`, `ed_service`, `ui_message`); `cr_ui_init` runs at the first `ui_input` (the ISR waits on
  `cr_ready`).
- **`cr_out.c`**: MAIN -> part 0, BASS -> part 1 (`V_MONO`), RAW MIDI-only (or part 0 too: Options > Raw Chord
  Sound); USB-MIDI packets as `seq.c`; each note's channel and part remembered for its note-off; `all_off`: the part
  released and CC 123 on the stream's channel (the engine's panic calls it for all three). 24 PPQN clock out from the
  sample clock (Options > MIDI Clock: Out). The input queue (`cr_post`, 128 events, one producer / one consumer)
  drained at the top of every 32-sample block, then `cr_tick` with `ms = samples * 1000 / 44100` (remainder kept);
  `cr_snapshot` copies what the UI shows with the IRQ off.
- **`cr_ui.c`**: the grammar of section 3: chord keys, LOCK, roots (+ OCT), the tap / hold / combo gesture
  (one armed button, as `ui_layer.c`; a hold locks its layer open), OCT- + OCT+ = panic (octave reset), OCT in
  layers and pickers = back / OK. Taps: KEY PERF FX BASS LATCH OPT EDIT SAVE SEQ; layers: KEY (select-key, MIN held = minor, KNOB 1-3
  tonic / scale / transpose), PERF (7 modes on the white roots, KNOB 1-4 the mode's parameters), FX (Reverb Chorus
  Delay Drive = part 0's sends, KNOB 1-3 the buses' `song.g` parameters, KNOB 4 the amount), BASS (behaviours,
  register, sound, level), EDIT (the engine picker; KNOB 1 the engine's sounds). Knobs: KNOB 1 voicing, KNOB 2 bass
  voicing, KNOB 3 the mode's main parameter, KNOB 4 FX amount, PRESETS the chord sound (Felucca's factory presets of
  the melodic engines, POLY), ALGORITHM the bass (presets named BASS / ACID; 0 = OFF), SELECT tempo (in a picker:
  move); OPT + ALGORITHM the bass part's engine (wherever ALGORITHM alone is the bass sound: `cu_epk_ok`, docs/PRESETS.md), BASS held + ALGORITHM the bass level. Options: a picker of 24 settings (USB Record and USB Level since 0.14, USB Rate since 0.19, docs/USB-AUDIO.md; Dual Core On / Off since 0.15, Off by default from 0.16 to 0.21, On since 0.22, at the next power-on, docs/DUALCORE.md), KNOB 1 sets. `cr_build_screen` in the order
  of section 5 (PANIC / message, knob meter 900 ms, layer, page, Options, the stripes (the splash; the screensaver after 3 min without input; the screen off after 10 min), the View: CHORD,
  KEYBOARD, NOTES, GEEK OUT, SCOPE; perform on never swaps the chosen view: it shows in the PERF LED and the top
  line's right text, and on the Chord view an arpeggio or a pattern playing shows the arp screen in its place:
  section 5); `cr_leds` as section 6.
- **Sounds** (`cr_bank.c`, `cr_pages.c`, `cr_name.c`; section 7, docs/PRESETS.md): PRESETS turns the pool of the
  chord part's engine, ALGORITHM the bass part's (after OFF); OPT + PRESETS the chord part's engine (a horizontal
  picker while OPT is held, applied on release). The meter: the place big, the name (a square after an overwritten
  factory preset's), a stripe per preset (INIT not counted); on the home pages the engine over the number in orange
  and "Hold OPT to change engine" under the stripes (the bass part: its own line), elsewhere "FM6 · 05/25" under
  them (docs/PRESETS.md).
  **Its fill goes on from the place shown** (0.17, the owner: "it should just animate from previous index to the
  current index"; until then every detent filled from none, because the label carries the place and `cu_animate` took a
  changed label for a new meter). The read-out carries its pool (`cu.pop.pool`, `CU_POOL_ID`: the part and its
  engine); `cu_animate` keeps, per part, the pool its read-out last showed and the fill (`ca.pool_of`, `ca.pool_pct`).
  The same pool: `pct_from` is where the stripes stand: mid-fill, the stripe they have reached (`cr_draw.c cr_fill_at`,
  the function `cr_frame` draws with) and one more towards the new place, so a knob turned faster than a stripe per
  30 ms is followed although every step restarts the fill; the read-out gone and shown again, or another meter in
  between: the place it last showed. Up lights the stripes between, down darkens them, at the meters' pace
  (`CR_FILL_MS` 30 ms a stripe; more than `CR_FILL_MAX` 14 to go, a wrap or a long jump: all in 420 ms, Calm 210 ms,
  inside the read-out's 900 ms). From none: the part's first read-out since power-on, and another engine's (OPT +
  PRESETS, the engine picker's landing). The bass OFF (`00 off`) is the place before the pool's
  first: it has the pool's stripes (it had twelve), all dark, so leaving a preset for OFF darkens its stripes and the
  next preset fills from none. The engine picker's preview (EDIT held, KNOB 1) still jumps (`cu.pop.jump`) and leaves
  its place for the next read-out. The other meters keep their rule: the same label, the fill from the value before; a
  new label, from none. Tests: `tools/emu/scripts/cr_meter_step.txt` (every frame's lit stripes, `EMU_UI_LOG=0`: the
  emulator's `fill F of N from A to B`), `tests/cr_draw_test.c` (a step up, down, a wrap).
  **EDIT tap**: the sound editor (`cr_edit.c`, docs/EDITOR.md; BASS held + EDIT: the bass sound's, SHIFT + EDIT inside
  switches part). Groups -> screens -> lanes: the function buttons become group buttons (FX = OSC, SEL = FILT, ENV,
  LFO, SEQ = MOD, PLAY = FX sends, REC = MIX; HOME or EDIT leaves); a group's tap opens it, tapped again it cycles its
  screens (a hold: nothing, reserved); SELECT moves the lane (the four parameters on KNOB 1-4) and runs on across the
  group's screens, wrapping, so SELECT alone reaches everything. The screens come from the engine's deep pages
  (`eng_deep_t`, by their titles, never by column name): the VA's OSC = the oscillator stack, the "+" stack, the
  oscillator mixer (the four LEVELs as tall bars); FILT = rows A / B under the filter curve; ENV = ENV 1-4, two lanes
  each under that envelope; LFO = the stack, the sync stack; MOD = the 8 slots; FX one lane, MIX two. Each part
  remembers its group, each group its screen and lane. Screens are the dense kinds `CR_K_STACK` and `CR_K_EDIT8`
  (`tall`: the mixer), no header bar, no footer. A detent: accelerated (0.22: the finest step slowly, up to 5 % of the
  range quickly), enums one by one; GLO = SHIFT: the OPT snap, a tap latches it (its LED, "snap" in the title line),
  held it is momentary (`cp_dstep`, `kn_*`: EDITOR.md §6a); deep
  edits mark the sound `*`. Engines without deep pages: OSC = EDIT 1, FILT = EDIT 2, ENV / LFO / MOD the platform
  pages (FM6: "own envelopes" on ENV). A layer opened over the editor (PERF held, the engine picker) closes back to
  it. **EDIT held** (or PRESETS turned inside the editor): the engine picker as a **preview** of the part's sound
  (snapshot: every parameter, the engine, the VA patch blob, `edited`, the user slot); SELECT switches the
  engine (keeping the envelope, filter, LFO and mix; until 0.18 the white roots did, unless KNOB 4 set them to play:
  they always play now, Settings `pick_roots` is reserved), KNOB 1 / PRESETS the engine's pool (the meter
  jumps, no fill), KNOB 2 init; OCT− / HOME cancel (the
  snapshot back), OCT+ / EDIT keep (a fresh load). Outside the editor PRESETS loads at once. **SAVE tap** (from a
  bass page or BASS held + SAVE: the bass): Overwrite / Save as new (docs/EDITOR.md section 8); Save as new's naming
  page shows the pool's place it takes ("FM6 · 27"), the white roots type
  (phone style: ABC DEF GHI JKL MNO PQRS TUV WXYZ 0123 4567 89-.), D#4 a space, F#4 deletes, KNOB 2 the last letter,
  SAVE again or OCT+ saves, OCT− or HOME cancels (back to the editor when it came from it);
  the prefilled name (grey) is replaced by the first letter typed. `upreset.c` is back (its Felucca-UI names in
  `cr_bank.c`: no undo copy, no pattern); records are Felucca's (no pattern). Persisted on the device (flash: `persist_boot`
  -> `cr_bank_boot`), RAM-only in the emulator. The emulator's logs get `edit:` / `page:` / `param:` / `engine:` /
  `preset:` / `picker:` / `save:` / `sound:` lines (`CR_TRACE`, set by `emu_firmware.h`).
- **`cr_anim.c`**: `cr_tween`, `cr_spring`, the animation clock and Options > Motion (Full / Calm / Off); the
  squeeze, picker slide, meter fill and spring, the stripes at one bar per cycle and their sweep by the first chord.

- **The SCOPE view** (View 5: Options > View, HOME held's view menu, HOME taps when unlocked): the master output as one bold 3 px white line over a thin
  grey centre line, the chord name small in the top line. `cr_ui.c cu_scope` reads `audio.c`'s own ring
  (`scope_buf`, 512 samples at 22 kHz, written in the audio ISR, as Felucca's GRAPH scope), takes 240 samples from the
  steepest rising zero crossing of the first 272 and auto-scales them (floor 2048: silence is a flat line) into
  `cr_screen_t.wave` (hashed with the struct: the cache redraws while it moves). It stays when quiet (no idle stripes).
  Host cost (`tools/emu` exit stats, a chord held 6 s): chord view avg 64-70 us a frame (settled: one hash),
  scope 192-228 us (a full redraw every frame), against 670-1230 us for a chord screen's own redraw.
- **Calibration**: Felucca's HARDWARE CALIBRATION (`ui_input.c panel_setup`: press each printed button, turn each
  knob right) on ChoralRoot's screens, run from the UI frame (`cr_ui.c cu_calib_*`): the label huge (CR_K_BIG 40 px),
  `n/21` and press / turn right under it, the ring as the progress; then "done": OCT+ (as just taught) keeps the
  table (`panel`, saved with the settings record by `cr_settings_save`, Felucca's `settings_save`), OCT- puts the
  old one back; 30 s idle cancels. Entry: OCT- + OCT+ held at power-on (`main.c` `panel_setup` asks for it) or
  Options > Calibrate (the last setting), OCT+. Felucca calibrates no pot: MASTER is not part of it.

### Different from the design above (for now)

- LEVEL and PAN on the MIX page are the part's (Felucca's `param_kept`): saved with a user sound, not loaded by it.
  An FM6 sound's ENV page edits values its own envelopes ignore (the page says so).
- Defaults: the RAW stream off (Orchid), MIDI clock out off, FX on, the bass OFF (ALGORITHM at 0; BASS tap: SUB
  BASS), TINE EP on part 0, the MOD palette. The parts' levels (`cr_ui_init`; LEVEL steps 0.5 dB): the chord part
  92 (-10 dB: Felucca's default 104, -4 dB, trimmed by 6 dB), the bass 92 too (-10 dB: trimmed by 6 dB; 98 until
  2026-10-06), so a 6-note chord stays out of the master limiter (Performance, item 2). A sound's load keeps the
  part's level (the rule above); MASTER makes up the loudness (about 4 dB quieter than before on a held chord).
  **Per-sound trims**: the loud factory presets carry a trim (`cr_bank.c` `CB_TRIM`, by engine and preset name: PHASE STRING / ORGAN -8 dB, PHASE BRASS -3 dB, VOICE CHOIR AAH -3 dB; CZ-1 BRASS 1 -5 dB, STRINGS 2
  -3 dB, PIPE ORGAN 1 -6 dB until 0.19, when the whole CZ-1 engine went 6 dB down and its three rows with it (docs/CZ1.md "Level": the table has no row left on ChoralRoot's unit); ANALOG STRINGS -1 dB until ANALOG was retired in 0.14; since docs/PRESETS.md also a user record bound to one of them), signed 0.5 dB steps, a
  gain after LEVEL (`track_t.trim`, applied in `fx.c` `mix_part` as LEVEL + trim steps of `LEVEL_Q12`, LEVEL 0 still
  OFF), set by `cu_pool_load` when such a preset loads from its pool; 0 for an added user preset, INIT, an engine
  switch, and on Felucca (its renders unchanged). LEVEL is never touched. Trims (measured as Performance item 2:
  the share of the held 6-note chord of scenario (a), 0.3-5.9 s, under the limiter's gain, chord alone / with SUB
  BASS, both parts at 92): CZ STRINGS -8 dB (56 / 86 % -> 0 / 0 %), CZ ORGAN -8 dB (100 / 100 % -> 0 / 2.1 %),
  CZ BRASS -3 dB (81 / 97 % -> 0.6 / 8.8 %), CHOIR -3 dB (55 / 91 % -> 0.9 / 6.7 %), STRINGS -1 dB (1.6 / 13.3 % ->
  0 / 3.9 %); every other chord sound is at most 3.7 % with the bass untrimmed (SOFT PAD 0.1 / 3.7, FULL ORGAN 0 / 1.5,
  PWM STRINGS 0 / 0.4, the rest 0 / 0). A user sound saved from a trimmed bank sound plays untrimmed (louder by
  the trim). The power-on splash is ChoralRoot's: `main.c`'s `splash()` calls `cr_shim.c` `cr_splash()`, the idle stripes sliding in (`CR_A_INTRO`, Options > Motion from the stored record) with `FELUCCA_VERSION` under them; the idle screen continues it, the version shown for its first 1.5 s (`cr_ui.c` `CR_SPLASH_MS`; the emulator plays the slide there, `tools/emu/scripts/cr_splash.txt`).

- **The looper** (step 4, `cr_loop.c`, docs/LOOPER.md): semantic events through the engine's `gesture` hook and
  `cr_loop_event` loop voices; Free / 1-16 bar sync with a clicked count-in, quantize on commit, overdub layers,
  undo, clear, panic; LOOP / REC / SEQ taps and holds, the LOOP layer (slots on D4..F5, D#4 CLEAR, F#4 UNDO,
  KNOB 1-4 SYNC QUANT COUNT-IN LEVEL, the Overdub/Pause/Undo/Clear picker while playing), SAVE held (Save / Load /
  Delete, OCT+), ten flash slots (storage.c's commit protocol on sectors of their own), the ring and `Rec 1.2` /
  `Loop 1` top lines, REC / LOOP / green LEDs, the metronome click (time signature, level). The count-in is a big
  red countdown (4 3 2 1 springing in, "count-in", the ring drawing itself in; over popups, under PANIC); undo shows
  the layers left huge in red ("layers · undo"); KNOB 1 / SELECT in the LOOP layer move its picker (stopped: the
  length, mock-up 8; playing: the action). The loop's sounding notes glow dim on the root keys of their note names (`cr_snap_t.lnote`,
  from the 8 loop voices), the player's stay lit. MIDI Clock Out sends 0xFA when the loop starts playing (LOOP from
  stopped, a take committed) and 0xFC when it stops (`cr_out.c`, the ISR; trace `midi:`).
  **0.18 (2026-10-11, the owner's three decisions; docs/LOOPER.md, docs/SETTINGS.md):** saving is manual (SAVE held:
  `cu_gsave` -> `cr_settings_commit` + `cu_loop_write`; GLO + SAVE: `cu_revert` -> `cr_settings_revert`, the slot
  reloaded); the loop in RAM is written only by that and by leaving its slot (`cu_loop_autosave` is gone, the SAVE-held
  slot map too); the PLAY menu has two pages on SELECT (Length Rate Quantize Count-in / Level), its keys on GLO alone;
  the Rate (`cr_loop_set_rate`: the loop's own clock, `crl.t`, runs at 1/16 .. 4 of the tempo's, `crl.rt`); Step entry
  with Length Free commits the steps exactly; the loop editor (`cr_loop_ed_*`, `cu_ledit_*`: EDIT in the PLAY menu).
  Both are a step sequencer (2026-10-12): `CRL_STEP` (a sixteenth) and `CRL_STEPS` (64), a step's length its notes'
  `dur` (docs/LOOPER.md).
- **Sounds, the naming screen**: SAVE held 1 s on a used slot asks "delete?" (the slot number huge in red, OCT+
  deletes it through `up_put(k, 0)`, OCT- keeps it); saving over the slot the part's sound came from, unedited,
  renames it (`up_rename`, the stored sound kept); else OCT+ overwrites. Its keys: the typing roots lit (D4..G5,
  D#4 space), the other black roots dark, OCT- lit, OCT+ blinking.
  Settings record v2 (metronome, slot); `CR_SETTINGS_BUSY` = a loop plays. Options > Split Point; Single Notes wired
  (`CRE_SINGLE`). Settings load at power-on inside `cr_ui_init` on both builds. The emulator builds with
  `FELUCCA_FLASH 1` (user sounds and loops persist with `--flash`); `--flash / --no-flash / --save-on-exit` are
  emu.c options (`emu_fw_options`).

- **The third-pass grammar** (2026-10-06, `design/make_mockups.py` states 1-24): printed SEL = KEY, printed EDIT =
  EDIT (swapped; the emulator: `X` = KEY, `B` = EDIT); B3 = LOCK (the chord block's latch, a UI virtual hold); a
  layer locks open on its hold (KEY PERF FX BASS LOOP SEQ, the engine picker), its button blinks, OCT- / HOME
  close it, another layer's hold switches; the HOME + hold lock, OPT + FX / PERF and OPT + KNOB 3 locks removed
  (OPT + KNOB 2 / 3 / 4 / PRESETS: the knob's own job). Footers "… · OCT-: back · HOME: home". Scripts: `cr_lock.txt`,
  `cr_layer_lock.txt` (new), `cr_perf_lock.txt` (gone); the others close their layers with `btn OCT-`. The editor's
  hooks into the grammar (`ce_owns` / `ce_button` / `ce_leds`) sit behind `CR_EDIT_HOOKS` (cr_edit.c sets it).

- **The editor after the first play-through** (2026-10-06, the user's feedback): groups -> screens -> lanes with SELECT
  running across the screens, the OSC mixer, SHIFT latched on a GLO tap, layers from the editor close back to it,
  the engine picker as a preview (OCT− cancels to the snapshot, OCT+ / EDIT keep; KNOB 4 roots engines / play,
  persisted as `pick_roots`, settings version 3), PRESETS in the editor opens the picker, SAVE in the naming screen
  saves (OCT− cancels, F#4 deletes a letter). Scripts: `cr_editor.txt`, `cr_editor_pick.txt` (new), `cr_engine.txt`
  (OCT+ keeps), `cr_save_del.txt`, `va_persist_*.txt` (U01 at power-on), `persist_roots_*.txt` (new).

- **Backup and restore** (2026-10-06, `cr_backup.c`; web/EDITOR_PROTOCOL.md "ChoralRoot: backup and restore"):
  Felucca's backup SysEx (INFO 1, LIST / GET / PUT 65-67) plus RESTART 72, answered from `ed_service`
  (`cr_shim.c` keeps a stub only without it) on ChoralRoot's objects: settings 1, user sound banks 6 / 7, FM6 bank 8,
  VA store 9, loop slots 40..49 (2026-10-07: no sample slots 32 / 33 and no SMP 11-14 any more, backup tag `43 01
  02`: a Felucca archive's samples, and a ChoralRoot 0.1 archive's, are reported skipped). Reads from flash (the current A/B copy) in 256-byte windows, also
  while a loop plays; writes staged in `cu_loop_buf` (`cu_loop_gen`: the UI's use ends a session), validated, then
  `st_save` / `crl_fl_save`; busy (rc 3) while a loop plays or records; a restored settings record sets
  `cr_restore_lock` (`CR_SETTINGS_BUSY`: no settings save until the restart). RAM: the session state only (~120 B),
  the replies in `st_buf`. The page (`web/fm1backup.js`, `web/index_pkg.html`: backup before install, Skip backup,
  Back up / Restore, the restore offered after installing over another firmware) and `tools/fm1_install.py --backup /
  --restore`. Host test `tests/cr_backup_test.c` (the emulator's build: it has no SysEx transport, so no emulator
  script); run on hardware 2026-10-07 (a full backup of the user's FM-1: settings, user sounds 1-16, FM6 patches 1-16).

- **Sounds as files** (2026-10-07, docs/SOUNDS.md): the installer page's Sounds section (`web/fm1sounds.js`,
  `web/index_pkg.html`) and `fm1_install.py --sounds / --export-sound / --import-sound / --rename-sound / --delete-sound`
  export, import, rename and delete single user sounds (the 192-byte record plus the VA / FM6 / CZ-1 blob) as
  `choralroot-sound` JSON files by rewriting the backup objects 6, 7, 9..13 per slot (the stores first, the bank last);
  no firmware change: `crb_commit` reloads the mirrors (`up_boot`), pinned by `tests/cr_backup_test.c` "sounds: single
  slots through PUT". Verified on the device the same day (an FM6 sound copied to U03, renamed, deleted).

### The effects (0.18, 2026-10-11: a stereo delay, a large reverb, global)

`cr_fx.c` (included by `fx.c` under `CR_FX 1`: `choralroot.c`, `tools/emu/emu_firmware.h`; Felucca's delay and ROOM /
SPRING stay behind `CR_FX 0` for its own units and goldens). `fx_buses` keeps the chorus; `mix_block` then calls
`xf_block(send_d, send_r, mix_l, mix_r)`: the delay at 44.1 kHz straight on its send, the reverb at 22.05 kHz between
half-band filters (`CR_FX_DELAY_DS`, `CR_FX_REVERB_DS`; `CR_FX_RAM=1`: the four loops in `.ram_text`; `CR_RAM_FX=1`:
the whole stage, the chorus and the mix loops linked at RAM, docs/FX.md "Code in RAM"), the stereo wet
into the mix before the FX layer's master effects and the limiter. **The sends are the parts'** (the owner,
2026-10-12): `cr_bank.c param_kept` keeps `P_DIST .. P_REV` through every load, the chord part's are `cs.fx_amt[]`,
the bass part's its `p[]` (session tags 26..29); the effects' parameters are `xfp[]` (16) and the chorus's two, in
the session by tag. `fx_off` (the FX tap) closes every part's sends; a send is reached as a ramp over a block
(`fx_sg`). The tempo for the synced delay is `xf_bpm`. Both effects switch themselves off when their tails are over
(a half then costs 2 us). The second build (the first was heard on the unit: a delay that dropped out, holes cut by
saves, a cost far above the estimate), the numbers and the owner's open decisions: **docs/FX.md**. Size: XIP
444 148 B, RAM 85 156 B, POOL 326 316 B (17 748 B free). CPU (the emulator's estimate): both on 267 us a half
(9.2 %); `perf.sh` (l): 919 / 1278 us (32 % / 44 %), no half over 80 % anywhere (the worst: (g), 58 %). **On the unit**: `fm1_install.py
--debug` reads SysEx 78 PEAK (`cr_backup.c crb_peak`, `cr_cpu1.h`): the worst half and each stage's since the last
read, the late halves and their largest stage, the fx stage's four parts, `flash_holes`. `cr_settings.c
crs_sounding`: no AUTOMATIC settings write while the output is above -60 dBFS, with no time limit. The global save
(SAVE held, 0.18) is the player's own press and writes at once: an erase (45 ms of silence) an object that changed,
three at most (the settings object, the session, the loop slot); it waits only for a playing loop or an open capture
("saves at stop"). `flash_holes` counts those erases too when the output sounds. **The third
build** (the second on the unit: the reverb 331 us a half for an estimate of 116, a pop for every late half):
`cr_fx.c`'s loops run block-wise, a stage and a line at a time (about 290 us for both effects by the unit's own
instruction count, `tools/fx_loops.py`; `CR_FX_LITE=1` a lighter reverb); **`audio.c fm1_alnk0_irq` clears the half's
pending bit before the render** (after it, a late half took the next boundary's interrupt with it and the DAC replayed
an old half; `CR_ACK_LATE=1` builds the old order); `shed_check` counts how far behind its boundary a half started;
78 PEAK carries six more values (`stale_halves`, `pk_lag_us`, `pk_wall_us`, `pk_t5_us`, `irq_gap_us`, `irq_gap_pc`:
`cr_cpu1.h cr_prof_entry`, `cr_prof_t5`, the latter from `main.c`'s TIMER5 with `fm1_irq.h fm1_reti`).

### All-synth (2026-10-07: the dead weight out)

ChoralRoot's unit drops Felucca's sequencer and the sample-based engines (section 1 has the names the kept files
still get): `FELUCCA_SEQ 0` (seq.c with song_chain.c, chord.c, motion.c, midi_control.c, midi_clock.c),
`FELUCCA_SAMPLE 0` (eng_sample.c and its ADPCM sets), `FELUCCA_GRAIN 0`, `FELUCCA_DRUM 0` (eng_drum.c, drum_voice.c),
`FELUCCA_ICONS 0` (icons.c) and `FELUCCA_KEYCAPS 0` (gfx.c's keycaps); Felucca's unit and its tests keep them all
(1). Measured with `./build.sh`, each step on top of the one before (the first row: HEAD before the change; the
second: this change with every flag at 1):

| build | .text | .bss (RAM) | POOL |
| --- | --- | --- | --- |
| before (b32e83f) | 490104 B | 90512 B (92.4 %) | 330116 B (95.9 %) |
| every flag 1 (the new presets, the backup without samples) | 489540 | 90528 (92.4 %) | 330116 (95.9 %) |
| seq.c and its satellites off | 480812 (-8728) | 74336 (-16192: 75.9 %) | 330116 |
| + SAMPLE and GRAIN off (GRAIN needs SAMPLE's sets) | 278672 (-202140) | 72976 (-1360: 74.5 %) | 302036 (-28080: 87.8 %) |
| + DRUM off | 270312 (-8360) | 72976 | 294740 (-7296: 85.7 %) |
| + icons and keycaps off (the default) | 270312 (0) | 72976 | 294740 |

The icon atlas and the keycaps cost nothing: no ChoralRoot screen called them, so the compiler had left them out of
the image already (`ui_icons.h` / `ui_keycaps.h` are no longer included; the generate step still writes them for
Felucca's tests). Total: flash 493280 -> 273488 B of the XIP slot (84.8 -> 47.0 %), RAM 92.4 -> 74.5 %, POOL 95.9 ->
85.7 %. The audio of what remains is bit for bit the same: `tools/emu/perf.sh`'s six WAVs (FM6, VA, ANALOG sounds,
the bass, the loop, the arp) are identical to b32e83f's.

(Since docs/PRESETS.md there is no bank: these are FM6 25 and VA 24 / 25 in their pools.) The bank kept its rows: PRESETS 03 PIANO (was SAMPLE's) is FM6's PIANO, TINE EP's patch through the macros (MRAT +1,
MLVL +10, MEG -16, VMOD +2, FB +1, DTUN 12: an EP-piano hybrid; no new factory patch; since Melodee's FM6, PTCH's F1..F24
come before B1..B32, docs/FM6.md);
15 CLOUD PAD and 16 SHIMMER (were GRAIN's) are VA presets 23 / 24 (docs/VA.md). Levels with `tests/va_levels.c`
(chord alone / with SUB BASS, the share under the limiter): PIANO 0 / 0 %, CLOUD PAD 0 / 1.4 %, SHIMMER 0 / 0 %;
trims 0. The emulator's `cr_allsynth.txt` plays the three. A user sound on a retired engine loads as INIT on the VA
(section 1; an ANALOG one with its macros). The flash of user sample slots 1-2 (0xA0000..0xC7FFF): 0xA0000..0xB1FFF holds the CZ-1 tone store's second half and the eight CZ-1 banks (docs/CZ1.md), 0xB2000 / 0xB3000 FM TONE's patch store, 0xB4000 / 0xB5000 the session (0.18, docs/SETTINGS.md), the rest stays free; slot 3's holds the loops.

### Stubbed (screens and gestures only; TODO in the code)

Nothing in the grammar. Of Felucca's web editor protocol only the backup subset is answered (no sound / sequence
editing over SysEx: `web/editor.html` does not work with ChoralRoot).

### Next steps

1. **Settings** (`cr_settings.c`): ChoralRoot's block in Felucca's settings record (`settings_persist.c`), saved on
   change (deferred while a loop plays), loaded by `persist_boot`; `panel_setup` on ChoralRoot's screens: done (above).
2. **Sounds**: done (above, 2026-10-05: delete / rename of user slots, the naming screen's key LEDs); left: the
   bank's choice by ear on the device.
3. **The looper**: done (above, 2026-10-05: the big count-in and undo, the loop-length picker on KNOB 1 / SELECT,
   loop notes glowing dim on the keys, MIDI start / stop with Clock Out, and MIDI clock / start / stop in with Clock In); left: the device's CPU
   measurement with FM6 chords, a bass and a playing loop.
4. **The device build** (2026-10-05: done, `build/choralroot.fwsc`, identity `FM-1_920`): `tools/build.py`
   compiles `choralroot.c` with the JieLi clang 4 (no source fix needed; one warning, an unused `b` in `cr_ui.c`),
   keeps the minsize round trip (`tools/size_fns.py`: the `cr_*.c` UI / screen / store files; not `cr_engine.c`,
   `cr_out.c`, `cr_loop.c`) and writes `build/choralroot.{bin,elf,dis,fwsc}` (releases keep the identity; the
   version string becomes `ChoralRoot X.Y`). Measured:
   `size: .text 448672 B, .ram_text 2888 B, .data 296 B, .bss 88336 B; XIP 451856 B of 581564 (77.7%), RAM 88632 B
   of 98304 (90.2%), POOL 315400 B of 344064 (91.7%), NOINIT 200 B of 15696 (1.3%)`.
   **The SLICER is dropped** (`FELUCCA_SLICER 0` in `choralroot.c`; 1 in `felucca.c` and the emulator): with it the
   POOL overflowed by 4104 B (its `sl_buf` is 32 KiB). `slicer.c` keeps its names as no-op stubs, and `perform.c`'s
   buffer effects (which borrow `sl_buf`) are off: `perf_press` returns at once (nothing in ChoralRoot sets
   `kb_mask` anyway). CPU: see Performance below (`tests/target_budget.txt` now holds ChoralRoot's ISR).
5. **All-synth, the dead weight out** (2026-10-07): see "All-synth" below.

## Performance (2026-10-06: pops and glitches)

### Reading it on the device

USB serial console (`FELUCCA_CDC`, the CDC-ACM port; the baud rate is ignored; since 0.14 the FM-1 presents it while
Options > USB Record is Off, and in SAFE MODE: docs/USB-AUDIO.md): `screen /dev/tty.usbmodem* 115200`
on the Mac (or any terminal), then `cpu` (`help` lists the rest; `status` and `dbg` are Felucca's; `boot` prints the
boot guard: this boot's mode (normal / safe), failed and pending, the reset that started it (power-on, wdt, soft,
other; the raw `p3_rst`, `rst_src`, `wdt_con`), the stage the last run reached (`last_stage 13 fm6 bank`) and every
stage's name). `cpu` prints the
last full second of the audio ISR (`audio.c` `cpu_window`, closed by the UI frame every second) and what cuts the
sound since power-on:

| key | meaning | healthy |
| --- | --- | --- |
| `audio_budget_us` | one half buffer: 128 frames at 44.1 kHz | 2902 |
| `audio_avg_us` / `audio_max_us` | the render of a half, TIMER5 nested in it left out | max well under 2902 |
| `audio_max_all_us` | the same with the nested TIMER5 / USB audio: what the DMA deadline sees | < 2902 |
| `audio_late` / `audio_late_total` | halves the DMA moved past while they rendered (an overrun: a repeated or torn half, a crackle) | 0 |
| `audio_max_us_total`, `cpu_pct` | Felucca's since-boot maximum and its ~93 ms load meter | |
| `voices_given_up` | voices faded out over one block (taken for a new note, from another part, or shed) | grows with chord changes |
| `voices_stolen` | a part's own voice taken for a new note (6-note chords: every change of chord) | |
| `voices_shed` | voices dropped because two halves in a row went over 85 % (`audio.c` shed) | 0 |
| `flash_erases`, `settings_saves` | each erase stops every IRQ for ~45 ms with the audio buffer zeroed | grow only when you save, or when the instrument is quiet |
| `ui_frame_max_ms`, `ui_frames` | the longest main-loop frame of the second (15 ms nominal; keys are read once a frame) | < 30 |
| `stage_<s>_avg_us` / `_max_us` | the ISR's stages per half, the last second (`cr_cpu1.h` cr_dbg): `tick` (the engine, the looper, MIDI, the events, voice allocation), `part0` (the chord part's render), `part1` (the bass part's: core 1's job time), `fx` (DIST / level / sends of the two parts, the idle parts' LFO ticks, the FX buses), `master` (limiter, USB tap, click), `wait` (core 0 waiting for core 1) | their sum ~ `audio_avg_us` |
| `c1_state`, `c1_jobs`, `c1_stack_bytes`, `c1_start_us` | the second core (docs/DUALCORE.md): 0 off (Options > Dual Core Off, the default; SAFE MODE), 1 ok, 2 failed at start, 3 gave up on a job; jobs done; its stack's high-water mark of 2816; its start | 0, 0 (Off); 1, rising, < 2816 (On) |

The GEEK OUT view (Options > View) shows the third line `isr <us> · late <n>`: the last second's longest half
(`audio_max_all_us`) and the overruns since power-on. After a boot that was not clean (a counted crash reset, or SAFE
MODE) it shows `boot <reason> stage <n>` instead: the reset class and the boot stage the crashed run reached
(`firmware/src/cr_bootguard.h` names them).
The fourth line is the second core (docs/DUALCORE.md): `c1 off` (Options > Dual Core Off, the default; SAFE MODE),
`c1 ok <jobs> · stk <bytes>` (Dual Core On), `c1 failed`
(no answer at power-on) or `c1 gave up` (a job took over 8 ms); the fifth the ISR's stages, the last second's average
in us per half: `t<tick> c<chord> b<bass> f<fx> m<master> w<wait>`.

The memory after the second core (`./build.sh`, 2026-10-09): `.text 414120 B, .ram_text 2888 B, .c1_text 60 B (RAMTEXT
2948 of 24576), .data 12072 B, .bss 72104 B; XIP 73.8 %, RAM 85.6 %, POOL 95.0 %`.

### Measured on the host

`sh tools/emu/perf.sh` runs the worst cases headless (`tools/emu/scripts/perf_a..e.txt`) and prints, per scenario, the
audio block's cost in host instructions (kernel-counted, deterministic within ~1 %) with the device estimate, the UI
frame's, the voices given up / stolen, the flash erases, and `tools/emu/wavclicks.py` on the WAV (sample jumps over
0.5 FS, silent holes mid-sound, high-frequency bursts against their neighbourhood). The device estimate: 1.7 % of the
2.9 ms half per 100 host instructions a sample (the ratio `tests/fm6_test.c` and `drum_test.c` take from the PHYS
measurements on the device), i.e. a half's device us = host instructions / 259. The emulator also plays a flash erase
as the device does: 45 ms of zero blocks with the ISR not run (`tools/emu/emu_hal_fw.h` `st_erase`).

| scenario | ISR avg / max (device est.) | holes | clicks | erases under sound | UI frame avg / max (device est.) |
| --- | --- | --- | --- | --- | --- |
| (a) 6-note chord, TINE EP | 19 / 31 % -> 20 / 33 % | 0 -> 0 | 1 -> 1 (its attack from silence) | 0 -> 0 | 0.8 / 9 -> 0.8 / 8.3 ms |
| (b) the same, FM PAD | 24 / 38 % -> 24 / 34 % | 1 -> 0 | 2 -> 0 | 1 -> 0 | 0.3 / 9 -> 0.3 / 9.0 ms |
| (c) FM PAD + bass + loop + arp, 200 BPM | 23 / 42 % -> 24 / 42 % | 2 -> 0 | 4 -> 0 | 2 -> 0 | 49 / 221 -> 9.4 / 194 -> 3.0 / 14 ms |
| (d) (c) on SCOPE | 22 / 41 % -> 22 / 46 % | 2 -> 0 | 4 -> 0 | 2 -> 0 | 22 / 40 -> 3.9 / 12 -> 3.9 / 11.5 ms |
| (e) (c) + KNOB 1..4 every 30 ms on EDIT | 23 / 39 % -> 24 / 43 % | 2 -> 0 | 4 -> 0 | 2 -> 0 | 43 / 138 -> 11.8 / 115 -> 6.7 / 34 -> 1.8 / 14.1 ms |

2026-10-06, the editor without motion (docs/EDITOR.md §10 "Responsiveness"): no tweens or slides, only the strips of
the editor's changed parts composed, `cr_poly` testing a pixel's centre first, and the blits sent as the box of the
changed 60 x 8 tiles from a second buffer while the next strip is drawn: (e) 6.7 / 34 -> 1.8 / 14.1 ms; a cutoff
detent on the FILTER screen 6.5 M host instructions a frame for 10 frames -> 3.3-3.5 M once (1 frame of lag).

Per sound (6-note chord held, FX on / off, device estimate of the ISR): 12-20 % average, 23-46 % worst block, the FX
buses +1 % (no buffer of theirs saturates). ChoralRoot's own ISR work (`cr_audio_block`: the queue, MIDI in, the
engine's and the looper's ticks, the click, the clock) is ~1.4 % over Felucca's idle mix (silent: 7 % against
Felucca's 5.4 %); nothing there is worth moving to the UI frame. The host says the ISR has 2x headroom; the device's
`cpu` readout is the proof (XIP cache misses, the nested TIMER5 and USB audio are not in the host figure; perf.sh's
(u) line estimates the USB recording at ~31 us a half, 1.1 %, without the SIE accesses: docs/USB-AUDIO.md).

### Measured on the unit (2026-10-14, `tools/perf_unit.py`)

The owner: "you should be able to send midi in to the FM-1 and maybe sysex commands to change settings so you could
put it in a state to fully test on device performance". `python3 tools/perf_unit.py [all | idle fm6 va cz quad]
[--seconds 20] [--notes 8] [--record]` does that over USB alone: 76 SELECT puts the sounds on the parts, an 8-note
chord (C3 E3 G3 B3 D4 F#4 A4 C5) goes in on the Perform channel and a bass note on the Bass channel, moved up a
semitone every 2 s so voices are stolen as in playing, and 73 DEBUG / 78 PEAK are read before (the read clears the
worst cases) and after. `--record` runs ffmpeg on "ChoralRoot In" through the scenario. The parts are put back on
the power-on sounds at the end. **These are the unit's numbers, not the emulator's.** Worst halves (us of 2902) over
20 s, Dual Core On (the bass on core 1: its `part1` is not in `block`), the 0.22 build of f71bbb5 plus the shed
counter; `shed` is the voices the overload shed dropped (cumulative over the runs: the difference is the run's):

| 8 notes + bass | block | tick | part0 | part1 | fx | master | t5 | wall | late | stale | shed (+) | avg | p0 avg | fx avg |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| idle | 233 | 63 | 50 | 44 | 48 | 80 | 25 | 253 | 0 | 0 | 0 | 158 | 22 | 24 |
| fm6: TINE EP + DEEP SUB | 2580 | 353 | 1647 | 476 | 649 | 111 | 197 | 2777 | 0 | 0 | 0 | 1706 | 1136 | 525 |
| va: MORPH PAD + PUNCH BASS | **3121** | 315 | **2293** | 355 | 611 | 112 | 232 | 3351 | 23 | 0 | **+26** | 2062 | 1539 | 510 |
| cz: JET ROAR + SYNTH.BASS | 2271 | 295 | 1439 | 374 | 608 | 103 | 169 | 2433 | 0 | 0 | 0 | 1565 | 1047 | 488 |
| quad: WOBBLE + BASS | 2345 | 237 | 1509 | 322 | 642 | 103 | 170 | 2515 | 0 | 0 | 0 | 1577 | 1061 | 488 |

| 6 notes + bass | block | part0 | fx | t5 | late | shed (+) | avg | p0 avg |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| fm6 | 2444 | 1521 | 633 | 194 | 0 | 0 | 1645 | 1031 |
| va | **3102** | **2280** | 637 | 230 | 18 | **+22** | 2211 | 1686 |
| cz | 2032 | 1169 | 594 | 150 | 0 | 0 | 1569 | 1041 |
| quad | 1993 | 1157 | 645 | 145 | 0 | 0 | 1585 | 1032 |

| 8 notes + bass, recording at 44.1 kHz | block | tick | part0 | fx | master | t5 | wall | late | shed (+) | avg |
| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- |
| fm6 | 2737 | 404 | 1726 | 720 | 172 | **349** | 3084 | 5 | **+12** | 1646 |
| va | 3217 | 323 | 2397 | 705 | 160 | **407** | 3625 | 40 | **+40** | 1813 |

What it says:

- **A VA MORPH PAD chord does not fit one core**: 2280 us of part0 alone with 6 notes, an average half of 2211 (76 %),
  peaks over the half (3100), and the shed drops voices (22 in 20 s with 6 notes, 40 more while recording). The pad's
  two MORPH oscillators cost about 280 us a voice on the unit; docs/VA.md's "38 / 54 %" was the emulator's.
- **FM6 TINE EP with 8 notes is at 88 %** (average 59 %), and sheds once a recording runs. A 6-note chord is 84 %.
  About 200 us a voice on the unit.
- **CZ-1 and FM TONE sit at 70 .. 80 % worst**, 54 % average: comfortable, no shed.
- **The fx stage's worst half is 600 .. 720 us (21 .. 25 %), its average about 500**: not the 290 the listing counts
  for the delay and the reverb; the stage also holds the parts' DIST, level, pan and sends, the idle parts, the
  chorus and the buses. Read apart below ("The fx stage's parts and TIMER5"): the four effects' parts are 75 % of
  it, the rest is five small loops and TIMER5's ticks, and the 600 .. 720 were the recording's ticks in it.
- **The engine tick peaks at 300 .. 400 us** at a chord change (8 note-ons, the stealing, the fades), against 7 .. 100
  on the emulator's scenarios.
- **TIMER5 takes 150 .. 230 us of the worst half dry (5 .. 8 %) and 350 .. 410 while recording (12 .. 14 %)**: the
  scan and the USB polls, the recording's endpoint service on top (HANDOFF item 23, now read; the tick counted
  below: 7 us a tick, 60 % of it the 595 shift's GPIO accesses, the recording 4.2 us a tick more).
- **The bass on core 1 costs core 0 nothing** (the wait 9 .. 41 us) and would add 300 .. 500 us to every row with
  Dual Core Off.
- **`stale` stayed 0 throughout**: even at 108 % the DAC never played something old; the late halves (up to 40 in 20 s)
  were caught up and the shed took voices instead. The audible effect of an overload is a voice dropping out, not a
  pop.

Against "the host says the ISR has 2x headroom" above: on the unit the heaviest pads have none, and an 8-note chord on
the lighter engines has 10 .. 25 %. The emulator was 1.5 to 2 times optimistic for the renders (FM6 and VA) and 2 to
3 times for the fx stage. Dual Core, when it holds, is the one lever already built: moving the effects (600 .. 700
us worst) or half the chord part's voices (1100 .. 2300) to core 1 are the two designs in docs/FX.md "If the unit
still wants less" and docs/DUALCORE.md.

#### The fx stage's parts and TIMER5 (2026-10-14, later: `perf_unit.py`'s second table)

`tools/perf_unit.py` now prints 78 PEAK's fx parts for each scenario (its second table: the sends' down-sampler,
the delay, the reverb and the wet's up-sampler, average / largest over the halves in which the effects ran; `4avg`
their sum against the stage's average `fxavg`, 73 DEBUG's `st_fx_us`; `rest` the difference; `4max` the four
largest against the stage's largest half, which is not the same half), and it sends CC 123 to both parts first: a
chord that a stopped run had left held made the first "idle" row an 8-note one (part0 903 us). The unit ran the
LCD-20 MHz variant of 1cc5972 (the same code where it matters), Dual Core On, 8 notes + bass, 20 s a scenario:

| us a half | fx max | fx avg | down avg / max | delay | reverb | up | the four, avg / max | the rest, avg / of the max |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| fm6 | 587 | 490 | 33 / 57 | 98 / 131 | 198 / 234 | 37 / 60 | 366 / 482 | 124 / 105 |
| va | 566 | 467 | 21 / 47 | 102 / 130 | 193 / 228 | 34 / 52 | 350 / 457 | 117 / 109 |
| cz | 533 | 448 | 20 / 40 | 94 / 122 | 193 / 229 | 34 / 59 | 341 / 450 | 107 / 83 |
| quad | 579 | 444 | 20 / 40 | 90 / 120 | 194 / 234 | 34 / 54 | 338 / 448 | 106 / 131 |
| fm6, recording | 657 | 504 | 37 / 96 | 104 / 179 | 206 / 280 | 38 / 104 | 385 / 659 | 119 / 0 |
| va, recording | 651 | 534 | 31 / 78 | 108 / 166 | 203 / 273 | 36 / 85 | 378 / 602 | 156 / 49 |

The whole rows (block, tick, part0 ...) were within 5 % of the tables above; TIMER5 read 195 / 230 / 150 / 166 dry
and 329 / 396 recording, the two recording rows late 1 / 40 and shed +10 / +40.

**The fx stage.**

- **The four parts are the listing's count times 1.2 .. 1.3** (docs/FX.md "The cost on the unit"): the reverb
  193 .. 198 for ~160 counted, the delay 90 .. 108 for ~70 (its largest, 120 .. 131, is the time gliding under
  the wow), the down-sampler 20 .. 21 and the up-sampler 34 .. 37 for 20 and 31 (fm6's down 33 / 57 runs the same
  code as the others' 20 / 40 and was not chased). Together 340 .. 385 of the stage's 445 .. 535 average: 75 %.
- **The rest, 106 .. 156 a half, is five small things and TIMER5**, none worth more than 40: the two parts' level,
  pan and sends loop (`fx.c mix_loop_f`, 65 lines a sample, 128 samples, both parts send in every scenario: ~80 by
  the lines), the chorus where a sound sends to it (`cr_fx.c xc_block`, 52 lines a sample: ~32; TINE EP and MORPH
  PAD do, the bass sounds do not, and with nothing in the line the loop returns after a 4-line scan), DIST where it
  is on (`track_dist`, inlined into `mix_part_out`: 51 lines a sample, ~32; WOBBLE's DRIVE 10 is the only one of
  the eight sounds), the idle parts 2 and 3 (`track_lfo_tick`, ~4), `xf_block`'s own hash, scans and clears (~8),
  `mix_part_out`'s straight code (~6), and **TIMER5's ticks**: the stage figures are wall time (`CR_PROF` takes
  TIMER4's difference; only `pk_block_us` has the nested ticks taken out, `shed_check`), and 5 .. 6 ticks fall into
  a 500 us stage: ~40 dry, ~65 recording. The lines over-count these short loops by about a third (fm6's sum is
  ~165 against 124 read) the way they under-count the effects' loops, and at this precision DIST and the chorus do
  not show in the rest (quad 106 with DIST and no chorus, cz 107 with neither, fm6 124 with the chorus).
- **The stage's largest half (533 .. 587 dry, 651 .. 657 recording) is not a loop.** It is the parts' own largest
  halves (each 30 .. 40 over its average) and the TIMER5 ticks that fall in it: recording, each part's largest rises
  by 40 .. 50 and the stage's by 70 .. 90, while the averages move by 5 .. 10. The four parts plus the rest account
  for the stage at the average and at the largest: nothing is hidden in it.
- **The loop to work on inside the stage is the reverb's sample loop** (`cr_fx.c xr_run`: 513 lines a pass, 64
  passes a half: 193 .. 198 us, 40 % of the stage, 7 % of the half); the delay's (`xd_run`, 90 .. 108) is next, and
  everything else in the stage is under 40 us a piece. Neither has a cut left that keeps the sound (FX.md: two
  diffusers instead of four, -42 lines a pass; two swinging lines, -18): what the reverb can give without a change
  of sound is a few tens of microseconds, 1 .. 2 % of the half. The stage as a whole (445 .. 535, 15 .. 18 %) is
  the lever, and it moves only as a whole: to core 1 (docs/FX.md "If the unit still wants less", docs/DUALCORE.md;
  the bass takes 300 .. 500 of core 1's 2902).
- A profiler note: because the stages are wall time and the block is not, the stages' largest halves sum to more
  than the block by about `pk_t5_us` (fm6 dry: 359 + 1627 + 587 + 124 + 16 = 2713 against 2509 + 195).

**TIMER5** (`main.c fm1_timer5_irq`, `hal/fm1_input.h fm1_input_tick` inlined into it).

- **What a tick runs, from the listing** (`build/choralroot.dis`; a tick nested in the render, a column with no
  LED pulse, the keys idle, not a frame's end): about 560 lines. The ISR wrapper 7 (`fm1_isr.S`: the pushes, the
  call, the pops, csync, rti); the entry, the gap statistics, the lines dark and the rows' read 112; the 595 shift
  (`fm1__sr_bits`: 11 lines a bit, 16 bits) 181; the latch, the LED lines and the column's bookkeeping 35; the four
  rows' debounce (`fm1__key` inlined, 28 a row) 112; the cost statistics, the millisecond accumulator, the USB and
  UART schedule and the call 66; `ua_service_paced` with the stream closed 47 (39 more every 250 us: `ua_service`
  finds `cap_alt` 0). The encoders' frame, every 11th tick, is 317 lines: 29 a tick. So 590 .. 600 lines, 2.9 us at
  205 a microsecond (HANDOFF 23 counted "about 600").
- **What the unit reads: 6.6 .. 7.3 us a nested tick** (`pk_t5_us` over the ticks in the half's wall time: fm6
  195 / 27, va 230 / 33, cz 150 / 22, quad 166 / 25). The other 4 us are the GPIO: a tick makes **110 accesses of
  PA's and PH's registers** (the shift's 16 bits at three read-modify-writes each, 96; the latch 4; the LED lines
  twice, 8; the rows 2; a column with a faint, dim or dim2 pulse adds a clock read and two writes of the lines)
  and 5 .. 6 reads of TIMER4: about 35 .. 40 ns an access, the peripheral bus's price. **The 595 shift is about 60 %
  of the tick**, the scan's own code 40 %.
- **What it costs: 7 % of core 0** (10 000 ticks at 7 us: 203 us of a 2902 us half; the 150 .. 230 read above are
  the ticks nested in halves of 2100 .. 3350 us wall). A tick that is not nested also runs `usb_poll` (every 5th)
  and the UART MIDI poll (every 5th): main-loop time, not the half's.
- **The recording's endpoint service: 4.2 us a tick more** (329 / 29 and 396 / 35 = 11.3 a nested tick), **42 us a
  millisecond, 4.2 % of core 0** (the 350 .. 410 read above). A millisecond of streaming runs ten ticks' `ua_tx_fill`
  (an SIE INDEX write and a TXCSR read each: has the host taken the packet) and the audio ISR's own 1.4 calls
  (`ua_isr_service`, four a half, most gated by the 250 us pace: 73 DEBUG `ua_isr_calls` 62 940 in 45.7 s of
  streaming, `ua_isr_gated` 51 161), four `ua_service` (`ua_hw_poll`: two fills, three FRAME reads, `ua_tx_prepare`),
  one packet of 528 / 540 bytes packed from the capture ring (`ua_transmit`: 45 frames x 6 channels, ~2000 lines,
  ~10 us) and handed to the DMA (`fm1_usb_ep_send`: two register writes). That is about 50 SIE transactions a
  millisecond (`sie_rd` / `sie_wr`: a start and a wait for done, about half a microsecond each: ~25 us), the packet
  ~10 and the calls' own lines ~7. `ua_service_max_us` 91 is the longest single service seen.
- **A 5 kHz scan would save half the ticks: 35 ms a second, 3.5 % of core 0, about 100 us of a half** (0.6 % more
  while recording: TIMER5's fills halve, the packet does not). What it changes: a frame becomes 2.2 ms, so a press
  is seen after 4.4 ms (`FM1_DEB_PRESS` 2 frames; 1 keeps 2.2 but admits one stray closed sample) and a release
  after 17.6 (`FM1_DEB_RELEASE` 8: halve it); the lit LEDs keep their 1/11 duty at 455 Hz (over the 200 Hz rule);
  the dim, dim2 and faint pulses come 455 times a second instead of 910, half the glow unless their widths double,
  and a doubled dim2 makes the wait on a tonic column ~14 us instead of ~5; the encoders are sampled 455 times a
  second (a detent is four transitions: fine at hand speed); `sub % 5` has to be re-spaced to keep USB at 2 kHz and
  the UART poll within its FIFO. **Cheaper and without any of that**: the shift writes the data line on every bit
  (16 of its 48 read-modify-writes) though `w` is all ones but one or two bits; writing it only where the bit
  changes saves ~14 accesses, 0.5 us a tick, 0.5 % of core 0. The clock's edges stay read-modify-writes: write-only
  edges latched the neighbouring column (fm1_input.h).

**Recommendation.** Core 0's half goes to the chord part's render (1300 .. 2300: what overloads), the fx stage
(445 .. 535, 15 .. 18 %), TIMER5 (~200, 7 %; 11 % recording), the engine tick (220 .. 400 at a chord change) and
the master (~120). In the order of what a change buys: (1) **the fx stage to core 1, as a whole** (docs/FX.md's
second design): 450 .. 650 us off every half, the one change that makes a MORPH PAD chord fit, and core 1 has the
room; (2) **the scan at 5 kHz**: 100 us, 3.5 %, with the debounce and the pulse widths re-tuned and the owner's
feel of the keys and the glow checked, or the data line's writes skipped for 0.5 % with no change to feel; (3)
inside the stage, the reverb's loop: 1 .. 2 % at best, and only with a change of sound. Not worth doing: the mix
loops, the chorus, the idle parts (each under 40 us, and the lines over-count them). Nothing was changed on the unit
for this: a measurement and a reading.

### What was found and fixed

1. **Flash erases under sound** (`cr_settings.c`). A settings change (sound, tempo, FX, bass, perform parameter,
   Options) was saved 1.5 s later whatever was sounding; `storage_hw.c` `st_erase` holds every IRQ ~45 ms (up to
   400 ms) with the audio buffer zeroed: a 46 ms hole with a click at each edge (scenarios b-e, the knob-turn script).
   Now a save also waits until nothing has sounded for 1 s (`crs_sounding`: a voice of the parts, a chord held or
   latched, a scheduled note, the master above -60 dBFS), besides the loop (`CR_SETTINGS_BUSY`). Explicit saves
   (SAVE: a sound, a loop at stop; the calibration) still erase when asked: a short silence there is expected.
2. **The master limiter crackled on held chords** (`fx.c` `fx_smooth`, set by `cr_out.c`). A 6-note chord with the
   bass reaches ~2x the limiter's threshold on ANALOG / WHEEL / PHASE sounds: limited 77-96 % of the time (FX on:
   +12 points), and the 4-sample attack re-attacking on each new peak of the beating chord modulated the gain at audio
   rate: -48 to -50 dB of sidebands above 1 kHz (FM6 sounds, quieter: limited 5-22 %, -56 to -60 dB, which is why
   they crackled less). ChoralRoot's branch holds the peak at once and eases the gain down over ~1.5 ms: -56 to -58 dB
   (ANALOG / WHEEL), -64 to -68 dB (FM6), the same loudness; Felucca's branch is unchanged (golden renders identical).
   Then the parts' default levels were trimmed (Defaults above: the chord -6 dB, the bass -3 dB). The share of the
   held chord's samples under the limiter's gain (`lim_g` < 1, 0.3-5.9 s of scenario (a) with each sound), before ->
   after, chord alone / with SUB BASS: SOFT PAD 97 / 100 % -> 0 / 8 %, STRINGS 100 / 100 % -> 1 / 24 %, PWM STRINGS
   98 / 100 % -> 0 / 11 % (ANALOG), FULL ORGAN 93 / 100 % -> 0 / 6 % (WHEEL), CZ STRINGS 100 / 100 % -> 62 / 95 %
   (PHASE: the loudest sound of the bank), TINE EP 4 / 7 % -> 0 / 0 %, FM PAD 0 / 3 % -> 0 / 0 %. Loudness of the
   held chord (MASTER as at power-on): ANALOG peaks -3.4..-4.0 -> -5.6..-6.5 dBFS, RMS -14 -> -17.2..-17.9 dBFS
   (with the bass: RMS -15 dBFS); TINE EP peak -4.1 -> -8.1 dBFS, RMS -24.9 -> -30.2 dBFS. The bass trimmed by 6 dB
   too would keep the ANALOG chords with the bass at 3-11 % (CZ STRINGS 82 %). 2026-10-06: the bass is now trimmed by
   6 dB too, and the loud sounds of the bank carry a per-sound trim after LEVEL (Defaults: CZ STRINGS, CZ ORGAN, CZ
   BRASS, CHOIR, STRINGS): every chord sound with SUB BASS is limited at most 8.8 % of the hold.
3. **A part's own voice was restarted in place** (`voice.c` `voice_fade_steal`, set by `cr_out.c`). With 6-note
   chords (FM6's cap is 6, the budget 8) every chord change takes the old chord's releasing voices; the new note
   started at the old note's level with its tail cut ("a new note cuts the previous one's envelope"). Now the voice
   fades over one block (0.7 ms, as a voice taken from another part) and the note starts on it in the next block
   (`vsq`, 0.7-1.5 ms later; a note-off meanwhile drops it; a voice not rendered yet is still reused at once). The
   envelope after a change is 0.2-1.7 dB closer to a render with no stealing at all; a sampled engine (PIANO)
   retriggering a sounding note fades it the same way instead of restarting its sample at full level (its click at
   the Dmaj -> Dmaj7 re-press is gone; since the all-synth cut no ChoralRoot engine is sampled, the path stays for
   Felucca's). Felucca's behaviour (flag 0) is unchanged.
4. **The delay's read tap jumped on a tempo change** (`fx.c` `DLY_XF`, under `fx_smooth`: Felucca's buses stay bit-exact): every SELECT detent (and every MIDI-clock tempo
   update) clicked while the delay rang. A new delay time now crossfades over 11.6 ms.

On a chord change the engine itself behaves as designed (`cr_engine.c`, design.md 17, unchanged): a note shared by a
new chord while the old one is held is retriggered (note-off + note-on in the same tick), a re-press of a root or a
Chord Type press re-strikes the chord, an Add Note keeps the shared notes sounding. In `voice.c` a retrigger of a
sounding note keeps its phases and continues its envelope from the current level (FM6: its gains too), so it does not
step; only sampled engines restarted their sample (fixed above).

### The audio ISR lighter (0.18, 2026-10-11)

An audit of the ISR for the 0.18 effects (a CZ-1 chord's part read 1699..1985 us of the 2902 us half on the unit).
Everything here leaves the output sample for sample as it was: `tests/regress.c`'s 197 goldens, the emulator's 52
WAVs (the same SHA-256 before and after: `test_cr.sh`, `test_persist.sh`, `perf.sh` with and without Dual Core) and a
side-by-side build of the two trees hashing the whole mix (every engine's presets on both parts, the sends on and
off and ramping, FX off, pan, LOWCUT off / on / BASS+, both limiters, a bend, TUNE, engine changes under notes; with
ChoralRoot In recording on and off, its six channels hashed too). The figures are counted in `build/choralroot.dis`
(instructions on the usual path, over the unit's 205 a microsecond; jump-table bytes are not instructions); the
emulator's estimate is not a device figure.

| what | before -> after | a half |
| --- | --- | --- |
| CZ-1, a two-line voice (docs/CZ1.md) | 50 800 -> 16 100 instructions | 248 -> 79 us a voice; eight: 1 980 -> 630 us |
| parts 2 and 3 (`fx.c MIX_NPART`) | ~360 -> ~36 a part and block | -12.6 us |
| `track_render_x`: TUNE and the bend | ~30 a sounding part and block, four divides | -1.2 us |
| the FX hold layer (`FELUCCA_PERFORM 0`) | ~84 a block | -1.6 us; XIP -5708 B, RAM -268 B |
| the master (`master_block`) | ~120 -> 97 a sample (LOWCUT off; BASS+ ~249 -> 223) | -14.4 us |
| the mix of a part without sends (`mix_loop`) | ~42 -> 21 a sample | -13.1 us a part; with sends 61 -> 64, +1.9 us |

- **Parts 2 and 3.** ChoralRoot's units play the chord and the bass part: `cr_out.c`'s `midi_event` and the
  engine's routes are parts 0 / 1, and a part above `ENG_STATE_NPART` has no engine state. `mix_block` still ran
  the matrix, `track_render_x` (its divides, two cleared buffers, a scan of 16 voices) and `mix_part_out` for them.
  They now do `mix_part_idle`: the LFO's tick and the SLICER's step clock (a stub in the unit). The tick stays
  because `voice.c lfo_rand` draws part 3's S&H value from the shared generator at every LFO cycle, whatever its
  wave, and the engines' noise and the notes' RAND follow that sequence: without it the mix's hashes differ (PHYS's
  noise), with it they do not. Felucca (`FELUCCA_SEQ 1`: four parts with state) is as it was.
- **`track_render_x`** works out TUNE and the bend (six divides) for the first voice that sounds, and not at all
  when they are 0. The bend's smoothing, the clears, the LFO and the engine's block are where they were.
- **The FX hold layer** (`perform.c`, Felucca's REPEAT / REVERSE / TAPE STOP ... on the master) could never run
  here: its keys come from `seq.c` (`keyboard_block`: `kb_layer`, `perf_press`, which also returns at once without
  the SLICER), its knobs, solo and kill from `ui_layer.c` and `ui_input.c` (`perf_mask`, `kb_mask`, `perf_k`,
  `perf_solo`, `perf_kill`): none of them is in a `FELUCCA_SEQ 0` unit and no `cr_*.c` names one. So `perf_begin`
  took its idle path every block and returned 0, `pf.mute` stayed 0. `FELUCCA_PERFORM` (`fx.c`, default 1; 0 in
  `choralroot.c` and the emulator; an `#error` with `FELUCCA_SEQ 1`) compiles the layer and its four call sites in.
  Felucca's suite and `tests/perform_test.c` keep it; `tests/target_budget.py` holds its five functions as optional.
- **The master.** `master_out` was a call a sample that loaded and stored the DC blocker's, the low cut's and the
  limiter's states and read `fx_lowcut` three times. `master_block` runs the block in place on the output's frames
  with the states in locals; LOWCUT off / on / BASS+ and the two limiters are six loops, picked once a block
  (+2.8 KB). `fx_lowcut` is the main loop's, which does not run under the ISR: read once.
- **The mix.** `mix_part_loop` is built eight times: with or without the stereo side, the sends, USB audio's tap.
  A part whose three sends and their ramps are 0 (the bass as a rule; every part with FX off) has no send in its
  loop; the tap's test left the others. Each is a function of its own (`mix_loop`, `_f`, `_s`, `_sf`, `_u`, `_fu`,
  `_su`, `_sfu`): the compiler folds two calls of one inlined function that differ in a constant back into one loop
  with the test in it (it did, before they were apart).
- **RAM and POOL.** The splash (`cr_shim.c cr_splash`) and SAFE MODE's erase screen (`cr_ui.c cu_flash_erase`) each
  had a static `cr_screen_t` (2436 B); both build on `cu_scr`, the pool's frame the UI clears every time: RAM
  -4880 B. `gfx.c`'s canvas is `CV_ROWS` rows (124: Felucca's graph strip); `choralroot.c` and the emulator set 120,
  `cr_draw.c`'s strip and its two DMA buffers (its `_Static_assert`): POOL -1920 B. CZ-1's definitions took 824 B of
  it.
- **The link.** The app is linked with `--gc-sections` as the loader and the stub are (`tools/build.py`; `app.ld`
  KEEPs crt0, `.ram_text`, `.c1_text`). 27 functions nothing refers to leave (`cr_engine.c`'s, `cr_loop.c`'s and
  `cr_settings.c`'s API this unit does not call: `cr_seed`, `cr_key`, `cr_mod`, `cr_pad_*`, `cr_loop_bar` ...,
  868 B) and `fm1_vec.S`'s weak `fm1_c1_fault_c`, which `cr_cpu1.h`'s own replaces (8 B): XIP -880 B. Compared with
  the same objects linked without it, every label that stays is the same instruction for instruction (or the same
  data but for pointers to the same place), in the same order; the vector stubs (`fm1_fatal_stubs`), the ISR
  wrappers and `fm1_c1_entry` are named from C (`fm1_irq.h`, `fm1_cpu1.h`) and stay. `.ram_text` is one input
  section, so its 2888 B do not shrink.

The size line: `XIP 449892 -> 458472 B` (78.5 -> 80.0 %: CZ-1's loops and table +10.3 KB, the master's and the mix's
loops +4.2 KB, the FX hold layer -5.7 KB, the link -0.9 KB), `RAM 85268 -> 80136 B` (86.7 -> 81.5 %), `POOL 328192
-> 327096 B` (95.4 -> 95.1 %).

`tools/emu/perf.sh` (g), a six-note CZ STRINGS 2 chord, as a sanity check only: the block 241 294 -> 155 480 host
instructions on average (-36 %), the worst 409 128 -> 248 000 .. 278 000 (two runs); its `part0` stage 514 -> 220 on
average. The other scenarios: 7 800 fewer a block (-3 .. -5 %: the master, the mix, the idle parts). (Its script's
bass lands on PHYS MARIMBA since the engine picker lost five engines: the bass is no CZ-1 voice there.)

### Fixed: the UI frame with the loop ring

With the loop playing (scenario c) the UI frame cost ~49 ms on average and up to ~220 ms on the device scale (host:
12.8 M instructions a frame): `cr_draw.c` drew the ring (`cr_arc` twice, the dotted circle and the progress, 16
samples a pixel with an `atan2`, a square root and two divides each) into all six strips at every change of its
fraction. Now (CR_SCREENS.md, Strips and the cache) the ring's coverage is a table
(`cr_ring`, 6.1 KB; since 2026-10-10 a `const` in flash, `firmware/src/cr_ring_table.h`, generated and checked by
`tests/cr_draw_test.c` (`CR_RING_GEN=1 sh tests/run_cr_draw.sh` rewrites it), no POOL; pixel for pixel `cr_arc`'s), and when only the fraction moved only the strips its
tip crossed are drawn. Scenario (c): 12.8 M -> 2.4 M host instructions a frame on average (49 -> 9.4 ms device
estimate; the same build with no ring at all: 1.2 M); the loop playing on the chord screen alone: 1.0 M a frame
(3.9 ms), 0.58 M with no ring. The worst frames of (c) (50 M, ~194 ms) and (e) (30 M, ~115 ms) were other screens'
full redraws, the same with or without the ring (fixed below).

### Fixed: the slow frames (2026-10-06)

`EMU_UI_LOG=N` (tools/emu/README.md) lists the UI frames above N M host instructions with the screen drawn. Scenario
(c)'s were all the **ARP view** (the in-motion screen, `cr_p_arp`): 8-13 M every frame the arp moved and 50 M when
the hop wrapped from the last note to the first; (e)'s the **EDIT page** (`cr_p_params`): 5.5 M a full redraw, 30 M
with a tall wave glyph. Instrumented (instructions per part of `cr_compose`), the panel was all of it, and in it
`cr_poly`: it tested each of a pixel's 16 samples against every segment of the polyline over the polyline's whole
box (the arp's dotted hop: `cr_quad`, 16 segments over a box up to 200 x 50 px; the WAVE glyph 44 segments, SAW,
ENV), times the strips the box crosses; the knob glyphs' `cr_arc` came next (an `atan2`, a square root and 32
divides for each pixel of the band). The glyph rasters were not the cause: a full six-strip chord-screen redraw with
the squeeze is 2.2-3.7 M. Fix (`cr_gfx.c`, CR_SCREENS.md Shapes): `cr_poly` keeps per row the segments whose box
(grown by half the width) reaches the row's samples and per pixel those reaching its sample columns, skipping a pixel
none reaches, then runs the same per-sample test on those only; `cr_arc_px` decides a pixel whose centre angle is
inside / outside an undashed sweep by more than any sample's angle can differ (|offset| x 10430 / radius) without the
samples' angles. Both are exact: 20 000 random polylines (dashed, quads, every strip) and 75 M arc pixels (random
arcs, the knobs, the ring) compare equal to the old code, and tests/run_cr_draw.sh's 58 PPMs are byte-identical.
Host instructions per UI frame, avg / max, before -> after: (a) 0.20 / 2.24 M -> 0.19 / 2.15 M; (b) 0.08 / 2.32 M
-> 0.08 / 2.32 M; (c) 2.41 / 49.98 M -> 0.78 / 3.74 M; (d) 1.02 / 3.22 M -> 1.01 / 2.98 M; (e) 3.06 / 30.45 M ->
1.74 / 8.81 M (device estimate of the worst: 194 -> 14 ms (c), 117 -> 34 ms (e)). Left in (e)'s worst frame: two
knob glyphs, the SAW glyph and the column texts.

### The arp screen back on the Chord view (2026-10-10)

Section 5's rule. A hop is one drawn frame a step, three strips of six (`cr_arp_strips`): 0.92 M host instructions
(3.6 ms device estimate, 1.1 M / 4.3 ms the worst), 1.6 M (6.1 ms) with the loop's dial in the same frame; the
whole screen (the press, a chord change) 2.0 M (7.6 ms; worst 2.7 M / 10.5 ms), a chord-view redraw 1.1 M avg /
2.3 M worst; the frames between draw nothing (0.1 ms). Before, with the arp running, the chord view drew nothing at
all. tools/emu/perf.sh, UI frame avg / max (device estimate), before -> after: (c) 3538 / 15928 us -> 2554 / 15610
us and (i) 3553 / 15578 -> 2585 / 15578 (the loop's chords under the held one no longer squeeze the big name at
every change: the arp screen redraws it plain); (a) (b) (d)-(h) (j) unchanged within 1 %. The audio block is
untouched: (c) 849 us avg / 1031 max of 2902 -> 850 / 1075 (29 % / 37 %), no block over the budget in any scenario.

### Keys while a frame draws (0.18)

**The path of a key, and where it waited.** The matrix is scanned by TIMER5 (10 kHz, a column a tick, a frame of 11
columns every 1.1 ms; `hal/fm1_input.h fm1_input_tick`), which outranks the audio ISR and is masked by nothing in
the draw. A press is debounced there (two frames closed: 1.1 to 2.2 ms after the contact) into `fm1_in.notes` and
its edge. From there on the key is the main loop's: `cr_ui_input` (cr_ui.c) decides what it means (a note, a chord
key, B3, a layer's pick, a letter of a name) and posts the engine's event to `cr_evq` (`cr_post`); the audio ISR
drains the queue before each 32-frame block of the half it renders (`cr_audio_block`), the voice starts in that
block, and the half leaves the DAC 2.9 ms later.

| stage | time |
|---|---|
| contact to the debounced press (TIMER5) | 1.1 .. 2.2 ms |
| debounced press to the main loop's scan | 0 while the loop idles (it scans continuously for the rest of each 15 ms); **up to the whole pass** while it works: a frame's redraw `T`, a sound's load, a save |
| posted to applied (the next half's render) | 0 .. 2.9 ms |
| applied to the DAC | 2.9 ms |

So until 0.18 a key struck just after a frame's scan sounded `8.0 ms + T` after its contact at worst, 4.0 ms at
best. `T` on the emulator's estimate: 15.6 to 16.3 ms for the worst frames of perf.sh's (c) (d) (e) (i) (l) (a
whole-screen redraw of six strips), 8.4 to 9.4 ms in the simpler scenarios, 0.4 to 3.6 ms on average. The unit's
factor on that estimate is not known (1.4 to 2.9 for the audio: docs/FX.md "The cost on the unit"), which puts the
worst `T` between 16 and 47 ms and the worst key at 24 to 55 ms. The draw itself masks no interrupt (lcd.c, gfx.c,
cr_gfx.c, cr_draw.c have no interrupt-off section): a long frame cost key latency, never audio.

**What reads the keys now.** `cr_ui_pump` (cr_ui.c), called from inside the redraw through `cr_draw_idle` (cr_gfx.c):
at each glyph, arc and polyline drawn and after each strip is sent. It runs the note keys' part of the scan
(`cu_scan_keys`, `cu_scan_step`: the same two functions `cr_ui_input` calls), so a key that plays is posted then.
With no key gone down or up since the last scan it is one comparison.

**Why the frame cannot be disturbed.** `cr_ui_draw` builds the screen model first (`cr_build_screen`, then
`cu_animate`, into `cu_scr`), and `cr_draw(&cu_scr, t)` is a function of that model alone: checked, it reads the
model, its own frame values (`cr_frame`, a local), the strip cache `cr_dc`, the canvas `cv_px` and the glyph
buffer, the fonts, the palette and `ux.gen`, and nothing of `cu`, `cs`, `ce`, `cr_snap` or the engine (the scope's
wave, GEEK OUT's lines and the editor's cells are copied into the model). The LEDs are built before the draw
(`cr_ui_frame`). The pump writes none of what the draw reads: the handlers it can reach write `cu`, `cs` and the
engine's queue. So the frame on the screen is the one that was built, and the next frame shows the key.

**What it will not do.** The meaning of a key is the UI's state, and most of that code is not re-entrant: a layer's
key can load a sound from the flash, save, or draw (`cu_flash_erase` calls `cr_draw` itself). So the pump handles a
key only where `cu_key_press` / `cu_key_release` can do nothing but post to the engine (a note, a chord key with its
LOCK latch, B3's tap), and stands back, leaving every edge untaken for the frame's own scan, when:

- a button is down, or was pressed or let go since the last scan, or a hold is armed (`fm1_in.buttons`, the edges,
  `cu.bheld`, `cu.armed`): a button's event comes before a key's, and a key during a hold opens a layer;
- the root keys belong to the open layer (`cu_layer_keys`: KEY's pick, BASS, SEQ, the view menu, the PLAY menu
  with Keys = Pick) or to the naming page (not to Options since 0.19: the keys play there, so they are pumped);
- the panel is being calibrated.

`cr_draw_idle` is set by `cr_ui_draw` around its own `cr_draw` only: the splash's draws, and `cu_flash_erase`'s
(reached from inside a scan), run without it, so the pump is never entered from a handler.

**Order and grouping.** The engine has no time window for a chord: it takes each event as it comes, and what a
root plays is the chord keys down at its press. While the loop idles (most of every 15 ms) the keys already reach
it in the order played, a scan apart. Inside a frame they used to arrive together at its end, the chord keys before
the roots whatever the order played (the scan's index order); now they arrive as they do everywhere else. The
debounce (TIMER5) is untouched, and within one scan the order is the same code as before.

**What still waits.** The part of a pass outside the redraw: the frame's own scan and what its handlers do (a sound's
load: `st_read` masks the interrupts about 0.5 ms a page; a global save's erases, 45 ms each, the audio silent
anyway), the LEDs and the screen model. The pump is not called there: those run inside the handlers themselves.

**Measured** (the emulator; `tools/emu/README.md` "A redraw in simulated time"):

- The longest stretch of a UI frame in which no key is read (`EMU_UI_GAP=1`, device estimate from the host's
  instructions): 15.0 to 15.3 ms before (the whole frame; 8.2 to 9.2 ms in (a) (b) (k)), 1.4 to 2.7 ms now (one
  large glyph, or a strip's hash and blit). With the unit's unknown factor: 2 to 8 ms.
- `tools/emu/scripts/cr_keylat.txt` (test_cr.sh): eight keys struck as a frame of six strips begins, just past its
  scan, a strip taking 2.7 ms of the device's clock (a 16 ms redraw): the note-on reached the engine after 17 to
  19 ms before, 4 ms at most now; with 7.8 ms a strip (47 ms): 49 ms before, 9 ms now. That model moves the clock
  a strip at a time, so it shows the reads after each strip only: an upper bound.
- perf.sh's (c) (d) (e) (i) (l) with 40 keys struck at random times (tools/emu/keylat.py; its lines are in
  perf.sh's output), key's scan to engine, average / worst, before -> after: 16 ms redraws: (c) 2.5 / 14 -> 1.5 / 4
  ms, (d) 9.7 / 18 -> 1.6 / 4, (e) 1.2 / 6 -> 1.1 / 2, (i) 2.0 / 13 -> 1.2 / 4, (l) 2.0 / 11 -> 1.1 / 4; 47 ms
  redraws: (c) 12.2 / 43 -> 3.6 / 9, (d) 26.5 / 48 -> 4.3 / 9, (e) 1.2 / 9 -> 1.1 / 2, (i) 12.5 / 44 -> 2.9 / 9, (l)
  9.6 / 49 -> 3.6 / 9.
- With frames that take no time (every other headless run) nothing changes: the WAV and the last LCD frame of
  every script in tools/emu/scripts (139, perf.sh's among them) are the same bit for bit as before. The UI frame's
  cost did not rise (host instructions, perf.sh: 1.5 to 3.5 % lower in every scenario, the compiler's layout; the
  pump adds a pointer test a glyph); the audio ISR's static cost 14739 -> 14748 (tests/target_budget.py).

**On the unit**: 78 PEAK (`fm1_install.py --debug`) appends `key_ons`, `key_pick_us`, `key_eng_avg_us`,
`key_eng_us` (cr_out.c `cr_klat`; 0.22 appends `c1_bar` after them, docs/DUALCORE.md "The persistent fallback"): the note-ons timed since the last read, and from the first scan that saw the key
closed (the debounce in it): the longest until the main loop posted the note-on, the average and the longest until
the audio ISR applied it. Expected: `key_pick_us` 1.1 to 2.2 ms plus the longest stretch above, `key_eng_us` up to
2.9 ms more; a `key_pick_us` of a frame's length would mean a key waited for a redraw.
