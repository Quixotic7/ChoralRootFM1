# The looper's record modes and the PLAY menu (design, 2026-10-09)

**Status: proposal, mock-ups to be approved** (`design/make_looper_mockups.py`, `design/choralroot-fm1-looper-screens.png`).
The slot bug (a slot's pattern lost on a switch) is fixed separately: a slot *is* its pattern, leaving a slot saves it
(docs/LOOPER.md "Slots and the record").

## What the user asked for (2026-10-09)

- Record modes: **Overwrite** (REC clears the loop and a fresh take starts with the first keys), **Advance** (each
  recording goes to the next slot), **Overdub** (today's), **Replace** (the notes under what you play are replaced),
  **Step** (chord mod + root locks a chord into the step; releasing every key advances to the next step).
- The PLAY menu (PLAY held): **PRESETS** turns the loop slot, **ALGORITHM** the record mode, **SELECT** the length.
- A way to play notes and chords with the PLAY menu open: **GLO** inside the menu toggles the keys between the
  loop shortcuts and playing; the setting is saved.
- While recording, the middle of the menu (the length, "Free") becomes a **record timeline**.

## The PLAY menu (LOOP held)

One screen, red, under the top line (the top line keeps the corner dial and its texts):

| band | what | knob |
| --- | --- | --- |
| slot strip | ten cells `1 .. 10`: filled = holds a loop, the selected one red and big ("Loop 3") | PRESETS |
| mode picker | Overwrite · Advance · Overdub · Replace · Step, the chosen one big, the neighbours peeking | ALGORITHM |
| the middle | **idle**: the length, big ("4 bars", "Free"); **recording / playing**: the timeline; **Step**: the step grid | SELECT (the length; in Step it is the number of steps' bars) |
| knob row | Quantize (pulses) · Count-in (gate) · Level (bar) · Keys (Play / Loops) | KNOB 1 .. 4 |

- **GLO** (OPT) tapped while the menu is open toggles **Keys**: `Play` = the roots and mods play as on the chord view
  (so the PLAY menu can stay open while you play into a take); `Loops` = today's shortcuts: white roots D4..F5 the
  slots 1..10, D#4 held 1 s clear, F#4 undo. The key LEDs follow (the slot map, or the normal keys). The setting is
  saved (settings record v8, `loop_keys`, default `Play`). KNOB 4 turns the same cell.
- **OCT+** does the picked action where there is one (today's Overdub / Pause / Undo / Clear picker is replaced by
  the mode picker; those actions move: Pause = LOOP tap, Undo = REC held, Clear = D#4 held with Keys = Loops, or the
  SAVE layer's Delete).
- The **record mode** is a saved setting (`loop_mode`, default Overwrite). It is also shown in the top line's right
  text while armed ("Rec · Overdub"), so the mode is known without opening the menu.

## The modes (REC outside and inside the menu)

| mode | REC when stopped / empty | REC while playing | the take ends |
| --- | --- | --- | --- |
| **Overwrite** | arms a fresh take (Free: the first chord starts it; synced: the count-in); a loop in the slot is cleared when the take starts (not on arming: REC again before the first chord cancels and the loop is back) | stops the loop and arms the same way | REC or LOOP as today; the take replaces the slot |
| **Advance** | as Overwrite, into the **next slot** (the current slot is saved by the slot rule; 10 wraps to 1; the slot strip shows the jump) | the same | the same; the new slot stays selected and plays |
| **Overdub** | a fresh take on an empty slot; otherwise today's overdub (arms; the first chord opens a layer) | today's overdub | REC ends the layer, LOOP ends it and stops |
| **Replace** | a fresh take on an empty slot | arms replace: while a gesture is held, the loop's events that start inside the held span are erased, and an event sounding when the gesture starts is cut there; the gesture is recorded as a new layer | REC ends it; Undo restores the erased events with the layer (the erased events are kept with the layer as "hidden" until the layer is undone, 512-event cap shared) |
| **Step** | enters step entry (the loop stops): a cursor on the first step; the grid = the Quantize setting (`none` → 1/16); a chord gesture writes the chord at the cursor (duration one step); when every key is released the cursor advances; **LOCK (B3) held** keeps the keys latched so a chord can be built from several presses before the release; OCT+ = a rest (advance), OCT- = back one step (the step's chords stay); the length grows (Free) or wraps (synced) | the same (stops first) | REC leaves step entry (Free: the length rounds up to the last bar used); LOOP leaves it and plays |

- The REC LED and the corner dial behave as today per state (armed, recording, overdubbing); Step shows the dial's
  REC dot steady and the cursor's step in the timeline.
- Quantize applies to Overwrite / Advance / Overdub / Replace at commit as today; Step is on the grid by nature.

## The timeline (the middle of the menu while recording or playing)

A strip 200 px wide, 40 px high, red on the panel's dark:

- synced length N bars: N bar boxes with beat ticks; the loop's events drawn as short marks on a lane per layer
  (up to 4 lanes, older layers dimmer); the playhead a red line; while recording the take's marks appear as they
  are played;
- Free, recording the first take: the bars appear as they pass (the current bar at the right edge, the earlier ones
  shifting left once there are more than four); the elapsed time ("0:07") under it;
- Step: the grid of steps (16 a bar, four bars at most across; more bars page), filled steps = a chord entered, the
  cursor a blinking red outline, the chord name of the cursor's step under the strip ("Dm7"), "step 5 · bar 2" right.

## Settings

Record v8: `loop_mode` (0..4) and `loop_keys` (0 Play / 1 Loops) from the reserve (`rsv_usb` is free since v7:
one of them goes there, the other into the next reserve byte; cr_settings.c's import gives older records the
defaults).

## Not changed

The take's timing, quantize, undo, the metronome, the slots' flash record, the SAVE layer (Save / Load / Delete),
the corner dial and where the ring is drawn, MIDI clock.
