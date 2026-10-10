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
`mod.c`, `voice.c`, `slicer.c`, `fx.c` (with `perform.c`), `usb.c`, `midi_uart.c`, `audio.c`, `panel.c`,
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
the message "engine retired: init sound" (and the trace line `load: slot N engine E retired -> INIT on VA`). ANALOG
(engine 0) is retired too since 0.14 (`FELUCCA_ANALOG` 0 here, in the emulator, `tests/regress.c` and ChoralRoot's host
units through `tests/hostsim.c`; default 1, so Felucca's unit keeps it): its code, presets and pool are out of the
image; a user sound saved on ANALOG (a 0.13 slot) loads on the VA with its macros carried by name (CUT RES DRIVE MIX
DTN, the filter envelope to FENV, ATK / REL; `cr_ui.c cu_analog_va`, the VA's init patch under them), an added preset
of the VA's pool (`cr_bank.c cb_rec_engine`; its binding to an ANALOG factory preset is dropped), with the message
"ANALOG retired: on the VA" (trace `load: slot N engine 0 (ANALOG) retired -> VA, its macros`).
Felucca's own unit (`felucca.c`) and its host suites (`tests/hostsim.c`, `regress.c`) keep every flag at its default
(1): nothing of Felucca changes. Felucca files touched only for the flags: `core.h` (the defaults, `NENG_SHOWN`),
`engines.c` (the includes, `ENGINES[]`, `ENGINE_ORDER`, `eng_ok`, `TRK_DEF`, the retired slot and the shims above),
`gfx.c` (the keycaps behind `FELUCCA_KEYCAPS`), `tests/hostsim.c` (`FELUCCA_SEQ 0`: the sound side only, without its
own renders); `voice.c`, `fx.c`, `audio.c`, `usb.c`, `main.c`, `params.c`, `mod.c` and `upreset.c` are unedited. The emulator (`tools/emu/emu_firmware.h`), `tests/cr_trans_test.c` and
`tests/cr_draw_test.c` set ChoralRoot's flags as `choralroot.c` does.

**Melodee's FM6 and voice model (2026-10-07, docs/FM6.md).** FM6 is Melodee's (Kerem Kilic's fork of Felucca): Dexed's
rendering, DX7 SysEx, the 32-voice bank (`eng_fm6.c`, `fm6_core.c`, `eng_fm6_rom.h`, `fm6_bank.c`, `fm6_store.c`, the FM6
tables of `tools/gen_tables.py`), with the platform it needs merged into the kept files: `core.h` (`NVOICE` 16 slots a
part, `NPOLY` 8, `VBUDGET` 16 units: an FM6 voice one, any other two, so the others keep their eight; the `engine_t`
hooks `alloc legato mono_key post cap units`, `vmod_t.plog`, `voice_was`, `track_t`'s `foot breath porta bend_raw`, the
`F_OFS F_FMNOTE F_FMFRQ` kinds), `voice.c` (the budget in units, the hooks; ChoralRoot's fade-and-defer steal, `render2`
and `part_side` kept, an engine with `alloc` steals in place as Dexed does), `engines.c` (`eng_state[NPART]`: PHYS's,
WHEEL's and FM6's runtime state in one pool union a part, cleared at an engine switch), `fx.c` (Melodee's saturation of
a loud part; the limiter, trim, PAN and stereo lines kept), `mod.c` (CC 2 / 4 / 5 / 65), `params.c` (the new kinds),
`usb.c` (DX7 frames to `fm6_sx_byte`, SysEx bytes sent as single-byte packets), `midi_control.c` / `cr_out.c`
(`bend_raw`). The other engines render bit for bit as before. `FM6_POLY` 8 in this unit (Melodee: 16). ChoralRoot's
additions: FM6's deep pages and patch blob, the FM6 patch store `fm6_ustore.c` (one blob per user slot, storage objects
`OBJ_PROJECT0 + 1, + 2`, backup ids 10 and 11), included by `upreset.c` as `va_store.c` is; `fm6_store.c` after
`cr_ui.c`. Felucca's own unit (`felucca.c`) is no longer kept building against this platform (its web editor's FM6
protocol, `editor_fm6.c`, and its backup of the FM6 bank expect Felucca's layouts); `tests/run_tests.sh` reports
which of its suites break (docs/FM6.md).

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
it (the engine picker, the popups "FM TONE · 03/17", the save page "FM TONE · 17", the Sounds page / CLI, backup
object 22 "FM TONE patches"); the code name, files, flags and the patch kind `quad` stay. ANALOG retired (above;
`ENGINES[0]` is `ENG_GONE`, `cr_bank.c`'s ANALOG trim row dropped, `tests/target_budget.txt`'s `analog_render` an
optional entry). `ENGINE_ORDER`: FM6, FM TONE, VA, CZ-1, PHASE, LOFI, VOICE, TRIO, WHEEL, PHYS, NOISE: eleven engines, a
white root each in the pickers (D4..G5). The bass at power-on (and for a settings record whose bass was ANALOG's) is VA
DEEP SUB. perf.sh (c) / (i) now run on the VA bass DEEP SUB (ALGORITHM +18): (c) ISR 30 / 41 % (device est. avg / max),
(i) 29 / 42 %, no holes, no clicks, no erase under sound.

Build flags stay Felucca's; `FELUCCA_SLICE=0`, `FELUCCA_SLICER=0`, `FELUCCA_FM4=0`, `FELUCCA_UAC=1` (since 0.14 Melodee's
USB audio recording: ChoralRoot In, docs/USB-AUDIO.md), `FELUCCA_UART=1`,
and the all-synth set `FELUCCA_SEQ=0`, `FELUCCA_SAMPLE=0`, `FELUCCA_GRAIN=0`, `FELUCCA_DRUM=0`, `FELUCCA_ICONS=0`,
`FELUCCA_KEYCAPS=0` (BUILDING.md).
The package identity becomes `FM-1_920` and the version string `ChoralRoot 0.1`.

## 2. Parts and streams (`cr_out.c`)

Felucca has four `track_t` parts sharing a budget of 8 voices (since Melodee's voice model, 16 units: an FM6 voice
takes one, any other two; FM6 plays up to 8 voices a part here, the other engines their 8 shared). ChoralRoot uses two:

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
- CC 7 → the part's LEVEL, CC 91 / 93 / 94 → its reverb / chorus / delay send (part 0's are the FX amounts), program
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
LOCK:         k = 6 (B3) → LOCK on / off (a tap; lit while on; off at power-on, not saved)
root keys:    k ≥ 9 → cr_key(c, 53 + k + 12*octave, vel, down)   (vel from Options > Velocity)
layer open:   k ≥ 9 → the layer's map instead (tonic / mode / slot / effect / engine)
```

**LOCK** (B3) is a mode toggle for the chord block, done in the UI as a virtual hold (`cu_mod_press` /
`cu_mod_release`, `cu.mlatch`): a latched chord key stays posted down to the engine (CRE_MOD down, no up), so
every play style (Simple / Advanced / Free) sees it held. LOCK off: the chord keys momentary. LOCK on: the chord
keys held are latched when released (their LEDs stay lit: the engine's `mods_active`); with no chord key held, a
top-row key (DIM MIN MAJ SUS) **resets** the latch to that type alone (the others posted up, the extensions
cleared) and a bottom-row key (6 m7 M7 9) **toggles** its extension (off: posted up at once, its release does
nothing). Hold MIN, release: Dm on D4; 6: Dm6; 9: Dm6/9; 9 again: Dm6; MAJ: D. LOCK off and PANIC post the
latched keys up (`cu_unlatch`). The top line shows a small white `lock` (after the perform mode, the octave and
Latch). Traces: `lock: on` / `off` / `latched MIN 6 9`, and `chord: Dm6 9` as the sounding chord changes.

Buttons (`panel.btn[]` ids): **tap** = release < HOLD_MS (300 ms, Felucca's `settings_hold`) with no key, knob or
other button touched meanwhile: always the button's action, also while its own layer is open. **Hold** = past
HOLD_MS, or a key / knob / button touched meanwhile (a combo, no tap): a layer button opens its layer **and locks
it** (`cu.lock`): it stays open after release, its LED blinks; **OCT− (back) or HOME closes it** (OCT− in a layer
is never the octave; OCT+ is OK, or the loop picker's action), holding another layer button switches to that layer.
The lockable layers: KEY, PERF, FX, BASS, LOOP, METRO and the engine picker (EDIT held: a preview, OCT− cancels,
OCT+ / EDIT keep). A layer opened from the sound editor closes back to the editor (OCT− / HOME). SAVE held (the loop slots) is momentary; OPT is the knob shift;
a button pressed during another's hold is that hold's combo (its release does nothing).

| printed | role | tap | hold (locked open unless noted) |
| --- | --- | --- | --- |
| SEL | KEY | Key Mode on/off | the knob row with the keyboard as its band: roots = tonic (MIN held: minor); KNOB 1–4 Tonic Scale Transpose Single as cells, the turned cell hot |
| ARP | PERF | performance on/off (LED lit while on, the only indicator: no message; the view never changes, the top line says the mode) | the knob row: SELECT = mode (picking one turns it on), the roots play the chord; **OCT+ = on/off** (the label "perform · on" / "· off"); KNOB 1–4 = the mode's params as cells; no popups, the turned cell hot 800 ms |
| FX | FX | main effect on/off | the knob row: SELECT = effect, the roots play; **OCT+ = on/off** (label "fx · on" / "· off"); KNOB 1–3 params, KNOB 4 amount (on: FX on); no popups, the turned cell hot 800 ms |
| ENV | BASS | bass on/off | the knob row: KNOB 1–4 BEHAVIOUR REGISTER SOUND LEVEL as cells (no popups, the turned cell hot); BASS held + EDIT = the bass sound's editor, + SAVE its saving |
| LFO | LATCH | latch on/off (Sticky Keys; also holds a performed arp / pattern after the keys are released: Hold defaults off in every mode, an arp stops when the keys are up; Hold is STRUM's KNOB 4) | — |
| GLO | OPT | Options (picker pages) | shift (momentary): OPT + KNOB 1 split point, + SELECT metronome level, + ALGORITHM bass level; the other knobs keep their job |
| EDIT | EDIT | the sound editor (again: leave; in the engine picker: keep its sound, close it) | the engine picker (a preview: OCT− cancels, OCT+ keeps) on the white roots, KNOB 1 its presets, KNOB 2 init, KNOB 4 roots engines / play |
| HOME | HOME | close the layer / page / menu (the view unchanged); on the view: next View **unless the view is locked** | **600 ms: the view menu** (`L_VIEW`, a picker: Chord · Keyboard · Notes · Geek Out · Scope by SELECT / the white roots, the last line "Lock: On / Off": KNOB 1 or the sixth white root; OCT+ ok, OCT− / HOME back; Settings v9 `view_lock`, the same view as Options > View) |
| SAVE | SAVE | save sound (naming; in it: save, as OCT+) | save / load / delete loops (momentary, while held; OCT+ does it; a plain picker, no ring) |
| SEQ | METRO | metronome on/off | the knob row: the time signature over Click (KNOB 1, hot; no popup) and three empty cells |
| PLAY | LOOP | play / stop | the knob row, no ring: slots on white roots, D#4 CLEAR (hold), F#4 UNDO; KNOB 1 the length picker, 2–4 Quantize Count-in Level cells (hot, no popups); playing: the action picker, OCT+ does it, Sync dim, the dial |
| REC | REC | record / overdub arm | undo the last layer (not a layer) |
| OCT−/OCT+ | | octave −2..+2; in a layer or picker: back / OK | **both: PANIC** (the LOCK latch cleared) |

Layer footers read "… · OCT-: back · HOME: home" (PERF / FX: "SELECT: mode · OCT+: on/off · OCT-: back"); the emulator's log has `layer: open N (locked)` / `layer: close N`.

Knobs (`fm1_enc_take(panel.enc[role])`, one step per detent): KNOB 1 → `cr_voicing_step`,
KNOB 2 → `cr_bass_voicing_step`, KNOB 3 → the current perform mode's main parameter, KNOB 4 →
the selected effect's amount, PRESETS → sound ±1 (loads at once, as Felucca's preset_step),
ALGORITHM → bass sound ±1 (position 0 = OFF), SELECT → tempo ±1 (in a picker: move; on an
EDIT page: next page). Every knob turn also sets `ui.popup = {knob, until_ms}` so the view shows
the knob's meter/dial for 900 ms (§5) — except in the FX layer, a **knob row** (`CR_K_KNOBROW`, `cu_fx_cells`): the
effect's picker over KNOB 1–4's cells (labels `gname` capitalised: Size Damp Type / Rate Depth / Time Feedback Colour;
the values as the popup showed them, enums and Hz through `param_format`: "90", "Room", "1/8", "0.80 Hz"; KNOB 4
"Amount" = `fx_amt * 99 / 127` or "off"); glyphs room (SIZE; TYPE Room), spring (TYPE Spring), moon (DAMP inverted,
COLOUR), echoes (TIME and FEEDBACK: the division's index, feedback / 120), lfo (RATE and DEPTH), mix (the amount;
Drive: clip). A turn changes the value as before and marks its cell hot for 800 ms (`cu_hot` {layer, knob, until},
cleared when a layer opens or another item is picked). The PERF layer (`cu_perf_cells`) and the BASS layer
(`cu_bass_cells`) are knob rows the same way: PERF's cells are the mode's `CU_PERF_KNOB` parameters, labelled as the
popups named them (Rate Division Direction Range Gate Swing Pattern Amount Hold), glyphs echoes (rate, division;
pct2 1.0), arrow (direction), range, gate, bar (slop amount, pattern), else text (the table in cr_ui.c); BASS's are
Behaviour (text: Chords / Unison / Single / Solo), Register (shift, -2..4 from `cr_snap.bass_voicing`), Sound (the
popup's number and name, "off"), Level (bar). Traces `perf: cells ...`, `perf: knob Arp division 8`, `bass: cells
...`, `bass: knob 4 level 84`; traces `fx: effect Delay`, `fx: cells Time Feedback Colour Amount`,
`fx: knob 2 Reverb damp 64`. KEY (`cu_key_cells`, `kr_band` 1: the keyboard strip as the band, the tonic lit yellow):
Tonic (text), Scale (Major / Minor), Transpose (shift, (v + 24) / 48, "+5"), Single (Full / Split). LOOP
(`cu_loop_cells`): Sync (range, index / 5, the length; `CR_CF_DIM` while playing), Quantize (echoes, index / 6, pct2
1.0), Count-in (gate 1.0 / 0.1), Level (bar). METRO: Click (bar, level / 100). Traces `key: cells ...`, `key: knob 3
transpose 5`, `loop: cells ...`, `loop: knob 2 quantize 1/4`, `metro: knob 1 click 60`. OPT + SELECT outside the
METRO layer still shows the click-level popup.

Pickers (`cr_picker_t { items, n, sel, on_change }`): SELECT or a root key moves `sel` (the white
roots index into the list), OCT+ confirms (`on_change` is already live for settings, so OCT+ only
closes), OCT− / HOME leave. Options is a picker of settings whose KNOB 1 edits the value of the
current one.

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
3. an open layer → its screen: KEY, PERF, FX, BASS, LOOP, METRO the `knobrow` (no popup over it: their knob turns never set one; KEY's band the keyboard); SAVE (loops) and the engine picker pickers; EDIT the `params` page
4. Options → the settings picker
5. the View: CHORD (`chord` with squeeze + notes line, `Key:` in the top line, `Rec`/loop status in the top line; the ring only for the count-in, the undo screen, recording / overdubbing and calibration; the corner dial (`dial_on`) while the loop merely plays — on every screen with a top line but Options, the LOOP and SAVE layers included), KEYBOARD, NOTES, GEEK OUT, SCOPE (Felucca's scope buffer)
6. the splash and the screensaver → `stripes` (the ChoralRoot logo): at power-on (the splash, CR_SPLASH_MS with the
   version, the intro slide once) until the first input; then only as a screensaver after **3 minutes without any
   input** (`CR_IDLE_MS` 180 000, fixed: keys, buttons, knobs and MIDI channel messages in are activity, `cu.last_input`;
   a loop playing alone or a chord still ringing is not). Any input brings the view back at once. Before 2026-10-09 the
   stripes came back 3 s after the last chord stopped sounding (and after every PANIC), which read as the logo popping
   up all the time. Test: `cr_panic.txt` (4 s: no stripes; 3 min: the stripes; a key: gone).

## 6. LEDs (`cr_leds()`)

As `ui_input.c: ui_leds` builds `fm1_led` / `fm1_led_dim` per column: every key and button dim
(the glow) unless Options > LEDs = STOCK; chord keys lit while held/latched (`cr_mod_active`; LOCK's latched keys
are posted down, so they are lit); B3 lit while LOCK is on (dim off);
root keys lit where the voiced notes sound (engine query), the sounding perform note blinking,
black roots off the scale dark in Key Mode, layer maps on the roots while a layer is open; KEY,
PERF, FX, BASS, LATCH, METRO lit when on; **the open layer's button blinks** (locked after its hold; SAVE while
held); EDIT blinks while the editor is open; REC blink while recording, lit when armed; PLAY lit when a loop
exists, its green LED when playing; OPT lit in Options; OCT− lit / OCT+ blinking in layers and pickers.

## 7. Settings and sounds (`cr_settings.c`)

Felucca's settings record (`settings_persist.c`) grows a ChoralRoot block: play style, extension
addition, secret scope, key mode + tonic + scale, transpose, single notes, velocity, bass
behaviour + register + sound, perform mode + params, latch, tempo, loop sync/quant/count-in/level,
MIDI channels/enables/clock mode, view, motion, palette (MOD default), LEDs. Saved on change,
deferred while a loop plays (Felucca's `settings_poll`). Sounds (docs/PRESETS.md, 2026-10-07): **one editable pool per
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
  the installer, backup reads and OCT- + OCT+ 5 s still work; a yellow SAFE MODE screen with the crash's boot stage,
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
  layers and pickers = back / OK. Taps: KEY PERF FX BASS LATCH OPT EDIT SAVE METRO; layers: KEY (select-key, MIN held = minor, KNOB 1-3
  tonic / scale / transpose), PERF (7 modes on the white roots, KNOB 1-4 the mode's parameters), FX (Reverb Chorus
  Delay Drive = part 0's sends, KNOB 1-3 the buses' `song.g` parameters, KNOB 4 the amount), BASS (behaviours,
  register, sound, level), EDIT (the engine picker; KNOB 1 the engine's sounds). Knobs: KNOB 1 voicing, KNOB 2 bass
  voicing, KNOB 3 the mode's main parameter, KNOB 4 FX amount, PRESETS the chord sound (Felucca's factory presets of
  the melodic engines, POLY), ALGORITHM the bass (presets named BASS / ACID; 0 = OFF), SELECT tempo (in a picker:
  move); OPT + ALGORITHM bass level. Options: a picker of 18 settings (USB Record and USB Level since 0.14, docs/USB-AUDIO.md), KNOB 1 sets. `cr_build_screen` in the order
  of section 5 (PANIC / message, knob meter 900 ms, layer, page, Options, the stripes (the splash; the screensaver after 3 min without input), the View: CHORD,
  KEYBOARD, NOTES, GEEK OUT; perform on never swaps the chosen view: it shows in the PERF LED and the top
  line's right text only); `cr_leds` as section 6.
- **Sounds** (`cr_bank.c`, `cr_pages.c`, `cr_name.c`; section 7, docs/PRESETS.md): PRESETS turns the pool of the
  chord part's engine, ALGORITHM the bass part's (after OFF); OPT + PRESETS the chord part's engine (a horizontal
  picker while OPT is held, applied on release). The meter: the place big, the name (a square after an overwritten
  factory preset's), "FM6 · 05/26".
  **EDIT tap**: the sound editor (`cr_edit.c`, docs/EDITOR.md; BASS held + EDIT: the bass sound's, SHIFT + EDIT inside
  switches part). Groups -> screens -> lanes: the function buttons become group buttons (FX = OSC, SEL = FILT, ENV,
  LFO, SEQ = MOD, PLAY = FX sends, REC = MIX; HOME or EDIT leaves); a group's tap opens it, tapped again it cycles its
  screens (a hold: nothing, reserved); SELECT moves the lane (the four parameters on KNOB 1-4) and runs on across the
  group's screens, wrapping, so SELECT alone reaches everything. The screens come from the engine's deep pages
  (`eng_deep_t`, by their titles, never by column name): the VA's OSC = the oscillator stack, the "+" stack, the
  oscillator mixer (the four LEVELs as tall bars); FILT = rows A / B under the filter curve; ENV = ENV 1-4, two lanes
  each under that envelope; LFO = the stack, the sync stack; MOD = the 8 slots; FX one lane, MIX two. Each part
  remembers its group, each group its screen and lane. Screens are the dense kinds `CR_K_STACK` and `CR_K_EDIT8`
  (`tall`: the mixer), no header bar, no footer. A detent: 5 % of the range, enums one by one; GLO = SHIFT: a tap
  latches fine steps (one unit a detent; its LED, "fine" in the title line), held it is momentary (`cp_dstep`); deep
  edits mark the sound `*`. Engines without deep pages: OSC = EDIT 1, FILT = EDIT 2, ENV / LFO / MOD the platform
  pages (FM6: "own envelopes" on ENV). A layer opened over the editor (PERF held, the engine picker) closes back to
  it. **EDIT held** (or PRESETS turned inside the editor): the engine picker as a **preview** of the part's sound
  (snapshot: every parameter, the engine, the VA patch blob, `edited`, the user slot); the white roots switch the
  engine (keeping the envelope, filter, LFO, sends and mix), KNOB 1 / PRESETS the engine's pool (the meter
  jumps, no fill), KNOB 2 init, KNOB 4 the roots' job: engines / play (Settings `pick_roots`); OCT− / HOME cancel (the
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
  **Per-sound trims**: the loud factory presets carry a trim (`cr_bank.c` `CB_TRIM`, by engine and preset name: PHASE STRING / ORGAN -8 dB, PHASE BRASS -3 dB, VOICE CHOIR AAH -3 dB, CZ-1 BRASS 1 -5 dB, STRINGS 2
  -3 dB, PIPE ORGAN 1 -6 dB; ANALOG STRINGS -1 dB until ANALOG was retired in 0.14; since docs/PRESETS.md also a user record bound to one of them), signed 0.5 dB steps, a
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
  undo, clear, panic; LOOP / REC / METRO taps and holds, the LOOP layer (slots on D4..F5, D#4 CLEAR, F#4 UNDO,
  KNOB 1-4 SYNC QUANT COUNT-IN LEVEL, the Overdub/Pause/Undo/Clear picker while playing), SAVE held (Save / Load /
  Delete, OCT+), ten flash slots (storage.c's commit protocol on sectors of their own), the ring and `Rec 1.2` /
  `Loop 1` top lines, REC / LOOP / green LEDs, the metronome click (time signature, level). The count-in is a big
  red countdown (4 3 2 1 springing in, "count-in", the ring drawing itself in; over popups, under PANIC); undo shows
  the layers left huge in red ("layers · undo"); KNOB 1 / SELECT in the LOOP layer move its picker (stopped: the
  length, mock-up 8; playing: the action). The loop's sounding notes glow dim on their root keys (`cr_snap_t.lnote`,
  from the 8 loop voices), the player's stay lit. MIDI Clock Out sends 0xFA when the loop starts playing (LOOP from
  stopped, a take committed) and 0xFC when it stops (`cr_out.c`, the ISR; trace `midi:`).
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
  layer locks open on its hold (KEY PERF FX BASS LOOP METRO, the engine picker), its button blinks, OCT- / HOME
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
(section 1; an ANALOG one with its macros). The flash of user sample slots 1-2 (0xA0000..0xC7FFF): 0xA0000..0xB1FFF holds the CZ-1 tone store's second half and the eight CZ-1 banks (docs/CZ1.md), the rest stays free; slot 3's holds the loops.

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
| `stage_<s>_avg_us` / `_max_us` | the ISR's stages per half, the last second (`cr_cpu1.h` cr_dbg): `tick` (the engine, the looper, MIDI, the events, voice allocation), `part0` (the chord part's render), `part1` (the bass part's: core 1's job time), `fx` (DIST / level / sends of every part, parts 2..3, the FX buses), `master` (limiter, USB tap, click), `wait` (core 0 waiting for core 1) | their sum ~ `audio_avg_us` |
| `c1_state`, `c1_jobs`, `c1_stack_bytes`, `c1_start_us` | the second core (docs/DUALCORE.md): 0 off (SAFE MODE), 1 ok, 2 failed at start, 3 gave up on a job; jobs done; its stack's high-water mark of 2816; its start | 1, rising, < 2816 |

The GEEK OUT view (Options > View) shows the third line `isr <us> · late <n>`: the last second's longest half
(`audio_max_all_us`) and the overruns since power-on. After a boot that was not clean (a counted crash reset, or SAFE
MODE) it shows `boot <reason> stage <n>` instead: the reset class and the boot stage the crashed run reached
(`firmware/src/cr_bootguard.h` names them).
The fourth line is the second core (docs/DUALCORE.md): `c1 ok <jobs> · stk <bytes>`, `c1 off` (SAFE MODE), `c1 failed`
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

### Fixed: the UI frame with the loop ring

With the loop playing (scenario c) the UI frame cost ~49 ms on average and up to ~220 ms on the device scale (host:
12.8 M instructions a frame): `cr_draw.c` drew the ring (`cr_arc` twice, the dotted circle and the progress, 16
samples a pixel with an `atan2`, a square root and two divides each) into all six strips at every change of its
fraction. Now (CR_SCREENS.md, Strips and the cache) the ring's coverage is a table computed once at power-on
(`cr_ring_build`, 6.1 KB of POOL, pixel for pixel `cr_arc`'s), and when only the fraction moved only the strips its
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
