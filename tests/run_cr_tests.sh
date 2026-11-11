#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build and run the ChoralRoot engine host tests (firmware/src/cr_engine.c), no make needed:
#   sh tests/run_cr_tests.sh
# The programs are compiled and run side by side (tests/par.sh; PAR_JOBS=1: one after another) and reported in the
# order written here, as when they ran in a row: the first one that fails (a compile or a test) is the last thing
# printed and its status the script's. A program is compiled again only when its command or one of the files it was
# compiled from has changed (tests/hostcc.sh: hashes, not dates); every test runs every time.
set -e
cd "$(dirname "$0")/.."
mkdir -p build/host
# P5's factory header follows the shipped list (assets/prophet5-factory/shipped.txt; written only when its text changes)
[ -d build/gen ] && python3 tools/gen_prophet_factory.py build/gen/melodee_prophet_factory.h >/dev/null
# the compiled layouts (tools/gen_layout.py: written only when its text changes)
[ -d build/gen ] && python3 tools/gen_layout.py build/gen/cr_layouts.h >/dev/null
. tests/par.sh
par_init build/host/jobs_cr
trap 'par_kill' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM
t() { par_run "" "$@"; }
logged() {      # logged NAME [ARGS]: the program's output to build/host/NAME.log, shown when it fails
    logged_n=$1; shift
    "./build/host/$logged_n" "$@" > "build/host/$logged_n.log" || { cat "build/host/$logged_n.log"; return 1; }
}
count_ok() { grep -c ' ok$' "build/host/$1.log" | sed "s/^/$1: /; s/\$/ checks ok/"; }
last() { tail -1 "build/host/$1.log"; }
logged_last() { logged "$@" && last "$1"; }
logged_count_last() { logged "$@" && count_ok "$1" && last "$1"; }
logged_count() { logged "$@" && count_ok "$1"; }
align() {
    ./build/host/cr_align_test > build/host/cr_align_test.log 2>&1 || { grep -a 'cr_align' build/host/cr_align_test.log; return 1; }
    grep -ac ' ok$' build/host/cr_align_test.log | sed 's/^/cr_align_test: /; s/$/ checks ok/' &&
        grep -a 'cr_align_test' build/host/cr_align_test.log | tail -1
}

