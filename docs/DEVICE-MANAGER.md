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

**Left: Your FM-1.** Tabs per engine in the picker's order (FM6, FM TONE, VA, CZ-1, WHEEL: five since 0.18; FM6, FM
TONE, VA, CZ-1, P5, WHEEL on a 0.22 FM-1 since 3y80, [below](#the-p5-tab-2026-10-15-history-3y80)) and
a **Loops** tab. A sound saved on a retired engine (PHASE, LOFI, VOICE, TRIO, NOISE, PHYS since 0.18; ANALOG) is a row of
the VA's tab, where the FM-1 plays its stand-in, marked `PHASE · retired` in place of the patch kind; Keep gives the
sound back as it was saved. An engine tab lists its pool exactly as PRESETS turns through it on the
unit: 00 INIT, then the factory presets (grey, with a mark when a user record overwrites one), then the user's
added sounds; each row: the position, the name, the engine's patch kind, a slot badge (U01..U32) when it is a user
record. Selecting a row auditions it (see below). The Loops tab: ten rows, "holds a loop" / "empty", the length
and layer count read from the record (`cr_loop_unpack`'s header: len ticks, nlayers, nev).

**Right: Library.** Sources in a dropdown: **This browser** (sounds the user saved as files or kept here:
IndexedDB), **a folder / files** (choralroot-sound JSON, .syx DX7 single or bank, Casio tone dumps, Digitone .dnsnd (docs/SOUNDS.md), loop files,
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
  user sounds (FM6 02 and WHEEL 01 and CZ-1 13 overwritten, VA / FM6 / CZ-1 added) and loops in slots 1 and 4); writes
  change it until the page reloads; it answers busy while `sim.playing`. The keyboard and the audition show what they
  would send.
- **Connection:** Web MIDI with SysEx, the in and out ports whose name is an FM-1's (`fm1manager.js MANAGER_PORT_RE`:
  "ChoralRoot FM-1", or the "FM-1" a Mac keeps for a device it first saw with another firmware, GitHub issue 32; the
  INFO answer then decides: a port that does not answer as ChoralRoot is refused); `onstatechange`
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
  `blob_get`). VA, FM6, CZ-1 and FM TONE carry their patches; WHEEL's are records
  (the sound is the record's values). Melodee's CZ-1 banks are not a pack yet.
- **Library:** This browser (IndexedDB "choralroot-manager"; in memory when IndexedDB is unavailable), files (sound
  files, .syx DX7 / Casio, packs, whole backups, loop files), the packs. Loop files: `choralroot-loop` version 1
  (`data` = the slot's record in base64, with `sig`, `len`, `ppqn`, `bars`, `nlayers`, `nev`).
- **Revision 2026-10-10 (after the first device try):** the Library is on the LEFT and the FM-1 on the RIGHT (Place →,
  ← Keep). Multi-select on both sides (click, shift-click a range, cmd / ctrl-click, Select all). **Rename** on both
  sides (12 characters): a This browser entry in place, a loaded file in the page, a site pack's row as an editable
  copy in This browser (`renameSoundFile`, `LibraryStore.put`). **Packs of one's own:** Save selected as a pack and Save
  all user sounds write ONE `choralroot-pack` file (`makePack` / `userSoundsPack`: name, author, created, sounds);
  Add files shows a pack as its own source ("File pack: …") and Keep all in this browser keeps a whole source.
  **Factory rows** can be kept, saved and multi-selected: their file is the factory pack's entry with `factory: true`,
  `engine`, `position` (`managerKeep` with the packs, `factorySoundFile`); a kept overwrite carries `position` too. Place
  with nothing selected puts a factory file back over its own position, and Place all puts files with a `position`
  back over that position when it is still factory. **Drag and drop:** within a pool `managerDrop` (= Swap: two user
  sounds exchange slots, so two added sounds exchange their order; a user sound onto a factory row binds there and
  frees its old place; a factory row onto a user row the same); library → FM-1 = Place (onto a row: over it), FM-1 →
  library = Keep; Loops: drag to swap slots. **Capacity:** "N of 32 user slots used" in the FM-1 header (with the
  firmware version), an "empty · Place here" row at the end of each pool while a slot is free, Place greys out as "no
  free slot" when all 32 are used (unless it overwrites a user sound), Place all says how many did not fit. `?sim=1`
  starts with 30 user sounds (`simObjects(30)`).
- **Audition by SELECT (2026-10-10, second device try):** selecting a FM-1 row sends SysEx 76 SELECT (part 0 chord /
  1 with Bass, engine, pool position; rc 0 ok, 1 bad engine, 2 bad position, 3 busy; firmware by another agent), and the
  status says what plays ("FM6 · 05 FM PAD on the unit"). After a Place / Rename / Swap or drag / Delete the row now at
  the affected place (by its slot, else its position) is selected again, so a patch placed into the playing slot is
  heard at once. No answer (an older firmware): the program change of before, with its limitation said. Model:
  `selectRequest`, `selectOnUnit`, `playingText`; the simulators (fm1manager.js `makeSimFM1`, test_installer.mjs's
  backup side) answer 76. Row numbers are the unit's: 00 INIT, then 01...
- Not yet: Melodee's banks as packs, the device run of this revision.

## The installer beside it (as built, 2026-10-12)

The manager's neighbour, `webapp/installer/index.html` (`web/index_pkg.html`, made self-contained by
`web/make_site.py`), is one page with the Install button at the top, as it was up to 0.19. (A flow of six steps
replaced it for a few hours on 2026-10-12 and was never published; the owner wanted the immediacy back, with the
notes and the older versions within reach.) From the top:

- the lead and the charge / cable warning; a notice only when the browser has no Web MIDI;
- "ChoralRoot X.Y · date" and the button, **Install ChoralRoot X.Y**: the label is always the version that will be
  written. Under it the progress bar and the status line;
- **Other versions**: one button for each older version of `web/releases.json` with its date. One click chooses it:
  the label and the notes change, the package is fetched and checked, and a yellow note above the button gives the
  version's going-back sentence with "Back to X.Y, the newest". When the unit is known to run something older than
  the chosen version the note only says that the version is not the newest;
- **What is on my FM-1?**: the handshake and INFO, read only (`classifyFirmware`: ChoralRoot and its version,
  stock, Felucca, Melodee, a refused firmware, update mode). The version found is marked "On your FM-1" in the
  list. After an install the same button is the read-back: it says whether the unit runs the version just written.
  Install does not need it: it identifies the unit itself, as before;
- the backup tick box, and the override box after a refusal;
- beside all this (under it on a phone) the notes of the chosen version: "New in X.Y" with the bullets, or "What
  X.Y brought" with the going-back sentence and the points in full behind a disclosure; full notes, file, source.

Each chosen package is fetched from `firmware/` and checked (identity, then sha256 against the list) before Install
is enabled; when either fails Install stays disabled and the status line gives the reason. When an install stops
after it began, a red box under the status says what to do ("The install stopped. The FM-1 is not lost.").

Below: what an install keeps and the other small print, "Device not found?" (the rescue key, the 0.18 gesture, SAFE
MODE, the WL82 disk, the Transporter), Back up and restore, Sounds, Return to official V15, and "The files" (every
version's package, sha256, notes and source at its tag).

- **The versions** are `web/releases.json` (newest first; `tests/releases_test.py`). `make_site.py` publishes the
  current package and, from `--packages DIR` (`CR_PACKAGES_DIR`, else `build/scratch/packages`), every other listed
  version whose file is there, each refused unless its sha256 is the listed one and its identity the current
  package's; a missing one is a "skipped:" line locally and an error with `--require-all` (the Pages job).
  `firmware/` also gets `index.html` (the files, their checksums, the source of each at its tag) and `SHA256SUMS`.
  0.15 and older are not listed: no boot stub, and the idle fault of 0.14 / 0.15.
- **Why the site hosts them:** a release asset's download redirects to `release-assets.githubusercontent.com`, and
  neither answer carries `Access-Control-Allow-Origin` (checked 2026-10-12 with `curl -sI -L -H "Origin: ..."`), so
  a page cannot `fetch` it. The page's only network request is the same-origin fetch of the chosen package.
- **Licences:** `firmware/LICENSE`, `LICENSING.md` and `LICENSES/` are the tree's own at the ref the site is built
  from. The licence texts are the same files since v0.16 (`git diff v0.16 HEAD -- LICENSE LICENSES` is empty;
  LICENSING.md gained credits), so they cover every hosted package; each version's complete source is the
  repository at its tag, linked beside each file.
- **Demo mode:** `?demo=idle`, `older`, `writing`, `interrupted`, `done`, `nomidi` show a state with made-up device
  data. `midiAccess()` is the page's one way to the browser's MIDI and refuses in demo mode, every handler that
  talks to an FM-1 returns first, and no package is fetched: `web/test_installer.mjs` clicks every such button in
  each demo state and counts no MIDI request and no fetch.
- **Not run:** against an FM-1, in Chrome or Edge themselves, on Android. `web/test_installer.mjs` runs the page's
  script against a simulated FM-1 in node; the look was read from headless Chromium screenshots.

## Replace factory (DN-3, 2026-10-14)

- **Replace factory (N)** (beside Place / Place all): the library's selection, or every sound shown when nothing is
  selected (e.g. with "this engine only"), of the open engine's pool, over that engine's factory presets in pool order:
  item 1 at 01, item 2 at 02, ..; more items than factory presets: the rest are placed after them as added sounds
  until the free user slots run out; fewer: only the first N are replaced. Places already holding a user sound are
  skipped; sounds of other engines are left out. The confirm says it all, e.g. "16 factory FM TONE sounds will be
  replaced by the first 16 of 200 selected; 112 more go into free user slots; 72 will not fit".
- **Drag**: several library rows dropped onto a factory row: the first takes that row, the rest the factory rows after it.
- **What replaced means**: each import is a user record bound to the factory preset (the same as Place over one
  factory row): the pool shows it in that place and the factory preset is not in the pool while it is there (PRESETS
  steps through the imports where the factory sounds were). Kept in the pool's bank: survives a power cycle. The
  factory sounds stay in the firmware.
- **Restore factory order** (since 3y79 in the FM-1 pane's "Factory presets:" row, below): every user sound of the
  engine over a factory preset becomes an added sound (kept, after the factory presets); the factory presets are back.
  Needs no free slot. Delete / Reset to factory on one row brings back that one preset (and deletes the import);
  Restore selected / all (below) does that for several.
- **Favourites**: a factory preset's mark is keyed by the preset, and a record over it has the same key (PRESETS.md
  "Favourites"): the mark stays with the place, so the unit shows the import as marked; after Restore it is the factory
  sound's again. Replace and Restore factory order do not edit marks; the confirm says this. (A deleted user slot's own
  mark: below.)

## Several FM-1s (2026-10-14, HISTORY 3y66)

Every FM-1's port is "ChoralRoot FM-1"; ChoralRoot gives each unit an ID (8 hex digits, the CRC-32 of its flash's
unique ID; under Options > Version on the unit; docs/INSTALL-COMPAT.md "Several FM-1s"), which INFO now carries.

