# Handoff: ChoralRoot FM-1, the current state

The current state only. What was done, when and why (the lettered items 3b .. 3y27, with the owner's words) is in
[`HISTORY.md`](HISTORY.md): new work is recorded there, at the end; this file changes only when the state does.

## State in three sentences

Released: v0.1 and v0.11 .. v0.25, v0.25.1, v0.26 and v0.26.1 (`git tag`; **v0.26.1 is the latest stable release**,
HISTORY 3y191, on the site as the installer's default, notes in `docs/releases/`), the betas v0.22-beta1 ..
v0.22-beta4 and v0.24-beta1 .. v0.24-beta3, GitHub pre-releases listed by the installer behind "Show beta releases";
and the SMK-37 community test builds v0.24-beta2-smk37, v0.24-beta3-smk37, v0.24-beta3-smk37.1, v0.25-smk37 and
v0.26-smk37 (their own list `web/releases-smk37.json` and the unlinked page `webapp/smk37/`, SMK37.md). **0.26.1 is
0.26's firmware (`FELUCCA_VERSION` the one change), the first release built and signed by The Firmware Hub's shared
workflow** (release.yml's draft, build and publish, `tools/hub_build.sh`, 3y190; BUILDING.md "Signed builds"); 0.26
and older are unsigned. 0.26 over 0.25.1: with Dual Core On the VA's and FM6's chord voices are shared between the
cores as P5's are (`engine_t.split`, HISTORY 3y177, 3y187; docs/DUALCORE.md, docs/VA.md and docs/FM6.md "Two cores")
and `CR_RAM_VA` is on in a device build, bit for bit the one-core render; MOD's background is (0, 0, 0) (3y185); the
installers recognise Bubba Machine and refuse an install over it with the way back to V15 (3y176,
docs/INSTALL-COMPAT.md). The stored format is 0.25.1's. `web/releases.json` lists 0.26.1 first (sha256 f118c4e7...,
single-core 7fec6a72...), 0.26 with its going-back note; `web/releases-smk37.json` lists 0.26-smk37 first (base 0.26:
no SMK-37 build of 0.26.1, nothing changed for it). The tree's `FELUCCA_VERSION` is "ChoralRoot 0.26.1"
(`firmware/src/choralroot.c`): the next release bumps it. The owner's unit runs package C (3y187: 0.26's firmware
built as a dev build of 0.25.1, sha256 21307657...; its version line reads ChoralRoot 0.25.1; unit ID 5BD75BE3):
check with `python3 tools/fm1_install.py --info`.

## Repos and folders (all under `/Volumes/Q7Media-2025/Projects/Github/ChoralRootFM1/`)

| folder | what | remote |
| --- | --- | --- |
| `ChoralRootFM1/` | the firmware, the Mac emulator, the web installer, manager and site | github.com/Quixotic7/ChoralRootFM1, main, tags v0.1, v0.11 .. v0.25, v0.25.1, v0.26, v0.26.1, v0.22-beta1 .. beta4, v0.24-beta1 .. beta3, v0.24-beta2-smk37, v0.24-beta3-smk37, v0.24-beta3-smk37.1, v0.25-smk37 and v0.26-smk37 |
| `ChoralRootFM1Designer/` | the mock-up designer (MIT); PNG sheets in Git LFS | github.com/Quixotic7/MvaveFM1-Designer |
| `MvaveFM1Unbricker/` | the recovery tools, `TRANSPORTER-GUIDE.md` | github.com/Quixotic7/MvaveFM1Unbricker |
| `FM-1-transporter/`, `jl-uboot-tool/`, `MVaveOfficial/`, `Felucca/`, `melodee/`, `sloop-fm1/`, `fm1-x0x/` | upstream tools and references | read-only |
| `FM1VST/`, `fm1-backups/`, `fx-review/`, `UI Inspiration/` | the plug-in plan; the unit's backups; the effects' review WAVs and plots; the look's references | |

Read first: `PLAN.md` (§3 .. §5, §7), `docs/INTEGRATION.md`, the area's own document in `docs/`,
`tools/emu/README.md`.

## Working rules (the owner's)

- The orchestrator designs, briefs and verifies; Opus subagents write the code, each in its own worktree with files
  no other agent has; every report is checked by running the suites.
- Only the orchestrator flashes (`python3 tools/fm1_install.py build/choralroot.fwsc --yes`), one `--info` before a
  flash, never from two sessions, never while the owner uses another firmware: "hold off" until he says the unit is
  back. A full battery or USB power, an awake Mac.
- Never tag or publish unless the owner says "tag it" / "publish"; commit verified batches.
- Headless only: nothing that takes the focus (`build/host/emu --headless --script`; the designer through the
  preview pane, launch entry `fm1-designer`).
- The dcg hook: no `rm -rf`, no `git checkout --`, no branch deletion, no force push, no `>` to a path in a
  variable. Move aside with `mv`, write with the Write tool.
- Never raise the core rails above SYSVDD 14 / VDC14 5 (`hal/fm1_power.h`; VDC14 4 -> 5 in 0.22 on the reports of
  fm1-x0x#10 / #21, the owner's decision of 2026-10-14: docs/DUALCORE.md "Reliability").
- **The unit's load, read 2026-10-14** (`tools/perf_unit.py`; docs/INTEGRATION.md "Measured on the unit"): a VA
  MORPH PAD chord (6 or 8 notes + the bass) overloads core 0 (part0 2280 us, the shed drops voices); FM6 TINE EP
  with 8 notes is at 88 % and sheds while recording; CZ-1 and FM TONE at 70 .. 80 %. The fx stage's worst half is
  600 .. 720 us (its parts under load not yet read: `--debug`'s `fx_*`), TIMER5's 150 .. 230 dry and 350 .. 410 while
  recording. "The host says 2x headroom" (INTEGRATION.md) is the emulator's: on the unit the pads have none. The
  levers: the effects or half the chord part's voices on core 1 (designs in docs/FX.md, docs/DUALCORE.md), TIMER5's
  scan rate (an LED level's pulse is a time constant: halving the tick halves the dim levels' duty, FM1_LED_*_NS
  would double; the lit LEDs keep their duty), the fx stage's parts.
- Two changes the owner judged on the unit 2026-10-14 and made the defaults the same evening: the delay at the half
  rate in half the line (`cr_fx.c CR_FX_DELAY_DS` 2: "sounded good"; POOL 72.7 % for 91.7 %; `CR_FX_DELAY_DS=1
  ./build.sh` is the old delay) and the LCD's SPI at 20 MHz (`lcd.c LCD_BAUD` 2: "looks good"; the panel's sheet gives
  16 MHz: a unit whose screen misdraws goes back with `LCD_BAUD=4 ./build.sh`, and that would be worth a report).
  POOL is no longer the tight resource: 94 KB free on that branch; 103 KB (69.4 %) after the merge with the 0.22 line (3y76), where XIP (92.9 %) is the tightest now.
- Open (2026-10-14, found on the owner's unit): a RESTART (72, `crb_leave` -> `CRB_REBOOT` -> `fm1_reboot`) once
  crashed in the soft reset's P33 write with DBG_MSG `axi_rd_inv` (a DMA still reading: the USB or the DAC?), 37.6 s
  of uptime, with Dual Core On: a 4 s crash screen, then the reboot it wanted anyway. `fm1_install.py --debug` prints
  the last crash's record now (PEAK 70..78). Not yet understood or fixed; the bar ignores other masters' faults
  (`fm1_c1_crash_bars`). Look at what `CRB_REBOOT` stops before `fm1_reboot` (`fm1_fault` stops the audio first;
  `usb_detach` alone may leave the USB DMA reading).
- PA2 must never be driven high: it switches the key matrix off with the backlight (`hal/fm1_lcd_hw.h`).
- The boot stub's bytes are pinned (`firmware/stub/stub.lock`): a stub change rewrites sector 0x4000 and is a
  decision, not a side effect.
- A cost on the unit is counted from the device's disassembly at about 205 instructions a microsecond (docs/FX.md
  "The cost on the unit"), never taken from the emulator's estimate.
- A change that must not alter the sound is proven bit-exact (the goldens, WAV compares).
- Saving is manual (SAVE held; GLO + SAVE reverts) except the Options and the calibration (docs/SETTINGS.md).
- Never two agents in `tools/emu/test_cr.sh` at once in one tree (it wipes `build/emu/test`).
- Taste: one big thing per screen, pickers not lists, role colours (white chords, blue voicing, orange bass, red
  loop, yellow key, green FX), the layers' knob-row grammar, OCT- back / OCT+ confirm / the layer's button held again
  closes.

## Verification

One command runs the list below, whole, in about a minute on the owner's Mac (it was nine one after another;
HISTORY 3y36):

```
CR_VERSION=0.22 sh tests/run_all.sh          # ends "ALL SUITES PASSED (20 suites, N s; ...)", exit 0; else names the suites
sh tests/run_all.sh --only test_cr,install   # some of them (--list names them); a prerequisite not named is not run
```

It starts with `./build.sh` (Docker running), then runs the suites side by side where they share no file, each with
its documented command and its whole output in `build/scratch/logs/NAME.log`; it prints one line per suite (PASS /
FAIL, seconds), then the lines to read: the stub's, the size line, the goldens, the lint, the emulator scripts' ok
counts. There is no quick mode: nothing is skipped. `CR_VERSION` goes to `tools/emu/web/build_web.sh` alone, as in
the list below (the version being tagged); the other suites run without it. `sh tools/emu/perf.sh` and `sh tools/emu/classic_exact.sh` are not in it (estimates; a reference
build). What is behind it, to know when a suite is run alone:

- **The host programs are compiled once** (`tests/hostcc.sh`, used by `run_tests.sh`, `run_cr_tests.sh`,
  `tools/emu/build.sh`, `tools/emu/web/build_web.sh`): a program is kept only when its compile command (compiler
  version, every flag) and the sha256 of every file it was compiled from (sources, every header the preprocessor
  opened, `build/gen`'s and the system's, and the program itself) are unchanged. Hashes, not dates: `./build.sh`
  writes `build/gen` again on every run with the same bytes, and a file put back with an old date is still seen.
  `HOSTCC_FORCE=1` compiles everything, `HOSTCC_VERBOSE=1` says kept / compiled. Only programs are kept; every test
  runs every time.
- **`run_tests.sh` and `run_cr_tests.sh` run their sections side by side** (`tests/par.sh`) and print them in the
  file's order, so the output reads as before and the last line means what it meant. `PAR_JOBS=1` runs them one
  after another. `tests/install_test.py` runs its 20 phases in forks of itself (`INSTALL_TEST_JOBS=1`: in a row).
- **Two tests read what another suite makes, and say "skip" when it is not there**: `web/test_web.mjs` compares the
  editor's mock with `build/host/desc.json` (written by `run_tests.sh`), and `tests/site_test.py` (in
  `run_tests.sh`) checks the browser emulator in the site only when `build/emu-web` exists. `run_all.sh` orders
  them; by hand, run `build_web.sh` before `run_tests.sh` and `run_tests.sh` before `test_web.mjs`.
