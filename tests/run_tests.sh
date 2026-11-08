#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Host tests of the firmware's kept sources in ChoralRoot's configuration (no hardware; tests/hostsim.c's base). Run from
# the repo root after ./build.sh:
#   tests/run_tests.sh
# (ChoralRoot's own host tests are tests/run_cr_tests.sh. Felucca's sequencer, UI, editor, projects and the retired
# engines left the tree in 3y232 with their tests: scale, chord, project, motion, MIDI control, DIGITAL, the UI and its
# renders, persistence, backup, editor, SLICER, swing, the FX layer, REVERB TYPE, PHYS, DRUM, NOISE, SLICE.)
#
# Regression suite (tests/regress.c, tests/target_budget.py; details at the top of regress.c):
#   golden renders  every engine x preset, voice modes (VA, FM6, P5), FX sends (VA), the SLICER's GATE: one hash each
#                   in tests/golden.txt. A change of the sound fails with the list of renders.
#   health          clipping, DC, peak level, voices free after the release, silence at the end.
#   CPU             instructions / sample per preset and mix (tests/cpu_baseline.txt, +25 %), ns printed;
#                   target: loop instructions of the render functions in the pi32v2 disassembly
#                   (tests/target_budget.txt, +10 %; exact, static).
#   voices          the shared budget of the chord and bass parts, steal fades, MONO / LEGATO / UNISON keep their
#                   note, P5's cap, key-ups after live mode changes.
# Audio (tests/audio_test.c): the real audio ISR against a simulated DMA: bounded overload fades, shared-voice limits.
# MOD (tests/mod_test.c): the modulation matrix: slots that do nothing are bit-identical, every source on each
#                   kind of destination, clamping, MIDI CC1 / CC11 / aftertouch routing, the cost of 4 active
#                   slots (at most +5 %), demos in build/mod_demo/.
# INPUT (tests/input_test.c): the key / button debounce of hal/fm1_input.h against the TIMER5 scan and bouncing
#                   contacts: a press within 2 scans (<= 2.3 ms), one note per bouncy press, no early or hanging
#                   release, stray samples ignored, fast repeats, the encoders' detents; the LED scan: lit LEDs every
#                   frame, dim ones a short pulse (the second line write) every frame, each only on its own column.
# USB audio (tests/cr_usbaudio_test.c, also in run_cr_tests.sh): Melodee's UAC1 recording as ChoralRoot builds it
#                   (no playback): the descriptors in each presentation, routing, packing, the ring against drift,
#                   the 44.1 kHz stream's digest. tests/cr_usbaudio48_test.c: the second rate (0.19), its requests,
#                   the 44.1 -> 48 kHz resampler's exactness and spectrum, the 48 kHz stream, a rate switch.
# web (web/test_web.mjs): the package builder, the updater, the editor page's protocol against its mock device.
# Transporter (tests/transporter_shim_test.py): tools/transporter's stand-ins for the FM-1 Transporter's fm1_ota / fm1fw:
#                   the review gate before a write, on synthetic packages and build/choralroot.fwsc.
# FM6 (tests/fm6_test.c, Melodee's): the engine (src/eng_fm6.c, src/fm6_core.c, Melodee's: Dexed's rendering): DX7
#                   SysEx framing, the 32 algorithms against the DX7 diagrams, the 24 factory patches through pack /
#                   unpack and the bank / voice SysEx, pitch, levels, envelopes, modulation, no DC / clipping, the
#                   macros, Dexed's 16 voices, the cost per voice; demos in build/fm6_demo/.
# Change baseline entries only for reviewed, intentional differences in sound or cost;
# retain every unaffected golden / CPU / target entry. VERBOSE=1: every render.
#
# The verdict is the last line, always: "ALL HOST TESTS PASSED" (exit 0) or "HOST TESTS FAILED" (exit 1), with the
# sections that failed and the ones skipped listed above it. A test that fails is counted and the suite goes on to the
# next: run and missing are the only places where a failure does not end the script. Anything else that fails (a test
# that does not compile, a generator, a missing build) ends the suite there, at its place in the report (report(),
# below; set -e for the script's own lines), and the EXIT trap still prints the verdict, so a run that stopped early
# cannot read as one that has only the failures printed so far. A skipped section needs a tool that is optional
# (node, DaisySP, Pillow) and is named again above the verdict.
# The sections run side by side (PAR_JOBS at once, default the number of cores; PAR_JOBS=1: one after another) and are
# printed in the order of this file; a program is compiled again only when its command or a file it was compiled from
# has changed (tests/hostcc.sh: hashes, not dates; HOSTCC_FORCE=1: everything). Every test runs every time.
set -e
NL='
'
fail=0
failed=
skipped=
section="(the start)"
ended=0
verdict() {
    rc=$?
    trap - EXIT
    par_kill 2>/dev/null || :                        # (an early end: the sections still running)
    if [ "$ended" -ne 1 ]; then
        echo "== the suite stopped (status $rc) in or after: $section"
        fail=1
    fi
    [ -z "$skipped" ] || echo "== skipped:$skipped"
    [ -z "$failed" ] || echo "== failed:$failed"
    if [ "$fail" -eq 0 ]; then echo "ALL HOST TESTS PASSED"; exit 0; fi
    echo "HOST TESTS FAILED"
    exit 1
}
trap verdict EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
# The sections below are written in the order they are reported, as before; they start in the background as the
# script reaches them (tests/par.sh: compiles through tests/hostcc.sh, which keeps a program whose command and inputs
# are unchanged) and report() prints them in that order: a compile's diagnostics where it stood, then "== section"
# and the test's output. A section waits for the compiles and files written above it since the last section, for the
# program on its command line, and for PAR_AFTER (a section that reads what another one writes).
run() { par_run "$@"; }
skip() { par_note skip "$1"; }
missing() { par_note missing "$1"; }
quiet() { "$@" >/dev/null; }                        # (a generator's chatter)
into() {                                            # into FILE CMD...: CMD's output is the file, put there whole
    into_f=$1; shift; into_rc=0                     # (web/test_web.mjs may be reading the one before: tests/run_all.sh)
    "$@" > "$into_f.tmp" || into_rc=$?
    mv "$into_f.tmp" "$into_f"
    return "$into_rc"
}
report() {
    for id in $par_order; do
        case $(par_kind "$id") in
            build|prep)                              # not a test: its failure ends the suite here (as set -e did)
                rc=0; par_wait "$id" || rc=$?
                cat "$PAR_DIR/$id.log"
                [ "$rc" -eq 0 ] || exit "$rc" ;;
            run)
                section=$(par_title "$id")
                echo "== $section"
                rc=0; par_wait "$id" || rc=$?
                cat "$PAR_DIR/$id.log"
                [ "$rc" -eq 0 ] || { fail=1; failed="$failed$NL   $section"; } ;;
            skip) echo "== skip $(par_title "$id")"; skipped="$skipped$NL   $(par_title "$id")" ;;
            missing) echo "== $(par_title "$id")"; fail=1; failed="$failed$NL   $(par_title "$id")" ;;
            stop) echo "$(par_title "$id")"; exit 1 ;;
        esac
    done
}

