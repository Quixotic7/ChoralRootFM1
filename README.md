# ChoralRoot FM-1

A Telepathic Orchid-style chord instrument as a firmware for the **M-VAVE FM-1**: one hand plays
roots, the other shapes chords; voicing, Key Mode, performance modes, bass, a looper — with the
FM-1's own sound engines, a colour screen, USB and TRS MIDI. Built on
[Felucca](https://github.com/hugelton/Felucca) (Leo Kuroshita, Hügelton Instruments) for the
platform and the engines, and on [choralroot](https://github.com/Quixotic7/choralroot) (the monome
grid version) for the musical engine. Independent of and unaffiliated with Telepathic Instruments
and M-VAVE.

## Install

**From the browser:** open the web installer at <https://quixotic7.github.io/ChoralRootFM1/> in Chrome or
Edge, connect the FM-1 by USB and press Install. Nothing is installed on the computer: the page talks to the
FM-1 over Web MIDI. Do not unplug while it writes; an interrupted install is resumed by pressing Install again.
Afterwards the FM-1 restarts and reports the identity `FM-1_920`.

**From the command line:** download `choralroot-X.Y.fwsc` from
[Releases](https://github.com/Quixotic7/ChoralRootFM1/releases) (`SHA256SUMS` next to it), then

```
pip3 install mido python-rtmidi
python3 tools/fm1_install.py choralroot-X.Y.fwsc
python3 tools/fm1_install.py --info          # identity of the connected FM-1 (FM-1_920 after the install)
```

**Back to the stock firmware:** the installer's "Return to official V15" section installs the official FM-1
V15 firmware, `FM-1.fwsc`, which you download yourself from M-VAVE's
[downloads page](https://www.m-vave.com/download) (or `python3 tools/fm1_install.py FM-1.fwsc`). ChoralRoot
has no backup protocol: the user sounds, loops and settings stored on the FM-1 are not saved and are erased
by a return to stock. If an install fails and the FM-1 no longer starts, recovery needs
[FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter).

Installing firmware is at your own risk.

## Status

A first public beta (0.1). The instrument plays: the chord block, Key Mode, the performance modes,
the bass, the views; the sound editor, the looper, MIDI (USB and TRS) and the VA engine are in. Open: the
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
| `tests/` | host tests; `tests/cr_*` are ChoralRoot's |
| `web/` | the landing page (`web/site/`), the web installer and `make_site.py` (the GitHub Pages site) |
| `.github/workflows/` | `release.yml` builds the package on a tag, `pages.yml` publishes the site |
| `assets/`, `LICENSES/` | the UI font, icons, CC0 samples and their licences |

## Building

See [BUILDING.md](BUILDING.md). The host side (tests and the emulator) needs only Xcode's clang,
Python 3 with Pillow and fontTools, and SDL2; the device build needs Docker and the JieLi toolchain
(on Linux x86-64 the toolchain runs natively, without Docker).

## Licence

GPL-3.0-only ([LICENSE](LICENSE)). Built on Felucca by Leo Kuroshita (@kurogedelic), Hügelton Instruments;
the chord logic after the Telepathic Instruments Orchid. The SAMPLE engine's CC0 instruments are by
Versilian Studios; the package carries three JieLi AC79 SDK files under Apache-2.0. The bundled font, icons,
ported DSP and SDK files keep their own licences ([LICENSES/](LICENSES/)); [LICENSING.md](LICENSING.md) has
the whole list. Orchid, M-VAVE and FM-1 are trademarks of their owners.
