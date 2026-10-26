# ChoralRoot FM-1

A Telepathic Orchid-style chord instrument as a firmware for the **M-VAVE FM-1**: one hand plays
roots, the other shapes chords; voicing, Key Mode, performance modes, bass, a looper — with the
FM-1's own sound engines, a colour screen, USB and TRS MIDI. Built on
[Felucca](https://github.com/hugelton/Felucca) (Leo Kuroshita, Hügelton Instruments) for the
platform and the engines, and on [choralroot](https://github.com/Quixotic7/choralroot) (the monome
grid version) for the musical engine. Independent of and unaffiliated with Telepathic Instruments
and M-VAVE.

**The manual:** [docs/MANUAL.md](docs/MANUAL.md) (the first five minutes, every button, knob and Options entry, the
looper, the sound editor, MIDI and USB, updating and recovery, a quick-reference table).

**M-Vave SMK-37:** a community test build of 0.25 (genkigenki's port; community-tested, not tested by the maintainer): see [SMK-37 instructions](SMK37.md).

## Install

Try it in the browser first: <https://quixotic7.github.io/ChoralRootFM1/emu/> (the same firmware, with sound, no
FM-1 needed).

**From the browser:** open the web installer at <https://quixotic7.github.io/ChoralRootFM1/webapp/installer/> in
Chrome or Edge, connect the FM-1 and press Install. The button names the version it writes (the newest), and the
page lists what is new in it beside the button. "Other versions" under the button offers the older ones from 0.16
on, each with its notes and a sentence on what going back means (0.15 and older are only on
[Releases](https://github.com/Quixotic7/ChoralRootFM1/releases): no protected update path). Beta releases
(`0.22-beta1`) are opt-in: tick "Show beta releases" under the versions to see them (the page remembers it in that
browser). A beta may have bugs and the latest stable release can replace it at any time; Install's default is
always the newest stable release, also when the FM-1 runs a beta. Nothing is
installed on the computer: the page talks to the FM-1 over Web MIDI, and the firmware files come from the site
itself (`firmware/`, with their SHA-256, which the page checks before it writes). Do not unplug while it writes; an
interrupted install is resumed by pressing Install again (after a power loss too, from 0.16 on: see "If an install
was interrupted"). An update keeps your saved sounds, loops and settings.
Afterwards the FM-1 restarts and reports the identity `FM-1_920`. Before installing, the page saves a backup of what
is stored on the FM-1 (settings, user sounds, loops, FM6 patches) to a file, and after installing over
Felucca it offers that backup back; its Back up and Restore buttons do the same at any time.

**Before any install:** charge the FM-1 fully (it updates on its battery), connect it straight to the computer with a
data cable (no hub), keep the computer awake, and leave the cable alone until the installer prints "done". Up to 0.15
an update interrupted mid-write could leave the FM-1 dark with no USB device at all (docs/INSTALL-COMPAT.md, "Before
an install"); from 0.16 on it comes back in update mode (below), except during the first second of the install of
0.16 itself over an older version.

**From the command line:** download `choralroot-X.Y.fwsc` from
[Releases](https://github.com/Quixotic7/ChoralRootFM1/releases) (`SHA256SUMS` next to it), then

```
pip3 install python-rtmidi
python3 tools/fm1_install.py choralroot-X.Y.fwsc --backup .   # back up to ./choralroot-backup-YYYYMMDD.json, install
python3 tools/fm1_install.py --info          # identity of the connected FM-1 (FM-1_920 after the install)
python3 tools/fm1_install.py --restore FILE  # a backup back onto the FM-1
python3 tools/fm1_install.py --sounds        # the 32 user sounds; --export-sound N FILE / --import-sound N FILE /
                                             # --rename-sound N NAME / --delete-sound N (docs/SOUNDS.md)
```

**Single sounds as files:** the installer page's Sounds section and the commands above export, import, rename and
delete one user sound at a time (its record and its VA / FM6 / CZ-1 patch) as a small JSON file, without a restart.

**The device manager** (`webapp/installer/manager.html` on the site, [docs/DEVICE-MANAGER.md](docs/DEVICE-MANAGER.md)):
the FM-1's pools per engine and its ten loop slots next to a library (this browser, files, the factory packs); Place,
Keep, Swap, Rename, Delete, Undo, a back-up, and an on-screen keyboard over Web MIDI. `manager.html?sim=1` runs it
against a simulated FM-1, no device needed.

**Back to the stock firmware:** the installer's "Return to official V15" section installs the official FM-1
V15 firmware, `FM-1.fwsc`, which you download yourself from M-VAVE's
[downloads page](https://www.m-vave.com/download) (or `python3 tools/fm1_install.py FM-1.fwsc --backup .`). It
saves a backup first: the stock firmware cannot use ChoralRoot's user sounds, loops or settings; reinstall ChoralRoot
and press Restore to bring them back. Backups move between firmwares too: a Felucca backup restores its settings,
user sounds and FM6 patches on ChoralRoot (its samples stay in the file: ChoralRoot is all-synth), and back (BUILDING.md, "Backup and restore").

**Over which firmware:** the stock firmware, Felucca 1.0 or later, Melodee and ChoralRoot. The installers refuse
Sloop, the Felucca 0.x betas and firmwares they cannot identify: an FM-1 has been bricked by an install over Sloop,
and Felucca 0.9-beta to 1.0 bricked several (the loader wrote correctly both times; the cause is still open;
[docs/INSTALL-COMPAT.md](docs/INSTALL-COMPAT.md)). Return to the official V15 with that firmware's installer first.
**Bubba Machine** takes only M-VAVE's own package, so ChoralRoot cannot go over it directly: return to V15 with the web
installer's "Return to official V15" (or `tools/fm1_install.py FM-1.fwsc`), then install ChoralRoot.

**SAFE MODE.** If ChoralRoot crashes twice in a row within 30 s of starting (switching it off and on does not count),
it starts in SAFE MODE (a yellow screen, "Safe mode" on the top line): it plays its factory sounds and ignores what is
stored in the flash (settings, user sounds, FM6 bank, loops), which stays untouched. Options > Flash Data (OCT+ twice)
erases that data and restarts; switching off and on tries the stored data again. Installing and Backup still work in
SAFE MODE, so you can save a backup first. Only if SAFE MODE itself crashes twice does the unit go to update mode
(ROM boot).

**If an install was interrupted** (ChoralRoot 0.16 and later, which carry the boot stub: docs/BOOT-SAFETY.md,
[docs/releases/0.16.md](docs/releases/0.16.md)): switch the FM-1 on. The key LEDs flash three times, the screen
stays black and the FM-1 is in update mode: open the installer and press Install, it finishes the write. The same on
purpose, whatever state the firmware is in: **hold OCT- (alone) while switching on**, three flashes, open the
installer, Install (or switch off and on to start the firmware again: nothing was written). The OCT- gesture is new
in 0.16. From 0.18 on, the same without switching off: **hold OCT-, OCT+ and HOME for 5 seconds** while ChoralRoot
runs (a countdown from 2 s; let go to cancel). The two OCT keys alone are PANIC and nothing else: they no longer
lead to the chip's USB boot when held.
One long flash instead of three: switch off for 10 seconds and try once more; one long flash again means no update
loader is stored (it is kept from the unit's last install on) and the FM-1 is the USB disk "WL82 UBOOT1.00" until it
is switched off and on.

**If the FM-1 no longer starts** (black screen, the computer shows a USB disk "WL82 UBOOT1.00"): leave it on for 2
minutes to see whether it restarts by itself, try holding OCT- and OCT+ while switching on (10 s), and check what the
computer sees (docs/INSTALL-COMPAT.md, "If an FM-1 is dark"); then
[MvaveFM1Unbricker](https://github.com/Quixotic7/MvaveFM1Unbricker). If it does not show up as that disk either, the
hardware route is [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter).

Installing firmware is at your own risk.

## Status

A public beta. The latest stable release is the one the web installer installs by default; the next version, while in
beta, is offered there too, behind "Show beta releases" ([docs/releases/](docs/releases/) has the notes of every
version, betas included; [Releases](https://github.com/Quixotic7/ChoralRootFM1/releases) the files). This tree is the
newest version, so it may be ahead of the stable release. What is in it; the [manual](docs/MANUAL.md) says how to play
each part:

- **Chords**: the chord block on the keybed (DIM MIN MAJ on the black keys F#3 G#3 A#3, SUS = G#3 + A#3 together,
  6 m7 M7 9 on the white keys F3 G3 A3 B3, LOCK on the LFO button), roots on C4..G5, voicing on KNOB 1, Key Mode, the
  Simple / Advanced / Free play styles, secret chords, LATCH (GLO + LFO). That is the FM-1 layout, the default since
  0.25.1; Options > Chord Keys: Classic is the Orchid's (SUS on C#4, the 9 on C4, B3 = LOCK, roots D4..G5).
- **Layers** that lock open on a hold (KEY, PERF, FX, BASS, SEQ for the metronome, LOOP), each a picker over a row of four knob
  cells; OCT- or HOME closes them.
- **Bass** (four behaviours), **performance modes** (strum, slop, arpeggio, twelve patterns, harp), **FX** (reverb,
  a stereo chorus, a stereo delay, drive, and a low and high cut on the whole sound), a metronome.
- **The looper**: it records chords, ten slots, five record modes (Overwrite, Advance, Overdub, Replace, Step), the
  PLAY menu; from 0.18 on saving is manual: a loop is written by the global save (SAVE held) or when its slot is
  left with unsaved changes (up to 0.17: at every stop) ([docs/LOOPER.md](docs/LOOPER.md)).
- **Seven engines**, in the picker's order: FM6 (Melodee's: DX7 voices rendered as Dexed renders them, DX7 SysEx
  import, [docs/FM6.md](docs/FM6.md)), FM TONE (engine 15, `eng_quad.c`, code name QUAD: four-operator FM in the style
  of the Digitone, aligned with the Digitone manual's algorithms, carrier levels and controls,
  [docs/QUAD.md](docs/QUAD.md)), VA ([docs/VA.md](docs/VA.md)), CZ-1 (Melodee's, [docs/CZ1.md](docs/CZ1.md)), P5
  (Melodee's Prophet-5 model with 115 of Sequential's factory programs, 0.22, [docs/PROPHET.md](docs/PROPHET.md)), WHEEL,
  GRAIN (a granular synth over two sources it makes itself, MORPH between them; 0.24-beta2, [docs/GRAIN.md](docs/GRAIN.md)).
  (PHASE, LOFI, VOICE, TRIO, NOISE and PHYS, Felucca's, are retired since 0.18: a sound saved on one stays in its
  slot and plays as a stand-in on the VA.) (Felucca's ANALOG is retired since 0.14: the VA replaces it; an ANALOG sound
  saved before loads on the VA.) One preset pool per engine (INIT, the factory presets, yours), 128 saved sounds shared
  by every engine (32 until 0.21), favourites with a filter, the
  sound editor ([docs/PRESETS.md](docs/PRESETS.md), [docs/EDITOR.md](docs/EDITOR.md)).
- **Views**: Chord, Keyboard, Notes, Geek Out, Scope, Geek Out 2; Options, one setting per screen.
- **MIDI**: three streams out over USB (performance on channel 1, bass on 2, the raw chord off by default), MIDI in
  on the parts (USB and TRS), clock both ways.
  **USB audio recording** (Melodee's, class compliant, no driver): the computer records the FM-1 through **ChoralRoot
  In**: the master, the CHORD part and the BASS part as three stereo pairs, at 44.1 kHz, and (0.19, after SLOOP 2.5
  and Felucca 1.1.5) at 48 kHz too when Options > USB Rate offers it; Options > USB Record switches it (Off:
  the serial console instead) ([docs/USB-AUDIO.md](docs/USB-AUDIO.md)).
- **On the computer**: the web installer with backup, restore and sounds as files, and the web device manager.
- **New in 0.27** ([docs/releases/0.27.md](docs/releases/0.27.md)): FX routing: the Routing page draws the chorus, delay,
  reverb and filter as boxes you reorder with the black keys and KNOB 1, in Parallel (the buses as before, the filter
  now movable before them) or Chain (one path, the original firmware's way); the device manager finds an FM-1 a Mac
  still calls "FM-1".
- **New in 0.26.1** ([docs/releases/0.26.1.md](docs/releases/0.26.1.md)): 0.26's firmware, the first release GitHub
  built from source and signed through The Firmware Hub's shared workflow (`gh attestation verify`, BUILDING.md
  "Checking a package").
- **New in 0.26** ([docs/releases/0.26.md](docs/releases/0.26.md)): with Dual Core On, VA and FM6 chords are shared
  between the two cores and the VA's code runs from RAM: an 8-note VA pad over a bass no longer runs late, the same
  sound bit for bit; the screen's background is pure black; the installers recognise Bubba Machine.
- **New in 0.25.1** ([docs/releases/0.25.1.md](docs/releases/0.25.1.md)): the FM-1 chord keys are the default
  (SUS = G#3 + A#3, the 9 on B3, roots from C4, LOCK on LFO); every FM-1 switches to them once at the update, LOCK
  and the latched chord carried over; Classic is in Options > Chord Keys.
- **New in 0.25** ([docs/releases/0.25.md](docs/releases/0.25.md)): GRAIN's MORPH blends sources A and B into one
  sound the grains play, smoothly under ENV 2, the LFOs and the matrix; TONE, MOTION, SEED and TYPE change a source as
  they turn, the grains playing on.
- **New in 0.24** ([docs/releases/0.24.md](docs/releases/0.24.md)): Options > Chord Keys FM-1, a chord layout for the
  FM-1's keybed (SUS from G#3 + A#3, LOCK on LFO) with the SUS Window; MIDI Keys; shortcuts and the voicing on the
  Full keyboard; GRAIN, a seventh engine; velocity on all 27 keys and Vel + Poly AT; SAFE MODE's Flash Data clears the
  saved sounds.
- **New in 0.23** ([docs/releases/0.23.md](docs/releases/0.23.md)): FM TONE at the other engines' level and without
  pops on its voices; Options > Velocity Curve; Solfège numerals in Key Mode; the device manager places sounds next to
  one saved on another build.
- **New in 0.22** ([docs/releases/0.22.md](docs/releases/0.22.md)): P5, a sixth engine; 128 sound slots shared by
  every engine and favourites with a filter; FM TONE in the Digitone's ranges and times, with Digitone `.dnsnd`
  import; the VA matrix's amounts in tenths below 6; Snap and numerals in Key Mode; velocity and aftertouch from the keys (Options > Key Touch) and a per-sound
  EXPR screen; knobs that accelerate; a master Filter (the FX layer's eighth page); the loop editor's step preview;
  Dual Core on by default, at 1.50 V, off by itself after a crash, and a single-core package; Geek Out 2; the unit
  ID; the device manager's P5 tab and bulk restore.
- **New in 0.21** ([docs/releases/0.21.md](docs/releases/0.21.md)): the chorus no longer clicks while Depth turns,
  and its page has a Width knob (KNOB 3) for the stereo spread ([docs/FX.md](docs/FX.md)).
- **New in 0.20** ([docs/releases/0.20.md](docs/releases/0.20.md)): Options > Chord Names (the Orchid's names, the
  default, or Standard lead-sheet names) and Options > 6th with 7th (the 6th an octave up over a seventh: a 13th)
  ([docs/CHORD-NAMES.md](docs/CHORD-NAMES.md)).
- **New in 0.19** ([docs/releases/0.19.md](docs/releases/0.19.md)): Options > Output, Clean (the default: a
  look-ahead limiter as a safety, MASTER last) or Classic (0.18's output stage)
  ([docs/AUDIO-QUALITY.md](docs/AUDIO-QUALITY.md)); the chorus in stereo; CZ-1 without its pops and 6 dB lower, level
  with the other engines; USB recording at 48 kHz as an option; the keys play while the Options are open; OPT +
  PRESETS / ALGORITHM pick the engine in the layers too.
- **New in 0.16** ([docs/releases/0.16.md](docs/releases/0.16.md)): Options > Dual Core is Off by default
  ([docs/DUALCORE.md](docs/DUALCORE.md)); the boot stub, so an interrupted update comes back in update mode
  ([docs/BOOT-SAFETY.md](docs/BOOT-SAFETY.md)); the device manager auditions sounds on the unit.

Open: the Orchid parity passes (M8: the secret-chord map, chromatic Key Mode quantization, Key Mode sevenths and the
factory patterns, which ship as labelled fallbacks until they are captured from an Orchid).
[PLAN.md](PLAN.md) is the plan and the interface specification; [design/](design/) holds the screen and
panel mock-ups (made with the [ChoralRoot FM-1 designer](https://github.com/Quixotic7/MvaveFM1-Designer)).

## Layout

| Path | What |
| --- | --- |
| `docs/MANUAL.md` | the player's manual (its screenshots: `docs/manual/`) |
| `PLAN.md` | the plan: controls, interaction grammar, screens, architecture, milestones |
| `design/` | the mock-ups (`make_mockups.py` generates the JSON; the PNG sheets are its renders) |
| `docs/` | the design documents (the editor, the engines, the looper, presets, settings, USB audio, boot safety) and `docs/releases/`, the release notes |
| `docs/INTEGRATION.md` | how the engine, the screens and Felucca's sound are wired together |
| `firmware/` | the firmware: Felucca's `hal/`, `src/` and `loader/`, plus ChoralRoot's `src/cr_*.c` |
| `tools/` | Felucca's build, generators, installer; `tools/emu/` the Mac emulator |
| `tools/emu/web/` | the emulator built for the browser (Emscripten) |
| `tests/` | host tests; `tests/cr_*` are ChoralRoot's |
| `web/` | the landing page (`web/site/`), the web installer and `make_site.py` (the GitHub Pages site) |
| `.github/workflows/` | `release.yml` builds the package on a tag, `pages.yml` publishes the site |
| `assets/`, `LICENSES/` | the UI font, icons, CC0 samples and their licences |

## Building

See [BUILDING.md](BUILDING.md). The host side (tests and the emulator) needs only Xcode's clang,
Python 3 with Pillow and fontTools, and SDL2; the device build needs Docker and the JieLi toolchain
(on Linux x86-64 the toolchain runs natively, without Docker).

## Licence

GPL-3.0-only ([LICENSE](LICENSE)). Built on Felucca by Leo Kuroshita (@kurogedelic), Hügelton Instruments, and its FM6
from [Melodee](https://github.com/keremimo/melodee) by Kerem Kilic (Ellic Studio); the keys' velocity and aftertouch
from [Velo](https://github.com/charlesvestal/fm1-velocity) and [MONK-1](https://github.com/charlesvestal/fm1-monk) by
Charles Vestal (GPL-3.0-only; [docs/VELOCITY.md](docs/VELOCITY.md));
the chord logic after the Telepathic Instruments Orchid. The firmware is all-synth (no samples in it: Felucca's
SAMPLE engine and its CC0 instruments by Versilian Studios stay in the tree for Felucca builds); the package carries three JieLi AC79 SDK files under Apache-2.0. The bundled font, icons,
ported DSP and SDK files keep their own licences ([LICENSES/](LICENSES/)); [LICENSING.md](LICENSING.md) has
the whole list. Orchid, M-VAVE and FM-1 are trademarks of their owners.
Some additional ideas and features that are implemented like a full keyboard mode were suggested by Rosen u/Creative_One919