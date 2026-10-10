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
lm() { sed -n 's/.* mode \([0-9]*\) keys \([0-9]*\) .*/\1 \2/p' "$1" | tail -1; }
[ "$(lm "$OUT/persist_1.log")" = "3 1" ] && [ "$(lm "$OUT/persist_2.log")" = "3 1" ] &&
    ok "the PLAY menu's record mode (Replace) and keys (Loops) after a relaunch (settings v8 loop_rec)" \
    || bad "record mode / keys: run 1 '$(lm "$OUT/persist_1.log")', run 2 '$(lm "$OUT/persist_2.log")'"
grep -q "record: current" "$OUT/persist_2.log" && ok "run 2: the record was read (current)" || bad "run 2: record not read"
[ -s "$OUT/persist_bpm.ppm" ] && ok "run 2: the BPM meter: $OUT/persist_bpm.ppm" || bad "run 2: no screenshot"

"$EMU" --headless --script "$S/persist_fresh.txt" >"$OUT/persist_3.log" 2>&1 || bad "run 3: exit status"
b=$(bpm "$OUT/persist_3.log")
[ "$b" = 120 ] && ok "run 3: no --flash: a fresh 120 BPM" || bad "run 3: bpm $b"
[ "$(st "$OUT/persist_3.log")" = 0 ] && ok "run 3: Play Style Simple" || bad "run 3: style $(st "$OUT/persist_3.log")"

# the VA patch store (firmware/src/va_store.c, docs/VA.md): run 1 edits a VA sound's patch on its deep page and saves
# it to U01; run 2 (the same file) powers on with U01 (the record's chord sound): the patch's CRC as saved
VFLASH=$OUT/persist_va_flash.bin
rm -f "$VFLASH" "$OUT"/persist_va_1.log "$OUT"/persist_va_2.log
"$EMU" --headless --flash "$VFLASH" --script "$S/va_persist_set.txt" >"$OUT/persist_va_1.log" 2>&1 || bad "VA run 1: exit status"
"$EMU" --headless --flash "$VFLASH" --script "$S/va_persist_check.txt" >"$OUT/persist_va_2.log" 2>&1 || bad "VA run 2: exit status"
vs=$(sed -n 's/^va: save slot 1 .*patch crc \([0-9a-f]*\)$/\1/p' "$OUT/persist_va_1.log" | tail -1)
vl=$(sed -n 's/^va: load slot 1 patch crc \([0-9a-f]*\)$/\1/p' "$OUT/persist_va_2.log" | tail -1)
grep -q "^deep: part 0 .* edited" "$OUT/persist_va_1.log" && ok "VA run 1: the patch edited on a deep page" || bad "VA run 1: no deep edit"
[ -n "$vs" ] && [ "$vs" = "$vl" ] && ok "VA run 2: U01's patch after a relaunch (crc $vl)" || bad "VA: patch crc saved '${vs}' loaded '${vl}'"

# FM6 (docs/FM6.md): run 1 edits TINE EP (OP 2's LEVEL, the BEND range) and saves it in U01: the blob into the FM6
# patch store (fm6_ustore.c); run 2 (the same file) powers on with U01: the same blob back, OP 2's LEVEL 0
FFLASH=$OUT/persist_fm6_flash.bin
rm -f "$FFLASH" "$OUT"/persist_fm6_1.log "$OUT"/persist_fm6_2.log
"$EMU" --headless --flash "$FFLASH" --script "$S/fm6_persist_set.txt" >"$OUT/persist_fm6_1.log" 2>&1 || bad "FM6 run 1: exit status"
"$EMU" --headless --flash "$FFLASH" --script "$S/fm6_persist_check.txt" >"$OUT/persist_fm6_2.log" 2>&1 || bad "FM6 run 2: exit status"
fs=$(sed -n 's/^fm6: save slot 1 .*patch crc \([0-9a-f]*\)$/\1/p' "$OUT/persist_fm6_1.log" | tail -1)
fl=$(sed -n 's/^fm6: load slot 1 patch crc \([0-9a-f]*\)$/\1/p' "$OUT/persist_fm6_2.log" | tail -1)
grep -q "^deep: part 0 page 1 OP 2 col 0 LEVEL [0-9]* -> 0 " "$OUT/persist_fm6_1.log" &&
    grep -q "^deep: part 0 page 35 FUNC col 0 BEND 3 -> 7 " "$OUT/persist_fm6_1.log" &&
    ok "FM6 run 1: OP 2's LEVEL and the BEND range edited on the deep pages" || bad "FM6 run 1: the deep edits"
