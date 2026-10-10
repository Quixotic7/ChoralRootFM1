# ChoralRoot FM-1

A Telepathic Orchid-style chord instrument as a firmware for the **M-VAVE FM-1**: one hand plays
roots, the other shapes chords; voicing, Key Mode, performance modes, bass, a looper — with the
FM-1's own sound engines, a colour screen, USB and TRS MIDI. Built on
[Felucca](https://github.com/hugelton/Felucca) (Leo Kuroshita, Hügelton Instruments) for the
platform and the engines, and on [choralroot](https://github.com/Quixotic7/choralroot) (the monome
grid version) for the musical engine. Independent of and unaffiliated with Telepathic Instruments
and M-VAVE.

## Install

Try it in the browser first: <https://quixotic7.github.io/ChoralRootFM1/emu/> (the same firmware, with sound, no
FM-1 needed).

**From the browser:** open the web installer at <https://quixotic7.github.io/ChoralRootFM1/> in Chrome or
Edge, connect the FM-1 by USB and press Install. Nothing is installed on the computer: the page talks to the
FM-1 over Web MIDI. Do not unplug while it writes; an interrupted install is resumed by pressing Install again.
Afterwards the FM-1 restarts and reports the identity `FM-1_920`. Before installing, the page saves a backup of what
is stored on the FM-1 (settings, user sounds, loops, FM6 patches) to a file, and after installing over
Felucca it offers that backup back; its Back up and Restore buttons do the same at any time.

**Before any install:** charge the FM-1 fully (it updates on its battery), connect it straight to the computer with a
data cable (no hub), keep the computer awake, and leave the cable alone until the installer prints "done". An update
interrupted mid-write can leave the FM-1 dark with no USB device at all (docs/INSTALL-COMPAT.md, "Before an install").

**From the command line:** download `choralroot-X.Y.fwsc` from
[Releases](https://github.com/Quixotic7/ChoralRootFM1/releases) (`SHA256SUMS` next to it), then

```
pip3 install mido python-rtmidi
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

**SAFE MODE.** If ChoralRoot crashes twice in a row within 30 s of starting (switching it off and on does not count),
it starts in SAFE MODE (a yellow screen, "Safe mode" on the top line): it plays its factory sounds and ignores what is
stored in the flash (settings, user sounds, FM6 bank, loops), which stays untouched. Options > Flash Data (OCT+ twice)
erases that data and restarts; switching off and on tries the stored data again. Installing and Backup still work in
SAFE MODE, so you can save a backup first. Only if SAFE MODE itself crashes twice does the unit go to update mode
(ROM boot).

**If the FM-1 no longer starts** (black screen, the computer shows a USB disk "WL82 UBOOT1.00"): leave it on for 2
minutes to see whether it restarts by itself, try holding OCT- and OCT+ while switching on (10 s), and check what the
computer sees (docs/INSTALL-COMPAT.md, "If an FM-1 is dark"); then
[MvaveFM1Unbricker](https://github.com/Quixotic7/MvaveFM1Unbricker). If it does not show up as that disk either, the
hardware route is [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter).

Installing firmware is at your own risk.

## Status

A first public beta (0.1). The instrument plays: the chord block, Key Mode, the performance modes,
the bass, the views; the sound editor, the looper, MIDI (USB and TRS) and the VA engine are in. The engines, in the
picker's order: FM6 (Melodee's: DX7 voices rendered as Dexed renders them, DX7 SysEx import, [docs/FM6.md](docs/FM6.md)),
FM TONE (engine 15, `eng_quad.c`, code name QUAD: four-operator FM in the style of the Digitone, aligned with the
Digitone manual's algorithms, carrier levels and controls, [docs/QUAD.md](docs/QUAD.md)),
VA, CZ-1 (Melodee's, [docs/CZ1.md](docs/CZ1.md)), PHASE, LOFI, VOICE, TRIO, WHEEL, PHYS, NOISE. (Felucca's ANALOG is
retired since 0.14: the VA replaces it; an ANALOG sound saved before loads on the VA.) **USB audio recording** (Melodee's,
class compliant, no driver): the computer records the FM-1 through **ChoralRoot In**: the master, the CHORD part and
the BASS part as three stereo pairs; Options > USB Record switches it (Off: the serial console instead)
([docs/USB-AUDIO.md](docs/USB-AUDIO.md)). Open: the
Orchid parity passes (M8: the secret-chord map, chromatic Key Mode quantization, Key Mode sevenths and the
factory patterns, which ship as labelled fallbacks until they are captured from an Orchid).
[PLAN.md](PLAN.md) is the plan and the interface specification; [design/](design/) holds the screen and
panel mock-ups (made with the [ChoralRoot FM-1 designer](../ChoralRootFM1Designer/)).

## Layout

| Path | What |
| --- | --- |
| `PLAN.md` | the plan: controls, interaction grammar, screens, architecture, milestones |
| `design/` | the mock-ups (`make_mockups.py` generates the JSON; the PNG sheets are its renders) |
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
from [Melodee](https://github.com/keremimo/melodee) by Kerem Kilic (Ellic Studio);
the chord logic after the Telepathic Instruments Orchid. The firmware is all-synth (no samples in it: Felucca's
SAMPLE engine and its CC0 instruments by Versilian Studios stay in the tree for Felucca builds); the package carries three JieLi AC79 SDK files under Apache-2.0. The bundled font, icons,
ported DSP and SDK files keep their own licences ([LICENSES/](LICENSES/)); [LICENSING.md](LICENSING.md) has
the whole list. Orchid, M-VAVE and FM-1 are trademarks of their owners.
