# Velocity and aftertouch from the FM-1's keys (study and design, 2026-10-14)

**Status (2026-10-15).** Built on branch claude/monk-velocity-fa0d69 after the study below, flashed and played by the
owner (HISTORY 3y81): the scan's ADC slot and `cr_sense.c` (sections 3.1, 3.2); the root's press waits for its
velocity (3.3, the measured velocity, no scale); the top row at the keys' recent velocity (3.4, way 2); Options >
**Key Touch** (Fixed, the default / Dynamic / Velocity + Aftertouch), **Velocity Sens** and **Aftertouch Sens**
(the two entries of 3.6 became three; bits 2..7 of the block's `bass_on`); with Key Touch on, MONK-1's Touch:
Velocity law shapes the VEL source (`mod.c cr_touch_gain`) and the level follows the sound's **Expression**
routing instead of the engines' own: the editor's EXPR screen on every engine (the MOD group's last: VEL and AT to a
destination by an amount, mod.c's slots 3 and 4, saved with the sound; unset: VEL -> AMP at the full law, AT -> CUT
by Aftertouch Sens; docs/EDITOR.md), which MIDI velocity and aftertouch reach too; the keys' aftertouch as the
rest-following lean (3.5), to both parts as channel pressure and out as 0xD0 on the Perform and Bass channels,
polyphonic aftertouch in accepted. One figure of the study is wrong: the velocity is known at the second pulled-up
sample after the touch, with a driven sample before each, 8.8 ms after the touch sample (tests/cr_sense_test.c), not
4.4 .. 6.6; so a sensed root's note comes 7 .. 10 ms later than before, within cu_scan_keys's 12 ms wait. Not built:
the PA10 probe, polyphonic aftertouch out, the screen, calibration,
the 5 kHz variant. The host tests (HISTORY 3y82): `tests/input_test.c`'s analog block, `tests/cr_sense_test.c`,
`tests/mod_test.c`'s Key Touch cases, `tests/cr_settings_test.c`'s bits, `tools/emu/scripts/cr_expr.txt` in
`test_cr.sh`. The owner's readings: 689 note-ons in ten minutes,
velocity 1 .. 127, quartiles 37 / 73 / 98. The sections below are the study as written before the build.

**fm1-ctl, read 2026-10-15** (Charles Vestal's CTL-1, a MIDI controller firmware, at `de372e7`; the owner: "Charles
has said that his repo fm1-ctl has the best velocity and aftertouch implementation"). It is the same sensing
(`sense.c` is MONK-1's plus two settings, `hal/fm1_input.h` and `hal/fm1_adc.h` are MONK-1's byte for byte), tuned
by playing on his unit on 2026-10-15; what differs from what ChoralRoot has, and what it would take:

| what fm1-ctl does | ChoralRoot today | worth bringing in |
|---|---|---|
| the firm end at **530** (DRIVE 2's default floor of 450 sat under his firm presses, ~547, so his hardest hit topped out near 95), the light end 896, the curve 0.70 | floor 450, ceiling 896, curve 0.70 (`cr_sense.c SN_FLOOR`); the owner's unit reached 127 (689 notes: max 127, q3 98), so its firm presses sit lower than his | the floor as a setting, not a constant: units differ by ~120 counts at the firm end (Velo); a **Velocity Curve** -5 .. +5 that scales the map's exponent (x 2 at +5, / 2 at -5: `d_curve = 70 x 2^(0.2 c)`), which moves the MIDI velocity too (our Velocity Sens shapes the level after the map, not the number sent) |
| **the top row through the PA10 probe**, its own ends 0 / 43 and exponent 2.40 (his 20 / 48 linear "gave only medium and hard"); tried on every key and dropped (a light press moves the probe 8 counts against the direct reading's ~200) | C#5 .. G5 play at the keys' recent velocity (the context) | yes, if the owner wants real velocity on the top seven roots: the third frame phase (the analog rows at 3.3 ms), PA6 driven for the probe tick, PA10 an input then; a compressor on firm presses, no aftertouch there (the design's 3.4, way 3) |
| **Aftertouch Off / Poly / Channel**: poly pressure to each note of a chord key, the hardest-pressed key's as channel pressure while the arpeggiator runs | channel pressure on the Perform and Bass channels (the chord's notes are one finger's) | poly out as an option: a loop over the stream's sounding notes at the same moments (the design's 3.5); small |
| **AT sens -5 .. +5** on the same law (`P_SPAN`, 0.5 + 0.09 s) | Aftertouch Sens Low / Mid / High (three points of the same law) | a finer setting; cosmetic |
| the sampling runs only while Touch is on (`plat_sense`) | always (the pot and the battery use its slots) | no |
| the **Touch page shows** the last note's velocity and the pressure now, as bars | nothing on the screen | a GEEK OUT line, or the hot cell on the EXPR screen; the design's 3.6 |
| the **host emulator plays a key at a velocity**: `host/ctl_host.c land_for` inverts the map into the driven landing the sensing would read, and a lean into the pressure, so scripts say `key 0 down 30` and expect `last_vel` 24 .. 36 | the emulator has no analog model; `cr_sense_test.c` feeds curves by hand | yes: the same inverse in `tools/emu` gives scripts velocity and aftertouch (the design's 3.7) |
| the manual's advice: "pressing near the bottom edge of a key plays softer than the middle, on every key: the most reliable way to play quietly"; the two zones; "a note plays about 5 ms after the key goes down" (ours: 8.8 ms after the touch sample, measured) | MANUAL.md has no Touch section yet | the manual's words, with our timing |

Not in fm1-ctl either: Velo's per-key calibration (off there too), a probe that tells light presses, anything
faster than WAIT 2. So "the best implementation" is the same method with his unit's tuning and two more controls;
the owner's unit already reaches the full range with ours. The order, if wanted: the Velocity Curve setting (cheap,
it also makes the floor tunable by feel), the emulator's velocity model (it makes the rest testable), the top row's
probe, poly aftertouch out, the readout.

**Brought in (2026-10-15, after the reading): Options > Velocity Curve** -3 .. +3, the entry after Velocity Sens:
the map's exponent 0.7 x 2^(0.2 k) (seven tables, `cr_sense.c SN_CURVES`; x 1.5 at +3, / 1.5 at -3: higher, you play
harder to reach 127; lower, softer playing reaches it), which moves the velocity the keys send, MIDI included,
where Velocity Sens shapes the level after it. Stored in bits 5..7 of the block's `pick_roots` (docs/SETTINGS.md).
**And the emulator's velocity model** (fm1-ctl's idea): `cr_sense.c` is compiled into `tools/emu` and fed by
`emu_fw.c emu_touch_feed` from the script's keys: `btn D4 down 30` lands the key at the driven reading the map at
Curve 0 gives 30 (the map inverted over its table), 300 pulled up, and `lean D4 60` presses it 60 % of the way to
full aftertouch once its rest has settled; the UI's traces `touch: key K vel V sensed|context|fixed` and `touch: at
N` are what `cr_touch.txt` checks in `test_cr.sh` (tools/emu/README.md).
**And the other three (2026-10-16, the owner: "Yes: bring in the top row's PA10 probe, poly aftertouch out and the
velocity / pressure readout")**: the top row through the PA10 probe (`hal/fm1_input.h`: a third frame phase while
Key Touch is on, PA10 an input with its pull-up and PA6 driven at the weakest level for the tick, the analog rows then
every 3.3 ms; `cr_sense.c cr_sense_probe`: each key's open baseline and the peak above it, CTL-1's ends 0 / 43 and
curve 2.40, the velocity taken once two readings are in; C#5 .. G5 sense velocity, no aftertouch); **Vel + Poly AT**,
Key Touch's fourth value (the pressure out as polyphonic aftertouch to every sounding note of the Perform and Bass
streams, `cr_out.c cr_touch_at`; the parts hear it as before); and the readout on **GEEK OUT 2**'s title line while
the keys sense, `VEL 73 AT 42` (the last key's velocity, the aftertouch now) in place of the cores' count. The
emulator models the probe too (`emu_touch_feed`: a top key's rise from its velocity, the analog rows every 3 ms
while it runs). The probe's price: the analog rows every 3.3 ms, so the lower keys' velocity is known 13.2 ms after
the touch sample instead of 8.8, and `cu_scan_keys` waits 18 ms for it (12 before: the designer-handoff session's
release agent caught the miss, 3y124). Still not here: Velo's per-key
calibration, a probe that tells light presses, anything faster than WAIT 2.

The owner: "look into https://github.com/charlesvestal/fm1-monk: he added velocity and aftertouch to the FM-1, which
would be super cool to bring in to ChoralRoot." This is the study of how fm1-monk does it and the design of the same
thing on ChoralRoot. **Nothing is built**: no code, no test, no measurement on the unit (another session holds it
tonight). The step list at the end is the proposal; the owner decides.

Read for this (cloned 2026-10-14, read only, not built): fm1-monk at `8af3c3d` ("README: how the keys sense velocity
and aftertouch") and its source, Velo (`fm1-velocity`, the lab firmware the sensing comes from) at `8f62ce0` with its
`notes/01-lab.md` (the measurements, two sessions, 2026-10-08 and 2026-10-14). Both are Charles Vestal's, both
GPL-3.0-only, both built on Felucca's platform layer by way of FoMni; the matrix, the 595s, TIMER5's scan and the
`hal/fm1_input.h` they start from are the same as ChoralRoot's.

## 1. What fm1-monk does

### The finding: the contact is a force sensor the chip can already read

The FM-1's keys are rubber domes with a carbon pill, one contact each, in an 11-column x 6-row diode matrix behind the
two 74HC595s; the stock scan asks only whether a key is closed. Two things make more of it:

- **A carbon pill's resistance falls with force** (conductive rubber: tens of kilohms at a graze, hundreds of ohms
  seated, lower still when squeezed), and the pill goes on responding while the key is held.
- **Two of the matrix rows are ADC inputs.** The AC79's SARADC channels 0 and 1 are PA7 and PA8, the rows of 20 of the
  27 note keys. The row has a 10 k pull-up; the 595 drives the column low; the key's pill and the matrix diode are in
  between. So the row's voltage is a divider that reads 1023 open and falls as the pill conducts. The third note row,
  PA6 (the seven keys at the right end, C#5 .. G5), has no converter. PA10, the LED line of row PA6, is channel 2; the
  MASTER pot (PB6) is channel 4 and the battery divider (PB1) channel 3, as ChoralRoot reads them today.

Nothing is changed on the board. Velo's first session (2026-10-08) found the obvious reading nearly useless: through
the 10 k pull-up a seated pill is almost nothing against the diode's 0.6 V drop, so every played key seated at the same
~185 counts and pressing harder moved it 10 .. 20. The dome snaps, so the time between two thresholds carried no
velocity either (0 or one sample for every press). What revived it (2026-10-13 / 14) is **DRIVE**: a key that has been
touched is sampled every other time with its row driven high as an output at the pin's weakest level (the datasheet's
2.4 mA). A driver that weak cannot hold 3.3 V against a seated pill, so it current-limits and the pad reads the diode
plus a fixed current through the pill: an ohmmeter, linear in the pill's resistance. Soft and firm presses then
separate by 100 .. 150 counts where the pull-up gave 5, and firm presses repeat within +-5 counts at DRIVE 2 (the level
MONK-1 uses; DRIVE 1 has a wider range but more spread). The row is an input with its pull-up again at the next tick,
before the rows are read, so the digital scan never sees the drive.

### How it is sampled (`hal/fm1_input.h`, Velo's additions)

The tick is the same 10 kHz, one column a tick, as ChoralRoot's. After it latches column n it **starts one ADC
conversion** on that column's PA7 (even frames) or PA8 (odd frames) and **takes the result at the next tick**, 100 us
later (a deferred read: the converter settings that return real values, clock divider 7 and start delay 2, make a
conversion of ~60 us; anything faster returns rails, delay 4 overruns the tick). So each of the 20 analog keys is
sampled once per two frames, **every 2.2 ms**; the MASTER pot takes one slot (odd frame, column 0, whose PA8 key is
OCT-) and lands in `fm1_an.master`: the tick owns the converter, and the main loop may no longer call
`fm1_adc_read` while the scan runs (fm1_monk reads the pot from `fm1_an.master`). Each sample goes to the application
through a hook, `FM1_INPUT_ANALOG(col, rowbit, v, driven)`, and before each sample the tick asks
`FM1_INPUT_DRIVE(col, rowbit)` whether to take it driven. `fm1_adc.h` gains non-blocking start / done / take / abort
and the CON word (divider, delay, channel).

**LEDs.** A lit LED's current goes through the same column line and lifts that column's readings by ~25 counts
(the 595 output's resistance is visible). So `FM1_INPUT_LED_OK(col, rowbit)` asks whether the key about to be sampled
is open; for a held key the column's LEDs stay dark on the tick that samples it (1 tick in 22 for that column: the
buttons "barely dim"). fm1-monk's LEDs are Felucca's lit + FoMni's one dim level on one frame in N; ChoralRoot's four
levels (lit, dim2, dim, faint as pulses riding on the 595 shift) are discussed in section 3.

### What is measured, and how it becomes velocity (`firmware/src/app/sense.c`, Velo's, host-tested)

A state machine per key on the pulled-up samples: OPEN, TOUCH (below TOUCH 900: the pill has started to conduct),
DOWN (below PRESS 400, seated), LIFT (above PRESS), and OPEN again after two samples above RELEASE 600 (a landed key
can read open for one sample: a bounce). A key that touches and never seats is a graze (an event, no note). The
digital debounce still runs beside it; MONK-1 plays the analog keys from the sense events and the top row from the
digital edges.

**Velocity is not a time: it is how deep the key sits just after landing.** The snap puts every press down within a
sample or two whatever its speed; what tells a hard hit from a soft one is the driven reading right after. The
velocity is the **lowest driven reading in the first WAIT (2) driven samples after the touch** (a sample that catches
the key still falling reads high, so a mean made hits soft; the deeper of two samples is steadier than one), ready
**4.4 .. 6.6 ms after the touch sample** (WAIT 1: 2.2 .. 4.4). It is mapped on a **log scale of the pill's
resistance**: reading - 185 (the diode and the 595: `SN_DBASE`) is the resistance; conductive rubber's resistance
falls as a power of the force, so its log is linear in the log of the force; t = log(r / r_floor) / log(r_ceiling /
r_floor) with the key's floor (its firmest landing, default 450 at DRIVE 2) and ceiling (its lightest seat, LIGHTEST
896), and velocity = 1 + 126 (1 - t)^0.7. Measured on 604 notes played freely: quartiles 40 / 59 / 74, three at
127, twelve below 10: "remarkably usable". Keys differ at both ends (~120 counts at the firm end, ~250 at the light
end) and where a finger lands on a key is part of the reading (off-centre, the pill rolls down from one edge and
reads softer), so Velo built a per-key **calibration** (each key's floor and ceiling as running quantiles of its
landings, saved); its first session played more extremes, not fewer (an open item there), and **MONK-1 ships with it
off**, at Velo's measured best fixed settings: DRIVE 2, WAIT 2, the log map, curve 0.7, LIGHTEST 896.

**Aftertouch is pressing harder than the key rests.** Every held analog key's driven reading keeps coming (every
2.2 ms). MONK-1's `ui.c pressure()` (every 5 ms, main loop) keeps a per-key rest: it follows the reading down for
250 ms after the landing (a key goes on settling after the note), then creeps after it slowly, and jumps back up at
once when the key is eased; the lean is log2(rest / reading) in octaves of resistance, 0.04 octave is noise, SENSITIVE
sets how many octaves make 127 (0.12 .. 0.8; a light rest to a firm press measured ~0.7 at DRIVE 2), smoothed over
~30 ms. The sounding key's pressure drives the voice; **every held key's goes out as polyphonic aftertouch** (0xA0,
only on change). Velo's first idea, "how lightly held" from the pull-up floor, made every soft note full pressure
and was dropped.

**A glow on the top row** (the owner, after 0.39's drums build: "the LEDs on the keybed with scale turned off always
dimly light up" C#5, D#5 and the white keys up to F5): the probe's price, since it came (3y123), with Key Touch on
(Dynamic, Velocity + Aftertouch, Vel + Poly AT) whatever the build: its tick makes PA10, the LED line of row PA6, an
input with its pull-up, and "the LED in column n passes a little current" (`hal/fm1_input.h`), one tick in 33 for
each of the seven top-row keys C#5 .. G5. With Key Mode off their own LEDs are dark, so the glow shows. Key Touch
Fixed (the default) runs no probe and lights nothing. Not changed: the reading is the current through that LED.

**The top row** (PA6, no converter): MONK-1 probes it through PA10, the LED line of row PA6, which is channel 2: for
that tick PA10 is an input with its pull-up and PA6 is driven high at the weakest level, so a pressed PA6 key pushes
a current through its pill into the column and lifts the 595 output's low level, more when pressed harder (contact
~20 counts, a light hold 25 .. 34, a squeeze 40 .. 46). It takes one frame in three (the analog rows drop to every
3.3 ms) and saturates on firm presses (a compressor); Velo compared it with the direct reading on the same keys:
"the same measurement two ways, neither more consistent". Those seven keys have velocity and no aftertouch.

### MIDI, cost, licence

- **MIDI out**: the notes on channel 1 with their velocity; polyphonic aftertouch per held key. **In**: notes (any
  channel) with velocity, channel and polyphonic aftertouch to the pressure, a few CCs.
- **Cost**: one conversion a tick (start: three register writes; take: two reads and two writes), the hook call with
  the per-key state machine (a few dozen lines a sample), on a driven sample four GPIO writes to drive the row and
  four to release it, and the LED check. Not measured in either repo; by ChoralRoot's reading of its own tick
  (`docs/INTEGRATION.md` on worktree-dualcore-1, "The fx stage's parts and TIMER5": 7 us a tick, of which the 595
  shift's 110 GPIO accesses at ~35 .. 40 ns are 60 %), the additions are about 10 .. 15 register accesses and
  ~60 .. 100 lines a tick: **roughly 1 us a tick, ~1 % of core 0**. The scan rate is unchanged. The note comes later:
  the velocity sample instead of the debounce (section 3). RAM: `sense_key_t` is ~60 bytes a key (with a 48-sample
  capture of the last press for the lab screen, which ChoralRoot would not keep): 27 keys, ~1.6 KB as it is, well
  under 1 KB without the capture and the probe.
- **Licence**: GPL-3.0-only, both repositories, the same as ChoralRoot's; `sense.c` and the `fm1_input.h` /
  `fm1_adc.h` additions carry `SPDX-License-Identifier: GPL-3.0-only`; `fm1_input.h` and `fm1_adc.h` keep Leo
  Kuroshita's copyright line (they are Felucca's headers, extended). A port is a row in `LICENSING.md` under
  third-party material, as Melodee's and SLOOP's: "Velo (fm1-velocity) and MONK-1 (fm1-monk) by Charles Vestal
  (GPL-3.0-only): the key sensing (`cr_sense.c`, from `sense.c`), the scan's analog sampling and the driven samples
  (`hal/fm1_input.h`), the converter's non-blocking reads (`hal/fm1_adc.h`); the measurements in Velo's
  `notes/01-lab.md`", with the files, and a line in the README's credits.

## 2. What it means on ChoralRoot's keys

ChoralRoot's key k (0 .. 26, MIDI 53 + k before the octave) and the rows:

| keys | row | converter | ChoralRoot's use (Chords layout) |
|---|---|---|---|
| F3 .. C#4 (k 0 .. 8; ids 14 .. 22) | PA8 | **yes** | the chord block: 6, DIM, M7, MIN, MAJ7, MAJ, B3 = LOCK, 9, SUS (`cr_ui.c CU_MOD_OF_KEY`) |
| D4 .. C5 (k 9 .. 19; ids 23 .. 33) | PA7 | **yes** | the first eleven roots (`CU_ROOT0` 9) |
| C#5 .. G5 (k 20 .. 26; ids 34 .. 40) | PA6 | no | the top seven roots |
| OCT-, OCT+ | PA8, columns 0 and 1 | (yes) | buttons; the pot's slot is OCT-'s |

So in the Chords layout **11 of the 18 roots** have the converter and the chord block, which only modifies, has it
for nothing; in the Full keyboard (`cs.kbd_full`) 20 of 27 notes. The top seven roots are the question the design has
to answer (section 3, "the top row").

**One thing is better here than on a real keyboard.** Velo's verdict against the pull-up reading was that "a chord's
fingers land unevenly (59, 76, 127, 104 on one chord), which no player can control", and Velo built CHORD (notes within
a window share the first note's velocity) to fix it by fiat. ChoralRoot's chord comes from **one root key**: every
note of the chord, the bass and the strum take the root's velocity (`cr_engine.c crx_start_voice`: `v->vel`), so the
one velocity that matters is one finger's, which is the case the sensing does well. In Advanced style two roots make
two chords, each with its own: right as well.

## 3. The design

The rule: the digital scan stays the source of truth for what is pressed (every layer, the LOCK tap, the chord
block, the hold menus, the pump inside redraws rest on `fm1_in.notes`), and the analog reading **adds** a velocity
to a root's press and a pressure to a held root. Nothing in the UI's meaning of a key changes.

### 3.1 The scan (`hal/fm1_input.h`, `hal/fm1_adc.h`): Velo's additions, re-cut

- `fm1_adc.h`: the non-blocking start / done / take / abort and the CON word, as Velo's. `fm1_adc_read` stays for
  power-on (`master.c master_boot`, before TIMER5 starts) and the host tests.
- The tick starts a conversion on the latched column's PA7 / PA8 row (even / odd frames) and takes it at the next
  tick, as Velo's; the hook `FM1_INPUT_ANALOG(col, rowbit, v, driven)` and `FM1_INPUT_DRIVE(col, rowbit)`; the drive
  at level 2, restored before the rows are read. **The converter becomes the tick's**: the MASTER pot takes OCT-'s
  slot into `fm1_an.master` (`master.c master_poll` reads that instead of converting: the smoothing stays), and the
  battery (`main.c`, read every pass today) takes one slot a second (OCT+'s, say) into `fm1_an.batt`. The
  main loop's blocking conversions of the pot and the battery go away with it.
- **The LEDs.** ChoralRoot's tick lights `fm1_led[n]` right after the latch and puts the faint / dim / dim2 pulses of
  column p at the start of the next tick, riding on the 595 shift, before `fm1__rows()`. The conversion started at
  the end of tick t runs ~60 us and is taken at the start of tick t+1, **before** that tick's pulses (its first act
  is `fm1_an.pend`), so the pulses never overlap a conversion. What does overlap is the lit LEDs of column n during
  the conversion (+25 counts on that column's keys): `FM1_INPUT_LED_OK` as MONK-1's, so a held key's column has its
  lit LEDs dark on the tick that samples it. A root that is held has its own LED lit (the chord's note names light
  every root of their name): dark 1 tick in 22, 4.5 % less light, invisible. The lit LEDs of the other columns are
  not touched. Checked in `tests/input_test.c`'s LED model (the lines dark at every latch, the pulses' widths): the
  sampled tick of a held key must read the lines dark while the conversion runs and the pulses unchanged.
- **The 5 kHz scan** (INTEGRATION.md's recommendation 2, not built either) pulls the other way: a tick of 200 us
  would sample each analog key every 4.4 ms and the velocity would be ready 8.8 .. 13.2 ms after the touch (WAIT 2):
  too slow. The way both fit: at 5 kHz a tick has room for **two conversions** (60 us each); the tick starts PA7 and
  enables the converter's interrupt (CON bit 5), the ADC interrupt takes it and starts PA8 on the same column, and the
  next tick takes that: both rows every frame, each key still every 2.2 ms, one short interrupt more a tick. It is
  written down so that the 5 kHz work, if it comes first, leaves the hook and the two-conversion phase in. Decide the
  order with the owner; this design assumes 10 kHz as the unit runs today.
- **Not ported**: the PA10 probe of the top row (its frame in three slows the analog rows to 3.3 ms, it saturates on
  firm presses, and it drives PA6 high at the moment the LED line is an input: more to get wrong in the LED pulses),
  the self-test, the immediate reads, the timed conversion, the capacitance probe. The probe can come later as its
  own step if the top row's fallback is not good enough (section 3.4).

### 3.2 The sensing (`firmware/src/cr_sense.c`, from Velo's `sense.c`)

Ported as it is in its logic (the state machine, the driven samples, the log map, the rest-following pressure) and
re-cut for ChoralRoot:

- **No HAL, host-tested** (`tests/cr_sense_test.c` from fm1-monk's `tests/host/sense_test.c`: the measured curves,
  a graze, a bounce while held, the events), run by `tests/run_cr_tests.sh`.
- **Integer** (the engine's rule): the log2 of a 10-bit reading as a small table (a 64-entry Q8 fraction table on the
  leading bit, 128 bytes) instead of `fm_log2f` / `fm_powf`; the curve 0.7 as a 128-entry velocity table built at
  compile time by `tools/gen_tables.py` (as the other tables), so the ISR does lookups, no float. The host test
  compares the table map against Velo's float map on every reading: within one velocity step.
- **The configuration fixed** at MONK-1's: DRIVE 2, WAIT 2 (the owner may prefer WAIT 1: 2.2 ms sooner, a little less
  steady), TOUCH 900 / PRESS 400 / RELEASE 600, curve 0.7, floor 450, LIGHTEST 896, calibration off (Velo's open item;
  the hooks stay so it can be tried as a step of its own). No capture buffer, no probe, no release velocity.
- **Its outputs**: `cr_sense_vel[27]` (0: not known yet for this press; 1 .. 127 from the key's EV_ON, cleared at its
  release), `cr_sense_pressure(k)` (0 .. 127 of a held analog key, the rest-following lean, computed where it is
  read), and the per-key state for the console (`inp` gains the readings: floor, last landing, timeouts).
- **Where it runs**: `cr_sense_sample` in the tick (the hook), as Velo's; the pressure's smoothing and rest in the
  reader (3.5). The tables are written by TIMER5 and read by the main loop and the audio ISR: bytes and halfwords,
  volatile, no locking.

### 3.3 Where velocity lands: the posted press (`cr_ui.c cu_key_press`)

Today every root posts `cr_post(CRE_KEY, note, cs.vel, 1)` with Options > Velocity (1 .. 127, default 100) as the
chord's velocity; `cr_key_ex` stores it in the voice (`v->vel`), and from there it is the velocity of **every note the
voice plays**: the chord's notes (`crx_sound_voice`, `crx_sync_owner`), the bass (`crx_bass_start`: the same
`v->vel`), the strum / slop / harp notes (`crx_strum_fire`), the arp (`crx_fire_clocked_once`: `v->vel`, the
pattern's accents scaling it), a retrigger of the same root at its new velocity (design.md 5.3), the last-chord
record, the looper's events and the pads (`cr_pad_set`, `cr_loop_event_v`: the velocity travels with the chord).
`cr_out.c cr_cb_note_on` hands it to `trk_note_on` (the voice: `voice.c voice_start v->vel`, the amplitude
`mulq15(env, vel * 258)`, the accent that opens the filter above 110, the engines' own velocity laws: FM6's operator
levels, VA's `VS_VEL` source and `VA_VEL`, QUAD's `QP_VEL`, DIGITAL's and FORMANT's brightness, DRUM's and PHYS's
accent; mod.c's VEL source) and to MIDI (`0x90`, the stream's channel: Perform 1, Bass 2, Raw 3). **So one change
puts the key's velocity everywhere**: the velocity in the post.

The press is debounced 1.1 .. 2.2 ms after the contact; the velocity is known 4.4 .. 8.8 ms after it (WAIT 2: the
touch sample lands anywhere in a 2.2 ms slot, then two driven samples). So a root on an analog row **waits for its
velocity**: `cu_scan_keys` sees the press edge, and when the key is a root with the converter and Key Velocity is on,
it holds the press in a pending mask instead of posting it; each scan posts the pending roots whose `cr_sense_vel` has
arrived (the pump inside redraws scans too, so a frame's draw does not add to the wait), and a root still pending
**12 ms** after its edge posts with the fallback velocity (a contact the digital debounce saw always crosses TOUCH, so
this is a safety, not a path). The release is the digital one, as today (9 ms: the sensing's two samples above RELEASE
is ~4.4 ms and would end a bouncing note early). What it costs: **the chord's attack 3 .. 7 ms later than today** on
the eleven analog roots (WAIT 1: 1 .. 5 ms), nothing on the chord block, the top row, the buttons. The whole path of a
key is 4 .. 8 ms today (INTEGRATION.md "Keys while a frame draws"); this makes it 7 .. 15. The owner should play it
with WAIT 1 and WAIT 2 before it is fixed. The loop records the posted velocity, so a recorded take plays back as it
was played; `cr_klat` (78 PEAK's key timing) measures the new wait as it measured the old.

**The velocity's scale and Options > Velocity.** Velo's free playing has its median at 59 and 127 rare: a ChoralRoot
unit that plays TINE EP at 59 instead of 100 is quieter than today, and the owner's Velocity setting (`cs.vel`) is how
loud the keys play. Proposed: the key's velocity is **scaled so that a medium press plays at Options > Velocity**:
vel = clamp(key_vel x cs.vel / 64, 1, 127) (a medium 59 .. 64 reproduces today's level at the default 100; a hard hit
goes to 127, a light one to ~40 x 100 / 64 = 62). So the setting keeps its meaning (and its 1 .. 127 range, its
saved block field, its MIDI), and Key Velocity on or off changes how the keys feel, not how loud the sound is. The
alternative, the key's velocity raw and Options > Velocity ignored, is one line to switch.

### 3.4 The top row, and the keys that do not sense

The seven roots C#5 .. G5 have no converter (and the Full keyboard's top seven notes). Three ways, by cost:

1. **Fixed**: Options > Velocity, as today. Simple, and a chord played up there is louder or softer than its
   neighbour by the player's last touch.
2. **Context** (Velo's TOP CONTEXT): the smoothed mean of the last few analog velocities (the UI keeps it: a byte,
   updated at each posted analog root). A phrase that walks up the keyboard keeps its loudness; the first chord after
   silence takes the fixed value. **Proposed as the first step**: no scan cost, two lines, and the Full keyboard
   gets it for nothing.
3. **The PA10 probe** (MONK-1's): real per-press readings, a compressor on firm presses, a third frame phase (the
   analog rows at 3.3 ms, the velocity 1.1 ms later), PA6 driven high during a tick's LED window. A step of its own,
   after the owner has played 2.

The chord block's keys sense but do not play; their readings are ignored (the state machine runs on them for the
console only, or they are masked out of the analog set: `cr_sense_init(mask)` takes the roots).

### 3.5 Aftertouch: a modulation source and MIDI

**Into the sound.** mod.c already has the source: `MS_AT`, "channel aftertouch", `t->at` written by `mod_midi` on
0xD0 (and reset by CC 121), routable in the matrix to PITCH, CUT, SHP, AMP, PAN, the sends, the LFO's rate and
vibrato, and the engine's eight parameters E1 .. E8, per part. **Key pressure writes the same byte**: a new engine
event `CRE_AT(part, value)` posted by the UI's scan when the part's pressure changes, applied in the audio ISR beside
the MIDI events (`cr_out.c midi_event`'s 0xD0 branch: `mod_midi(&trk[p], 0xD0, v, 0)`), so the sound hears the key
exactly as it hears a MIDI keyboard's aftertouch, and every sound that already routes AT (the matrix's slot, saved in
the sound) responds at once. Nothing new in the engines or the voices.

**Which pressure.** The chord part's is the pressure of its roots: the **highest pressure among the held analog
roots** (one root: that root's; two in Advanced style: whichever leans harder; a root let go: the other's), and the
bass part's the same value (the bass is the chord's). Polyphonic within the chord makes no sense here: the chord's
notes are one finger's. Rate: the UI reads `cr_sense_pressure` every 5 ms (`cu_scan_keys` runs more often than that
while the loop idles; inside a redraw the pump runs it; MONK-1's 5 ms), smoothed ~30 ms as MONK-1's, and posts only
on change (at most 200 events a second, in practice a few tens while a key leans). The rest-following zero and
SENSITIVE as MONK-1's (`P_SETTLE` 250 ms, `P_DEAD` 0.04 octave); Sensitive as an Options entry (section 3.6), default
MONK-1's 50 (~0.46 octave for 127).

**MIDI out.** Channel aftertouch (0xD0) on the Perform channel and the Bass channel, on change, the same value the
part hears; Raw (channel 3) the same when enabled. Polyphonic (0xA0 per sounding note) is the alternative if a DAW
should see the chord's notes separately; it is a loop over the stream's sounding notes (`cr_ono`) at the same
moments: offered as an option later, not first. The key's velocity goes out in the note-on as it does today.

**MIDI in** is already there: 0xD0 on a part's channel reaches `t->at`. Polyphonic 0xA0 in is forwarded and dropped
by `mod_midi` (it takes 0xD0 only); one line maps it to `t->at` as well, so a recorded MONK-1 performance plays into
ChoralRoot. Small, and separate.

### 3.6 Settings and the screen

Two Options entries, saved as the block's automatic fields (`cr_settings.h`, docs/SETTINGS.md: a format version
bump), defaults off so that an updated unit plays as before until the owner turns them on:

| entry | values | what |
|---|---|---|
| Key Velocity | Off, On | the analog roots' presses carry their velocity (3.3), the others the context (3.4) |
| Aftertouch | Off, Low, Mid, High | the held roots' pressure to the parts and MIDI (3.5); the level is Sensitive |

Options > Velocity stays (the medium press's level with Key Velocity on; the velocity with it off). The emulator's
`cr_opt_keys` walk and the Options scripts move by two entries, as the 6th with 7th entry moved them (HISTORY 3y30).

The screen: nothing new at first. The owner's taste is one big thing per screen, and the velocity is heard. Two
small, contained things when the feel is settled: the chord's name drawn at a brightness that follows the velocity
of the press (the white of the role colours, dimmer for a soft chord, over the first ~300 ms), and the GEEK OUT 2
page's input line showing the last press's reading, floor and velocity (the console's `inp` has them from step 2).
Aftertouch could move the voicing line's glow; not designed here.

### 3.7 The emulator and the host tests (what exists, what is added)

- `tests/input_test.c` (the TIMER5 scan, the debounce, bouncing contacts, the LED pulses, the tick's cost): the ADC
  slot is added to its model of the tick: a conversion started at the latch is done 60 us later, taken at the next
  tick, the row's drive set and restored around it, the lines dark on a held key's sampled tick, the cost of a tick
  with the sample. It compiles the header as the firmware does, so the hook is exercised.
- `tests/cr_sense_test.c` (new): fm1-monk's `sense_test.c` curves (a hard hit lands at ~390 .. 450 driven, a gentle
  press 515 .. 584, a graze, a bounce, two keys at once), the integer map against the float one, the pressure's rest
  following a settling key and jumping back on an easing.
- `tests/cr_engine_test.c`: already runs the chord, bass, strum and arp with a velocity per key; a case with two
  roots at two velocities (Advanced style) and a retrigger at a new velocity exists or is one more.
- `tests/mod_test.c`: an AT slot driven by `t->at`; `tests/cr_midi_test.c`: 0xD0 out on the stream channels and
  0xA0 in to the part.
- `tools/emu` (`emu_fw_tick` drives `fm1_input_tick`'s inputs): the emulator gets an analog model, a reading curve
  per press from the click's position on the key (MONK-1's browser emulator: "click lower on a key for more velocity,
  drag down for aftertouch"; the scripts: `key D4 120 vel 90`), so `test_cr.sh` can `expect` a note-on's velocity in
  the dump and the key-latency scripts (`cr_keylat.txt`) show the new wait. The emulator has no ADC today: the
  conversion is a register the tick reads, `emu_hooks.h` supplies it.
- On the unit: `tools/perf_unit.py` (worktree-dualcore-1, GEEK OUT 2) reads `pk_t5_us`, TIMER5's time a tick: the
  before / after of the sampling's cost, and `inp`'s timeouts (a conversion not done in a tick) must stay 0.

## 4. The cost

| | today | with the sensing |
|---|---|---|
| TIMER5 a tick | 6.6 .. 7.3 us (7 % of core 0) | +~1 us (one conversion's registers, the hook, the drive's writes on a held key's sample): ~8 % |
| a root's note after the contact | 4 .. 8 ms (1.1 .. 2.2 debounce + the post + the block) | 7 .. 15 ms on the eleven analog roots (WAIT 2; WAIT 1: 5 .. 11); the others as today |
| RAM | 81 332 B, 82.7 % (HISTORY's last figures) | +~900 B (27 keys' state without the capture, the pending mask, the context) |
| flash (XIP) | 483 396 B | +~3 KB (the sensing, the tables, two Options entries) |
| the main loop | | the pressure read every 5 ms: a few hundred instructions |
| the audio ISR | | nothing (the AT event is a byte write; the matrix's AT slot costs what it costs today) |
| the pot and the battery | two blocking conversions a pass of the main loop | none: the tick's slots |

A 5 kHz scan: compatible only with the two-conversion tick (3.1); otherwise the velocity waits twice as long.

## 5. The risks

1. **The feel is unmeasured on this unit.** Velo's numbers are one FM-1's; keys differ by ~120 / ~250 counts at the
   two ends and the calibration that should absorb that is Velo's open item. The fixed map may play one owner's keys
   hot or cold; the first session on the unit is with the console's readings up, and the floor / LIGHTEST are
   settings in the console (not in Options) until they are right.
2. **Latency.** 3 .. 7 ms more on the analog roots is audible on a percussive sound and in a strum; the owner has to
   want velocity more than that. WAIT 1 halves it. An off switch keeps today's behaviour exactly (the pending mask is
   never entered).
3. **Uneven keys inside one phrase**: the eleven analog roots sense, the seven top ones take the context; a run across
   C5 / C#5 changes how the keys answer. The probe is the answer if it is felt.
4. **The LEDs and the reading.** ChoralRoot lights more than MONK-1 (the chord's note names across the board, the
   tonic's dim2, the faint idle chord keys): the +25 counts of a lit LED on the sampled column are kept out by
   `LED_OK` on the sampled tick, but a pulse (faint / dim / dim2) that runs late into the next tick's conversion
   window would not be; the input test's timing model checks it, the unit's `inp` timeouts and a held key's reading
   while the LEDs change confirm it.
5. **The converter is shared.** The pot and the battery move into the tick; `master_boot` keeps its blocking reads
   before TIMER5 starts; nothing in the main loop may call `fm1_adc_read` after. JieLi's Bluetooth library uses the
   ADC (Velo's note): not in ChoralRoot.
6. **The digital input buffers of PA7 / PA8 must stay on** (Velo's trap: off, the scan reads every key on them as
   pressed, OCT- and OCT+ included, and five seconds later that was the update-mode gesture; ChoralRoot's gesture is
   OCT- + OCT+ + HOME, the same two rows). The drive is an output for one tick and an input with its pull-up again
   before `fm1__rows()`: a fault in that restore reads a whole row pressed. The input test asserts the row's
   direction at every rows read.
7. **Dual core**: TIMER5 runs on core 0 and the tables are plain bytes; core 1's render reads `t->at` of part 1 as it
   reads a MIDI aftertouch today. Nothing new crosses the cores.
8. **RAM at 82 .. 85 %**: ~900 B is within what the last entries moved, to be checked in the build's figures.
9. **The loop and the pads record velocities now**: a take recorded with Key Velocity on plays back with the dynamics
   it was played with, which is the point, and a take recorded before plays as before (its velocities were `cs.vel`).

## 6. The steps (each small, each with its test)

1. **`hal/fm1_adc.h` non-blocking, `hal/fm1_input.h`'s ADC slot and hook** (no consumer: the hook empty, the pot and
   the battery from the tick's slots). Test: `input_test.c`'s tick model with the conversion, the drive's restore, the
   lines dark on a sampled held key, the cost; the unit: `pk_t5_us` before / after, `inp` timeouts 0, MASTER and the
   battery read as before. Nothing audible changes.
2. **`cr_sense.c`, integer, with the console's readings** (`inp`: each key's last landing, floor, velocity, the
   pressure). Test: `cr_sense_test.c`; the unit: the owner plays and reads the numbers; floor / LIGHTEST / WAIT in
   the console. Still nothing audible.
3. **Key Velocity** (Options entry, the pending post in `cu_scan_keys`, the scale on `cs.vel`, the context for the
   top row). Test: `cr_engine_test` (velocity through chord, bass, strum, arp, retrigger), the emulator's analog
   model and a script that expects velocities in the dump, `cr_keylat.txt` for the wait; the unit: the owner's ear,
   WAIT 1 against 2.
4. **Aftertouch** (Options entry with Sensitive, the pressure read at 5 ms, `CRE_AT` to `t->at`, 0xD0 out). Test:
   `mod_test` with an AT slot, `cr_midi_test` for the out and the poly in, the emulator's drag; the unit: a sound with
   AT -> CUT in its matrix (the editor's MOD page already offers the AT source).
5. **docs**: LICENSING.md's row and the README's credit (with step 2), docs/SETTINGS.md's two fields and the version
   bump (step 3), MANUAL.md's Options entries (steps 3, 4), INTEGRATION.md "The path of a key" with the new wait, and
   this file's status.
6. Later, each on its own: the top row's probe; polyphonic aftertouch out; the velocity on the chord screen; Velo's
   calibration once its open item is understood; the 5 kHz scan with the two-conversion tick.

The study stops here; nothing of 1 .. 6 is begun.
