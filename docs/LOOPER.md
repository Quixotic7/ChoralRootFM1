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
| `qx` | u8 | quality (CR_Q_*) << 4 \| extension mask (CR_EXT_*) — the *final* chord (Advanced / Free) |
| `layer` | u8 | RAM only (in flash layers are counts): the layer, or `CRL_HID` \| the Replace layer that hid it |

Capture comes from the engine's `cr_out_t.gesture` hook (keyed voices and pads; a same-root retrigger ends one
gesture and starts another). Playback calls `cr_loop_event(c, lv, root, q, ext, vel, on)`: a resolved chord on one
of `CR_MAX_LOOPV` (8) loop voices, bypassing the chord-key state machine; voicing, performance, bass apply live.

## Timing

- Ticks: 96 per quarter note at the engine's BPM, accumulated from the ms clock with the remainder kept
  (no drift). A BPM change changes the loop's speed, never its pitch. Bar = 384 (4/4) or 288 (3/4, 6/8) ticks.
- **Free**: REC arms; the first chord starts the take; REC or LOOP ends it (length = elapsed, 24..65535 ticks).
- **1/2/4/8/16 bars** (SELECT in the PLAY menu): REC starts a one-bar count-in (clicked; KNOB 2 turns it off), then
  records the sync length and commits by itself. REC/LOOP earlier: the length rounds up to whole bars.
- The take is the first cycle: playback continues seamlessly from the commit.
- What REC does over a loop is the **record mode** (below, "Record modes"); **Overdub**: REC while playing (or
  stopped with a loop: it starts) arms; the next chord opens a layer whose times are cycle-relative; REC ends it,
  LOOP ends it and stops. Empty layers are discarded.
- **Quantize** (KNOB 1: none, 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32) moves note-ons to the nearest grid point at commit;
  note-offs stay (>= 1 tick); a note-on rounded past the end goes to 0.
- **Undo** (REC held; F#4 in the PLAY menu with Keys = Loops): the layer in progress, else the last layer (the first
  stays: CLEAR); a Replace layer's undo brings back what it erased.
- **Clear**: D#4 held 1 s in the PLAY menu with Keys = Loops (or the SAVE layer's Delete). **Panic**: stops, keeps the
  loop; a fresh take is dropped (a take only armed or counting in: the loop kept), a partial overdub kept. **Level**
  (KNOB 3) scales the loop's velocities (0 = mute).
- **Metronome**: METRO tap = click on/off (on the take's / the loop's / its own grid), held = 4/4 3/4 6/8 picker,
  KNOB 1 there or OPT + SELECT = click level. The click is a 25 ms two-pole sine burst mixed after fx.c (cr_out.c
  `cr_click_mix`), 2.5 kHz on the bar's first beat.

## Record modes (docs/LOOPER-MODES.md)

The record mode (`cs.loop_mode`, settings v8; ALGORITHM in the PLAY menu; posted as `crl.mode`) decides what REC does
when nothing is being captured. REC while capturing ends the capture whatever the mode (a take, a layer, step entry);
REC while armed or counting in cancels.

| mode | REC on an empty slot | REC over a loop (stopped or playing) |
| --- | --- | --- |
| **Overwrite** (default) | a fresh take (Free: armed; synced: the count-in) | stops it and arms the same way; the loop is cleared when the take **starts** (the first chord, the count-in's end: `crl.ow`, `crl_take_start`), so REC / LOOP before that cancels and the loop is intact |
| **Advance** | as Overwrite, where it is | the UI first stops the loop and switches to the next slot (`CRL_NEXT_SLOT`: 10 wraps to 1; `cu_loop_load`, the slot left saved by the slot rule, stopped so written at once), the strip's hop arrow ~800 ms and "slot 3 saved · next: slot 4"; then Overwrite there (that slot's loop goes when the take starts) |
| **Overdub** | a fresh take | arms an overdub (above) |
| **Replace** | a fresh take | arms as Overdub (`crl.rep`); while a gesture of the layer is held the older layers' events whose on-time is reached are hidden instead of played (`crl_fire`), and an older event sounding when a gesture starts is cut there (`crl_cut`: it is hidden and a copy ending there goes into the new layer; its voice ends). Hidden events keep their place (`layer` = `CRL_HID` \| the hiding layer) inside the 512-event cap; undo of that layer (`crl_restore`, by `crl.lev0`, each layer's first event) brings them back whole; the flash record stores only live events |
| **Step** | step entry (`CRL_CAP_STEP`) | the same (the loop stops first) |