- **The header** shows the version and `unit ID A1B2C3D4` (a ChoralRoot from before the ID: the version alone).
- **Which FM-1:** the page lists the port pairs whose name is an FM-1's (`fm1manager.js managerPorts`, `MANAGER_PORT_RE`: two
  FM-1s may have ports of one name; the k-th input goes with the k-th output of that name). One, at the first
  connection: opened as before. Several: each is asked INFO and the page asks which one to open, by its number in the
  list or its unit ID (`pickUnit`); cancelled: nothing is opened ("choose an FM-1"; Connect asks again). The FM-1 opened
  before is taken again without a question when it comes back (a hot-plug, a restart); when it leaves and another is
  there, the page asks instead of moving to the other one by itself: a write never goes to an FM-1 that was not chosen.
- Tests: `web/test_manager.mjs` (managerPorts, pickUnit, deviceInfo's unitId from `makeSimFM1({ unitId })`).

## Restore factory presets, Delete all, Reset to factory (2026-10-15, HISTORY 3y79)

The owner: "a way to restore multiple factory presets" and "a way to quickly fully delete all user presets and reset
to factory".

- **The "Factory presets:" row** of the FM-1 pane (under the engine tabs; hidden on Loops). A replaced factory preset
  is shown as the user row over it ("MY TINE over FM BELL"), so these act on those rows:
  - **Restore N selected**: of the rows selected on the FM-1, the user sounds over a factory preset are deleted and
    each factory preset is back in its place, as Delete / Reset to factory does on one row. The other selected rows are
    left as they are (the dialog counts them).
  - **Restore all FM6 (N)**: every replaced preset of the open engine's pool; its added sounds stay.
  - **Restore all engines (N)**: of every engine's pool the manager shows (P5's too since 3y80).
  - **Restore factory order** (moved here from the middle column): the keeping one, the user sounds stay as added sounds.
  - Delete, not keep, because the single row's Reset to factory deletes, and Restore factory order already keeps. The
    dialog says both, e.g. "FM6 02 FM BELL comes back in its place. The user sound over it is deleted from the FM-1:
    U001 MY TINE. To keep it as an added sound after the factory presets instead, cancel and use Restore factory order."
  - A later slot whose binding names a preset that an earlier slot holds (an added sound so far: the first bound slot
    wins, `cr_bank.c cb_bound`) is unbound by a restore, else it would take the preset's place once the earlier one is
    gone. Restore factory order now does the same (until 3y79 such a slot stayed bound and took the place).