- **Tests that measure real time fail on a busy machine**: `tools/emu/web/test_web_emu.mjs` (the longest render
  quantum, so `run_all.sh` runs it last and alone), `tests/install_test.py` and the web tests (bounds of tenths of
  a second against simulated devices). Run a suite that failed beside a build again on a quiet machine before
  believing it.

The suites one by one. In a fresh worktree `./build.sh` comes first (Docker running; the site and installer tests
read `build/choralroot.fwsc`). No counts here: they go stale.

```
export DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
./build.sh                  # "stub: ... version 3, sha256 baee55ed142fe1cc" (= stub.lock), the size line, the package
sh tests/run_cr_tests.sh    # exit 0
sh tests/run_cr_draw.sh     # exit 0
sh tools/emu/build.sh && sh tools/emu/test_cr.sh && sh tools/emu/test_persist.sh     # each ends PASS
sh tools/emu/perf.sh        # estimates only: compare with main's, never quote as the unit's cost
python3 tests/install_test.py; for t in backup web sounds installer manager; do node web/test_$t.mjs; done
python3 tests/releases_test.py                    # web/releases.json: the versions the installer offers
sh tests/run_tests.sh       # ends ALL HOST TESTS PASSED
sh tools/emu/test_retired.sh                      # the engines retired in 0.18: a unit that comes with sounds on them
CR_VERSION=0.22 sh tools/emu/web/build_web.sh && node tools/emu/web/test_web_emu.mjs    # the browser emulator
sh tools/layout/build_layout.sh && node web/test_layout.mjs   # the layout viewer (docs/LAYOUT-TOOL.md; after build_web.sh:
                            # its frames are compared with the browser emulator's; HISTORY 3y47: step 2 of 5, a read-only viewer)
sh tools/emu/classic_exact.sh build/scratch/ref18 # Output Classic against a 0.18 reference build, bit for bit
```

