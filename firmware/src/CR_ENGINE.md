# cr_engine — the ChoralRoot chord / performance engine

`cr_engine.c` / `cr_engine.h` are a C99 port of the grid build's `d_cr_engine.lua`
(`../choralroot_fullsource`). The engine covers the chord tables, Key Mode quantization, voicing, the
Simple / Advanced / Free play styles, Extension Addition, bass, the note-ownership registry,
Sticky / Hold latch, chord pads, panic, and the strum / slop / arp / pattern / harp scheduler.

- Integer math only. No allocation. No libc: the engine includes only `stdint.h` and has its own
  zero / sort / string loops. Struct copies may still compile to `memcpy` / `memset`, which Felucca's
  `libc.c` provides.
- All state lives in one caller-owned `cr_t`: about 5.9 KB on a 64-bit host, a little less on a
  32-bit target. The only globals are `const` tables, so two instances can coexist (the tests run one
  next to another).
- Clean builds:
  - `cc -std=c99 -Wall -Wextra -Werror -pedantic`, also with `-Wconversion -Wsign-conversion`.
  - `clang -target arm-none-eabi` and `i386` with `-ffreestanding`.
  - gcc-13 with `-ffreestanding`.
- Portability: no VLAs, no designated initializers, no compound literals, and no 64-bit division
  (the code has no 64-bit math at all).
- Tests: `sh tests/run_cr_tests.sh` builds and runs `tests/cr_engine_test.c`.

## API

```c
cr_t cr;                       /* static; ~6 KB */
cr_out_t out = { note_on, note_off, all_off, user };
cr_init(&cr, &out);
cr_tick(&cr, now_ms);          /* from the audio block, every 2.9 ms */
cr_key(&cr, note, vel, 1);     /* a root key, already octave-shifted (D4..G5 + OCT) */
cr_mod(&cr, CR_MOD_MAJ, 1);    /* the chord block: DIM MIN MAJ SUS / 6 m7 M7 9 */
cr_voicing_step(&cr, +1);      /* KNOB 1 detent */
cr_panic(&cr);                 /* OCT- + OCT+ */
```

### Output callbacks

The callbacks receive `(stream, note, vel)`. The three streams are named after Orchid's:

| stream | Orchid name | default MIDI channel |
| --- | --- | --- |
| `CR_STREAM_MAIN` | performance | 1 |
| `CR_STREAM_BASS` | bass | 2 |
| `CR_STREAM_RAW` | raw chord | 3 |

The caller maps each stream to a Felucca part and/or a MIDI channel. `all_off` means CC 123 on that
stream's channel.

### Stream enables and channels

`cr_set_stream(s, on)` gates *new gestures* on a stream, as the Lua's `route_set` did. A sounding owner
always finishes, so toggling a stream never strands a note. Defaults: main on, bass on, raw off.

MIDI channel numbers are the caller's concern. If the caller changes a channel while notes sound, it
must send each note-off on the channel its note-on used: keep a `uint8_t ch[3][128]`, written at
note-on.

The registry is **per stream**. Two streams routed to the same channel are not refcounted against
each other.

### Root keys

`cr_key_ex(note, id, vel, down)` takes a press id 0..7 (the grid's velocity row). A second press on a
held root retriggers it at the new velocity, and the superseded press's release is ignored (design.md
§5.3). `cr_key` uses id 0, so a repeated note-on on an FM-1 key or a MIDI note retriggers the root, and
one note-off ends it.

### Settings

Each setting has its own setter. A setter applies the setting's live side effect the way the Lua did:

| setter | side effect |
| --- | --- |
| `cr_set_playstyle` | clears Free state |
| `cr_set_ext_addition` | — |
| `cr_set_secret` | — |
| `cr_set_key` | new gestures only |
| `cr_set_transpose` | new gestures only |
| `cr_set_single_notes(mode, split_pc)` | new gestures only |
| `cr_set_bass` | next gesture |
| `cr_set_bass_mode` | next gesture |
| `cr_set_perform` | restart sounding voices + latch flush |
| `cr_set_perform_mode` | restart sounding voices + latch flush |
| `cr_set_param(mode, CR_P_*, value)` | rephase / rebuild / restart (see the timing model) |
| `cr_set_sticky` | latch flush |
| `cr_set_tempo` | rephase clocked voices only |
| `cr_set_stream` | next gesture |