# the boot guard's decision (firmware/src/cr_bootguard.h, main.c fm1_cstart)
par_build cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 -o build/host/cr_bootguard_test tests/cr_bootguard_test.c
t ./build/host/cr_bootguard_test
# the boot stub (docs/BOOT-SAFETY.md "As built"): its decision (firmware/stub/stub_core.h: marker, rescue key, staged
# loader, rescue count; the sanitizers watch its reads), and the package builder's side (the marker, the compensation
# bytes, sector 0x4000 byte-identical for two bodies). The loader's interruption model: tests/ldr_test.c (run_tests.sh)
par_build cc -std=c99 -Wall -Wextra -Wno-unused-function -Werror -pedantic -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -o build/host/cr_stub_test tests/cr_stub_test.c
t ./build/host/cr_stub_test
t python3 tests/stub_pkg_test.py
# the compiled layouts (build/gen/cr_layouts.h, firmware/src/cr_layout.h; docs/LAYOUT-TOOL.md "Step 4"): P5's rows as
# a screen plan over pages of its own, its counts, every column's built-in address, every parameter once, the screens
par_build cc -std=c11 -Wall -Wextra -Wno-unused-function -Werror -pedantic -O2 -Ibuild/gen -Ifirmware/src -o build/host/cr_layout_test tests/cr_layout_test.c
t ./build/host/cr_layout_test
par_build cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 -o build/host/cr_engine_test tests/cr_engine_test.c firmware/src/cr_engine.c
t ./build/host/cr_engine_test
par_build cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 -o build/host/cr_loop_test tests/cr_loop_test.c firmware/src/cr_loop.c firmware/src/cr_engine.c
t ./build/host/cr_loop_test
par_build cc -std=c11 -Wall -Wextra -Werror -pedantic -O2 -o build/host/cr_settings_test tests/cr_settings_test.c firmware/src/cr_settings.c firmware/src/cr_engine.c
t ./build/host/cr_settings_test
# the favourites' store (firmware/src/cr_favs.c on storage.c and a simulated NOR): set, clear, a power cycle, a torn
# write, an unknown version, ids out of range (docs/PRESETS.md "Favourites")
par_build cc -std=c11 -Wall -Wextra -Werror -pedantic -O2 -o build/host/cr_favs_test tests/cr_favs_test.c
t ./build/host/cr_favs_test
par_build cc -O1 -Wall -Wno-unused-function -o build/host/cr_sense_test tests/cr_sense_test.c -lm
t ./build/host/cr_sense_test
par_build cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 -Ibuild/gen -o build/host/cr_va_test tests/cr_va_test.c -lm
t ./build/host/cr_va_test
# QUAD (docs/QUAD.md): blob, pages, ratios, the routings, against tests/quad_ref.py's goldens, envelopes, filters, LFOs,
# presets; an int32 wrap aborts (the sanitizer). The CPU figure: tests/run_quad_test.sh
par_build cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 -fsanitize=signed-integer-overflow -fno-sanitize-recover=all -DFELUCCA_QUAD=1 -Ibuild/gen -o build/host/cr_quad_test tests/cr_quad_test.c -lm
t ./build/host/cr_quad_test
# GRAIN's source generator (firmware/src/grain_src.c, docs/GRAIN.md step 1): the grid, slices, goldens, the sketch WAVs
par_build cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 -fsanitize=signed-integer-overflow,shift -fno-sanitize-recover=all -Ibuild/gen -o build/host/cr_grain_test tests/cr_grain_test.c -lm
t ./build/host/cr_grain_test
# the drums' voices (firmware/src/cr_drum.c: CoralFlower's morph drum synth, X Club and X Deep): each sounds, rings out and
# stops; the choke, the snare / clap crossfade, the CPU test's pattern (build/host/cr_drum_test --cpu: the host's cost)
par_build cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 -fsanitize=signed-integer-overflow,shift -fno-sanitize-recover=all -Ibuild/gen -o build/host/cr_drum_test tests/cr_drum_test.c -lm
t logged_last cr_drum_test
# FM TONE's pops (docs/QUAD.md "Pops"): every factory sound and INIT through eight scenarios (retriggers, chord changes,
# steals, arps) at C2 and C4: no jump, no hole, no operator restarted under the voice's level, no wrap (the sanitizer)
par_build cc -O2 -w -fsanitize=signed-integer-overflow -fno-sanitize-recover=all -Ibuild/gen -Ifirmware/src -o build/host/quad_pops tests/quad_pops.c -lm
t ./build/host/quad_pops check
# FM TONE's times against the Digitone's (docs/QUAD.md "Times"): quad_times.h not stale, the amp envelope's times, the
# proxy readings, the delay, the fade, through the measurement's own analysis (tools/digitone/dt_analyse.py; numpy)
par_build cc -std=c99 -O2 -w -DFELUCCA_QUAD=1 -Ibuild/gen -o build/host/quad_render tools/digitone/quad_render.c -lm
t python3 tools/digitone/test_quad_times.py
par_build cc -std=c99 -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_trans_test tests/cr_trans_test.c -lm
t ./build/host/cr_trans_test
# the bass part on the second core (firmware/src/cr_cpu1.h, docs/DUALCORE.md): the split bit for bit, the budgets, the fail-safes
par_build cc -std=gnu11 -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_cpu1_test tests/cr_cpu1_test.c -lm
t ./build/host/cr_cpu1_test
# .. the persistent fallback inert in the single-core package (CR_SINGLE_CORE 1), and the same record barring in the
# standard one (docs/DUALCORE.md "The persistent fallback")
par_build cc -std=gnu11 -O2 -w -Ibuild/gen -Ifirmware/src -Itests -DCR_SINGLE_CORE=1 -o build/host/cr_c1_bar_1core_test tests/cr_c1_bar_1core_test.c
t ./build/host/cr_c1_bar_1core_test
par_build cc -std=gnu11 -O2 -w -Ibuild/gen -Ifirmware/src -Itests -DCR_SINGLE_CORE=0 -o build/host/cr_c1_bar_std_test tests/cr_c1_bar_1core_test.c
t ./build/host/cr_c1_bar_std_test
# .. and that job on a real second thread (3y45): the inline job's samples for twelve pairs of engines (P5 on both
# parts among them), no ThreadSanitizer report, and a planted shared word reported
t sh tests/run_c1_thread_test.sh
# the firmware's memset / memcpy / memcmp, a word at a time since 3y50 (firmware/src/libc.c): the host's bytes
par_build cc -std=gnu11 -O2 -w -fno-builtin -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_libc_test tests/cr_libc_test.c
t ./build/host/cr_libc_test
# ChoralRoot's delay and reverb (firmware/src/cr_fx.c, docs/FX.md): the coefficients, tempo sync, the half rate's response
# and noise, every corner stable and ending in exact silence, smooth parameter changes, the level against Felucca's
# effects, mono, the cost; impulse responses for tools/fx_plots.py into build/fx-review
mkdir -p build/fx-review
par_build cc -std=gnu11 -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_output_test tests/cr_output_test.c -lm
t logged_count_last cr_output_test
par_build cc -std=gnu11 -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_fx_test tests/cr_fx_test.c -lm
t logged_count_last cr_fx_test build/fx-review
# 0.19: what does not pass the master lined up with it (the click, the USB recording's part pairs), the chorus's test
# switch, MASTER at power-on (docs/AUDIO-QUALITY.md "Round 3"), on the emulator's firmware build
par_build cc -std=gnu11 -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_align_test tests/cr_align_test.c -lm
t align
# backup / restore SysEx (cr_backup.c) on the emulator's firmware build (tools/emu/emu_firmware.h; macOS: os_unfair_lock)
par_build cc -std=gnu11 -O1 -w -Ibuild/gen -Ifirmware/src -Itests -DCZ_FACTORY_RAW -o build/host/cr_backup_test tests/cr_backup_test.c -lm
t ./build/host/cr_backup_test
par_build cc -std=gnu11 -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_pool_test tests/cr_pool_test.c -lm
t ./build/host/cr_pool_test
# the editor on P5's compiled rows (cr_pages.c cp_deep, cr_edit.c's screen plan; docs/LAYOUT-TOOL.md "Step 4"): the
# layout's pages (copies of the built-in descriptors), its screens (EXPR after VOICE), every cell turned (the parameter at
# its built-in address, edited, hot), empty slots inert, SELECT across lanes and screens
par_build cc -std=gnu11 -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_p5rows_test tests/cr_p5rows_test.c -lm
t logged_last cr_p5rows_test
# MIDI CC on the sounds' parameters (3y269, docs/MANUAL.md section 15): the common CCs per engine, every block CC the cell
# SELECT reaches, CCs in through the queue (coalesced, the editor's hot cell, the popup), out and the echo guard,
# Options > MIDI CC. ./build/host/cr_cc_test --map prints the manual's tables
par_build cc -std=gnu11 -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_cc_test tests/cr_cc_test.c -lm
t logged_last cr_cc_test
# a full pool's browsing: no flash read on a frame, a detent reads only the sound it loads (3y60)
par_build cc -std=gnu11 -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_poolperf_test tests/cr_poolperf_test.c -lm
t ./build/host/cr_poolperf_test
# a restart asked over SysEx (72 RESTART, 77 STUB) leaves once the host has the reply: the device's ed_service and usb.c's
# EP1 against a model of the SIE, the host and TIMER5 (the same firmware build, FELUCCA_OTA 1)
par_build cc -std=gnu11 -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_restart_test tests/cr_restart_test.c -lm
t ./build/host/cr_restart_test
# FM6 (Melodee's engine, docs/FM6.md): patches, the blob, DX7 SysEx, the bank, the deep pages, the patch store, voices
par_build cc -std=gnu11 -O1 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_fm6_test tests/cr_fm6_test.c -lm
t logged_last cr_fm6_test
# CZ-1 (Melodee's engine, docs/CZ1.md): Casio's tones, the blob, Casio SysEx, the banks, the deep pages, the tone store
par_build cc -std=gnu11 -O1 -w -Ibuild/gen -Ifirmware/src -Itests -DCZ_FACTORY_RAW -o build/host/cr_cz_test tests/cr_cz_test.c -lm
t logged_last cr_cz_test
# P5 (Melodee's Prophet-5 engine, docs/PROPHET.md): Sequential's bank against its .syx, the blob, every shipped program
# through voice.c, the four voices, UNISON (a load takes the program's), GLIDE, the bend, the deep pages, the pool, SAVE
# refused; then the remix's three optimisations bit for bit (P5_STAGE36 0 / 1 / 2 / 4 / 7), the build with INIT alone
# (FELUCCA_P5_FACTORY=0) and the shipped list (three lists: a program and a saved session are the same in each)
par_build cc -std=gnu11 -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_prophet_test tests/cr_prophet_test.c -lm
t logged_last cr_prophet_test
t sh tests/run_prophet_test.sh
# USB audio recording (Melodee's, docs/USB-AUDIO.md): usb.c's descriptors in each presentation (USB Record on, the
# console), the capture routing, the PCM packing, the ring against clock drift and the host stopping (usb_audio_stream.c)
par_build cc -std=gnu11 -O2 -w -o build/host/cr_usbaudio_test tests/cr_usbaudio_test.c
t logged_last cr_usbaudio_test
# ChoralRoot In at 48 kHz (0.19, tests/cr_usbaudio48_test.c): the two-rate descriptors against SLOOP 2.5's, the
# sampling-frequency requests, the resampler (160 / 147 exactly, bit for bit against a reference, its spectrum), the
# 48 kHz stream and a rate switch mid-stream. (44.1 kHz bit for bit: the digest in cr_usbaudio_test above)
par_build cc -std=gnu11 -O2 -w -o build/host/cr_usbaudio48_test tests/cr_usbaudio48_test.c -lm
t logged_last cr_usbaudio48_test
# Options > USB Rate (0.19, 2026-10-12: 48 kHz is opt-in; tests/cr_usbrate_test.c): at 44.1 kHz the descriptors are
# 0.18's byte for byte (tests/cr_usb018_desc.h) and SET_CUR 48000 changes nothing; the 44.1 kHz digest in both
# states; a change of the option leaves the bus once, and never under a SysEx transfer
par_build cc -std=gnu11 -O2 -w -o build/host/cr_usbrate_test tests/cr_usbrate_test.c
t logged_last cr_usbrate_test
# does the USB recording change what the instrument plays? (tests/cr_usbtap_test.c, docs/USB-AUDIO.md "Does USB
# recording affect the sound?"): the DAC's 24-bit words in five USB states (Record Off, closed, open and waiting,
# 44.1 kHz, 48 kHz) for Output Clean / Classic, USB Level, MASTER, one core and two: bit-identical
par_build cc -std=gnu11 -O2 -w -Ibuild/gen -Ifirmware/src -Itests -o build/host/cr_usbtap_test tests/cr_usbtap_test.c -lm
t logged_last cr_usbtap_test
par_build cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 -o build/host/cr_midi_test tests/cr_midi_test.c
t ./build/host/cr_midi_test
# Bluetooth MIDI (HISTORY 3y236): ChoralRoot's glue (firmware/src/cr_ble.c) with the real stack on the fake radio, and
# the stack's own fixtures as Melodee 3fe570d runs them (tests/ble_tests.py: protocol, encryption, pairing, the driver,
# the radio program's replay against the pinned Optimist interpreter)
par_build cc -std=c11 -Wall -Werror -Wno-unused-function -Wno-bitwise-op-parentheses -O1 -Itests/ble_fake -Ifirmware/src \
    -o build/host/cr_ble_test tests/cr_ble_test.c