export AC79_SDK="${AC79_SDK:-$HOME/fw-AC79_AIoT_SDK}"
cd "$(dirname "$0")/.."
# Pillow finds Homebrew's libraqm (the text spacing reference: the font's kerning). macOS strips DYLD_* from the
# environment of /bin/sh, so an exported value does not reach this script: set it here (as build.sh does).
if [ "$(uname -s)" = Darwin ] && [ -z "$DYLD_FALLBACK_LIBRARY_PATH" ] && [ -f /opt/homebrew/lib/libraqm.dylib ]; then
    export DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib
fi
OUT=build/host
mkdir -p "$OUT"
# P5's factory header follows the shipped list (assets/prophet5-factory/shipped.txt; written only when its text changes)
[ -d build/gen ] && python3 tools/gen_prophet_factory.py build/gen/melodee_prophet_factory.h >/dev/null
[ -d build/gen ] && python3 tools/gen_layout.py build/gen/cr_layouts.h >/dev/null   # (the compiled layouts)
. tests/par.sh
par_init "$OUT/jobs"
CC="${CC:-cc} -O1 -Wall -Wno-unused-function"

par_build $CC -o "$OUT/storage_test" tests/storage_test.c
run "flash storage (A/B, torn writes)" "$OUT/storage_test"
par_build $CC -o "$OUT/pool_test" tests/pool_test.c
run "sound pool (floating banks, power cuts)" "$OUT/pool_test"
# the flash driver's erase / program wait (hal/fm1_flash_wait.h, felucca-bsp #12, 3y264): never back to XIP while the
# part is busy; -4 past 500 ms / 10 ms; the part and the chip reset at 2 s / 50 ms; across the TIMER4 wrap
par_build $CC -std=c11 -Wextra -Werror -fsanitize=address,undefined -o "$OUT/flash_wait_test" tests/flash_wait_test.c
run "flash erase / program wait: never XIP while busy, late at the bound, the reset at 2 s / 50 ms" "$OUT/flash_wait_test"
run "fm1_flash.h: the erase and the page program wait with fl_wait_issued (500 ms / 2 s, 10 ms / 50 ms), never fl_wait_ready" \
    sh -c 'f=firmware/hal/fm1_flash.h
           grep -q "rc = fl_wait_issued(issued, 500000u, 2000000u, took_us);" $f &&
           grep -q "rc = fl_wait_issued(issued, 10000u, 50000u, took_us);" $f &&
           ! grep -q "rc = fl_wait_ready(" $f && grep -q "^#include \"fm1_flash_wait.h\"" $f && echo "ok: both waits busy-safe" ||
           { echo "FAIL: $f does not use fl_wait_issued for both waits"; exit 1; }'