[ -n "$fs" ] && [ "$fs" = "$fl" ] && ok "FM6 run 2: U01's patch and functions after a relaunch (crc $fl)" || bad "FM6: patch crc saved '${fs}' loaded '${fl}'"
grep -q "^deep: part 0 page 1 OP 2 col 0 LEVEL 0 -> " "$OUT/persist_fm6_2.log" && grep -q '^expect sound .*: ok' "$OUT/persist_fm6_2.log" &&
    ok "FM6 run 2: OP 2's LEVEL is 0 as saved, the sound plays" || bad "FM6 run 2: $(grep -m1 '^deep:' "$OUT/persist_fm6_2.log")"
# the preset model (docs/PRESETS.md): run 1's SAVE on the edited TINE EP was an Overwrite, a record bound to FM6 01 in
# U01; run 2: the record's binding survived the relaunch (FM6 01 is U01, with the mark), the chord sound came back
grep -q '^save: part 0 overwrite slot U01 FM6 01 TINE EP rc 0' "$OUT/persist_fm6_1.log" &&
    grep -q '^popup: part 0 01 / TINE EP (mark) / FM6 · 01/26$' "$OUT/persist_fm6_2.log" &&
    ok "a bound preset after a relaunch: U01 overwrites FM6 01 TINE EP (the mark), the power-on sound" \
    || bad "bound preset: $(grep '^save: part 0' "$OUT/persist_fm6_1.log") / $(grep '^popup:' "$OUT/persist_fm6_2.log" | tail -1)"

# CZ-1 (docs/CZ1.md): run 1 edits CZ BRASS 1 (DCW 1's L1) and saves it in U01: the tone into the CZ-1 tone store
# (cz_ustore.c); run 2 (the same file) powers on with U01: the same tone back, DCW 1's L1 0
ZFLASH=$OUT/persist_cz_flash.bin
rm -f "$ZFLASH" "$OUT"/persist_cz_1.log "$OUT"/persist_cz_2.log
"$EMU" --headless --flash "$ZFLASH" --script "$S/cz_persist_set.txt" >"$OUT/persist_cz_1.log" 2>&1 || bad "CZ-1 run 1: exit status"
"$EMU" --headless --flash "$ZFLASH" --script "$S/cz_persist_check.txt" >"$OUT/persist_cz_2.log" 2>&1 || bad "CZ-1 run 2: exit status"
zs=$(sed -n 's/^cz: save slot 1 .*tone crc \([0-9a-f]*\)$/\1/p' "$OUT/persist_cz_1.log" | tail -1)
zl=$(sed -n 's/^cz: load slot 1 tone crc \([0-9a-f]*\)$/\1/p' "$OUT/persist_cz_2.log" | tail -1)
grep -q "^deep: part 0 page 11 DCW 1+ col 0 L1 [0-9]* -> 0 " "$OUT/persist_cz_1.log" &&
    ok "CZ-1 run 1: DCW 1's L1 edited on the deep pages" || bad "CZ-1 run 1: the deep edit"
[ -n "$zs" ] && [ "$zs" = "$zl" ] && ok "CZ-1 run 2: U01's tone after a relaunch (crc $zl)" || bad "CZ-1: tone crc saved '${zs}' loaded '${zl}'"
grep -q "^deep: part 0 page 11 DCW 1+ col 0 L1 0 -> " "$OUT/persist_cz_2.log" && grep -q '^expect sound .*: ok' "$OUT/persist_cz_2.log" &&
    ok "CZ-1 run 2: DCW 1's L1 is 0 as saved, the sound plays" || bad "CZ-1 run 2: $(grep -m1 '^deep:' "$OUT/persist_cz_2.log")"

# FM TONE (engine 15, code name QUAD; docs/QUAD.md): run 1 edits FM TONE's EP (Ratio A, KNOB 3 +5 on SYN 1) and saves it in U01: the patch into the QUAD
# patch store (quad_store.c, 0xB2000 / 0xB3000); run 2 (the same file) powers on with U01: the same patch back
QFLASH=$OUT/persist_quad_flash.bin
rm -f "$QFLASH" "$OUT"/persist_quad_1.log "$OUT"/persist_quad_2.log
"$EMU" --headless --flash "$QFLASH" --script "$S/quad_persist_set.txt" >"$OUT/persist_quad_1.log" 2>&1 || bad "FM TONE run 1: exit status"
"$EMU" --headless --flash "$QFLASH" --script "$S/quad_persist_check.txt" >"$OUT/persist_quad_2.log" 2>&1 || bad "FM TONE run 2: exit status"
qs=$(sed -n 's/^quad: save slot 1 .*patch crc \([0-9a-f]*\)$/\1/p' "$OUT/persist_quad_1.log" | tail -1)
ql=$(sed -n 's/^quad: load slot 1 patch crc \([0-9a-f]*\)$/\1/p' "$OUT/persist_quad_2.log" | tail -1)
grep -q "^deep: part 0 page 0 SYN 1 col 2 RATIO A 3 -> 8 .* edited" "$OUT/persist_quad_1.log" &&
    ok "FM TONE run 1: Ratio A edited on SYN 1 (3 -> 8, one step a detent)" || bad "FM TONE run 1: $(grep -m1 '^deep:' "$OUT/persist_quad_1.log")"
