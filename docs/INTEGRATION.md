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
`mod.c`, `voice.c`, `slicer.c`, `fx.c`, `usb.c`, `midi_uart.c`, `audio.c`, `panel.c`, `icons.c`,
`storage*`, `upreset.c`, `ota*`, `console.c`, `main.c`; dropping Felucca's instrument: `seq.c`,
`song_chain.c`, `motion.c`, `perform.c`, `chord.c`, `ui*.c`, `project.c`, `editor*.c`, `favorites.c`,
`ui_name.c` (naming is re-done small in `cr_ui.c`). Where a kept file references a dropped one
(`voice.c` → `seq.c`'s `trk_note_on`, `audio.c` → `seq_block`, `usb.c` → `midi_event`, `main.c` →
`ui_input/ui_leds/ui_draw`), `cr_out.c` / `cr_ui.c` provide functions of the same names, so kept
files are not edited (a `#define` shim header `cr_shim.h` where a signature must differ).

Build flags stay Felucca's; `FELUCCA_SLICE=0`, `FELUCCA_FM4=0`, `FELUCCA_UAC=1`, `FELUCCA_UART=1`.
The package identity becomes `FM-1_920` and the version string `ChoralRoot 0.1`.

## 2. Parts and streams (`cr_out.c`)

Felucca has four `track_t` parts sharing 8 voices. ChoralRoot uses two:

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

MIDI out goes through Felucca's `midi_out_q` (USB) exactly as `seq.c` lines 408–424 do today.
Channels default 1 / 2 / 3 (Orchid); each stream has `enabled` and `channel` in settings.
Clock out: 24 PPQN from the engine's tempo on the audio clock (`midi_clock.c` already parses clock
*in*; add the *out* pulse in `cr_out.c` from the same sample counter the loop uses).

MIDI in (`cr_out.c` `cr_midi_in`, pure parts in `cr_midi.c`): the audio ISR drains usb.c's `midi_in_q` (USB and TRS)
at the top of each block, before `seq.c`'s `events_block`, which then finds it empty (its overflow recovery stays).
Only the CHORD and BASS channels of Options (MIDI Perform / MIDI Bass; Off: ignored) are heard:
- notes, pedal, bend, pressure and the other CCs go to Felucca's `midi_event` as channel 1 (part 0) or 2 (part 1),
  so they play exactly as channels 1 / 2 did before, now on the configured channels (`seq.c` is not edited); CC 123 is
  `midi_control.c`'s all notes off on that part.