`cr_get_param` reads a parameter back. `cr_latching` tells whether Sticky or arp Hold is in force (the
LATCH button LED).

### Chord pads

Eight chord pads (design.md §10.7): `cr_pad_arm`, `cr_pad_down` / `cr_pad_up`, `cr_pad_stop_all`, and
`cr_pad_get` / `cr_pad_set` for persistence. A pad stores the resolved chord, untransposed. It replays
through the current voicing, perform mode, bass and transpose.

### Queries for the screen and LEDs

- **`cr_chord_info`**: the shown chord. That is the most recently played, transformed or retriggered
  voice; after it is released, the most recent voice still sounding; after everything is released, the
  last chord (with `sounding = 0`). It returns:
  - the root pitch class (Key Mode resolved, transposed);
  - the quality id (`CR_Q_*`), the extension mask, and a `secret` flag;
  - the voiced notes;
  - the Orchid name parts `root` / `qual` / `sup` (see Naming).
- **`cr_scale_mask`**: 12 bits, absolute pitch classes. 0 when Key Mode is off.
- **`cr_mods_held` / `cr_mods_active`**: modifiers held, or latched in a held voice (Advanced / Free).
- **`cr_perform_pos`**: the shown chord's index of the last performed note. Upper-octave notes fold
  onto their chord tone. This is the hop for the arp view.
- **`cr_sounding`, `cr_voices`, `cr_pending`**: diagnostics.
- **`cr_note_name`**: MIDI 60 = C4.
- **`cr_pattern_name`**: the pattern names.

### Threading

The engine is **not reentrant**. Felucca runs the scheduler in the audio ISR (PLAN §7), so every `cr_*`
call must come from that one context. Push key, modifier, knob and setting events from the main loop
into a small FIFO, and drain it at the top of the block handler before `cr_tick`; otherwise mask the
audio IRQ around each call. The output callbacks must not call back into the engine. They should
enqueue into the voice allocator or the MIDI-out FIFO.

## Timing model

**Time.** Time is the caller's `uint32_t` millisecond clock, given to `cr_tick`. Every comparison is a
signed difference, so the clock may wrap through 2^32: one test suite runs across the wrap. API calls
between ticks act at the last tick's time, as the Lua's `now` did.

**Scheduler.** The scheduler is a pooled, sorted queue of `CR_MAX_EV` = 128 events:

- Each event is `{due, kind, owner, voice, note}`, with named dispatch: `NOTE_OFF`, `STRUM`, `CLOCK`.
  This is the Lua's `sched(delay, fn, owner, ...)` without closures.
- Order is ascending by due time. Equal dues keep insertion order.
- `cr_tick` drains every event due at `now` in order. Strum or harp rates faster than the tick
  therefore collapse into ordered same-tick bursts (design.md §26.3). Tested at 1 ms, 2.9 ms and 5 ms
  ticks.
- Every scheduled delay is at least 1 ms, so an event added during a drain never joins it (the Lua's
  re-entrancy rule).
- Cancelling is per owner. Each voice has a strum / note-off owner and a clock owner, so rephasing
  keeps the note-offs of notes already sounding.
- A full queue refuses the event and counts it in `ev_overflow`. A note is started only if its
  note-off can be queued, so overflow can drop notes but never strand one.

**Finite modes (Strum, Slop, Harp).** The rate is a 120-BPM reference in ms, scaled `× 120 / BPM`
(design.md §11 target). The Harp gate is `rate × gate %`. Slop jitter is uniform in
±`rate × amount %` (integer xorshift; `cr_seed`).

**Clocked modes (Arp, Pattern).** Divisions are kept in 1/24-beat units (2/1 = 192 … 1/32T = 2), so
triplets are exact. Step k of a voice lands at `anchor + pos(k)`, where:

