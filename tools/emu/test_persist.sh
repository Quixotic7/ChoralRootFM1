#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Headless: settings persist across emulator runs through the file-backed flash (docs/SETTINGS.md).
#   run 1 (a fresh flash file): SELECT +17 -> 137 BPM, saved 1.5 s later, quit
#   run 2 (the same file): the dump says bpm 137; the BPM meter (SELECT +1 -1) is screenshotted:
#         build/emu/test/persist_bpm.ppm
#   run 1 also sets Play Style Advanced (Options); run 2: the engine plays Advanced (loaded by cr_ui_init)
#   run 3 (no --flash): 120 BPM (headless runs use a fresh flash unless --flash is given)
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
EMU=build/host/emu
OUT=build/emu/test
S=tools/emu/scripts
FLASH=$OUT/persist_flash.bin
fail=0
ok()  { echo "  ok    $*"; }
bad() { echo "  FAIL  $*"; fail=1; }
if [ ! -x "$EMU" ] || [ -n "$(find tools/emu firmware/src tests/hostsim.c -newer "$EMU" -name '*.[ch]' 2>/dev/null | head -1)" ]; then
    sh tools/emu/build.sh || { echo "build failed"; exit 1; }
fi
mkdir -p "$OUT"
rm -f "$FLASH" "$OUT"/persist_1.log "$OUT"/persist_2.log "$OUT"/persist_3.log "$OUT"/persist_bpm.ppm "$OUT"/persist_bpm.png
bpm() { sed -n 's/.* bpm \([0-9][0-9]*\) .*/\1/p' "$1" | tail -1; }

"$EMU" --headless --flash "$FLASH" --script "$S/persist_set.txt" >"$OUT/persist_1.log" 2>&1 || bad "run 1: exit status"
b=$(bpm "$OUT/persist_1.log")
[ "$b" = 137 ] && ok "run 1: tempo set to 137" || bad "run 1: bpm $b"
grep -q "settings saves 1 " "$OUT/persist_1.log" && ok "run 1: the settings were saved once" || bad "run 1: no save"
size=$(wc -c <"$FLASH" 2>/dev/null | tr -d ' ')
[ "$size" = 1048576 ] && ok "flash file: 1 MiB" || bad "flash file: ${size:-missing}"

"$EMU" --headless --flash "$FLASH" --script "$S/persist_check.txt" >"$OUT/persist_2.log" 2>&1 || bad "run 2: exit status"
b=$(bpm "$OUT/persist_2.log")
[ "$b" = 137 ] && ok "run 2: tempo 137 after a relaunch" || bad "run 2: bpm $b"
st() { sed -n 's/.* style \([0-9]*\) .*/\1/p' "$1" | tail -1; }
[ "$(st "$OUT/persist_1.log")" = 1 ] && ok "run 1: Play Style set to Advanced" || bad "run 1: style $(st "$OUT/persist_1.log")"
[ "$(st "$OUT/persist_2.log")" = 1 ] && ok "run 2: Play Style Advanced after a relaunch (cr_ui_init -> cr_settings_load: the engine has it)" \
    || bad "run 2: style $(st "$OUT/persist_2.log")"
grep -q "record: current" "$OUT/persist_2.log" && ok "run 2: the record was read (current)" || bad "run 2: record not read"
[ -s "$OUT/persist_bpm.ppm" ] && ok "run 2: the BPM meter: $OUT/persist_bpm.ppm" || bad "run 2: no screenshot"

"$EMU" --headless --script "$S/persist_fresh.txt" >"$OUT/persist_3.log" 2>&1 || bad "run 3: exit status"
b=$(bpm "$OUT/persist_3.log")
[ "$b" = 120 ] && ok "run 3: no --flash: a fresh 120 BPM" || bad "run 3: bpm $b"
[ "$(st "$OUT/persist_3.log")" = 0 ] && ok "run 3: Play Style Simple" || bad "run 3: style $(st "$OUT/persist_3.log")"
[ $fail = 0 ] && echo PASS || echo FAIL
exit $fail
