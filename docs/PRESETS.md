# Presets: one pool per engine (the user's model, 2026-10-07)

**Status: done in the firmware 2026-10-07** (`cr_bank.c` the pools and the binding, `cr_ui.c` PRESETS / ALGORITHM /
OPT + PRESETS / the save dialog, settings v6, `tools/emu/scripts/cr_presets.txt`; docs/HANDOFF.md 3e). Decided on the
way: Overwrite never renames; Overwrite on a user preset keeps its binding; the bound record of a loud factory preset
keeps that preset's trim; a delete loads what is then at the deleted place (the one before at the end); the CZ-1's
INIT TONE is the pool's INIT (its pool: INIT + Casio's 64); an engine not played yet lands on its first preset.
The Sounds page and the CLI show the binding ("over FM6 02 FM BELL" / "added").

Replaces the sound model of PLAN.md §3 / §7 and docs/INTEGRATION.md §7 ("Sounds"): the curated 48-row ChoralRoot
bank on PRESETS followed by 32 user slots, with the engines' own factory presets reachable only inside the editor
(EDIT held, KNOB 1). The user found it confusing (three lists, a saved sound landing at position 49 with no sign of
it, no way to save over a factory sound). Mock-ups: `design/choralroot-fm1-preset-screens.png`
(`design/make_preset_mockups.py`).

## The model

- **A pool per engine.** Each engine's pool starts with **INIT** at position 00 (the engine's init sound, `cu_sound_init`; it cannot be overwritten: Overwrite on it is Save as new), then its factory presets (`engine_t.presets`: FM6 25 (F1..F24, PIANO),
  FM TONE 16 (engine 15, `eng_quad.c`, code name QUAD), VA 25, CZ-1 64, PHASE 6, LOFI 5, VOICE, TRIO 5, WHEEL 5, PHYS 9,
  NOISE 4: the picker's order since 0.14) followed by the presets the user added. ANALOG (engine 0, 12 presets) is retired
  since 0.14 (`FELUCCA_ANALOG` 0): no pool; a user record of ANALOG is an added preset of the VA's pool and plays there
  with its CUT / RES / DRIVE / MIX / DTN, filter envelope and ATK / REL on the VA's macros of the same name (`cr_ui.c`
  `cu_analog_va`; its binding to an ANALOG factory preset is dropped), another retired engine's record (SAMPLE, GRAIN,
  DRUM) the VA's init sound.
  Every entry is editable: **saving over a factory preset is allowed** and the factory one stays recoverable (reset).
