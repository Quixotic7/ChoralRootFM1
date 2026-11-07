#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The whole verification list of docs/HANDOFF.md, side by side where the suites do not share files:
#   sh tests/run_all.sh [--only name[,name...]] [--list]
#   CR_VERSION=X.Y sh tests/run_all.sh      the browser emulator built as that version: CR_VERSION goes to
#                                           tools/emu/web/build_web.sh alone, as in the documented list (the other
#                                           suites run without it: test_cr.sh checks the version against
#                                           choralroot.c only then)
# Nothing is left out and there is no quick mode: every suite is the documented command, run whole. Each suite's
# output goes to build/scratch/logs/NAME.log; one line per suite (PASS / FAIL, seconds), then the lines to read
# (the stub's, the size line, the goldens, the lint, test_cr's ok count), then the verdict. Exit 0 only when every
# suite that ran exited 0.
#
# The suites (name: command), in the order of the summary:
#   build          ./build.sh                           (Docker; first and alone: everything reads build/)
#   cr_tests       sh tests/run_cr_tests.sh
#   cr_draw        sh tests/run_cr_draw.sh
#   emu_build      sh tools/emu/build.sh
#   test_cr        sh tools/emu/test_cr.sh              (after emu_build)
#   test_persist   sh tools/emu/test_persist.sh         (after emu_build)
#   test_retired   sh tools/emu/test_retired.sh         (after emu_build)
#   run_tests      sh tests/run_tests.sh                (after web_emu_build)
#   install        python3 tests/install_test.py
#   releases       python3 tests/releases_test.py
#   layout_gen     python3 tests/gen_layout_test.py     (the layout generator: P5's panel, the broken designs)
#   web_backup     node web/test_backup.mjs
#   web_manager    node web/test_manager.mjs
#   web_sounds     node web/test_sounds.mjs
#   web_dnsnd      node web/test_dnsnd.mjs              (after cr_tests: build/host/sound_templates)
#   web_dn2        node web/test_dn2.mjs                (after cr_tests: build/host/sound_templates; the Digitone II importer)
#   web_installer  node web/test_installer.mjs
#   web_web        node web/test_web.mjs                (once run_tests has written build/host/desc.json)
#   web_emu_build  sh tools/emu/web/build_web.sh        (Emscripten: ~/emsdk)
#   layout_build   sh tools/layout/build_layout.sh      (Emscripten: the layout tool's preview module, docs/LAYOUT-TOOL.md)
#   layout_test    node web/test_layout.mjs             (after layout_build and web_emu_build)
#   web_emu_smk    node tools/emu/web/test_web_emu_smk.mjs (after web_emu_build: the SMK-37 emulator, build/emu-web-smk37;
#                                                        it measures no real time, so it runs beside the others)
#   web_emu_test   node tools/emu/web/test_web_emu.mjs  (last and alone: it times the audio render against real time)
# What may not run at the same time, and why the rest may:
#   - test_cr, test_persist and test_retired share build/emu/test but not a file in it (cr_*, persist_* / gsave_*,
#     retired_* / physcut_*: test_cr.sh removes cr_* only);
#   - run_cr_tests.sh and run_tests.sh share build/host but not a program (run_tests.sh's builds of the four USB
#     tests are rt_cr_usb*), and each has its own step directory (build/host/jobs_cr, build/host/jobs);
#   - run_tests.sh runs install_test.py and the five web tests itself: they keep their files in temporary
#     directories of their own, so the same test may run twice at once;
#   - web/test_web.mjs compares the editor's mock with build/host/desc.json and skips that check when the file is
#     missing: here it waits until run_tests.sh has written this run's (or has ended);
#   - the emulator's tests need build/host/emu: they wait for emu_build;
#   - the browser emulator's test needs build/host/emu and build/emu-web, and one of its checks is the longest
#     128-frame render in wall-clock time (under 4 budgets of 2.9 ms): a single preemption by another suite fails it
#     (seen: 20 ms beside the others, 0.3 ms alone), so it runs when every other suite has ended;
#   - run_tests.sh's site test (tests/site_test.py) puts build/emu-web into the site and checks it when it is there
#     (and says "skip: build/emu-web missing" when not): run_tests waits for web_emu_build, so the site is checked
#     with the emulator in it and never while the files are being copied; the same for build/layout-web (the site's
#     webapp/layout/): run_tests waits for layout_build too;
#   - the layout tool's test compares its frames with the browser emulator's (build/emu-web/choralroot.wasm) and
#     skips that when the file is missing: it waits for web_emu_build. Its files are its own (build/layout-web-test,
#     a Chromium profile named by its process), it measures no real time, so it runs beside the others.
# Several of the tests measure real time against simulated devices (install_test.py, the web tests), so the two
# suites that compile and compute (run_tests, cr_tests) get half the cores each here unless PAR_JOBS says otherwise.
# A suite whose prerequisite failed still runs (as the documented command would) and fails by itself.
# --only: the named suites, in this order, with the waits among them; a prerequisite that is not named is not run
# (build first by hand, or name it).
set -u
cd "$(dirname "$0")/.." || exit 1
export DYLD_FALLBACK_LIBRARY_PATH="${DYLD_FALLBACK_LIBRARY_PATH:-/opt/homebrew/lib}"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
ALL="build cr_tests cr_draw emu_build test_cr test_persist test_retired run_tests install releases layout_gen web_backup web_manager web_sounds web_dnsnd web_dn2 web_installer web_web web_emu_build layout_build layout_test web_emu_smk web_emu_test"
only=
while [ $# -gt 0 ]; do
    case $1 in
        --only) [ $# -ge 2 ] || { echo "run_all.sh: --only needs names" >&2; exit 2; }; only="$only,$2"; shift ;;
        --only=*) only="$only,${1#--only=}" ;;
        --list) for s in $ALL; do echo "$s"; done; exit 0 ;;
        *) echo "usage: sh tests/run_all.sh [--only name[,name...]] [--list]" >&2; exit 2 ;;
    esac
    shift
