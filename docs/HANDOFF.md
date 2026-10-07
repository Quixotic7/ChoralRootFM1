# Handoff: ChoralRoot FM-1, state at 2026-10-07 (commit `4ec3f42`)

Paste this into a new session as the first message.

---

Continue the ChoralRoot FM-1 project. Repos (all under /Volumes/Q7Media-2025/Projects/Github/ChoralRootFM1/):

| folder | what | remote |
| --- | --- | --- |
| `ChoralRootFM1/` | the firmware (GPL-3.0 fork of Felucca 1.0.1 for the M-VAVE FM-1), the Mac emulator, the web installer and site | github.com/Quixotic7/ChoralRootFM1, branch main, tags v0.1 v0.11 v0.12 v0.13 |
| `ChoralRootFM1Designer/` | the mock-up designer (MIT); its PNG sheets are in Git LFS | github.com/Quixotic7/MvaveFM1-Designer, Pages at quixotic7.github.io/MvaveFM1-Designer |
| `MvaveFM1Unbricker/` | `fm1_unbrick.py` (MIT): recovers a soft-bricked FM-1 ("WL82 UBOOT1.00") over USB on Windows/Linux via jl-uboot-tool; mock-device tests | github.com/Quixotic7/MvaveFM1Unbricker |
| `Felucca/` (at v1.0.1, tags to v1.0.5.1 fetched), `melodee/`, `sloop-fm1/` | read-only references | |

Read first, in order: `ChoralRootFM1/PLAN.md` (§3 controls, §4 grammar, §5 screens, §7 architecture, §8 milestones), `docs/INTEGRATION.md` (the mechanism and its Status section), `docs/EDITOR.md`, `docs/VA.md`, `docs/FM6.md`, `docs/CZ1.md`, `docs/USB-AUDIO.md`, `docs/LOOPER.md`, `docs/SETTINGS.md`, `docs/INSTALL-COMPAT.md`, `tools/emu/README.md` (key map, headless script grammar), and the memory notes (choralroot-fm1-workflow, -ui-taste, -device-build, opus-agents-for-coding, no-focus-stealing-gui, dcg-hook-blocks-rm-rf).

## Working rules (the user's)

- You are the designer/orchestrator; all coding is done by Opus subagents (Agent tool, model "opus", run_in_background), with self-contained briefs, disjoint file ownership, and you verify every report yourself by running the suites.
- NEVER launch the emulator, a browser window, or any GUI in the foreground; headless only (`build/host/emu --headless --script …`). The designer renders through its dev server in the app's own browser pane (preview tool, launch.json entry `fm1-designer` in the parent folder).
- Never let two agents run `tools/emu/test_cr.sh` at once (it wipes `build/emu/test`).
- Commit when a verified batch lands (the user wants frequent commits), push to main; NEVER tag a release yourself: tags are the user's (they said "hold off on tagging until I test"). The release workflow (`.github/workflows/release.yml`) builds on a `v*` tag, publishes the release with `docs/releases/<version>.md` prepended, and refreshes the Pages site.
- Flash every verified build to the user's FM-1 when it is plugged in: `python3 tools/fm1_install.py build/choralroot.fwsc --yes` (`--info` shows identity FM-1_920 and the version). Docker Desktop must be running for `./build.sh` (ask before starting it). `export DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib` for the host generators.
- The dcg hook blocks `rm -rf`, `git checkout --`, `git show ref:path > path`, force pushes and history rewrites: move trees aside with `mv`, write files with the Write tool, and give the user the command for anything destructive.
- The user's taste: one big thing per screen, the 1960s mod look, pickers not lists, stripe meters, role colours (white chords, blue voicing, orange bass, red loop, yellow key, green FX); key LEDs always mean the chord keys' own state; the printed black-key labels are not used.

## Verification (run all of these before calling anything done)

```
export DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
sh tests/run_cr_tests.sh          # engine 1791, loop 54, settings 48, VA 227364, transpose 24, backup 89, FM6 29, CZ 33, fm4_div0 5, usbaudio 35, bootguard 90, MIDI 26
sh tests/run_cr_draw.sh           # 51 screens, 0 lint findings
sh tools/emu/build.sh && sh tools/emu/test_cr.sh && sh tools/emu/test_persist.sh   # PASS / PASS (271 checks)
sh tools/emu/perf.sh              # scenarios a-h + u: CPU estimate, WAV click analysis
node web/test_backup.mjs; node web/test_web.mjs; node web/test_sounds.mjs; node web/test_installer.mjs; python3 tests/install_test.py   # sounds 99, installer 101, install (syx too)
./build.sh                        # size line; RAM 80.4 %, POOL 92.3 %, flash 59.8 % at 4ec3f42
sh tests/run_tests.sh             # Felucca's suite: known failures only: "text spacing" (font kerning check), Felucca's old 27-slot FM6 editor/backup expectations
```

