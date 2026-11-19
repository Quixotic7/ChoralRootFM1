# Progressions: the store

GitHub issue 64, "Ripchord style chord progressions". KEY layer page 2, KNOB 2 Keyboard has a third value, **Prog**:
every key of the keybed then plays the chord stored for it in the selected one of 16 progressions, as trackbout's
Ripchord does (one trigger key, one stored chord of up to 8 notes). How it is played and chosen on the unit: see the
manual (docs/MANUAL.md). This page is the store: the format, where it lives, the factory set and the backup object.

| | |
| --- | --- |
| Contract | `firmware/src/cr_prog.h` (between the store, the engine / UI and the web editor `web/fm1prog.js`) |
| Store | `firmware/src/cr_prog.c` (the RAM copy, the check, the factory set) |
| Flash | `storage.c` object `OBJ_PROG` (21): FM-1 0xB2000 / 0xB3000, SMK-37 0xB0000 / 0xB1000 |
| Backup | id 24 (`CRPG_BACKUP_ID`; web/EDITOR_PROTOCOL.md "ChoralRoot: backup and restore") |
| Tests | `tests/cr_prog_test.c` (the format, the lookups, the power-on), `tests/cr_backup_test.c` (id 24) |

## The object

All little-endian, at most `CRPG_SIZE` = 3840 bytes (one sector's payload, `ST_PAYLOAD_MAX`):

| Bytes | Field | |
| --- | --- | --- |
| 0..3 | magic | `CRPG_MAGIC` 0x31505243, "CRP1" |
| 4 | version | `CRPG_VERSION` 1 |
| 5 | n | `CRPG_N` 16: the progressions |
| 6..7 | used | the object's length in bytes, header included; equal to the stored length |
| 8..15 | rsv | 0 |
| 16..271 | directory | 16 entries of 16 bytes, one per progression (slot 1 first) |
| 272.. | data | the chords of the used slots |

A directory entry:

| Bytes | Field | |
| --- | --- | --- |
| 0..11 | name | `CRPG_NAME` 12 characters, ASCII 0x20..0x7E, padded with 0 (a name of 12 has no 0) |
| 12 | base | the Ripchord trigger note of key 0, 0..127, kept for the `.rpc` round trip (the unit plays by key, not by note) |
| 13 | nkeys | the keys the progression spans, 0..`CRPG_KEYS` 48; 0 = an empty slot (then off is 0 and the name all 0) |
| 14..15 | off | where its data starts, from the start of the object |

The data of a used slot, at `off`: `nkeys` records, one per key from key 0, each a count n (0..`CRPG_NOTES` 8) and n
notes (MIDI 0..127, ascending, distinct). n = 0 is a key without a chord: it is silent. The slots' data do not overlap
and lie in [272, used); gaps are allowed (a writer packs them in slot order, so there are none).

**Valid** (`crp_valid`) is exactly that: the magic, version 1, n = 16, used = the stored length, 272 <= used <= 3840, the
reserved bytes 0, every name printable and 0-padded, base <= 127, nkeys <= 48, an empty slot with no off and no name,
every used slot's off in [272, used) and every record (its count and its notes) before used, at most 8 notes, notes
strictly ascending and <= 127, no two slots' data overlapping. Anything else makes the whole object invalid and the unit
plays the factory set instead; there is no partial reading.

**Key to chord**: keybed key k (0 = the lowest: F3 on the FM-1, C3 on the SMK-37) plays progression key
i = k + 12 x window, the window 0..3 (`CRPG_WINDOWS`: OCT- / OCT+ while in Prog). i >= nkeys, or a record of n = 0:
silent.

## Where it lives

One storage.c object, `OBJ_PROG` (21, after `OBJ_BLE`), an A/B pair with storage.c's protocol (a save goes to the copy
that is not current, the commit record last; a torn write leaves the previous copy):

