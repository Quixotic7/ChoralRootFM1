# ChoralRoot FM-1 — the looper

`firmware/src/cr_loop.[ch]` (portable C99, no libc, no allocation; host tests `tests/cr_loop_test.c`), driven by
`cr_out.c` in the audio ISR and by `cr_ui.c` (gestures, screens, LEDs, flash slots). Grid reference:
`d_cr_loop.lua`, `d_cr_slots.lua`, `design.md` §14.

## What is recorded

A **semantic event** per chord gesture, written when the gesture ends (or when the capture ends with it held):

| field | bits | meaning |
| --- | --- | --- |
| `t` | u16 | on time, ticks from the loop start |
| `dur` | u16 | length in ticks (>= 1) |
| `root` | u8 | absolute resolved root (transpose and Key Mode applied: the recorded harmony) |
| `vel` | u8 | 1..127 |
| `qx` | u8 | quality (CR_Q_*) << 4 \| extension mask (CR_EXT_*) — the *final* chord (Advanced / Free). A single note (quality 0) has no extensions: its low nibble is its **part**, `CRL_X_BASS` (1) = a bass-side note (below) |
| `layer` | u8 | RAM only (in flash layers are counts): the layer, or `CRL_HID` \| the Replace layer that hid it |

Capture comes from the engine's `cr_out_t.gesture` hook (keyed voices and pads; a same-root retrigger ends one
gesture and starts another). Playback calls `cr_loop_event(c, lv, root, q, ext, vel, on)`: a resolved chord on one
of `CR_MAX_LOOPV` (8) loop voices, bypassing the chord-key state machine; voicing, performance, bass apply live,
and so does Options > 6th with 7th (0.20): an event is a root, a type and extension bits, so a recorded `C7 6` has its
6th in place or an octave up by the setting at the moment the step is struck (docs/CHORD-NAMES.md "The 6th above a
seventh").

**Bass-side notes (Single = Split Bass; since 0.18).** A key played on the bass side is recorded as a bass-side note
and plays back on the bass part: the BASS stream alone (the bass sound, the bass channel on MIDI out), never the
chord part. Until 0.17 it was recorded as an ordinary single note and came back with the chord sound.

- **Decided when it is recorded, and stored.** The engine knows the side when the key goes down (`cr_key_ex`: the
  split in force then, the pitch class in the Chords layout, the key on the Full keyboard) and says so in the gesture
  (`ext | CR_GX_BASS`, with quality and mask 0; `root` = the played pitch + transpose, without the bass register).
  `crl_write` keeps it as `CRL_X_BASS` in the event's `qx`; `crl_play_ev` hands it back to `cr_loop_event` as
  `CR_GX_BASS`, which makes the loop voice a `CR_Z_BASS` voice, as the key was. Nothing at playback is consulted:
  Single (Full, Split Oct, Split Bass), the split point, the layout (Chords / Full), Bass on / off and its Behaviour
  may all have changed, the note stays on the bass part. The other way too: a note recorded on the chord side (or
  with Split Bass off) is never moved to the bass part by turning Split Bass on later. The loop is a recording of
  what was played.
- **Where the bit lives.** In `qx`, not in `layer`: a single note's extension nibble meant nothing (`cr_chord_base`
  ignores it), so the event and the record keep their size (8 bytes in RAM, 7 in flash), and every layer operation
  carries it without knowing: Overwrite, Overdub, Replace's hide and its cut copy (`crl_cut` copies the event),
  undo's restore, quantize, Step's commit, `cr_loop_pack` / `cr_loop_unpack`, the slot's A/B copies, the pending
  save, the backup's loop objects (opaque records). `crl_write` now stores 0 in a chord-side single note's nibble
  (it used to store the extension keys held, which never sounded).
- **What still applies at playback**, as to a live bass-side key: the **bass register** (BASS layer KNOB 2; KNOB 2 on
  the view) shifts it by octaves, at once if it sounds; the bass part's sound, level and voice mode; the loop's
  Level; the BASS stream on / off (off: silent). **What does not**: the chord voicing (KNOB 1) and Transpose (the
  recorded root is absolute, as for every loop event; the octave the key was played in is in it), the performance
  mode (a bass-side note is never arpeggiated, live or looped).
- **Live over a loop.** A looped bass-side note and a live one meet on the bass part. The engine counts owners per
  note (`crx_note_start`): two different pitches are two notes on the BASS stream and the part's voice mode decides
  what is heard (the default bass is mono: the last note wins, as with two live notes); the same pitch is re-struck
  and ends when its last owner ends. Neither end leaves a note stuck (`s_bass`, `cr_loop_bass.txt`).
- **The bass that follows the chords** (Bass on with a Behaviour, Split Bass off at playback) keeps following the
  loop's chords, and the recorded bass-side notes play too: both are notes of the one bass part, the last one wins
  on a mono bass. With Split Bass on at playback nothing follows (as for live chords), so only the recorded bass
  notes and the ones played by hand use the bass part.
- **Step entry**: a bass-side key entered as a step is a bass-side step.
- **Lights and screens**: a looped bass-side note glows dim on the keys of its note name like any loop note (by
  name, so the bass register does not move it); the PLAY menu's marks, the step grid, the cursor's note name and the
  corner dial do not show the part (a bass-side step is named by its note, as a single note is).

## Timing

- Ticks: 96 per quarter note at the engine's BPM, accumulated from the ms clock with the remainder kept
  (no drift). A BPM change changes the loop's speed, never its pitch. Bar = 384 (4/4) or 288 (3/4, 6/8) ticks.
- Two clocks since 0.18 (the Rate, below): `crl.rt`, the tempo's ticks (the count-in, the click), and `crl.t`, the
  LOOP's ticks, which every time of the loop's content is in. At Rate x1 they tick together.
- **Free**: REC arms; the first chord starts the take; REC or LOOP ends it (length = elapsed, 24..65535 ticks).
- **1/2/4/8/16 bars** (KNOB 1 in the PLAY menu; SELECT until 0.17): REC starts a one-bar count-in (clicked; KNOB 4 turns it off), then
  records the sync length and commits by itself. REC/LOOP earlier: the length rounds up to whole bars.
- The take is the first cycle: playback continues seamlessly from the commit.
- What REC does over a loop is the **record mode** (below, "Record modes"); **Overdub**: REC while playing (or
  stopped with a loop: it starts) arms; the next chord opens a layer whose times are cycle-relative; REC ends it,
  LOOP ends it and stops. Empty layers are discarded.
- **Quantize** (KNOB 3: none, 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32) moves note-ons to the nearest grid point at commit;
  note-offs stay (>= 1 tick); a note-on rounded past the end goes to 0.
- **Undo** (REC held; F#4 in the PLAY menu with Keys = Loops): the layer in progress, else the last layer (the first
  stays: CLEAR); a Replace layer's undo brings back what it erased.
- **Clear**: D#4 held 1 s in the PLAY menu with its keys on loops (GLO). **Panic**: stops, keeps the
  loop; a fresh take is dropped (a take only armed or counting in: the loop kept), a partial overdub kept. **Level**
  (page 2, KNOB 1) scales the loop's velocities (0 = mute).
- **Metronome**: on / off in the PLAY menu (page 2, KNOB 2) or by GLO held + SEQ from anywhere but the sound editor
  (`metronome on`; SEQ's tap was the click until 2026-10-11: it is the loop editor's key now, below); it clicks on the
  take's / the loop's / its own grid. SEQ held = the SEQ layer (the beat's preset and the drums: a SEQ tap or PRESETS
  there is the beat on / off), KNOB 1 there or OPT + SELECT = click level. The click is a 25 ms two-pole sine burst mixed after fx.c (cr_out.c
  `cr_click_mix`), 2.5 kHz on the bar's first beat, at MASTER's level (after 0.39: the owner, "The volume of the
  metronome does not change when reducing the master volume"; until then it was mixed after the master at its own
  level only), still out of the limiter and out of the recording. Since the drums the PLAY menu's KNOB 2 is labelled **Beat** and
  GLO + SEQ is the beat on / off: the click with the SEQ layer's preset on Metronome, the drums with Drums (below).

## The drums (after 0.39; `cr_drum.c`, `cr_loop.c crl_tick1`, `cr_ui.c cu_beat_post`)

The SEQ layer's preset Drums (docs/MANUAL.md "The drums"): a Grids beat on CoralFlower's morph drum synth. The beat
on, the drums play; they follow a loop while one plays, and are never part of it:

- **Not in a loop.** No event, no layer, nothing in a slot or `crl_data_t`; their state is the session's drum block
  (docs/SETTINGS.md), saved by the global save. Undo, Clear, the record modes and the loop editor never see them.
- **The grid.** `crl_tick1`, after the click, with `crl.drums` on (the beat on with Drums: `LP_DRUMS`): the step is
  the tempo tick's place in the bar from an anchor: a take recording, its start (`rrec0`); the count-in, its start
  (`cin0`: in step with the count, and so with the take after it); a loop playing (an overdub too), its cycle's
  (`rcyc0`); none of these, the drums' own (`dr0`). A 32nd (`CRL_PPQN / 8` = 12 ticks), Grids' step, 0 .. 31 in 4/4.
  `crl.dstep` (step + 1) is taken by the audio block (`choralroot.c mix_block`: `crd_beat_step`).