## What is done (all on the device, commit 4ec3f42)

Milestones M0–M6 complete; M7 partly. The instrument: chord block on the keybed with B3 = LOCK, roots D4–G5, layers that lock open on hold (OCT−/HOME close), Key Mode, play styles, performance modes, bass, FX, the looper, Options one setting per screen, views incl. SCOPE and GEEK OUT, calibration, splash, MIDI in/out with clock both ways, backup/restore over SysEx (installer page + `fm1_install.py --backup/--restore`), the sound editor (groups → screens → lanes, SELECT wraps, SHIFT tap/hold, picker preview with OCT− cancel, quick mod mapping by holding ENV/LFO, no slide animations), engines: ANALOG PHASE LOFI VOICE TRIO WHEEL PHYS NOISE, ChoralRoot's **VA** (4 osc with Basic/Morph/Noise modes, morphing filter with stereo spread, 4 env, 4 LFO, 8-slot matrix, patch per user slot), Melodee's **FM6** (Dexed-exact, 24 factory, DX7 SysEx, patch per slot, capped at 8 voices) and **CZ-1** (Casio's 64 tones, 8 banks, Casio SysEx, patch per slot), the all-synth cut (SAMPLE/GRAIN/DRUM/sequencer removed), the performance pass (deferred saves, smoothed limiter, faded steals, cheap ring and rasterisers), USB audio **capture only** ("ChoralRoot In": master + CHORD + BASS, 6 ch; Options USB Record / USB Level; the serial console only when USB Record is Off or in SAFE MODE), the boot guard rework (power-on never counts; two crash boots → SAFE MODE; ROM boot after four; `boot` console command; GEEK line), Felucca 1.0.3.1's divide-by-zero brick fix (fm4_convert.c + div0 trap off) and 1.0.5.1's sub-bass fix, the installer guard (refuses Sloop / Felucca betas / unknown firmwares with an override; see INSTALL-COMPAT.md: the loader was never the cause), the website + installer on Pages, releases via Actions.

Released: v0.13 (brick fix) is live. Untagged since: CZ-1, SAFE MODE, capture-only USB audio, sub-bass fix. `docs/releases/0.14.md` is the draft for the next tag; the dev version string is "ChoralRoot 0.14" (choralroot.c).

## Open items, in the order the user set

1. **The user is testing** CZ-1, SAFE MODE and USB Record in Ableton on the device (it worked before the playback removal; re-check after). Fix what they report; then they tag v0.14.
2. **The published release bodies** for v0.1/0.11/0.12/0.13 show the generic text (the workflow lacked a checkout in the release job; fixed in `39dbe41`). The user can paste `docs/releases/<v>.md` by hand or `gh release edit`.
3. **Sounds page: done 2026-10-07** (docs/SOUNDS.md): the installer's Sounds section (`web/fm1sounds.js`) and `fm1_install.py --sounds / --export-sound / --import-sound / --rename-sound / --delete-sound`, no firmware change, verified on the device (the backup protocol ran on hardware for the first time). **.syx export / import done the same day** (SOUNDS.md ".syx export and import": DX7 single voice / bank for FM6, Casio tone dump for CZ-1; the import's record comes from per-engine templates generated by `tests/sound_templates.c`, pinned by `run_cr_tests.sh`'s `--check`), verified on the device. Left: more than 32 slots (SOUNDS.md "Not done": not worth it yet).
3b. **The loop indicator: done 2026-10-07** (the user chose B, the corner dial, from `design/choralroot-fm1-loop-screens.png`; generator `design/make_loop_mockups.py`): `cr_ui.c cu_dial` / `cu_ring` (the ring only while capturing, in the count-in / undo screens and the L_LOOP / L_SAVE layers; the dial while the loop merely plays, never in the editor or Options), `cr_draw.c cr_dial` in the top-line strip with its own cache key, the right text 22 px left; checks in `tests/cr_draw_test.c` and `tools/emu/scripts/cr_loop_load.txt` / `test_cr.sh`. Nit left: while an overdub is armed the top line does not yet say "Dub 3.2" (mock-up 7; `cu_header` builds "Dub" only once the overdub records).
3d. **The layers' knob row: FX, PERF and BASS done 2026-10-07; KEY / LOOP / METRO next.** Every layer hid what KNOB 1-4 do until a knob turned (a popup). The decided **layer grammar** (`design/choralroot-fm1-fx-screens.png`, generator `design/make_fx_mockups.py`; the main sheet's state 16): the picker on top and one row of four cells in the knob colours under it, each a **bespoke glyph that changes with the value**, the cell just turned on a hot block (800 ms, no popup), '–' for a missing parameter. Firmware: `CR_K_KNOBROW` (`cr_screen.h`; `cr_cell_t.pct2`; glyph ids `CR_G_ROOM MOON ECHOES LFO CLIP SPRING MIX GATE RANGE ARROW SHIFT`, drawn by `cr_draw.c cr_picto`), `cr_ui.c cu_fx_cells` builds the FX layer (Size room · Damp moon (1 - v) · Type room / spring · Amount mix; Delay echoes (time index, feedback) · Colour moon; Chorus lfo (rate, depth); Drive clip), the knob handler sets `cu_fx_hot` instead of a popup; the cache composes only the row's strips on a turn (`perf.sh` (h): UI frame 2875 us avg, was 4124 with the popups). Fixtures: `tests/gen_cr_screens.py` reads the fx sheet too (states 45-51); `tools/emu/scripts/cr_fx_row.txt`. **PERF and BASS done** (the fx sheet's states 10, 11, now fixtures 52-53): `cu_perf_cells` (the mode's `CU_PERF_KNOB` parameters, labels as the popups named them, so ARP reads Division / Direction / Gate / Swing where the mock-up says Rate / Order / Range / Gate; glyph table in cr_ui.c), `cu_bass_cells` (Behaviour text, Register shift, Sound text, Level bar), footer "a root: preview · OCT-: back · HOME"; the hot cell is `cu_hot` {layer, knob, until} for all three layers; tests `cr_perf_row.txt`, `cr_bass_row.txt`. Also: the top line reads "Dub 3.2" while an overdub is merely armed. **Next**: KEY (Tonic, Scale, Transpose shift, Single Notes), LOOP (Sync, Quantize, Count-in, Level) and METRO, dropping their popups; their mock-ups are being drawn.
3c. **Melodee 0.12** (github.com/keremimo/melodee, tag v0.12, fetched into `melodee/`): native user presets (64 FM6 voices, 128 CZ-1 tones as raw tones in the pool, SysEx command 78 per slot, .syx import / export in its editor), 64 general presets (ids 17 / 18), microtonal scales, A4 400-480 Hz, timed recording, note editing; its storage.c leaves the last 256 bytes of its new objects erased because of the SPL's scan: checked here, the SPL reads only the fixed OTA record at 0xE4F00 (`ota.c OTA_RES`), so ChoralRoot's loop records (up to 3664 + 256 bytes into a sector) are not affected. Nothing else of 0.12 is a ChoralRoot concern.
4. **M7 finish**: bank by ear (the user), sticker sheet, README/manual (EDITOR.md/VA.md/FM6.md/CZ1.md are most of it), the Options "Calibrate" entry exists.
5. **Hardware-only checks nobody has done**: the reset-reason register bits (console `boot`), SAFE MODE on a real crash, the USB-record clock drift over an hour, Windows enumeration of "ChoralRoot In", the Reddit user's bricked unit (guide in the Unbricker README).
6. **Felucca 1.0.5.1 leftovers** not ported: the LED "breath" (fm1_input.h), its UI changes (not used by ChoralRoot).
7. M8: Orchid captures (secret-chord map, chromatic Key Mode, factory patterns).

## Facts that are easy to get wrong

- Engine indices (append-only): 0 ANALOG, 1 reserved, 2 PHASE, 3 LOFI, 4 SAMPLE (retired), 5 VOICE, 6 TRIO, 7 WHEEL, 8 GRAIN (retired), 9 PHYS, 10 DRUM (retired), 11 NOISE, 12 FM6, 13 VA, 14 CZ-1. SLICE is off.
- Flash map: settings 0xFC000/0xFD000; VA store 0x97000 pair; FM6 user stores 0x99000 and 0x9B000 pairs; CZ store 0x9D000 pair + 0xA0000 pair; CZ banks 0xA2000..0xB1FFF; FM6 bank 0x9F000/0xFE000; user sound banks 0xDC000..0xDFFFF; loops on sample slot 3 at 0xC8000..0xDBFFF; SAFE MODE's erase covers 54 sectors of these.
- Backup object ids: 1 settings, 6/7 user banks, 8 FM6 bank, 9 VA store, 10/11 FM6 stores, 12/13 CZ stores, 14–21 CZ banks. Felucca/Melodee backups restore their common objects.
- The package identity is always FM-1_920; `./build.sh --release X.Y` only sets the version string and names.
- `tools/sdk/` vendors the three JieLi SDK files (Apache-2.0); the release workflow builds without network except the toolchain download.
- Settings record: PER5 Felucca record + ChoralRoot block CRS v5 (`docs/SETTINGS.md` has the add-a-field procedure).