par_build $CC -o "$OUT/upreset_test" tests/upreset_test.c
run "user sound records (UP_PUT parser, versions, PHYS DRUM -> DRUM on a read, grid records, DIGITAL kept)" "$OUT/upreset_test"

par_build $CC -o "$OUT/input_test" tests/input_test.c
run "keys and buttons: fast press, long release, bouncy contacts (one note each), glitches, encoders" "$OUT/input_test"

par_build $CC -o "$OUT/midi_uart_test" tests/midi_uart_test.c
run "TRS MIDI parser" "$OUT/midi_uart_test"

par_build $CC -std=gnu11 -o "$OUT/rt_cr_usbaudio_test" tests/cr_usbaudio_test.c
run "USB audio: descriptors, routing, packing, clock drift and recovery" "$OUT/rt_cr_usbaudio_test"
par_build $CC -std=gnu11 -o "$OUT/rt_cr_usbaudio48_test" tests/cr_usbaudio48_test.c -lm
run "USB audio at 48 kHz: the two rates, SET_CUR, the resampler, a rate switch" "$OUT/rt_cr_usbaudio48_test"
par_build $CC -std=gnu11 -w -o "$OUT/rt_cr_usbrate_test" tests/cr_usbrate_test.c
run "Options > USB Rate: 0.18's descriptors at 44.1 kHz, SET_CUR, the digest, the replug" "$OUT/rt_cr_usbrate_test"
# USB MIDI host (tests/usb_host_test.c, Melodee's, 3y260): usb_host.c against a model of the SIE, a keyboard, hubs, a stick
par_build $CC -std=gnu11 -fsanitize=address,undefined -o "$OUT/usb_host_test" tests/usb_host_test.c
run "USB MIDI host: duplex I/O, enumeration, hubs, ACK ownership, back-pressure, recovery, the parts released" \
    "$OUT/usb_host_test"
par_build $CC -std=gnu11 -DFELUCCA_OTA=1 -fsanitize=address,undefined -o "$OUT/usb_host_ota_test" tests/usb_host_test.c
run "USB MIDI device after USB Host Off: notes and clock as queued, a SysEx never split" "$OUT/usb_host_ota_test"

# the build's outputs the sections below read: without one of them a section would have nothing to test
have=1
for f in build/choralroot.fwsc build/choralroot.dis build/gen/felucca_tables.h build/gen/ui_fonts.h; do
    [ -f "$f" ] || { par_note stop "== no $f: run ./build.sh first"; have=0; break; }
done
if [ "$have" -eq 1 ]; then                           # (else: the sections above are reported, then that line ends it)

par_build $CC -DOWN_PKG=1 -o "$OUT/ota_test" tests/ota_test.c
run "M-UPGRADE entry (own loader)" "$OUT/ota_test" build/choralroot.fwsc

# the loader, and its interruption model (docs/BOOT-SAFETY.md "As built"): the install cut after every erase and every
# programmed page, the boot stub's decision on each state, the resume; over a firmware without the stub (the 0.16
# layout), over the same stub with another body, over the dark unit's state, over an earlier stub (a stub change).
# Then the old order: a package without
# the marker written by this loader
for m in no-stub other-body no-stub-other old-stub; do
    PAR_MAKES="$OUT/stub-test-$m.fwsc"
    par_prep quiet python3 tools/make_stub_test_pkg.py "$m" -o "$OUT/stub-test-$m.fwsc"
