# ChoralRoot on the M-Vave SMK-37

ChoralRoot also builds for the M-Vave SMK-37, a 37-key controller with the same chip as the FM-1 (JieLi AC791N, 1 MiB
of flash). The FM-1 build is unchanged; everything for the SMK-37 is behind `BOARD_SMK37`. On the SMK-37 it is the
same ChoralRoot: the same sounds and engines (GRAIN among them), screens, boot and [manual](docs/MANUAL.md), with
velocity-sensitive keys, the chord block and the performance modes on the pads, and the SMK-37's extra controls.

**Status: a community test build of 0.27.** The port is genkigenki's (M. Geisler, genkigenki / makkuro:
[PR #14](https://github.com/Quixotic7/ChoralRootFM1/pull/14) and the porting guide
[fm1-to-smk37](https://github.com/genkigenki/fm1-to-smk37)), brought onto 0.24-beta2 here and built from the FM-1's
releases since (0.27 now). It is **community-tested, not tested by the maintainer**: the maintainer has no SMK-37, so nothing on
this page has been tried on a unit by the project. What only a unit can answer is listed under
[What to test](#what-to-test). Report what you find in the project's issues.

## Supported models and packages

All of them are the same hardware; each package carries its model's name, so M-Vave's updater still takes the unit's
factory package afterwards. The test packages (`build/release-smk37/`, see [Build](#build)):

| Model | Package | Identity |
| --- | --- | --- |
| SMK-37 Pro (and the Starrykey 37 Play) | `choralroot-0.27-smk37.fwsc` | `SMK-37 Pro_920` |
| SMK-37 Elite | `choralroot-0.27-smk37-elite.fwsc` | `SMK-37 Elite_920` |
| MKE-P37 | `choralroot-0.27-smk37-mke.fwsc` | `MKE-P37_920` |

Each has a `-1core` twin (`choralroot-0.27-smk37-1core.fwsc`, ...): the same firmware on one processor core, for
a unit that crashes or freezes with the standard package (as the FM-1's, [docs/DUALCORE.md](docs/DUALCORE.md)). The
version a player sees (the splash, Options > Version, `fm1_install.py --info`) is `ChoralRoot 0.27-smk37`
(`... 1-core` for the twin). 0.27-smk37 is the FM-1's 0.27 on the SMK-37 ([docs/releases/0.27.md](docs/releases/0.27.md):
with Dual Core On, VA and FM6 chords shared between the two cores and the VA's code in RAM, the same sound bit for bit;
the screen's background pure black), with everything of 0.25-smk37 (GRAIN's MORPH a blend of the two sources,
[docs/releases/0.25.md](docs/releases/0.25.md)) and of 0.24-beta3-smk37.1, genkigenki's fix for the stock firmware's
mapping among it (below, "If every key sends the same note after going back"). The FM-1's chord layout options
(Chord Keys, SUS Window, Full K Shorts, MIDI Keys) are hidden on the SMK-37: its chord block is on the pads and every
key is a root.

## Controls

The manual still applies, with one change: the chord block's keys (DIM MIN MAJ SUS, 6 m7 M7 9, LOCK) are on the pads,
so the whole keybed plays roots. The SMK-37's buttons and knobs map onto the FM-1's. The screen keeps the FM-1's names
(SELECT, SEL, ENV, GLO, HOME, OCT-, ...); the table translates them.

| SMK-37 | ChoralRoot |
| --- | --- |
| Keys C3..C6 (all 37) | roots, with velocity: a chord with a chord pad held (or latched by LOCK), a note on the Full keyboard; with the octave shift. Where the root keys pick (a layer's map, naming), D4..G5 pick as on the FM-1 |
| Pads 1-4 (top row) | DIM, MIN, MAJ, SUS: the chord types (white) |
| Pads 9-12 (bottom row) | 6, m7, M7, 9: the extensions (orange) |
| Pad 13 | LOCK (blue; as B3: tap to switch it, hold for the LOCK layer) |
| The chord pads and LOCK | they are the FM-1's chord keys: a pad holds its function while it is held, pads combine as the keys do, LOCK latches them as on the FM-1, MIN / MAJ pick the scale in the KEY layer. Bright while held or latched (LOCK: while on), dim otherwise |
| Pads 5-8 (top row) | the free-timing performance modes: Strum, Strum 2 Octaves, Slop, Harp (cyan) |
| Pads 14-16 (bottom row) | the tempo-synced performance modes: Arpeggiate, Arp 2 Octaves, Pattern (green) |
| The mode pads | a tap picks that mode and switches the performance on; the playing mode's pad again switches it off, as a PERF tap. Bright: the mode playing; dim: the others. PERF (ARP) and the PERF layer show the same state |
| K1 | MASTER (volume): a turn shows the level; it is remembered across power-off |
| K2 / K3 / K4 | SELECT / ALGORITHM / PRESETS |
| K5..K8 | KNOB 1..4 (the SAVE menu and the naming help say K5..K8) |
| SCALE / CHORD / NOTE REPEAT | KEY (printed SEL) / BASS (ENV) / LATCH (LFO) |
| PARA / GLOBE | EDIT / OPT (GLO) |
| PATCH / BT | HOME / SAVE |
| FX / ARP / SEQ | FX / PERF (ARP) / SEQ |
| SEQ PLAY / SEQ REC | LOOP (PLAY; its light red with a loop in the slot, green while it plays) / REC |
| OCTAVE- / OCTAVE+ | OCT- / OCT+ |
| KNOB BANK | the view menu (as HOME held); again: closed. Lit while it is open |
| < / > | where SELECT picks or pages (a layer, a menu, the Options, the sound editor, the save dialog): its step back / forward, never the tempo; on the view: the view before / after. Lit where they page |
| FADER BANK | what the faders set: A the chord part's sends, B the bass part's (lit on B) |
| Faders 1..4 | the sends into Drive, Chorus, Delay and Reverb (the order of the FX layer's Sends page), shown as they move; a fader takes over only once it moves, also after FADER BANK |
| Pitch wheel | bends the chord and the bass part (their channels' bend range: 2 semitones unless set over MIDI); also sent on the CHORD and BASS MIDI channels |
| Mod wheel | vibrato on both parts (5.5 Hz, up to half a semitone at the top) and the sounds' mod wheel source; also sent as CC 1 |
| Options > Key Touch | `Dynamic` (the SMK-37's default): every key as played, its two contacts through the unit's factory curve; `Fixed`: every key at Options > Velocity (1..127). Velocity Sens shapes how much the velocity changes the level, as on the FM-1 |
| OCTAVE- held at power-on | update mode (OCTAVE-'s light flashes three times; install a package, or switch off and on) |
| OCTAVE- + OCTAVE+ held at power-on | HARDWARE CALIBRATION (the screen asks for the FM-1's names, the table above) |
| OCTAVE- + OCTAVE+ + PATCH held 5 s | update mode |

The SMK-37's TRS jack is a MIDI output, and this build does not use it; there is no TRS MIDI IN. MIDI works over USB.
The SMK-37's button lights are on or off: the FM-1's dim glow of idle buttons does not show. The pedal input is not
used.

### What the FM-1 has and the SMK-37 does not

These came to ChoralRoot after the port was made and have no SMK-37 counterpart; they are left out rather than given
an invented meaning:

- **Aftertouch from the keys** (Key Touch `Vel + Aftertouch`, `Vel + Poly AT`): the SMK-37's keys have two contacts
  and no pressure, so Key Touch stops at `Dynamic`. (The pads sense pressure, but they are the chord block here.)
- **Velocity Curve** and **Aftertouch Sens**: they shape the FM-1's analog key sensing (`firmware/src/cr_sense.c`,
  [docs/VELOCITY.md](docs/VELOCITY.md)); their rows read `FM-1 only`. The SMK-37's velocity follows each unit's own
  factory curves instead.
- **The FM-1's key-sensing hardware** (the analog rows PA7 / PA8 and the velocity probe on PA10): on the SMK-37 PA8 and
  PA10 are the pads' and faders' analog inputs, so none of it is built in. The FM-1-only pins (the PA2 rail, the PH8 MIDI
  IN, the PB1 battery input) are never driven by the SMK build (genkigenki's board layer, `hal/smk_input.h`,
  `hal/fm1_lcd_hw.h`, the boot stub).

## Try it in the browser

The SMK-37 build runs in an emulator in the browser, with the SMK-37 Pro's panel: the keys (struck low on a key loud,
high soft), the pads lit as the firmware lights them, K1..K8, the faders, the wheels, the buttons by their SMK names,
the computer keyboard and Web MIDI. <https://quixotic7.github.io/ChoralRootFM1/webapp/smk37/emu/> (the SMK-37 test
installer links it too). It is the same firmware as the packages (`BOARD_SMK37`) on a modelled panel
(`tools/emu/emu_smk_hal.h`): the feel of the real keys and pads is the unit's to tell.

## Install

Have M-Vave's stock package for your model and M-UPGRADE ready first (see [Going back](#going-back-to-the-stock-firmware)).

**From the browser** (Chrome or Edge on a computer): the SMK-37 test installer, a page of the project's site that no
other page links (the maintainer sends testers the link). Tick that you accept the risk, connect the keyboard and
press Install: it picks your model's package (a Starrykey 37 Play gets the Pro's), checks its checksum before writing,
backs up a keyboard that already runs ChoralRoot to a file first, and finishes an interrupted install. It refuses an
FM-1 and other firmwares' update modes. The Single core choice above the button is for a keyboard that crashes or
freezes with the standard package.

**From the command line**: connect the keyboard by USB and run, with the package for your model:

```sh
python3 tools/fm1_install.py choralroot-0.27-smk37.fwsc
```

`fm1_install.py` needs `python-rtmidi` (`pip3 install python-rtmidi`). It checks that the package is for the connected
keyboard: an SMK-37 package is refused on an FM-1 and an FM-1 package on an SMK-37, on every path (the install, and
the resume of an interrupted one, which checks the update loader's model too). Any SMK-37 model's package may go on any
SMK-37 model (one hardware); install your own model's, so M-Vave's updater still knows the unit. As on the FM-1, an
install that was interrupted resumes: switch the keyboard on (or hold OCTAVE- while switching on) and run the install
again. The project's main web installer offers FM-1 releases only.

**The first start** clears any other firmware's data in ChoralRoot's flash areas (the store's owner tag,
[Storage](#storage)); this takes a few seconds once. The stock firmware's key, knob and fader mapping in USRFLASH is
kept until ChoralRoot first writes there; just before that write ChoralRoot erases the stock firmware's end marker
(the sector 0xEA000), so after a return to the stock firmware it rebuilds its factory mapping. A unit that ran genkigenki's earlier ChoralRoot
build for the SMK-37 keeps its data: its sounds move into 0.24's sound pool, as on an FM-1 updated from 0.21, and a
Velocity of `keys` becomes Key Touch `Dynamic`.

## Going back to the stock firmware

Install M-Vave's firmware package for your model with M-Vave's updater, M-UPGRADE. It works directly, because the
installed package keeps the keyboard's model name. The keyboard's calibration (USRTRIM, the factory key, pad and wheel
curves), Bluetooth pairing and key areas are never written by ChoralRoot, so they are as they were. The key, knob
and fader mapping is the one the stock firmware had, or its factory one once ChoralRoot has stored something in
USRFLASH (the stock firmware rebuilds it at its first start).

Going back to genkigenki's earlier ChoralRoot build (0.22) works too: it finds the sounds it had, but not the ones saved
on 0.24 (they are in the sound pool, which it does not know).

### If every key sends the same note after going back

The stock firmware keeps its key, knob and fader mapping in USRFLASH and trusts it as long as its end marker
(`usrdata` at 0xEAFF8) is there. Builds before the fix in 0.24-beta3-smk37.1 could leave that marker standing over
ChoralRoot's own data, and the stock firmware then reads ChoralRoot's data as its mapping: every key sends one note.
From the code, the units that can be affected are those that ran genkigenki's earlier ChoralRoot build for the SMK-37
(0.22, from his web installer) at any point: its first start cleared USRFLASH only up to 0xE5FFF, so the marker
stayed, and a later 0.24-beta2-smk37 or 0.24-beta3-smk37 kept it too (the same data family: no clearing). A unit that
went from the stock firmware straight to 0.24-beta2-smk37 or 0.24-beta3-smk37 had the marker erased at that first
start (their clearing reached 0xF0FFF), so its stock firmware rebuilds the factory mapping; if such a unit does show
one-note keys, please report it, because then the cause is something else.

**What recovers it:** install 0.24-beta3-smk37.1 or later, switch the keyboard on once (the first start finds the
marker over data that is not the stock mapping and erases it; nothing else of the stock firmware's is touched), then
go back to the stock firmware with M-UPGRADE as above. At its next start the stock firmware finds no marker and builds
its factory mapping again: every key its own note. A custom stock mapping you made before ChoralRoot is not brought
back (ChoralRoot's data had already replaced it). The stock firmware's own reset may also rebuild the mapping, but that
is not documented in the porting guide and has not been tried; the way above follows from the code.

## Storage

ChoralRoot writes only inside its own windows of the SMK-37's flash (`firmware/hal/fm1_flash_map.h`), and the flash
driver checks every erase and write against them and against the protected ranges a second time. What 0.22 stored keeps
the places of genkigenki's port; 0.24's favourites and sound pool take what was free, and the pool needs 7 sectors past
the port's USRFLASH window: the unlisted flash after the stock USRFLASH. The stock USRFLASH's last sector (0xEA000)
holds the stock firmware's end marker (`usrdata` at 0xEAFF8): ChoralRoot only ever erases that sector, once, before
its first write into USRFLASH, or at a start that finds the marker over anything but the intact stock mapping. A
pool sector that 0.24-beta2-smk37 or 0.24-beta3-smk37 kept there (their pool began at 0xEA000) moves to 0xF1000 at
the first start.

| Flash | What | Window |
| --- | --- | --- |
| 0x00000-0x03FFF | head (SPL, boot config) | never |
| 0x04000-0x9CFFF | the app area: the boot stub, the app, the commit marker | the update loader only |
| 0x9D000-0xA4FFF | the VA store, the FM6 stores, the CZ-1 tone store's first half (legacy: read once) | FL_DATA |
| 0xA5000-0xA8FFF | the user sound banks of 0.21 (legacy: read once) | FL_DATA |
| 0xA9000-0xAAFFF | the FM6 bank | FL_DATA |
| 0xAB000-0xACFFF | the settings | FL_DATA |
| 0xAD000-0xAEFFF | the session | FL_DATA |
| 0xAF000 | the store owner tag (`SMK37DATACHORAL`) | FL_DATA |
| 0xB0000-0xB3FFF | the CZ-1 tone store's second half, the QUAD store (legacy) | FL_DATA |
| 0xB4000-0xB5FFF | the favourites | FL_DATA |
| 0xB6000-0xBAFFF | the sound pool, sectors 0..4 | FL_DATA |
| 0xBB000-0xBFFFF | the update's staging (M-UPGRADE's loader and its record) | FL_OTA |
| 0xC0000-0xC1FFF | BTIF (Bluetooth pairing), USRTRIM (calibration) | never |
| 0xC2000-0xD1FFF | the eight CZ-1 banks | FL_SMP |
| 0xD2000-0xE5FFF | the ten loop slots | FL_SMP |
| 0xE6000-0xE9FFF | the sound pool, sectors 5..8 | FL_SMP |
| 0xEA000-0xEAFFF | the stock firmware's end marker | erased only (FL_STOCKTAG) |
| 0xEB000-0xF1FFF | the sound pool, sectors 9..15 | FL_TAIL |
| 0xF2000-0xF3FFF | free | not written |
| 0xF4000-0xFDFFF | USR (the stock resources) | never |
| 0xFE000-0xFEFFF | free | not written |
| 0xFF000-0xFFFFF | key_mac | never |

Each object has two copies (A / B); the pool's 16 sectors float (each save goes to a free one). Options > Flash Data (in
SAFE MODE) and a change of the store's owner erase exactly the data rows above, never the tag, the staging or anything
else. `tests/smk_flash_test.c` checks every object's copies and every pool sector against the windows and the
protected ranges, and that no two share a sector.

## Safety

- Have M-Vave's stock package for your model and M-UPGRADE ready before you install.
- Keep the USB cable connected until the install has finished and the keyboard has restarted.
- ChoralRoot keeps its data apart from other firmwares' (its own data family): the first start clears any other
  firmware's data in ChoralRoot's areas, including 0xEB000-0xF1FFF, flash that the stock firmware's region table does
  not list (nothing protected). An intact stock mapping in USRFLASH is kept; the stock firmware's end marker at
  0xEA000 is erased before ChoralRoot's first write there, so the stock firmware rebuilds its factory mapping.
- The firmware never writes the keyboard's calibration, Bluetooth pairing or key areas; the flash map refuses them at
  compile time and again in the flash driver.
- This is a test build that the maintainer has not run on an SMK-37. Installing other firmware is at your own risk.

## Build

See [BUILDING.md](BUILDING.md) for the toolchain.

```sh
BOARD=smk37 ./build.sh                 # build/choralroot-smk37.fwsc (SMK-37 Pro, Starrykey 37 Play)
BOARD=smk37 MODEL=elite ./build.sh     # build/choralroot-smk37-elite.fwsc
BOARD=smk37 MODEL=mke ./build.sh       # build/choralroot-smk37-mke.fwsc
./build.sh                             # the FM-1 build again before tests/run_tests.sh (the tests use its files)
```

A release names the board in its version (`--release X.Y-smk37`; one without it is refused) and collects the three
models in `build/release-smk37/`, with the `-1core` twins, `SHA256SUMS`, this file and the licences (publishing it:
[BUILDING.md](BUILDING.md) "An SMK-37 test release"):

```sh
BOARD=smk37 ./build.sh --release 0.27-smk37
BOARD=smk37 MODEL=elite ./build.sh --release 0.27-smk37
BOARD=smk37 MODEL=mke ./build.sh --release 0.27-smk37
```

## What to test

What no test here can answer, in the order to try it. Note the model, the package and what you saw.

1. **First boot and the owner change.** From the stock firmware: the first start takes a few seconds longer (the
   areas cleared, once), then the splash shows `ChoralRoot 0.27-smk37`. A second start is quick. `fm1_install.py
   --info` names the firmware. From genkigenki's earlier build: your sounds are there (moved into the pool).
2. **The pads.** Pads 1-4, 9-12 hold the chord types and extensions, pad 13 is LOCK (tap and hold), pads 5-8 and 14-16
   pick and stop the performance modes; their colours as in [Controls](#controls).
3. **Velocity.** Options > Key Touch reads `Dynamic`: soft and hard strokes sound different on all 37 keys (the ten
   outer keys too); `Fixed` plays every key at Options > Velocity.
4. **A save into a pool slot and a power cycle.** SAVE a sound as new (it lands in a user slot), switch off and on: it
   is there and sounds the same. Save over it, delete it; save a handful more. `fm1_install.py --sounds` lists them.
5. **A loop save.** Record a loop, save it to a slot, power-cycle: the slot is lit and plays.
6. **Favourites.** Mark a few sounds (SAVE > Favorite), switch the filter on, power-cycle: the marks and the filter
   stay. `fm1_install.py --favorites` lists them.
7. **GRAIN.** Pick GRAIN (OPT + PRESETS), play its presets, turn its knobs (K5..K8), save a GRAIN sound and load it
   again after a power cycle. Turn a source's TONE or SEED while a chord holds: the sound changes as you turn, never
   silent.
8. **Dual core.** Options > Dual Core `On` (the default), the Geek Out 2 view shows two cores' loads; play dense chords
   with the bass on for a few minutes. A crash or freeze: say so, then try the `-1core` package.
9. **An install's resume.** Pull the USB cable in the middle of an install (with the stock package and M-UPGRADE at
   hand), switch the keyboard on again (OCTAVE- held if it does not come up in update mode) and run the install again:
   it finishes. The installer refuses an FM-1 package at the keyboard in update mode.
10. **Going back with M-UPGRADE.** Install your model's stock package with M-UPGRADE: the stock firmware starts, and the
    keys', pads' and wheels' feel (the calibration) is as before ChoralRoot. Every key sends its own note, the pads,
    knobs and faders their factory messages (the mapping kept, or rebuilt after ChoralRoot wrote into USRFLASH).
11. **FX routing (0.27).** Hold FX, SELECT to the last page, Routing. On the keybed hold C#4, D#4, F#4 or G#4 (the
    black keys above the chord pads' octave: the SMK's keys 5..31 are the FM-1's note keys, C4 the eighth of them) and
    turn KNOB 1: the block moves; A#4 taps between Parallel and Chain; the white keys keep playing. In Chain the
    effects run one into the next; in Parallel, G#4 held and KNOB 1 anticlockwise puts the Filter before the buses.
    Say whether the boxes draw and slide cleanly on the SMK's screen and whether the black keys land where the
    footer says.
12. **The volume (issue 30).** Turn K1 while a chord plays: a popup reads `volume` and the level in per cent, and the
    sound follows. Turn it down, hold SAVE (BT) for the global save, switch off and on: the level is where you left
    it, not back at the default.

Also worth a line: whether `fm1_install.py --info` shows a unit ID (`unit ID: XXXXXXXX`) and whether two SMK-37s show
different ones (it is read from the flash chip, whose maker differs from the FM-1's).

## Credits

The SMK-37 port, its board layer, flash map, installers' SMK support and this page's first version are genkigenki's
(M. Geisler, genkigenki / makkuro), from the porting guide [fm1-to-smk37](https://github.com/genkigenki/fm1-to-smk37).
