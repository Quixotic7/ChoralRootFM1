# ChoralRoot FM-1: user sounds as files (the installer's Sounds page, `fm1_install.py --sounds`)

A user sound (one of the 32 slots U01..U32: its record plus, for a VA / FM6 / CZ-1 sound, its patch) can be exported
to a small JSON file, imported into any slot, renamed and deleted from the computer, without touching the rest of the
FM-1's data. Nothing new in the firmware: everything is built on the backup protocol's whole-object reads and writes
(`web/EDITOR_PROTOCOL.md` "ChoralRoot: backup and restore", `firmware/src/cr_backup.c`): the page or the CLI reads the
objects that hold the user sounds, edits the slot in them, and writes back only the objects that changed. The FM-1
validates each object before it is written, reloads its RAM mirrors from flash after each commit (`cr_backup.c`
`crb_commit` -> `up_boot`), and no restart is needed (the settings record is not touched).

| where | what |
| --- | --- |
| `web/fm1sounds.js` | the pure module: parse the objects, the slot table, export / import / rename / delete as new object bytes, the file format; no DOM, no MIDI |
| `web/index_pkg.html` | the **Sounds** section of the installer page: Read sounds, the 32 rows, Export / Import / Rename / Delete per slot |
| `web/test_sounds.mjs`, `web/test_installer.mjs` | the module's checks; the page against a simulated FM-1 |
| `tools/fm1_install.py` | `--sounds`, `--export-sound`, `--import-sound`, `--rename-sound`, `--delete-sound` (the same logic in Python) |
| `tests/install_test.py` | the CLI against the simulated firmware |
| `tests/cr_backup_test.c` | the firmware side: a bank and a store written through PUT reload into the slot (name, engine, patch) |

## The objects that hold the user sounds

All sizes are the whole object as `BACKUP_LIST` reports it; 0 = the object was never written (an empty bank or store:
the writer creates it with the header below). Multi-byte fields are little-endian.

| backup id | object | size | holds |
| --- | --- | --- | --- |
| 6, 7 | user sound bank 0 / 1 (`upreset.c` `up_bank_t`) | 3080 | slots 1..16 / 17..32: the records |
| 9 | the VA patch store (`va_store.c` `va_store_t`) | 3536 | one 110-byte VA blob per slot 1..32 |
| 10, 11 | the FM6 patch store, halves 0 / 1 (`fm6_ustore.c` `fm6u_t`) | 2064 | one 128-byte FM6 blob per slot 1..16 / 17..32 |
| 12, 13 | the CZ-1 tone store, halves 0 / 1 (`cz_ustore.c` `czu_t`) | 2320 | one 144-byte CZ-1 tone per slot 1..16 / 17..32 |
| 22 | the FM TONE patch store ("FM TONE patches": FM TONE is engine 15, `eng_quad.c`, code name QUAD; `quad_store.c` `quad_store_t`; firmware 0.14 on) | 2576 | one 80-byte FM TONE blob per slot 1..32 |

Object 22 is read when the FM-1 lists it: an older ChoralRoot (no FM TONE) is read as before, its FM TONE store empty (an
FM TONE sound cannot be imported there: the write of object 22 is refused, rc 1).

### A bank (`up_bank_t`, 3080 bytes)

```
0   u32  magic   0x31425055  "UPB1"
4   u16  rsize   192
6   u16  nslot   16
8   16 x up_rec_t (192 bytes each): slot (bank x 16 + i)
```

A bank whose magic, rsize or nslot differ reads as empty on the FM-1 (`up_bank_check`); the writer always sets them.

### A record (`up_rec_t`, 192 bytes)

```
0    u8   used     0xA5 when the slot holds a sound; anything else: empty (the FM-1 zeroes a deleted slot)
1    u8   ver      1..5 (the layout version the FM-1 wrote; 4 = today's)
2    u8   engine   0..15 (the table below)
3    u8   np       the parameter count stored (8..144 for ver >= 4, 8..72 before)
4    char name[12] ASCII 32..126, zero-padded (no terminator when 12 long); name[0] != 0
16   u8   packed[144] (ver >= 4: each value + 64, 0..191) / i16 p[72] (ver <= 3)
160  u8   note[16]
176  u8   flags[16]
```

A record is **valid** (`up_valid`) when used == 0xA5, 1 <= ver <= 5, engine < 16, 8 <= np <= (ver >= 4 ? 144 : 72),
name[0] != 0 and, for ver >= 4, every packed[i] for i < np is <= 191. The page and the CLI apply the same test: an
invalid record reads as an empty slot and is never written.