- **Delete all user sounds (N)…** and **Reset sounds to factory…** (the bottom bar, in red): every slot of every engine
  emptied, U001..U128 (P5's sounds and the retired engines' records too; a 0.21 FM-1: U01..U32). With no user record
  every pool is INIT and its factory presets in their order, so the two write the same: Delete's dialog counts the
  sounds and the factory presets that come back, Reset's says the sounds are as the FM-1 came (every factory preset of
  every engine in its place, in the factory order, nothing added). Reset is also offered when no sound is left but a
  user slot still carries a mark. Loops, settings, the FM6 voice bank, the CZ-1 banks and the favourites filter are not
  touched.
- **What is written.** Delete all / Reset: each pool bank holding a used slot (byte 0 of a slot 0xA5) once, as a PUT of
  length 0 (`cr_backup.c crb_commit`: the bank is written empty; a bank never written would be "nothing to write"), the
  banks already empty not at all: at most 13 writes and no data over USB; then object 23 once. A 0.21 FM-1: each of its
  objects (6, 7, the stores) once, as `deleteSound` empties a slot. Restore and the Delete button with several rows
  (`managerDeleteRows`) write each bank they touch once: the bank as read, the deleted slots 384 zeros (a bank left
  with no used slot goes as length 0). 0.21's FM TONE slots (patch header 16 / 80) are sent as read: 3y78's firmware
  takes them; the beta built before 3y78 refuses such a bank (rc 2), which a Restore or an Undo would then report.