done
par_build $CC -O2 -o "$OUT/ldr_test" tests/ldr_test.c
run "update loader: no stub -> this build; the interruption model" "$OUT/ldr_test" "$OUT/stub-test-no-stub.fwsc" \
    build/choralroot.fwsc "$OUT/stub-test-other-body.fwsc" "$OUT/stub-test-no-stub-other.fwsc" "$OUT/stub-test-old-stub.fwsc"
run "update loader: this build -> a package without the marker (address order)" "$OUT/ldr_test" build/choralroot.fwsc \
    "$OUT/stub-test-no-stub.fwsc"

# the M-Vave SMK-37 (BOARD_SMK37, SMK37.md): the flash map, the store's owner tag, the package layout, the OTA staging
# and the loader with its interruption model on SMK-layout packages (made here from the FM-1 binaries: only the layout
# and the protocol are under test)
par_build $CC -DBOARD_SMK37=1 -Ifirmware/hal -o "$OUT/smk_flash_test" tests/smk_flash_test.c
run "SMK-37 flash map: allow-lists never reach the head, BTIF/USRTRIM, USR, key_mac; every object's place" \
    "$OUT/smk_flash_test"
par_build $CC -DBOARD_SMK37=1 -Ifirmware/hal -o "$OUT/smk_owner_test" tests/smk_owner_test.c
run "SMK-37 store owner tag: another firmware's store cleared, ours kept, nothing else written" "$OUT/smk_owner_test"
par_build $CC -DBOARD_SMK37=1 -Ifirmware/hal -o "$OUT/smk_trs_test" tests/smk_trs_test.c
run "SMK-37 TRS MIDI out: bytes in order, the clock ahead, bursts never block or cut, releases always in, Off" \
    "$OUT/smk_trs_test"
PAR_MAKES="$OUT/smk-new.fwsc"
par_prep quiet python3 tools/fm1pkg_make.py --board smk37 --product "SMK-37 Pro_920" build/choralroot.bin \
    build/loader/ota.bin "$OUT/smk-new.fwsc"
run "SMK-37 package layout (fm1pkg_make --board smk37): PRCT 0x9D000, stock region table, the stub's places" \
    python3 tests/smk_pkg_test.py "$OUT/smk-new.fwsc"
par_build $CC -DBOARD_SMK37=1 -DOWN_PKG=1 -o "$OUT/ota_test_smk" tests/ota_test.c
run "M-UPGRADE entry, SMK-37 build (staging 0xBB000, parm ota-SMK-37 Pro_016)" "$OUT/ota_test_smk" "$OUT/smk-new.fwsc"
for m in 1 2; do                                     # the MODEL=elite / mke builds of ota.c and the stub's record
    par_build $CC -DBOARD_SMK37=1 -DT_MODEL=$m -DOWN_PKG=1 -o "$OUT/ota_test_smk_m$m" tests/ota_test.c
    run "M-UPGRADE entry, SMK model $m build (MODEL=elite / mke: its own parm identity, the stub's the same)" \
        "$OUT/ota_test_smk_m$m" "$OUT/smk-new.fwsc"
done
for m in no-stub other-body no-stub-other old-stub; do
    PAR_MAKES="$OUT/smk-stub-test-$m.fwsc"
    par_prep quiet python3 tools/make_stub_test_pkg.py "$m" --board smk37 -o "$OUT/smk-stub-test-$m.fwsc"
done
par_build $CC -O2 -DBOARD_SMK37=1 -Ifirmware/hal -o "$OUT/ldr_test_smk" tests/ldr_test.c
run "update loader, SMK-37: app area 0x4000-0x9CFFF, the interruption model, the protected ranges untouched" \
    "$OUT/ldr_test_smk" "$OUT/smk-stub-test-no-stub.fwsc" "$OUT/smk-new.fwsc" "$OUT/smk-stub-test-other-body.fwsc" \
    "$OUT/smk-stub-test-no-stub-other.fwsc" "$OUT/smk-stub-test-old-stub.fwsc"
for m in pro elite mke; do                           # the packages BOARD=smk37 MODEL=... ./build.sh made, when present
    f=build/choralroot-smk37.fwsc; id="SMK-37 Pro_920"
    [ $m = elite ] && f=build/choralroot-smk37-elite.fwsc && id="SMK-37 Elite_920"
    [ $m = mke ] && f=build/choralroot-smk37-mke.fwsc && id="MKE-P37_920"
    [ -f $f ] && run "SMK package $f: identity $id, stock region table" python3 tests/smk_pkg_test.py $f "$id"
done

