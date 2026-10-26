# ChoralRoot FM-1: settings persistence

Files: `firmware/src/cr_settings.h` (the record), `firmware/src/cr_settings.c` (import / apply / capture, and
in the firmware unit the flash side and the UI glue), `firmware/src/settings_persist.c` (Felucca's record,
now PER5), `tests/cr_settings_test.c`, the emulator's flash in `tools/emu/emu_hal_fw.h` and `tools/emu/emu_fw.c`,
and `tools/emu/test_persist.sh`.

Since 0.18 there are two records: the **settings block** (the options: 192 bytes inside Felucca's settings object,
unchanged in size since 0.1) and the **session** (the state of the instrument: both parts' sounds as they are, the
voicing, the octave, Bass on, LOCK, the FX layer; 1024 bytes in a flash object of its own). **Saving is manual**
(the owner, 2026-10-11): what the Options menu alone sets is still written by itself; everything else, in either
record, is written only by the **global save** (SAVE held), and the unit powers on as the last global save left it:
[Manual and automatic](#manual-and-automatic-018). The inventory of what is kept where is in
[The session](#the-session-018).

## Manual and automatic (0.18)

"Hold Save button to manually do a global save, instead of trying to auto-save settings at runtime. So if you power
cycle without global saving it will reset to what it was. You could also hold GLO + save to reload the global
settings and current preset, which would also make a nice feature for being able to change a bunch of settings then
quickly revert back or save." (the owner, 2026-10-11)

| gesture | does | the screen |
| --- | --- | --- |
| **SAVE held** past the hold time (Options > Hold Time, as the layer buttons), alone | the **global save** (`cr_ui.c cu_gsave` -> `cr_settings_commit` + `cu_loop_write`): the whole settings block, the session, and the loop in RAM if it changed | the message box `saved`, green (a flash error: `save error`, red) |
| SAVE held while a loop plays or a capture is open (armed, count-in, recording, overdub, step entry) | nothing is written (no flash erase then); the save waits (`cu.gsave_pend`) and happens when the loop has stopped and no capture is open | `saves at stop`, then `saved` at that moment |
| **OPT (GLO) + SAVE**, both held 1 s (`CU_REVERT_MS`; either one first) | the **revert** (`cu_revert` -> `cr_settings_revert`): what the last global save wrote, applied as a power-on applies it | `reverted`, white |
| SAVE tapped | the save-preset dialog, as ever (a preset is written at once) | |
| SAVE held in the naming dialog, 1 s | delete / reset the preset, as ever (no global save there) | |
| SAVE held + a key, a knob or another button | a combo: nothing (the loop slots' Save / Load / Delete map of 0.17 is gone: `L_SAVE` is retired as `L_RSV6`, the layer numbers stay) | |

No confirm for either ("No confirm, just a message to communicate what happened"). SAFE MODE: `safe mode: no saving`
/ `safe mode: no revert` (nothing is read or written there). After a backup's settings were restored and before the
restart: `restored: restart first`.

**Every setting and its side.** Automatic = written by `cr_settings_poll` when the unit is quiet (the rules below,
unchanged); manual = written only by the global save. The rule for a setting reachable both from the Options and
from a layer: it goes with the layer (manual).

| setting | where it is set | record | side |
| --- | --- | --- | --- |
| Play Style | Options | block `playstyle` | **automatic** |
| Extension Addition | Options | block `extadd` | **automatic** |
| Secret Chords | Options | block `secret` | **automatic** |
| 6th with 7th (0.20) | Options | block `pick_roots` bit 4 | **automatic** |
| Velocity | Options | block `vel` | **automatic** |
| Key Touch (Fixed, Dynamic, Vel + Aftertouch, Vel + Poly AT), Velocity Sens, Aftertouch Sens (docs/VELOCITY.md) | Options | block `bass_on` bits 2..7 (`CRS_TOUCH`, `CRS_VSENS`, `CRS_ATSENS`; 0 = Fixed / Mid, the defaults: no version) | **automatic** |
| Velocity Curve -3 .. +3 (docs/VELOCITY.md "fm1-ctl") | Options | block `pick_roots` bits 5..7 (`CRS_VCURVE`, two's complement; 0 = 0, the default: no version) | **automatic** |
| MIDI Perform, MIDI Bass, MIDI Raw Chord | Options | block `midi_en[3]`, `midi_ch[3]` | **automatic** |
| Raw Chord Sound | Options | block `raw_sound` bit 0 | **automatic** |
| MIDI Keys (0.24, docs/CHORD-LAYOUT.md) | Options | block `raw_sound` bits 1 .. 5 (`CRS_MIDI_KEYS`: 0 Off, 1 .. 16 the channel) | **automatic** |
| MIDI Clock | Options | block `clock_mode` | **automatic** |
| Motion | Options | block `motion` | **automatic** |
| Chord Names (0.20) | Options | block `pick_roots` bit 3 | **automatic** |
| LEDs | Options | block `leds` bit 0 (bit 1: 0.25.1's move to the FM-1 chord keys, `CRS_KEYS_MOVED`, written with it) | **automatic** |
| Hold Time | Options | Felucca's `bold` (HOLD), in the same flash object | **automatic** |
| USB Record, USB Level | Options | block `usb_in`, `usb_level` | **automatic** |
| USB Rate (0.19) | Options | block `pick_roots` bit 2 | **automatic** |
| Dual Core (On by default since 0.22; a record keeps its own; the single-core package never changes it: docs/DUALCORE.md "Two packages") | Options | block `view` bit 6 | **automatic** |
| Output (0.19) | Options | block `pick_roots` bit 1 | **automatic** |
| Chord Keys, Full K Shorts, SUS Window (0.24, docs/CHORD-LAYOUT.md) | Options | block `sticky` bits 1, 2 and 3 .. 6 (`CRS_LAYOUT_FM1`, `CRS_FULL_SHORT`, `CRS_SUSWIN_MS`; `CRS_STICKY_AUTO`) | **automatic** |
| the palette (no control: MOD) | | block `palette` | automatic |
| the panel calibration | Options > Calibrate, OCT- + OCT+ at power-on | Felucca's `panel` | **automatic** (written by the calibration itself) |
| Bass Behaviour | Options **and** the BASS layer (SELECT, KNOB 1) | block `bass_mode` | manual |
| Single Notes | Options **and** the KEY layer (KNOB 4) | block `single` | manual |
| Split Point | Options **and** OPT + KNOB 1 | block `split_pc` | manual |
| View | Options **and** HOME (tap, the view menu) | block `view` bits 0..2 | manual |
| the view's lock | the view menu | block `view` bit 7 | manual |
| Key Mode, tonic, scale | KEY tap, the KEY layer | block `key_on` bit 0, `tonic`, `scale` | manual |
| Key plays, Keyboard | the KEY layer's page 2 | block `key_on` bits 1 / 2 | manual |
| Snap (0.22) | the KEY layer's page 2, KNOB 3 | block `key_on` bits 3 .. 4 | manual |
| Numerals (0.22; its Solfège after 0.22) | the KEY layer's page 2, KNOB 4 | block `bass_on` bit 1 (On); `scale` bit 7 (Solfège) | manual |
| the KEY layer's keys (pick / play) | GLO in the KEY layer | block `loop_rec` bit 5 | manual |
| Transpose | the KEY layer | block `transpose` | manual |
| the LOCK layer's two settings | B3 held | block `loop_rec` bits 6 / 7 | manual |
| Perform on, the mode, its parameters | PERF tap, the PERF layer, KNOB 3 | block `perform_on`, `perform_mode`, `perf_sel`, `par` | manual |
| LATCH on | LATCH tap (FM-1 chord keys: GLO held + LFO) | block `sticky` bit 0 | manual |
| the tempo | SELECT | block `bpm` | manual |
| the bass register | KNOB 2, the BASS layer | block `bass_voicing` | manual |
| each part's chosen sound, each engine's last place | PRESETS, ALGORITHM, the pickers | block `chord_sound`, `bass_sound`, `pool_pos` | manual |
| FX on | FX tap | block `fx_on` | manual |
| the looper: Length, Quantize, Count-in, Level, the record mode, the PLAY menu's keys | the PLAY menu | block `loop_sync`, `loop_quant`, `loop_count_in`, `loop_level`, `loop_rec` bits 0..4 | manual |
| the looper's Rate | the PLAY menu's KNOB 2 | session `loop_rate` | manual |
| the loop slot selected | the PLAY menu's PRESETS | block `loop_slot` | manual |
| the metronome: on, time signature, level | the PLAY menu's page 2 (KNOB 2), GLO + SEQ, the SEQ key's hold menu (metronome), OPT + SELECT | block `metro_on`, `metro_sig`, `metro_vol` | manual |
| the voicing, the octave | KNOB 1, OCT | session `voicing`, `octave` | manual |
| Bass on | BASS tap | session `bass_on` | manual |
| LOCK on, the chord keys it latched | B3 | session `lock`, `latched` | manual |
| the FX layer: the page picked, the effects' parameters, both parts' sends | the FX layer, KNOB 4, the editors | session `fx` | manual |
| both parts' sounds as they are (unsaved edits, deep patch, name), their Level and Pan | the editor, the layers | session `part[2]` | manual |
| the loop in RAM | REC, the editor, clear, undo | its slot's flash record | manual, **and** written when its slot is left (docs/LOOPER.md) |
| a preset saved (SAVE tap), patches and banks received | | their own flash objects | written at once, as ever |

In the block the automatic fields are 17 bytes and one bit of the 180 after the header (`cr_settings_auto`,
`tests/cr_settings_test.c t_auto`: every byte is one side or the other).

**How the two sides share one flash object.** The block is one 192-byte record inside one flash object, so an
automatic write cannot leave the manual fields out: it writes **flash's own block with the automatic fields as they
are now** (`crs_write(0)`: `crs_rec`, the mirror of what flash holds, with `cr_settings_auto` over it), and the manual
fields in flash stay what the last global save wrote. The global save writes the block as captured, whole
(`crs_write(1)`), then the session (`css_write`). The poll starts an automatic write only when an automatic field
(or Hold Time) changed (`cr_settings_poll`: the last state seen with the Options of now differs from it), so turning
a manual knob costs no erase, ever.

**The revert** applies `crs_rec` (flash's block) with the Options as they are now over it, reads the session object
again (`css_read`, the same "does it belong" rule as at power-on), puts both parts and the effects where `cr_ui_init`
leaves them (`crs_reinit`: an edit not saved is gone), and runs `cr_settings_load`: exactly the power-on path. The UI
(`cu_revert`) first quiets the unit as PANIC does without its screen (`cu_quiet`: notes off, the loop stopped, LATCH
off, the octave 0; LOCK off and its latch up), closes every layer, page and dialog (the engine picker's preview and
the loop editor with them), and afterwards reloads the loop: the slot the block names, from flash, its RAM changes
dropped (a loop staged by a slot switch while it played is written first: that save is the switch's). A unit that
never saved reverts to what it powered on as.

**Audio.** A flash erase holds every IRQ for the sector's erase time with the audio buffer zeroed (`storage_hw.c
st_erase`; 17.8 ms measured on the unit, 45 ms by the data sheet and in the emulator's model). The
global save writes at most three objects back to back (the settings object: 4 page programs; the session: 5; the
loop slot: 1 erase + up to 16) and only those that changed (`crs_write`'s compare, `css_write`'s hash,
`crl.dirty`). Since 0.22 the session is programmed into a copy that was erased ahead
([When flash is written](#when-flash-is-written-022-the-quiet-writes)): no erase, nothing heard, also into a held
chord; the settings object erases at its save as before (3y52: the calibration's fallback copy). A second global save before the erase ahead has come (10 s idle) erases for itself, one hole a written
object, the sound going on after each (`test_persist.sh` `quiet_writes_save.txt`: "audio stalled" in the dump); the
loop slot erases as before. It is not deferred to silence: the owner presses and expects it. It IS deferred while a
loop plays or a capture is open. The automatic write keeps main's rule: never while anything sounds above the tail
floor.

**Power-on**: as the last global save left it, plus the Options as they were when the unit was last quiet. Never
saved: the defaults. **Migration**: none; what a 0.17 unit or an earlier 0.18 build stored is "the last save".
`cr_settings_save` (Felucca's `settings_save`: the power-on calibration, the update gesture) is the automatic part
alone: the update gesture is not a global save.

**When automatic writes happen (3y48)**: only from `cr_settings_poll` (the Options) and `crf_poll` (the favourites),
once nothing changed for `CRS_QUIET_MS` (1.5 s) AND nothing sounded for `CRS_IDLE_MS` (1 s; `crs_sounding`: a voice
of either part, P5's on either core and the bass stack included, the engine's voices and queued notes, an arp or
perform pattern stepping, the master output above -60 dBFS), at least `CSS_GAP_MS` after the last write, never while
a loop plays or records. **A read never writes flash**: a backup's LIST and GET, `--favorites`, `--debug` (73 DEBUG,
78 PEAK, 79 USB) and INFO only read. Until 3y48 LIST called `cr_settings_save` first, which erased for a pending
Option or mark whatever sounded (the owner's stutter under an arp; the emulator reproduced two erases from one LIST).
Now LIST lists the Options as last written (a change not yet written is in the next backup after the unit was quiet)
and the favourites from RAM when a mark is not written yet (`crb_favram`: the bytes `crf_pack` gives; GET serves
them; a restore of them writes as any PUT does). 78 PEAK's `erases_auto_sounding` (the rule: 0) and `last_erase_why`
/ `last_erase_obj` / `last_erase_ms` say who wrote last (docs/FX.md).

## When flash is written (0.22: the quiet writes)

The owner, 2026-10-13, on the build with the favourites' filter (marking, typing and filtering favourites across
engines while playing): "I am now getting lots of audio glitches and dropouts now, even with older engines like
FM6." The unit after that session: `flash_erases` 9, `irq_gap_us` 17 851 at `st_erase`'s return, `stale_halves` 46.

**What it was.** `storage.c st_save` erased the copy it was about to write at EVERY save: one sector erase a write,
17.8 ms with every interrupt masked (six audio halves nobody serves, the buffer silenced first), then the page
programs. The automatic writes (an Option changed, a favourite marked, typed or filtered) waited 1.5 s after the
change and for one second without a sound: the pause after a mark, where the next note is. A note in those 18 ms
starts late or loses its attack; a tail under −60 dBFS is cut.

**What it is now** (`storage.c` "erase ahead", `cr_settings.c` "the quiet writes"): for the **favourites** and
the **session** the erase is taken out of the save. **The settings object is not** (3y52, the owner's decision of
2026-10-14: "Lets go with your recommendation."): it holds the panel calibration, and its previous copy stays beside
the current one as the fallback until its next save, which erases at that time as it always did. That costs an 18 ms
pause when an Option changes, accepted: the automatic Options write keeps its rule from before the quiet writes
(nothing changed for 1.5 s, nothing sounded for 1 s: `cr_settings.c crs_when` treats an object that is not erased
ahead as `ready`), so the pause falls after the sound (`erases_auto_sounding` 0; it may come within 2 s of an input,
which `erases_near` counts: that rule is the favourites' and the session's); SAVE held erases at once (the player's own press). p5-g's first version (e31c096) erased
all three ahead; this line (3y52) keeps only these two (`storage.c st_ahead_ix`, `cr_settings.c CRQ_OBJ`).

| | when | masks the interrupts for |
| --- | --- | --- |
| **the erase ahead** (`st_prep`): the copy that is NOT the current one is made blank | at power-on, before the audio starts (`crs_ahead_boot`, from `persist_boot`); afterwards only after **10 s** without a sound and without input (`ST_AHEAD_IDLE_MS`; `crs_ahead_poll`), one object at a time, 250 ms apart | one sector erase (17.8 ms), where nobody plays |
| **an automatic write into a blank copy** (`st_write_due`, `ready`) | as before: nothing changed for 1.5 s (`ST_SETTLE_MS`: a run of changes is one write), nothing sounded for 1 s (`ST_SILENT_MS`), no loop playing, no restore | page programs only, each its own masked stretch with the interrupts open between: 3 for the favourites (2 payload pages when the object is over 256 bytes, 262 at most, + the commit record), 5 for the session (the settings: their erase + 4, see below). A page: about 1 ms by the driver's comments (3 ms the generic maximum), **not measured**: `flash_prog_us` in `--debug` is the unit's own figure. Under one audio half (2.9 ms) |
| **an automatic write that has to erase** (a second change before the erase ahead came; a spare copy that is not blank for any reason) | the same, and **4 s** without a sound AND without input (`ST_ERASE_IDLE_MS`) instead of one silent second | the erase + the programs |
| **SAVE held** (`cr_settings_commit`), the calibration, the update gesture (`cr_settings_save`; 3y48: never a backup's LIST) | at once, as before | programs only when the copy is blank; else the erase too |
| **the settings object** (any save: the Options poll, SAVE held, the calibration) | the Options poll: 1.5 s unchanged, 1 s silent (as before 0.22); the others at once | its erase (18 ms) + 4 programs, every time; the previous copy kept |

"Sound" is `crs_sounding` (a voice of the parts, a chord held or latched, a scheduled note, the master output above
−60 dBFS over the scope's 23 ms); "input" is `cu.last_input` (`cr_ui.c`: a key, a button, a knob, a MIDI channel
message: the screensaver's clock). Both are read in the frame that starts the erase: an input before it starts puts
it off by the whole wait; once started an erase runs to its end.

**What a save reads.** `st_save` does not trust the RAM flag (`st_ready`, which only decides WHEN): it reads the
whole target sector (16 reads of 256 bytes) and programs without erasing only if every byte is 0xFF; else it erases
as it always did. A copy half written or half erased when the power went is therefore erased again before use.

**Power cuts** (for the favourites and the session; the settings object keeps Felucca's two copies: a cut leaves
the older or the newer valid copy in charge, as before. The commit protocol is unchanged: only the copy that is not current is ever erased or programmed,
the commit record last; `tests/storage_test.c` walks these):

| the flash | what a power-on reads | what happens next |
| --- | --- | --- |
| valid A + blank B (after an erase ahead; also a new unit after its first save) | A | the next save programs B |
| valid A + B half programmed (cut during a save: payload without a commit record, or a torn record) | A | B is not blank: erased at power-on |
| valid A + valid B, B newer (cut after a save, before the erase ahead; every unit coming from an older firmware) | B | A erased at power-on |
| valid B + A half erased (cut during the erase ahead) | B | A is not blank: erased again at power-on |
| no valid copy, anything in A | nothing (the defaults) | A erased at power-on; the first save programs it |

**What it costs.** (1) Power-on: per object the two commit records, the current payload and one sector's blank check
(about 16 reads of 0.5 ms: 8 ms; by reading, not measured), and an erase (18 ms, + 8 ms to read it back) for each
copy that is not blank: about 16 ms when both are blank, about 70 ms at worst (two objects; the settings object is
not checked). (2) **The older copy of the favourites and the session does not survive until the next save** (the
settings' does: the reason they are not erased ahead). Before 0.22 a unit kept the previous version of each object beside the current one,
and a current copy that went bad (a flash fault, not a power cut) fell back to it; now, from the erase ahead on,
there is one copy, as on a unit saved once. (3) An erase cut by the power so late that every byte already reads
0xFF cannot be told from a finished one; such a sector is programmed, and read back and verified by the save, but
its cells may hold their charge less well than after a whole erase. Felucca's writer had no such window (it erased
and programmed in one go). Not done: a second erase when the previous power-on did not finish its own.
(4) Wear: unchanged, one erase a write; it only moved. (5) A change made less than 1.5 s (or, with the spare copy
not blank, until 4 s of quiet) before the power goes is lost, as before; SAVE held writes it at once.

**Compatibility.** The format and the map are unchanged. An older firmware meets "one valid copy, one blank" on
every unit saved once, and its writer erases before it programs; what it leaves (two valid copies) is the third
row above (`storage_test.c`: 74eae04's writer on this one's flash and back).

**The counters** (`--debug`, 78 PEAK; docs/FX.md): `saves_auto`, `erases_ahead`, `erases_near` (erases the unit
started by itself while a voice was active or within 2 s of an input: 0 is the rule), `flash_prog_us`.
The emulator's dump has them in its `quiet writes:` line (`tools/emu/test_persist.sh`, `quiet_writes*.txt`).

`ST_AHEAD` (`choralroot.c`: 1) switches it on; without it `storage.c` is Felucca's writer (its host tests run so).

## Where it lives

Felucca keeps one flash object, `OBJ_SETTINGS`: an A/B sector pair at 0xFC000 / 0xFD000 (`storage.c`). Each
copy has a commit record with a sequence number and a CRC, so a torn write leaves the older copy in charge. Its
payload is `persist_t` (`settings_persist.c`), whose magic marks the layout version:

| magic | adds |
| --- | --- |
| PER1, PER2 | upstream: palette, lowcut, panel table |
| PER3 | `bold` (HOLD now lives there) |
| PER4 | favorites |
| **PER5** | **`cr_settings_t cr`: ChoralRoot's block, 192 bytes, last** |

A PER1 to PER4 record imports as before, and its `cr` block is zeroed. A zeroed block fails the block's magic,
so the ChoralRoot side reads defaults. A Felucca build (`felucca.c` → `project.c`) keeps the block exactly as
saved. Felucca's own fields stay where they were: the panel calibration table, the palette, HOLD in `bold`, and
LEDs in `zoom`.

**0.22 added one bit and no version, in another byte**: the KEY layer's Numerals (page 2, KNOB 4: the chord's Roman
numeral on the Chord view; Off, the default, is 0) in bit 1 of `bass_on`. The byte was chosen by what the older
firmwares do with it, read in `git show v0.17:` .. `v0.21:firmware/src/cr_settings.c`: all five have
`CRS_FIX(bass_on, 0, 1)` with the default 0, and since v15 the block's `bass_on` is always written 0. So an older
firmware that meets Numerals On (the byte = 2) finds it out of range and takes 0, the value it would have read
anyway: **nothing is reset on the way back**, and its own next global save writes 0 (Numerals is Off again when the
newer firmware returns). The two homes that were looked at first cost something: bit 3 of `key_on`
(`CRS_FIX(key_on, 0, 7)` in 0.17 .. 0.21: Key Mode off, Key plays Chords, Keyboard Chords there), bit 5 of
`pick_roots` (0 .. 31 in 0.20 and 0.21: Output, USB Rate, Chord Names and 6th with 7th to their defaults there).
Steps 3, 5 and 6 of "How to add a field": `crs_sanitize`'s range for the byte is 0 .. 3; `cr_settings_apply` gives
the engine bit 0 only (`cr_set_bass(c, s->bass_on & 1u)`) and `cr_settings_capture` keeps bit 1; the glue
(`crs_capture` / `cr_settings_load`) maps it to `cs.numerals`; `tests/cr_settings_test.c` checks the round trip,
that it never switches the bass on, that it is manual, and the older firmwares' rule (modelled from their `CRS_FIX`
line: the old binaries were not run). Manual, as the page's other two: not in `cr_settings_auto`. The web manager
and the backup tools carry the block as 192 opaque bytes and decode no field of it (`web/fm1backup.js`,
`tools/fm1_install.py`: `CR_BLOCK`), so they need not know the bit.

**0.22 added two more bits and no version, in `key_on`**: the KEY layer's Snap (page 2, KNOB 3: a key out of the
scale plays the nearest note of the scale or of the chord sounding, with Key plays Notes or the Full keyboard; Off,
the default, is 0; GitHub issue 18, HISTORY 3y99) in bits 3 .. 4 (`CRS_SNAP`, `CRS_SNAP_BITS`; `crs_sanitize`'s range
for the byte is 0 .. 23, `CRS_KEY_MAX`: a Snap of 3 is out of range with the higher bits). 0 in every older record
and 0 is the default: nothing to migrate. **The caveat is v14's**: every firmware from 0.17 to 0.22-beta2 checks the
byte against 0 .. 7 (`CRS_FIX(key_on, 0, 7)`), so after a downgrade with Snap on it reads Key Mode as off, Key plays
Chords and Keyboard Chords, until they are set again there, and nothing else; with Snap Off the record reads as
before. The byte was chosen as Key Mode's own, next to the two settings it belongs with; the other homes looked at
cost a setting too on the way back (`scale`'s upper bits: Major there; `single`'s: Full Octave there) and not the
one the player would look for. `cr_settings_apply` gives the engine `cr_set_key_snap(CRS_SNAP(s))`,
`cr_settings_capture` and the glue write the bits with `CRS_KEY_BYTE`'s; `tests/cr_settings_test.c t_snap` checks
the 24 bytes' round trip, 24 .. 255 to the default, apply / capture, that it is manual, and the older firmwares' rule
(modelled from their `CRS_FIX` line). Manual, as the page's others: not in `cr_settings_auto`.

**Numerals' third value, Solfège (after 0.22; GitHub issue 15), is one bit of its own and no version, in
`scale`.** KNOB 4 reads Off / On / Solfège (the degree as DO RE MI .., docs/CHORD-NAMES.md "Solfège"). The owner
(2026-10-16): "Add a new bit for the Solfège mode. don't mix it with bass". So On stays bit 1 of `bass_on` and
Solfège is **bit 7 of `scale`** (`CRS_SOLFEGE`; the scale is bits 0 .. 6, `CRS_SCALE`, which leaves room for more
scales), each written alone: Off neither, On `CRS_NUMERALS`, Solfège `CRS_SOLFEGE` (if a record had both, Solfège
wins). 0 in every older record and 0 is the default: nothing to migrate. **What an older firmware reads**: every
firmware from 0.17 to 0.22 checks the byte against 0 .. 1 (`CRS_FIX(scale, 0, 1)`, read in `git show v0.17:` ..
`v0.22:firmware/src/cr_settings.c`), so after a downgrade with Solfège saved it reads **the scale as Major and
Numerals as Off**, until set again there, and nothing else; without Solfège the byte is one it knows. The byte was
chosen as the KEY layer's own; a bit of the bass byte was ruled out by the owner (and its bit 0 would switch 0.18 ..
0.22's engine bass on), `key_on`'s would cost Key Mode, Key plays, Keyboard and Snap there, `tonic`'s the key. Steps
3, 5 and 6: `crs_sanitize` takes the scale from bits 0 .. 6 (an unknown one: Major, the bit kept);
`cr_settings_apply` gives the engine `CRS_SCALE(s)` and `cr_settings_capture` keeps bit 7; the glue
(`crs_capture` / `cr_settings_load`) writes it from `cs.numerals == 2` and reads `cs.numerals` from the two bits;
`tests/cr_settings_test.c t_solfege` checks the round trip with both scales, the engine's side, an unknown scale, that
it is manual, an older record, and the older firmwares' rule (modelled from their `CRS_FIX` line);
`tools/emu/test_persist.sh` sets Solfège, saves, relaunches and reads `SOL` for G in C major. Manual, as the page's
others: not in `cr_settings_auto`.

## The ChoralRoot block (`cr_settings_t`, `CRS_SIZE` = 192 bytes)

The block has its own header:

- `magic` `CRS1`
- `version` (`CRS_VERSION`, currently 15)
- `size` (the writer's `CRS_SIZE`)
- `check`: FNV-1a over bytes 12..size

| field | range | default |
| --- | --- | --- |
| playstyle / extadd / secret | Simple–Free / Add Note, Play Chord / Off, Simple, All | Simple, Add Note, Off |
| key_on, tonic, scale | bit 0 Key Mode 0/1 (**v14: bit 1 the KEY layer's Key plays**, 0 Chords / 1 Notes, `CRS_KEY_NOTES`, `cs.key_notes`; **bit 2 its Keyboard**, 0 Chords / 1 Full, `CRS_KBD_FULL`, `cs.kbd_full`; `CRS_KEY_ON` / `CRS_KEY_PLAYS` / `CRS_KEYBOARD` / `CRS_KEY_BYTE`; **0.22: bits 3 .. 4 its Snap**, 0 Off / 1 Scale / 2 Chord, `CRS_SNAP`, `CRS_SNAP_BITS`, `cs.snap`, `cr_keysnap_t`; a higher bit or a Snap of 3, 24 .. 255: the byte's default, `CRS_KEY_MAX`), 0..11, Major/Minor (bits 0..6; **bit 7 after 0.22: the KEY layer's Numerals Solfège**, `CRS_SOLFEGE`, `CRS_SCALE` / `CRS_SOL`; an unknown scale: Major, the bit kept) | off, Chords, Chords, Off, C, Major; a v1..v13 record: its Key Mode, Chords, Chords, Off |
| transpose | −24..24 | 0 |
| single, split_pc | 0 Full Octave / 1 Split Oct ("Split" until v14) / **v14: 2 Split Bass**, 0..11 (the Chords layout's split, a pitch class; the Full keyboard splits at F4, fixed, not stored) | Full Octave, F (5) |
| vel | 1..127 (the keys' velocity) | 100 |
| bass_on | bit 0: 0/1; **bit 1 (0.22): the KEY layer's Numerals**, 0 Off / 1 On (`CRS_NUMERALS`, `CRS_NUMS`, the UI's `cs.numerals`; 0 .. 3 in range; Solfège is `scale`'s bit 7, not this byte's) | **0**. Bit 0 is always written 0: until 0.17 the rule was "the bass is off at every power-on"; since 0.18 (v15) Bass on is kept, in the session (`bass_on` there), because a firmware older than v15 would switch the engine's bass on from this byte with its own UI still showing off |
| bass_mode, bass_voicing | Chords Only..Solo, −2..4 octaves | Chords Only, 0 |
| perform_on, perform_mode, perf_sel | 0/1, Strum..Harp, the PERF layer's entry 0..6 | off, Strum, 0 |
| sticky | bit 0: LATCH (manual). **0.24, docs/CHORD-LAYOUT.md** (automatic, `CRS_STICKY_AUTO` 0x7E): bit 1 Options > Chord Keys FM-1 (`CRS_LAYOUT_FM1`), bit 2 Full K Shorts On (`CRS_FULL_SHORT`), bits 3 .. 6 the SUS Window as (ms - 30) / 5, 0 .. 10 = 30 .. 80 ms (`CRS_SUSWIN_MS`; 30 the owner's default); a code above 10: 30 ms, the other bits kept; bit 7 unknown: the byte's default. No version (0.24: 0 was every default). Every older firmware checks the byte with CRS_FIX(sticky, 0, 1): a byte with a new bit set reads as its default there (LATCH off), nothing else touched, so it starts in Classic. The engine takes bit 0 alone (`cr_settings_apply` / `cr_settings_capture` merge it). **0.25.1: FM-1 is the default** (bit 1 set by `cr_settings_defaults`), and a record without `leds`' mark is moved to it once ("0.25.1: FM-1 the default" below) | off; **FM-1** (0.25.1; Classic in 0.24 / 0.25; the SMK-37: Classic, hidden), Off, 30 ms |
| bpm | 20..300 | 120 |
| par[5][11] | per mode, the engine's `CR_PAR_MIN`..`MAX` | design.md §21 (= `cr_init`) |
| loop_sync, loop_quant, loop_count_in, loop_level | 0..5, 0..5, 0/1, 0..100 | Free, none, **on** (the count-in), 100 (the looper: docs/LOOPER.md) |
| midi_en[3], midi_ch[3] | per stream MAIN BASS RAW | on, on, **RAW off**; channels 1 / 2 / 3 |
| clock_mode | Off / **Out** / In | **Out** (Orchid sends clock) |
| raw_sound | bit 0: RAW also plays part 0. **0.24**: bits 1 .. 5 Options > MIDI Keys, 0 Off, 1 .. 16 the channel (`CRS_MIDI_KEYS`; above 16: Off, bit 0 kept). An older firmware's CRS_FIX(raw_sound, 0, 1) reads a byte with a channel as Raw Chord Sound Off. The engine takes bit 0 alone | off; MIDI Keys Off |
| view, motion, leds | view bits 0..2: Chord / Keyboard / Notes / Geek Out / Scope / **Geek Out 2 (5; 0.22**: four meters, docs/DUALCORE.md "Geek Out 2"; a firmware up to 0.21 finds 5 out of range and reads Chord) (Options > View, HOME held's menu); **v9: bit 7 the view lock** (`CRS_VIEW` / `CRS_VIEW_LOCK` / `CRS_VIEW_BYTE`; locked: HOME tap never cycles the view); **v12: bit 6 Options > Dual Core On** (`CRS_DC_ON`, `CRS_DUALCORE`; set = On: core 1 started at power-on, docs/DUALCORE.md; v11, 0.15, had the bit the other way round: set = Off, On the default), another bit or view: the default; Full/Calm/Off, Glow/Stock (`leds` bit 0, `CRS_LEDS`; **0.25.1: bit 1 `CRS_KEYS_MOVED`**, the record moved to the FM-1 chord keys: "0.25.1: FM-1 the default" below; 0 .. 3 in range, another value: the default byte) | Chord, unlocked, Dual Core **On** (0.22, a unit with no record and Reset Settings; Off 0.16 .. 0.21; a view byte out of range keeps the record's Dual Core), Full, Glow; a v1..v8 record: its view, unlocked; a v1..v11 record: Dual Core Off, whatever its bit 6 |
| palette | gfx.c index, 0xFF = MOD | **MOD** |
| fx_on | 0/1 | on |
| chord_sound | v6: the chord part's sound, (engine << 8) \| its pool position (docs/PRESETS.md); 0xFFFF = the UI's default. v1..5: an old list position, read as the default | FM6 TINE EP |
| bass_sound | v6: the bass part's sound (what BASS tap brings), (engine << 8) \| pool position; 0xFFFF = the UI's default (v1..5: read as the default; v6 on ANALOG, engine 0, retired in 0.14: read as the default) | VA DEEP SUB (ANALOG SUB BASS until 0.14) |
| metro_on, metro_sig, metro_vol, loop_slot (v2) | the click, 4/4 3/4 6/8, 0..100, 0..9 | off, 4/4, 70, 0 |
| pick_roots (v3) | bit 0: RESERVED since 0.18 (it was: the engine picker's white roots, 1 choose engines, 0 play, KNOB 4 in the picker): the roots always play there now; written 0, read and ignored. **Bit 1 (0.19): Options > Output**, set = Classic (0.18's master), clear = Clean (`CRS_OUT_CLASSIC`, `CRS_OUT_CLEAN`; the UI's `cs.out_clean`; applied by `crs_usb_apply` at boot without a fade, by `cr_settings_load` and the Options with one: `fx.c fx_output_set`; docs/AUDIO-QUALITY.md). No version for it: the bit is 0 in every older record and 0 is the default. A firmware up to 0.18 finds 2 or 3 out of range and takes its own default for the byte, nothing else. **Bit 2 (0.19, 2026-10-12): Options > USB Rate**, set = ChoralRoot In offers 44.1 and 48 kHz, clear = 44.1 kHz only, 0.18's device (`CRS_USB_48K`, `CRS_USB48`; the UI's `cs.usb48`; to `usb.c` as `ua_off`'s `UA_OFF_48` by `crs_usb_apply` at boot, before `usb_start`, and by `cu_usb_apply` from the Options, which replugs: docs/USB-AUDIO.md "Opt-in"). No version for it either, by the same reasoning: 0 in every older record, and 0 is the default. A firmware up to 0.18 finds 4 .. 7 out of range and takes its own default for the byte, a 0.19 build of before the option (range 0 .. 3) does the same and so reads Output as Clean until it is set there. **Bit 3 (0.20): Options > Chord Names**, set = Standard (lead-sheet names of the notes that sound), clear = Orchid (the ORC-1's naming, the default) (`CRS_NAMES_STD`, `CRS_NAMES`; the UI's `cs.names_std`; to the engine as `cr_chord_names_set` by `cr_settings_load` and the Options: docs/CHORD-NAMES.md). No version, by the same reasoning: 0 in every older record, and 0 is the default. 0.19 and older find 8 .. 15 out of range and take their own default for the byte: Output reads Clean and USB Rate 44.1 kHz there until they are set again, nothing else is touched. **Bit 4 (0.20): Options > 6th with 7th**, set = Octave up (a chord with the 6 and a seventh plays its 6th 21 semitones above the root, a 13th), clear = In place (the Orchid's, the default) (`CRS_SIXTH_UP`, `CRS_SIXTH`; the engine's `cr_t.sixth_up` through `cr_settings_apply` / `cr_settings_capture`, the UI's `cs.sixth_up`, the Options through the event `CRE_SIXTH`: docs/CHORD-NAMES.md "The 6th above a seventh"). No version, by the same reasoning: 0 in every older record, and 0 is the default. Values 0 .. 31 are in range now (`crs_sanitize`). **A firmware of before the option** (0.19: range 0 .. 7; the 0.20 builds of before it: 0 .. 15) finds 16 .. 31 out of range and takes its own default for the whole byte: with Octave up set, Output reads Clean, USB Rate 44.1 kHz and Chord Names Orchid there until they are set again (and that firmware's next save of the block writes its own value of the byte, so the bit is gone when the newer firmware returns); with In place set the byte is inside its range and is read as before | **1** in a record's defaults (0.17 reads it): Output Clean, USB Rate 44.1 kHz, Chord Names Orchid, 6th with 7th In place |
| loop_rec (v8; was rsv_usb, v4: usb_out) | v8: the looper's record mode (bits 0..3: 0 Overwrite, 1 Advance, 2 Overdub, 3 Replace, 4 Step; `CRS_LOOP_MODE`) and the PLAY menu's keys (bit 4: 0 Play, 1 Loops; `CRS_LOOP_KEYS`), docs/LOOPER-MODES.md; **v13: bit 5 the KEY layer's keys** (0 pick: a root = the tonic, MAJ / MIN = the scale; 1 play: the keys play as outside the layer; OPT tapped in the layer; `CRS_KEY_PLAY`, `CRS_KEY_KEYS`; the UI's `cs.key_play`); **v13 also: bits 6 and 7 the LOCK layer's styles** (B3 held): bit 6 Type key (0 Clears, 1 Keeps the latched extensions; `CRS_LOCK_KEEP`, `cs.lock_keep`), bit 7 Ext keys (0 Toggle, 1 Pushed; `CRS_LOCK_PUSH`, `cs.lock_push`); a mode out of range: the defaults (every bit of the byte has a job now). Before v8: retired in v5: v4's Options > USB Audio Out (the playback device, removed: docs/USB-AUDIO.md). A 0.14 dev build (still v6) kept the chord part's pool position on FM TONE (engine 15, `eng_quad.c`, code name QUAD) here; v6 → 7 moves it into `pool_pos` slot 0 and clears the byte: 0 since | 0 (Overwrite, Play, pick, Clears, Toggle); a v4 record's byte (1 by default there) is cleared on import; v1..v7 records read 0; a v8..v12 record: its mode and PLAY-menu keys, pick |
| usb_in (v4) | Options > **USB Record**: ChoralRoot In is presented to the computer (docs/USB-AUDIO.md); Off: the serial console instead | **1** (on); a v1..v3 record takes it (its zero would mean off) |
| usb_level (v4) | Options > USB Level: `CRS_USB_MASTER` (the recording follows MASTER) / `CRS_USB_FIXED` (recorded at the full level, MASTER after) | Master |
| pool_pos[2][10], pool_pos10_chord, pool_pos10_bass (v6) | per part (chord, bass), per engine slot (slots 0..9 in `pool_pos`, slot 10 in the two bytes that were `rsv0` / `rsv1`; `crs_pool_get` / `crs_pool_set`). The slots are fixed per engine in 0.13's display order, whatever the order shown (`cr_settings.c crs_slot`): 0 FM TONE (ANALOG's until v7), 1 FM6, 2 VA, 3 PHASE, 4 CZ-1, 5 LOFI, 6 VOICE, 7 TRIO, 8 WHEEL, 9 PHYS, 10 NOISE (3, 5, 6, 7, 9, 10: engines retired in 0.18, the slots reserved, their bytes kept): the pool position last played there, where OPT + PRESETS lands; 0..127, checked against the pool by the glue | 1 (each engine's first preset); a v1..v5 record takes the defaults |

The reserve is used up by v6 (`rsv[20]` became `pool_pos`), and v8 took the last free byte (`rsv_usb`, packed with two
fields), v9 put the view lock in the view's byte (bit 7; an older firmware reads a locked record's view as out of
range, Chord), v11 put Dual Core in bit 6 of the same byte, set = Off, and v12 (0.16) turned it round: set = On, so
that a fresh, erased or zeroed record reads Off, the default from 0.16 to 0.21 (since 0.22 the default is On:
`cr_settings_defaults` sets the bit for a unit with no record and Reset Settings; a record's Off stays Off) (the same caveat: an older firmware reads an
On record's view as Chord; 0.15 reads a v12 record's bit its own way, On as Off and Off as On, until it is set there),
and v13 (0.17) put the KEY layer's keys in bit 5 of `loop_rec`, next to the other "what do the keys do in this menu"
bit, and the LOCK layer's two styles in its bits 6 and 7 (the caveat here: a firmware older than v13 takes a record
with one of the three set for an unknown bit and gives the whole byte its defaults, so after a downgrade with the KEY
layer's keys left on play, or a LOCK style off its default, the record mode reads Overwrite and the PLAY menu's keys
Play until they are set again; nothing else is touched. With all three at their defaults an older firmware reads the
record as before). The byte is full. v14 (0.17) needed two more bits, for the KEY layer's page 2 (Key plays, Keyboard),
and a third value of Single: the bits ride in `key_on`'s byte (bits 1 and 2: Key Mode's own byte, where a 0 / 1 flag
left seven bits unused), 0 = the defaults, and Single's 2 is a new value of an existing byte, so the record did not
have to grow. The caveat is the same kind: **a firmware older than v14 checks `key_on` and `single` against 0..1**,
so after a downgrade with Key plays = Notes or Keyboard = Full it reads Key Mode as off (whatever it was), and with
Single = Split Bass it reads Full Octave; each until set again there, nothing else is touched, and with the three at
their defaults (and Single Full or Split Oct) an older firmware reads the record as before. 0.22 took bits 3 .. 4
of `key_on` for Snap (above: the same caveat). Bits 5..7 of `key_on`
and the upper bits of the other 0 / 1 bytes (`scale`, `sticky`, `raw_sound`, `fx_on`, ...) are what is left for
flags; a field that needs a byte of its own goes into the session record (below), which has room.

**v15 (0.18) changed no field of the block.** What 0.18 needed (about 400 bytes a part for the edit buffers, the
voicing, the octave, Bass on, LOCK, the FX layer) did not go into a longer block, although "a larger `CRS_SIZE`" was
the procedure written here until 0.17, for three reasons found in the code: (1) `settings_persist.c`
`settings_import` takes a settings object only at its exact size (`n == sizeof *p`), so **0.17 and older would read
a longer object as no record at all**: every option at its default and the panel calibration table gone with it,
and their next save would write that over the unit's settings; (2) `web/fm1backup.js` and `tools/fm1_install.py`
know the block as 192 bytes (`CR_BLOCK`: a ChoralRoot settings object restored on Felucca, and back), and
`cr_backup.c` takes a restored settings object through the same exact-size import, so older backups and newer
firmware would stop meeting; (3) a session written every time a knob is turned should not share a sector pair with
the calibration table. So the block keeps its size and layout, an older firmware reads a v15 block exactly as it read
a v14 one (a newer version of the same size: its fields are taken, `tests/cr_settings_test.c t_v15`), and the version
number says one thing: this block was written by a firmware that keeps a session beside it.

**0.19 added two bits and no version**: Options > Output in bit 1 of `pick_roots` and Options > USB Rate in bit 2
(the table below). The version stays 15: an older record's bits are 0, which is the default of both (Clean; 44.1 kHz
only), so there is nothing to migrate, and a version bump would have said something the bytes do not. This is the
spare-bit case of "How to add a field" below: its steps 3, 5 and 6 (the range in `crs_sanitize`, the mapping in
`crs_capture` / `cr_settings_load`, a check in `tests/cr_settings_test.c`), not 1, 2 and 4 (no byte taken from the
reserve, no version, no migration line because zero is the default); and, being Options, a line in
`cr_settings_auto` (`CRS_PICK_AUTO`: the byte's automatic bits, its bit 0 stays manual and reserved).

**0.20 added one more the same way**: Options > Chord Names in bit 3 of `pick_roots` (Orchid, the default, is 0;
docs/CHORD-NAMES.md). The version stays 15. Steps 3, 5 and 6 again: `crs_sanitize`'s range is 0 .. 15,
`crs_capture` / `cr_settings_load` map the bit to `cs.names_std` (and tell the engine: `cr_chord_names_set`),
`tests/cr_settings_test.c` checks the round trip, every older byte (0 .. 7) and every older version reading Orchid,
and the bit being automatic; `CRS_PICK_AUTO` has it.

**And a second one in 0.20**: Options > 6th with 7th in bit 4 of `pick_roots` (In place, the default, is 0;
docs/CHORD-NAMES.md "The 6th above a seventh"). The version stays 15. Before taking the bit, what an older firmware
does with a larger byte was read in the code: `crs_sanitize` is `CRS_FIX(pick_roots, 0, hi)`, "a field out of range
takes its default", with `hi` 7 in 0.19 and 15 in the 0.20 builds of before this option, and the default of the
byte is 1 (the retired picker bit; Output Clean, USB Rate 44.1 kHz, Chord Names Orchid). So an older firmware that
meets the bit loses nothing but the three options that share the byte, back to their defaults until set again, and
only when the new option is On; with it Off (the default) the byte is one it knows. That is the same price Output,
USB Rate and Chord Names each paid for the versions before them, and less than a version bump would cost here (a
newer version of the same size is read the same way by an older firmware: the bump would change nothing for it).
The alternative, the upper bits of `extadd` or `secret`, would make an older firmware drop a playing option
instead. Steps 3, 5 and 6: `crs_sanitize`'s range is 0 .. 31; this one is the engine's, so `cr_settings_apply` /
`cr_settings_capture` map the bit to `cr_t.sixth_up` (`cr_set_sixth_up`) and the glue (`crs_capture` /
`cr_settings_load`) to `cs.sixth_up`; `tests/cr_settings_test.c` checks the round trip, every older byte (0 .. 15)
and every older version reading In place, the engine's side, the bit being automatic, and the older firmwares' rule
(modelled from their `CRS_FIX` range: the old binaries were not run); `CRS_PICK_AUTO` has it. Bits 5 .. 7 of the
byte are free, at the same price: an older firmware that meets one resets the byte.

**Felucca's SPEAKER setting (0.19).** The object's head holds Felucca's own fields; one of them, `lowcut` (MENU >
SPEAKER there: 1 LOWCUT, 2 BASS+), is a stage of the master that ChoralRoot has no control for. Until 0.19 a unit
that had run Felucca with it on kept it under ChoralRoot, through every save, backup and Reset Settings. Since 0.19
`cr_settings_boot` sets `settings.lowcut` and `fx_lowcut` to 0 before `settings_init` applies them (and keeps what
it found in `crs_lowcut_found`, the emulator's dump), `crs_rec` keeps what flash holds so that the next write of
the object stores the 0, `cr_backup.c crb_commit` stores 0 from a restored object, and `cr_settings_reset` clears
it. docs/AUDIO-QUALITY.md "Left behind" lists every other value another firmware can leave.

### Import rules (`cr_settings_import`)

`cr_settings_import` returns 1 for a current record, 2 for a migrated one, and 0 when it falls back to defaults.

1. **No defaults path:** an absent or short block, a bad magic, version 0, a size that does not fit, or a bad
   checksum gives **all defaults**. It never loops and never hangs; the test feeds it 2000 garbage blocks.
2. **Older record:** an older version, or a shorter size, is copied over the defaults. Any field it lacks keeps
   its default.
3. **Newer record:** a newer, longer record (a downgrade) keeps its first 192 bytes, which hold our fields.
4. **Range check:** every field is checked against its range. A field out of range takes its default and the
   others are kept.
5. **Lists:** `palette`, `chord_sound` and `bass_sound` are checked against the lists by the glue.

Migrations by version (the `in.version < CRS_VERSION` block): v1 → 2 the metronome fields and the loop slot take
their defaults; v2 → 3 `pick_roots`; v3 → 4 the USB settings (`usb_in` on, `usb_level` Master; the zeros there would
switch the recording off); v4 → 5 the USB playback is removed: `rsv_usb` (v4's `usb_out`) is cleared, `usb_in` and
`usb_level` are kept; v5 → 6 presets per engine (docs/PRESETS.md): `chord_sound` / `bass_sound` (old list positions)
read as the defaults, every `pool_pos` 1; v6 → 7 (0.14: ANALOG retired, FM TONE takes its slot 0): slot 0's chord
place from `rsv_usb` when a 0.14 dev build wrote it there (1..127; a 0.13 record's 0: the first preset), its bass place
from `bass_sound` when the bass played FM TONE (else the first preset), `rsv_usb` cleared; a `chord_sound` /
`bass_sound` on ANALOG (engine 0) reads as the default (TINE EP / DEEP SUB). Both bytes of a 0.14 dev record are read,
the slot is what is written; v7 → 8 `loop_rec` (the byte `rsv_usb` was, 0 since v7) takes its default (Overwrite, Play); v9 → 10 every mode's Hold (`par[m][CR_P_HOLD]`) set to 0: the arp no longer
latches by default (it had no control in the ARP layer); LOCK (Sticky Keys) is the latch, STRUM KNOB 4 still sets Hold;
v11 → 12 (0.16: Dual Core is Off by default and bit 6 of `view` means On): bit 6 cleared in every older record,
whatever it said: **Dual Core Off** (a v11 record's On, bit 6 clear, cannot be told from 0.15's default, and its Off,
bit 6 set, would read as On); the view (bits 0..2) and its lock (bit 7) are kept. A v12 record keeps what it says;
v12 → 13 (0.17: the KEY layer's keys in bit 5 of `loop_rec`, LOCK's styles in bits 6 / 7): the three cleared in every
older record (no writer set them): **pick, Clears, Toggle**, the record mode and the PLAY menu's keys kept (`tests/cr_settings_test.c t_key_keys_v13`);
v13 → 14 (0.17: Key plays and Keyboard in bits 1 / 2 of `key_on`, Single's value 2): an older record's `key_on` is
its Key Mode alone (1 stays 1, anything else reads off: no older writer set the new bits) and a `single` above 1
reads Full Octave: **Chords, Chords**, Split kept as Split Oct (`t_page2_v14`);
v14 → 15 (0.18): nothing to migrate (no field changed; `t_v15` reads v12, v13 and v14 records). A
settings object restored from a backup goes through the same import (`cr_backup.c`), so a 0.14 / 0.15 backup restores
with Dual Core Off.

**0.22: Dual Core's default is On, and the version stays 15.** `cr_settings_defaults` sets bit 6 of `view`: a unit
with no record (new, erased, zeroed), a Felucca / Melodee record without ChoralRoot's block, SAFE MODE's defaults
(where core 1 is not started all the same) and Options > Reset Settings. **A record that is there keeps what it
holds**: its bit 6 clear is 0.16 .. 0.21's default as much as a chosen Off, and nothing else in the record tells
them apart, so no migration turns it On (a v16 that did would switch the second core on for everyone who chose
Off). Where `crs_sanitize` finds an unknown bit or a view out of range in the view's byte it takes the default view,
unlocked, and **keeps the record's Dual Core bit** (before 0.22: the whole default byte, which was Off).
`tests/cr_settings_test.c t_dualcore_v12`; docs/DUALCORE.md.

## The session (0.18)

"Too much is forgotten at power-off. We should get this to the point where you turn off, turn back on, pick up
exactly where you left off." (the owner, 2026-10-11, the session's first build: written by itself when the unit was
quiet. The same day's later decision, above: it is written by the global save alone; "0.18" below means "after a
global save".)

### The inventory

Every piece of state a player can see, whether it came back after a power cycle in 0.17, whether it does in 0.18,
and where it lives. "block" is `cr_settings_t` above, "session" is `cr_session_t` (`cr_settings.h`).

| state | 0.17 | 0.18 | where |
| --- | --- | --- | --- |
| every entry of Options (Play Style .. Dual Core, Hold Time) | yes | yes | block; Hold Time in Felucca's `bold` |
| the panel calibration | yes | yes | Felucca's `panel` in the settings object |
| Key Mode, tonic, scale, Transpose | yes | yes | block `key_on`, `tonic`, `scale`, `transpose` |
| the KEY layer: its keys (pick / play), Key plays, Keyboard, Numerals (0.22: `bass_on` bit 1; its Solfège: `scale` bit 7), Single, the split | yes | yes | block `loop_rec` bit 5, `key_on` bits 1 / 2, `single`, `split_pc` |
| the KEY layer's page, the PLAY menu's page | no | **no, on purpose** | page 1 whenever the layer opens (`cu.kpage`, `cu.lpage`) |
| the performance mode, its parameters, Perform on | yes | yes | block `perform_mode`, `perf_sel`, `par`, `perform_on` |
| LATCH on | yes | yes | block `sticky` |
| the chord LATCH was holding, any sounding note | no | **no, on purpose** | |
| LOCK on | no | **yes** | session `lock` |
| the chord keys LOCK latched | no | **yes** | session `latched` (bit `cr_mod_t`), posted down to the engine at power-on as the keys post them |
| the LOCK layer's two styles | yes | yes | block `loop_rec` bits 6 / 7 |
| the voicing (KNOB 1) | no | **yes** | session `voicing` |
| the octave (OCT) | no | **yes** | session `octave` |
| Bass on | no (off at every power-on, by rule) | **yes** (the rule is superseded by the owner's sentence above) | session `bass_on` |
| which sound the bass part plays | yes | yes | block `bass_sound`, `pool_pos`; and the session's part |
| the bass behaviour, the bass register | yes | yes | block `bass_mode`, `bass_voicing` |
| the bass level (the BASS layer's Level, BASS held + ALGORITHM; OPT + ALGORITHM until 2026-10-12), each part's Level and Pan (MIX) | no ("bass level 102 came back as 92") | **yes** | session `part[k].p[P_LEVEL]`, `p[P_PAN]`: the part's own parameters travel with its record |
| FX on / off | yes | yes | block `fx_on` |
| the page picked in the FX layer (Sends, Reverb 1, Reverb 2, Chorus, Delay 1, Delay 2, Drive, Filter, Routing) | no | **yes** | session `fx.sel` (an effect tag; the second pages, Sends, Filter and Routing have their own) |
| the effects' own parameters, GLOBAL (the reverb's 7 and the delay's 9: [FX.md](FX.md); the chorus's rate, depth and width; the master filter's four, 0.22; the routing's Order and Mode, issue 21) | no | **yes** | session `fx.par[]` (tag, value) |
| the chord part's four sends (the FX layer's "Send" cells, KNOB 4) | no (the preset's again) | **yes** | session `fx.amt[]`. The sends are the PART's since 0.18: no sound load touches them (MANUAL section 8) |
| an FM TONE sound's Drive (its AMP screen; the Sends page's Drive while the part plays FM TONE) | no (the preset's again) | **yes**, with the part's sound as it is | with the SOUND: a user preset's record (`upreset.c`, its `P_DIST` field), a factory preset's table row; the session `part[].p[P_DIST]`. The part's own drive send stays beside it: `fx.amt[DRIVE]` (chord), tag 29 (bass). docs/FX.md "FM TONE's Drive" |
| the bass part's four sends (its editor's FX group, CC 91 / 93 / 94 on the bass channel) | no (the bass sound's again) | **yes** | session `fx.par[]`, tags 26..29 |
| which sound the chord part plays, each engine's last place | yes | yes | block `chord_sound`, `pool_pos` |
| each part's sound as it is: an edit not saved, the deep patch, the name, "edited", the pool entry | no | **yes** | session `part[0]`, `part[1]` (the edit buffers) |
| the tempo, the metronome (on, signature, level) | yes | yes | block `bpm`, `metro_*` (a metronome left on clicks at power-on, as before) |
| the looper's settings, its record mode, the PLAY menu's keys | yes | yes | block `loop_*`, `loop_rec` |
| the looper's Rate | n/a | **yes** | session `loop_rate` |
| the loop slot loaded, the loop in it | yes | yes | block `loop_slot`; the slot's own flash record, written by the global save and when the slot is left (docs/LOOPER.md; until 2026-10-11: each time a changed loop stopped) |
| a loop playing | no | **no, on purpose**: the slot is loaded and STOPPED, it never starts by itself | |
| a take in progress | no | **no, on purpose** | |
| the view and its lock | yes | yes | block `view` |
| an open layer, menu, dialog, the editor; the editor's place (group, screen, lane) | no | **no, on purpose**: the unit starts on the view | |
| what a PANIC cleared (octave 0, LATCH off, the latch empty) | n/a | stays cleared: the state left is the state kept | |
| saved sounds, patches, banks | yes | yes | their own flash objects |

`tools/emu/test_persist.sh` sets one recognisable state (`session_set.txt`: a loop in slot 3, an unsaved edit on each
part, Bass on with its Level down, the delay picked and set, Perform on with Arpeggiate, voicing +2, octave -1, LOCK
with MAJ + M7), checks that 4 s later nothing is in flash, holds SAVE, relaunches on the same flash file and checks
every line of it, that the loop is stopped, that nothing sounds, and that nothing is written again.

### The record (`cr_session_t`, `CSS_SIZE` = 1024 bytes)

Header: `magic` `CSS1`, `version` (`CSS_VERSION`, 1), `size`, `check` (FNV-1a over bytes 12..size), then `bind`
(below) and:

| field | range | default |
| --- | --- | --- |
| voicing, octave | -12..12, -2..2 | 0, 0 |
| bass_on | 0/1 | 0 |
| lock, latched | 0/1, bit `cr_mod_t` (cleared when `lock` is 0) | off, none |
| loop_rate | -7..3: the loop's Rate in steps from x1 (x1/16 x1/12 x1/8 x1/6 x1/4 x1/2 x3/4 **x1** x1.5 x2 x4; `cr_loop.h CRL_NRATE`; -7 .. -4 since 2026-10-11: a 0.18 dev build of before reads them as x1); the first byte of the old reserve, so a record written before it reads 0 = x1 with no migration | 0 (x1) |
| fx (`css_fx_t`, 128 bytes: 108 until 0.22, when `par[]` grew by four entries into `rsv_fx`; 124 until the routing (issue 21), whose entry took `rsv_fx`'s last 4 bytes: `rsv_fx` is gone, `part[]` where it was, offset 196) | the FX sub-block, below | nothing stored |
| part[2] (`css_part_t`, 384 bytes each: chord, bass) | `valid`; `eng`, `preset`, `user`, `trim` (the part's `track_t` fields; **`trim` is written and, since 0.19, not read**: a part put back takes the trim of its pool entry from the running firmware's table, `cr_session_part_trim`: a factory preset its `CB_TRIM` row, a user preset bound to one that row, an added user preset and INIT none, as `cu_pool_load`, so a row removed or added by an update holds for a session saved before it); `edited`, `kind`, `fk`, `slot`, `name` (the UI's `cu_snd_t`: the pool entry the sound came from); `np` and `p[96]` (the writer's `P_COUNT`, 91 today, and every parameter: the engine's eight, the envelopes, the LFO, the modulation, the sends, **Level and Pan**); `nblob` and `blob[160]` (the engine's deep patch, `ENG_BLOB_MAX`: FM6 128, VA 110, CZ-1 144, FM TONE 80; none on the other engines) | not valid: the pool's sound (the block's `chord_sound` / `bass_sound`) |

**Reserve: 122 bytes of the 1024** (46 after the state; none after the FX block since the routing took the last 4 of
`rsv_fx`, 8 in each part, 60 at the end), none of the FX block's 29 parameter entries (all 29 can be stored since the
routing) and 5 of a part's 96 parameters; and the object holds 3840 bytes, so `CSS_SIZE`
itself can grow: the import takes a shorter (older) record over the defaults and a longer (newer) one by its prefix,
and no other firmware and no tool reads this object.

**The FX sub-block is versioned on its own** (`CSS_FX_VERSION`, 1) and stores by TAG, never by position, so that the
delay and the reverb can change their parameters without a new record: `sel` is an effect tag (`CSS_FXE_REVERB` 0,
`CHORUS` 1, `DELAY` 2, `DRIVE` 3, and for the FX layer's second pages `REVERB2` 4, `DELAY2` 5, for its Sends page `SENDS` 6, its Filter page `FILTER` 7 (0.22), its Routing page `ROUTE` 8 (issue 21): `sel` only, a reader
that does not know a tag keeps its default page; `amt[]` has room for 8 and is indexed only by the four effects' tags
0..3, so `ROUTE` 8, past it, is never an `amt` index), `amt[tag]`
that effect's send from the chord part (0..127, `CSS_FX_NONE` = not stored: the sound's own send), and `par[]` up to
24 pairs of a parameter tag and its value. The rules, for whoever reworks an effect:

| Tag | Parameter | Range, default | |
| --- | --- | --- | --- |
| 1 `RSIZE`, 2 `RDAMP`, 3 `RTYPE` | Felucca's ROOM / SPRING reverb | | **retired** with it (0.18's new reverb): never reused, not read, gone from a record at its next save |
| 4 `CRATE`, 5 `CDEPTH` | the chorus's rate, depth (`song.g`) | 0..127, 40 / 60 | |
| 6 `DTIME`, 7 `DFDBK`, 8 `DCOLOR` | Felucca's mono delay | | **retired** with it |
| 9 `XD_TIME` | the first 0.18 build's free time (21 .. 1000 ms, the delay at half rate) | | **retired** (25) |
| 10 `XD_DIV` | .. its synced division (1/32 .. 1/2) | 0..12, 6 (1/8) | `XP_DDIV` |
| 11 `XD_SPREAD` | .. the free spread (x1.00 .. x0.50) | 0..127, 0 | `XP_DSPREAD` |
| 12 `XD_RATIO` | .. the synced spread (1:1 3:4 2:3 1:2) | 0..3, 0 | `XP_DRATIO` |
| 13 `XD_FDBK` | .. feedback | 0..127, 58 | `XP_DFDBK` |
| 14 `XD_SYNC` | .. Free / Tempo | 0..1, 1 | `XP_DSYNC` |
| 15 `XD_MODE` | .. Stereo / Ping-pong | 0..1, 1 | `XP_DMODE` |
| 16 `XD_TONE` | .. the loop's low-pass | 0..127, 70 | `XP_DTONE` |
| 17 `XD_WOW` | .. the time's wobble | 0..127, 24 | `XP_DWOW` |
| 18 `XR_SIZE` | the reverb's size | 0..127, 88 | `XP_RSIZE` |
| 19 `XR_DAMP` | .. damping | 0..127, 56 | `XP_RDAMP` |
| 20 `XR_DECAY` | .. decay (0.3 .. 72 s, 127 Inf) | 0..127, 52 (3 s) | `XP_RDECAY` |
| 21 `XR_PRE` | .. pre-delay (0 .. 138 ms) | 0..127, 16 | `XP_RPRE` |
| 22 `XR_MOD` | .. modulation | 0..127, 56 | `XP_RMOD` |
| 23 `XR_LOW` | .. low cut (20 .. 500 Hz) | 0..127, 60 | `XP_RLOW` |
| 24 `XR_WIDTH` | .. width | 0..127, 127 | `XP_RWIDTH` |
| 25 `XD_TIME2` | the delay's free time (16 .. 740 ms: the delay at the full rate) | 0..127, 91 | `XP_DTIME` |
| 26 `BS_REV`, 27 `BS_CHO`, 28 `BS_DLY`, 29 `BS_DRIVE` | the BASS part's sends (the chord part's are `amt[]`) | 0..127; 10, 0, 0, 0 | `trk[1].p[P_REV ..]`; 29 while the bass part plays FM TONE: `cu_bdrive` (`cr_ui.c cu_bdrive_p`: the part's send waits there, the track's `P_DIST` is the sound's Drive and is stored in `part[1].p[]`) |

| 30 `XC_WIDTH` | the chorus's width (0.21; KNOB 3 of the Chorus page) | 0..127, 127 | `XP_CWIDTH`. A record without it (0.19, 0.20): 127, their chorus. 0.19 and 0.20 skip it and write their record without it |
| 31 `XQ_LOW`, 32 `XQ_HIGH`, 33 `XQ_SLOPE`, 34 `XQ_ON` | the master filter's Low cut, High cut, Slope, Filter (0.22; the FX layer's Filter page, [FX.md](FX.md) "The master filter") | 0..127, 0 (Off); 0..127, 127 (Off); 0..1, 1 (12 dB); 0..1, 0 | `XP_FLOW` .. `XP_FON`. **Written only off their defaults** (`CSS_FXG`'s `opt`): a unit that never touched the filter writes 23 entries as before, and a firmware before 0.22 reads its record as before; one that did writes up to 27, and a firmware before 0.22 reading more than 24 entries takes the whole FX block's defaults (its `cr_session_import`: the effects' settings and the chord part's sends, not the sounds, not the rest of the session) |
| 35 `XF_ORDER`, 36 `XF_RMODE` | the routing's Order and Mode (GitHub issue 21, second design; the FX layer's Routing page, [FX.md](FX.md) "The routing") | 0..23, 0 (Chorus > Delay > Reverb > Filter: `cr_fx.c XF_ORD`); 0 Parallel / 1 Chain, 0 | `XP_ORDER`, `XP_RMODE`. **Written only off their defaults**, as the filter's: a unit that never touched the routing writes its record as before. The first design (b1e1222, one dev unit, never released) stored an order 0..5 under 35 and a Feed 0..127 under 36: a value out of range takes the default, so such a Feed above 1 reads Parallel. A firmware of 0.22 .. 0.26 (28 entries) reads a record of up to 28 entries as before and skips 35 / 36 as unknown tags (it has Parallel only), and finds `sel` 8 unknown (the FX layer opens on its default page); a record of 29 entries (Order AND Mode AND all four of the filter's tags off their defaults) makes it take the whole FX block's defaults, as above. A firmware before 0.22 takes the defaults from 25 entries on |

29 entries can be stored (29 fit, `CSS_NFXP`: 24 until 0.22, when `par[]` grew by four entries into `rsv_fx`, 28 until
the routing took its last 4 bytes for one more, the entries before them where they were and `CSS_FX_VERSION`
unchanged; **none is left**; the next effect parameter needs the layout to change, or a place outside `par[]`). A session written by 0.17 or by a 0.18 build before the new effects holds
tags 1..8: the chorus's two are read, the six retired ones are ignored, and the new delay and reverb start at
their defaults ([FX.md](FX.md), decision 13). `cr_settings.c` has a static assert on the numbers.

- a new parameter: a new `CSS_FXP_*` number and a line in `cr_settings.c` `CSS_FXG` (tag -> `song.g`'s `G_*`, or
  `cr_fx.c`'s `XP_*` with `x` 1);
- a parameter whose meaning or range changes: a NEW tag; the old number is retired and never reused;
- a parameter removed: its line goes, its number stays retired;
- a reader skips a tag it does not know, keeps its default for a tag it does not find, and for a value outside
  the parameter's range (`GP[g].min..max`, `XFP[id].min..max`);
- `CSS_FX_VERSION` goes up only if `css_fx_t`'s layout changes; a reader that sees another version takes the
  defaults for the whole block (the sounds' own sends, the effects' defaults) and keeps the rest of the session.

**Import** (`cr_session_import`, the block's rules): no record, a bad magic, version 0, a size that does not fit or
a bad checksum: the defaults, which are "nothing stored" (the unit powers on as 0.17 did); a shorter record: what it
has, zeros for the rest (0 is the default of every later field; another default needs a migration line); a longer
one: its first 1024 bytes; every field range-checked on its own; a part with more parameters or a longer blob than
the record can hold: no sound stored. The glue then refuses a part whose `np` is not this build's `P_COUNT`, whose
engine is gone or not a melodic one, whose blob is not that engine's size, or whose pool entry cannot be
(`css_part_put`): that part keeps the pool's sound. `tests/cr_settings_test.c t_session` (round trip of every item,
2000 garbage blocks, shorter, too-new, the FX block by tag and by version, the ranges).

### Where it lives, and its wear

`OBJ_SESSION` (`storage.c`): the A/B sector pair **0xB4000 / 0xB5000**, the first two of the 20 sectors that were
free after FM TONE's patch store (0xB2000 / 0xB3000) in what was Felucca's user sample slot 2 (ChoralRoot has no
SAMPLE engine; docs/HANDOFF.md's flash map and `storage.c`'s said 0xB4000..0xC7FFF free; since 0.22 the favourites
take 0xB6000 / 0xB7000, below, and 0xB8000..0xC7FFF still is).
It is inside `FL_DATA` (`hal/fm1_flash.h`: the HAL writes there), outside the firmware, the update's staging area and
every other object. The same commit protocol as every object: the other copy is erased and written, its header
last, so a write torn by the power switch leaves the previous session in charge (`t_session_flash`).

- **SAFE MODE** neither reads nor writes it (`ST_BLOCKED`), and Options > Flash Data erases it with the rest
  (`cr_ui.c cu_flash_erase`: 76 sectors after 0.24-beta2, with the sound pool's 16; 60 from 0.22, with the
  favourites' two; 58 until then).
- **Backup**: it is NOT a backup object (no new id; `web/fm1backup.js`, the manager and `tools/fm1_install.py` are
  unchanged). A backup holds what you keep (settings, saved sounds, loops), not where the unit happened to be. A
  settings object restored over SysEx empties the session (`cr_backup.c crb_commit`: a zero-length object), so the
  restart comes up on the restored settings alone.
- **Wear**: a session save is one sector erase (since 0.22 done ahead, at power-on or after 10 s idle) and five page programs. It happens only at a global save (SAVE held),
  and only if the state differs from what flash holds (a hash compare: a save with nothing changed writes nothing).
  The two copies alternate, so a sector is erased every second save. The part is rated for 100 000 erases a sector:
  200 000 session saves, which is 50 saves a day for eleven years. The settings object's pair: the same at a global
  save, plus an automatic write when an Option changed.

### The favourites are a third object (0.22)

Not a setting and not in the block (which never grows): the marks on sounds (and, since 3y42, the sounds' types and
the favourites filter's Off / On, which Options > Reset Settings therefore leaves as it is) are `OBJ_FAVS`, the pair **0xB6000 /
0xB7000**, written by `cr_favs.c crf_flush` with the same commit protocol (docs/PRESETS.md "Favourites" has the
format and the keys). It is AUTOMATIC in the sense of [Manual and automatic](#manual-and-automatic-018): `crf_poll`
(called from `cr_settings_poll`) writes a changed mark once no mark changed for `CRS_QUIET_MS` and nothing sounded for
`CRS_IDLE_MS` (and, when the write has to erase, no input came for 4 s:
[When flash is written](#when-flash-is-written-022-the-quiet-writes)), never while a loop plays or after a restore, and `cr_settings_commit` (SAVE held) and
`cr_settings_save` (the calibration, the update gesture) write a pending one at once; a backup's LIST does not (3y48:
it lists the pending marks from RAM). GLO + SAVE (revert) does not
touch the marks; Options > Reset Settings does not either. It IS a backup object (id 23), unlike the session: marks
are something you keep. SAFE MODE neither reads nor writes it; Flash Data erases it. Wear: one sector erase a write
(done ahead since 0.22, not at the write), the two copies in turn.

### `bind`: which settings a session belongs to

A session is used at power-on only next to the settings it was written beside:

- the settings block read as **current** (v15: this firmware wrote it; an older firmware in between would have left
  its own version there): used;
- any other block (none, the defaults, a migrated v1..v14 one): used only if the settings object in flash is still
  byte for byte the one the session was written beside (`bind` = the object's payload CRC then, 0 = none): a new
  unit that has only ever changed session state, or a unit just updated from 0.17 that has not changed an option
  since;
- otherwise (0.18, then 0.17 which saved a setting, then 0.18 again; another firmware's settings; sectors another
  firmware erased) it is left alone and the unit starts from the settings: `session: .. not these settings'` in the
  emulator's dump (`test_persist.sh`: the flash of the session run with its settings object rewritten as v14).

### 0.25.1: FM-1 the default

**Every unit moved to the FM-1 chord keys once, with no version.** The owner, 2026-10-16: "the new FM-1
chord key layout is significantly better on this device than the classic, let's make it the default", then "Units
from old should get put on the new chord key layout". So `cr_settings_defaults` sets `sticky` bit 1 (a unit with no
record, Flash Data's erase, SAFE MODE's defaults, Options > Reset Settings), and `cr_settings_import` moves every
record that no 0.25.1 firmware wrote to FM-1, whatever it holds: 0.23 and older (no Chord Keys), 0.24 / 0.25 with
Classic or FM-1. What tells the two apart is **bit 1 of `leds`** (`CRS_KEYS_MOVED`), set in the defaults and by the
move, and written with every save after it (the byte is automatic, `cr_settings_auto` takes it whole; `crs_capture`
keeps the record's mark beside the UI's LEDs bit). A record with the mark keeps the layout it holds, so a player who
chooses Classic after the move keeps it, across power cycles and later updates (`tests/cr_settings_test.c
t_keys_default`; `tools/emu/test_persist.sh`, "0.25.1: FM-1 is the default chord keys"). The move changes `sticky` bit 1
and the mark only: LATCH, Full K Shorts, the SUS Window, LEDs Stock and every other field are kept, and the import's
result is unchanged (a 0.25 record is still **current**, rc 1).

- **Why not a version.** A v16 would do the same job, and every 0.18 .. 0.25 firmware already reads a newer version
  of this size. But the session is applied at power-on only beside a block read as current, or one whose object is
  byte for byte the one it was saved beside (`bind`, above): a v15 block read by 0.25.1 would be "migrated", and so
  would a v16 block read by 0.25 after a downgrade. On a unit where an Option changed after the last SAVE (the Options
  are written by themselves, the session only by SAVE) both would drop the session: the sounds, Bass on, LOCK and its
  latch. The mark costs one bit and leaves rc and `bind` as they were.
- **Why `leds`.** Every byte's spare bits have the same price on a downgrade: an older firmware takes the whole byte's
  default. `leds` costs the least of them: 0.18 .. 0.25 check it with `CRS_FIX(leds, 0, 1)`, so a marked byte reads
  as their default, LEDs **Glow**, and a player who chose Stock sees Glow there until it is set again; nothing else.
  `sticky`'s bit 7 would cost LATCH and the three chord layout Options there, `raw_sound`'s Raw Chord Sound and MIDI
  Keys, `fx_on` the effects.
- **The switch runs once.** The move needs a record without the mark. Every record this firmware writes has it (the
  defaults, the move, a save), so a record written by 0.25.1 or later is never moved again. Until the first save after
  the update the record in flash is still the older one, and each power-on moves it again in RAM, to the same FM-1:
  nothing is written for the move itself (`test_persist.sh`: 0 writes), and the first save (any Option, SAVE held)
  stores FM-1 with the mark. The only record moved a second time is one an older firmware wrote again after a
  downgrade (its save has no mark): that is an update from an older firmware, and the owner's rule is that those come
  to FM-1.
- **A downgrade** (no binary of an older release was run; the reading follows each one's `crs_sanitize`): **0.25 and
  0.24** read `sticky` with its range 0 .. 0x7F, so they show the layout the record holds: FM-1 after the move, Classic
  if the player chose it since; LATCH, Full K Shorts and the SUS Window as stored; LEDs Glow (the marked byte is out
  of their range). **0.23 and older** have no Chord Keys and read `sticky` with `CRS_FIX(sticky, 0, 1)`: with FM-1 set
  the byte is out of range, so LATCH reads off (they play their own Classic keys); LEDs Glow as above. Nothing else
  changes and nothing breaks; the version is still 15, so the session is applied as before.
- **A backup restore** goes through the same import (`cr_backup.c crb_commit`): an older backup's settings object (no
  mark) restores onto FM-1 with the rest as the backup has it, and a backup made by 0.25.1 restores what it holds,
  Classic included (`tests/cr_backup_test.c`, "0.25.1: FM-1 is the default Chord Keys").
- **LOCK across the move.** LOCK and its latch are the session's (`lock`, `latched`), and `latched` is a set of chord
  types (`cr_mod_t`: DIM .. 9), not of keys. So a unit updated with LOCK on keeps it on and keeps the same chord
  latched, which the FM-1 keys show where they are now: a latched SUS on G#3 + A#3, a latched 9 on B3, LOCK's light on
  LFO; C4 and C#4, which held SUS and the 9 in Classic, are roots. The first root played sounds the same chord as
  before the update (`test_persist.sh`: SUS + 9 latched under Classic, then D4 on FM-1 is Dsus 9). This is the
  Options' own rule for a layout change (docs/CHORD-LAYOUT.md "Changing the layout"), without the keys to release:
  nothing is held at power-on.
- **The SMK-37** (Chord Keys hidden, Classic: `cr_settings_load` reads the layout as Classic there) neither sets the
  bit nor moves a record: `BOARD_SMK37` leaves its defaults and import as 0.25's.

### Compatibility, both ways

- **0.18 on a 0.17 (or older) flash**: the block migrates as ever (v1..v14), there is no session: the unit powers on
  as 0.17 would, and from the first change on it keeps its session.
- **0.17 (or older) on a 0.18 flash**: it reads the v15 block as it read v14 (same size, same fields; `bass_on` is 0
  there, so no engine / UI mismatch), keeps the panel calibration and every option, and never looks at
  0xB4000 / 0xB5000: voicing 0, the bass off, the pool's sounds, as 0.17 always powered on. Its SAFE MODE erase does
  not cover the session's sectors; `bind` is what keeps that stale session from coming back later.
- **backups**: a 0.17 backup restores on 0.18 and a 0.18 backup on 0.17, the settings object being the same size.

### Runtime

`cr_settings_boot` reads the object (`css_read`) into the one RAM copy, `css_buf` (1024 bytes in POOL: the session at
power-on, or at a revert, until `cr_settings_load` has applied it, afterwards the scratch the state is captured into
for a write; what flash holds is kept as a 32-bit hash, so "unchanged: no erase" needs no second kilobyte). `cr_settings_load` applies it once, at power-on, after the block (`css_apply`): the two parts as
the engine picker's cancel restores a sound (`cu_pick_end`), the FX parameters by tag, the amounts, the voicing and
Bass on directly in the engine (IRQ off, nothing sounds yet), the octave, LOCK and its latch through the engine's
queue. The session is captured and written by `cr_settings_commit` (the global save) and by nothing else: until 2026-10-11
`cr_settings_poll` captured it every 200 ms and wrote a change when the unit was quiet (`css_poll`: gone). The
emulator's dump prints `session: saves N (at power-on: ..) now HASH flash HASH pending P ..` (pending 1: the state
differs from flash's, a global save would write it), the FX layer and each part's sound.

## How to add a field

A new option that fits a spare bit of the block (the list above): of the steps below, 3, 5 and 6, with zero as its
default so that 2 and 4 are not needed (Output and USB Rate, 0.19; Chord Names and 6th with 7th, 0.20); a field of its own: steps 1 to 6. **Anything that needs a byte:
the session record** (`cr_session_t`): take it from `rsv` (the state), `rsv_fx`, a part's `rsv` or `rsv_end`,
default 0, range in `css_sanitize`, capture and apply in `css_capture` / `css_apply`, a check in
`tests/cr_settings_test.c t_session`, a line in the inventory above; bump `CSS_VERSION` only if an older record's
zero there would be wrong (then a migration line in `cr_session_import`). An FX parameter: a tag (above), no version.

1. Take its bytes from the front of `rsv` in `cr_settings.h` and shrink `rsv` by that many. `CRS_SIZE` never
   changes, and the build checks it (`crs_size_ok`).
2. Bump `CRS_VERSION`.
3. Set its default in `cr_settings_defaults`, and its range in `crs_sanitize` (`CRS_FIX`).
4. If an older record's bytes there are not zero, or zero is not the default, add a line to the
   `in.version < CRS_VERSION` block of `cr_settings_import`: `if (in.version < 2) s->x = d.x;`.
5. If the field is the engine's, map it in `cr_settings_apply` / `cr_settings_capture`. If it is the UI's, map it
   in `crs_capture` / `cr_settings_load` (the glue part of `cr_settings.c`).
6. Add a check to `tests/cr_settings_test.c`.

Never reorder or remove a field. A retired field stays in place, unused.

## Runtime

| call | where | does |
| --- | --- | --- |
| `cr_settings_boot()` | `persist_boot` (cr_shim.c), the emulator's `emu_fw_init` | the flash object → Felucca's fields (panel table, palette, HOLD, LEDs) + the block; the USB settings to `usb.c` (`ua_off`) and `fx.c` (`fx_usb_fixed`) through `crs_usb_apply`, before `usb_start` builds the configuration the computer first reads |
| `cr_settings_load()` | the end of `cr_ui_init` (**patch below**); the emulator calls it after `cr_ui_init` until then | the block → engine (IRQ off), `cr_route`, the UI mirror `cs`, motion, palette, the sounds, tempo |
| `cr_settings_poll()` | every frame (`main.c` `settings_poll` → cr_shim.c; the emulator's `emu_fw_frame`) | captures the state (the block, and Felucca's HOLD beside it: `crs_hold_seen`, below); a change **of an automatic field** (`cr_settings_auto`; a manual one alone starts nothing) is written, as flash's block with the automatic fields of now, once nothing changed for 1.5 s and nothing has sounded for 1 s (no voice of the parts, no chord held or latched, no scheduled note, the master output under -60 dBFS: a flash erase stops the audio for ~45 ms, docs/INTEGRATION.md Performance), never while a loop plays (`CR_SETTINGS_BUSY()`), retried 1 s after a flash error, and skipped when the bytes are unchanged |
| `cr_settings_save()` | `settings_save` (cr_shim.c): the power-on calibration; the update gesture (not a backup's LIST since 3y48) | the automatic part now |
| `cr_settings_commit()` | `cr_ui.c cu_gsave_now` (SAVE held), `cr_settings_reset` | the global save: the whole block and the session, now; 0, or -1 on a flash error |
| `cr_settings_revert()` | `cr_ui.c cu_revert` (GLO + SAVE) | flash's block (the Options of now over it) and flash's session applied as at power-on |
| `cr_settings_reset()` | Options > Reset Settings, OCT+ twice (`cr_ui.c cu_reset`) | the defaults: into the block (`cr_settings_factory`), applied (the default sounds, `cr_settings_load` again), saved; 0.18: the session with it (the parts' own parameters, the FX parameters and the effect picked back to `cr_ui_init`'s), so what is saved is a new unit's session |

The automatic saving is driven by change detection, so `cr_ui.c` needs no save call at each Options entry. The poll compares the
block as captured with the last one seen (`crs_seen`). One setting of the Options lives outside the block: **Hold
Time** is Felucca's HOLD (`panel.c` `settings_hold`, saved in `bold`), so the poll watches it beside the block
(`crs_hold_seen`) and a change of Hold Time alone is saved like any other; `crs_write`'s `settings_export` puts it
in the record. Felucca's other fields need no watch: the panel table is saved by the calibration
(`cr_settings_save`), and the palette, LOWCUT and Felucca's LEDS have no control in ChoralRoot's UI (its palette and
LEDs are the block's).

The only call `cr_ui.c` needs
is the load at init. **The patch for `cr_ui.c`**, at the end of `cr_ui_init()`, after `cu_set_tempo(...)` and
before `song.grid = 2;`:

```c
#if CR_HAVE_SETTINGS
    cr_settings_load();                           /* the saved settings over the defaults (docs/SETTINGS.md) */
#endif
```

`cr_settings_load` is defined after `cr_ui.c` in the unit, so `cr_ui.c` also needs a forward declaration near
its top: `static void cr_settings_load(void);`. That declaration must sit inside the same `#if CR_HAVE_SETTINGS`.
The emulator does not define `CR_HAVE_SETTINGS`; there it includes `cr_settings.c` from `emu_fw.c`. Once the
patch is in, define `CR_HAVE_SETTINGS 1` in `emu_firmware.h` too, or keep the emulator's guard: `emu_fw_init`
calls `cr_settings_load()` only `if (!crs_loaded)`.

## Defaults, and Options > Reset Settings

"Default" is one thing: `cr_settings_defaults()` (part 1, portable). Three ways lead to it:

- **no usable record at power-on** (`cr_settings_boot`): no settings object (a new unit, erased sectors), an object
  that is not PER1..PER5, a Felucca / Melodee PER1..PER4 object (`settings_import` zeroes the block), or a block
  without the `CRS1` magic, cut short, or with a bad checksum: `cr_settings_import` leaves the defaults. Play Style
  is Simple on every one of them (`tests/cr_settings_test.c t_fresh_playstyle`, `test_persist.sh`: a PER4 object and
  erased settings sectors made from a flash that had Advanced saved);
- **SAFE MODE**: the record is not read (the defaults for this power-on, nothing written), nor the session; its
  Options > Flash Data erases the data sectors, the settings and the session among them, and the next power-on is
  the first case;
- **Options > Reset Settings** (0.17, the last entry outside SAFE MODE; OCT+ asks, OCT+ again does it; another entry,
  OCT-, HOME or a closed menu drop the ask, as they now do for Flash Data's): `cu_reset` ends the session as a PANIC
  does without its screen (`cu_quiet`: notes off, the loop stopped, LATCH off, the octave 0; plus LOCK off and the
  voicing 0), then `cr_settings_reset()`: `cr_settings_factory(&crs_rec.cr)` (the defaults, keeping `loop_slot`: the
  loop in RAM is that slot's, and the loops are not the reset's), Felucca's HOLD to its default, the parts as
  `cr_ui_init` leaves them (TINE EP and DEEP SUB loaded, the part levels, the bass off), `cr_settings_load()` again
  (the block -> the engine, the routing, the UI mirror), the looper's configuration and USB Record re-applied,
  `cr_settings_commit()`: the reset resets both sides, the manual and the automatic, and writes at once (it is a
  deliberate act: no SAVE held is needed after it). The record then captured is byte for byte a fresh unit's (the emulator dump's
  `settings: now` checksum; `test_persist.sh`), and since 0.18 so is the session (`session: .. now`): the parts'
  Level and Pan and every other parameter a sound's load keeps, the effects' parameters, the effect picked, with
  LOCK, the voicing and the octave that `cu_reset` clears, and the bass off. An edit not saved is gone: the one place
  where it is, now that a power-off keeps it.

What the reset does not write: the panel table (`persist_t.panel`: `settings_export` writes the table in RAM, as
every save does), the favourites, and every other flash object but the session (saved sounds, VA / FM6 / CZ / FM
TONE patches and banks, the loops): `test_persist.sh` compares the flash outside the settings object and the
session's pair byte for byte and the panel table inside it. Dual Core: On is saved (the default since 0.22); `cr_c1_off` is read at power-on only, so a
second core that this power-on did not start starts at the restart (the message says `restart`). In SAFE MODE the entry works on the session only (`ST_BLOCKED`: no write).
Dual Core is also written Off by the unit itself (0.22, docs/DUALCORE.md "The persistent fallback"): after a crash,
a watchdog reset or a give-up with the second core running, `cr_settings_boot` keeps core 1 off and `cr_settings_load`
reads the option as Off and primes the automatic save (`crs_dc_bar`), so the record's bit 6 is cleared at the next
quiet moment; the user's own Options > Dual Core clears the bar (`cr_settings_dc_user`). The fallback wins over the
default: a Reset Settings in that power-on saves Off, not On (`crs_dc_bar` still set). The single-core package never
bars (`cr_c1_bar_boot`: core 1 never starts there) and so never writes Dual Core.
Tests: `tests/cr_settings_test.c t_factory`, `reset_set.txt` / `reset_do.txt` / `reset_check.txt`.

## Power-on calibration

OCT− + OCT+ held at power-on still runs Felucca's HARDWARE CALIBRATION:

- In the ChoralRoot unit, `main.c` has its own port, `cr_panel_setup`, because `ui_input.c` is not in the unit.
  `main.c`'s `#define panel_setup cr_panel_setup` picks it, so cr_shim.c's empty stub goes unused.
- `settings_save` then writes the table into the same record.

## The emulator's flash

`tools/emu/emu_hal_fw.h` holds the 1 MiB NOR as a RAM image backed by a file. It is read at power-on, and every
erase and program is written through to the file, so a crash or kill keeps what was saved. NOR rules apply:

- an erase sets a 4 KiB sector to 0xFF;
- a program only clears bits and must not wrap a 256-byte page.

`emu_fw.c` builds Felucca's `storage.c` and `cr_settings.c` on it with `FELUCCA_FLASH 1`. The rest of the UI is
still built RAM-only by `emu_firmware.h`.

| option | meaning |
| --- | --- |
| `--flash PATH` | the image file (default `build/emu/flash.bin` in windowed runs) |
| (headless) | no file unless `--flash` is given: every scripted run starts fresh and stays deterministic |
| `--no-flash` | RAM only |
| `--save-on-exit` | save the automatic settings at exit, without waiting for the 1.5 s quiet time (`cr_settings_save`; a global save is a script's `btn SAVE 900`) |

`emu_fw.c` takes these options out of `argv` before `emu.c` parses them; they become `--demo`, which ChoralRoot
ignores. The dump (F12 / `dump`) prints the flash file, the number of writes and saves, and whether the record
read as current, migrated or defaults.

`sh tools/emu/test_persist.sh` checks it end to end:

1. SELECT +17 sets 137 BPM, the PLAY menu's mode and keys and Play Style are changed; SAVE held; the record is saved
   once.
2. A relaunch on the same file reads 137 BPM. The BPM meter is captured to `build/emu/test/persist_bpm.ppm`.
3. A run without `--flash` reads 120 BPM.
4. Manual and automatic (`gsave_none.txt`, `gsave_save.txt`): the tempo, the KEY layer's tonic and an Option changed
   and NOT saved: after a relaunch the Option is there, the tempo and the key are not; then SAVE held (`saved`), more
   changes, GLO + SAVE (`reverted`: the tempo, the voicing, Bass on, the chord sound, LATCH back as saved, nothing
   sounding, the Option untouched), GLO + SAVE let go early (nothing); the relaunch: as saved.
5. VA / FM6 / CZ-1 / FM TONE: a deep edit saved to U01, SAVE held; the relaunch powers on with U01 and its patch as saved.
6. Hold Time alone (an Option): set to 600 ms and nothing else: saved once, by itself; the relaunch reads 600 ms.
7. The KEY layer's keys, the LOCK layer's styles, the KEY layer's page 2: set, SAVE held: saved once; the relaunch has
   them in the UI and in the engine.
8. The loops by hand (`loop_persist_stop` / `_save` / `_switch` / `_revert` + `_back`: docs/LOOPER.md).
9. The session: `session_set.txt` / `session_check.txt`, the inventory's state set, nothing written for 4 s, SAVE
   held, found again after a relaunch, the loop stopped, nothing written again; the same flash with a v14 settings
   object: the session not applied; `session_storm.txt`: knobs turned and left alone write nothing, each SAVE held
   writes what changed, a second one nothing; `session_hold.txt`: SAVE held under a held chord writes at once (the
   erases' silence in the dump), an Option changed under it waits for silence; the reset runs: the session a new
   unit's.

## The sound pool's sectors (0.22)

0xB8000..0xC7FFF, free until 0.22, holds the sound pool (docs/PRESETS.md "The pool"): 16 sectors over which the 13
banks of 10 user slots float (storage.c `sp_*`: a commit record per sector, type 0x4C50, the newest seq per bank wins).
The legacy sound objects (0x97000..0x9EFFF, 0xA0000 / 0xA1000, 0xB2000 / 0xB3000, 0xDC000..0xDFFFF) stay where they are,
read once by the migration and never written by 0.22. The pool's target sector is erased ahead with the favourites'
and the session's spare copies (power-on, a long idle). SAFE MODE neither reads nor writes it; Options > Flash Data
erases its 16 sectors with the other data sectors (`cr_ui.c cu_flash_erase`, 76 sectors in all). 0.22 to 0.24-beta2
left them out of that list, so a Flash Data kept every user sound while it erased the legacy objects and the favourites.