- **The browser emulator's test must run before any tag** (`CR_VERSION` is the version being tagged). It needs
  Emscripten: 6.0.11 is in `~/emsdk` on the owner's Mac and is **not on the PATH**; `tools/emu/web/build_web.sh`
  sources `~/emsdk/emsdk_env.sh` itself when `emcc` is not found. (Several HISTORY entries of 0.19 say "not run: no
  emcc": they had not looked there.)
- **The 0.18 reference build** for `classic_exact.sh` is a `git archive 19b6453` built beside the tree: the four
  lines are in the script's head and in docs/AUDIO-QUALITY.md "Classic is 0.18's, bit for bit"; "What Classic's
  guarantee is now" there says what the two parts of the script prove since the stereo chorus.

POOL was the tight resource until 2026-10-14 (the delay at the half rate gave 65 KB back), again with GRAIN (97.0 %
on eb223df, beside the flash diet's 27.7 KB of FM TONE HARM cycles), and less so since 3y150: FM TONE reads its HARM
waves from the halves in flash with a mirror (the owner's choice, docs/FLASH.md; the HARM presets about +9 % of the
render). The size line is XIP 544 196 B of 573 440 (94.9 %), RAM 80 544 (81.9 %), RAMTEXT 16 120 of 24 576 (the
VA's render in RAM since 3y177), POOL 308 636 of 344 064 (89.7 %: GRAIN's two 32 KB source buffers the largest part,
its render scratch 2.3 KB since 3y168), 0.26's and 0.26.1's release builds the same; 0.25.1's XIP 542 828, POOL
308 508; 0.25's XIP 542 764 (before 3y172's move); 0.24's XIP 541 948, POOL 306 196; 0.24-beta2's (eb223df) XIP
537 084, POOL 333 900.
POOL is laid out by the linker, so a full POOL cannot fail in use, but tools/build.py refuses a build with under 8 KB
spare: about 27 KB can still be added. Read them off the size line.

## Release steps

BUILDING.md "Releasing" has them in full. The ones that are easy to forget, in order, before the tag:

1. `docs/releases/X.Y.md`: the first line replaced by the summary paragraph, a "Going back to" section.
2. `./build.sh --release X.Y` (both packages: `choralroot-X.Y.fwsc` and the single-core
   `choralroot-X.Y-1core.fwsc`, docs/DUALCORE.md "Two packages"), then **add the version to web/releases.json**
   (first entry: date, tag, 4 to 8 bullets for players, the sha256 of `build/release-X.Y/choralroot-X.Y.fwsc` and,
   as `sha256_1core`, of `choralroot-X.Y-1core.fwsc`: without it the installer offers no Single core for X.Y; the
   entry that was first gets a `going_back` note). The web installer's button, notes and "Other versions", the landing page's "New in X.Y" and the packages the
   site hosts all come from this file; `python3 tests/releases_test.py` checks it, and the Pages job refuses to
   publish a version that is not its first entry.
3. The browser emulator's test (Verification, above), then the tag when the owner says so. release.yml builds and
   signs the packages in The Firmware Hub's shared workflow (`zvenson/fm1-build@v1`, running `tools/hub_build.sh`;
   BUILDING.md "Signed builds"): the release starts as a draft and is published only when the signed packages are
   the ones `web/releases.json` lists. Never upload or replace a package on a release by hand: the hub's "Built from
   source on GitHub" holds only for the signed files.

A beta (`X.Y-betaN`, opt-in in the installer): the same steps with `"beta": true` on its entry; BUILDING.md "A beta
release".

To publish the site again without a new release (a change to the pages): run pages.yml by hand with the current
version and ref `main`. A local copy of the whole site, as Pages builds it: `python3 tools/fetch_packages.py`
(the listed packages into `build/scratch/packages/`, checked; needs the network once), then
`python3 web/make_site.py build/choralroot.fwsc X.Y build/site --require-all`. `fetch_packages.py` falls back to
`curl -sL` when Python's own download fails (this Mac's Python has no CA certificates) and says which it used. The
installer page shows its states without an FM-1 with `?demo=idle`, `older`, `single` (Advanced open, Single core
picked: needs a release with a single-core package), `writing`, `interrupted`, `done`,
`nomidi`, `stockwriting` and `stockdone` (the last two: the "Return to official V15" section, far down the page):
demo mode never asks the browser for MIDI and fetches nothing.

## Tools for the device

`tools/fm1_install.py` (python-rtmidi only; `--help` is complete): `--info`; `--debug` (SysEx 73 counters, then 78
PEAK, worst cases since the last read, cleared by reading: `stale_halves`, the audible ones, must be 0; `pk_*` the
worst half and its stages; `fx_*` the effects' parts; `irq_gap_*` the longest interrupt mask and where; table in
docs/FX.md); `--stub test|rescue`; `--rescan-midi`; `--screen off|on`;
`--select PART ENGINE POS`; `--backup` / `--restore`; `--sounds` and the per-sound import / export.
`tools/uac_check.py` records "ChoralRoot In" and judges by the unit's counters. After restarts in quick succession
the FM-1's MIDI port can stay offline in CoreMIDI: the tool names it, `--rescan-midi` brings it back. `gh` is not
installed on the owner's Mac.

## Open items

- **GRAIN at the unit** (docs/GRAIN.md section 6, step 6; HISTORY 3y137 .. 3y140): the owner plays it on main
  ("This Grain engine is fantastic!"); still to do there: a `grain` scenario for `tools/perf_unit.py` (not written yet: 8 notes +
  bass, CLOUD PAD and GLITCH, `part0` worst and average against GRAIN.md's 600 / 900 us), the render's time from a counter
  (the longest slice, the longest render), the eight presets by ear and their levels for `CB_TRIM`. Not built: a
  settings slot for GRAIN's remembered pool place (after a power cycle OPT + PRESETS lands on its first preset).
- **P5's rows on the unit** (branch lay-7, HISTORY 3y97, docs/LAYOUT-TOOL.md "Step 4, the firmware: what was
  built"): P5's editor is the owner's rows (`design/layouts/designs/p5-rows.json`, compiled by `tools/gen_layout.py` as
  a screen plan over pages of its own, served by `cr_pages.c cp_deep`); it replaced the panel of lay-5 (3y85 .. 3y87),
  whose free-screen editor and renderer are no longer in the unit (the layout tool keeps previewing free designs).
  Built and tested on the host, the emulators and the layout tool, **not device-tested**: the owner tries it on the
  unit. The other engines keep their classic pages; a design for one of them needs its classic screens of OSC .. MOD as
  the plan has them (edit8 of one or two lanes, a plan band) or free screens, its layout file's `params_hash`
  (`design/layouts/<engine>.json`) and a line in the generator's `BUILD`; the designer marks what the generator would
  refuse "not buildable yet", with its reason.
- **Snap on the unit** (branch claude/key-snap-a8626d, HISTORY 3y99, GitHub issue 18; docs/MANUAL.md "Key Mode",
  docs/SETTINGS.md): the KEY layer's page 2, KNOB 3: a key out of the scale plays the nearest note of the scale or of
  the chord sounding, with Key plays Notes or the Full keyboard (`cr_engine.c cr_snap_note`; bits 3 .. 4 of `key_on`,
  no version, the v14 caveat for a downgrade). The owner tried it on the unit ("This checks out"); released in
  v0.22-beta3 (3y101). The emulator has no Chord case over a loop (the host test uses the pads for the sounding
  chord). With it, 3y100: the Full keyboard with Key Mode on follows Key plays (Chords: the key's chords on every
  key; Notes: as before). A reply on GitHub issue 18 is the owner's.
- **Digitone `.dnsnd` import** (branch dn-1, HISTORY 3y56, docs/SOUNDS.md ".dnsnd import"): built from decoded files;
  refine with the owner's Digitone by controlled exports and `node tools/dnsnd_diff.mjs` (RATIO B, the LFO
  destinations, RATIO A's steps, the filter type order are the open ones). The LFO destinations are 1:1 since
  HISTORY 3y59 (FM TONE has the Digitone's 57, numbers 40..57 appended, the editor in the Digitone's order).

