# The device manager (web), plan 2026-10-10

**Status: planned.** A page on the site next to the installer: the FM-1's sounds and loops on the left, a library
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
