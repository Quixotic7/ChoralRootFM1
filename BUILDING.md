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
(`choralroot-1.0-app.bin`), `SHA256SUMS`, `LICENSE`, `LICENSING.md` and
`LICENSES/` (the package contains Apache-2.0 SDK files, so the licence texts travel with it).

`FELUCCA_SIZE=0` builds everything at `-Os` (by default the main-loop files listed in `tools/size_fns.py`, the UI,
screens and stores, are built for size).

Build options (environment, `0` or `1`; defaults in `firmware/src/choralroot.c`; Felucca's in `core.h`, `gfx.c` and
`icons.c`, all 1, so Felucca's unit and its host tests are unchanged):

| Flag | Default | |
| --- | --- | --- |
| `FELUCCA_FLASH` | 1 | settings, presets and projects in flash |
| `FELUCCA_OTA` | 1 | update entry (needs `FELUCCA_FLASH`) |
| `FELUCCA_CDC` | 1 | USB serial console (with `FELUCCA_UAC`: presented while Options > USB Record is Off, and in SAFE MODE) |
| `FELUCCA_UAC` | 1 | USB audio recording (Melodee's, docs/USB-AUDIO.md): "ChoralRoot In" (master, CHORD, BASS: 6 channels), 44.1 kHz; no playback; 0: MIDI and the console only |
| `FELUCCA_UART` | 1 | TRS MIDI IN |
| `FELUCCA_SLICE` | 0 | the SLICE engine (ChoralRoot: off) |
| `FELUCCA_SLICER` | 0 | the SLICER insert and its 32 KB POOL buffer (ChoralRoot: off; Felucca and the emulator: 1) |
| `FELUCCA_ICONS` | 0 | Felucca's icon atlas (`icons.c`, `build/gen/ui_icons.h`): parameter icons on Felucca's knob cards (ChoralRoot: off; its header glyphs are drawn by `cr_draw.c`) |
| `FELUCCA_KEYCAPS` | 0 | Felucca's keycaps and key hints (`gfx.c`, `build/gen/ui_keycaps.h`; ChoralRoot: off, no screen draws one) |
| `FELUCCA_FM4` | 0 | the retired DIGITAL engine (4-operator FM) instead of its FM6 conversion |
| `FELUCCA_SEQ` | 0 | Felucca's sequencer (`seq.c`, `song_chain.c`, `chord.c`, `motion.c`, `midi_control.c`, `midi_clock.c`); ChoralRoot: off, `cr_out.c` has the `events_block` and MIDI in (docs/INTEGRATION.md section 1) |
| `FELUCCA_SAMPLE` | 0 | the SAMPLE engine and its ADPCM sets (about 190 KB of flash); ChoralRoot: off, engine 4 a retired slot |
| `FELUCCA_GRAIN` | 0 | the GRAIN engine (needs `FELUCCA_SAMPLE`; 27 KB of POOL); ChoralRoot: off, engine 8 retired |
| `FELUCCA_DRUM` | 0 | the DRUM engine (its kit: 7 KB of POOL); ChoralRoot: off, engine 10 retired |

ChoralRoot is all-synth: with the defaults the image has no samples and no Felucca sequencer (measured 2026-10-07:
`.text` 490104 -> 270312 B, `.bss` 90512 -> 72976 B (RAM 92.4 -> 74.5 %), POOL 330116 -> 294740 B (95.9 -> 85.7 %);
docs/INTEGRATION.md "All-synth"). A user sound saved on a retired engine (4, 8, 10) loads as the INIT sound on the VA; one saved on ANALOG (0,
retired in 0.14, `FELUCCA_ANALOG` 0) plays on the VA with its macros.

## Samples

ChoralRoot does not use them: its image has no SAMPLE engine (`FELUCCA_SAMPLE=0`), and a release carries no sample
attribution. They stay in the tree for Felucca builds and the host tests (`tools/build.py`'s generate step still writes
`build/gen/felucca_samples.h`, which `tests/hostsim.c` and Felucca's unit include). The CC0 instrument samples that
Felucca's SAMPLE engine uses are in `assets/samples-cc0/` (Versilian Studios, see `ATTRIBUTION.txt` there).
`tools/fetch_cc0.py` downloads them again from the source repositories. Without that folder the build still works and
the SAMPLE engine has only the generated drum kit.

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
python3 tools/fm1_install.py build/choralroot.fwsc --backup ~/fm1-backups/   # back up first, then install
python3 tools/fm1_install.py --info          # identity of the connected FM-1 (FM-1_920 after the install)
python3 tools/fm1_install.py --backup FILE   # save what is stored on the FM-1 (a directory: a dated file name)
python3 tools/fm1_install.py --restore FILE  # write a backup back (ChoralRoot restarts afterwards)
```

**Backup and restore.** A backup is one JSON file (`choralroot-backup-YYYYMMDD.json`, Felucca's format
`felucca-backup` version 1) with everything stored on the FM-1: the settings, the 32 user sounds and their VA patches,
the 10 loops and the FM6 patch bank (no samples: ChoralRoot has none). Felucca and ChoralRoot answer the same SysEx
(`web/EDITOR_PROTOCOL.md`, "ChoralRoot: backup and restore"; `firmware/src/cr_backup.c`); the stock firmware does
not. The web installer backs up before it installs (a "Skip the backup" box for an FM-1 that cannot be, or when one is
saved already), offers the backup back after installing ChoralRoot over another firmware, and has Back up / Restore
buttons; the return to the stock V15 backs up first too. `PACKAGE --backup F --restore F` does the same from the
command line. A restore writes what the connected firmware uses: a Felucca backup restores its settings, user preset
banks and FM6 patches on ChoralRoot (its songs and samples stay in the file, reported skipped), a ChoralRoot backup
restores the same on Felucca (the VA patches and loops stay in the file). Each part is checked before anything is written and committed
torn-write safe; stop a playing loop first (the FM-1 answers busy and the restore waits); each flash write holds the
sound for about 45 ms. Tests: `tests/cr_backup_test.c` (in `tests/run_cr_tests.sh`), `web/test_backup.mjs`,
`web/test_installer.mjs`, `tests/install_test.py`.

Or install your own build from the web installer (Chrome or Edge): make a local copy of the site and open it from
`localhost` (Web MIDI needs a secure context):

```
python3 web/make_site.py build/choralroot.fwsc dev build/site && python3 -m http.server 8000 --directory build/site
# open http://localhost:8000/ (the landing page) or http://localhost:8000/webapp/installer/ (the installer)
```

`make_site.py` reads the identity from the package (`FM-1_9xx`; ChoralRoot's `FM-1_920`) and refuses a package
without Felucca's own loader. Felucca's released installer (<https://hugelton.github.io/Felucca/webapp/installer/>)
installs Felucca, not ChoralRoot.

**Which firmware it installs over** (`docs/INSTALL-COMPAT.md`). After the handshake both installers ask the running
firmware for its version (the backup protocol's INFO) and classify it (`web/fm1ota.js` `classifyFirmware`,
`tools/fm1_install.py` `classify_firmware`, one table): the official M-VAVE firmware (`FM-1_0NN`), Felucca 1.0 or
later (`FELUCCA v1.x`), Melodee and ChoralRoot are installed over; Sloop, the Felucca 0.x betas (`FELUCCA 0.9-BETA`),
Sloop's rescue mode (`FM-1_000`) and any 9xx firmware that does not say what it is are refused (exit 8; the page
says why and shows an "I understand the risk" box, then asks once more). `--force` / that box install anyway. The
return to the official V15 is never refused. `--info` prints the version and the verdict.

Installing firmware is at your own risk. **Recovery.** The boot guard (`firmware/src/cr_bootguard.h`, `main.c`) counts
only watchdog and soft (crash) resets that come within 30 s of a boot; a power-on clears it. Two in a row start SAFE
MODE: no flash object is read or written (factory sounds, default settings), the installer, the backup's reads and
OCT- + OCT+ 5 s (update mode) work, Options > Flash Data erases the data objects (not the firmware) and reboots; a
power cycle tries the stored data again. Four in a row (SAFE MODE crashed twice) enter ROM boot. The console `boot`
prints the guard, the reset reason and the stage the crash reached. Emulator: `build/host/emu --boot-fail 1
--reset-reason wdt` starts in SAFE MODE (`tools/emu/test_cr.sh`). If an install fails and the FM-1 no longer starts
(black screen, a USB disk "WL82 UBOOT1.00"), see docs/INSTALL-COMPAT.md ("If an FM-1 is dark") and
[MvaveFM1Unbricker](https://github.com/Quixotic7/MvaveFM1Unbricker); if it does not even show up as that disk,
[FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter).

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
