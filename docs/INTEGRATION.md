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

MIDI in (`usb.c` → `midi_event`): notes on the CHORD channel play part 0 directly
(`trk_note_on`), on the BASS channel part 1; CC 7/91/93/94 map to level and the sends; program
change selects the sound; clock/start/stop follow Options > MIDI Clock (`IN` uses Felucca's
`midi_clock.c` to set the tempo). No chord generation from MIDI in (Orchid's behaviour).

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
| SEL→EDIT | sound pages (EDIT mode: SELECT = page, KNOB 1–4 = params; BASS held + EDIT = the bass sound) | engine picker on white roots |
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
  wah), MOD (env -> filter / pitch / shape, tremolo), FX sends, MIX (level, pan, voice, glide). SELECT turns pages,
  KNOB 1-4 edit with Felucca's ranges and value text (`track_desc`, `param_format`); the top line is the sound's
  name (`*` once edited) and `ENV 4/8`; the turned column's glyph eases to its value (`cr_tween`, 220 ms). **EDIT
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

### Different from the design above (for now)

- `seq.c` (with `song_chain.c`, `chord.c`, `motion.c`, `midi_control.c`, `midi_clock.c`) is **kept, inert**
  (`song.grid = 2`: its keyboard is silent; its transport never starts): its `events_block` still does the engine
  switches, the releases of a sound load (`panic_req`) and MIDI in (channels 1 / 2 play parts 1 / 2). Replacing it
  with `cr_out.c`'s own `events_block` saves its flash and RAM: do it with the device build's measurements.
- LEVEL and PAN on the MIX page are the part's (Felucca's `param_kept`): saved with a user sound, not loaded by it.
  An FM6 sound's ENV page edits values its own envelopes ignore (the page says so).
- Defaults: the RAW stream off (Orchid), MIDI clock out off, FX on, the bass OFF (ALGORITHM at 0; BASS tap: SUB
  BASS), TINE EP on part 0, the MOD palette. The power-on splash is still `main.c`'s "FELUCCA".

- **The looper** (step 4, `cr_loop.c`, docs/LOOPER.md): semantic events through the engine's `gesture` hook and
  `cr_loop_event` loop voices; Free / 1-16 bar sync with a clicked count-in, quantize on commit, overdub layers,
  undo, clear, panic; LOOP / REC / METRO taps and holds, the LOOP layer (slots on D4..F5, D#4 CLEAR, F#4 UNDO,
  KNOB 1-4 SYNC QUANT COUNT-IN LEVEL, the Overdub/Pause/Undo/Clear picker while playing), SAVE held (Save / Load /
  Delete, OCT+), ten flash slots (storage.c's commit protocol on sectors of their own), the ring and `Rec 1.2` /
  `Count-in -3` / `Loop 1` top lines, REC / LOOP / green LEDs, the metronome click (time signature, level).
  Settings record v2 (metronome, slot); `CR_SETTINGS_BUSY` = a loop plays. Options > Split Point; Single Notes wired
  (`CRE_SINGLE`). Settings load at power-on inside `cr_ui_init` on both builds. The emulator builds with
  `FELUCCA_FLASH 1` (user sounds and loops persist with `--flash`); `--flash / --no-flash / --save-on-exit` are
  emu.c options (`emu_fw_options`).

### Stubbed (screens and gestures only; TODO in the code)

OPT + KNOB 3, MIDI clock in, MIDI start / stop with the loop,
MIDI CC / program change in, the SCOPE view, the calibration screen.

### Next steps

1. **Settings** (`cr_settings.c`): ChoralRoot's block in Felucca's settings record (`settings_persist.c`), saved on
   change (deferred while a loop plays), loaded by `persist_boot`; `panel_setup` on ChoralRoot's screens.
2. **Sounds**: done (above); left: deleting / renaming a user slot, the key LEDs of the naming screen, the
   bank's choice by ear on the device.
3. **The looper**: done (above); left: MIDI start / stop and clock in, loop notes glowing dim on the keys, the
   device's RAM / CPU measurement of the 8 KiB of loop buffers.
4. **The device build**: `tools/build.py` with `choralroot.c` as the unit (the `.fwsc` marker `FM-1_920`, the
   installer's identity), measure flash / RAM / CPU (FM6 chords + bass, section 9 of PLAN.md), decide on `seq.c`,
   the splash.
