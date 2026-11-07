# Presets: one pool per engine (the user's model, 2026-10-07)

**Status: done in the firmware 2026-10-07** (`cr_bank.c` the pools and the binding, `cr_ui.c` PRESETS / ALGORITHM /
OPT + PRESETS / the save dialog, settings v6, `tools/emu/scripts/cr_presets.txt`; docs/HISTORY.md 3e). Decided on the
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
  FM TONE 16 (engine 15, `eng_quad.c`, code name QUAD), VA 25, CZ-1 64, P5 115 (engine 16, code name PROPHET, 0.22 in development: a list,
  below), WHEEL 5, GRAIN 8 (engine 8, 0.24-beta2: docs/GRAIN.md): the picker's order, seven
  engines; six from P5 until GRAIN, five from 0.18 until P5; PHASE 6, LOFI 5, VOICE 4, TRIO 5, PHYS 9 and NOISE 4 came between and after them until then) followed by
  the presets the user added. ANALOG (engine 0, 12 presets) is retired
  since 0.14 (`FELUCCA_ANALOG` 0): no pool; a user record of ANALOG is an added preset of the VA's pool and plays there
  with its CUT / RES / DRIVE / MIX / DTN, filter envelope and ATK / REL on the VA's macros of the same name (`cr_ui.c`
  `cu_analog_va`; its binding to an ANALOG factory preset is dropped), another retired engine's record (SAMPLE, GRAIN,
  DRUM) the VA's init sound. PHASE, LOFI, VOICE, TRIO, NOISE and PHYS (engines 2, 3, 5, 6, 11, 9) are retired since 0.18
  (`FELUCCA_PHASE` / `LOFI` / `VOICE` / `TRIO` / `NOISE` / `PHYS` 0): no pool; a user record of one is an added preset of the
  VA's pool under its name (its binding to a factory preset of its engine is dropped) and loads as the VA's init
  sound with the record's sends (DIST CHO DLY REV) and its envelope's ATK / REL on the VA's macros of that name: a
  stand-in, with the message `PHASE retired: stand-in` (`cr_ui.c cu_load_user`); the record is not rewritten.
  Every entry is editable: **saving over a factory preset is allowed** and the factory one stays recoverable (reset).