done
sel=
if [ -n "$only" ]; then
    for s in $(printf '%s' "$only" | tr ',' ' '); do
        case " $ALL " in *" $s "*) ;; *) echo "run_all.sh: no suite '$s' (--list names them)" >&2; exit 2 ;; esac
    done
    for s in $ALL; do
        case ",$only," in *",$s,"*) sel="$sel $s" ;; esac
    done
else
    sel=$ALL
fi
selected() { case " $sel " in *" $1 "*) return 0 ;; esac; return 1; }

L=build/scratch/logs
mkdir -p "$L"
for s in $ALL; do rm -f "$L/$s.log" "$L/$s.rc" "$L/$s.rc.tmp" "$L/$s.secs"; done
rm -f "$L/start"
: > "$L/start"
now() { python3 -c 'import time; print("%.2f" % time.time())'; }

command_of() {
    case $1 in
        build) echo "./build.sh" ;;
        cr_tests) echo "sh tests/run_cr_tests.sh" ;;
        cr_draw) echo "sh tests/run_cr_draw.sh" ;;
        emu_build) echo "sh tools/emu/build.sh" ;;
        test_cr) echo "sh tools/emu/test_cr.sh" ;;
        test_persist) echo "sh tools/emu/test_persist.sh" ;;
        test_retired) echo "sh tools/emu/test_retired.sh" ;;
        run_tests) echo "sh tests/run_tests.sh" ;;
        install) echo "python3 tests/install_test.py" ;;
        releases) echo "python3 tests/releases_test.py" ;;
        layout_gen) echo "python3 tests/gen_layout_test.py" ;;
        web_backup) echo "node web/test_backup.mjs" ;;
        web_manager) echo "node web/test_manager.mjs" ;;
        web_sounds) echo "node web/test_sounds.mjs" ;;
        web_dnsnd) echo "node web/test_dnsnd.mjs" ;;
        web_dn2) echo "node web/test_dn2.mjs" ;;
        web_installer) echo "node web/test_installer.mjs" ;;
        web_web) echo "node web/test_web.mjs" ;;
        web_emu_build) echo "sh tools/emu/web/build_web.sh" ;;
        layout_build) echo "sh tools/layout/build_layout.sh" ;;
        layout_test) echo "node web/test_layout.mjs" ;;
        web_emu_smk) echo "node tools/emu/web/test_web_emu_smk.mjs" ;;
        web_emu_test) echo "node tools/emu/web/test_web_emu.mjs" ;;
    esac
}
waits_of() {    # the suites that must have ended before this one starts (those of them that are selected)
    case $1 in
        build) ;;
        test_cr|test_persist|test_retired) echo "build emu_build" ;;
        run_tests) echo "build web_emu_build layout_build" ;;
        layout_test) echo "build web_emu_build layout_build" ;;
        web_emu_smk) echo "build web_emu_build" ;;
        web_dnsnd|web_dn2) echo "build cr_tests" ;;
        web_emu_test) for o in $ALL; do [ "$o" = web_emu_test ] || echo "$o"; done ;;
        *) echo "build" ;;
    esac
}
ended() { [ -f "$L/$1.rc" ]; }
suite() {       # the suite's command, its output to its log, its status and seconds beside it
    s=$1
    c=
    trap '[ -z "$c" ] || kill $c 2>/dev/null; exit 143' TERM
    for w in $(waits_of "$s"); do
        selected "$w" || continue
        while ! ended "$w"; do sleep 0.1; done
    done
    if [ "$s" = web_web ] && selected run_tests; then
        while ! ended run_tests && ! [ build/host/desc.json -nt "$L/start" ]; do sleep 0.1; done
    fi
    [ "$s" = web_emu_build ] || unset CR_VERSION
    t0=$(now)
    rc=0
    $(command_of "$s") > "$L/$s.log" 2>&1 &
    c=$!
    wait $c || rc=$?
    c=
    echo "$t0 $(now)" | awk '{ printf "%.1f\n", $2 - $1 }' > "$L/$s.secs"
    echo "$rc" > "$L/$s.rc.tmp"
    mv "$L/$s.rc.tmp" "$L/$s.rc"
}