- CC 7 → the part's LEVEL, CC 91 / 93 / 94 → its reverb / chorus / delay send (part 0's are the FX amounts), program
  change → the chord sound (PRESETS list position: factory bank, then user slots) or the bass sound (ALGORITHM list
  position, 0 = off); out of range ignored. The ISR posts these to a small ring (`cr_min_q`) and the UI frame
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
chord keys:   k ∈ {1,3,5,8} → DIM MIN MAJ SUS;  k ∈ {0,2,4,7} → 6 m7 M7 9;  k = 6 → nothing
root keys:    k ≥ 9 → cr_key(c, 53 + k + 12*octave, vel, down)   (vel from Options > Velocity)
layer open:   k ≥ 9 → the layer's map instead (tonic / mode / slot / effect / engine)
```

Buttons (`panel.btn[]` ids): tap = release < HOLD_MS (300 ms, Felucca's `settings_hold`) with no
key, knob or other button touched meanwhile; hold = past HOLD_MS → the layer opens (`ui.layer`)
and closes on release unless HOME was tapped meanwhile (lock). Exactly Felucca's `ui_layer.c`
gesture (`layer_gesture`), reused with ChoralRoot's table:

| button | tap | layer (held) |
| --- | --- | --- |
| EDIT→KEY | Key Mode on/off | roots = tonic (MIN held: minor); KNOB 1–4 TONIC SCALE TRANSPOSE SINGLE NOTES |
| ARP→PERF | performance on/off | white roots = mode; KNOB 1–4 = the mode's params |
| FX | main effect on/off | white roots = effect; KNOB 1–3 params, KNOB 4 amount |
| ENV→BASS | bass on/off | KNOB 1–4 BEHAVIOUR REGISTER SOUND LEVEL; roots preview the bass |
| LFO→LATCH | latch on/off | — |
| GLO→OPT | Options (picker pages) | shift: OPT + knob = second function; OPT + FX = FX lock; OPT + PERF = perform lock |
| SEL→EDIT | sound pages (EDIT mode: SELECT = page, OPT + SELECT = section, KNOB 1–4 = params; BASS held + EDIT = the bass sound) | engine picker on white roots |
| HOME | leave page/menu/layer; on the view: next View | hold + layer button: lock the layer |
| SAVE | save sound (naming) | save / load / delete loops |
| SEQ→METRO | metronome on/off | beat / time signature picker |
| PLAY→LOOP | play / stop | loop layer: slots on white roots, D#4 CLEAR (hold), F#4 UNDO; KNOB 1–4 SYNC QUANT COUNT LEVEL |
| REC | record / overdub arm | undo the last layer |
| OCT−/OCT+ | octave −2..+2 | in a picker: back / confirm; **both: PANIC** |

Knobs (`fm1_enc_take(panel.enc[role])`, one step per detent): KNOB 1 → `cr_voicing_step`,
KNOB 2 → `cr_bass_voicing_step`, KNOB 3 → the current perform mode's main parameter, KNOB 4 →
the selected effect's amount, PRESETS → sound ±1 (loads at once, as Felucca's preset_step),
ALGORITHM → bass sound ±1 (position 0 = OFF), SELECT → tempo ±1 (in a picker: move; on an
EDIT page: next page). Every knob turn also sets `ui.popup = {knob, until_ms}` so the view shows
the knob's meter/dial for 900 ms (§5).

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
3. an open layer → its screen: KEY `keyboard` select-key; PERF / FX / BASS / LOOP / engine pickers; EDIT the `params` page
4. Options → the settings picker
5. the View: CHORD (`chord` with squeeze + notes line, `Key:` in the top line, `Rec`/loop status + ring while the loop runs), KEYBOARD, NOTES, GEEK OUT, SCOPE (Felucca's scope buffer)
6. idle (no chord sounding, no loop, 3 s after the last note) → `stripes`

## 6. LEDs (`cr_leds()`)

As `ui_input.c: ui_leds` builds `fm1_led` / `fm1_led_dim` per column: every key and button dim
(the glow) unless Options > LEDs = STOCK; chord keys lit while held/latched (`cr_mod_active`);
root keys lit where the voiced notes sound (engine query), the sounding perform note blinking,
black roots off the scale dark in Key Mode, layer maps on the roots while a layer is open; KEY,
PERF, FX, BASS, LATCH lit when on; REC blink while recording, lit when armed; PLAY lit when a loop
exists, its green LED when playing; OPT lit in Options; OCT− lit / OCT+ blinking in pickers.

## 7. Settings and sounds (`cr_settings.c`)

Felucca's settings record (`settings_persist.c`) grows a ChoralRoot block: play style, extension
addition, secret scope, key mode + tonic + scale, transpose, single notes, velocity, bass
behaviour + register + sound, perform mode + params, latch, tempo, loop sync/quant/count-in/level,
MIDI channels/enables/clock mode, view, motion, palette (MOD default), LEDs. Saved on change,
deferred while a loop plays (Felucca's `settings_poll`). Sounds: Felucca's factory presets per
engine + a ChoralRoot bank (`cr_bank.c`: chord-friendly presets in PRESETS order, basses for
ALGORITHM) + the 32 user slots (`upreset.c`), named with the root keys as Felucca's `ui_name.c`.

- **VA** (engine 13, `eng_va.c`, `FELUCCA_VA`; docs/VA.md): a four-oscillator virtual analog with deep pages
  (`eng_deep_t`: OSC / FILTER / ENV / LFO / MOD, 31 pages), its own patch per part (P_E0..P_E7 are macros into it),
  16 chord sounds at the end of PRESETS (25..40) and 4 basses at the end of ALGORITHM (9..12), levels set in the
  presets (trim 0). A user slot saved from a VA sound keeps its patch in `va_store.c` (one per slot, on the unused
  project sectors 0x97000 / 0x98000).

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
`build/emu/test/cr_*.ppm` (`cr_dmaj7.ppm` matches mock-up state 2).

### Wired

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
- **`cr_ui.c`**: the grammar of section 3: chord keys, roots (+ OCT), the tap / hold / combo / HOME-lock gesture
  (one armed button, as `ui_layer.c`), OPT + FX / PERF locks, OCT- + OCT+ = panic (octave reset), OCT in pickers =
  back / OK. Taps: KEY PERF FX BASS LATCH OPT EDIT SAVE METRO; layers: KEY (select-key, MIN held = minor, KNOB 1-3
  tonic / scale / transpose), PERF (7 modes on the white roots, KNOB 1-4 the mode's parameters), FX (Reverb Chorus
  Delay Drive = part 0's sends, KNOB 1-3 the buses' `song.g` parameters, KNOB 4 the amount), BASS (behaviours,
  register, sound, level), EDIT (the engine picker; KNOB 1 the engine's sounds). Knobs: KNOB 1 voicing, KNOB 2 bass
  voicing, KNOB 3 the mode's main parameter, KNOB 4 FX amount, PRESETS the chord sound (Felucca's factory presets of
  the melodic engines, POLY), ALGORITHM the bass (presets named BASS / ACID; 0 = OFF), SELECT tempo (in a picker:
  move); OPT + ALGORITHM bass level. Options: a picker of 16 settings, KNOB 1 sets. `cr_build_screen` in the order
  of section 5 (PANIC / message, knob meter 900 ms, layer, page, Options, idle stripes after 3 s, the View: CHORD,
  ARP in motion, KEYBOARD, NOTES, GEEK OUT); `cr_leds` as section 6.
- **Sounds** (`cr_bank.c`, `cr_pages.c`, `cr_name.c`; section 7): PRESETS browses the ChoralRoot bank (24 chord
  sounds: EPs, piano, pads, strings, organs, choir, brass, mallets, plucks), ALGORITHM 8 basses, each a table row
  (engine, Felucca preset name, display name; the name resolves to the index at boot), then the used user slots
  (ALGORITHM: the slots saved MONO / LEGATO). The meter shows the bank number (a user sound: its slot) and the name.
  **EDIT tap**: the chord sound's pages (BASS held + EDIT: the bass sound's), 8 pages of 4 on `params`: ENGINE
  (engine, its presets, INIT), the engine's EDIT 1 / 2 (its `page_title`, `edit[]`), ENV, LFO (rate, wave, vibrato,
  wah), MOD (env -> filter / pitch / shape, tremolo), FX sends, MIX (level, pan, voice, glide). An engine with deep
  pages (`engine_t.deep`, `eng_deep_t`: the VA) adds its own between EDIT 2 and ENV (OSC 1..4, FILTER, ENV 1..4, LFO,
  MOD 1..8: `get` / `set` per column, the same steps; an empty column draws nothing). SELECT turns pages (the
  columns are dealt in from the side turned to), **OPT + SELECT jumps sections** (ENGINE, the engine's own
  `section[]`, ENV, FX, MIX; turned back: the section's first page, then the previous section; on a sound page it
  replaces OPT + SELECT's click level), KNOB 1-4 edit with Felucca's ranges and value text (`track_desc`,
  `param_format`; a detent: 5% of the range, enums one by one; OPT held: one step, the page wins over OPT's knob
  functions); the top line is the sound's name (`*` once edited, deep edits too) and `ENV 4/8` (deep: `OSC 2 · 4/39`
  with one square per section under it, the current one filled); the turned column's glyph eases to its value
  (`cr_tween`, 220 ms; deep glyphs: the selected waveform, the filter type's curve, the page's envelope, a mod slot's
  source -> destination over its amount). **EDIT
  held**: the engine picker on the white roots (a root switches the engine keeping the envelope, filter, LFO, sends
  and mix), KNOB 1 the engine's factory presets, KNOB 2 init. **SAVE tap** (from a bass page or BASS held + SAVE:
  the bass): KNOB 1 the slot U01-U32 (a used one shows its name), the white roots type (phone style: ABC DEF GHI JKL
  MNO PQRS TUV WXYZ 0123 4567 89-.), D#4 a space, KNOB 2 the last letter, OCT- deletes, OCT+ saves, SAVE cancels;
  the prefilled name (grey) is replaced by the first letter typed. `upreset.c` is back (its Felucca-UI names in
  `cr_bank.c`: no undo copy, no pattern); records are Felucca's (no pattern). Persisted on the device (flash: `persist_boot`
  -> `cr_bank_boot`), RAM-only in the emulator. The emulator's logs get `edit:` / `page:` / `param:` / `engine:` /
  `preset:` / `save:` / `sound:` lines (`CR_TRACE`, set by `emu_firmware.h`).
- **`cr_anim.c`**: `cr_tween`, `cr_spring`, the animation clock and Options > Motion (Full / Calm / Off); the
  squeeze, picker slide, meter fill and spring, the stripes at one bar per cycle and their sweep by the first chord.

- **The SCOPE view** (View 5: Options > View, HOME taps): the master output as one bold 3 px white line over a thin
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

- `seq.c` (with `song_chain.c`, `chord.c`, `motion.c`, `midi_control.c`, `midi_clock.c`) is **kept, inert**
  (`song.grid = 2`: its keyboard is silent; its transport never starts): its `events_block` still does the engine
  switches, the releases of a sound load (`panic_req`) and plays the MIDI notes `cr_out.c` forwards to it (section 2). Replacing it
  with `cr_out.c`'s own `events_block` saves its flash and RAM: do it with the device build's measurements.
- LEVEL and PAN on the MIX page are the part's (Felucca's `param_kept`): saved with a user sound, not loaded by it.
  An FM6 sound's ENV page edits values its own envelopes ignore (the page says so).
- Defaults: the RAW stream off (Orchid), MIDI clock out off, FX on, the bass OFF (ALGORITHM at 0; BASS tap: SUB
  BASS), TINE EP on part 0, the MOD palette. The parts' levels (`cr_ui_init`; LEVEL steps 0.5 dB): the chord part
  92 (-10 dB: Felucca's default 104, -4 dB, trimmed by 6 dB), the bass 92 too (-10 dB: trimmed by 6 dB; 98 until
  2026-10-06), so a 6-note chord stays out of the master limiter (Performance, item 2). A sound's load keeps the
  part's level (the rule above); MASTER makes up the loudness (about 4 dB quieter than before on a held chord).
  **Per-sound trims**: the bank's loud sounds carry a trim (`cr_bank.c` `cb_entry_t.trim`, signed 0.5 dB steps), a
  gain after LEVEL (`track_t.trim`, applied in `fx.c` `mix_part` as LEVEL + trim steps of `LEVEL_Q12`, LEVEL 0 still
  OFF), set by `cu_list_load` when a bank sound loads; 0 for a user sound, an engine preset (EDIT + KNOB 1), INIT, an
  engine switch, and on Felucca (its renders unchanged). LEVEL is never touched. Trims (measured as Performance item 2:
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
  D#4 space), the other black roots dark, OCT- lit, OCT+ blinking. OPT + KNOB 3 toggles the perform lock (once per
  OPT hold), as OPT + PERF locks it.
  Settings record v2 (metronome, slot); `CR_SETTINGS_BUSY` = a loop plays. Options > Split Point; Single Notes wired
  (`CRE_SINGLE`). Settings load at power-on inside `cr_ui_init` on both builds. The emulator builds with
  `FELUCCA_FLASH 1` (user sounds and loops persist with `--flash`); `--flash / --no-flash / --save-on-exit` are
  emu.c options (`emu_fw_options`).

### Stubbed (screens and gestures only; TODO in the code)

OPT + KNOB 2 / 4 ("shift: not yet").

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
   `kb_mask` anyway). **`seq.c` stays**: `cr_out.c` has no `events_block` of its own yet, and `seq.c` holds nothing
   in the POOL. CPU: see Performance below (`tests/target_budget.txt` now holds ChoralRoot's ISR).

## Performance (2026-10-06: pops and glitches)

### Reading it on the device

USB serial console (`FELUCCA_CDC`, the CDC-ACM port; the baud rate is ignored): `screen /dev/tty.usbmodem* 115200`
on the Mac (or any terminal), then `cpu` (`help` lists the rest; `status` and `dbg` are Felucca's). `cpu` prints the
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

The GEEK OUT view (Options > View) shows the third line `isr <us> · late <n>`: the last second's longest half
(`audio_max_all_us`) and the overruns since power-on.

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
| (e) (c) + KNOB 1..4 every 30 ms on EDIT | 23 / 39 % -> 24 / 43 % | 2 -> 0 | 4 -> 0 | 2 -> 0 | 43 / 138 -> 11.8 / 115 -> 6.7 / 34 ms |

Per sound (6-note chord held, FX on / off, device estimate of the ISR): 12-20 % average, 23-46 % worst block, the FX
buses +1 % (no buffer of theirs saturates). ChoralRoot's own ISR work (`cr_audio_block`: the queue, MIDI in, the
engine's and the looper's ticks, the click, the clock) is ~1.4 % over Felucca's idle mix (silent: 7 % against
Felucca's 5.4 %); nothing there is worth moving to the UI frame. The host says the ISR has 2x headroom; the device's
`cpu` readout is the proof (XIP cache misses, the nested TIMER5 and USB audio are not in the host figure).

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
   the Dmaj -> Dmaj7 re-press is gone). Felucca's behaviour (flag 0) is unchanged.
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