| Board | Copies | Before |
| --- | --- | --- |
| FM-1 | 0xB2000 / 0xB3000 | 0.21's FM TONE store, free since HISTORY 3y231 |
| SMK-37 | 0xB0000 / 0xB1000 (the first pair of `ST_SMK_HIGH`) | 0.21's CZ-1 tone store 2, free since 3y231 |

A record 0.21 left in those sectors names its own object in its header (17 or 8), so storage.c refuses it: such a unit
plays the factory set until progressions are sent. Options > Flash Data (SAFE MODE) erases the pair on both boards (the
FM-1's list in `cr_ui.c cu_flash_erase`, the SMK-37's `ST_SMK_REGIONS`, which a change of the store's owner clears too).

**The RAM copy** (`crp_x`, 3840 bytes of RAM, +3844 B in all on each board): the whole object, read once at power-on
(`crp_init`, from `cr_bank_boot`) and again after a backup restore of id 24 (`crp_reload`). The engine and the UI read
only this copy (`crp_nkeys`, `crp_count`, `crp_name`, `crp_chord`): a loop may resolve a progression's chord from the
audio side, and the flash cannot be read while a sector is being erased. The lookups check every offset against the
copy's length, so a half-done reload gives a wrong chord at worst, never a read outside it.

**The power-on never writes flash.** The stored object when it is valid, else the factory set built into RAM; in SAFE
MODE the factory set. The factory set is written to flash only when progressions are sent.

## The factory set

Six progressions of ChoralRoot's own in slots 1..6, slots 7..16 empty. Each spans keys 0..7, base 48 (C3), five notes
a chord: the bass alone in MIDI 36..47 (C2..B2), the upper voices in 52..70, voiced for smooth movement.

| Slot | Name | Keys 0..7 |
| --- | --- | --- |
| 1 | Pop Four | C, G/B, Am, F, C/E, G, Fmaj7, Gsus4 |
| 2 | Jazz ii-V-I | Dm9, G13, Cmaj9, A7b9, Dm9, G7b9, Cmaj7, C6/9 |
| 3 | Neo Soul | Ebmaj9, Fm9, Gm7, Abmaj7, Bb13, Cm11, Abmaj9, Bb7sus4 |
| 4 | Gospel Walk | C, C7/E, F, F#dim7, C/G, A7, Dm7, G7 |
| 5 | Minor Epic | Am, F, C, G, Dm, E7, Am/E, E |
| 6 | Dorian Days | Dm7, Em7, Fmaj7, G7, Am7, Cmaj7, Dm9, G13 |

The notes are in `cr_prog.c CRPG_FAC`. The object is 560 bytes (272 + 6 x 8 x 6).

## The backup object, id 24

- **LIST** names 24 with the RAM copy's length and CRC: the stored object, or the factory set, so the web editor always
  sees what the unit plays (also in SAFE MODE). Never size 0.
- **GET** returns those bytes (from RAM: nothing waits for the flash).
- **PUT** takes a begin of 0 or 272..3840 bytes (else rc 1). The commit checks the CRC and `crp_valid` (rc 2, nothing
  written), then stores the object with storage.c's protocol (one sector erase) and reloads the RAM copy at once; no
  `RESTART` is needed. 0 bytes: back to the factory set (an empty object is stored, which reads as none). The factory
  set's exact bytes sent to a unit that stores none write nothing (a restore of a backup taken before any progression
  was sent costs no erase).
- Busy (rc 3, a loop plays, records or a slot load is on its way) and stale (rc 5) as for every object.
- A firmware before the progressions does not list 24; INFO's `43 01` tag is 4 from this one (no client reads it).
- An older backup page (`web/fm1backup.js`) needs nothing new: it reads every id the unit lists (an id it does not know
  may be up to 80 KB, so 24 passes its checks), names it "object 24", keeps it in the archive and restores it onto any
  firmware that lists 24 (it is skipped on one that does not).
