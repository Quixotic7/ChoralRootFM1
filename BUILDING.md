# Building ChoralRoot FM-1

ChoralRoot FM-1 builds with Felucca's toolchain and scripts (below): the unit is `firmware/src/choralroot.c`
instead of `felucca.c`. There is also a host side that needs no toolchain.

## Host side (tests and the Mac emulator)

```
pip3 install Pillow fonttools
brew install libraqm sdl2
export DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib   # Pillow finds libraqm for the UI font
python3 -c 'import sys; sys.path.insert(0, "tools"); import build; build.generate()'   # build/gen/*.h
cc -O1 -w -Ibuild/gen -Ifirmware/src -o build/host/ui_test tests/ui_test.c -lm && build/host/ui_test
sh tools/emu/build.sh && build/host/emu          # the Mac emulator (tools/emu/README.md)
sh tools/emu/web/build_web.sh                    # the browser emulator (needs the Emscripten SDK: tools/emu/README.md "Browser build")
sh tests/run_cr_tests.sh                         # the ChoralRoot engine tests
sh tests/run_cr_draw.sh                          # renders every mock-up screen to build/cr_screens/
```

## Device build

The build compiles `firmware/src/choralroot.c` (one compilation unit) and makes in `build/`:

| File | What |
| --- | --- |
| `choralroot.bin` | the firmware app |
| `choralroot.elf`, `choralroot.dis` | the linked app and its disassembly |
| `loader/ota.bin` | the update loader (Felucca's) |
| `choralroot.fwsc` | the installable package (app + loader), identity `FM-1_920` |

The package identity `FM-1_920` is what the device reports on the update handshake and what the installers check
after an install; it is constant for ChoralRoot (releases too). After the checks the build prints a size line, e.g.

```
size: .text 448672 B, .ram_text 2888 B, .data 296 B, .bss 88336 B; XIP 451856 B of 581564 (77.7%),
      RAM 88632 B of 98304 (90.2%), POOL 315400 B of 344064 (91.7%), NOINIT 200 B of 15696 (1.3%)
```

XIP is `.text + .ram_text + .data` against the app slot in `firmware/app.ld`; RAM is `.data + .bss` (96 KiB);
POOL and NOINIT are the big-buffer and reset-surviving regions. The build fails if RAM or POOL overflows or if the
POOL keeps less than 8 KiB spare.

## Prerequisites (macOS)

- Python 3 with Pillow and fontTools: `pip3 install Pillow fonttools` (the UI font and icons are
  rasterised at build time)
- Docker Desktop. The JieLi toolchain is Linux x86-64 only; the build runs each tool in a
  `linux/amd64` `debian:bookworm-slim` container (Rosetta on Apple silicon). Keep the source
  tree in a folder Docker can share, e.g. under `/Users`.
- The JieLi Linux toolchain (clang 4.0.1 for pi32v2, from JieLi's package server):

  ```
  tools/get_toolchain.sh            # installs to ~/.jieli/toolchain
  ```

- The JieLi AC79 SDK (Apache-2.0): the package uses three of its files
  (`cpu/wl82/tools/uboot.boot`, `cfg_tool.bin`, `cfg/eq_cfg_hw.bin`). They are vendored in `tools/sdk/` (used when
  no checkout is found), so this step is optional:

  ```
  git clone --depth 1 --branch AC79NN_SDK_V1.2.1_2023-12-13 \
      https://gitee.com/Jieli-Tech/fw-AC79_AIoT_SDK.git ~/fw-AC79_AIoT_SDK
  ```

- Node.js (optional, for the web tests).

On Linux x86-64 the toolchain runs natively and Docker is not needed.

## Build

```
./build.sh
```

`JIELI_TOOLCHAIN` and `AC79_SDK` override the default locations
(`~/.jieli/toolchain`, `~/fw-AC79_AIoT_SDK`).

`build.sh` sets `DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib` itself on macOS (macOS drops `DYLD_*` variables
on the way into `/bin/sh`); calling `python3 tools/build.py` directly needs it exported.

`./build.sh --release 1.0` makes a release build: the identity stays `FM-1_920`, the version string becomes
`ChoralRoot 1.0`; the package is `build/choralroot-1.0.fwsc`, and
`build/release-1.0/` holds what a release ships: the package, the app
(`choralroot-1.0-app.bin`), `SHA256SUMS`, the sample attribution, `LICENSE`, `LICENSING.md` and
`LICENSES/` (the package contains Apache-2.0 SDK files, so the licence texts travel with it).

`FELUCCA_SIZE=0` builds everything at `-Os` (by default the main-loop files listed in `tools/size_fns.py`, the UI,
screens and stores, are built for size).

Build options (environment, `0` or `1`; defaults in `firmware/src/choralroot.c`, `core.h` and `icons.c`):

| Flag | Default | |
| --- | --- | --- |
| `FELUCCA_FLASH` | 1 | settings, presets and projects in flash |
| `FELUCCA_OTA` | 1 | update entry (needs `FELUCCA_FLASH`) |
| `FELUCCA_CDC` | 1 | USB serial console |
| `FELUCCA_UAC` | 1 | USB audio input (the master output, 44.1 kHz stereo) |
| `FELUCCA_UART` | 1 | TRS MIDI IN |
| `FELUCCA_SLICE` | 0 | the SLICE engine (ChoralRoot: off) |
| `FELUCCA_SLICER` | 0 | the SLICER insert and its 32 KB POOL buffer (ChoralRoot: off; Felucca and the emulator: 1) |
| `FELUCCA_ICONS` | 1 | parameter icons on the knob cards |
| `FELUCCA_FM4` | 0 | the retired DIGITAL engine (4-operator FM) instead of its FM6 conversion |

## Samples

The CC0 instrument samples that the SAMPLE engine uses are in `assets/samples-cc0/`
(Versilian Studios, see `ATTRIBUTION.txt` there). `tools/fetch_cc0.py` downloads them
again from the source repositories. Without that folder the build still works and the
SAMPLE engine has only the generated drum kit.

## Tests

```
tests/run_tests.sh
```

Runs the host tests and, with Node.js, the web page tests. Run it after `./build.sh`
(it uses `build/` and needs `AC79_SDK` set as for the build). The suites cover flash storage,
user presets, projects of every format, backup, the keys and knobs, MIDI (USB, TRS, clock,
control), USB audio, the update entry and loader, the command-line installer, the UI (the real
drawing code against stubs: every screen in every palette is rendered and checked for clipped or
overlapping text; PNGs land in `build/ui_new/`), every engine (DRUM, NOISE, PHYS, FM6, SLICE, the
DIGITAL conversion), the chord keys, the modulation matrix, the FX layer, the reverbs, the SLICER
and swing. With `DAISYSP` pointing at a DaisySP checkout, the PHYS models are also compared with
their floating-point originals; without it that test is skipped.

The regression suite (`tests/regress.c`) renders every engine and preset and compares a
hash of each render with `tests/golden.txt`; it also checks levels, voices and the CPU
cost (`tests/cpu_baseline.txt`, `tests/target_budget.txt`). After an intended change of
the sound, `GOLDEN_UPDATE=1 sh tests/run_tests.sh` rewrites the hashes; `BUDGET_UPDATE=1`
does the same for the cost files.

## Install

From the command line (needs `pip3 install mido python-rtmidi`):

```
python3 tools/fm1_install.py build/choralroot.fwsc
python3 tools/fm1_install.py --info          # identity of the connected FM-1 (FM-1_920 after the install)
```

Or install your own build from the web installer (Chrome or Edge): make a local copy of the site and open it from
`localhost` (Web MIDI needs a secure context):

```
python3 web/make_site.py build/choralroot.fwsc dev build/site && python3 -m http.server 8000 --directory build/site
# open http://localhost:8000/ (the landing page) or http://localhost:8000/webapp/installer/ (the installer)
```

`make_site.py` reads the identity from the package (`FM-1_9xx`; ChoralRoot's `FM-1_920`) and refuses a package
without Felucca's own loader. Felucca's released installer (<https://hugelton.github.io/Felucca/webapp/installer/>)
installs Felucca, not ChoralRoot.

Installing firmware is at your own risk. If an install fails and the FM-1 no longer
starts, recovery needs [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter).

## Releasing

A release is a tag `vX.Y`; GitHub Actions builds it and publishes the site.

1. Check the release build locally: `./build.sh --release X.Y` (it makes `build/release-X.Y/`).
2. Tag the commit and push the tag:

   ```
   git tag vX.Y && git push origin vX.Y
   ```

   `.github/workflows/release.yml` builds the package on ubuntu (the JieLi toolchain runs natively there; it is
   fetched with `tools/get_toolchain.sh`, and the three SDK files with a sparse clone of the AC79 SDK; both are
   cached), creates the GitHub release `vX.Y` (a pre-release for 0.x and for `X.Y-suffix`) and attaches
   `choralroot-X.Y.fwsc`, `choralroot-X.Y-app.bin`, `SHA256SUMS`, `LICENSE`, `LICENSING.md`, `ATTRIBUTION.txt`
   and `LICENSES.zip`. Run from the Actions tab (Run workflow, with a version) it only builds, and the package is
   a workflow artifact.
3. Then it starts `.github/workflows/pages.yml`, which downloads `choralroot-X.Y.fwsc` from the release, builds the
   browser emulator (`tools/emu/web/build_web.sh`, Emscripten; the site's `emu/`), runs `web/make_site.py`
   and deploys the site to <https://quixotic7.github.io/ChoralRootFM1/>. A release published by hand starts it
   too. Versions with a suffix (`1.1-rc1`) leave the site as it is.

Once, before the first release: Settings → Pages → Build and deployment → Source: **GitHub Actions**.

When CI cannot fetch the toolchain (pkgman.jieliapp.com) or the SDK (gitee.com): build locally with
`./build.sh --release X.Y`, create the release `vX.Y` on GitHub by hand and upload every file of
`build/release-X.Y/` (zip `LICENSES/` as `LICENSES.zip`), publish it, and the site follows; or run pages.yml from
the Actions tab (Run workflow, version `X.Y`).