- **PRESETS turns inside the current engine's pool only** (the chord part's engine; ALGORITHM the bass part's). The
  popup: the number big, the name, `FM6 · 05/26` (00 = INIT, 01.. the factory presets, then the user's), the stripe meter over the pool; an overwritten factory preset
  carries a small square mark after its name.
- **OPT + PRESETS changes the engine** outside the editor (a horizontal picker: the engine big, the pool size as the
  value); the part lands on the preset it last had in that pool (remembered per engine per part; the first preset
  before that). The editor's EDIT-held picker is unchanged (it is the same thing inside the editor).
- **SAVE tap**: a two-item picker, **Overwrite** (the current preset; saves at once, name kept) or **Save as new**
  (the pool's next free place, then the naming screen prefilled with the current name). The default is Overwrite when
  the current preset is a user one or an edited one, Save as new otherwise. OCT+ takes it, OCT- cancels. **SAVE held
  1 s in the dialog**: "reset to factory?" on an overwritten factory preset, "delete?" on a user-added one. SAVE held
  outside the dialog stays the loops' save / load / delete.
- The bass: the same model on ALGORITHM (the bass part's engine's pool; OPT + ALGORITHM stays the bass level; the
  bass engine changes through BASS held + EDIT's picker). ALGORITHM position 0 = OFF as today.
- The idea of "templates" is gone: what the editor's picker steps with KNOB 1 is the same pool PRESETS turns.

## Storage (no flash layout change)

- The 32 user slots (`upreset.c` records, the VA / FM6 / CZ-1 stores beside them) hold every user-saved preset.
  A record **binds** to its place in a pool through two of the record's pattern bytes, unused by ChoralRoot (no
  sequencer): `note[15] = 0xA6` (the binding marker) and `flags[15]` = the factory index + 1 it overwrites, or 0 for
  a preset added to the pool. A record without the marker (saved by older firmware, or restored) is a user-added
  preset of its engine. The record's engine is the pool. `up_valid` is unchanged; the JSON sound files carry the
  binding with the record (docs/SOUNDS.md: the clients show it: "overwrites FM6 02 FM BELL").
- The pool of engine E = its factory presets, each replaced by a bound record where one exists (the first bound slot
  wins; a duplicate binding is reported and the later one is treated as added), then the added records of E in slot
  order. The list is built on demand from the 32 records (cheap: 32 comparisons).
- "Reset to factory" deletes the bound record (the factory preset reappears). "Delete" deletes an added record.
- The curated bank (`cr_bank.c` `CB_CHORD` / `CB_BASS`): its **trims** survive as a lookup by (engine, factory
  preset) applied when that factory preset loads (the loud CZ-1 presets; ANALOG's STRINGS row went with ANALOG in 0.14); its PIANO hybrid (FM6 TINE EP
  through the macros) becomes a factory-shipped **bound record**? No: ship it as a real FM6 factory preset entry
  (`FM6_PRESETS` gains PIANO as F25 with its macro values), so the pool stays pure. The bank's row order, names and
  the 48-row list go away; the default sound at power-on is FM6 TINE EP (F1); the bass's, VA DEEP SUB (ANALOG SUB BASS until 0.14).
- Settings (`cr_settings.h`, add-a-field procedure of docs/SETTINGS.md): `chord_sound` becomes (engine, pool index)
  and the per-engine last preset per part (11 bytes x 2) joins the record (version 6); `bass_sound` the same.
- The emulator's test_persist scripts and the Sounds page / CLI (docs/SOUNDS.md) need their expectations updated
  (the record's two binding bytes; `--sounds` lists "FM6 02 (over FM BELL)" or "FM6 +1").

## What changes where (the firmware brief)

- `cr_ui.c`: the sound list (`cu_list_load`, `cb_count`, `cb_slot_at`, `cu_sound_popup`) rebuilt on the pool; PRESETS /
  ALGORITHM steps; OPT + PRESETS the engine picker (a momentary layer while OPT is held, horizontal picker, the part
  switches on release); the save dialog (a picker page before the naming page), SAVE held 1 s in it; the editor's
  KNOB 1 steps the same pool.
- `cr_bank.c`: the trims table by (engine, preset); the bank rows removed (the tests that name bank rows: update).
- `upreset.c` glue (`cr_bank.c`'s wrappers): the binding bytes written by the save, read by the pool builder.
- `eng_fm6.c`: PIANO as a factory preset.
- Settings record v6; `tests/cr_settings_test.c`.
- Tests: `tools/emu/scripts/cr_presets.txt` (PRESETS stays in the engine; OPT + PRESETS changes it; the popup texts;
  save-as-new lands at the next place and PRESETS shows it; overwrite keeps the number; reset brings the factory
  name back; the per-engine memory across an engine change), `test_persist.sh` (a saved preset after a relaunch),
  `cr_draw` fixtures from the preset sheet, `install_test.py` / `test_sounds.mjs` for the binding bytes.
- Docs: PLAN.md §3 (PRESETS / ALGORITHM / SAVE rows, §4 "Sound editing"), docs/INTEGRATION.md §7, docs/EDITOR.md §7-§8,
  docs/SOUNDS.md (the record's binding), the release notes.

## Also in this pass (the user, 2026-10-07)

- **The PERF layer's root keys play the chord** as outside the layer, so a mode change is heard at once; SELECT picks
  the mode (it already moves the picker; KNOB 1..4 stay the mode's parameters). The same for the **FX layer** (SELECT
  picks the effect, the keys play). KEY (tonic), LOOP (slots), BASS (preview) keep their key maps. The footers change
  accordingly ("SELECT: mode · OCT-: back · HOME: home"). The LEDs on the roots in PERF / FX go back to the chord's.
- The LOOP knob row's **Quantize glyph at "none"** must read as no grid (no pulses), not a dense one: pct 0 draws an
  empty baseline; the grid densities start at 1/4.