1. **The effects' pops, round 3 on the unit.** The pending bit is cleared before the render and the effects run
   block-wise (docs/FX.md "Late is not slow"); not yet read on the unit: the owner plays CZ-1 with both effects up,
   then `--debug` (PEAK). Switches to hear: `CR_FX_LITE=1`, `CR_ACK_LATE=1` (HISTORY 3y12, 3y13).
2. **Performance work**, in other worktrees: CZ-1's cost (docs/FX.md "CZ-1"), key polling during redraws.
3. **More preset slots for FM6, FM TONE, VA, CZ-1**: asked for with PHYS's cut, not built: the owner's decision
   (HISTORY 3y16 has the facts and the options). There is no table of slots per engine to redistribute: the 32 user
   slots are one pool shared by all engines, a slot takes any engine's sound, and the owner's unit used 10 of them
   on 2026-10-10. More slots mean new flash objects, new backup ids and POOL for their mirrors.
4. **Memory**: the store mirrors read through XIP (HISTORY 3t), FM6's tables to flash.
5. **FM TONE presets by ear** with the owner (docs/QUAD.md).
6. **The designer**: stale PNG sheets, and the landing page's two editor panel pictures (HISTORY 3x, 3y4, 3y12).
7. **The v0.16 and v0.18 release pages** may still begin "ChoralRoot 0.16 (in progress)" / "ChoralRoot 0.18 (in
   progress)": both tags carry that first line (the files were corrected after the tags: 0.16's in 1056af2, 0.18's
   with 0.19's notes). Edit the pages on GitHub. The rule since 0.17: the notes' first line is replaced by the
   release's summary paragraph before the tag, as 0.19's is.
8. **The MIDI port offline in CoreMIDI** after rapid restarts: cause unknown (HISTORY 3y2).
9. **The owner's review list** (behaviour, his decision; still true in the code): the BASS layer's footer says "a
   root: preview" but the roots pick the Behaviour silently; BASS + EDIT only with BASS held, not with its layer
   locked, and in the editor ENV + EDIT jumps to the bass sound; Raw Chord Sound On with Perform off doubles every
   note; step entry with LOCK latched does not advance (OCT+ each chord); ALGORITHM one click from off gives 00 INIT,
   a BASS tap the last bass sound; "Key: C" is hidden while a loop plays; Advance outside the PLAY menu: "next: slot
   N" is replaced at once; calibration names the SEL button "SCL"; the Chord view is black after PANIC until a
   chord; the modulation mark is blue for every source but ENV and LFO (the manual names three colours).
10. **CI runs no test suite** (`release.yml` builds, `pages.yml` builds the site).
11. **The emulator's build flags drifted** from the device's: `tools/emu/emu_firmware.h` has `FELUCCA_SLICER` 1
    (`choralroot.c`: 0) and builds without `FELUCCA_UAC`.
12. **M7 / M8**: the sticker sheet; Orchid captures (secret-chord map, chromatic Key Mode, factory patterns).
13. **Hardware checks nobody has done**: the reset-reason bits (console `boot`), SAFE MODE on a real crash, Windows
    enumeration of "ChoralRoot In", the stub's second attempt (one long flash, UBOOT), the dual-core checklist
    (docs/DUALCORE.md), the LED levels' cost (console `inp`).
14. **The loudness gap to Sloop 2.6** (docs/AUDIO-QUALITY.md "Verdict", "Open questions" 1): at the same MASTER a
    single note is a median 6.3 dB (Classic) / 8.8 dB (Clean) under Sloop 2.6's, a chord 3.0 / 4.9 dB, the power-on
    sound 13.8 / 16.2 dB; the loudest chords are level. It is the part LEVEL (92 against 104) and the per-sound
    trims, which 0.19 did not touch. The owner, for 0.19: **no per-sound loudness matching right now.**
15. **The make-up switch is unfinished** (`CR_OUT_MAKEUP=1 ./build.sh`, `fx.c CLN_MAKEUP` 5461: Clean's 2.5 dB trim
    given back in the DAC word's upper 6 dB). Not a release build: with it USB's master pair clips above the Q15
    sample's full scale in USB Level = Master and the SCOPE view's sample wraps; nobody has heard the unit's
    headphone and speaker stages up there (docs/AUDIO-QUALITY.md "Proposed, not built in").
16. **Classic is kept for comparison** (the owner's third-round decision; Clean is the default). Whether it stays is
    his. Since the stereo chorus it is 0.18 bit for bit only for a sound without a chorus send
    (docs/AUDIO-QUALITY.md "What Classic's guarantee is now"). Also open there: 32 or 64 samples of look-ahead
    (`-DCLN_LA_K=2u`).
17. **USB recording at 48 kHz is untested on phones, Windows and hubs** (docs/USB-AUDIO.md "The owner's test plan":
    macOS at both rates, a phone, Windows, the heavy case with `--debug`). One observation so far: macOS chose
    48 kHz unasked, which is why Options > USB Rate is opt-in. After the plan: whether six channels at 48 kHz stay
    opt-in, get the 16-tap filter, become the default, or 48 kHz is dropped.
18. **The unit's own speaker: the left channel or the sum?** Not known. It decides what the stereo chorus (and its
    Width, KNOB 3 of the Chorus page since 0.21) is on the speaker: the left tap alone is 0.18's chorus, the sum is
    docs/FX.md's "mono sum" column. 0.21's chorus changes (the glide under a turning Depth or Width, the Width
    knob) are built and tested on the host and in the emulator, not flashed and not heard on the unit:
    `../audio-0.21/chorus/` has the listening files.
19. **CZ-1's outlier trims are proposed, not applied** (docs/CZ1.md "Level", the table of sounds more than 4 dB from
    the engine's median: INIT TONE, STRINGS 1, BRASS 1, PIPE ORGAN 2 down; SWEEP 2, VOICE 2, JET ROAR, ACCORDION
    up). `CB_TRIM` has no CZ-1 row now. Also the owner's: whether a re-attack should open from a closed DCW when the
    DCA has let go (`cz_note_on`; not built; `../audio-0.19/cz-trim/brass1-bass-restrike-60ms.wav`).
20. **`cu_engine_switch` and the trim** (HISTORY 3y25): it sets trim 0 for the engine's first preset where
    `cu_pool_load` would read the sound's `CB_TRIM` row. No row makes it audible today; it becomes a bug the day an
    engine's first preset gets a trim (item 19 proposes rows).
21. **CZ-1's cost on the unit is above the count**: 1066 us measured for the chord part (`pk_part0_us`) against the
    630 us predicted for eight voices from the listing (docs/CZ1.md "Cost on the unit"; `tools/cz_pop_check.py`
    calls anything above 1450 far from the count). **A likely explanation since 3y41, a hypothesis until package E
    is measured**: the count is instructions, and code executed from the flash stalls on the code cache: P5's
    sample loop lost 26 % of its part's time in RAM with the same instructions (docs/FX.md "Code in RAM"; docs/CZ1.md
    "The unit"). `CR_RAM_CZ=1 ./build.sh` is CZ-1's render in RAM (item 29).
22. **USB capture's cost at 44.1 kHz**: about 126 us a half by the listing, and the SIE's waits on top, against
    `tools/emu/perf.sh`'s estimate of about 30 (scenario (u); docs/USB-AUDIO.md "What it costs" has the table and
    what to read on the unit: `pk_t5_us` with and without the stream). In the heaviest patches a recording at
    44.1 kHz can already cost a voice.
