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
2    u8   engine   0..14 (the table below)
3    u8   np       the parameter count stored (8..144 for ver >= 4, 8..72 before)
4    char name[12] ASCII 32..126, zero-padded (no terminator when 12 long); name[0] != 0
16   u8   packed[144] (ver >= 4: each value + 64, 0..191) / i16 p[72] (ver <= 3)
160  u8   note[16]
176  u8   flags[16]
```

A record is **valid** (`up_valid`) when used == 0xA5, 1 <= ver <= 5, engine < 15, 8 <= np <= (ver >= 4 ? 144 : 72),
name[0] != 0 and, for ver >= 4, every packed[i] for i < np is <= 191. The page and the CLI apply the same test: an
invalid record reads as an empty slot and is never written.

**The binding** (docs/PRESETS.md, firmware 0.14): ChoralRoot has no sequencer, so a record's pattern bytes are free;
two of them place the preset in its engine's pool. `note[15]` (byte 175) = **0xA6** marks a record the FM-1 saved
with a binding, and `flags[15]` (byte 191) is then the **index + 1 of the factory preset it overwrites** (the engine's
`presets[]`, FM6 1..25, CZ-1 2..65: its INIT TONE is the pool's INIT) or **0 for a preset added to the pool**. A record
without the mark (older firmware, a client's, a restore) is an added preset of its engine; so is a mark naming no
factory preset of the engine, or one an earlier slot already binds (the first bound slot wins). The pool of an engine:
00 INIT, its factory presets (each replaced by the slot bound to it), then the added records in slot order. The pool
of a record is its engine; a DIGITAL record (1) is in FM6's, another retired engine's in ANALOG's; a drum grid record
(ver 3 or 5) is never bound. Older firmware reads a bound record as an ordinary one (a note 38 on step 16 of a pattern
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
(`cr_bank.c` `pool_f0`: 1 for the CZ-1, whose preset 0 INIT TONE is the pool's INIT; else 0), each compact on one
line. Both clients embed the two strings verbatim (`FACTORY_PRESETS`, `FACTORY_FIRST`: a JSON object of strings and
numbers is a JS and a Python literal), and its `--check` fails unless each client contains both strings, so a renamed
or added factory preset fails the suite until they are pasted again.

Engines (`ENGINES[]`, append-only; the retired ones are never produced by this firmware but a record may carry them):

| engine | name | patch kind | engine | name | patch kind |
| --- | --- | --- | --- | --- | --- |
| 0 | ANALOG | – | 8 | GRAIN (retired) | – |
| 1 | DIGITAL (retired: loads as FM6) | – | 9 | PHYS | – |
| 2 | PHASE | – | 10 | DRUM (retired) | – |
| 3 | LOFI | – | 11 | NOISE | – |
| 4 | SAMPLE (retired) | – | 12 | FM6 | `fm6` (128 bytes) |
| 5 | VOICE | – | 13 | VA | `va` (110 bytes) |
| 6 | TRIO | – | 14 | CZ-1 | `cz` (144 bytes) |
| 7 | WHEEL | – | | | |

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

A VA blob: byte 0 = 'V' (0x56), byte 1 = the version 1..3, then the values; the FM-1 checks every value's range
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

## The slot table

From the seven objects, 32 slots (1..32). A slot is **used** when its record is valid. Its patch is the blob of the
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

Every operation starts from a fresh read: `LIST`, then `GET` of ids 6, 7, 9, 10, 11, 12, 13 (each object's CRC
compared with the list's; a difference = the FM-1 changed: read again). It then edits the objects in memory and writes
**only the objects whose bytes changed**, each as the restore does (`PUT` begin / data / commit; rc 3 = busy: the loop
plays on the FM-1, wait a second and send it again; rc 2 at a commit = the FM-1 refused the content: report it, write
nothing more; a failed data chunk: abort). A `PUT` begin ends the `LIST` snapshot, so the writes come after all the
reads. **Order: the stores first, the bank last**, so a refused patch leaves the slot's record as it was.

| operation | the bank | the stores |
| --- | --- | --- |
| **export** slot k | the record | the blob of the slot's engine kind, if its bit is set |
| **import** file into slot k | the file's record (name from the JSON if given), `used` 0xA5 | the file's patch into the store of the engine's kind at slot k (bit set); the other two kinds' bit k cleared and their bytes zeroed (as `va_store_saved` / `fm6u_saved` / `czu_saved` do on the FM-1) |
| **rename** slot k | `name` rewritten (zero-padded), nothing else | – |
| **delete** slot k | the record zeroed (192 x 0) | bit k cleared and the bytes zeroed in all three kinds |

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
./build/host/sound_templates                                              # {"templates": {"fm6", "cz"}, "fixtures": {...}, "factory": .., "factory_first": ..}
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

- **More than 32 slots.** 160 KiB of flash is free (0xB2000..0xC7FFF), but the stores are mirrored in the pool
  (`va_store` 3536, `fm6u` 2 x 2064, `czu` 2 x 2320 bytes) and the banks in RAM (2 x 3080); POOL is at 92 % and RAM at
  80 %. 64 slots would need a second VA store object (64 x 110 bytes exceed one object's 3840-byte payload), new halves
  for FM6 and CZ-1, wider `used` masks, the UI's `U01..U32` picker, new backup ids, and about 12 KB of POOL. Possible
  later; not part of this.
- The FM-1 shows no message when a bank is written from the computer (the PRESETS list simply changes).