- `pos(k) = ((k/2)·200 + odd·2·swing) · div` in 1/2400-beat units;
- 1/2400 beat = 25 / BPM ms;
- the anchor carries a remainder in 1/BPM ms.

Each completed swing pair is folded into the anchor exactly. Every step is therefore rounded once, from
its exact time, and never accumulates drift: the tests check 25 steps in 6 s, 1/8T, and 70 steps at
70 BPM. Gate is `div × gate %`.

**Retrig.**

- On: the anchor is the trigger.
- Off: the anchor is the global grid (`cr_clock_reset` / `cr_clock_epoch`; reset on tempo change and
  on the first tick). The first step waits for the next grid point. A new chord, or a same-root
  retrigger, substitutes into the running sequence.
- The global anchor is moved forward in whole 16-beat blocks. 16 beats is an exact number of pairs of
  every division, so the grid stays the same, and the arithmetic stays 32-bit forever. A late tick
  fires the missed steps in order (at most 64 per call; then one per tick).

**Live edits** (design.md §17, as `on_param_changed`):

| edit | effect |
| --- | --- |
| div / swing / retrig | rephase in place, keeping the melodic position (retrig on: a step fires now, as in the Lua) |
| dir / range | rebuild the sequence |
| gate / pattern / rotate | read at the next step |
| any Strum / Slop / Harp parameter | restart the finite gesture from the held chord |
| hold | flush the latch |
| tempo | grid reset, then rephase clocked voices; a held Strum / Slop / Harp is **not** restarted (deviation from design.md §20, see below) |

## Data tables and provenance

| Table | Content | Provenance |
| --- | --- | --- |
| `CR_QUALITY` | dim 0 3 6, min 0 3 7, maj 0 4 7, sus 0 5 7, aug 0 4 8, pow 0 7 12, min4 0 3 5 (packed: low nibble = count) | **Firmware ground truth** (fw 3.63 → 3.84 diff; ORCHID_FIRMWARE_REFERENCE.md §6.1/§7). |
| `CR_EXT_IV` | 6 +9, m7 +10, M7 +11, 9 +14 | Firmware, high confidence (§6.2). |
| Secret combo map (`crx_secret_quality`) | DIM+SUS → pow, MAJ+SUS → aug, MIN+SUS → min4; other pairs → last pressed. Scope Off / Simple / All. | Types firmware-confirmed. **Combo map is a Choralroot choice** (provisional; min+sus → min4 inferred). |
| `CR_KEYMAP` | One byte per pitch class from the tonic: low 3 bits = quality (1 dim, 2 min, 3 maj, 4 sus), bits 3–4 = root shift (8 down, 16 up). Major `3 12 2 12 2 3 20 3 12 2 12 1`, minor `2 12 1 3 12 2 12 2 3 20 3 12`. | Diatonic triads: theory. **C# in C → Csus is measured** (manual §9.3). The other chromatic entries are a **labelled fallback** (lower neighbour as sus, upward where its sus4 leaves the scale). A held Chord Type overrides the quality and keeps the shift. |
| `CR_PATTERNS` | 12 packed phrases: 13 offset bytes, then one byte per step (low nibble = chord index, 0 = rest; high nibble L = accent, vel × (L+1) / 16). Names: pulse, alberti, waltz, cascade, updown, syncopate, gallop, skip, ballad, stride, drive, ladder. | **Choralroot-designed**, ported byte for byte. Orchid's factory patterns are unpublished, so none is factory parity until measured. |
| `CR_DIV_UNITS` | 2/1 … 1/32T in 1/24 beat | design.md §12.3. |
| `CR_PAR_DEFAULT` | Strum 40 ms Up ×1 · Slop 40 ms Up ×1 30 % · Arp 1/8 Up ×1 70 % swing 50 Retrig Hold · Pattern 1/8 #1 ×1 70 % rot 0 swing 50 Retrig · Harp 8 ms Up ×3 100 % | design.md §21 (Arp Hold default on, §12.3). |
| Names (`CR_QUAL_NAMES`, `cr_chord_name`) | See Naming | Orchid manual §6 + firmware label table (§6.4). |

