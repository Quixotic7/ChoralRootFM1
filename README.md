# ChoralRoot FM-1

A Telepathic Orchid-style chord instrument as a firmware for the **M-VAVE FM-1**: one hand plays
roots, the other shapes chords; voicing, Key Mode, performance modes, bass, a looper — with the
FM-1's own sound engines, a colour screen, USB and TRS MIDI. Built on
[Felucca](https://github.com/hugelton/Felucca) (Leo Kuroshita, Hügelton Instruments) for the
platform and the engines, and on [choralroot](https://github.com/Quixotic7/choralroot) (the monome
grid version) for the musical engine. Independent of and unaffiliated with Telepathic Instruments
and M-VAVE.

**Status:** in development. [PLAN.md](PLAN.md) is the plan and the interface specification;
[design/](design/) holds the screen and panel mock-ups (made with the
[ChoralRoot FM-1 designer](../ChoralRootFM1Designer/)).

## Layout

| Path | What |
| --- | --- |
| `PLAN.md` | the plan: controls, interaction grammar, screens, architecture, milestones |
| `design/` | the mock-ups (`make_mockups.py` generates the JSON; the PNG sheets are its renders) |
| `docs/INTEGRATION.md` | how the engine, the screens and Felucca's sound are wired together |
| `firmware/` | the firmware: Felucca's `hal/`, `src/` and `loader/`, plus ChoralRoot's `src/cr_*.c` |
| `tools/` | Felucca's build, generators, installer; `tools/emu/` the Mac emulator |
| `tests/` | host tests; `tests/cr_*` are ChoralRoot's |
| `web/` | Felucca's web installer and editor |
| `assets/`, `LICENSES/` | the UI font, icons, CC0 samples and their licences |

## Building

See [BUILDING.md](BUILDING.md). The host side (tests and the emulator) needs only Xcode's clang,
Python 3 with Pillow and fontTools, and SDL2; the device build needs Docker and the JieLi toolchain.

## Licence

GPL-3.0-only ([LICENSE](LICENSE)); the bundled font, icons, ported DSP and SDK files keep their own
licences ([LICENSES/](LICENSES/), [LICENSING.md](LICENSING.md)).