23. **TIMER5's cost, to be read on the unit.** The static budget (`tests/target_budget.py`) failed on the merged
    tree for `fm1_timer5_irq` without a tick having an instruction more: USB's control code was inlined into the
    ISR and its branches counted as loops. Since 2489a89 `usb_poll` and its rare events are functions of their own
    (`usb.c USB_OUTLINE`, `USB_COLD`) and the budget has a line for what a tick runs (4511) and lines for
    `usb_poll`, `ep1_rx`, `ua_service_paced`, `ua_transmit`; `fm1_alnk0_irq`'s line is the present 16415 (it was
    44152, from before PHYS left). The script counts loops only: straight-line growth of the scan is not caught by
    it. By the listing a tick is about 600 instructions with the stream closed (the 595's sixteen bits are 160 of
    them), about 3 us at 205 a us; the unit's own figure is higher (GPIO writes): `pk_t5_us` read 32 idle on the
    merged build (a 414 us block, four or five ticks nested: 6 to 8 us a tick), 214 playing and 266 with a 48 kHz
    recording having run on the builds before. Not read on a build with 2489a89: `pk_t5_us` idle about 32 and
    playing no higher than 214, `ua_svc_t5` still counting (the endpoint is served from TIMER5), `irq_gap_us` about 340 as before.

24. **Chord names: the owner's questions** (docs/CHORD-NAMES.md "Open questions"; HISTORY 3y29, 3y30). Options >
    Chord Names (0.20): Orchid, the default, or Standard (lead-sheet names; display only). The same user's second
    wish, the 6th an octave higher when a seventh is held, is built as **Options > 6th with 7th** (In place, the
    default / Octave up; HISTORY 3y30): **not heard by the owner yet** (the listening files are in
    `../audio-0.20/sixth/`), and not played on the unit. Open: whether Standard should write the major triad `C` (it
    keeps `CM`, because a single note reads `C` on every screen); `C13` standing for the chord with and without its
    9th; `Cmadd4` not saying that its fifth is missing; whether turning 6th with 7th should re-strike a chord that
    is held (today it acts from the next chord on).
