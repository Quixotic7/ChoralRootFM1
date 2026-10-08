#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Headless acceptance of the FM-1 emulator (no window, no audio device, deterministic):
#   sh tools/emu/test.sh            builds build/host/emu when a source is newer, runs tools/emu/scripts/*
# Checks: a 300 ms press of D4 (computer key S) lights its LED and renders sound; the power-on screen is written; two runs
# give the same LCD and the same audio; then ChoralRoot's own acceptance (test_cr.sh). (scripts/sel.txt,
# knobs.txt and acceptance.txt were Felucca's UI: no longer run.)
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy     # (--headless opens neither; belt and braces)
EMU=build/host/emu
OUT=build/emu/test
S=tools/emu/scripts
fail=0
ok()  { echo "  ok    $*"; }
bad() { echo "  FAIL  $*"; fail=1; }

if [ ! -x "$EMU" ] || [ -n "$(find tools/emu firmware/src tests/hostsim.c -newer "$EMU" -name '*.[ch]' 2>/dev/null | head -1)" ]; then
    sh tools/emu/build.sh || { echo "build failed"; exit 1; }
fi
rm -rf "$OUT"
mkdir -p "$OUT"

run() {   # run NAME SCRIPT FRAMES [more options]: the log in $OUT/NAME.log, the exit status checked
    name=$1; script=$2; frames=$3; shift 3
    "$EMU" --headless --script "$script" --frames "$frames" "$@" >"$OUT/$name.log" 2>&1
    st=$?
    if [ $st -eq 0 ]; then ok "$name: exit 0 ($(grep -c '^expect .*: ok' "$OUT/$name.log") expectations met)"
    else bad "$name: exit $st (see $OUT/$name.log)"; grep 'FAILED\|script' "$OUT/$name.log" | sed 's/^/        /'; fi
}
num() { sed -n "s/.*$1 \([0-9][0-9]*\).*/\1/p" "$2" | head -1; }
same() { cmp -s "$1" "$2"; }

echo "D4 held 300 ms"
run key_a "$S/key_a.txt" 80 --shot "$OUT/key_a_end.ppm" --wav "$OUT/key_a.wav"
nz=$(num "non-silent blocks" "$OUT/key_a.log")
[ "${nz:-0}" -gt 0 ] && ok "non-silent audio blocks: $nz ($(num 'non-zero samples' "$OUT/key_a.log") non-zero samples)" \
                     || bad "no sound from the D4 press"
grep -q 'expect led D4 on .*: ok' "$OUT/key_a.log" && ok "D4's LED lit during the press" || bad "D4's LED not lit"
[ -s "$OUT/home.ppm" ] && ok "power-on screen written: $OUT/home.ppm" || bad "no power-on screenshot"
[ -s "$OUT/key_a_end.ppm" ] && ok "--shot wrote $OUT/key_a_end.ppm" || bad "--shot wrote nothing"

echo "determinism"
mkdir -p "$OUT/again"
"$EMU" --headless --script "$S/key_a.txt" --frames 80 --shot "$OUT/again/key_a_end.ppm" --wav "$OUT/again/key_a.wav" >/dev/null 2>&1
same "$OUT/key_a_end.ppm" "$OUT/again/key_a_end.ppm" && ok "same LCD on a second run" || bad "LCD differs on a second run"
same "$OUT/key_a.wav" "$OUT/again/key_a.wav" && ok "same audio on a second run" || bad "audio differs on a second run"

echo "timing (headless, render of one 128-frame block):"
grep '^audio: [0-9]* blocks' "$OUT/key_a.log" | sed 's/^/  /'
echo "ChoralRoot (tools/emu/test_cr.sh)"
sh tools/emu/test_cr.sh | sed 's/^/  /' | tee "$OUT/test_cr.txt"
tail -1 "$OUT/test_cr.txt" | grep -q PASS || fail=1
[ $fail -eq 0 ] && echo "PASS" || echo "FAIL"
exit $fail