if [ -z "${PAR_JOBS:-}" ]; then
    PAR_JOBS=$(( $(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 4) / 2 ))
    [ "$PAR_JOBS" -ge 2 ] || PAR_JOBS=2
fi
export PAR_JOBS
pids=
stop() { [ -z "$pids" ] || kill $pids 2>/dev/null; exit 130; }
trap stop INT TERM
T0=$(now)
for s in $sel; do
    suite "$s" &
    pids="$pids $!"
done

# one line per suite as it ends, in the list's order
fail=0
failed=
for s in $sel; do
    while ! ended "$s"; do sleep 0.2; done
    rc=$(cat "$L/$s.rc")
    if [ "$rc" -eq 0 ]; then v=PASS; else v="FAIL (exit $rc)"; fail=1; failed="$failed $s"; fi
    printf '%-14s %-14s %6s s   %s\n' "$s" "$v" "$(cat "$L/$s.secs")" "$(command_of "$s")"
done
wait
pids=
T1=$(now)

# the lines to read
line() { [ -f "$L/$1.log" ] && grep -a -m1 -- "$2" "$L/$1.log" | sed "s/^/  $1: /"; }
echo
line build '^stub: '
line build '^size: \.text'
if [ -f "$L/run_tests.log" ]; then
    grep -a -o '[0-9]* golden renders ([0-9]* changed, [0-9]* gone)' "$L/run_tests.log" | sed 's/^/  run_tests: /'
    grep -a -o '[0-9]* lint findings' "$L/run_tests.log" | sed 's/^/  run_tests: /'
    tail -1 "$L/run_tests.log" | sed 's/^/  run_tests: /'
fi
for s in test_cr test_persist test_retired; do
    [ -f "$L/$s.log" ] || continue
    echo "  $s: $(grep -a -c '^  ok ' "$L/$s.log") ok, $(grep -a -c '^  FAIL ' "$L/$s.log") FAIL, last line: $(tail -1 "$L/$s.log")"
done
echo
for s in $failed; do
    echo "FAILED: $s ($(command_of "$s")), the end of $L/$s.log:"
    tail -15 "$L/$s.log" | sed 's/^/    /'
done
total=$(echo "$T0 $T1" | awk '{ printf "%.0f", $2 - $1 }')
if [ $fail -eq 0 ]; then
    echo "ALL SUITES PASSED ($(echo $sel | wc -w | tr -d ' ') suites, $total s; logs in $L/)"
    exit 0
fi
echo "SUITES FAILED:$failed ($total s; logs in $L/)"
exit 1