if [ -f build/gen/felucca_tables.h ]; then
    par_build $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/speaker_test" tests/speaker_test.c -lm
    run "SPEAKER EQ: the three modes' response (BASS+ no longer cancels 440 Hz, #42), BASS+ harmonics, the sub cut" "$OUT/speaker_test"
    par_build $CC -O1 -w -Ibuild/gen -Ifirmware/src -o "$OUT/theme_test" tests/theme_test.c -lm
    run "themes: contrast, text blending and font metrics" "$OUT/theme_test"
    par_build $CC -O1 -w -Ibuild/gen -Ifirmware/src -o "$OUT/text_ref_test" tests/text_ref_test.c -lm
    run "text: pens, kerning and pixels equal the reference renderer" "$OUT/text_ref_test"
    if python3 -c "import PIL" 2>/dev/null; then
        run "text spacing: glyph gaps against the font's own (16x supersampled), S M <= 0.25 px, L <= 0.5 px" \
            python3 tests/text_spacing_test.py build/gen/ui_fonts.h
    else
        skip "text spacing (python3 has no Pillow)"
    fi
    par_build $CC -O1 -w -Ibuild/gen -o "$OUT/settings_test" tests/settings_test.c
    run "settings: PER1..PER4 migration, palette ids and preference preservation" "$OUT/settings_test"
    par_build $CC -w -Ibuild/gen -Ifirmware/src -o "$OUT/audio_test" tests/audio_test.c -lm
    run "audio: overload protection, bounded fades and DMA diagnostics" "$OUT/audio_test"
    par_build $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/mod_test" tests/mod_test.c -lm
    mkdir -p build/mod_demo
    run "modulation matrix: off = bit-identical, the math, MIDI CC1 / CC11 / aftertouch, cost, demos" "$OUT/mod_test" build/mod_demo
    par_build $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/regress" tests/regress.c -lm
    run "regression: golden renders, health, voices, CPU budget" "$OUT/regress" tests/golden.txt tests/cpu_baseline.txt
    par_build $CC -std=gnu11 -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o "$OUT/rt_cr_usbtap_test" tests/cr_usbtap_test.c -lm
    run "USB recording is a tap: the DAC's words bit-identical in five USB states (Output, USB Level, MASTER, one core and two)" "$OUT/rt_cr_usbtap_test"
    par_build $CC -O2 -w -Ibuild/gen -Ifirmware/src -o "$OUT/fm6_test" tests/fm6_test.c -lm
    mkdir -p build/fm6_demo
    run "FM6 (Melodee's test): SysEx, algorithms, patches, pitch, levels, envelopes, modulation, DC, macros, voices, cost, demos" "$OUT/fm6_test" build/fm6_demo
else                                                 # (not reached: the build's outputs are checked above)
    missing "no build/gen/felucca_tables.h: the engine and format tests did not run (run ./build.sh)"
fi

run "regression: target cost of the render loops (pi32v2 disassembly)" python3 tests/target_budget.py \
    build/choralroot.dis tests/target_budget.txt

run "installer CLI (fm1_install.py) against a simulated FM-1" python3 tests/install_test.py

run "FM-1 Transporter stand-ins (tools/transporter: fm1_ota, fm1fw): the review gate on synthetic packages and this build" \
    python3 tests/transporter_shim_test.py

if command -v node >/dev/null 2>&1; then
    run "web pages: editor protocol + samples, package builder, update protocol" node web/test_web.mjs
    run "web backup: capture, validation before writes, restore order" node web/test_backup.mjs
    run "web installer: the page against a simulated FM-1 (node, no browser)" node web/test_installer.mjs
    run "web SMK-37 test installer: the page against simulated SMK-37s (node, no browser)" node web/test_smk_installer.mjs
    run "web sounds: slots as files, bindings, .syx" node web/test_sounds.mjs
    run "web device manager: pools, place / keep / swap / undo, loops, packs (simulated FM-1)" node web/test_manager.mjs
else
    skip "web tests: editor, backup, installer, SMK-37 installer, sounds, device manager (no node)"
fi

run "releases: web/releases.json (the versions the installer offers) against docs/releases and the tags" \
    python3 tests/releases_test.py
# the other versions' packages: CR_PACKAGES_DIR, else build/scratch/packages (python3 tools/fetch_packages.py); a
# version whose package is not there is reported as "skipped" by the test and left out of the local site
run "site: make_site.py into build/site, the versions' packages, every internal link resolves" python3 tests/site_test.py

fi
report
par_pids=
ended=1                                              # (the verdict is the EXIT trap's: verdict, above)