### Fixed constants

- Voicing clamp: −12…+12. A rotation step that would leave MIDI 0–127 stops.
- Bass register: C2 + octave offset −2…+4.
- Tempo: 20–300 BPM.
- Transpose: ±24.

### Naming (Orchid Standard Chord Naming Framework)

`root` uses sharps: `C`, `C#` … `B`.

`qual` is drawn at root size:

| quality | `qual` |
| --- | --- |
| maj | `""` |
| min | `m` |
| dim | `dim` |
| sus | `sus` |
| aug (secret) | `+` |
| pow (secret) | `""` (sup starts with `5`) |
| min4 (secret) | `m` (sup starts with `add4`) |

`sup` is the superscript, with tokens separated by spaces (`CR_SUP_MAX` = 12 bytes):

- The power chord's sup starts with `5` (the mock-ups' red superscript 5). Min+Sus starts with `add4`
  (the manual's secret-chord table prints "Cm add4"; the firmware label renders `ma`+glyphs+`4`).
  Extensions follow: `5 M7`, `add4 6`, `add4 JAZZ`.
- 7ths come first, then 6, then 9: `M7 9`, `7 6`, `6 9`.
- A minor 7th prints `7`: the dominant anomaly C⁷, and also Cm⁷ and Cdim⁷, as in the manual's table.
  On sus it prints `m7` (Csusᵐ⁷).
- 3 extensions print `JAZZ`; 4 print `WTF`.
- A single note (no quality) has empty `qual` and `sup`.

Examples: `C`, `Cm`, `Cdim`, `Csus`, `C+`, C`5`, Cm`add4`, C`7`, C`M7`, Cm`7 9`, Cm`JAZZ`, C`WTF`.
`secret = 1` marks the three secret types so the renderer can colour them.

## Deliberate differences from the Lua, and target behaviour adopted

Each item below is allowed by the Lua tests (none pins the old behaviour). Items 1–4 are design.md
targets (item 1 only partly); the rest are porting choices.

1. **Strum, Slop and Harp follow the BPM** (design.md §11 target; the Lua used absolute ms). At
   120 BPM the result is identical. **Deliberate deviation from design.md §20:** a tempo change does
   *not* restart a held finite gesture. BPM lives on an encoder, and every detent would re-strum. The
   gesture keeps its already-scheduled events; the new rate applies to the next gesture. Only Arp and
   Pattern rephase on a tempo change.
2. **Millisecond resolution** instead of 5 ms ticks. The Lua's `ticks()` rounded Harp's 8 ms to 10 ms;
   here 8 ms is 8 ms.
3. **Bass Behaviour is implemented** (design.md §9.1; the Lua always played bass on the resolved root).
   This is a Choralroot interpretation of the manual's one-line descriptions, parity unverified. The
   behaviours apply only while Bass is on:
   - **Chords Only** (default, §21): bass only when the gesture is a chord. A single note has no bass,
     where the Lua gave it one; no Lua test plays bass under a single note. An Advanced transform from
     a single note to a chord starts the bass.
   - **Unison**: bass at the played key's own pitch (the literal key, not Key Mode's resolved root),
     shifted by the bass-voicing octaves.
   - **Single Notes**: bass on every gesture; main and raw only for chords. A transform to a chord
     starts them.
   - **Solo**: bass only.
4. **Global transpose** (design.md §10.6 target, absent in the Lua):
   - Key Mode resolves the *pressed* key, then the transpose applies. It is added to the root before
     chord construction, which is equivalent to "after voicing" except at the MIDI range edges.
   - Each voice captures the transpose at note-on, so a held chord re-voices and releases its own
     notes.
   - The bass gets the same offset once.
   - Pads store untransposed roots.
5. **A duplicate down of an already-held modifier is ignored.** The Lua test pressed MAJ twice without
   a release; the port releases it first.
6. **Chord-pad voices follow** voicing steps, Perform toggles and tempo rephases. The Lua iterated only
   keyed voices. They are still excluded from latch, Free and modifier transforms, as in the Lua.