25. **The web installer's "Return to official V15" has its own progress display (HISTORY 3y35): NOT used on a real
    FM-1 and not seen in real Chrome.** It is tested against the simulated FM-1 of `web/test_installer.mjs` and
    looked at in headless Chromium (`?demo=stockwriting`, `?demo=stockdone`). To read on the unit: how far the bar
    is when the official loader finishes (the write is measured against the whole image in 512-byte reads, 1368
    for the V15: an estimate, the loader is M-VAVE's), and whether an interrupted write really waits in update mode
    for the resume box, as the guidance says. The texts of the errors there still end "press Install again" (they
    are the install card's).
26. **P5 (the engine shown as "P5"; code name PROPHET) on the unit: one reading, of the build before this one
    (HISTORY 3y38, 3y39, docs/PROPHET.md "Cost on the unit").** The owner played cd43720 (five voices) on 2026-10-13:
    `pk_part0_us` 2033 (the listing's count said 1318: 1.54 × low), `pk_block_us` 2814 of 2902, `pk_late` 17,
    `stale_halves` 0, nothing heard. So the engine is **four voices of four budget units** now (three chord voices
    when the bass sounds on one core): about 1630 us for the part by proportion, **not measured**. To read on the
    unit with this tree: the same counters with a four-key 01 ITS PROPHET5 chord, with the bass on, and with a UNISON
    program (43 LAGOON BASS) on the chord part and on the bass part under a chord. Also to hear: which notes a five-
    and six-key chord keeps (the bottom and the top three), UNISON (one note of the chord, four voices; a UNISON bass
    thins to one voice under a chord), both filters, sync and Poly-Mod programs for aliasing, the wheel and aftertouch.
27. **P5, what is open** (docs/PROPHET.md "Open decisions"). Decided by the owner on 2026-10-13: the name P5, UNISON
    on a load, a trimmed list; and (3y51) **the list: the 115 programs he marked as favourites on the unit**
    (`assets/prophet5-factory/shipped.txt`, by Sequential's number; "We should only ship with the presets I
    favorited. The other presets can be optionally loaded from the website."). **Closed (the owner, 2026-10-14):
    the other 85 programs will NOT be hosted on the site** (it was open as "optionally loadable from the website":
    a publishing decision, Sequential's data). Open: whether a release carries Sequential's data at all (`FELUCCA_P5_FACTORY`; no licence granted); **the
    store for user programs and the device manager's "load all 200"** (the owner's plan; flash objects in 0xB6000 ..
    0xC7FFF as `va_store.c`, backup ids, POOL for the mirrors, the web manager's engine list: `web/fm1sounds.js`
    treats a record of engine 16 as no record today; a stored program must keep its 138 bytes, which carry its
    Sequential number); the settings record's place for P5's pool position and its `chord_sound` / `bass_sound`,
    which hold a pool position, not a program's number (harmless while the session record is there: it holds the
    program); a fifth voice (chord voices on core 1, the loop built per program, `P5_RAM`: none built). XIP is at
    91.3 % with the 115 (92.6 % with all 200, 89.3 % with INIT alone).

28. **Favourites and the three-unit P5 voice: not on a unit yet** (HISTORY 3y40). To do with the build of all 200
    (`P5_SHIPPED=all ./build.sh`): the owner marks P5 programs (SAVE, KNOB 4; or the editor's browser, KNOB 4), then
    `python3 tools/fm1_install.py --favorites` and `tools/p5_shipped_from_favorites.py` make the list (docs/PROPHET.md
    "The shipped list from the unit's favourites"). To read on the unit with it: **four P5 chord voices + a VA bass +
    all three effects** (`--debug`: `pk_part0_us`, `pk_block_us`, `pk_late`, `stale_halves`), and the same with a P5
    bass (five P5 voices: 15 of 16 units); `p5_units` goes back to 4 if it does not fit. Not decided: whether the
    star is yellow (`cr_draw.c CR_STAR_COL`); a favourites-only way of turning PRESETS and favourites in the web
    Sound Manager (not built, the store supports both); the browser's label does not name KNOB 4 (kept so that a
    screen without a mark is c176dd4's). The first right turn in the save dialog from Save as new is Favorite now.
29. **Code in RAM: eight packages for the unit, seven of them not measured** (HISTORY 3y41; docs/FX.md "Code in RAM" has the
    mechanism, the table of switches and the plan). `P5_RAM` is 1 in a device build now (the unit: `pk_part0_us`
    2084 -> 1533, `pk_block_us` 3136 -> 2623, `stale_halves` 22 -> 0 for four P5 voices, a P5 bass and three
    effects). The rest is switches, all off: `CR_RAM_FX`, `CR_RAM_MASTER`, `CR_RAM_CORE`, `CR_RAM_CZ`,
    `CR_RAM_VA`, `CR_RAM_FM6`, `CR_RAM_TABLES_P5`. `../ChoralRootFM1/build/scratch/p5-ram/` holds
    `choralroot-0-all-flash`, `-A-p5` (the default; byte for byte what was read), `-B-p5-fx-master`,
    `-C-all-audio` (B + CORE: the engines do not fit beside it), `-D-all-tables`, and `-E-p5-cz`, `-F-p5-va`,
    `-G-p5-fm6` (one engine each), with `README.txt`. **None but A has run on a unit.** To read for each, after a
    power cycle and a clearing `--debug`: `pk_part0_us`, `pk_part1_us`, `pk_fx_us`, `pk_master_us`, `pk_tick_us`,
    `pk_block_us`, `pk_late`, `stale_halves`. Then decide which switches become defaults (RAMTEXT is 24 576 B:
    FX + MASTER + CORE leave 1436; an engine's render is 6 to 16 KB), whether the tables follow (`SINE` + `TANH_Q15`
    2.6 KB and `CZ_CARRIER` 4 KB are the candidates, not built), and whether the audio ISR's own body (5.6 KB, not
    moved: it does not fit beside the rest) and P5's per-block code (1.9 KB) are worth it. Open by design: RAM code
    is not write-protected (a stray write into RAMTEXT changes audio code).
30. **The quiet writes: every automatic save erased a sector beside the playing (HISTORY 3y43; merged into p5-s and
    restricted in 3y55; not on a unit).** The owner on 74eae04's build, marking, typing and filtering favourites while playing: "I am now getting
    lots of audio glitches and dropouts now, even with older engines like FM6." The unit: `flash_erases` 9,
    `flash_holes` 0, `irq_gap_us` 17 851 at `irq_gap_pc` 0x02014E9E, `stale_halves` 46, `pk_late` 6. **Found, by the
    code:** `storage.c st_save` erased its copy at EVERY save (item 30 used to say "when a sector fills": wrong), so
    each settled change of a favourite or an Option was one sector erase, 17.8 ms with every interrupt masked, 1.5 s
    after the change and one silent second after the last sound: where the next note is. 0x02014E9E in that build's
    listing (the package's bytes rebuilt from 74eae04: the same sha256) is the instruction after `st_erase`'s `sti`,
    behind its call of `fl_erase4k_ram`. An erase leaves 5 boundaries unserved (6 in one of eight: 17.75 ms against
    2.902): 9 erases are 45 to 54 stale halves, the unit read 46. **So the 46 are the erases', by the count**; with
    `flash_holes` 0 every one of them started with the output under −60 dBFS, so what they cost is a note that came
    in those 18 ms (late, or its attack cut when it started between the poll's look and the erase) and the last of a
    tail. **Whether that is all the owner heard is not shown**: 9 holes in 56 minutes are few for "lots", and the
    same reading has `pk_block_us` 2785 of 2902 with 6 late halves: over 85 % of a half twice running, `audio.c
    shed_check` fades a voice, which is a drop-out no flash counter sees (`voices_shed` is on the console only).
    **Built**: the erase ahead (docs/SETTINGS.md "When flash is written"): the session's and the favourites' spare
    copy (**not the settings'**: the owner's decision of 2026-10-14, 3y55: the settings hold the panel calibration,
    their previous copy stays as the fallback until the next save, which erases then; an 18 ms pause when an Option
    changes, after 1.5 s unchanged and 1 s of silence as before, accepted) is erased at power-on before the audio starts and after 10 s without a sound or an input;
    a save then only programs (3 to 5 pages, each under a half); a change before the next erase ahead waits for 4 s
    of that quiet instead of one silent second; SAVE held writes at once as before. **To read on the unit** after a
    session of marking while playing, `--debug`: `erases_near` 0, `saves_auto` about the changes made,
    `erases_ahead` about as many, `flash_prog_us` (a page's time: not measured yet), `irq_gap_us` (17.8 ms only if an
    erase came since the last read: then compare `flash_erases` with `erases_ahead`), `stale_halves`. **And to tell
    the two causes apart**: play for some minutes WITHOUT touching a favourite, an Option or SAVE, read `--debug`
    before and after: `flash_erases` unchanged and glitches heard (or `stale_halves` above 0, `pk_late` rising) is
    the load, not the flash: then `pk_block_us`, the largest stage, and the console's `voices_shed`. **Open by
    design**: from the erase ahead on there is ONE copy of the favourites and of the session until the next save (before: the
    previous version stayed as a fallback against a flash fault); an erase cut by the power so late that the sector
    already reads 0xFF is not told from a whole one. `ST_AHEAD 0` (`choralroot.c`) is the old writer.
    star is yellow (`cr_draw.c CR_STAR_COL`); favourites in the web Sound Manager (not built, the store supports
    it); the browser's label does not name KNOB 4 (kept so that a screen without a mark is c176dd4's). **The filter
    and the type (HISTORY 3y42) are not on a unit either**: to read there: the SAVE menu's five entries and its four
    knobs, the yellow line of the read-out, and that a turn of PRESETS in P5's pool with the filter on is as quick as
    without. In the SAVE menu KNOB 1 no longer moves the entry (SELECT does): the one habit that changes.
31. **P5's crackle on the unit: NOT reproduced, NOT explained, nothing fixed** (HISTORY 3y44; docs/PROPHET.md "The
    crackle" has the sweep, the ranked guesses and the steps). The owner, 74eae04 with all 200, Dual Core On:
    "preset 16 .. minor + 6 + 9 then playing chord again as minor + 9 which had a long crackling effect like the voice
    got stuck in a washing machine .. All dry, no bass on". Place 16 is Sequential's 16, Canned Happiness (no UNISON,
    glide, sync or resonance; Vintage 123). The host and the emulator play the gesture clean in every timing and
    option tried, and an overload modelled (`EMU_SHED_OVER`) only thins the chord. (3y44 did fix another P5 fault it
    met on the way, a click 10 to 130 ms after every release of a short-release program: the part's DC blocker was cut
    to 0 when the last voice ended; 18 of the 49 P5 goldens changed for it. Canned Happiness does not have that one.)
    **For the crackle, what 3y44 adds is the reading**:
    78 PEAK's `shed_voices`, `shed_last_voice`, `shed_last_why`, `shed_over_halves`, `shed_last_us`, `voices_faded`,
    `voices_stolen` in `fm1_install.py --debug`. **To do on the unit** with
    `build/scratch/p5-fav/choralroot-p5-crackle-fix-all200.fwsc` (the name was asked for: for the crackle it counts,
    it fixes nothing): `--debug` to clear, make it crackle, `--debug` at once (`stale_halves`, `pk_*`, `irq_gap_us`,
    `flash_holes`, the overload line); record it over USB while it crackles (in the recording: rendered that way;
    only on the output: the DAC replayed a half); the same with Dual Core Off; whether the five-note chord is needed;
    which of `../audio-0.22/prophet/crackle/`'s files it is like. The first guess, an inference: the deadline
    (stale halves), not the engine.

32. **P5 after the unit's second reading (HISTORY 3y45; docs/PROPHET.md "The unit's second reading")**: nothing of
    it has been on a unit. The package: `build/scratch/p5-fav/choralroot-p5-i-all200.fwsc`. **To read / hear**:
    (a) the bass part's `pk_part1_us` with Bass off (the idle part: was the wheel's whole block every block, now
    FM6's idle cost; expect the order of FM6's 96 us), with Bass on and a program WITHOUT the UNISON switch (one
    voice: about 500 us by the chord's ratio) and with one that has it (40 of the 200: four voices with Dual Core
    On, about 1300; one under a chord with one core); (b) after a crackle, `c1_rejected`, `c1_faults`,
    `stale_halves` against `flash_erases`, and whether it ever comes with Dual Core Off and Bass off; (c) the
    steals by ear on a dark program (161 BRASS): a chord of five keys, an arpeggio over a held chord
    (`../audio-0.22/prophet/steal/` has the host's before / after / four); (d) a UNISON program's key against a
    chord (`../audio-0.22/prophet/headroom/`). **The owner's decisions**: docs/PROPHET.md "Open decisions" 4 (UNISON
    on the bass part), 8 (`P5_UNISON_TRIM`), 9 (`P5_STEAL_BLOCKS` 2 or 4), 10 (P5 on both parts with one core:
    about 105 % of the half with three effects, the shed thins the chord). **Not done**: a counter of the bass
    part's voices and of P5's filter states at their clamp in `--debug` (the two things that would settle "a stack"
    and "a filter that blew up" on the unit; 78 PEAK is 41 values in three tests); the chorus send's 16-bit clamp
    with both parts at send 127 (by arithmetic it is reached; any engine's); `FELUCCA_PROPHET=0`'s image against
    a773d30's (the new code is behind the switch; not compared).

33. **P5 on two cores, the level, Dual Core On by default (HISTORY 3y46; docs/PROPHET.md "Voices", "Two cores, eight
    voices", "Level"; docs/DUALCORE.md)**: built on the host and in the emulators, **nothing of it on a unit**. The
    package: `build/scratch/p5-fav/choralroot-p5-j-all200.fwsc`. **The owner's unit keeps its own Dual Core
    setting** (the default changed for units without a record only). **To read**: with Dual Core On, a four-, a
    six- and an eight-note chord of ITS PROPHET5, dry and with three sends up, then with a UNISON P5 bass:
    `pk_block_us`, `pk_part0_us` (core 0's share), `pk_part1_us` (core 1's whole job), `pk_wait_us`,
    `shed_over_halves`, `shed_voices`, `stale_halves`, `p5_core0_voices` / `p5_core1_voices` (four each is the
    rule), `p5_bass_voices`, `p5_bass_given_up`, `c1_rejected`, `c1_faults`. **Predicted**: four voices about
    1860 us (64 %), six about 2250 (77 %), **eight with three effects about 2632 (91 %): above the shed's line**;
    if so, `-DP5_CORE=3` (six voices) is the build that fits. **To hear**: the engine 6 dB lower (P5 is now the
    quietest engine: a four-note chord 6.6 dB under the VA's at the median); a UNISON key, whose voices now start
    together; a UNISON bass with one core (one voice at its stack's level) against two;
    `../audio-0.22/prophet/eight/`. **The owner's decisions left** (docs/PROPHET.md "Open decisions"): one core,
    three chord voices under a P5 bass (`-DP5_ONE_POOL=5`: four and one); the level. (Decided 3y53: the bass
    always keeps one voice, eight chord keys sound seven; `-DP5_BASS_KEEP` is gone.) **Not done**: a real hang of core 1's start on a unit (the boot
    after a failed one comes up on one core: host-tested only); `FELUCCA_PROPHET=0`'s image against a773d30's.
34. **A read never writes flash; who erased, and when (HISTORY 3y48; docs/SETTINGS.md "When automatic writes
    happen")**: a backup's LIST no longer calls `cr_settings_save` (it erased for a pending Option or mark whatever
    sounded: in the emulator one LIST during an arp made two erases); LIST serves unwritten marks from RAM. 78 PEAK
    46..53 (docs/FX.md): `erases_auto_sounding` (the rule 0), the last erase's writer / object / time, the times of
    `irq_gap_us`, `ua_arm_gap_max_us`, the last USB capture under/overrun, the clock. **To read on the unit**: the next
    stutter's `--debug` gives the times; a stutter with none of them near it is not the device's. Branch p5-k, not
    pushed; p5-g (e31c096) also appends to PEAK (values 35..38 on its own list): merge by name.
35. **The reverb on core 1, P5's coefficients once a part, word-wide memset (HISTORY 3y50; docs/DUALCORE.md "What
    each core runs", docs/PROPHET.md "The effects off core 0's path ...")**: bit-exact (246 goldens 0 changed; the
    threaded test: inline = thread = the effects all on core 0, every pair, three sends up; TSan clean). Predicted
    (not measured): eight P5 voices with three effects about 2400 .. 2440 us (83 .. 84 %) where the reading was 2632.
    The edges' study: nothing under -90 dBFS, nothing built (numbers for the owner). 78 PEAK 55..61: each core's busy
    time, the joins' waits, `pk_fx_map`. **To read on the unit**: the `all200` package of branch p5-m
    (`build/scratch/p5-fav/choralroot-p5-m-all200.fwsc`), 4 / 6 / 8 voices of ITS PROPHET5 with the three sends up:
    `pk_block_us`, `pk_c0_busy_us`, `pk_c1_busy_us`, `pk_fx_join_us`, `pk_fx_map`, `shed_over_halves`. Branch p5-m,
    not pushed; p5-l (CR_SINGLE_CORE) touches the core-1 start path: this branch does not.

- **WHEEL**: the owner is on the fence about cutting it. Not touched in 0.19.

## Facts that are easy to get wrong

- **Engines** (`engines.c`): indices are append-only, 0 .. 16; six are shown, in `ENGINE_ORDER`: FM6 (12), FM TONE
  (15, code name QUAD), VA (13), CZ-1 (14), P5 (16, code names PROPHET and `p5_`, since 0.22 in development), WHEEL (7). Every
  other slot is `ENG_GONE` (PHYS, 9, since 0.18:
  `FELUCCA_PHYS` 0, its files kept); records of retired engines stay and load as stand-ins (docs/PRESETS.md).
- **P5** (docs/PROPHET.md; shown as "P5", the files and switches say PROPHET): `FELUCCA_PROPHET` (1 in
  `choralroot.c`; `FELUCCA_PROPHET=0 ./build.sh` is the image without the engine, byte for byte the one before it:
  `choralroot.bin` sha256 501a9d09.. until the favourites, d43c66e3.. at 40acb01 and after 3y41 (3y40: the favourites
  are every engine's);
  code added for P5 in a shared file must stay behind the switch, and even a
  local variable outside it changed the image once), `FELUCCA_P5_FACTORY` (1: Sequential's programs of the shipped
  list; 0: INIT alone), `P5_SHIPPED` (the environment: another list file, or `all`), `P5_RAM` (1: its sample loop in
  `.ram_text`; **1 in a device build since 3y41**, `P5_RAM=0 ./build.sh` for flash; the source's default, the host's
  and the emulator's, is 0). **Four voices (`P5_POLY`), three budget units each since 3y40 (a measurement build: the
  chord keeps four voices with a bass; c176dd4: four units, three voices with a bass).** **A factory program's identity is its
  Sequential number 1..200** (in the program's own bytes: `p5_patch_id`), never its place in the pool: the pool is
  `assets/prophet5-factory/shipped.txt`, which changes; the session finds the place again from the number
  (`cr_settings.c css_part_put`), and the regression's P5 entries are named by the number. After the list changes:
  `python3 tools/p5_goldens.py` (P5's goldens and CPU entries only), not `GOLDEN_UPDATE=1`. **A load takes the
  program's UNISON switch** (`p5_track_loaded`); the two paths that restore a part from a record (`css_part_put`,
  `cu_pick_end`) set the recorded voice mode again after it (`p5_voice_restore`), and `cu_engine_switch` calls
  `p5_leave`. It has no store: a P5 part's SAVE menu has the three favourites' entries alone (Favorite, Favorites filter, Favorite type) and OCT+ there is refused (the session keeps
  the part's edited program over a power cycle), and the settings record has no slot for its per-engine pool position (back from another engine after
  a power cycle it lands on 01).
- **Sounds**: a pool per engine = INIT + the factory presets (ROM: FM6 25, FM TONE 16, VA 25, CZ-1 64, P5 48 of Sequential's 200: a list,
  WHEEL 5) + the user's. The user's are the 32 slots of `upreset.c` (`UP_SLOTS`, `core.h`), shared by all engines; a slot's deep
  patch sits at the same index in its engine's store (`va_store.c`, `fm6_ustore.c`, `cz_ustore.c`, `quad_store.c`).
- **Flash map**: the comment at the top of `storage.c` owns it. The FM TONE store is 0xB2000 / 0xB3000, the session
  0xB4000 / 0xB5000, **the favourites 0xB6000 / 0xB7000** (0.22, `OBJ_FAVS`); 0xB8000 .. 0xC7FFF **the sound pool** (0.22: 128 user
  slots shared by every engine, 13 banks floating over the 16 sectors, `storage.c sp_*`, `upool.c`, backup ids 50..62;
  0.21's sound objects and backup ids 6, 7, 9..13, 22 retired in 3y231: not read, not taken, their sectors free;
  docs/PRESETS.md "The pool", docs/FLASH.md "What the flash holds"); the loops
  are 0xC8000 .. 0xDBFFF.
- **Favourites** (0.22, `cr_favs.c`, docs/PRESETS.md "Favourites"): a bit per sound. A factory sound's key is (the
  engine's index, the preset's index in the engine's table), P5's the Sequential number 1..200, a user sound's its
  slot; a record over a factory preset has that preset's key; INIT has none. **Version byte 2** (3y42; version 1,
  40acb01's marks alone, is read: every mark Both), sections of tag / length / bits: tag the marks, tag | 0x40 the
  sounds typed Chord, tag | 0x20 typed Bass, 0xFF the options (bit 0 the filter); 262 bytes at most. Written as an
  Option is (the poll, when quiet), with SAVE held and before a backup's LIST. Backup id 23.
  The editor's browser: KNOB 4 marks. `tools/fm1_install.py --favorites` reads them (the type in the comment).
  `--restore-favorites FILE [--yes]` (3y54) writes such a text back (object 23 alone, read back; no restart).
- **The favourites' type and filter** (3y42): a TYPE per sound, Both / Chord / Bass, the sound's own (kept without a
  mark; KNOB 4 never changes it; a change of type marks an unmarked sound). The FILTER, one Off / On kept in the same
  object: PRESETS / ALGORITHM / the BASS layer's SOUND / the editor's browser step the part's fitting favourites of
  its engine (`cr_ui.c cuf_n`, `cuf_move`; a per-part cache of 32 bytes of bits, rebuilt on `crf_gen` / `up_gen` /
  the engine); none in the engine: the whole pool, "no favorites: all"; an engine change lands on the first; program
  changes, the session, loops, restores are not filtered. **The SAVE menu**: SELECT the entry (Overwrite, Save as
  new, Favorite, Favorites filter, Favorite type), KNOB 1 its value, and on every entry KNOB 2 the filter, KNOB 3 the
  type, KNOB 4 the mark; OCT+ saves on the first two only. **The naming page** (3y42, `cr_name.c`): a cursor
  (`cn.cur`, always on a character), SELECT moves it and adds a character right of the last, KNOB 1 the character
  under it, KNOB 4 left deletes it, every other knob inert (`cu_name_knob`, first thing in `cu_knob`); a key goes in
  after the cursor; the cursor is `cr_line_t.cur`, a
  steady bar.
- **SAFE MODE's Flash Data** erases 76 sectors (`cr_ui.c cu_flash_erase`: 0x97000..0xDFFFF and 0xFC000..0xFEFFF; 60
  until the sound pool joined after 0.24-beta2, 58 until the favourites).
- **Backup object ids** (`cr_backup.c`'s head): 1 settings, 6 / 7 user banks, 8 the FM6 bank, 9 the VA store,
  10 / 11 FM6, 12 / 13 CZ-1, 14 .. 21 the CZ banks, 22 FM TONE, 23 the favourites (0.22; a firmware before it does not
  list 23 and the clients skip it there), 40 .. 49 the loop slots. The session is not in a backup.
- **Settings**: the block is v15, 192 bytes, and never grows; the session record sits beside it. docs/SETTINGS.md
  owns both and the add-a-field procedure.
- **The package identity** is always FM-1_920; `./build.sh --release X.Y` sets only the version string and names.
- **Boot**: the stub is app.bin's first bytes, the commit marker at 0x91FE0; OCT- at power-on, or OCT- + OCT+ +
  HOME held 5 s, is update mode (docs/BOOT-SAFETY.md).
- **Audio**: a half is 128 frames (`HALF_FRAMES`, 2902 us), a control block `CTL` 32.

## The sound pool (0.22, branch pool-1; docs/PRESETS.md "The pool")

Facts: user slots **128** (`UP_SLOTS`, `UP_POOL` 1 with VA, CZ-1 and FM TONE), a slot = the record +
its engine's patch (384 bytes, `upool.c`); 13 banks of 10 floating over 0xB8000..0xC7FFF (`storage.c sp_*`); backup
ids **50..62** (not 24..36: Felucca's 32..34), legacy ids 6, 7, 9..13, 22 refused and 80 IMPORT gone since 3y231; favourites section **0x81** (slots 33..128); P5 sounds save (138 bytes). Tests: `tests/pool_test.c` (power cuts
on the floating banks), `tests/cr_pool_test.c` (every engine into U128 and back, the full pool, a cut on the firmware's
path, section 0x81), the web and install tests' pool cases (`tools/emu/test_pool.sh`, the migration from a 377264f
image, went with the migration in 3y231). Open items: nothing run on a unit (the save's 21 page programs: by reading,
not measured); Sequential program SysEx import for P5 not done; the
favourites filter covers pool places 1..255 only (fine for the default build: 244); the emulator's SAVE scripts reach
U033 (a mark on U100 is the host test's).
