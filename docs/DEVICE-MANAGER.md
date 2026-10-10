# The device manager (web), plan 2026-10-10

**Status: built 2026-10-10** (host-verified: `web/test_manager.mjs` against the simulated FM-1, the page under
headless Chromium with `?sim=1`; not yet run against a device). See "As built" at the end. Planned as follows. A page on the site next to the installer: the FM-1's sounds and loops on the left, a library
on the right, drag or Place between them, audition from the browser. The model is Baud Girl's FM-1+VA device
manager (https://baudgirl.com/work/FM-1+VA/device-manager: Your FM-1 | Library, Place / Keep, Rename, Swap with,
Save as a file, Back up everything, Install a backup, Undo, an on-screen keyboard), fitted to ChoralRoot's model
of presets per engine (docs/PRESETS.md) and of loops in ten slots (docs/LOOPER.md).

## What ChoralRoot already has that the page builds on

- The backup protocol over USB MIDI SysEx (web/fm1backup.js, docs/SOUNDS.md): every stored object by id: the
  settings (1), the 32 user sound records (6), the patch stores of VA (7), FM6 (9..13), CZ-1 (14..21), FM TONE
  (22), the loop slots (40..49); GET / PUT per object, no restart for sounds; the FM-1 answers busy while a loop
  plays (RC 3).
- web/fm1sounds.js: a sound = its record + its engine's patch; export / import / rename / delete per slot as
  `choralroot-sound` JSON; the pool binding of each record ("overwrites FM6 02 FM BELL" / "added"); the factory
  preset names per engine (`FACTORY_PRESETS`, pinned to the firmware by `sound_templates --check`); .syx for FM6
  (DX7) and CZ-1 tones.
- The FM-1 plays MIDI notes in on its channels (CHORD 1, BASS 2 by default; docs/INTEGRATION.md) and sends its
  audio over USB (ChoralRoot In), so the browser can audition a placed sound through Web MIDI and the user hears it
  on the unit.
- SysEx 73 DEBUG / 74 SCREEN (the device's counters; the screen off for tests).

## The page: `manager.html` on the site (self-contained like the installer, made by web/make_site.py)

Two panes under a header with the connection state (Web MIDI, "ChoralRoot FM-1"; Chrome / Edge; the installer's
"Device not found?" text reused), the firmware version read from the device, and the Sounds / Loops tabs.

**Left: Your FM-1.** Tabs per engine in the picker's order (FM6, FM TONE, VA, CZ-1, PHASE, LOFI, VOICE, TRIO,
WHEEL, PHYS, NOISE) and a **Loops** tab. An engine tab lists its pool exactly as PRESETS turns through it on the
unit: 00 INIT, then the factory presets (grey, with a mark when a user record overwrites one), then the user's
added sounds; each row: the position, the name, the engine's patch kind, a slot badge (U01..U32) when it is a user
record. Selecting a row auditions it (see below). The Loops tab: ten rows, "holds a loop" / "empty", the length
and layer count read from the record (`cr_loop_unpack`'s header: len ticks, nlayers, nev).

**Right: Library.** Sources in a dropdown: **This browser** (sounds the user saved as files or kept here:
IndexedDB), **a folder / files** (choralroot-sound JSON, .syx DX7 single or bank, Casio tone dumps, loop files,
whole backups), and **packs** served from the site (`web/packs/*.json`: a pack = a name, an author, a licence, a
list of sounds in the choralroot-sound format; the first packs: the factory sounds of each engine as files, and
Melodee's CZ-1 banks). Rows as on the left.

**Between them: Place and Keep.** Place puts the library's selected sound into the FM-1: into the selected
engine's pool as a new added sound (the next free user slot), or over a factory preset / an existing user sound
when one is selected on the left (the binding is set the way SAVE > Overwrite does: `note[15]` 0xA6, `flags[15]`
the factory index + 1). A sound of another engine goes to its own engine's pool. Keep copies the FM-1's selected
sound into This browser. Place all N for a pack. Loops: Place a loop file into a slot, Keep a slot as a file.

**Bottom bar:** Rename (12 characters, the unit's rule), Swap with… (two user slots, or a user sound with a
factory position: the bindings swap), Delete (a user sound; a factory preset "resets to factory"), Save as a file,
Back up everything (the installer's backup), Install a backup, **Undo** (the page keeps the object set before
every write; Undo writes the previous set back), and the on-screen keyboard: two octaves, the mouse plays, the
computer keyboard as the emulator's Ableton layout (a..k, w e t y u o p), with a chord-quality row (the FM-1's own
black-key modifiers are not sent; the page sends plain notes on channel 1, so what you hear is the chord sound
as a MIDI instrument: say so on the page).

**Writes** go through `writeSounds` (only the changed objects, no restart); a loop write while the loop plays is
refused by the unit ("stop playback first": shown). Every write is followed by a read-back, and the page shows
the unit's state, never its own guess.

## Not in the first version

Editing parameters (that is the editor page), the FM-1's screen on the page, sharing packs from the page
(packs are files in the repo), loop names (the firmware has none: slots are numbered).

## Build

`web/manager.html` + `web/fm1manager.js` (the pane model, the library store, undo), reusing fm1backup.js and
fm1sounds.js unchanged where possible (additions there: swap, place-over with the binding, the loop header
decode); `web/test_manager.mjs` against the simulated FM-1 of web/test_installer.mjs; `make_site.py` emits
`manager.html` and links it from the landing page and the installer; `web/packs/` with the generator
`tools/make_packs.py` (the factory sounds of every engine from `sound_templates`); docs: this file, README,
docs/SOUNDS.md.

## As built (2026-10-10)

- Files: `web/manager.html` (the page), `web/fm1manager.js` (the model: `managerPools` / `managerLoops`, `managerPlace`,
  `managerPlaceAll`, `managerSwap`, `managerRename`, `managerDelete`, `managerKeep`, the loop operations, `ManagerSession`
  with undo, `LibraryStore`, the file / pack readers, the audition messages, `makeSimFM1`), additions in
  `web/fm1sounds.js` (`firstFreeSlot`, `withBinding`, `placeSound`, `bindSound`, `swapSounds`) and `web/fm1backup.js`
  (`loopHeader`); `web/test_manager.mjs` (in `tests/run_tests.sh`); `web/make_site.py` writes
  `webapp/installer/manager.html` (the three modules and the packs inlined) and `webapp/installer/packs/`, linked from
  the landing page and the installer's header.
- **Simulator:** `manager.html?sim=1` runs against `makeSimFM1()` (the backup side of `web/test_installer.mjs`, with six
  user sounds (FM6 02 and PHASE 01 and CZ-1 13 overwritten, VA / FM6 / CZ-1 added) and loops in slots 1 and 4); writes
  change it until the page reloads; it answers busy while `sim.playing`. The keyboard and the audition show what they
  would send.
- **Connection:** Web MIDI with SysEx, the in and out ports whose name contains "ChoralRoot FM-1"; `onstatechange`
  re-attaches on hot-plug; INFO must answer ChoralRoot. Nothing is enabled until the objects are read.
- **Writes:** `ManagerSession.apply`: the changed objects through `writeSounds` (PUT, no restart), the set before kept
  (20 steps), then a full read-back; busy (rc 3) is not retried: "Stop the loop on the FM-1 first", no undo step. Undo
  writes the previous set's changed objects back and reads again. Install a backup ends with RESTART (it restores the
  settings).