[ -n "$qs" ] && [ "$qs" = "$ql" ] && ok "FM TONE run 2: U01's patch after a relaunch (crc $ql)" || bad "FM TONE: patch crc saved '${qs}' loaded '${ql}'"
grep -q "^deep: part 0 page 0 SYN 1 col 2 RATIO A 8 -> 9 " "$OUT/persist_quad_2.log" && grep -q '^expect sound .*: ok' "$OUT/persist_quad_2.log" &&
    ok "FM TONE run 2: Ratio A is 8 as saved, the sound plays" || bad "FM TONE run 2: $(grep -m1 '^deep:' "$OUT/persist_quad_2.log")"

# the engine picker's roots (Settings pick_roots, docs/SETTINGS.md): run 1 turns them to "play" in the picker (KNOB 4);
# run 2 (the same file) opens the picker with them playing
RFLASH=$OUT/persist_roots_flash.bin
rm -f "$RFLASH" "$OUT"/persist_roots_1.log "$OUT"/persist_roots_2.log "$OUT"/persist_roots.ppm
"$EMU" --headless --flash "$RFLASH" --script "$S/persist_roots_set.txt" >"$OUT/persist_roots_1.log" 2>&1 || bad "roots run 1: exit status"
"$EMU" --headless --flash "$RFLASH" --script "$S/persist_roots_check.txt" >"$OUT/persist_roots_2.log" 2>&1 || bad "roots run 2: exit status"
grep -q '^picker: roots play' "$OUT/persist_roots_1.log" && grep -q "settings saves 1 " "$OUT/persist_roots_1.log" &&
    ok "roots run 1: KNOB 4 in the picker: the roots play, saved" || bad "roots run 1: not set / not saved"
grep -q '^picker: open part 0 .* roots play' "$OUT/persist_roots_2.log" && ! grep -q '^engine: ' "$OUT/persist_roots_2.log" &&
    grep -q '^expect sound .*: ok' "$OUT/persist_roots_2.log" &&
    ok "roots run 2: the picker's roots still play after a relaunch (D4 sounded, no engine switch): $OUT/persist_roots.ppm" \
    || bad "roots run 2: $(grep '^picker: open' "$OUT/persist_roots_2.log")"

# the loop slots (docs/LOOPER.md "Slots and the record"): run 1 (tools/emu/scripts/cr_loop_slots.txt) records into
# slots 1 and 2 and only switches between them (no SAVE held): each loop is saved to its slot as it is left (or at the
# stop that cancelled a switch); run 2 (the same file) finds both: slot 1 with its three overdubs (5 events, 4 layers),
# slot 2's take
LFLASH=$OUT/persist_loop_flash.bin
rm -f "$LFLASH" "$OUT"/persist_loop_1.log "$OUT"/persist_loop_2.log
"$EMU" --headless --flash "$LFLASH" --script "$S/cr_loop_slots.txt" >"$OUT/persist_loop_1.log" 2>&1 || bad "loop run 1: exit status"
"$EMU" --headless --flash "$LFLASH" --script "$S/loop_persist_check.txt" >"$OUT/persist_loop_2.log" 2>&1 || bad "loop run 2: exit status"
lp() { grep '^  loop:' "$OUT/persist_loop_2.log" | sed -n "$1p" | sed -n "s/.* $2 \([0-9A-F]*\) .*/\1/p"; }
grep -q '^loop: auto-save slot 1 .* rc 0' "$OUT/persist_loop_1.log" && ! grep -q '^loop: save slot' "$OUT/persist_loop_1.log" &&
    ok "loop run 1: slots 1 and 2 recorded, saved only by leaving them (no SAVE held)" || bad "loop run 1: no auto-save"
[ "$(lp 1 used)" = 003 ] && [ "$(lp 2 slot)" = 2 ] && [ "$(lp 2 events)" = 1 ] && [ "$(lp 3 slot)" = 1 ] &&
    [ "$(lp 3 events)" = 5 ] && [ "$(lp 3 layers)" = 4 ] && [ "$(lp 4 state)" = 2 ] && grep -q '^expect sound .*: ok' "$OUT/persist_loop_2.log" &&
    ok "loop run 2: both slots after a relaunch (used 003): slot 2's take, slot 1 with its overdubs (5 events, 4 layers), playing" \
    || bad "loop run 2: $(grep '^  loop:' "$OUT/persist_loop_2.log" | head -3 | tr '\n' ' ')"
[ $fail = 0 ] && echo PASS || echo FAIL
exit $fail