7. **Pool limits**: 16 keyed voices (`CR_MAX_VOICES`, overridable) plus 8 pad voices, and 128 events.
   A root press beyond the pool is ignored; it is not stolen.
8. **Owner capacities**: 32 notes per performance owner, enough for 7 notes × 4 octaves.

## Not ported (with reason)

- **The looper** (`d_cr_loop.lua`, design.md §14) is separate work (PLAN `cr_loop.c`). The Lua's
  `loop_rec_open` / `loop_rec_close` hooks have no equivalent yet. Add a voice open / close callback
  when `cr_loop.c` lands. `cr_chord_base` and `cr_voice_apply` are exported for its playback.
- **MIDI clock output (0xF8)**: Felucca's `midi_clock.c` sends it. The engine exposes `cr_clock_reset`
  so loop or transport start can align the arp grid.
- **The keyboard view, KEY-hold tonic selection, parameter editors, LEDs and the log**: grid UI. On the
  FM-1 these become `cr_ui` / layers; the engine only takes the resulting settings.
- **Velocity Sense**: the caller passes the velocity.
- **Persistence**: settings are plain fields and setters, and pads have `cr_pad_get` / `cr_pad_set`.
  The record format belongs to `cr_settings.c`.

## Single Notes (design.md §8.3)

`cr_set_single_notes(c, CR_SINGLE_FULL | CR_SINGLE_SPLIT, split_pc)`. Full Octave (default) plays every key at
its pitch. Split: a gesture that resolves to a **single note** (no chord quality) whose *pressed* key has a pitch
class below `split_pc` (0..11, C = 0; default 5 = F) sounds one octave lower. Chords are unaffected, and so are the
bass (its register is its own) and pads. An Advanced transform of a lowered single note into a chord rebuilds from
the pressed key, so the chord sounds at the key's own octave. On the FM-1's 18 root keys (D4..G5) the split repeats
per octave (it is a pitch-class split, as Orchid's one-octave keyboard). Not in the Lua; tested in `s_single`.

## Panic

`cr_panic` clears the queue and sends a note-off for every registry note. It then calls `all_off` on
**all three streams, unconditionally** (PLAN §4); the caller (`cr_out.c`) decides what a disabled stream
does with it. This replaces the Lua's main + bass (+ raw if enabled), so the ported test now expects three.
Panic then drops every voice and pad, the pad recording and the Free memory. Settings,
held modifiers and the physical-key state are kept, so a still-held key's later release is a no-op.

## Tests

`tests/cr_engine_test.c` ports the engine cases of `dev/test_choralroot.lua`:

- chords and stacked extensions (C69 / CJazz / CWTF);
- Simple capture and last-quality-wins;
- the shared-note refcount;
- panic;
- the no-stuck-notes mash;
- voicing and its clamps;
- bass, its voicing and supersede;
- the whole Key Mode table, including C# → Csus and both scales;
- chord pads;
- Secret Chords and their scope;
- Advanced (re-strike, toggles, LED latch, Play Chord / Add Note);
- every row of the Free transition table;
- strum / slop / harp: order, spacing, cancel, supersede, range, sub-tick bursts;
- arp: steps, drift, swing, orders, retrig-off grid and substitution, gate > 100 %, Hold latch;
- pattern: accents, rests, rotate, live pattern change;
- live division and tempo rephasing;
- the raw stream;
- torture interleavings.

It also covers what the Lua lacked: the bass behaviours, transpose, BPM scaling, naming, the queries,
the pool limits, two instances, and a 4000-gesture random run.

The time-driven suite runs four times: 1 ms ticks, 2.9 ms ticks, 5 ms ticks, and 2.9 ms across the
2^32 ms wrap. Every section also asserts no stuck note, no double note-on and no stray note-off. That
is 483 distinct checks and 1683 check lines; the suite is clean under ASan / UBSan.

Not ported from the Lua suite (grid-only): LED frames, the hex bytecode loader, tab / overlay gestures,
parameter-editor locks, the looper, flash persistence and the parameter log.