- **Favourites** (object 23, now read with the sounds): a user slot's mark and type are the slot's (sections 0x80 /
  0x81), and the FM-1's own Delete takes them off (`cr_favs.c crf_slot_new`), else the next sound placed in that slot
  would show the mark. The manager now does the same for every slot it deletes (Delete, Restore), and Delete all / Reset
  take off every user slot's marks and types (the dialog counts them). A factory preset's mark is the preset's, and a
  record over it has that key: the mark stays and is the factory sound's again; P5's marks and the filter stay, byte
  for byte. An object the FM-1 cannot read (another version, cut short) is left as it is.
- **The dialog** (a `<dialog>`, not `confirm()`): what comes back, what is deleted (by name for a restore), the marks,
  what is not touched, Undo, and **Back up everything** inside it (the bottom bar's backup: the file is downloaded, the
  dialog stays open with its name). Cancel or Esc: nothing is written.
- **Undo**: one step, as every write (the page keeps the object set before it): the banks and object 23 written back
  byte for byte. Only while the page stays open (the dialog says so, and offers the backup for good). A write that
  stops part-way (a loop started: rc 3; a refusal) now keeps its step too, so Undo puts back what was written
  (`ManagerSession.apply`; `writeSounds`' error carries `.written`).
- **Progress**: the bar and the status per object ("writing the user sounds 011-020 (2 of 5)…"), then the read-back.
- **P5** (as built in 3y79): the manager's pools were the five engines of `FACTORY_PRESETS`; a P5 record showed as an
  added row of the VA's tab ("P5 · retired"), so a P5 sound over a P5 program was not restored by the Restore buttons
  (Delete all and Reset emptied its slot all the same). Since 3y80 P5 has its own tab, and the Restore buttons, Delete
  all and Reset treat it as every engine ([The P5 tab](#the-p5-tab-2026-10-15-history-3y80)).
- Model: `fm1manager.js` `managerDeleteRows`, `managerRestorePresets`, `replacedRows`, `replacedOf`, `managerDeleteAll`,
  `restorePresetsText`, `deleteAllText`, `favUserSlots`, `favsClearSlots`, `FAVS_ID`; `fm1sounds.js` `rawBindings`,
  `writeSounds`' `onObject`. `makeSimFM1` takes a pool bank as `cr_backup.c` does (`crb_bank_ok`, `crb_bank_fill`, a
  PUT of length 0) and logs every PUT begin (`sim.puts`). Tests: `web/test_manager.mjs` (the 3y79 block: a bank of
  0.21's FM TONE slots, the favourites, each bank once, the empty ones skipped, Undo, a write stopped part-way).

## The P5 tab (2026-10-15, HISTORY 3y80)

The owner: P5 is a current engine (engine 16, Melodee's Prophet-5 model: docs/PROPHET.md), not a retired one; until
3y80 its user sounds were added rows of the VA's tab marked "P5 · retired".

- **The tab**, after CZ-1 and before WHEEL as on the unit's picker, where the FM-1 has P5: a 0.22 FM-1 (the sound
  pool's objects). A 0.21 FM-1 has no P5: its five tabs as before, and `?tab=P5` opens FM6 (`managerEngines`).
- **The pool** as PRESETS turns it: 00 INIT, the shipped programs (01 ITS PROPHET5 .. 115, the unit's names), then the
  added P5 sounds. The names come from the firmware: `tests/sound_templates.c` is now built with P5, as the unit is,
  so `FACTORY_PRESETS["16"]` is INIT P5 and the list of `assets/prophet5-factory/shipped.txt`, `FACTORY_FIRST["16"]`
  is 1 (`cr_bank.c pool_f0`), and the new `FACTORY_IDS["16"]` holds each program's Sequential number (the
  favourites' key). `--check` pins the three lines in `web/fm1sounds.js` and `tools/fm1_install.py`, so a changed
  shipped list fails `tests/run_cr_tests.sh` until they are pasted again (docs/SOUNDS.md "The binding").
- **Over a program**: the firmware binds a P5 record to a factory program as any engine's (`cr_bank.c cb_bind_of`:
  `flags[15]` = the program's index + 1, from 1, INIT P5 being the pool's INIT, below `npresets`; nothing P5-specific;
  no firmware change). So a P5 user sound over a program is that program's row ("MY PROPHET over FOREVER KEYS"),
  and Place onto a program, Swap or drag onto one, Replace factory, Restore selected / all P5 / all engines, Restore
  factory order and Delete (Reset to factory) work on P5's rows as on every engine's. Rename, Delete, Undo the same.
- **Files**: Keep, Save as a file and Save all user sounds give a P5 user sound as the `choralroot-sound` file the
  installer has written since 0.22 (its whole program: patch kind `p5`, 138 bytes; a sound over a program carries
  its `position`). A P5 file in the library (files, a pack of one's own, This browser) is listed on the P5 tab ("this
  engine only") and goes into P5's pool. **A P5 factory program has no file**: the site offers no P5 pack (Sequential's
  programs, no licence granted: LICENSING.md, docs/PROPHET.md "Open decisions" 2; `tools/make_packs.py` skips engine
  16), so Keep / Save are off for those rows with the reason in their title, and Select all on P5 takes the user sounds
  (`keepable`, `P5_NO_PACK`). To keep a program as a file: save it on the FM-1 (SAVE > Save as new), then Keep that.
  A 0.21 FM-1 refuses a P5 sound, its record as well as its program ("ChoralRoot 0.22 or later needed": 0.21 reads a
  record of engine 16 as an empty slot, so the next save there would take it).
- **The favourites** are shown on every tab: a yellow star before the name where the FM-1 shows one, with C or B when
  the sound is typed Chord or Bass (`favMarks`, `favKeyOf`, `favOf`: `cr_ui.c cu_fav_key`'s keys). P5's programs are
  keyed by their Sequential number (object 23's section 16), so the owner's marks show on P5's rows whatever the
  shipped list's order; a P5 user sound over a program shows that program's mark (the place's), an added one its
  slot's. The manager never sets a mark, and its writes keep P5's section and its types byte for byte (Delete,
  Restore, Delete all and Reset take off only the deleted user slots' own marks, as since 3y79).
- **Audition**: SELECT with engine 16. A position past 127 (P5's added sounds after its 115 programs; the CZ-1's
  after its 64) now sends its high bits in the fourth byte `cr_backup.c crb_select` takes; until 3y80 it wrapped.
- **Retired** stays for the truly retired engines' records (PHASE, LOFI, VOICE, TRIO, NOISE, PHYS; ANALOG, SAMPLE,
  GRAIN, DRUM). Colours: P5's tab is green.
- **The simulator** (`?sim=pool`): U004 MY PROPHET over P5 03 and U007 P5 ADDED (`simP5Patch` programs), and a
  favourites object (U003, FM6 02, P5 01..05 with 03 typed Chord). `?sim=1` (0.15) has no P5 tab.
- Model: `fm1manager.js` `MANAGER_ENGINES` (six), `managerEngines`, `P5_ENGINE`, `favMarks`, `favKeyOf`, `favOf`,
  `keepable`, `P5_NO_PACK`, `simP5Patch`, `selectRequest`'s fourth byte; `fm1sounds.js` `FACTORY_IDS` and the 0.21
  refusal in `importSound`. Tests: `web/test_manager.mjs` (the 3y80 block: the pool and its stars from an object with
  all 115 programs marked and one typed; Place over a program, its program read back byte for byte; SELECT; rename,
  drag, Replace factory, Restore all P5, Restore factory order, Delete, Delete all and Undo, P5's marks and types
  byte for byte after each; Keep of a program refused; a 0.21 FM-1), `web/test_sounds.mjs` (P5's table; binding over
  P5 03 and the last program; past the end, INIT P5 and a second binding added), `tests/install_test.py` (the table
  and the ids, the same in both clients); the built page driven in headless Chromium with `?sim=pool` and `?sim=1`.
- Not done: a device run; P5's factory programs as files (the owner's decision: a pack would put Sequential's data on
  the site); Sequential program SysEx import (docs/PROPHET.md "Saving").
