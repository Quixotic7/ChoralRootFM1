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
| `layer` | u8 | RAM only; in flash layers are counts |

Capture comes from the engine's `cr_out_t.gesture` hook (keyed voices and pads; a same-root retrigger ends one
gesture and starts another). Playback calls `cr_loop_event(c, lv, root, q, ext, vel, on)`: a resolved chord on one
of `CR_MAX_LOOPV` (8) loop voices, bypassing the chord-key state machine; voicing, performance, bass apply live.

## Timing

- Ticks: 96 per quarter note at the engine's BPM, accumulated from the ms clock with the remainder kept
  (no drift). A BPM change changes the loop's speed, never its pitch. Bar = 384 (4/4) or 288 (3/4, 6/8) ticks.
- **Free**: REC arms; the first chord starts the take; REC or LOOP ends it (length = elapsed, 24..65535 ticks).
- **1/2/4/8/16 bars**: REC starts a one-bar count-in (clicked; KNOB 3 turns it off), then records the sync length
  and commits by itself. REC/LOOP earlier: the length rounds up to whole bars.
- The take is the first cycle: playback continues seamlessly from the commit.
- **Overdub**: REC while playing (or stopped with a loop: it starts) arms; the next chord opens a layer whose times
  are cycle-relative; REC ends it, LOOP ends it and stops. Empty layers are discarded.
- **Quantize** (KNOB 2: none, 1/4, 1/8, 1/8T, 1/16, 1/16T, 1/32) moves note-ons to the nearest grid point at commit;
  note-offs stay (>= 1 tick); a note-on rounded past the end goes to 0.
- **Undo** (REC held, F#4 in the layer): the overdub in progress, else the last layer (the first stays: CLEAR).
- **Clear**: D#4 held 1 s in the LOOP layer. **Panic**: stops, keeps the loop; a fresh take is dropped, a partial
  overdub kept. **Level** (KNOB 4) scales the loop's velocities (0 = mute).
- **Metronome**: METRO tap = click on/off (on the take's / the loop's / its own grid), held = 4/4 3/4 6/8 picker,
  KNOB 1 there or OPT + SELECT = click level. The click is a 25 ms two-pole sine burst mixed after fx.c (cr_out.c
  `cr_click_mix`), 2.5 kHz on the bar's first beat.

## Slots and the record

Ten slots; only the selected one lives in RAM. LOOP held + a white root D4..F5 selects a slot (load when stopped,
at the end of the cycle when playing). SAVE held: Save / Load / Delete on the root-chosen slot, OCT+ does it. No
flash erase while a loop plays (Felucca's rule): a save then waits for the stop. The selected slot is a setting
(record v2) and comes back at power-on.

Flash: storage.c's commit record and A/B copies (its hooks, header and CRC; `storage.c` unedited), on user
sample slot 3's 80 KiB (0xC8000..0xDBFFF; nothing in ChoralRoot 0.1 writes it, `eng_sample.c` ignores non-sample
data): slot k copy c at `0xC8000 + (2k + c) * 4 KiB`, header type `0x4C30 + k`. Payload (little-endian):

```
0  u32 "CRL1"   4 u8 version 1   5 u8 sig   6 u8 nlayers   7 u8 0
8  u32 len (ticks)   12 u16 nev   14 u16 ppqn (96)
16 nlayers x u16: events per layer, then nev x 7 bytes: t u16, dur u16, root, vel, qx
```

Unpack checks everything (magic, version, ppqn, counts, each field's range) and refuses a bad record as empty: a
newer version or garbage never loads half a loop.

## Caps

512 events per loop (all layers; 4 KiB in RAM + a 4 KiB staging copy for slot switches; 3.6 KiB record),
32 layers, 16 open gestures, 8 loop voices, 65535 ticks. Reaching the event cap ends the take ("loop full").