- **PRESETS turns inside the current engine's pool only** (the chord part's engine; ALGORITHM the bass part's). The
  popup: the number big, the name, the stripe meter over the pool, and two lines that depend on where it shows.
  **Where OPT held + the part's knob is its engine picker** (`cu_epk_ok(part)`, 0.19: wherever the knob's plain job
  is the part's sound: the home pages, the Options, the layers but the PLAY menu, and for the bass part the sound
  editor; in 0.18 the home pages only, `cu_pool_home`; 2026-10-11, the owner: "show above the preset number in orange the current engine name. Then at the bottom
  it currently says 'ENGINE - 20/25', let's change that to say 'Hold OPT to change engine', the numbers and the bar
  already communicate how many presets there are and which one is selected"): the part's engine over the number in
  the role orange (`cr_screen_t.line` / `line_col`, the meter's line over its value; the bass with Bass Behaviour
  Solo: `VA · solo`), and under the stripes `Hold OPT to change engine` for both parts (since 2026-10-12 OPT + ALGORITHM is the bass
  part's engine, as OPT + PRESETS the chord part's; until then the bass part's line read `BASS + EDIT, hold EDIT:
  engine` and OPT + ALGORITHM was the bass level, which is BASS held + ALGORITHM now). **Elsewhere**
  (the PLAY menu, the editor's picker and its preview, the save dialog; the chord part in the sound editor): nothing over the number and
  `FM6 · 05/25` under the stripes, as everywhere until then (the place and the number of presets: 00 = INIT, which
  is not counted as a preset, 01.. the factory presets, then the user's); an overwritten factory preset
  carries a small square mark after its name. The meter is a stripe per preset and place k lights k of them: none on
  INIT, every one on the last preset (`WHEEL · 05/05`: five stripes, all lit). A pool of more than 32 presets (the
  CZ-1's 64, FM6 with eight added) has 32 stripes: none on INIT, one from preset 01 on, every one only on the last
  (`cr_ui.c` `cu_pool_lit`); since 0.16 (before: INIT was counted, `FM6 · 05/26`, and the last preset left a stripe dark).
  A step moves the stripes from the place shown to the new one, up (lit) or down (darkened), a stripe every 30 ms, more
  than 14 of them in 420 ms (a wrap); the part's first read-out and another engine's fill from none (0.17; before, every
  step filled from none: docs/INTEGRATION.md section 5, `cu_animate`).
- **OPT + PRESETS changes the engine** outside the editor (a horizontal picker: the engine big, the pool size as the
  value); the part lands on the preset it last had in that pool (remembered per engine per part; the first preset
  before that). The editor's EDIT-held picker is unchanged (it is the same thing inside the editor).
- **Where the two engine shortcuts work (0.19; the owner, 2026-10-12: "If GLO + Algo is changing bass level
  anywhere, it should change bass engine, let's keep the UI consistent where we can.").** One rule for both knobs,
  `cr_ui.c cu_epk_ok(part)`: where the knob's plain job is the part's sound, OPT held makes it the part's engine;
  where the knob has a job of the place's own, OPT changes nothing about it. Before and after, per place:

  | place | OPT + PRESETS, 0.18 | OPT + PRESETS, 0.19 | OPT + ALGORITHM, to 0.17 | 0.18 | 0.19 |
  | --- | --- | --- | --- | --- | --- |
  | home pages (every view) | chord engine | chord engine | bass level | bass engine | bass engine |
  | the Options | chord engine | chord engine | bass level | bass engine | bass engine |
  | layers KEY, PERF, FX, BASS, SEQ, LOCK, the view menu (also over the sound editor) | chord sound (plain) | **chord engine** | bass level | bass sound (plain) | **bass engine** |
  | the sound editor's pages | the editor's picker opens (PRESETS's job there) | the same | bass level | bass sound (plain) | **bass engine** |
  | the PLAY menu | the slot (PRESETS's job there) | the same | bass level | the record mode (ALGORITHM's job there) | the same |
  | the loop editor | 8 steps a click | the same | Voicing Follow / 0 | the same | the same |
  | the editor's picker (`L_EDIT`) | its presets | the same | bass level | bass sound (plain) | the same |
  | the save dialog | chord sound (plain) | the same | bass level | bass sound (plain) | the same |

  The last two are kept out on purpose: each holds one sound (a preview OCT- takes back; the sound being named and
  stored), and another engine loaded under it by a second picker would be previewed, cancelled or saved in its
  place. The picker, the landing rule (`cs.pool_pos[part]`), the read-out and "a bass that is off stays off" are
  `cu_epk_step` / `cu_epk_commit` everywhere: one code path. The read-out's hint line follows the same predicate, so
  `Hold OPT to change engine` is shown exactly where it is true. `tools/emu/scripts/cr_glo_knobs.txt` walks every
  row.
- **SAVE tap**: a menu of five since 0.22 (two until then), **Overwrite** (the current preset; saves at once, name
  kept), **Save as new** (the pool's next free place, then the naming screen prefilled with the current name),
  **Favorite**, **Favorites filter** and **Favorite type** ([Favourites](#favourites-022), below: nothing is saved by
  these three). It is a menu in the Options' grammar: **SELECT picks the entry** (a step a turn, stopping at the
  ends), **KNOB 1 changes the selected entry's value** (the two saves have none: nothing), and three knobs do the
  same thing on every entry: **KNOB 2 the filter, KNOB 3 the sound's type, KNOB 4 its mark**. (Until 0.22 any turn of
  SELECT or KNOB 1 right was Save as new, any turn left Overwrite; KNOB 2 / 3 / 4 fell through to their home jobs: the
  bass register, the Perform mode's knob, the chord part's send.) **The naming page** (Save as new) has a cursor
  since 0.22: SELECT moves it, KNOB 1 changes the character under it, KNOB 4 left deletes it, and every other knob
  does nothing there ("The naming page", below). The default is Overwrite when
  the current preset is a user one or an edited one, Save as new otherwise. OCT+ takes it, OCT- cancels. **SAVE held
  1 s in the dialog**: "reset to factory?" on an overwritten factory preset, "delete?" on a user-added one. SAVE held
  outside the dialog stays the loops' save / load / delete.
- The bass: the same model on ALGORITHM (the bass part's engine's pool). ALGORITHM position 0 = OFF as today. Its
  engine: OPT held + ALGORITHM on the home pages since 2026-10-12, and since 0.19 wherever ALGORITHM alone is the
  bass sound (the table above) (the chord part's picker in orange, the same
  engines, wrapping, the place last played in that engine, `cs.pool_pos[1]`; a bass that is off stays off), and as
  before BASS held + EDIT's picker. The bass level, OPT + ALGORITHM until then, is BASS held + ALGORITHM.
- The idea of "templates" is gone: what the editor's picker steps with KNOB 1 is the same pool PRESETS turns.

## P5's pool (0.22 in development; docs/PROPHET.md; the engine's code name is PROPHET)

- **00 INIT** is the engine's init sound (its preset 0 is the pool's INIT, as the CZ-1's INIT TONE: `cr_bank.c
  pool_f0`); **01 ..** are the **shipped** factory programs: the ones of Sequential's 200 (v1.03) that
  `assets/prophet5-factory/shipped.txt` names, in that file's order, with 12-character PRESETS names made from the
  programs' own (`tools/gen_prophet_factory.py`). Since 3y51 the owner's 115 favourites, by Sequential's number (the table is
  in docs/PROPHET.md "The shipped list", with the steps to change it); no list or `all`: the 200. A build with
  `FELUCCA_P5_FACTORY=0` has INIT alone. The read-out is `P5 · 03/115`; the stripe meter has 32 stripes, as every
  pool over 32.
- **A place in this pool is not a program's identity**: the list may change between builds. A program is known by
  its Sequential number (1..200), which it carries in its own bytes; the session keeps the program itself and finds
  its place again from that number (docs/PROPHET.md).
- **A load takes the program's UNISON switch** (18 of the shipped 115): a chord on such a program sounds one note on up to four
  stacked voices; the next program without the switch is poly again (docs/PROPHET.md "UNISON").
- The programs are dry; they play with the part's sends, which no sound load touches (the model above).
- **It grows as every pool does since the sound pool (3y57)**: SAVE on a P5 part has the five entries, and Overwrite
  or Save as new keeps the whole 138-byte program in one of the 128 slots ([The pool](#the-pool-022-128-user-slots-shared-by-every-engine);
  docs/PROPHET.md "Saving"). (Before the pool its SAVE menu had the three favourites' entries alone and OCT+ said
  "P5: no save yet": there was no store for the program.) Its edits also live in the part and in the session. A
  factory program's mark is on its Sequential number, a P5 user sound's on its slot.
- Left as it is for phase B: the settings record has no slot for P5's per-engine pool position (`cr_settings.c
  crs_slot`), so after a power cycle a part that comes back to P5 from another engine lands on 01 (the sound a part
  was playing at power-off does come back: the session). A pool's place, an engine's preset count and a track's
  preset are bytes: 200 fits, 255 is the end.
- The pool read-out showed 11 of a name's 12 characters; it shows 12 with P5 in the build (`cu.pop.sub`).

## Favourites (0.22)

The owner, 2026-10-13: "I would like to add a feature to mark presets as favorites. To do this I am thinking, press
save to go into the save menu, turn select to third option "Favorite", use knob 4 to mark as favorite or remove from
favorites. Or inside the edit menu turning a preset brings a locked on preset browser, use knob 4 in this to mark
favorites too. Then what I can do is mark my favorite Prophet patches and only those get included."

And the same day, for the filter and the type (HISTORY 3y42): "now with the Favorites, I would love a feature to
filter the presets to just favorites. This could be done by adding another option after favorite in the Preset Save
Menu. [..] Lets also make knob 1 work for changing values to match menu behaviour in other menus like GBL, Knob 4
should set favorite regardless of which setting is selected. I want filter to reduce the preset list to just
scrolling between favorites, still limited by engine. [..] I would also like an additional filter option in the
preset save menu to setup what type of favorite it is: Both, Chord, or Bass."

**What a mark is.** A flag on a SOUND (`firmware/src/cr_favs.c`, `cr_ui.c cu_fav_key`):

| the sound | its key |
| --- | --- |
| a factory preset of FM6, FM TONE, VA, CZ-1, WHEEL | the engine's index and the preset's index in the engine's own table (`engine_t.presets`). That index is already what a user record's binding stores (`flags[15]`, below), so it has to stay put from release to release; nothing was reordered since 0.14 (checked through the tags for VA, FM TONE, WHEEL and FM6's table; CZ-1's 64 are Casio's order). There is no other stable id for these engines |
| a P5 factory program | its **Sequential program number 1..200** (`p5_preset_id`; for the part's sound the number in the program's own bytes), never its place: marks survive `shipped.txt` being trimmed or reordered, and a marked program that a build does not ship keeps its mark for the build that does |
| a user sound added to a pool | its slot 0..31 |
| a user record that overwrites a factory preset | that factory preset's key (it is the same place of the pool): the mark survives Overwrite and Reset to factory |
| INIT | none: **INIT cannot be a favourite** (`INIT: cannot be marked`) |
| a sound edited and not saved | the sound it came from (an edited factory sound marks its factory origin) |

A user slot loses its mark (and its type) when it is deleted, and when a record goes into an empty slot (`upreset.c
up_put`). A slot rewritten from outside (the Sound Manager importing over a used slot) keeps them: the slot is the
identity.

**The type.** Every sound has one of three types, **Both** (until it is given another), **Chord** or **Bass**, under
the same key as its mark. It is the SOUND's, not the mark's: the owner, "I could have presets that work well for
bass and ones that work well for chords, but favorite/unfavorite them and the type does not change". Taking the mark
off and putting it back leaves the type; a sound can be typed without being a favourite. The type decides one thing:
with the filter on, a favourite typed Chord is offered to the chord part only, one typed Bass to the bass part only,
Both to both. The star does not depend on it.

**The filter.** One Off / On for the whole unit, kept with the marks (default Off). While it is On, every knob that
steps through a part's pool steps through **that part's favourites of its engine** only: PRESETS (the chord part:
favourites typed Both or Chord), ALGORITHM and the BASS layer's SOUND (the bass part: Both or Bass), and KNOB 1 /
PRESETS in the editor's browser (the part being edited). User sounds by the same rule; INIT is left out (it cannot be
a favourite). The order is the pool's own. `cr_ui.c cuf_*`:

| the case | what happens |
| --- | --- |
| the ends | PRESETS and ALGORITHM go round (past the last favourite comes the first; ALGORITHM's OFF stays a place between the last and the first, as in the whole pool); the BASS layer's SOUND stops at the ends, OFF below the first, as it does without the filter |
| the sound playing is not a favourite of the part (the filter just turned on, its mark just taken off, a program change, a restored session) | it keeps playing; the next turn goes to the nearest favourite in that direction |
| the part's engine has **no** favourite fitting the part | the knob turns the engine's whole pool as if the filter were off there, and the read-out says `no favorites: all` |
| the engine changes (OPT + PRESETS / + ALGORITHM) | the part lands on the new engine's **first** fitting favourite, not on the place remembered there; an engine without one: the place remembered, as ever |
| the read-out | the big number and the stripes stay the sound's place in the POOL (its identity: `FM6 26` is what a save and the Sound Manager call it); the line under the stripes, in the star's colour, is the sign that the filter is on and the place among the favourites: `favorites 03/07` (`--/07`: the sound is not one of them), or `no favorites: all`. It stands where `Hold OPT to change engine` is on the home pages (the hint is not shown while the filter is on) and after the engine's name elsewhere (`FM6 · fav 03/07`) |
| not filtered | a MIDI program change, the session and power-on restore, a loop's recorded sound changes, a backup's restore, the Sound Manager: they name a place. The editor's engine picker (SELECT / a root in the browser) loads the engine's first preset as before |

A turn never walks the pool: the fitting places of the part's engine are kept as 32 bytes of bits per part, built
once when a mark, a type, the user bank or the engine changes (P5: 200 places, each a look through the 32 slots, then
nothing per detent).

**Where it is kept.** One small flash object of its own, `OBJ_FAVS`, the A/B sector pair **0xB6000 / 0xB7000**
(`storage.c`; not the settings block, which never grows). In RAM 217 bytes: three bitmaps of 72 bytes (the marks, the
sounds typed Chord, the sounds typed Bass) and the filter; and 88 bytes of the filter's two lists. In flash,
**version 2**: the version byte, the number of sections, then per section a tag, a length and that many bytes. A
tag is the engine's index (0x80 the user slots): its marks, a bit a sound; that tag | 0x40: its sounds typed Chord;
| 0x20: those typed Bass (marked or not); 0xFF: one byte of options, bit 0 the filter. Trailing zero bytes and
sections of zeros are not stored (nothing marked or typed, the filter off: 2 bytes; the longest: 262; the buffer
264). An unknown version reads as no marks; an unknown section is skipped; bits past a section are ignored; Chord
and Bass at once read as Both. The write is `storage.c`'s (the other copy, the commit record last): a torn write
leaves everything as it was.

**Version 1** (the object of 0.22's first builds, 40acb01: the marks alone, 96 bytes at most) is read as it is: every
mark is there, every sound Both, the filter off. It is not rewritten at power-on; the next change writes version 2.
The other way round, a version-1 reader meeting version 2 (only the build 40acb01, on the owner's unit): it reads
"another version", shows no marks, and leaves the object alone until a mark is made THERE, which then writes a
version-1 object over it: the marks, types and filter of this one are gone, nothing else in flash is touched (run on
the host with 40acb01's own `cr_favs.c` against a 96-byte version-2 object; an object over 96 bytes it cannot load,
which it also reads as none: by reading its code, not run). A backup taken before going back keeps them.

**When it is written.** By itself, as an Option is: once no mark changed for 1.5 s and nothing has sounded for 1 s,
never while a loop plays (`cr_settings.c crf_poll`: the erase holds the audio 45 ms, so a mark made under a ringing
chord waits for the quiet); also with the global save (SAVE held) and before a backup's LIST. Not in SAFE MODE.
Options > Reset Settings leaves the marks; SAFE MODE's Flash Data erases them with the sounds (76 sectors now: the pool's 16 joined after 0.24-beta2, which left
the sounds in place).

**Backup.** Object id **23**, "favorites" (`cr_backup.c`, `web/fm1backup.js`, `tools/fm1_install.py`). A firmware
before 0.22 does not list 23, the clients write only what the FM-1 lists, so restoring a 0.22 backup there leaves the
favourites in the file (reported as skipped); asked directly, 0.21 answers a PUT begin of an unknown id rc 1 and
writes nothing. An older ChoralRoot never reads or erases 0xB6000 / 0xB7000 (0.21's map ends at the session,
0xB5FFF), so the object's presence does not touch it, and it is still there when 0.22 comes back.

**Restoring marks from a saved list** (3y54). `tools/fm1_install.py --favorites > FILE` keeps the marks as text;
`tools/fm1_install.py --restore-favorites FILE` writes them back: P5's lines by Sequential number, the engines' and
the user sounds' comment lines (`# FM6 01 ..`, `# user U03 ..`, by pool place and slot), each `(chord)` / `(bass)`
type, and the filter as the header line says (`, the filter on`). It prints what it will write and asks (`--yes`: not),
PUTs object 23 alone (the object packed as `crf_pack` packs it; nothing else on the unit is touched), reads it back,
compares, and says `N marks restored`. The unit takes it at once (`cr_backup.c`: the PUT reloads the RAM copy,
`crf_boot`), no restart. Other `#` lines are notes and are skipped (and listed). ChoralRoot 0.22 on only. The text
replaces the marks: a sound not in it is left unmarked (its type Both). Another firmware (the third-party "remix",
whose loop slots 11-20 cover 0xB4000-0xC7FFF, or Melodee) can overwrite 0xB6000 / 0xB7000: a full `--backup` before
trying one keeps the marks with everything else.

**On the unit.**

- **The SAVE menu** (`cr_ui.c cu_knob`, `cu_save_screen`). Five entries, on every engine (P5's too since the sound
  pool, 3y57; before it a P5 part's menu had the last three alone, `cu_save_favonly`, and OCT+ said "P5: no save yet"):

  | entry | the big word (its value) | KNOB 1 |
  | --- | --- | --- |
  | 1 Overwrite | `Overwrite` | nothing (no value) |
  | 2 Save as new | `Save as new` | nothing |
  | 3 Favorite | `Favorite` / `Not a favorite` | right marks the sound, left takes the mark off |
  | 4 Favorites filter | `Filter on` / `Filter off` | right On, left Off |
  | 5 Favorite type | `Both` / `Chord` / `Bass` (the sound's, marked or not) | a step a turn, stopping at the ends; **a turn here also marks a sound that is not a favourite** (the owner: "I am fine if the sound is unfavorited and you change type to auto favorite it, which would make things quicker to organize"), a turn against the end stop too |

  **SELECT** moves between the entries, a step a turn, stopping at the ends. On EVERY entry: **KNOB 2** is the filter
  (left Off, right On), **KNOB 3** the sound's type (as KNOB 1 on entry 5, the auto-mark included), **KNOB 4** the
  mark (right on, left off; it never changes the type). On an entry that does not show what the knob changed, a
  yellow message says it for a moment (`Filter on`, `Type: Chord`); the star by the name shows the mark on every
  entry. **OCT+** (and SAVE again) saves on entries 1 and 2 only; on 3 .. 5 OCT+ does nothing and SAVE again closes; OCT- closes. The look is the dialog's: the big word, the entries next to it
  small (`Filter off`, `Type: Chord`), the sound's name with its star, a line that names the entry (`FM6 ·
  favorite`, `favorites filter` or, with the filter on and no favourite of the part in its engine, `no favorites
  here: all`; `favorite type` or `type · not a favorite`), five dots, and the footer `K1 change · K2
  filter · K3 type · K4 favorite` on entries 3 .. 5 (the knobs' names spelled out do not fit the line; entries 1 and
  2 keep `OCT+: save · OCT-: cancel`, where the three knobs work all the same). Over the red question (SAVE held)
  the four knobs do nothing; the naming page has its own (below).
- **The naming page** (`cr_name.c`, `cr_ui.c cu_name_knob`; the owner, 2026-10-13: "When you save as new, the editor
  to name the preset needs work. All the other knobs do default things which is weird. Select knob should select
  character, knob 1 to change the letter at selected index, knob 4 CCW as a backspace, the other knobs 2 and 3 in the
  name editor should not do anything." And: "The cursor should always sit at the character you are editing, not the
  next character."). A **cursor**, always ON a character of the name: the last one when the page opens, so KNOB 1 at
  once changes what KNOB 2 changed until then. **SELECT** moves it a step a turn over the characters; **right from
  the last character it adds a character** (`A`: the set's first is the space, which would show nothing) and sits on
  it, up to 12, where it stops: that is how a name grows with the knobs. **KNOB 1** steps the character under the
  cursor through ` A..Z 0..9 - .` (the set and order KNOB 2 had). **KNOB 4 left** is the backspace: the character
  UNDER the cursor goes and the cursor moves to the one before it (the first deleted: it stays on the new first);
  KNOB 4 right: nothing. **KNOB 2, KNOB 3, PRESETS and ALGORITHM do nothing**, with GLO or BASS held too. Until
  then, with a name on the screen, KNOB 1 was the voicing (the Full keyboard: the octave), KNOB 2 the last letter,
  KNOB 3 the Perform mode's knob, KNOB 4 the chord part's send, SELECT the tempo, PRESETS and ALGORITHM changed the
  chord and bass sound under the name being typed, GLO + KNOB 1 was the split point, GLO + SELECT the click level,
  BASS held + ALGORITHM the bass level. A layer held open over the page (PERF, FX, ..) still owns the knobs while it
  is open; the buttons are unchanged (OCT+ / SAVE save, OCT- / HOME cancel: OCT- never deleted a letter, so nothing
  conflicts with the backspace). **The keys** type as before: a typed character goes in AFTER the cursor and the
  cursor moves onto it, which with the cursor on the last character is appending, as ever; F#4 is the same
  backspace. **An empty name** is allowed as it was (every character deleted; it is saved under the slot's automatic
  name): no character, so no cursor is drawn; SELECT right or KNOB 1 gives it a first `A`. The cursor is a steady
  3 px bar in the title's colour under its character (`cr_line_t.cur`); the `_` that blinked after the name until
  then redrew the screen twice every 0.8 s. A long name is set smaller so that it is never cut (36 px down to 22 for
  twelve wide letters). The page's two
  lines read `SELECT: cursor · K1: letter` and `keys: letters · K4 left: delete`.
- **The editor's browser** (PRESETS turned in the editor, or EDIT held): **KNOB 4** marks the sound under the cursor
  (the one previewed) the same way, the browser stays open, the read-out pops with the star. OCT- gives the sound
  back as before; marks made while browsing stay. (KNOB 4 had no job there in 0.19 .. 0.21.) The mark alone: the
  sound keeps the type it has (Both when it never had one). With the filter on, KNOB 1 / PRESETS there step the
  edited part's favourites.
- **The star**: a small five-point star in the role yellow before the name, the two centred together, wherever a
  sound's name is read out: the pool read-out (PRESETS, ALGORITHM), the editor's browser, the save dialog. Drawn as
  five triangles (`cr_draw.c cr_star`, about 250 bytes of flash; the font has no star). Yellow is the KEY layer's
  colour; the star never stands on a KEY screen's own content, and a gold star reads as "favourite" without a
  legend. With no mark and the filter off every screen is drawn as before, pixel for pixel, except the SAVE menu's
  page (five entries).
- **Not built**: favourites in the web Sound Manager (it neither shows nor filters by them). The store answers
  `crf_has` / `crf_type` for any sound, which is what it needs.

**Off the unit.** `python3 tools/fm1_install.py --favorites` prints the marks (read-only: the backup protocol's read
of object 23; versions 1 and 2): `# FM6 01 TINE EP` a line, `# user U03 NAME`, and P5's programs by Sequential's
number, `NUMBER  # NAME` a line, so the whole output is a valid `shipped.txt`. A mark typed Chord or Bass says so
after the name, inside the comment: `# FM6 25 PIANO  (chord)`, `12     # BLOW BY BLOW  (bass)`; the first line adds
`, the filter on` when it is. A sound that is typed and not marked is not listed. docs/PROPHET.md "The shipped list from the unit's
favourites" has the commands.

## The pool (0.22: 128 user slots shared by every engine)

The owner, 2026-10-14: "For preset slots can we have a larger shared pool?" and, on the design, "Yes build it".
Since 0.22 the user sounds are **128 slots, U001..U128, shared by all engines** (P5 included), in a store of their own;
the 32 slots of 0.21 and before (the records of `upreset.c` and each engine's patch store beside them) are read once,
at the first power-on, and never written again.

**A slot** (`firmware/src/upool.c`) is 384 bytes: the user record as before (`up_rec_t`, 192 bytes, `upreset.c`'s
format, unchanged), then at 192 the engine of the patch that follows + 1 (0: none), at 193 its length, 194 / 195 zero,
and from 196 that engine's deep patch (`eng_deep_t.blob_get`: VA 110, FM6 128, CZ-1 144, FM TONE 80, P5 138; at most
188). A record and its patch are saved, loaded, erased and backed up together.

**The store** (`firmware/src/storage.c`, the `sp_*` section): 13 logical banks of 10 slots (3840 bytes, one sector's
payload; slots 129 and 130 of the last are not offered) **float** over the 16 sectors 0xB8000..0xC7FFF. A sector is a
bank's copy when it carries a valid commit record (`st_hdr_t`: `ST_MAGIC`, type `SP_TYPE` 0x4C50 "PL", slot = the
logical bank, len 3840, `rsv[0]` = `SP_VER` 1, the header CRC) and its payload CRC matches.
- **Load** (`sp_scan`, at power-on): the 16 commit records; per bank the valid copy with the newest seq wins (serial
  arithmetic: seq wraps; two copies with the same seq, which this writer never makes: the lower sector); a copy whose
  payload fails its CRC is passed over for the next newest. Every save takes seq = the newest seq of any valid pool
  header + 1, so seq orders all of the pool's writes.
- **Save** (`sp_commit`): the new copy goes to the **target**, the first sector after the newest one (cyclic) that holds
  no bank's current copy (16 - 13: at least three such sectors; the writes walk round all 16, the wear spreads). If the
  target is not blank it is erased (the only erase), then the ten slots are programmed from the current copy slot by
  slot through 384 bytes of `st_buf` (each in the pieces that fall in one page), the changed slot substituted, and the
  commit record **last**; then everything is read back. Until the commit record is programmed the old copy is the
  newest valid one, after it the new one is; the old copy stays as it is until its sector becomes a target. A cut
  anywhere gives the old bank or the new one, never a mix (`tests/pool_test.c`).
- **The quiet writes**: the target is erased ahead (`sp_prep`) at power-on before the audio starts and after a long idle
  (`cr_settings.c crs_ahead_boot` / `crs_ahead_poll`), so a SAVE finds it blank and only programs (21 page programs).
  A SAVE that finds it not blank erases at once (the player's own press, as before).
- **No RAM mirror**: `upn[128]` in the pool section keeps per slot the name, the engine, used / has a pattern, step 16
  (the binding); `up_rec(k)` reads a record from flash into one of two cached copies; a patch is read when a sound
  loads (the main loop, never the audio ISR). One 384-byte work buffer composes a save. (RAM -4992 B, POOL -12832 B,
  XIP +304 B against cd125b0.)

### What browsing reads (3y60)

The owner, on 0d227e0 with all 128 slots used: "with all the user presets filled, my FM-1's UI is now very laggy".
The pools' order (`cr_bank.c`: each slot's pool, the factory place it is bound to, the first bound slot winning) was
worked out from the **records**, through `up_rec` (a flash read of 192 bytes each), for every slot on every call, and
the duplicate check made it quadratic: one PRESETS detent with a full pool read **~5500 records (1.05 MB)**, with the
favourites filter on ~10900 (2.1 MB). The driver reads a byte at a time with the XIP cache off (`fm1_flash.h
fl_read_ram`), so that is in the order of a second per detent. The frames themselves read nothing.

Now **nothing in browsing reads flash**:
- `upn[]` (the name table, POOL) already holds what the binding needs: the engine, step 16's two bytes, the grid flag;
  `cr_bank.c cb_rec_raw` reads it there, never the record (the RAM mirror of builds without the pool otherwise);
- `cr_bank.c cbc` (RAM, 128 + 128 + one per engine bytes: 281 B) keeps each slot's pool, its resolved binding and
  the added sounds per engine. It is built again (O(slots^2) byte compares in RAM, once) when a slot's line of the
  table changes (`upool.c upn_note` clears it: save, delete, rename, import, restore, boot) or `up_gen` moves;
- names come from the table (`up_name`), as before; `pool_count` is a sum, `pool_entry` / `pool_pos_slot` one walk of
  128 bytes, the favourites' list (`cr_ui.c cuf_n`) is rebuilt from these when marks or the pool change.
A record and its patch are read only when a sound **loads** (2 reads, ~270 bytes). `tests/cr_poolperf_test.c` holds
that: a full pool's frames read nothing, a PRESETS or ALGORITHM detent (filter off and on, after a mark change) at most
4 reads. 78 PEAK's values 65..68 (`ui_frame_max_us`, `ui_frame_avg_us`, `flash_reads_ps`, `flash_read_bytes_ps`,
docs/FX.md) show it on the unit.

**Reading the pool through XIP instead** (the owner: "Can the user presets all be stored in Flash like the factory
ones?" They are: the difference is the way they are read). `fl_plain_window_init` (at boot, `cr_shim.c
persist_boot`) maps flash from 0x93000 up as a plain XIP window (`FM1_XIP(off)` = 0x02000000 + off - 0x4000), which
covers the pool (0xB8000..0xC7FFF) and the legacy stores; the RAM driver's erase and program invalidate the cache lines
of what they touched (`fl_inval`), and the CPU cannot read XIP while they run (interrupts off, code in RAM), so an XIP
read is never stale. User sample sets already play that way (`eng_sample.c SMP_USER_XIP`). An XIP read is ~30 MB/s
(docs/BOOT-SAFETY.md: 440 KiB in 14 ms) against the driver's byte loop; but the old walk through XIP would still have
touched 1 MB a detent (~35 ms, the code cache thrashed), the slot's place moves with its bank's copy (`sp_cur`), and the
emulator and host tests have no XIP mapping of the data region. The cache removes the walk instead; XIP remains an
option for the loads themselves (2 reads a load: not worth it now).

**The migration, 0.22 .. 0.29.2 (retired in HISTORY 3y231)**: at their first power-on, when the pool held no bank and
a legacy bank held a record, those firmwares copied legacy slot k (1..32: the record of the user bank of 0.21 and the
patch k of the store of that record's engine: the VA store, the FM6 store's halves, the CZ-1 store's halves, the FM
TONE store) byte for byte into pool slot k, once; the device manager's **Import the 0.21 slots** (SysEx 80 IMPORT) did
it again on request. **Since 3y231 nothing reads 0.21's objects**: the import, command 80 and the four store files are
gone, and their sectors (0x97000..0x9EFFF, 0xA0000 / 0xA1000, 0xB2000 / 0xB3000, 0xDC000..0xDFFFF) are free. A unit
still on 0.21 or older goes through **0.29.2** first (the web installer asks, `index_pkg.html needsPoolStep`); going
back to 0.21 from a later firmware still finds its 32 slots as they were, since nothing writes those sectors.

**Favourites**: slots 1..32 keep their keys (section 0x80); marks and types of U033..U128 go to a new section 0x81
(`cr_favs.c CRF_T_USER2`, 12 bytes). 377264f's reader skips a section it does not know, so it keeps every other mark;
it reads an object of at most 264 bytes, and the 0.22 object can reach 305 (then 377264f reads no marks at all and its
next mark change writes its own object). The favourites filter covers pool places 1..255 (the default build's longest
pool, P5's, is 115 + 128 + INIT = 244).

**Backups** (`cr_backup.c`): the pool's banks are **ids 50..62** (each 3840 bytes, 0 when never written). 0.21's sound
ids 6, 7, 9..13, 22 are neither listed nor taken (a PUT begin answers rc 1, which the pages report as skipped): a backup
of 0.21 or older restores its settings, banks and loops but not its 32 sounds (0.22 .. 0.29.2 imported them into
U001..U032; restore such a backup on 0.29.2, then back up again). Ids 24..36 were the plan's but would take Felucca's sample ids
32..34 (refused since ChoralRoot 0.1). A backup of 0.22 restores on 0.21 only in part: its settings, the FM6 and CZ-1
banks and the loops; the sounds (ids 50..62) are refused there (rc 1, reported as skipped), and so are the favourites
(0.21 has none; a 0.22 build of before the pool, such as 377264f, takes them).

**Third-party firmware**: the "remix" that keeps loop slots 11-20 at 0xB4000-0xC7FFF overwrites the pool's sectors as
it does the favourites': back up before trying such a build.

## Storage (no flash layout change until 0.22: [The pool](#the-pool-022-128-user-slots-shared-by-every-engine))

- The 32 user slots (`upreset.c` records, the VA / FM6 / CZ-1 stores beside them; since 0.22 the pool's 128) hold every user-saved preset.
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

**Replace factory (the device manager, DN-3)**: binding user records to factory places in bulk (docs/DEVICE-MANAGER.md
"Replace factory"). Nothing new in the firmware: a record bound to a factory preset takes its place in `pool_entry`
and the preset is out of the pool until the record is deleted or unbound (Restore factory order: the records become
added sounds). A factory preset's favourite mark stays with its place (the record over it has its key).