t logged_count_last cr_ble_test
par_build cc -std=c11 -Wall -Werror -Wno-unused-function -Wno-bitwise-op-parentheses -O1 -DBLE_DIAG=1 -Itests/ble_fake \
    -Ifirmware/src -o build/host/cr_ble_diag_test tests/cr_ble_test.c
t logged_count_last cr_ble_diag_test
t python3 tests/ble_tests.py
# the sound templates the clients embed for .syx imports are the firmware's defaults (docs/SOUNDS.md), and their factory
# tables the firmware's. Built with P5, as the unit is (3y80, the device manager's P5 tab; until then without it): the
# clients hold P5's shipped programs (INIT P5, then assets/prophet5-factory/shipped.txt's list) and their Sequential
# numbers, the favourites' keys; when the list changes this check says what to paste
par_build cc -std=gnu11 -O1 -w -Ibuild/gen -Ifirmware/src -Itests -DCZ_FACTORY_RAW -o build/host/sound_templates tests/sound_templates.c -lm
t ./build/host/sound_templates --check web/fm1sounds.js tools/fm1_install.py

for id in $par_order; do
    rc=0
    par_wait "$id" || rc=$?
    cat "$PAR_DIR/$id.log"
    [ "$rc" -eq 0 ] || exit "$rc"
done
par_pids=