**The binding** (docs/PRESETS.md, firmware 0.14): ChoralRoot has no sequencer, so a record's pattern bytes are free;
two of them place the preset in its engine's pool. `note[15]` (byte 175) = **0xA6** marks a record the FM-1 saved
with a binding, and `flags[15]` (byte 191) is then the **index + 1 of the factory preset it overwrites** (the engine's
`presets[]`, FM6 1..25, CZ-1 2..65: its INIT TONE is the pool's INIT) or **0 for a preset added to the pool**. A record
without the mark (older firmware, a client's, a restore) is an added preset of its engine; so is a mark naming no
factory preset of the engine, or one an earlier slot already binds (the first bound slot wins). The pool of an engine:
00 INIT, its factory presets (each replaced by the slot bound to it), then the added records in slot order. The pool
of a record is its engine; a DIGITAL record (1) is in FM6's (and binds there), another retired engine's in the VA's
(ANALOG, 0, retired in 0.14, plays there with its macros; PHASE LOFI VOICE TRIO NOISE PHYS, retired in 0.18, and SAMPLE
GRAIN DRUM as the VA's INIT: a stand-in), never bound; a drum grid
record (ver 3 or 5) is never bound. Older firmware reads a bound record as an ordinary one (a note 38 on step 16 of a pattern
it never plays here); `up_valid` is unchanged.

**What the clients show** (`web/fm1sounds.js` `parseSoundObjects` / `soundBindings`, `tools/fm1_install.py`
`parse_sound_objects` / `sound_bindings`, the firmware's `cr_bank.c` `cb_bind_raw` / `cb_bound` rule for rule): per
used slot `bound` = the factory preset it overwrites (`index` in `presets[]`, `pos` its place in the pool = index -
the pool's first factory preset + 1, `label` "FM6 02", `name` "FM BELL") or null, and `added` = used and not bound.
The page's table has a fourth column, Preset: "over FM6 02 FM BELL" or "added"; `--sounds` ends each line with
`over FM6 02 FM BELL` or `+`. The sound file carries `"binding": {"overwrites": 2, "name": "FM BELL"}` (`overwrites`
= the pool position) or `null`; it is informative: an import keeps the record's bytes, binding included, and reads a
file with or without the field (a stale field is ignored). A record imported where an earlier slot already binds that
preset is an added one on the FM-1 (the first bound slot wins), and the table says so after the write. A .syx import
builds its record with the mark and `flags[15]` = 0: an added preset of its engine.

**The factory names** come from the firmware, never typed: `tests/sound_templates.c` prints `"factory": {"<engine>":
[name, ..]}` (`ENGINES[e]->presets[k].name` for every engine with `eng_ok`) and `"factory_first": {"<engine>": n}`
(`cr_bank.c` `pool_f0`: 1 for the CZ-1, whose preset 0 INIT TONE is the pool's INIT, and for P5, whose INIT P5 is;
else 0), each compact on one line, and `"factory_ids": {"16": [0, 1, 2, 3, 6, ..]}`: P5's presets' favourites ids
(their Sequential program numbers, `cr_ui.c cu_fav_key`; every other engine's id is the preset's index). Both
clients embed the three strings verbatim (`FACTORY_PRESETS`, `FACTORY_FIRST`, `FACTORY_IDS`: a JSON object of
strings and numbers is a JS and a Python literal), and its `--check` fails unless each client contains all three,
so a renamed or added factory preset fails the suite until they are pasted again. Since 3y80 it is built with P5, as
the unit is: P5's names are the shipped list's (`assets/prophet5-factory/shipped.txt`), so a changed list fails the
check until both clients have it.

Engines (`ENGINES[]`, append-only; the retired ones are never produced by this firmware but a record may carry them):

| engine | name | patch kind | engine | name | patch kind |
| --- | --- | --- | --- | --- | --- |
| 0 | ANALOG (retired in 0.14: loads on the VA) | – | 8 | GRAIN (0.22: over its own material; the pool only) | `grain` (64 bytes; version 1, 20 bytes, loads) |
| 1 | DIGITAL (retired: loads as FM6) | – | 9 | PHYS (retired in 0.18) | – |
| 2 | PHASE (retired in 0.18) | – | 10 | DRUM (retired) | – |
| 3 | LOFI (retired in 0.18) | – | 11 | NOISE (retired in 0.18) | – |
| 4 | SAMPLE (retired) | – | 12 | FM6 | `fm6` (128 bytes) |
| 5 | VOICE (retired in 0.18) | – | 13 | VA | `va` (110 bytes) |
| 6 | TRIO (retired in 0.18) | – | 14 | CZ-1 | `cz` (144 bytes) |
| 7 | WHEEL | – | 15 | FM TONE | `quad` (80 bytes) |
| | | | 16 | P5 (0.22; the pool only) | `p5` (138 bytes) |

A record of a retired engine keeps its slot, its name and its engine number: the clients list it with the engine's
name and `retired` (`--sounds`: `U17  BELL          PHASE   retired   +`; the installer page's table `PHASE · retired`;
the device manager: a row of the VA's pool with `PHASE · retired`), export it and import it as it is; the FM-1 plays
a stand-in for it (docs/PRESETS.md). `engine_retired` / `engineRetired`: the engine has no factory table on this
firmware (`FACTORY_PRESETS`, which `tests/sound_templates.c` prints for the engines with `eng_ok`).

### The VA store (`va_store_t`, 3536 bytes)

```
0   u32  magic   0x31534156  "VAS1"
4   u16  ver     3 (2 is read too: the same size, older blobs; the FM-1 converts it at boot)
6   u16  nslot   32
8   u32  used    bit k: slot k + 1 holds a patch
12  u16  blob    110
14  u16  rsv     0
16  32 x 110 bytes: patch k
```

A VA blob: byte 0 = 'V' (0x56), byte 1 = the version 1..5 (5: a matrix slot in tenths, docs/VA.md), then the values; the FM-1 checks every value's range
(`va_blob_ok`). The page and the CLI check the magic byte, the version and the length only; the FM-1's commit is the
full check (rc 2: nothing written).

### The FM6 store, one half (`fm6u_t`, 2064 bytes) and the CZ-1 store, one half (`czu_t`, 2320 bytes)

```
0   u32  magic   FM6: 0x55364D46 "FM6U"      CZ-1: 0x55315A43 "CZ1U"
4   u16  ver     1
6   u16  nslot   16
8   u16  first   0 for half 0 (slots 1..16), 16 for half 1 (slots 17..32)
10  u16  blob    FM6: 128                    CZ-1: 144
12  u32  used    bit k (k < 16): slot first + k + 1 holds a blob; bits 16..31 must be 0
16  16 x blob bytes: blob k
```

An FM6 blob: 128 bytes, bytes 112, 113 = 'F' (0x46), 1 (the FM-1 checks the voice, `fm6_blob_ok`). A CZ-1 tone: 144
bytes (the FM-1 checks it, `cz_patch_valid`). The clients check the lengths and FM6's magic only.

### The FM TONE store (`quad_store_t`, 2576 bytes; docs/QUAD.md)

```
0   u32  magic   0x53445551  "QUDS"
4   u16  ver     1
6   u16  nslot   32
8   u32  used    bit k: slot k + 1 holds a patch
12  u16  blob    80
14  u16  rsv     0
16  32 x 80 bytes: patch k
```

An FM TONE blob (`eng_quad.c` `quad_pack`): byte 0 = 'Q' (0x51), byte 1 = the version 3 (0.14: 73 values, the
Digitone manual's algorithms and ranges, B2 KEY), then a byte per value (the value minus its minimum: 0..200, so **not
7-bit clean**: the backup protocol's pack7 carries it), then zero bytes to 80. Versions 1 and 2 (0.14 dev builds') are
still read: the FM-1 converts them. The FM-1 checks every byte (`quad_blob_ok`: the ranges, the zero padding); the
clients check the magic, the version (1..3) and the length. A
FM TONE sound's PAN and DRIVE are the record's track parameters, not in the blob. **No .syx** for FM TONE (no standard
format): the page shows no Export .syx button on an FM TONE row (it offers one for FM6 / CZ-1 patches only) and
`exportSyx` / `--export-syx` refuse the slot.

## The slot table

From the seven objects (eight with the FM TONE store), 32 slots (1..32). A slot is **used** when its record is valid. Its patch is the blob of the
store that matches its engine (VA 13 -> store 9, FM6 12 -> store 10 / 11, CZ-1 14 -> 12 / 13) when that store's `used`
bit is set; a used slot of such an engine without its blob plays with the engine's defaults (the FM-1 loads the init
patch or its PTCH / BANK slot: a sound saved before the store existed, or restored without it). A blob in a store whose
slot's record is empty or of another engine is stale: ignored on read, cleared on the next write of that slot. Per slot
the table gives: the number, used, the name (as stored; the FM-1 shows it upper case), the engine number and name,
`patch` = `va` / `fm6` / `cz` / none.

## The sound file (`choralroot-sound`, version 1)

```json
{
  "format": "choralroot-sound",
  "version": 1,
  "firmware": "ChoralRoot 0.14",
  "created": "2026-10-07T12:00:00Z",
  "slot": 5,
  "name": "MY PAD",
  "engine": 13,
  "engineName": "VA",
  "binding": null,
  "record": "<base64: the 192-byte record>",
  "patch": { "kind": "va", "data": "<base64: the blob>" }
}
```

- `record` is the truth: the 192 bytes as the FM-1 stores them (`used` 0xA5 and all). `name` and `engine` /
  `engineName` are decoded from it for people and tools; `slot`, `firmware`, `created` are informational; so is
  `binding` (the record's binding bytes decoded: `{"overwrites": <pool position>, "name": <factory preset>}` or
  `null` for an added preset, "The binding" above; files without it read the same).
- `patch` is `null` for an engine without a patch kind, or when the slot has no blob; else `kind` must be the engine's
  kind of the table above and `data` the blob of that kind's length.
- **Import** takes `record` as the sound, after the validity test above, and takes **`name` from the JSON** when it
  is present (1..12 ASCII 32..126; it is written into the record): editing the name in the file renames the sound.
  A missing or null `name` keeps the record's. `engine` in the JSON, when present, must equal the record's.
- The file name on export: `choralroot-sound-U05-MY_PAD.json` (`U` + the two-digit slot, then the name with every
  character outside `A-Za-z0-9-` replaced by `_`).
- A `felucca-backup` file (the whole backup) is not a sound file; the page and the CLI say so.

## Operations (what is written)

Every operation starts from a fresh read: `LIST`, then `GET` of ids 6, 7, 9, 10, 11, 12, 13 and 22 when listed (each object's CRC
compared with the list's; a difference = the FM-1 changed: read again). It then edits the objects in memory and writes
**only the objects whose bytes changed**, each as the restore does (`PUT` begin / data / commit; rc 3 = busy: the loop
plays on the FM-1, wait a second and send it again; rc 2 at a commit = the FM-1 refused the content: report it, write
nothing more; a failed data chunk: abort). A `PUT` begin ends the `LIST` snapshot, so the writes come after all the
reads. **Order: the stores first, the bank last**, so a refused patch leaves the slot's record as it was.

| operation | the bank | the stores |
| --- | --- | --- |
| **export** slot k | the record | the blob of the slot's engine kind, if its bit is set |
| **import** file into slot k | the file's record (name from the JSON if given), `used` 0xA5 | the file's patch into the store of the engine's kind at slot k (bit set); the other kinds' bit k cleared and their bytes zeroed (as `va_store_saved` / `fm6u_saved` / `czu_saved` / `quad_store_saved` do on the FM-1); the stores in the order 9 10 11 12 13 22, the bank last |
| **rename** slot k | `name` rewritten (zero-padded), nothing else | – |
| **delete** slot k | the record zeroed (192 x 0) | bit k cleared and the bytes zeroed in all four kinds (VA FM6 CZ-1 FM TONE) |

An empty bank or store object (size 0) that needs a slot written is created from the header above with every other
slot empty. A bank is written whole (3080 bytes, one sector erase on the FM-1, ~45 ms of silence); so is a store.

After the writes the page and the CLI read the objects again and show the table from flash. The FM-1 keeps playing
through all of it; a part playing the slot that was changed keeps the sound it has in RAM until it is loaded again
(PRESETS / ALGORITHM); the PRESETS list refreshes at once (`up_gen`).

## The page (`web/index_pkg.html`, section "Sounds")

Below "Back up and restore". The look is the installer's (cream on black, the mod stripes, Inter Tight; no new
assets; the buttons as the page's). **Read sounds** connects (Web MIDI, the FM-1 found as for a backup), requires a
ChoralRoot firmware (`deviceInfo` family `choralroot`; else the status says the Sounds need ChoralRoot), reads the
objects and shows the table: one row per slot, `U01`..`U32`, the name (empty: "empty", dim), the engine name, the
patch kind, the preset it is in the pool ("over FM6 02 FM BELL" or "added", "The binding"), and the row's buttons: **Export** (downloads the file), **Import** (a file picker for that row; a used
slot asks to confirm the overwrite), **Rename** (a prompt, 1..12 characters), **Delete** (confirm). Writes lock the
installer's other buttons as a restore does, show the status ("Writing U05 …", the busy text while the loop plays),
and refresh the table. Every error goes to the status line and the log as the page's other errors do. The section's
text says what a sound file is and that Back up covers everything at once.

## The device manager (`web/manager.html`, docs/DEVICE-MANAGER.md)

The same objects and edits in a two-pane page (the FM-1's pools per engine and loop slots; a library). Additions to
`web/fm1sounds.js` for it: `placeSound(objs, slot, sound, f)` imports with the binding set as SAVE > Overwrite sets it
(`note[15]` 0xA6, `flags[15]` f + 1, or 0 for an added sound), `bindSound` moves a slot's binding, `swapSounds`
exchanges two slots (in one pool the binding bytes stay with the slots, so the sounds exchange their places),
`firstFreeSlot`, `rawBindings` (per slot the factory preset its binding bytes name, before the first-bound-slot-wins
rule: a Restore unbinds a later slot naming the same preset, 3y79). Delete all / Reset to factory send each pool bank
that holds a sound once as a PUT of length 0 (the FM-1 writes it empty), DEVICE-MANAGER.md "Restore factory presets,
Delete all, Reset to factory". The factory sounds of every engine as sound files: `web/packs/` (`tools/make_packs.py`).

## The CLI (`tools/fm1_install.py`)

| option | does |
| --- | --- |
| `--sounds` | prints the 32 slots: `U05  MY PAD        VA      patch     +` / `U06  (empty)` / `U07  TINE 2        FM6     no patch  over FM6 02 FM BELL` (`+`: an added preset) |
| `--export-sound N FILE` | slot N (1..32) to FILE (a directory: the file name above); an empty slot is an error |
| `--import-sound N FILE` | FILE into slot N; asks before overwriting a used slot (`--yes` skips the question) |
| `--rename-sound N NAME` | renames slot N |
| `--delete-sound N` | empties slot N (asks; `--yes` skips) |

These go alone or together with `--port`; not with a package, `--backup`, `--restore` or `--info`. They need a
ChoralRoot firmware (a Felucca firmware has the banks but not the stores: refused with a message). Exit codes are the
installer's (`EXIT`: a bad file = 2, the FM-1 not found = 3, …; a refused write = the backup error code).

## .syx export and import (FM6 and CZ-1 slots; the user's pick from Melodee 0.12)

Beside the JSON sound file, an FM6 slot's voice and a CZ-1 slot's tone travel in the standard SysEx files the rest of
the world uses (Dexed / DX7 editors, Casio tools), so patches can be exchanged. The page gets an **Export .syx** per
FM6 / CZ-1 slot and the Import picker accepts `.syx` files; the CLI gets `--export-syx N FILE` and `--import-syx N
FILE`. The JSON file stays the complete sound; a .syx carries the engine's patch only, and an import builds the rest
of the record from a **template** (below).

### FM6: the DX7 single voice

| direction | bytes |
| --- | --- |
| **export** | the slot's 128-byte blob -> `fm6_unpack7` (112 -> 128: the VMEM packed voice, eight 7-bit bytes in seven, `eng_fm6.c`) -> the 155-byte VCED (the DX7 unpacking Dexed does: per operator 17 packed -> 21 unpacked bytes, then the pitch EG, algorithm, feedback/OKS, LFO, transpose, the 10-character name) -> `F0 43 00 00 01 1B <155> <sum> F7` (163 bytes; the checksum is the two's complement of the 155 bytes' sum, 7 bits; `web/EDITOR_PROTOCOL.md` "FM6 patches"). The function settings (blob bytes 114..121) have no place in a DX7 voice and are left out. |
| **import** | `F0 43 0n 00 01 1B <155> <sum> F7` (a single voice; the channel nibble ignored; the checksum checked) or `F0 43 0n 09 20 00 <4096> <sum> F7` (a 32-voice bank: voice 1 unless the page's / CLI's voice number says which) or a raw 155 / 4096-byte file -> VMEM 128 (packed, every byte clamped into its range as the firmware's `fm6_pack` does) -> `fm6_pack7` -> blob bytes 0..111, `'F'`, 1 at 112, 113, the **function defaults** at 114..121 (`FM6_FNDEF` packed: the fixture `fm6_fn` below, 8 bytes), zeros at 122..127. |

### CZ-1: Casio's tone dump

| direction | bytes |
| --- | --- |
| **export** | the slot's 144-byte tone -> `F0 44 00 00 70 30 <288 nibbles> F7` (295 bytes): each byte as two 7-bit bytes, the **low nibble first** (`cz_store.c`, docs/CZ1.md), no checksum. |
| **import** | the same frame (any channel nibble `7n`), 288 nibbles -> 144 bytes, validated as the firmware will (`cz_patch_valid`: the clients check the length and the frame only); a CZ-101 / 1000 **128-byte** tone (256 nibbles) is **refused** with a message (the firmware converts those only live, over MIDI); a file with several frames: the first unless a number is given; a raw 144-byte file is taken too. |

### The template: the rest of the record

A .syx has no ChoralRoot record, so the importer makes one from a per-engine **template**: a valid ver-4 record of
that engine with every parameter at the firmware's default (`param_desc_of(engine, i)->def`), `np` = P_COUNT, no
pattern, named `SYX IMPORT`; the importer writes the voice's / tone's own name over it (the DX7 name is VCED bytes
145..154, 10 characters; the CZ-1 LCD name is tone bytes 128..143, 16 characters: trimmed of spaces, characters
outside ASCII 32..126 replaced by a space, cut to 12; empty -> `FM6 VOICE` / `CZ TONE`), then imports it as a sound
of that engine with the blob / tone as its patch (the operations table above). The record gets the binding mark with
`flags[15]` = 0 (byte 175 = 0xA6, byte 191 = 0): an added preset, listed after the engine's factory presets. The firmware maps a record's values by
count (upreset.c), so a template stays valid when P_COUNT grows.

The templates are **generated from the firmware**, never typed: `tests/sound_templates.c` (built on the emulator's
firmware as `cr_backup_test.c` is) prints them with the fixtures as JSON:

```
cc -std=gnu11 -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/sound_templates tests/sound_templates.c -lm
./build/host/sound_templates                                              # {"templates": {"fm6", "cz", "quad"}, "fixtures": {..., "quad_blob"}, "factory": .., "factory_first": .., "factory_ids": ..}
./build/host/sound_templates --check web/fm1sounds.js tools/fm1_install.py   # the embedded templates and factory names are the firmware's
```

`web/fm1sounds.js` and `tools/fm1_install.py` embed the two base64 strings as constants (`SOUND_TEMPLATES` /
`SOUND_TEMPLATES`), and `tests/run_cr_tests.sh` runs the `--check`, so a firmware change of a default or of P_COUNT
fails the suite until the constants are pasted again. Fixtures for the clients' tests: `fm6_blob` (factory F1's
blob), `fm6_vced` (the same voice as 155 bytes), `fm6_fn` (the packed function defaults), `cz_tone` (Casio's A-1
BRASS 1, 144 bytes); at 2026-10-07 P_COUNT is 91.

### File names and the page

Export names: `choralroot-sound-U05-NAME.syx` (as the JSON's, the extension apart). On the page each FM6 / CZ-1 slot
row gets an **Export .syx** button beside Export; the row's Import accepts `.json` and `.syx` (the kind by content:
a JSON object, else SysEx `F0`, else a raw voice / tone by length); a bank .syx asks which voice (a prompt, 1..32).
The CLI: `--export-syx N FILE`, `--import-syx N FILE [--voice V]` (V 1..32 for a bank file, default 1); a .syx of the
other engine's kind or of an engine the slot is not is simply the file's engine: the slot becomes that engine.

### .syx decisions (2026-10-07)

- The clients sanitise a voice into the firmware's ranges first (`fm6_sanitize` / the JS mirror), then pack: `fm6_pack`
  itself only masks bits. A single voice comes back from the parser as in the file; bank voices come back unpacked and
  clamped, as the firmware would hold them.
- A raw 128-byte file is refused with the generic size message (it could be a packed DX7 voice as well as a CZ-101
  tone); only a framed 256-nibble Casio dump gets the CZ-101 / 1000 message.
- Several frames in one file concatenate (a bank plus a voice: 33); a file mixing DX7 and Casio frames is refused. The
  page refuses any other frame; the CLI skips frames of other manufacturers as long as one usable frame is in the file.
- Names: characters outside 32..126 become spaces, the name is trimmed, cut to 12, trimmed again; blank -> `FM6 VOICE`
  / `CZ TONE`.
- On hardware (2026-10-07): `--export-syx 2` (TINE EP, 163 bytes), `--import-syx 3` of it, `--export-syx 3` byte-identical
  to the first file, the slot's JSON export equal to U02's blob including the function defaults, then `--delete-sound 3`.

## .dnsnd import (Elektron Digitone sounds as FM TONE sounds; 2026-10-14)

**Since FM TONE version 4 (2026-10-14, docs/QUAD.md "Ranges")** the hi-res values map 1:1 with the fine byte kept:
FM TONE value = coarse x 100 + round(fine x 100 / 256) (minus 6400 / 6300 for the centred ones); FDBK unscaled; the
blob written is version 4 (96 bytes). `fm1sounds.js` and `fm1_install.py` read 96-byte FM TONE patches and take an
80-byte version 1..3 one (a sound file, a pool slot or store of before: converted, fm1dnsnd.js `quadToV4`, or zero-padded
for the FM-1 to convert); a firmware before the pool gets version 3 in its 80-byte store (`quadToV3`).

**A sound placed on a firmware before the pool (0.21 and older)** is converted down to what that firmware reads, and
the result says what it will not have (`importSound`'s `notes`, one line each: the device manager's status line after
Place / Place all / Replace factory, the installer page's message and log after Import). FM TONE (`fm1dnsnd.js
quadToV3`): the decimals rounded, FDBK 0..127 -> 0..120, AMP DEC INF stays 127 (there the longest decay), and an LFO
destination added in 0.22 (40..57: 0.21's `quad_blob_ok` takes 0..39 and would refuse the whole patch, which then plays
INIT) becomes NONE. The times keep their numbers: 0.21 plays them on its old 1 ms .. 10 s curve, not on 0.22's
Digitone tables ([QUAD.md](QUAD.md) "Times"), so most come out much shorter there; no note says so (the release notes'
"Going back to 0.21" does). VA (`fm1sounds.js vaToV3`, version 4 or 5 -> 3, the same 110 bytes; version 5's fine matrix amounts the nearest whole number, its flags 0): FENV's hundredths rounded into the
whole number (value 103 back to 0), and a synced LFO's RATE (an index into the 42 rates) becomes the nearest of
version 3's 14 divisions by length (the ms rates, faster than all: 1/32) as the smallest 0..127 RATE version 3 reads as
that division (RATE x 14 >> 7); the 14 themselves come back exactly (`VA_DIV3_TO_SYNC`). P5 sounds are refused there
(no P5 store). Tests: `web/test_dnsnd.mjs` (v5 -> v3), `web/test_sounds.mjs` (a VA version 4, and a version 5, into 0.21's objects).

The owner: "Being able to load in Digitone presets for the FM-Tone engine in the device manager" ... "Build now, I do
have a Digitone we can refine later." FM TONE (engine 15, docs/QUAD.md) follows the Digitone's FM voice, so most of a
Digitone sound has a place in its patch. The decoding is ours (clean-room, from exported files; Elektron publishes no
spec), the files are read **on this computer**: nothing is uploaded. No Elektron sound, value or name is in this
repository; the tests build their `.dnsnd` with our own encoder and made-up values.

**Where.** The device manager's library (Add files: `.dnsnd`, several at once; each becomes a library item, an FM TONE
sound, with its notes on its row; Place writes it, as any library item: to a chosen slot or each to the next free one);
the installer page's Sounds section (Import on a row: a `.dnsnd` always asks first, listing the notes); the host:
`node tools/dnsnd_to_sound.mjs IN.dnsnd OUT.json` makes a sound file (and prints the notes) that `fm1_install.py
--import-sound N OUT.json` writes (the CLI has no `.dnsnd` option of its own: the field table stays in one place,
`web/fm1dnsnd.js`).

**Digitone SysEx dumps (.syx, 3y200).** The same two places take a Digitone `.syx`: one sound, the pool's 128 or a
whole project's dump (the device manager: each sound a library item with its notes; the installer page: asks which
one). `fm1dnsnd.js parseDnSyx`, decoded from the Dissonant World pack's `DW_Play.syx` (128 messages of 339 bytes,
each the same sound as the pack's own `.dnsnd`: all 123 that have one give the same FM TONE blob): `F0 00 20 3C`
(Elektron) `0D` (Digitone) `00 53` (a sound) `01 01 nn`, the data 8:7 packed (a byte of the next seven's top bits,
bit 6 first, then the seven), the checksum (the packed bytes' sum & 0x3FFF, 7 + 7 bits), the length (the bytes after
`nn` up to `F7`, 7 + 7 bits), `F7`. The data is the record below without the LZ4 (a version-1 record 283 bytes, its end
marker at 279 as in the factory files; read as any record is, below). A wrong checksum or length is read with a note; the
project's other messages (kits, patterns, songs, settings) are skipped and counted in the first sound's notes (a
kit's own sounds are not read: their layout is not decoded). A `.syx` holds no tags or firmware version. The CLI
(`fm1_install.py --import-syx`) still takes DX7 and CZ-1 dumps only.

**Record version 4** (3y200, from FIZZ b67df20): a sound saved on Digitone OS 1.31 / 1.42 (payload header "0095",
the Dissonant World pack's `.dnsnd`) has version 5's layout byte for byte and is read as version 5 with a note ("a
version-4 sound: read as version 5"); it was refused as "a newer Digitone format (version 4)".

**Any other record version** (3y201, the owner: "Presets with a newer format should still import."): no file is
refused for its version number. A version the importer does not know (2, 3, 6 and up) is read with the newest layout
(version 5's) and one note naming it: "a version-7 sound (newer than the versions known, 1 4 5): read as version 5;
fields the FM-1 does not know were skipped" ("not one of the versions known" for 2 / 3). The parameters keep their
slots at 28 + 2 x id (the law the Digitone II kept for its own longer record, FIZZ `research/DN2-FORMAT.md` §3: what
grows is the tail), so the head reads in place; the tail (the sound's OCTAVE) is found from the record's end marker
`ba ce f0 0c`, 18 bytes before it as in version 5; bytes past the fields known are skipped. A version-3 dump of 2018
(159 sounds of 282 bytes, the end marker at 278) bears it out: version 5's head to 182 (its constants at 170 / 172, the
ratio offsets 64-centred), and 18 before the marker only multiples of 12 (0, -12, -24, -36, +12), an OCTAVE's values.
**A record cut short** (no end marker, shorter than its layout: `Revealed.syx` holds ten 113-byte messages, their
checksum and length wrong) gives the fields it holds; the others stay at FM TONE's defaults, with a note ("the sound is
cut short (113 bytes, no end marker): 22 of its settings not in it, FM TONE's defaults instead"; before, the missing
bytes were read as zeros: LEVEL 0, the filter shut). Still refused: a file whose framing is not a Digitone sound (not a
ZIP, no manifest, not a Sound, the payload's or the record's magic, a damaged LZ4 chain, a body the header's length
does not hold) and a **Digitone II** sound (the payload's device 15, or a body with its 5-byte prefix: its parameters
are numbered differently, FIZZ reads them).

**The file** (`web/fm1dnsnd.js parseDnsnd`). A ZIP (stored or deflated: `DecompressionStream("deflate-raw")` in a
browser, zlib in node) of `manifest.json` (`FileType` "Sound", `Payload` the payload's name, `FirmwareVersion`,
`ProductType`, `MetaInfo.Tags`) and the payload: a 0x1f-byte header (magic `ac 11 d3 03`, the device u16be at 7: 9
the Digitone, 15 the Digitone II; the saving OS's build in ASCII at 9: "0051" 1.31, "0095" 1.42, "0097" 1.44; the
body's length u32be at 0x19; the compressed flag at 0x1d), the body (flag 1: an LZ4 chain, a u32be size before each
block, a u32be 0 after the last; flag 0: the bytes as they are), a 12-byte trailer (a CRC, a length, `aa a1 da aa`;
FIZZ `research/DN2-FORMAT.md` §2; before 3y201 the length was read as a u16le at 0x1c, the length's low byte and the
flag: 302 by luck). The record, 302 bytes: `be ef ba ce`,
u32be version, a u32 of tag bits, the name (16 bytes) at 12, then the parameters as byte pairs from 30 (the coarse
value; the second byte is the fine part on the hi-res ones). **Record versions**: 1 (the factory sounds, the end
marker `ba ce f0 0c` at 279) and 5 (a sound saved on the owner's Digitone, OS 1.44, the manifest still "1.42A"; the
marker at 298). Both have the same slots up to 168; version 5 adds 170..182 (the ratio offsets, the filter's ENV
DELAY; zeros in version 1) and its bytes 270..297 change from save to save (not parameters). Refused with the reason:
not a ZIP, no manifest, not a Sound, no payload, another payload magic, a body length no sound has, a Digitone II
sound, the record's magic, a damaged LZ4 chain. Read with a warning: a record version other than 1, 4, 5 (as version
5, above), a record cut short, a firmware other than 1.42A, a ProductType other than `["24","30"]`, an unusual trailer.

**Slot = LFO destination.** Slot s is the byte pair at offset 28 + 2 s, and LFO DEST number s modulates the parameter
in slot s (every destination export proves one pair: DEST 17 ALGO is offset 62, DEST 51 FILTER FREQ offset 130, LFO
2's DEST 1 LFO 1 SPEED offset 30). That places fields no single export turned ("slot" below).

**The field table** (`DNSND_FIELDS`: name, offset, hi-res, confidence, decode, the FM TONE value it sets, the first
record version that has it). Confidence: **verified** (a controlled export of the owner's changed exactly this field:
the offset and the encoding are proved; 2026-10-14), **slot** (the offset proved by the destination numbering, the
encoding inferred), *inferred* (the corpus's ranges, the manual), *guess*, *unknown* (decoded for the diff tool, not
imported).

| offset | Digitone | conf. | FM TONE (eng_quad.c) |
| --- | --- | --- | --- |
| 30 / 32 | LFO 1 / 2 SPEED (hi-res, 64 = 0: 0 = -64, 127.99 = 63.99) | slot / **verified** | SPEED 1:1 (hundredths) |
| 34 / 36 | MULT 0..23 (0 = 1, 5 = 32, 21 22 23 = 512 1k 2k) | slot / **verified** | MULT, the same 24 (0..11 the tempo, 12..23 120 BPM: which half is which is not proved) |
| 38 / 40 | FADE (64 = 0) | slot | FADE |
| 42 / 44 | DEST (the table below) | **verified** | DEST by `DN_LFO_DEST` (none: off, a note) |
| 46 / 48 | WAVE TRI SINE SQR SAW EXP RMP RND (the manual's order) | slot | the same waves (EXP / RAMP swap places) |
| 50 / 52 | SPH (start phase) | slot | PHASE |
| 54 / 56 | MODE FREE TRG HLD ONE HLF | slot | TRIG, the same five |
| 58 / 60 | DEPTH (hi-res, 64 = 0) | slot | DEPTH 1:1 (hundredths) |
| 62 | ALGO (0..7) | slot | ALGO 1..8 |
| 64 | RATIO C, 19 steps (0 = 0.25, 3 = 1, 4 = 2, 18 = 16) | **verified** | RATIO C, the same step |
| 66 | RATIO A, 36 steps (0 = 0.25, 3 = 1, 9 = 2.5, 15 = 4, 25 = 8, 35 = 16) | **verified** (the six steps) | RATIO A, the nearest quarter (`DN_RATIO_A`: 0..15 in quarters fits all anchors; 16..24 and 26..34 linear between them, **not proved**: a note) |
| 68 | RATIO B: u16be = 2 x (B1 step x 19 + B2 step) + a low bit | **verified** (four exports) | QP_RB1 / QP_RB2, the same pair (FM TONE's deep value B1 x 19 + B2). The low bit (set in 103 of the 256 factory sounds, and by the RATIO A exports) is not known: ignored |
| 70 | HARM (hi-res, 63 = 0: 37 = -26, 89 = +26) | **verified** | HARM 1:1 (-26.00..+26.00) |
| 72 | DTUN (hi-res 0..127) | **verified** | DTUNE 1:1 (hundredths) |
| 74 | FDBK, stored 0..127 (the display's 20 = 20, MAX = 127) | **verified** | FDBK 1:1 (FM TONE 0..127 since 2026-10-14) |
| 76 | MIX (0 = -64, 64 = 0, 127 = +63) | **verified** | MIX |
| 78 | PHRT 0..4 (OFF ALL C A+B A+B2?) | inferred | PHRT, the same five |
| 82..88 / 90..96 | ENV A / B ATK DEC END LEV | **verified** | ENV A / B: ATK DEC END LEV 1:1 (the times are the Digitone's: below) |
| 98 / 104 | A DLY / B DLY | slot | A DLY / B DLY |
| 100 102 106 108 | A TRIG, A RESET, B TRIG, B RESET | inferred | ENV 2 / ENV 2+ |
| 128 | filter TYPE: version 5 0 OFF, 1 LP2, 2 HP2, 3 LP4 | **verified** (version 5) | TYPE 1:1 (= the manual's order = FM TONE's) |
| 128 | filter TYPE: version 1, five values 0..4: 0 OFF, 1 LP2, 2 HP, 3 LP4, 4 LP4 | **verified** 2 3 4 (the owner's re-saves on OS 1.44), inferred 0 1 (continuity with version 5) | OFF, LP12, HP12, LP24, LP24 (a note only for a value past 4) |
| 130 / 132 | FREQ / RESO (hi-res) | **verified** | FREQ / RESO 1:1 (hundredths) |
| 134 | ENV depth (hi-res, 64 = 0) | **verified** | FILTER DEPTH 1:1 (hundredths) |
| 136..142 | filter ATK DEC SUS REL (DEC and REL 127 = INF) | **verified** | FILTER ATK DEC REL 1:1 (the times: below), SUS direct (DEC INF: SUS 127, a note; REL INF stays 127) |
| 144 / 146 | BASE (hi-res) / WIDTH | slot | BASE 1:1 (hundredths) / WIDTH |
| 148..152 | AMP ATK DEC SUS (DEC 127 = INF) | **verified** | AMP ATK DEC 1:1 (the times: below), SUS direct; DEC INF -> AMP DEC 127 = FM TONE's INF (version 5: the level held, no decay; until then SUS 127 and a note) |
| 154 | AMP REL (hi-res, 127 = INF) | **verified** | AMP REL the hi-res value rounded to a whole number (version 5) but at most 126 below 127.00 (10.13 -> 10); 127.xx INF = 127 |
| 156 | DRIVE (hi-res) | **verified** | the record's DRIVE (P_DIST), rounded |
| 158 | PAN (0 = L64, 64 = 0, 127 = R63) | **verified** | the record's PAN (P_PAN) |
| 160 | VOL (hi-res; init 110) | **verified** | LEVEL 1:1, rounded to a whole number (version 5: 110.40 -> 110) |
| 162 / 164 / 166 | REVERB / DELAY / CHORUS send (hi-res) | **verified** | not imported (a note when not 0) |
| 168 | AENR (version 5: 1 on, 0 off; version 1 holds 0..15: something else) | **verified** (version 5) | none (a note when off) |
| 174..180 | RATIO OFFSET C, A, B1, B2 (hi-res, 64 = 0; version 5) | slot (the scale not proved: +-64 read as +-1.00) | OFS C A B1 B2 (a note when not 0) |
| 182 | the filter's ENV DELAY (version 5) | slot | FILT 2 DELAY |
| 263 (version 1) / 280 (version 5) | the sound's OCTAVE (the manual §9.5.10), a signed byte in semitones | inferred (below) | every ratio x 2^(octave / 12) when FM TONE's tables hold the result, else a note |
| 110, 170, 172, 186..262 | ? (110: 127 or 29; 170 / 172: 63 / 39 in the init sound; 262: 2 in 251 factory sounds, 0 in version 5's 279 = 2: maybe PITCH BEND DEPTH) | unknown | - |
| 112..126 | the slots of PITCH ALL .. AB DELAY (LFO-only targets): always 64 | - | - |

The old table's mistakes (dn-1, inferred without ground truth): the AMP block was one slot off (it read HOLD at 150,
DEC from SUS, SUS from REL and REL from the chorus send; there is no HOLD), the DRIVE was read as the delay send and
the delay and chorus sends were swapped, RATIO B was not found (always 1/1), RATIO A's table was wrong from step 8 up
(step 9 read 3.00, is 2.50; step 25 read 8.50, is 8.00), no LFO destination was mapped, and the owner's own version-5
exports were refused.

**LFO destinations** (`DN_LFO_DEST`, `DN_LFO_DEST_NAMES`; each number from one export, C042..C099, names as the record
holds them; the number is the target's slot) -> FM TONE `QD_*`, **1:1 since 2026-10-14** (FM TONE has the Digitone's
whole list, HISTORY 3y59; its editor shows them in the Digitone's order, docs/QUAD.md "LFOs"):

| Digitone | FM TONE | Digitone | FM TONE | Digitone | FM TONE |
| --- | --- | --- | --- | --- | --- |
| 0 none | NONE | 31 B ATTACK | B ATK | 54 FILTER ATTACK | F ATK |
| 1 3 5 9 11 13 15: LFO 1 SPEED MULT FADE WAVE SPH MODE DEPTH (LFO 2 only) | L1SPD L1MUL L1FAD L1WAV L1SPH L1MOD L1DEP | 32 B DECAY | B DEC | 55 FILTER DECAY | F DEC |
| 17 ALGO | ALGO | 33 B END | B END | 56 FILTER SUSTAIN | F SUS |
| 18 RATIO C | RAT C | 34 B LEVEL | B LEV | 57 FILTER RELEASE | F REL |
| 19 RATIO A | RAT A | 35 A DELAY | A DLY | 58 FILTER BASE | BASE |
| 20 RATIO B | RAT B | 38 B DELAY | B DLY | 59 FILTER WIDTH | WIDTH |
| 21 HARM | HARM | 42 PITCH ALL | PITCH | 60..63 AMP ATTACK DECAY SUSTAIN RELEASE | AMP A D S R |
| 22 DETUNE | DTUN | 43 PITCH A+B2 | P AB2 | 64 DRIVE | DRIVE |
| 23 FEEDBACK | FDBK | 44 RATIO ALL | R ALL | 65 PAN | PAN |
| 24 MIX | MIX | 45..49 AB LEVEL ATTACK DECAY END DELAY | ABLEV ABATK ABDEC ABEND ABDLY | 66 VOLUME | LEVEL |
| 27 A ATTACK | A ATK | 51 FILTER FREQ | FREQ | 67 68 69 REVERB DELAY CHORUS send | REV DEL CHO |
| 28 A DECAY | A DEC | 52 FILTER RESO | RESO | 73..76 RATIO C A B1 B2 OFFSET | OFS C A, OFSB1, OFSB2 |
| 29 A END | A END | 53 FILTER ENV DEPTH | FENV | 77 FILTER ENV DELAY | F DLY |
| 30 A LEVEL | A LEV | | | | |

77: the export's record name is "LFO FILT ENV DEC" (the file name cut to "FILT ENV DE", as C075's ENV DEPTH); read as
the filter's ENV DELAY (the manual's Appendix C ends with "FILTER: Env. Delay", 55 is already FILTER DECAY and 77 is
slot 182, a version-5 field): an inference. The depth's full scale per destination is FM TONE's own (docs/QUAD.md
"LFOs"): copied 1:1, not compared.

**What stays approximate** (copied 1:1 or by our own scale; only listening to the owner's reference recordings can
settle these): the envelope times (one curve through two measured points, below), FREQ's cutoff curve and BASE / WIDTH's (FM TONE: 30 Hz x 533^(v/127)), FDBK's
amount (proportional, FM TONE's curve is ours), DTUN's amount (FM TONE follows the manual's description: slight to
64, wide above), MIX's law, VOL against LEVEL (FM TONE's LEVEL^2; the Digitone's init 110, FM TONE's 100), the LFO
depth per destination, HARM's fractions (rounded), the DRIVE's character (the FM-1's distortion), the ratio offsets'
scale, RATIO A's unproved steps, the version-1 filter types 0 and 1, PHRT (inferred), the sound's OCTAVE (inferred). The LFO rate matches by construction
(the manual's table: SPD x MULT = 128 is one bar = FM TONE's `QUAD_LFO_K`).

Every FM TONE value is clamped into the firmware's range (`DN_QUAD_RANGES` = `quad_range`; the test compares it with
`build/host/sound_templates --quad-ranges`, and every test blob passes the firmware's own `quad_blob_ok`). What the
Digitone file does not give keeps FM TONE's default (VEL 64, the KEY scalings 0, KTRK 0, LFO 3; version 1: OFS 0,
filter DELAY 0). Lost always: the arpeggiator, the multimap and the sends (global effects on the FM-1). The record is
the FM TONE template (`SOUND_TEMPLATES.quad`, pinned by `sound_templates --check`) with the name (cut to 12), the PAN,
the DRIVE and the macros P_E0..P_E7 set from the patch; it is added to FM TONE's pool (no binding).

On the owner's 256 factory files (a local check, not in the suite) with the corrected table: all parse, all 256 blobs
pass `quad_blob_ok`, every LFO destination is a number of the table, every RATIO B pair is in range (0..360); the
notes fell from 1325 (5.2 a sound) to 377 (1.5): the LFOs with a destination 349 of 371 mapped (were 0), 22 off (AB
LEVEL 7, RATIO ALL 5, LFO 1's parameters 10 on LFO 2, DRIVE 1). Since FM TONE has every destination (2026-10-14,
HISTORY 3y59): **371 of 371 mapped**, none off, notes 355, all 256 blobs pass `quad_blob_ok`; RATIO A between the proved steps 25; the version-1
filter types 63; the sends 263; DEC INF 4. The owner's 143 controlled exports (version 5): all parse (were refused),
all pass `quad_blob_ok`. With the version-1 filter types, the time curve and the OCTAVE (2026-10-14, HISTORY 3y60): all 256 parse, all
256 blobs pass `quad_blob_ok`, notes 397 (1.55 a sound): no filter-type notes (were 63), the OCTAVE 105 sounds (44
transposed by their ratios, 61 not: a ratio of 0.25 has no half, a 16 no double, or a quarter step's half needs a finer
OFS: they play their OCTAVE higher or lower than on the Digitone, with a note), the sends 263, RATIO A 25, DEC INF 4.
The 143 exports: all pass, notes 6 (none about the OCTAVE: theirs is 0).

**The SQB TK recording (2026-10-14).** The owner played "a C" on his Digitone (the factory A003 SQB TK, version 1) and
on the FM-1 (its import) at once (12 s, 44.1 kHz; local files, not in the repo). Measured (the note held 2.61 s):

| | Digitone | FM-1 (the old import) | host render, old import | host render, now |
| --- | --- | --- | --- | --- |
| lowest partial | 65.4 Hz (C2) | 130.8 Hz (C3) | 130.8 Hz (at MIDI 60) | 65.4 Hz (at MIDI 60) |
| harmonics of 65.4 Hz at 0.3 s (dB: 1st..6th) | 0 -16 -17 -32 -30 -43 | -80 -1 -78 -7 -80 0 | -114 0 -105 -6 -103 0 | 0 -15 -13 -25 -22 -36 |
| spectral centroid 0.1 / 0.3 / 0.6 / 1.0 / 2.0 s | 473 185 93 74 71 Hz | 3129 2456 2395 2399 2456 | 2709 2261 2220 2219 2219 | 448 181 123 109 106 |
| after the key's release, -40 / -60 dB | 49 / 71 ms | the dry sound ~3 ms, then a tail (the FM-1's effects) | ~2 ms | 48 / 68 ms |

Two causes, both in the import: **the filter type** (version 1's 4 was read as HP4 -> HP12: the high-pass at FREQ 42
removed the fundamental and left the even, bright partials; it is LP4, the owner's re-save) and **the sound's
OCTAVE** (-12 in SQB TK's record, not read: the Digitone plays it an octave below the key). The FM-1's recording
matches a render of the old import at MIDI 60 (its partials are those of 130.8 Hz; at MIDI 48 they would be those of
65.4 Hz with odd ones present), so the FM-1 got MIDI 60 for the owner's C, a single note (no chord tones in the
spectrum); with the OCTAVE the Digitone sounds MIDI 48. RATIO C 0.5 converts right: rendered at MIDI 48 with LP24
the old ratios give 65.4 Hz and the Digitone's harmonic profile within 3 dB up to the 12th (0.3 s); algorithm 6's
carriers (C and B1, MIX between X and Y) are the Digitone's. No engine change. What still differs: the closed
filter (from about 0.6 s the Digitone's centroid is 71..93 Hz, FM TONE's 106..123: FREQ 42's cutoff or LP4's slope
differ; FM TONE 30 Hz x 533^(v/127) = 238 Hz) and the level (below).

**Envelope times** (`DN_TIME_TO_QUAD`, `dnTimeQ` in fm1dnsnd.js): **1:1** since 2026-10-14. FM TONE's times are the
Digitone's, measured on the owner's unit ([QUAD.md](QUAD.md) "Times": a table per envelope type, the DELAYs v / 32
beats, the LFO FADE's times), so a Digitone time value is FM TONE's; the AMP REL's hundredths are rounded (at most 126
below 127.00, 127.xx INF). Before, a power law through two points of the SQB TK recording (AMP REL 10.13 -> FM TONE
52.5, the filter DEC 71 -> FM TONE 97) mapped them onto FM TONE's old 1 ms .. 10 s curve (HISTORY 3y60); a sound
imported then keeps those values, which now mean the Digitone's times of the larger numbers: import it again.

**Level.** The FM-1's recording of the old import against the host render of the same patch at MIDI 60 gives the
recording chain's gain (-10.2 dB over the held note). The new import's render through that gain would be about 7 dB
louder than the Digitone's recording at the note's onset (0.1 s) and about 10.6 dB over 0..2 s (the closed filter
keeps more on FM TONE). That suggests VOL 110 -> LEVEL about 73 (FM TONE's amplitude is LEVEL^2: -7 dB) for this sound,
but it is one sound, both recordings' gains are assumed equal, and the velocity (the render's 100) is not known: not
applied; LEVEL stays VOL 1:1 until a second recording (the sine above at VOL 110 and 60 on both units, at the same
interface gain, the same velocity) settles it.

### Refining the table (with the owner's Digitone)

Export a sound, change ONE parameter on the Digitone, export again, then compare:
`node tools/dnsnd_diff.mjs BEFORE.dnsnd AFTER.dnsnd` prints every byte of the record that differs, both values and
the field it belongs to (`?` for an unknown offset; an LFO destination by name; version 5's save-to-save bytes
270..297 marked). The record keeps the full 16-character name (the file name may be cut). Then edit the one line of
`DNSND_FIELDS` in `web/fm1dnsnd.js`. Still worth an export: RATIO A at steps 16..24 and 26..34 (a few: 17, 20, 30),
the LFO WAVE (EXP and RMP), MULT's two halves (the dotted ones), a ratio offset at its maximum, PHRT, A / B TRIG and
RESET, a version-1 sound's filter type re-saved on OS 1.44 (it shows its OS 1.44 type and the version-5 value).

## Decisions taken in the implementation (2026-10-07)

- **A version-2 VA store** (the same 3536 bytes, older blobs) is converted by the FM-1 at boot by reading every blob as
  version 2, so a version-3 blob placed in it would be misread: the clients refuse to import a VA patch into a
  version-2 store ("save a VA sound on the FM-1 once first", which rewrites the store as version 3); clearing a bit
  in such a store is allowed.
- **A CRC mismatch on read** (the FM-1 changed between LIST and GET) repeats the whole read (LIST and the seven GETs)
  once; a second mismatch is the error "the FM-1 changed during the read".
- **An import whose file has no patch** for a VA / FM6 / CZ-1 engine clears the slot's bit in all three stores, so no
  stale blob survives; clearing touches a store only where the bit is set (an already-clear slot causes no write).
- **Delete of an empty slot** writes nothing; an object that is not a valid bank / store reads as empty and is rebuilt
  from its header only when a sound or blob goes into it.
- **Names**: export writes `name: null` when the stored name is not printable ASCII (the record still imports); the
  page trims the rename prompt's answer; the CLI's listing has a 7-character engine column (`DIGITAL`), so a line reads
  `U05  MY PAD        VA      patch`, and engines without a patch kind print no third column.
- **CLI exits**: an empty slot on export or rename, or a file that cannot be written, exit 1; a refused or failed write
  names the object and lists the ones not written, exit 7; `nothing to write` when nothing changed.
- **Firmware builds without CZ-1** list no ids 12 / 13: the clients read the ids the FM-1 lists and refuse an
  operation that needs a missing store, naming it.
- **On hardware** (2026-10-07, ChoralRoot 0.14 on the user's FM-1): `--sounds`, `--export-sound 2`, `--import-sound 3`
  of that file (the FM6 store written, then the bank), `--rename-sound 3`, an export of U03 equal to U02 outside the
  name, `--delete-sound 3`; the FM-1 kept playing and needed no restart. The full backup (`--backup`) ran on hardware
  for the first time the same day.

## Not done

- **More than 32 slots.** 72 KiB of flash is free (0xB6000..0xC7FFF; 0xB2000 / 0xB3000 hold the FM TONE store, 0xB4000 / 0xB5000 the session since 0.18), but the stores are mirrored in the pool
  (`va_store` 3536, `fm6u` 2 x 2064, `czu` 2 x 2320 bytes) and the banks in RAM (2 x 3080); POOL is at 92 % and RAM at
  80 %. 64 slots would need a second VA store object (64 x 110 bytes exceed one object's 3840-byte payload), new halves
  for FM6 and CZ-1, wider `used` masks, the UI's `U01..U32` picker, new backup ids, and about 12 KB of POOL. Possible
  later; not part of this.
- The FM-1 shows no message when a bank is written from the computer (the PRESETS list simply changes).

## The sound pool (0.22)

ChoralRoot 0.22 keeps the user sounds in **128 slots shared by every engine** (docs/PRESETS.md "The pool"). Over the
backup protocol they are **objects 50..62**, the pool's 13 banks: 3840 bytes each (size 0: a bank never written), ten
slots of 384 bytes: the 192-byte record, at 192 the patch's engine + 1 (0: no patch), at 193 its length, the patch from
196 (VA 110, FM6 128, CZ-1 144, FM TONE 96 or an older slot's 80, as 0.21's sounds migrated, P5 138). A slot's patch
kind comes from the slot itself (`va`, `fm6`, `cz`, `quad`, `p5`), not from a store. A bank's commit checks each used
slot (a valid record; no patch, or an engine's patch of that engine's length: rc 2 else, nothing written) and writes
every other slot empty, 384 zeros, whatever was sent (3y78); a Delete sends the slot so, a placed sound starts from it.
The clients (`web/fm1sounds.js`, `tools/fm1_install.py`) see which an FM-1 has from its LIST: ids 50..62 (0.22: 128
slots, labels U001..U128) or the legacy ids 6, 7, 9..13, 22 (0.21 and before: 32 slots, U01..U32, unchanged). An
edit writes the banks it changed (one for a slot; a swap two). A sound file is the same format either way (version 1;
a P5 sound carries `"patch": {"kind": "p5"}`); a P5 sound cannot go to a 0.21 FM-1.
`--sounds`, `--export-sound` / `--import-sound`, `--rename-sound`, `--delete-sound` take slots 1..128 (a slot past the
FM-1's own is a usage error); `--favorites` / `--restore-favorites` name user slots `U05` .. `U128` (section 0x81 for
33..128). The device manager's **Import the 0.21 slots** sends 80 IMPORT (-> rc, count): the FM-1 copies its legacy
slots into U001..U032 again.