**Step entry**: the grid is Quantize (none: 1/16; `crl.step_g` ticks a step); a cursor on step 1. A chord gesture's end
writes its chord at the cursor, one step long (several gestures before every key is up: all at the cursor); when every
key is up and nothing is latched (`cu.kheld`, `cu.mlatch`; LOCK keeps the chord block latched) the UI posts
`LP_STEP KEYSUP` and the cursor advances if a chord was written there; OCT+ = a rest (the cursor advances), OCT- = back
one step (nothing erased), in or out of the menu. Synced lengths wrap the cursor at the length; Free grows. The steps
are kept after the loop's events until the commit: REC = they become the loop (the loop there before goes; Free: the
length rounds up to the bar of the last step used), stopped; LOOP = the same and it plays; nothing entered = the loop
as it was. The chords sound as they are entered (the chord engine plays the keys as always).

## The PLAY menu (`cr_ui.c cu_loopmenu`, `CR_K_LOOPMENU`)

LOOP held past HOLD_MS opens it and locks it (OCT- / HOME close it; a LOOP tap still plays / stops; OCT+ does nothing
there, outside step entry). Mock-ups: `design/choralroot-fm1-looper-screens.png` (generator `make_looper_mockups.py`).

| control | what |
| --- | --- |
| PRESETS | the **slot** (the strip: filled = holds a loop, the selected one red): a switch through `cu_loop_load`, at once when stopped, playing once the knob rests 250 ms (`cu_lp.want`; the strip follows at once) and at the end of the cycle |
| ALGORITHM | the **record mode** (Overwrite · Advance · Overdub · Replace · Step, the picker band; saved) |
| SELECT | the **length** (Free 1 2 4 8 16 bars): the middle while idle; while the timeline shows, "next take: 4 bars" |
| KNOB 1 / 2 / 3 | Quantize (echoes) / Count-in (gate) / Level (bar): the cell hot 800 ms, no popup |
| KNOB 4, GLO tap | **Keys**: `Play` (default) = the roots and the chord block play as on the chord view, so the menu stays open while you play into a take; `Loops` = the slot shortcuts (white roots D4..F5 slots 1..10, D#4 held 1 s clear, F#4 undo) and their LED map, GLO lit (saved, settings v8) |
| REC tap / held | by the record mode / undo, as outside the menu |

The middle: **idle / armed** the length big ("4 bars", "Free"; armed Free: "the first chord starts the take", after
Advance its message); **recording or playing** the timeline (`lm_*`: the bars with their beat ticks, more than 8: the
half the playhead is in; recording Free the bars as they pass, the current one at the right, at most 4, the elapsed
time under it; a lane of marks per layer, at most 4 (older layers fold into lane 0), the take's lane bright, marks of
the gestures still held where they began; Replace: the held span); **step entry** the step grid (16 a bar at 1/16, at
most 4 bars a page, 64 steps; the filled steps, the cursor blinking with the REC LED's phase, the cursor step's chord
under it, "step 7 · bar 1", the foot "OCT-: back · OCT+: rest · REC: done"). The marks are read from the ISR's loop
with the IRQ off only when its events (`crl.gen`) or the window change (`cu_lm_marks`). The top line: "Loop 3" and at
the right "Rec · Overwrite" (armed: the mode; also outside the menu), "Rec 2.4", "Dub 3.2", "Rep 3.2", "Rec · Step",
"3.2" playing, "empty" for an empty slot.

## Gestures and screens (`cr_ui.c`)

| gesture | stopped | playing |
| --- | --- | --- |
| LOOP tap | play (a take: commit; step entry: commit and play) | stop |
| LOOP held | the PLAY menu (above) | the same, the timeline in the middle, the corner dial in the top line |
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

It shows on the views, the layers (KEY, PERF, FX, BASS, LOOP, SAVE, METRO, the engine picker), the SAVE naming dialog
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

## Slots and the record

Ten slots, and a slot keeps its loop: only the selected one lives in RAM, and the loop being left is saved to its own
slot first. The PLAY menu's PRESETS (or, with Keys = Loops, a white root D4..F5) selects a slot (load when stopped, at
the end of the cycle when playing).
If the loop in RAM changed since its last save or load (a take, an overdub, an undo, a clear: `crl.dirty`; an overdub
in progress ends at the switch, its layer kept), it goes to its slot before the other slot is loaded:

- stopped: written at once, the message folds both: "loop 2 · saved 1" (green);
- playing: no flash erase while a loop plays (Felucca's rule), so it is packed into a buffer of its own (`cr_ui.c
  cu_loop_pend`) as a pending save for its slot, the switch happens at the end of the cycle as before ("loop 2: next
  cycle"), and the pending save is written when the loop next stops ("saved to slot 1"), before any other write to
  the loop slots, and before a second switch while it still plays: then at once, a one-off audio hiccup rather than
  a lost take (traced "loop: pending save flushed while playing"); the slot about to be loaded may be that one;
- a cleared loop deletes its slot's record (the slot comes back empty: "loop 2 · cleared 1");
- a flash error on that save refuses the switch ("save error"): the loop stays in RAM, its slot selected.

Before the end of the cycle, a stop, a clear, a panic or an edit of the loop still playing (an overdub armed, an
undo) cancels a queued switch: the slot it was leaving stays selected, its loop still in RAM (`cr_loop_t.loads`
tells a switch taken from one cancelled), and a pending save is still written at the stop. LOOP held + the root of the slot it is on (Keys = Loops) keeps its
loop as it is. Nothing is saved at power-off: changes to a slot never left are lost, as before. No save in SAFE
MODE, after a backup restore (the flash is newer than the RAM) or without the flash: the loop left is dropped.

SAVE held: Save / Load / Delete on the root-chosen slot, OCT+ does it. Save is rarely needed now (a copy into
another slot, which becomes the selected one; while a loop plays it waits for the stop, and a switch before then
saves the loop there). Load of the slot in RAM reverts it to its saved loop (nothing asked: it is explicit); Load of
another slot is a switch. Delete of the slot in RAM leaves its loop in RAM, unsaved (leaving drops it). The selected
slot is a setting (record v2) and comes back at power-on (a load only).

Flash: storage.c's commit record and A/B copies (its hooks, header and CRC; `storage.c` unedited), on user
sample slot 3's 80 KiB (0xC8000..0xDBFFF; ChoralRoot has no SAMPLE engine (`FELUCCA_SAMPLE 0`) and its backup no
sample objects, so nothing else writes it; a Felucca build's `eng_sample.c` ignores non-sample data): slot k copy c at `0xC8000 + (2k + c) * 4 KiB`, header type `0x4C30 + k`. Payload (little-endian):

```
0  u32 "CRL1"   4 u8 version 1   5 u8 sig   6 u8 nlayers   7 u8 0
8  u32 len (ticks)   12 u16 nev   14 u16 ppqn (96)
16 nlayers x u16: events per layer, then nev x 7 bytes: t u16, dur u16, root, vel, qx
```

Unpack checks everything (magic, version, ppqn, counts, each field's range) and refuses a bad record as empty: a
newer version or garbage never loads half a loop.

## Caps

512 events per loop (all layers, a Replace layer's hidden events and its cut copies included; 4 KiB in RAM + a 4 KiB staging copy for slot switches; 3.6 KiB record, and
3664 bytes in the POOL for the loop being left, packed),
32 layers, 16 open gestures, 8 loop voices, 65535 ticks. Reaching the event cap ends the take ("loop full").