- **Start, stop, join** (after 0.39's first drums build, the owner: "if there is no loop the drums don't start"):
  the beat on = the drums play. Turned on with nothing playing they start at once, free on the tempo (`dr0` = the
  tick they were turned on: a beat). Turned on while a loop plays, they join on its next beat (`drum_run` waits for
  `rel % beat == 0`), in step with its bar. A loop starting while they run takes them over: its start is step 0, bar 1
  (the anchor jumps to `rcyc0`). LOOP stopping does not stop them: `dr0` keeps the last anchor, so they run on in the
  same step; only the beat off (GLO + SEQ, PRESETS or SEQ in the layer, the PLAY menu's Beat) stops them. A cycle that
  is not whole bars (Free) restarts them at its start, as the click's grid. A take armed and waiting for its first
  chord (Free): they run free; the take's start re-anchors them (its first chord is step 0).
- **3/4 and 6/8.** The bar is 288 ticks: steps 0 .. 23, the first 24 of Grids' 32 (12 of each 16 sixteenths; the plan's
  open question 1, option a). The click's grid is unchanged.
- **The Rate** does not change them: they follow the tempo's ticks (`crl.rt`), as the click.
- **The home chip** reads `crl.bpos` (the beat the click or the drums are on + 1; 0 with the beat off).
- **The sound** goes into the mix with the parts' (`fx.c mix_block`, after the parts, before the effects): the master
  filter and the limiter see it as the parts', and each drum's FX send (the layer's Mix page) feeds the delay's and
  the reverb's buses at the same amount (none with FX off).

## The Rate (0.18)

"We should also have the ability to change the rate of the loop so I could make it go faster or slower." The PLAY
menu's KNOB 2: **x1/16, x1/12, x1/8, x1/6, x1/4, x1/2, x3/4, x1 (default), x1.5, x2, x4** (`cs.loop_rate`, the
session's `loop_rate`: manual, kept by the global save; posted as `LC_RATE` -> `cr_loop_set_rate`). The four slowest
are of 2026-10-11 (the owner: "it would be good to go slower, 1/8, 1/12, 1/16 as it can be nice to make it so each
step is a whole bar for long loops"; x1/6 is ours, the value between x1/4 and x1/8 as x1/12 is between x1/8 and
x1/16): see "The slow rates" below.

- **What it scales**: how fast the loop's own time runs against the tempo. `crl_tick1` runs once per tempo tick
  (`crl.rt`) and advances the loop's clock `crl.t` by the Rate in 48ths of a tick (`CRL_RATE_Q48` 3 4 6 8 12 24 36 48 72 96 192,
  the remainder kept in `rfrac`, 0 .. 47; quarters until the slow rates): 0, 1, 2 or 4 loop ticks a tempo tick, each with the take's end, the voices' ends
  and the events due. The loop is derived from the clock, so it cannot drift from it; an event fires on a tempo
  tick, as it always did (5.2 ms at 120 BPM). At x1 the code path is the one there was: one loop tick a tempo tick
  (`tests/cr_loop_test.c`: every scenario older than the Rate passes unchanged).
- **Locked to the clock**: a loop of whole bars starts on a beat again at x1/16, x1/12, x1/8, x1/6, x1/4, x1/2, x2 and
  x4 (4 bars: every 64, 48, 32, 24, 16, 8, 2, 1 bars); at x3/4 and x1.5 its length in beats is not whole for most lengths (4 bars: 21.33 and 10.67 beats) and
  its start falls between the beats until the rounds add up (every third one).
- **A change while it plays is taken at the loop's start** (`rate_want` -> `rate` in `crl_wrap`, the end of the
  cycle), never in the middle: the position does not jump, a sounding note keeps its length in loop time, and a loop
  whose cycles ended on a beat starts the new rate on one. (The other way, scaling the position's speed at once, is
  as glitch-free but lets the loop's start land anywhere between the beats from then on.) Stopped, or with no loop:
  at once; during a take: at its first wrap. The UI shows the rate asked for.
- **Recording at a rate other than x1**: every time of a take, an overdub or Replace is taken from the loop's clock,
  so events are written in loop time and playback at that same rate gives back what was played. Length (the next
  take's bars) and Quantize count the LOOP's bars and grid: at x2 a 4-bar take lasts 2 bars of the metronome, at x1/2
  eight. (The other reading, "Length is bars of the tempo", would make the same setting mean loops of different
  sizes; the owner did not choose: this one keeps one rule, "everything of the loop's content is in loop time".)
- **The tempo's, not the loop's**: the count-in (one bar of the tempo), the metronome (its grid restarts at the
  loop's start when that start is on one of its beats, always at x1 as before; else it keeps running), MIDI clock out
  (cr_out.c: from the tempo) and 0xFA / 0xFC.

### The slow rates (x1/6, x1/8, x1/12, x1/16)

At x1 a step is a sixteenth and 16 steps are a bar; at x1/16 a step is a whole bar of 4/4, at x1/12 three quarters of
one (a bar of 3/4), at x1/8 a half note, at x1/6 a dotted quarter. 64 steps at x1/16 are 64 bars.

- **The encoding.** The engine's index is 0 .. `CRL_NRATE` - 1 in the knob's order (x1/16 = 0, x1 = `CRL_RATE_1` = 7,
  x4 = 10); it is RAM only (`cs.loop_rate`, `LC_RATE`, the emulator dump's `loop2: rate`). What is stored is the
  session's `loop_rate`, steps from x1: **-7 x1/16, -6 x1/12, -5 x1/8, -4 x1/6**, then as before -3 x1/4, -2 x1/2,
  -1 x3/4, 0 x1, 1 x1.5, 2 x2, 3 x4. No stored value moved; the record's layout and version are the same.
- **An older firmware and a new value.** A 0.18 dev build of before reads -7 .. -4 as out of range and takes x1
  (`cr_session_import`'s clamp, -3 .. 3 there); its next manual save writes x1. The releases up to 0.17 have no
  session and never read it. The loop slots' record and the backup do not hold the Rate (it is the instrument's, not
  the loop's), so a slot or a backup moves between builds untouched.
- **Why nothing else changed.** Everything of the loop's content is in the loop's ticks (`crl.t`): event times and
  lengths, a ratchet's slices (12, 8, 6 ticks) and gates, a note's part of a step (6, 12, 18 ticks), the cycle and its
  pass count (the conditions), Quantize's grid, the take's length, the position (`cr_loop_pos`, the ring, bar.beat,
  the editor's playhead `lpos / CRL_STEP`). A slow rate only makes those ticks come less often: at x1/16 one loop
  tick every 16 tempo ticks (83 ms at 120 BPM), always on the same tempo ticks (3 divides 48). The tempo's clock
  (`crl.rt`, u32) counts 24 576 ticks for a 64-step pass at x1/16; it wraps after 259 days at 120 BPM, as before. The
  free take's clock in the PLAY menu (`m:ss`) is `pos * 30000 / (bpm * q48)`: 1.97e9 at most (`CRL_MAX_LEN`).
- **What a slow rate costs.** A take played by hand is written with the loop tick's resolution: 83 ms at x1/16 and
  120 BPM (x1/4: 21 ms, x1: 5 ms), and a key held for less than a tick is one tick long. Steps entered in Step mode
  or the editor are on the grid and exact. A synced take's Length counts the loop's bars: 1 bar at x1/16 is 16 bars of
  the metronome.
- **Not the loop's**: the count-in, the click, MIDI clock out and the arpeggiator (the engine's, on the tempo) do not
  slow down.
- Tests: `tests/cr_loop_test.c` `s_rate` (every rate: a 1-bar loop's starts in 48 bars) and `s_slow` (a pass at each
  slow rate: the steps, a ratchet x4, the 1/4 and 1/2 step lengths, the wrap; 64 steps at x1/16 over two passes with
  the position, the ring and bar.beat; a 1:2 condition; the click; the count-in, a 1-bar take and Quantize at x1/16);
  `tests/cr_settings_test.c` (-7 kept, -8 is x1); `tools/emu/scripts/cr_loop_rate.txt` (the knob's slow end, x1/8 and
  x1/16 against the clock).
- **Following the loop's time**: the corner dial (`cr_loop_ring`), the top line's bar.beat, the PLAY menu's
  timeline and playhead (`cr_loop_pos`, `cr_loop_where`); a Free take's elapsed time is shown in real seconds.

## Record modes (docs/LOOPER-MODES.md)

The record mode (`cs.loop_mode`, settings v8; ALGORITHM in the PLAY menu; posted as `crl.mode`) decides what REC does
when nothing is being captured. REC while capturing ends the capture whatever the mode (a take, a layer, step entry);
REC while armed or counting in cancels.

**GLO held + REC** (2026-10-11, the owner: "I would also add shortcut for GLO + REC which changes the record mode,
cycling through them"; `cu_rec_mode_next`, in `cu_btn_press` beside GLO + SEQ): the next mode in the picker's order,
wrapping (Step -> Overwrite; the knob stops at the ends), with the message `Rec: Overdub`. Where: everywhere GLO +
SEQ works: the views, the PLAY menu (its picker follows; GLO's tap, the keys, is not taken: the combo), every other
layer, the Options, and the loop editor, where it clears no step (REC alone does); not in the sound editor, where
GLO is SHIFT. REC is the second button of a combo there, so it is swallowed: no `LP_REC`, no undo (REC held is undo
only when REC is pressed first). It posts `LC_MODE` and nothing else, exactly as the knob (`cu_loop_mode_turn`),
and is stored where the knob's is (no format change). **During a capture** (armed, counting in, a take, a layer,
step entry) neither the knob nor the shortcut is refused and there is no message but the mode's: `cr_loop_rec`
reads `crl.mode` only when nothing is captured, so the capture under way ends as it began and the new mode counts
from the next REC (one visible effect: a take armed in Overwrite with the mode then set to Advance still records
where it is; the top line's `Rec · <mode>` follows the setting). `tools/emu/scripts/cr_glo_rec.txt`.

| mode | REC on an empty slot | REC over a loop (stopped or playing) |
| --- | --- | --- |
| **Overwrite** (default) | a fresh take (Free: armed; synced: the count-in) | stops it and arms the same way; the loop is cleared when the take **starts** (the first chord, the count-in's end: `crl.ow`, `crl_take_start`), so REC / LOOP before that cancels and the loop is intact |
| **Advance** | as Overwrite, where it is | the UI first stops the loop and switches to the next slot (`CRL_NEXT_SLOT`: 10 wraps to 1; `cu_loop_load`, the slot left saved by the slot rule, stopped so written at once), the strip's hop arrow ~800 ms and "slot 3 saved · next: slot 4"; then Overwrite there (that slot's loop goes when the take starts) |
| **Overdub** | a fresh take | arms an overdub (above) |
| **Replace** | a fresh take | arms as Overdub (`crl.rep`); while a gesture of the layer is held the older layers' events whose on-time is reached are hidden instead of played (`crl_fire`), and an older event sounding when a gesture starts is cut there (`crl_cut`: it is hidden and a copy ending there goes into the new layer; its voice ends). Hidden events keep their place (`layer` = `CRL_HID` \| the hiding layer) inside the 512-event cap; undo of that layer (`crl_restore`, by `crl.lev0`, each layer's first event) brings them back whole; the flash record stores only live events |
| **Step** | step entry (`CRL_CAP_STEP`) | the same (the loop stops first) |

**Step entry** (a step sequencer since 2026-10-12; the owner: "I would expect this system to work more like an
Elektron sequencer in which you can record up to 64 steps. So at 1x, 16 steps is a bar").

- **The grid.** A step is a **sixteenth note** of the loop's own time (`CRL_STEP`, 24 ticks; `crl.step_g`): 16 steps a
  bar of 4/4 at Rate x1 (12 in 3/4 and 6/8). The **Rate** scales the pattern with the loop, as it does any loop: x2
  makes the steps thirty-seconds of the tempo, x1/2 eighths, x1/16 whole bars (64 steps: 64 bars). **Quantize does not set the step**: it is the grid a take
  played by hand is pulled to. While the record mode is Step its cell reads `1/16`, dim, and KNOB 3 says `Step: always
  1/16` and leaves the takes' Quantize as it is.
- **Length.** A pattern has 1 .. 64 steps (`CRL_STEPS`). **Length Free: the loop is the steps entered, exactly** ("3
  chords, the loop should just be 3 steps long", the owner 2026-10-11), growing to 64; a chord after the 64th is refused
  (`64 steps: full`; `crl.step_no`), and so is a rest there. **A set Length**: N bars are N x 16 steps and the cursor
  wraps at the end; a Length longer than a pattern can be gives its first whole bars within 64 steps (8 and 16 bars: 4
  bars; 3/4 and 6/8: 5 bars, 60 steps) and says so when step entry begins (`Step: 4 bars (64 steps)`;
  `cr_loop_step_len`). Takes played by hand keep their 8 and 16 bars.
- **Entering.** The cursor starts on step 1. A chord gesture's end writes its chord, one step long, on **the step the
  gesture was pressed on**. The cursor moves on a step
  - when every key is up and nothing is latched, if a chord was written there (`cu.kheld`, `cu.mlatch`: the UI posts
    `LP_STEP KEYSUP`), and
  - **when a root is pressed after one was written there and let go**, whatever is still held or latched
    (`cr_loop_gesture`): the chord key kept down while the roots are tapped under it, or a chord latched by LOCK, gives a
    step a root. Roots held **together** share the step (a chord over a Split Bass bass note; a second root pressed
    before the first is let go).
  OCT+ = a rest (the cursor moves on), OCT- = back one step (nothing erased), in or out of the menu.
- The steps are kept after the loop's events until the commit: REC = they become the loop (the loop there before
  goes), stopped; LOOP = the same and it plays; nothing entered = the loop as it was. The chords sound as they are
  entered (the chord engine plays the keys as always).

**The two faults of the first build (0.18 dev of 2026-10-11), as the owner met them.** (1) "With quantize off it is
capped to 4 total steps": the step was the Quantize value, and with Quantize none a **quarter note**, so a bar was
four steps: a 1-bar Length wrapped on the fifth chord, an empty slot opened in the editor had four steps, and the grid
drew four cells a row. (With Length really on Free the engine did go on: six chords made six quarter-note steps.) (2)
"With quantize 16 it shows 16 steps but only records to the first step": the cursor moved on **only when every key was
up**; with the chord key held down between the roots, or a chord latched by LOCK, every chord was written on the same
step (six events on step 1, a loop one step long); in the editor the cursor does not move by itself at all. Neither
depended on Quantize beyond the step's size. `tools/emu/scripts/cr_loop_stepfaults.txt` plays both; its checks fail on
6619937.

## The PLAY menu (`cr_ui.c cu_loopmenu`, `CR_K_LOOPMENU`)

LOOP held past HOLD_MS opens it and locks it (OCT- / OCT+ / HOME / LOOP held again close it; a LOOP tap still plays /
stops; in step entry OCT+ is a rest and OCT- a step back). Mock-ups: `design/choralroot-fm1-looper-screens.png`
(generator `make_looper_mockups.py`; drawn before 0.18: its knob row is the one of 0.17).

| control | what |
| --- | --- |
| PRESETS | the **slot** (the strip: filled = holds a loop, the selected one red; **a 3 px square in the cell's top right corner = its loop has changes not saved yet**, `lm_dirty`): a switch through `cu_loop_load`, at once when stopped, playing once the knob rests 250 ms (`cu_lp.want`; the strip follows at once) and at the end of the cycle |
| ALGORITHM | the **record mode** (Overwrite · Advance · Overdub · Replace · Step, the picker band) |
| SELECT | the **page** (`cu.lpage`, the first whenever the menu opens; the owner 2026-10-11: "turn the select knob to change the page. It currently changes length") |
| page 1: KNOB 1 / 2 / 3 / 4 | **Length** (Free 1 2 4 8 16 bars; also big in the middle while idle; while the timeline shows, the message "next take: 4 bars") / **Rate** (x1/16 .. x4) / **Quantize** (echoes) / **Count-in** (gate): the cell hot 800 ms, no popup |
| page 2: KNOB 1 / 2 | **Level** (bar) / **Metronome** (On / Off, the gate glyph as Count-in: right on, left off; `cs.metro`, stored where it was: the owner 2026-10-11: "Add Metronome to the second param page of the play menu"); KNOB 3 / 4: nothing (empty cells) |
| GLO tap | **the keys**: play (default) = the roots and the chord block play as on the chord view, so the menu stays open while you play into a take; loops = the slot shortcuts (white roots D4..F5 slots 1..10, D#4 held 1 s clear, F#4 undo) and their LED map, GLO lit. No Options from this menu. The Keys cell of 0.17 (KNOB 4) is gone: "get rid of the Keys / play shortcut and map this to the GLO button like in other pages" |
| EDIT tap | **the loop editor** (below) |
| SEQ tap | **out of the menu**, to the view, however the menu was entered (the third step of SEQ's cycle: the view, the editor, the PLAY menu, the view; below). SEQ's LED is lit in the menu |
| REC tap / held | by the record mode / undo, as outside the menu |
| GLO held + REC | the next record mode, wrapping (above, "Record modes"): the picker band follows |
| LOOP held again | closes the menu. **With a knob turned while LOOP is still down the menu opens again and the knob acts in it** (2026-10-13): until then LOOP held + PRESETS with the menu already open chose a slot when turned within the hold time and changed the chord sound's preset when turned after it, the menu having closed under the hand ("sometimes in the play menu turning the preset knob ends up changing the preset"; `cu.hclosed`, `tools/emu/scripts/cr_loop_presetknob.txt`). The same for every layer's own button |

The bottom line (`foot`, as the KEY layer's label and footer): `1/2 · keys: play · GLO: loops` (the page, what the
keys do, what GLO makes them); in step entry and in the editor their own.

The middle: **idle / armed** the length big ("4 bars", "Free"; armed Free: "the first chord starts the take", after
Advance its message); **recording or playing** the timeline (`lm_*`: the bars with their beat ticks, more than 8: the
half the playhead is in; recording Free the bars as they pass, the current one at the right, at most 4, the elapsed
time under it; a lane of marks per layer, at most 4 (older layers fold into lane 0), the take's lane bright, marks of
the gestures still held where they began; Replace: the held span); **step entry** the step grid (sixteenths, a bar a
row, four rows: the whole pattern of 64 steps on one screen; a set Length: all its steps, Free: the bars reached so
far; the filled steps, the cursor blinking with the REC LED's phase, the cursor step's chord under it, "step 7" (Free),
"step 7/32" (a set Length) or "64 steps · full", the foot "OCT-: back · OCT+: rest · REC: done"). The marks are read from the ISR's loop
with the IRQ off only when its events (`crl.gen`) or the window change (`cu_lm_marks`). The top line: "Loop 3" and at
the right "Rec · Overwrite" (armed: the mode; also outside the menu), "Rec 2.4", "Dub 3.2", "Rep 3.2", "Rec · Step",
"3.2" playing, "empty" for an empty slot.

## Gestures and screens (`cr_ui.c`)

| gesture | stopped | playing |
| --- | --- | --- |
| LOOP tap | play (a take: commit; step entry: commit and play) | stop |
| LOOP held | the PLAY menu (above) | the same, the timeline in the middle, the corner dial in the top line |
| SEQ tap on the view; EDIT tap in the PLAY menu | the loop editor | the same (the loop plays on) |
| REC tap | by the record mode (above) | the same; ends a take / layer / step entry |
| REC held | undo: the layers left huge in red ("layers · undo"); none: "nothing to undo" | the same |
| OCT+ / OCT- in step entry | a rest / back one step | — |

The count-in takes the whole panel: the beats to go (4 3 2 1 from the time signature) huge in red, each springing
in, "count-in" under it, the top line `Rec`, the red ring drawing itself in over the bar. It is above popups and
layers, below PANIC.

Orchid's red ring runs round the edge only where the loop is the whole screen — the count-in and the undo screen
(calibration uses it too, as its progress). Every other loop state lives in a small **corner dial** in the top line
(`design/choralroot-fm1-loop-screens.png`, states 2-7 and 9a-9c; 2026-10-09: before, REC armed drew the ring round
every screen, layer, picker and popup until the take ended): a 16 px dial at the right end, the top line's right text
("Oct +1", "Arp", "ready", "2.3", "Dub 3.2") moved left of it. Its looks (`cr_draw.c cr_dial`, `cr_screen_t.dial_mode`):

- **playing**: the dotted track grey, the loop's progress red from 12 o'clock, its arc drawn thick for ~100 ms on each
  downbeat (not with Options > Motion Off);
- **REC armed** (no loop yet, Free: the grey track alone) and **overdub armed** (over the playing arc): the **REC dot**
  in the dial's middle, red, lit like the REC LED;
- **recording the first take**: the track red (no loop yet: the whole circle is being made), the arc the take's
  progress (`lring`: with a loop length set the whole take, N bars; Free the bar's position, sweeping once a bar), the
  REC dot blinking with the REC LED (its 250 ms phase);
- **overdubbing**: the playing dial with the REC dot blinking.

It shows on the views, the layers (KEY, PERF, FX, BASS, LOOP, SEQ, the engine picker), the SAVE naming dialog
and the knob popups; not in the sound editor (no top line) nor on the Options pages, where the REC LED and the LOOP
button's green LED carry it. `tools/emu/scripts/cr_rec_dial.txt` shoots the REC looks (`cr_rec_armed.ppm`,
`cr_rec_rec.ppm`, `cr_rec_od.ppm`).

While the loop sounds, its notes glow **dim** on the root keys where they sit in the current octave window
(`cr_snap_t.lnote`, a 128-bit note mask from the 8 loop voices, taken with the snapshot); the player's own notes stay
lit, and a chord shown on screen that is the loop's lights nothing. With Options > LEDs = Glow every idle key glows
dim anyway, so the glow is visible with LEDs = Stock (idle keys lit, the loop's dim): `tools/emu/scripts/cr_loop_glow.txt`.

With Options > MIDI Clock = Out (the default) the ISR sends 0xFA (start) when the loop starts playing (LOOP from
stopped, a take committed into playback) and 0xFC (stop) when it stops, USB MIDI CIN 0x0F; the 0xF8 pulses run as
before, restarting their phase at the start.

With MIDI Clock = In the loop follows the other way (cr_out.c `cr_in_transport`): 0xFA starts it from its start
(restarts it if playing), 0xFB plays it if stopped, 0xFC stops it; a take in progress is left alone, an empty loop
ignores them, and nothing is sent out.

## The loop editor (0.18; `cr_loop.c cr_loop_ed_*`, `cr_ui.c cu_ledit_*`)

"In the play menu, lets make it so you can press Edit and edit the loop; once in the loop edit mode, you select a
step and can change: notes by playing new notes which overwrites the notes, step length (default 1, but you can make
it longer or shorter), loop length in number of steps, and we also need a way to clear a step of notes which could
be done by pressing REC (which when in the loop editor doesn't do the normal record)." And on the first build
(2026-10-12): "adjusting a step's length is changing the steps after it, which is weird. I would expect this system to
work more like an Elektron sequencer".

**A step sequencer's view of any loop, with nothing stored for it.** The grid is the sixteenth note of Step entry
(`CRL_STEP`, 24 ticks, whatever Quantize says), for a pattern entered by steps, a take played by hand, or a step loop
of the first build (whose steps were the Quantize value: quarter-note steps show as a chord every fourth step, four
steps long). **An event belongs to the step it starts in.** Nothing is moved when the editor opens: an event that
starts off the grid keeps its exact time until that step's notes are replaced; opened, looked at and closed, the loop
is byte for byte what it was. **A step's length is its NOTES' length** (`crl_ev_t.dur`), never its place in time:
changing it moves no other step and does not change the loop's length. A long note sounds on over the steps after it,
and those steps still play: the voices are the looper's eight (`CR_MAX_LOOPV`; when all are busy the one that would
end first is taken), each chord its own voice, so a held chord and the next step's chord sound together as two chords
played on the keys would. The steps are read from the events each time (`cr_loop_ed_map`: one pass, a page of 64
steps as four 64-bit maps and a byte a step, `crl_edmap_t`: 104 bytes in the POOL, the editor's only buffer; 520
bytes before the maps), and **the notes need
nothing new in the record**: `t` and `dur` are what the record always had (a part of a step is a `dur` of 6, 12 or 18),
version 1, and 0.14 .. 0.17 read an edited loop's record as any other (`tests/cr_loop_test.c s_edit`;
`old_unpack_ok`). Only a step's ratchet, condition, chance and voicing are stored for the editor (below, and "Byte 7").

| gesture | does |
| --- | --- |
| **SEQ tap on the view** (2026-10-11) | opens it from the main screen, whichever view shows, with the same refusals (then SEQ does nothing else) and the same empty pattern as below (`cu_seq_tap`: the PLAY menu is locked open under it). **SEQ is a cycle of three**: the view -> the editor -> the PLAY menu (the editor closed, the menu as if LOOP had been held) -> the view. The owner: "The edit sequence is a bit buried, hold Play, then press edit. Most people won't find this ... from Main level you should be able to press the seq key to enter the edit sequence menu", then: "press once -> sequencer, press SEQ a second time -> Play menu, press SEQ a third time -> Exit". In KEY, PERF, FX, BASS, LOCK, the view menu, the engine picker, the Options and the save dialog SEQ tapped does nothing (it toggled the click there); in the sound editor it is the MOD group; in the SEQ key's hold menu (metronome) the click on / off |
| EDIT tap in the PLAY menu | opens it (`cr_loop_ed_open`), **at any Quantize setting** (the first build refused a loop with Quantize none: `set Quantize to edit`). Refused: a slot switch on its way (`a slot is on its way`); a capture open (`end the take first`). An empty slot: an empty pattern of the set Length (Free: a bar, 16 steps; more than 64 steps: its first whole bars within 64), which is the slot empty again if nothing is entered |
| PRESETS | the **step cursor**, a step a detent, wrapping at the loop's end (`cu_ledit_move`). **GLO held + PRESETS**: 8 steps a detent, wrapping the same way. Never the slot, never the chord sound, while the editor is open. (Until 2026-10-13 the cursor was SELECT and KNOB 1.) **While the loop is stopped the step the cursor lands on is heard** (below, "Step preview"; 2026-10-15, GitHub issue 8) |
| SELECT | the editor's **page**, clamped: 1 **Step**, 2 **Trig** (`cu.le_page`, the first whenever it opens). The page's name stands where the record mode does, with the picker's two marks; the bottom line begins `1/2` / `2/2` |
| ALGORITHM | the selected step's **voicing**, on both pages (the same value as Trig's KNOB 3; on the Step page, which has no cell for it, the message `step voicing: +2`). **GLO held + ALGORITHM** (or + Trig's KNOB 3) **snaps between Follow and 0** (2026-10-11, the owner: "below -12 is fine, but let's make it so you can hold GLO key and turn voicing to snap to Follow or 0"): from Follow a turn either way gives 0, from 0 a turn either way gives Follow; from any other value clockwise gives 0, the other way Follow; one snap a turn event, however fast the knob (`cu_ledit_voic`'s `snap`). GLO + ALGORITHM is the bass part's engine on the home pages (since 2026-10-12; the bass level until then, everywhere but here): **not inside the editor**, where the snap takes its place (the bass level: the BASS layer, or BASS held + ALGORITHM outside the editor) |
| OCT- / OCT+ | the **octave**, exactly as on the view (the popup, -2 .. +2): what is played into a step next is entered there. They closed the editor until 2026-10-13 |
| the keys | they play, as on the view (auditioned by the chord engine). A gesture's end writes its chord INTO the selected step, replacing its notes: the first key of a new push (no gesture open) clears, the keys of one push add up (`crl_ed_write`, `ed_w`). **By side** (3y9's rule): a bass-side note (Split Bass) replaces the step's bass-side notes, anything else the chord side. The new notes start on the step's tick and are **as long as the step's notes were** (one step on an empty step). **Only what the player plays is written, while the loop plays as when it is stopped** (below, "What goes into a step") |
| | **The cursor stays on the step** (it does not advance as Step entry does; an Elektron's grid recording stays on the selected trig too): an editor's step is tried, heard and tried again. PRESETS moves on |
| REC tap | clears the selected step (`cr_loop_ed_clear`): its notes and its ratchet, condition, chance and voicing. No recording in the editor |
| **page 1 "Step"** | |
| KNOB 1 | **Note len**: how long the selected step's notes sound: `1/4`, `1/2`, `3/4` of a step (6, 12, 18 ticks: `cr_loop_ed_setnfrac`), then 1 .. 64 steps (`cr_loop_ed_setnlen`), one scale, a value a detent. Shown: a part when the step's longest note is exactly 6, 12 or 18 ticks and none of its notes sounds past the step's end (`cr_loop_ed_nfrac`); otherwise the whole steps as before (the longest, a part of a step counted whole, `cr_loop_ed_nlen`: a note of 10 ticks reads `1`). A part is a gate from where the note starts; a whole length ends on the grid (a note played off the grid keeps its start). Nothing else moves. An empty step has none (`-`). Longer than the loop: it sounds across the loop's start |
| KNOB 2 | **Steps**: the loop's length, 1 .. 64 steps, a step a detent (`cr_loop_ed_len`): shorter hides the last step (its events stay in RAM beyond the end, unheard), longer brings it back, and past what there was adds an empty step; `64 steps at most`. **The hidden steps are deleted when the editor closes**; a save while it is open writes what sounds (`cr_loop_pack` skips events beyond the end). A loop played to a length off the grid: the first detent up makes its last step whole. A take longer than 64 steps can be made shorter, not longer. **GLO held + KNOB 2** snaps (`cu_ledit_snap`): the next of 8, 16, 32, 48, 64 steps in the turn's direction (3/4 and 6/8, 12 steps a bar: 12, 24, 36, 48); none further that way: it stays. One snap a turn event, however fast the knob |
| KNOB 3 | **Chance**: how likely the step plays on a pass: 100 (default), 90, 75, 60, 50, 40, 25, 10 % (`CRL_CHANCE_PCT`; clockwise = more) |
| KNOB 4 | the step's velocity (the event format's `vel`; the highest of its events shown) |
| **page 2 "Trig"** | |
| KNOB 1 | **Ratchet**: Off, x2, x3, x4 |
| KNOB 2 | **Condition**: Always; 1:2, 2:2; 1:3, 2:3, 3:3; 1:4, 2:4, 3:4, 4:4; 1st; not 1st |
| KNOB 3 | **Voicing**: Follow, -12 .. +12 (as ALGORITHM; GLO held: Follow / 0) |
| KNOB 4 | **Transpose** (2026-10-11): the selected step's notes a semitone a detent, up or down (`cr_loop_ed_transpose`; below, "Transpose") |
| LOOP tap | play / stop from the loop's start, as ever; the cursor does not follow the playback, the playhead shows it. A length changed under the playhead: the playhead where it is in the new cycle (`crl_ed_renorm`) |
| REC held | the looper's undo, as outside (below) |
| GLO tapped | nothing (the keys play; GLO held is the shift of PRESETS, Steps and the voicing above) |
| SEQ / EDIT tapped, EDIT held again | close it, back to the PLAY menu (`cr_loop_ed_close`), however it was opened: nothing to confirm, an edit is applied when it is made. A knob turned while EDIT is still down after closing it by the hold does nothing (it would be the PLAY menu's: PRESETS there is the slot) |
| SEQ held | **the SEQ layer** (the beat: Metronome / Drums), the editor closed (after 0.39, the owner: "when you hold seq from the sequencer it takes you to the drums"; until then SEQ held closed the editor and there was no hold menu from it). A key or a knob with SEQ held past nothing: the layer at once, as SEQ held; a knob inside the hold time is still the editor's (`cu_activity`, `cu_knob`; `tools/emu/scripts/cr_seq.txt`). EDIT keeps the editor / PLAY menu switch |
| **LOOP held** | the PLAY menu: the editor closes, the menu stays (the owner, 2026-10-11: "from the Sequencer, holding the play key should enter the Play Menu, not return to home"; until then it closed the menu with the editor). A knob turned with LOOP still down is the menu's (PRESETS: the slot), never the sound's. A LOOP tap is play / stop as ever, the editor stays |
| HOME | the view: the editor and the menu close (it went back to the PLAY menu until 2026-10-11; with SEQ's cycle HOME is "home" here as everywhere). Another layer, a revert: the same |

### What goes into a step, and what the screen names (2026-10-11)

The owner, with the editor on the unit: "In the sequencer if playback is going on then when I enter keys to a selected
step, it also ends up combining the playing notes into that step. In this case it should only enter the keys I'm
playing, not the notes playing from the sequence. Also the name of the chord changes during playback, I would like this
to always show chord/notes of the selected step. Then during playback in the sequencer, show the name of the playing
step chord/notes to the left of the Page Name (don't offset the page name, also don't show voicing here since that
would overlap the page name probably) in blue."

- **The loop's own notes cannot reach a step.** The editor writes what the engine's gesture hook reports
  (`cr_out_t.gesture` -> `cr_loop_gesture` -> `crl_ed_write`), and that hook is raised for keyed voices only: a loop
  voice never raises it (`crx_gesture`: `v->loop`), and MIDI in plays the parts, not the chord engine. So a chord held
  over the loop's chords and let go is in the step alone (`tests/cr_loop_test.c s_edkeys`, `cr_loop_edplay.txt`).
- **What did reach it: a latched chord.** With LATCH (Sticky) or a Perform mode's Hold a chord does not end when its
  keys go up: it sounds on over the loop until the next press. The editor wrote a chord at its END, so with a latch
  the chord just played was not in the step; it arrived a press later, in whichever step was selected by then: a step
  got the chord that was still sounding, not the keys being played, and the keys being played went into the step
  after. **Now the engine says when a latched gesture's keys are up** (`CR_G_KEYUP`, a third value of the hook's `on`,
  from `cr_key_ex`'s release) **and the editor writes the step then**, as at an unlatched key's release; the later end
  of that chord writes nothing (`open[].used` 2), and the next push replaces as ever. A take is not touched: it
  records a latched chord with the length it sounded. Nothing of the sound changes: the hook only reports.
- **Checked and left as they are**: Split Bass (a bass-side key replaces the step's bass side only, by 3y9's rule);
  LOCK with latched chord keys (a root alone is one chord in the step); the arp running (the gesture is the chord, not
  the arp's notes). **Free play style**: a chord key pressed with no root held re-sounds the last root (the style's
  own rule), and that chord is a gesture like any other: it is written, and a root pressed while it sounds adds to
  the same push. That is what was played and heard; it is not changed here.
- **The name line** (the chord under the grid, its blue voicing, `step n/m`) **is the selected step's, playing or
  stopped**: a chord by its name, a single note or a bass-side note by its note, several things in one step by the
  last one written, an empty step `-`. **A major triad reads `CM`** (2026-10-12, the owner: "for Major chords,
  nothing shows, major chords should use capital M to differentiate from single notes. So C major would read CM"):
  `C` alone is a single note C. The rule is `cr_chord_name`'s (`cr_engine.c`), the one function every screen names a
  chord with, so the name line, the playing name, step entry's line and the home views agree: `M` only for the plain
  major triad; a major triad with an extension keeps `C7` `CM7` `C6` `C9` `C6 9` `CJAZZ` `CWTF` (none reads as a note,
  and `CM7` stays the major seventh); the power chord stays `C5`; minor `Cm`. The stored event is unchanged (root,
  quality, extension bits): only the text. (Those are Orchid's names, the default of Options > Chord Names; set to
  Standard, 0.20, the same lines read the lead-sheet names, the longest of which are shorter than
  `G#madd4 JAZZ`: docs/CHORD-NAMES.md.) **While a root key is down** it names the chord being played instead (the
  player's own voice: `cr_snap.pl_*`, the last keyed voice still sounding), and goes back to the step when the keys
  are up, a latch or not. Until then it showed the engine's "chord on display" unless that was a loop voice's: with a
  key held over a playing loop it flipped between the played chord and the step's old one at every loop note, and
  with a latched chord it showed that chord between the loop's notes.
- **The playing name**: while the loop plays, **the chord or the note the loop struck last, in the role blue, at the
  left of the page's name** (`cr_screen_t.lm_now`, `cr_draw.c cr_lm_now`). The page's name ("Step" / "Trig") does not
  move; no voicing. Left-aligned at x 6 on the neighbours' baseline, 78 px wide at most: "Step", the wider name, begins
  at x 90, so 6 px stay clear. Bold, the largest of 13 / 11 / 10 px that fits: 13 px for nearly all names, 11 px for
  about one in twenty, 10 px for the few longest (`G#madd4 JAZZ`: 95 px at 13, 73 at 10): none is cut. On the Trig
  page it stands where the left neighbour ("Step") is, which is not drawn while a name shows; the two marks under the
  page's name still show the pages. While the page's name slides (160 ms after SELECT) it is not drawn.
  **What it names is what sounded** (`cr_loop_t.hit_root / hit_qx`, set in `crl_hit`): a step its condition or chance
  kept silent does not change it; of a step with several things the last one struck (the name line's rule). **It
  stays until the next step that sounds** (a chord alone in a bar would otherwise flash for a sixteenth, at Rate x4 for
  31 ms), is empty before the loop's first note and gone when the loop stops. Cost: three byte stores a hit in the
  ISR; the screen hashes `lm_now` apart and a new name redraws one strip (rows 40 .. 79), on top of the grid's rows the
  playhead redraws at every step anyway.

**Transpose** (Trig's KNOB 4; the owner: "this should move all the notes up or down, so if my chord is C maj and I
transpose it up one, now it's a C# maj"). An edit of the SELECTED step: each detent moves every note that starts in it
(chord roots, single notes, bass-side notes) a semitone, by rewriting the events' roots; nothing new is stored and
nothing else moves (the quality, the extensions, the part, the times, the lengths, the step's four properties, the
other steps). **Chromatic, whatever Key Mode says**: the loop's roots are stored absolute, and this is an edit of
stored notes, not a way of playing them. **The range is 0 .. 127 for every note of the step**: a detent that would
take one of them out is refused whole, no note moves and nothing wraps (`no higher` / `no lower`); a fast turn goes as
far as it can. The cell shows the root note of the step's name with its octave (`C#4`), and while it is hot (800 ms)
how far this turn has moved it (`+2`); an empty step `-`. It is an edit like the others: the first one makes the loop
one layer, the loop is marked changed, no undo of its own (turn it back; -1 after +1 gives the record byte for byte).
A note sounding when it is turned ends at its old pitch.

### Step preview (2026-10-15; GitHub issue 8; `cr_loop.c cr_loop_ed_preview`, `cr_out.c LP_PREVIEW`)

Apieceofbluesky (issue 8, 2026-10-13): the editor would be much improved if moving the cursor to a step played it. The
owner: "all I want is step preview switching steps when playback is stopped. So turn preset knob in the step editor,
if the step has notes, it plays." No button, no setting.

- **When the cursor lands on a step while the loop is stopped, the step is heard once, as the loop would play it**:
  its live events, both sides, each on a loop voice as `crl_play_ev` strikes them (the velocity by the Level, the
  step's voicing, its ratchet with its hits on the loop's clock, a bass-side note on the bass part, the notes' own
  length: a note four steps long sounds four steps, at the Rate in force). The step's condition and chance are not
  asked: a preview always sounds. A step without notes is silent, and ends the preview sounding; so does the next
  move. PRESETS and GLO + PRESETS alike (`cu_ledit_move`); nothing when the editor opens (the cursor is on step 1 and
  has not moved).
- **It is the engine's, as the keys are**: the UI posts `LP_PREVIEW` after the cursor's move and the audio ISR plays
  it (`cr_loop_ed_preview`), so the LEDs glow the previewed notes and the Chord view names the chord exactly as for a
  chord the loop plays. Its voices carry a mark (`crl.pre`, a bit a loop voice): **not a hit of the loop**: not
  counted in `played`, not the editor's "now playing" name (which shows while the loop plays only), and the loop's
  own voice stealing treats a preview's voice as any other. **Nothing is written** (no event, no gesture: the
  editor's "only what you play goes in" holds), **REC is not armed, the position does not move** (the loop is
  stopped: `cr_loop_pos` 0 before and after), the slot is not dirtied.
- **It ends** with the next cursor move, when a key is played by hand (the engine's gesture hook flags it,
  `pre_end`, and the next `cr_loop_tick` ends the voices: never inside the hook; the chord played replaces the step as
  before), when REC clears the step, when the editor closes, when LOOP starts the loop (its voices are the loop's
  from the start), with a stop or a panic. **While the loop plays nothing is struck**: the cursor moves, the loop is
  heard (`cr_loop_ed_preview` returns 0 while `CRL_PLAYING`).
- Tests: `tests/cr_loop_test.c s_preview` (the notes of a chord, a bass-side note on the bass stream, the length,
  the ratchet's two hits, a condition and a chance ignored, the step's voicing, a key by hand, REC, play, close);
  `tools/emu/scripts/cr_loop_preview.txt` (the dump's `loop3: .. pre N`: the preview's voices). If a way to turn it
  off is wanted later it is an Options line, not now.

### Ratchet, condition, chance, voicing (2026-10-13; `crl_data_t.st`, `cr_loop.c crl_play_ev`)

The owner: "maybe do a ratchet on a step we can enable. Conditional steps would also be good ... Algo knob to change the
voicing per step"; and Chance, which took the place of the random conditions. **They belong to the STEP**: to every
event that starts in it, the chord side and the bass side alike. An empty step has none (the cells show `-`, nothing
can be set); clearing a step clears them; replacing a step's notes keeps them.

- **Ratchet** (x2, x3, x4): the step's notes strike that many times, evenly inside ONE step (12, 8, 6 ticks apart).
  Each hit is as long as the slice; if Note len is a part of a step, that part of the slice (x2 on a 1/2-step note:
  two hits of 6 ticks, 12 apart; never under a tick). **A note longer than a step: the last hit holds to the note's
  end** (2026-10-11; the owner: "if length is greater than one step, the last trig of ratchet then holds for the
  length. So if ratchet is 2 and step length is 2, the sequence would be note of length 0.5, then after that a note of
  1.5; if the length was 4 and the ratchet was 4, you get 3 notes that are 0.25 length, then the 4th note is a length
  of 3.25 steps"): the hits before the last are a slice long, the last is Note len less the slices before it (x2 on 2
  steps: 12 then 36 ticks; x4 on 4: 6, 6, 6 then 78; x3 on 2: 8, 8 then 32). Until then a ratchet replaced the note's
  length: a long note ratcheted inside its step and was silent after it. Note len of a step or less: as it was. The
  held last hit sounds over the following steps like any long note (they still play, on their own voices), across
  the loop's end too, and the grid already shows it: the white bar and the dark cells follow Note len, a ratchet or
  not.
- **Condition**: `A:B` plays on pass A of every B passes of the loop, **counted from when the loop starts playing**
  (LOOP from stopped, a take committed into playback, a slot taken at the end of a cycle: pass 1 again); `1st` only on
  the first pass, `not 1st` on every other.
- **Chance**: the step plays on a pass when its condition is met AND its roll passes. **One roll per step per pass**:
  the chord, the bass-side note and every hit of a ratchet come or stay away together.
- **Voicing**: `Follow` (default) = the live voicing, as every loop plays; a fixed rotation -12 .. +12 (the range of
  KNOB 1 on the view: `cr_engine.c CR_VOICE_MIN / MAX`) is used for that step's chords instead, and a live voicing
  change while they sound leaves them alone (`cr_loop_event_v`, `CR_LOOP_FIXV`). Single notes and bass-side notes are
  not voiced, so it does nothing to them.

**Storage: a table, two bytes a step, for the loop's first 64 steps** (`crl_data_t.st[64][2]`: ratchet 2 bits,
condition 4; voicing 5, chance 3). A table rather than bits in the events because an event has 3 spare bits and these
need 14, and widening `crl_ev_t` would cost 512 x 2 bytes in each of the two copies of a loop. What follows from it:

- **A take longer than 64 steps** has them on its first page only: on a later page the cells show `-`.
- **A take played by hand**: the properties are its steps', i.e. of the events that start in them (`t / 24`); opening
  and closing the editor moves no byte of the loop (the table included).
- **What keeps the table right**: a fresh take, a pattern committed by step entry and a clear zero it (a new loop);
  the editor's REC, its close (the steps Steps hid) and the looper's undo drop the entries of steps no event starts
  in any more (`crl_st_prune`; an event a Replace layer hid counts, so undoing that layer brings step and properties
  back); an overdub's or a Replace layer's events take the properties of the steps they land in; Quantize moves
  events, never entries; a slot's loop comes with its own table (`cr_loop_set` / `cr_loop_queue`: the whole
  `crl_data_t`). The first property set is an edit like any other: the loop becomes one layer.

**In the audio ISR** (estimated from the source, not yet counted in the device's disassembly): per event fired, one
divide by the constant 24 (the step), two table bytes and two tests, about 15 instructions when the step has nothing;
a condition adds a table word and a shift (no division: the pass is kept mod 12, which 2, 3 and 4 divide); a chance
adds two multiplies and a compare; a ratchet about 20 once (a subtract and a multiply more since the last hit holds:
its length is worked out once, when the event fires, and kept in the voice: `pv[].last`), then each further hit is one
`cr_loop_event_v`, the cost of any chord's note-on. Every hit, a ratchet's or a plain note's: one test and a load
more for its length (`hits ? gate : last`), and three byte stores for the editor's playing name. Per tick, the voices'
loop (8) has one more compare for a voice in use. **A ratchet stays on
its one loop voice** (`pv[].used` 2 = waiting for the next hit, the chord kept in `pv[]` so a hit never reads an event
that an edit or a slot switch took away): it can never hold more voices than a plain note, and a stop, a clear of the
step, a Replace cut or a stolen voice ends its hits.

**The rolls** are xorshift32 (the engine's `crx_rand` algorithm, the looper's own state `crl.rnd` so the arp's random
order is not disturbed): a new word each pass, stirred with the loop's clock at every start, hashed with the step
number for each step's roll. `cr_loop_seed` sets it: the same seed and timing give the same passes (the host tests).

**The screen** (`CR_K_LOOPMENU` with `lm_mid = CR_LM_STEPS`, the step grid of Step entry: `cr_draw.c cr_lm_steps`): a
bar a row, **four rows of 16: all 64 steps at a glance** (3/4 and 6/8: four rows of 12). A step with a note starting
in it is filled, coloured by its velocity (below); a step a longer note still sounds in (half of it or more) is dark red (`lm_lane[1]`); an empty one grey;
the grid ends where the loop ends (`lm_steps`). The cursor is the red outline; in the editor its blink is bright /
dim (`lm_ed`), never gone. What a cell carries is under "The grid's cells" below. While the loop plays, the step
being played is white (grey when empty: `CR_LM_PLAY` in `lm_pos`; only its rows' strips are redrawn), and what the loop
struck last is named in blue at the left of the page's name (above, "The playing name"). Under the grid the selected
step's chord, big, with its voicing after it in blue when that is not Follow (`Dm7 +3`), and `step 5/16`; the cells
Note len / Steps / Chance / Velocity (page 1) or Ratchet / Condition / Voicing / Transpose (page 2); the bottom line
`1/2 · PRESETS: step · REC: clear · SEQ: back` (`EDIT: done` until 2026-10-11; both keys go back to the PLAY menu,
the line names the one that opens the editor from the view: `EDIT/SEQ` does not fit `foot`'s 47 characters). **A take
longer than 64 steps is paged**: the cursor walks all its steps and the grid shows the 64 the cursor is in (`step 70/128 · p2`); one page was chosen over a scrolling row
because a pattern fits it whole, and a page turns only for loops no step sequencer made.

**Layers and undo.** The editor works on what sounds (the live events). Its **first edit makes the loop one layer**
(`crl_ed_flat`: the overdubs merged, the events a Replace layer had hidden removed for good), so after an edit REC
held finds `nothing to undo`. Writing "into the current layer like an overdub" was tried in the design and dropped: an
undo would then take the notes the editor wrote and the overdub they happened to share a layer with, bring back
nothing the editor deleted from the layers under it, and a step's length cannot be undone by layer at all. There is
**no editor-level undo**; the way back is the revert (GLO + SAVE: the loop as last saved). Opened and closed without
an edit, the loop and its layers are untouched.

**Saving**: edits are in RAM and set `crl.dirty`, like a take: the global save and leaving the slot write them.

**The grid's cells (2026-10-13).** The owner, with the editor on the unit: "there is no white bar on the selected step, so
when the step length is 2, you just see a white bar on the next step which is confusing and makes you think the next
step is selected. So we should always put a white bar also on the current selected step, and that bar should also
become smaller when the step length is below 1 ... The white bars should only render to show length of selected step.
The color of the steps is good, but I think the color should morph colors based on velocity. Also there's a black box
that shows when chance is less than 100, I think we should use maybe a yellow color that fades in brightness to black
(0% chance), and then also extend this box to cover the full vertical drawing of the step. If a condition is enabled,
render the yellow chance bar now as blue." And: "When selecting a step we should show the voicing number in blue after
the name of the chord." A cell is 10.25 x 7 px (14.5 in 3/4 and 6/8); on it, drawn in this order (`cr_draw.c
cr_lm_steps`):

| element | what | data |
| --- | --- | --- |
| the fill | **the velocity** (the loudest of the step's events) in 15 levels, one every 9 (`CRL_EC_VEL`): level 12 (96 .. 104, the default 100) is the role red (224, 58, 47), as every step was; below it a straight mix down to a dark plum (96, 40, 128) at 1 .. 5; above it up to a light peach (255, 176, 120) at 123 .. 127. The brightness rises with every level; the plum's hue keeps a soft step apart from the dark red of a sounding-on step | `lm_cell` bits 0-3 (0: no velocity given: the role red, as Step entry's grid) |
| the strip | the cell's **3 right-hand px over its full 7 px**: **Chance** under 100 %: yellow (242, 183, 5) mixed towards the background by the chance (90 % .. 10 %: seven levels down to nearly black); a **Condition** (not Always): blue (43, 80, 200), full at 100 %, dimmed by the chance in the same way. Chance 100 % and Always: no strip | `lm_cell` bits 4-6 (the chance's index), bit 7 (`CR_LMC_COND`) |
| the ticks | **Ratchet**: x2, x3, x4 = 2, 3, 4 ticks of 1 x 2 px in the background's colour, 2 px apart along the cell's top from its second px. The fourth sits at the strip's first column when there is a strip (the strip keeps 19 of its 21 px) | `lm_lane[3]` / `[4]` (the ratchet's two bits) |
| the bar | **the selected step's Note len, and no other step's**: white, 2 px along the cell's foot, from the selected step's own cell (a quarter, half, three quarters of its width for a part of a step, to a whole px: 3, 5, 8 of 10 px) over the cells its note sounds on into, into the next row and across the loop's end. An empty selected step: none | `lm_bar` (quarters, 4 = the cell), `lm_lane[2]` (the further cells) |
| the cursor | the 2 px outline in a 1 px background ring, role red; the other blink phase the same at half brightness | `lm_pos`, `CR_LM_CUR_OFF`, `lm_ed` |
| the playhead | the cell white (grey when empty); strip, ticks and bar stay on it | `CR_LM_PLAY` |

A step past the 64th of a long take has no properties: its colour is its velocity's, and it has no strip or ticks.
The colours are mixed once per palette (`cr_lmc`: 32 colours, 72 bytes of RAM), never per cell or per frame. The
voicing after the chord: `sub` carries it after a tab (`"Dm7\t+3"`), drawn in the role blue; shown only for a fixed
voicing on a step that has a chord (a single note or a bass-side note is not voiced; Follow is the default and adds
nothing). The name gives way to it; with the longest name (`G#madd4 JAZZ -12` beside `step 64/64`) the two gaps
shrink from 5 and 8 px to 3 and 3 and nothing is cut.

**Cost**: 104 bytes of POOL (`cu_le_map`: 32 before the cells' byte a step; 24 before the notch's map); in RAM the steps' table, 128 bytes in each of the
two `crl_data_t` (the loop and the staged slot), 64 for the voices' ratchet state and a few bytes of state: 352 in all
(RAM 80168 -> 80520 B, POOL 326600 -> 326608 B, flash +3520 B on 2026-10-13); the grid's cells: POOL 314600 -> 314744 B (the map +72, the screen `cu_scr` +72: `lm_cell`, a fifth lane, `lm_bar`, `lm_ed`), RAM 80532 -> 80612 B (the colours), flash +992 B; the playing name, Transpose, the voicing's snap and the held last hit (2026-10-11): POOL 314744 -> 314764 B (`lm_now` 16, the draw cache's hash 4), RAM unchanged (the new bytes are in padding), flash +784 B; no copy of the loop. A frame, on the emulator (estimates, `cr_loop_trig.txt` / `cr_loop_step.txt`, host instructions a UI frame): the average +0.3 % and +0.6 %, the worst frame +0.1 % and +1.6 %. The UI rebuilds its view only
when the events change (`crl.gen`) or the cursor moves, with the IRQ off for the two passes; the operations run with
the IRQ off from the UI thread, as `cu_rec`'s stop does.

## Slots and the record

Ten slots, and a slot keeps its loop: only the selected one lives in RAM. **Loops are saved by hand** (the owner,
2026-10-11: "Only save the loops after we hold save to manual save, thus if I have some loops recorded that I like
and accidentally record over them, I have a way to recover, and also it should make it so you can record / play a
loop with no glitches", and "I think we just keep one loop in memory; if you switch loop slots, then auto save the
loop if it's dirty / not saved"). The loop in RAM is written to its slot

- **(a) by the global save** (SAVE held: `cu_gsave_now` -> `cu_loop_write`, with the settings and the session; while
  it plays or a capture is open the save waits for the stop: "saves at stop"), and
- **(b) when its slot is left** with unsaved changes (`cu_loop_keep`, below),

and nowhere else: **not at a stop, not after a take, not when idle** (`cu_loop_autosave`, 2026-10-09 .. 2026-10-11, is
gone). A take, an overdub, a clear, an undo, an edit change the loop in RAM (`crl.dirty`); a power cycle or a revert
(GLO + SAVE: `cu_revert` reloads the saved slot from flash) brings the slot's saved loop back. The PLAY menu marks the
slot whose loop is not saved. **No flash write happens while recording or playing**, with the one exception that
existed before and is kept as it was: a second slot switch while the loop still plays (below), a one-off hiccup.

The PLAY menu's PRESETS (or, with its keys on loops, a white root D4..F5) selects a slot (load when stopped, at
the end of the cycle when playing).
If the loop in RAM changed since its last save or load (a take, an overdub, an undo, a clear, an edit: `crl.dirty`;
an overdub in progress ends at the switch, its layer kept), it goes to its slot before the other slot is loaded:

- stopped: written at once, the message folds both: "loop 2 · saved 1" (green);
- playing: no flash erase while a loop plays (Felucca's rule), so it is packed into a buffer of its own (`cr_ui.c
  cu_loop_pend`) as a pending save for its slot, the switch happens at the end of the cycle as before ("loop 2: next
  cycle"), and the pending save is written when the loop next stops ("saved to slot 1"), before any other write to
  the loop slots, and before a second switch while it still plays: then at once, a one-off audio hiccup rather than
  a lost take (traced "loop: pending save flushed while playing"); the slot about to be loaded may be that one;
- a cleared loop deletes its slot's record (the slot comes back empty: "loop 2 · cleared 1");
- a flash error on that save refuses the switch ("save error"): the loop stays in RAM, its slot selected.

Before the end of the cycle, a stop, a clear, a panic or an edit of the loop still playing (an overdub armed, an
undo) cancels a queued switch: the slot it was leaving stays selected, its loop still in RAM
(`cr_loop_t.loads` tells a switch taken from one cancelled), and the copy staged for the switch is still written at
the stop. LOOP held + the root of the slot it is on (keys on loops) keeps its
loop as it is. No save in SAFE MODE, after a backup restore (the flash is newer than the RAM) or without the flash:
the loop left is dropped.

The SAVE-held map of 0.17 (Save / Load / Delete on the root-chosen slot) is removed with its layer, LEDs and screen:
a copy into another slot is no longer offered; a slot is emptied by clearing its loop and saving (or leaving it); the
slot in RAM is reverted by GLO + SAVE. The selected slot is a manual setting (record v2) and comes back at power-on (a
load only). Tests: `tools/emu/test_persist.sh` (`loop_persist_stop`: saved, recorded over, not saved, a power cycle:
the old loop; `_save`: SAVE held: the new one; `_switch`: the slot left and come back to: kept; `_revert`: GLO + SAVE
after recording over a saved loop: the saved one; `cr_loop_slots.txt`: the stopped overdub is NOT written at its stop
and is when its slot is left), `test_cr.sh` (`cr_save_hold.txt`: SAVE held saves, no slot map; `cr_loop_sync.txt`:
SAVE held while playing waits for the stop).

Flash: storage.c's commit record and A/B copies (its hooks, header and CRC; `storage.c` unedited), on user
sample slot 3's 80 KiB (0xC8000..0xDBFFF; ChoralRoot has no SAMPLE engine (`FELUCCA_SAMPLE 0`) and its backup no
sample objects, so nothing else writes it; a Felucca build's `eng_sample.c` ignores non-sample data): slot k copy c at `0xC8000 + (2k + c) * 4 KiB`, header type `0x4C30 + k`. Payload (little-endian):

```
0  u32 "CRL1"   4 u8 version 1   5 u8 sig   6 u8 nlayers   7 u8 flags (bit 0 CRL_RF_PART, bit 1 CRL_RF_STEP; 0 until 0.17)
8  u32 len (ticks)   12 u16 nev   14 u16 ppqn (96)
16 nlayers x u16: events per layer, then nev x 7 bytes: t u16, dur u16, root, vel, qx
then, with CRL_RF_STEP: u8 n (1 .. 64), n x 3 bytes: step (0 .. 63, ascending), st[0], st[1]
```

Unpack checks everything (magic, version, ppqn, counts, each field's range) and refuses a bad record as empty: a
newer version or garbage never loads half a loop.

**Byte 7, the flags (0.18).** 0.14 to 0.17 wrote 0 there and never read it. Bit 0, `CRL_RF_PART`: a single note's
`qx` low nibble is its part (`CRL_X_BASS`). `cr_loop_pack` sets it only when the loop holds a bass-side note, so a
loop without one is written byte for byte as 0.17 wrote it. The version stays 1 and the record's size is unchanged.

- **An older record in 0.18** (flags 0): loads and plays as before. A single note's nibble there is not a part (it
  held the extension keys that were down, which never sounded): unpack clears it, so no old loop grows a bass note.
- **A 0.18 record in 0.14 to 0.17**: accepted whole (its checks do not cover byte 7 or a single note's nibble;
  `tests/cr_loop_test.c old_unpack_ok` is those checks). The bass-side notes play as single notes of the chord part,
  which is what 0.17 itself recorded. If that firmware then writes the loop again (a stop after a change, a switch),
  it writes flags 0 and the bass side is lost for good; a loop only played or left alone keeps its record.
**Bit 1, `CRL_RF_STEP` (0.18, 2026-10-13): the steps' properties follow the events**: a count, then `{step, st[0],
st[1]}` for each step that has a property AND a stored event, ascending. `st[0]` = ratchet (bits 0-1) | condition << 2
(0 .. 11); `st[1]` = voicing (bits 0-4: 0 Follow, 1 .. 25 = -12 .. +12) | chance << 5 (0 .. 7 = 100 90 75 60 50 40 25
10 %).

- **A loop that uses none** is written without the flag and without the bytes: byte for byte the record of before
  (`tests/cr_loop_test.c s_trig`: compared with a copy of the old packer).
- **`CRL_REC_MAX` is what it was (3664)**: the entries go in the room the events leave, so the pending-save buffer in
  the POOL did not grow and no record is longer than an older firmware's slot reader accepts. All 64 entries fit up
  to 494 events (one layer); at the 512-event cap 20 do. The editor refuses a step's first property when its entry
  would not fit (`loop too full`, `CRL_ED_NOROOM`); the one way past that check is to overdub more events onto a loop
  that already has entries until fewer fit: then the save keeps the lowest steps' entries and drops the rest.
- **Unpack checks the table as it checks everything**: no entry, a step past 63 or out of order, an unknown condition
  or voicing, a record cut short: the record is refused as empty.
- **An older record in this build** (0.14 .. 0.17, main before this): loads unchanged, the table all zero.
- **This record in 0.14 .. 0.17 and in main before this**: accepted whole, as an unknown flag is today: their unpack
  reads neither byte 7's other bits nor anything after the events (`old_unpack_ok` is those checks, run on such a
  record), and the record is never longer than their `CRL_REC_MAX`. The loop plays without ratchets, conditions,
  chances and step voicings (a part-of-a-step note plays right: it is only a `dur`). If that firmware writes the loop
  again, the properties are gone from the record.

- **Backup, restore, the device manager, `fm1_install.py`**: a slot is an opaque object (40..49); the firmware
  validates a restored one with `cr_loop_unpack`, the manager's header line (`loopHeader`: bars, time signature,
  layers, events) reads neither byte 7 nor the events. A backup taken on 0.18 restores on 0.17 and the other way.

## Caps

512 events per loop (all layers, a Replace layer's hidden events and its cut copies included; 4 KiB in RAM + a 4 KiB staging copy for slot switches; 3.6 KiB record, and
3664 bytes in the POOL for the loop being left, packed; 32 bytes there for the editor's map: a page of 64 steps),
32 layers, 16 open gestures, 8 loop voices, 65535 ticks; a step-entered pattern 64 steps. Reaching the event cap ends the take ("loop full").