- **Audition, and what the protocol lacks:** ChoralRoot answers only the backup subset of the editor protocol
  (EDITOR_PROTOCOL.md "ChoralRoot: backup and restore"): `PRESET` (8) and `UP_LOAD` (20) get no reply and do nothing,
  for factory presets and user records alike. What the firmware does take over MIDI is a **program change**: on the
  chord channel the position in the chord part's *current* engine's pool (0 INIT), on the bass channel 0 OFF then the
  position + 1 (`cr_ui.c cu_midi_poll`). So selecting a row sends `C0 pos` (with Bass on: `C1 pos+1`); it plays that
  row only when the FM-1 is on the row's engine, and the page says so. Missing for a full audition: an engine select
  over MIDI (e.g. a bank select CC 0 = engine, or a SysEx "load engine e, position p"), which would make every row of
  every pool reachable; a sound that is only in the library (not on the FM-1) cannot be heard before it is placed.
- **The keyboard:** two octaves, mouse or A W S E D F T G Y H U J K O L P (Z / X octave), note on / off velocity 100 on
  channel 1, channel 2 with Bass; plain notes (the FM-1's chord-quality keys are not sent).
- **Packs:** `tools/make_packs.py` writes `web/packs/factory-<engine>.json` and `index.json` from
  `./build/host/sound_templates --packs` (new in `tests/sound_templates.c`: every factory preset loaded on a part as
  `cu_load` does, stored as `cb_store` would, bound to its own position, with the engine's patch from `blob_preset` /
  `blob_get`). VA, FM6, CZ-1 and FM TONE carry their patches; PHASE, LOFI, VOICE, TRIO, WHEEL, PHYS, NOISE are records
  (their sound is the record's values). Melodee's CZ-1 banks are not a pack yet.
- **Library:** This browser (IndexedDB "choralroot-manager"; in memory when IndexedDB is unavailable), files (sound
  files, .syx DX7 / Casio, packs, whole backups, loop files), the packs. Loop files: `choralroot-loop` version 1
  (`data` = the slot's record in base64, with `sig`, `len`, `ppqn`, `bars`, `nlayers`, `nev`).
- Not yet: drag and drop between the panes (Place / Keep buttons only), Melodee's banks as packs, the device run.
