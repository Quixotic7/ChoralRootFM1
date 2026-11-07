#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Headless: a unit that comes to 0.18 with sounds on the engines retired there (PHASE, LOFI, VOICE, TRIO, NOISE, and
# PHYS: docs/INTEGRATION.md "0.18: PHASE, LOFI, VOICE, TRIO, NOISE retired", "0.18: PHYS retired").
#   run 1 (the pre-cut build, a fresh flash file): the chord part to PHASE (EDIT held + A4), saved as U01 "ADG", then
#         parked on NOISE (OPT + PRESETS); the settings saved. The pre-cut build is v0.29.2's tree (the engines' sources
#         left this one in 3y232), taken by `git archive` into build/scratch/retired/v0.29.2 with its own generated
#         headers and emulator, its firmware with the five switched back on and PHYS with them (-DFELUCCA_PHASE=1 ..
#         -DFELUCCA_NOISE=1 -DFELUCCA_PHYS=1: the engine set 0.17 had; -DFELUCCA_GRAIN=0: 0.17 had no GRAIN, which came
#         back over its own material in 0.22): build/scratch/retired/v0.29.2/build/host/emu_precut. Its flash formats
#         are today's (the pool, 0.22), so what it writes is what a unit coming from such a build holds
#   run 2 (today's build, the same file): the unit starts and plays FM6's default (TINE EP), as a fresh unit; the PHASE
#         sound is an added preset of the VA's pool under its name, the VA's init sound when picked, with the message
#         "PHASE retired: stand-in": build/emu/test/retired_standin.ppm
#   run 3 (the pre-cut build, the same file): U01 is still the PHASE sound (nothing rewritten, nothing deleted)
# The PHYS runs (another fresh flash file, the same three steps):
#   run 1 (pre-cut): the chord part on PHYS 03 PLUCK (SysEx SELECT), saved as U01 "ADG" and left on it; the bass on, on
#         PHYS 02 MARIMBA; SAVE held: the settings and the session written with both parts on PHYS
#   run 2 (today's build): the unit starts on FM6 TINE EP, the bass part on VA DEEP SUB (the settings' sounds refused,
#         the session's too: the defaults, as a fresh unit); the PHYS sound is VA 26 "ADG", the VA's init sound when
#         picked, "PHYS retired: stand-in": build/emu/test/physcut_standin.ppm
#   run 3 (pre-cut): both parts are on PHYS again, U01 is still the PHYS sound
#   sh tools/emu/test_retired.sh       (test_cr.sh runs it)
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
EMU=build/host/emu
REF=v0.29.2                      # the pre-cut tree: a release with the retired engines' sources (and the pool's format)
OLD=build/scratch/retired/$REF
PRE=$OLD/build/host/emu_precut
OUT=build/emu/test
S=tools/emu/scripts
FLASH=$OUT/retired_flash.bin
fail=0
ok()  { echo "  ok    $*"; }
bad() { echo "  FAIL  $*"; fail=1; }
has() { LC_ALL=C grep -aq "$1" "$2"; }
if [ ! -x "$EMU" ] || [ -n "$(find tools/emu firmware/src tests/hostsim.c -newer "$EMU" -name '*.[ch]' 2>/dev/null | head -1)" ]; then
    sh tools/emu/build.sh || { echo "build failed"; exit 1; }
fi
# the pre-cut build: v0.29.2's firmware unit with the five engines and PHYS on (and without PROPHET, which 0.17 had
# not: with it the picker would show twelve engines, one more than the settings record has places for), linked with
# v0.29.2's own emulator objects (its tools/emu/build.sh); its generated headers from ITS OWN generators
if [ ! -f "$OLD/firmware/src/eng_phase.c" ]; then
    mkdir -p "$OLD" && git archive "$REF" | tar -x -C "$OLD" || { echo "$REF: git archive failed"; exit 1; }
fi
export DYLD_FALLBACK_LIBRARY_PATH="${DYLD_FALLBACK_LIBRARY_PATH:-/opt/homebrew/lib}"   # (Pillow's Raqm; /bin/sh drops it)
if [ ! -d "$OLD/build/gen" ]; then
    (cd "$OLD" && python3 -c 'import sys; sys.path.insert(0, "tools"); import build; build.generate()') \
        >/dev/null || { echo "$REF: the generated headers failed"; exit 1; }
fi
[ -x "$OLD/build/host/emu" ] || sh "$OLD/tools/emu/build.sh" >/dev/null 2>&1 || { echo "$REF's emulator: build failed"; exit 1; }
if [ ! -x "$PRE" ] || [ "$0" -nt "$PRE" ]; then
    SDL_LIBS=$(/opt/homebrew/bin/sdl2-config --libs 2>/dev/null || echo "-L/opt/homebrew/lib -lSDL2")
    O=$OLD/build/host/emu_obj
    (cd "$OLD" && ${CC:-clang} -O2 -w -Ibuild/gen -Ifirmware/src '-DFELUCCA_VERSION="ChoralRoot 0.17"' -DFELUCCA_GRAIN=0 \
        -DFELUCCA_PHASE=1 -DFELUCCA_LOFI=1 -DFELUCCA_VOICE=1 -DFELUCCA_TRIO=1 -DFELUCCA_NOISE=1 -DFELUCCA_PHYS=1 \
        -DFELUCCA_PROPHET=0 -c tools/emu/emu_fw.c -o build/host/emu_obj/emu_fw_precut.o) &&
    ${CC:-clang} -o "$PRE" "$O/emu.o" "$O/keymap.o" "$O/emu_img.o" "$O/emu_midi.o" "$O/emu_fw_precut.o" \
        $SDL_LIBS -lz -lm -framework CoreMIDI -framework CoreFoundation || { echo "pre-cut build failed"; exit 1; }
fi
mkdir -p "$OUT"
rm -f "$FLASH" "$OUT"/retired_1.log "$OUT"/retired_2.log "$OUT"/retired_3.log "$OUT"/retired_standin.ppm "$OUT"/retired_2.wav
part0() { LC_ALL=C sed -n 's/^.*part 0: \([^,]*\).*$/\1/p' "$1" | tail -1; }

echo "retired engines: a 0.17 unit's flash on 0.18"
EMU_UI_LOG=1 "$PRE" --headless --flash "$FLASH" --script "$S/retired_set.txt" >"$OUT/retired_1.log" 2>&1 || bad "run 1: exit status"
has '^engine: part 0 -> PHASE' "$OUT/retired_1.log" && has '^save: part 0 slot U001 name ADG' "$OUT/retired_1.log" &&
    ok "run 1 (pre-cut): the chord part on PHASE, saved as U01 ADG" || bad "run 1: no PHASE sound saved"
has 'part 0: NOISE /' "$OUT/retired_1.log" && has 'settings saves [1-9]' "$OUT/retired_1.log" &&
    ok "run 1 (pre-cut): the chord part parked on NOISE ($(part0 "$OUT/retired_1.log")), the settings saved" ||
    bad "run 1: not parked on NOISE: $(part0 "$OUT/retired_1.log")"

EMU_UI_LOG=1 "$EMU" --headless --flash "$FLASH" --script "$S/retired_check.txt" --wav "$OUT/retired_2.wav" >"$OUT/retired_2.log" 2>&1 ||
    bad "run 2: exit status"
has 'record: current' "$OUT/retired_2.log" && ok "run 2: the unit starts, its settings record read" || bad "run 2: record not read"
LC_ALL=C grep -a 'part 0: ' "$OUT/retired_2.log" | head -1 | grep -q 'part 0: FM6 / TINE EP' &&
    ok "run 2: the chord part parked on NOISE comes up on FM6 TINE EP, as a fresh unit" ||
    bad "run 2: the chord part: $(LC_ALL=C grep -a 'part 0: ' "$OUT/retired_2.log" | head -1)"
has '^load: slot 1 engine 2 (PHASE) retired -> INIT on VA, its ATK, REL' "$OUT/retired_2.log" &&
    has '^sound: part 0 pos 26 ADG' "$OUT/retired_2.log" &&
    ok "run 2: VA 26 is the PHASE sound ADG: the VA's init sound, a stand-in (its ATK, REL; the sends are the part's since 0.18)" ||
    bad "run 2: no stand-in: $(LC_ALL=C grep -a '^load:\|^sound: part 0' "$OUT/retired_2.log" | tail -2 | tr '\n' ' ')"
has '^popup: part 0 26 / ADG / VA ' "$OUT/retired_2.log" && LC_ALL=C grep -a 'part 0: ' "$OUT/retired_2.log" | tail -1 | grep -q 'part 0: VA /' &&
    [ -s "$OUT/retired_standin.ppm" ] &&
    ok "run 2: it plays on the VA under its own name (the popup: ADG, VA 26/26): $OUT/retired_standin.ppm" || bad "run 2: $(part0 "$OUT/retired_2.log")"
n=$(LC_ALL=C sed -n 's/.*non-zero samples \([0-9]*\).*/\1/p' "$OUT/retired_2.log" | tail -1)
[ "${n:-0}" -gt 1000 ] && ok "run 2: both chords sounded ($n non-zero samples)" || bad "run 2: silent (${n:-?} non-zero samples)"
b=$(LC_ALL=C grep -a 'parts busy' "$OUT/retired_2.log" | tail -1 | sed -n 's/.*parts busy \([0-9]*\).*/\1/p')
[ "${b:-x}" = 0 ] && ok "run 2: silent 2 s after the release" || bad "run 2: ${b:-?} voices still sounding"

EMU_UI_LOG=1 "$PRE" --headless --flash "$FLASH" --script "$S/retired_back.txt" >"$OUT/retired_3.log" 2>&1 || bad "run 3: exit status"
has '^sound: part 0 pos 7 ADG' "$OUT/retired_3.log" && has '^popup: part 0 07 / ADG / PHASE ' "$OUT/retired_3.log" && has 'part 0: PHASE /' "$OUT/retired_3.log" &&
    ok "run 3 (pre-cut): U01 is still the PHASE sound (PHASE 07 ADG): nothing rewritten, nothing deleted" ||
    bad "run 3: $(part0 "$OUT/retired_3.log")"

# ---- PHYS (retired after the five): both parts parked on it, a user sound on it, the session with both
PF=$OUT/physcut_flash.bin
rm -f "$PF" "$OUT"/physcut_1.log "$OUT"/physcut_2.log "$OUT"/physcut_3.log "$OUT"/physcut_standin.ppm "$OUT"/physcut_2.wav
part1() { LC_ALL=C sed -n 's/^.*part 1: \([^,]*\).*$/\1/p' "$1" | tail -1; }
echo "retired engines: a 0.17 unit's flash with both parts on PHYS on 0.18"
EMU_UI_LOG=1 "$PRE" --headless --flash "$PF" --script "$S/physcut_set.txt" >"$OUT/physcut_1.log" 2>&1 || bad "PHYS run 1: exit status"
has '^select: part 0 engine PHYS pos 3' "$OUT/physcut_1.log" && has '^save: part 0 slot U001 name ADG' "$OUT/physcut_1.log" &&
    has '^select: part 1 engine PHYS pos 2' "$OUT/physcut_1.log" &&
    ok "PHYS run 1 (pre-cut): the chord part on PHYS, saved as U01 ADG; the bass on PHYS 02" || bad "PHYS run 1: no PHYS sound saved"
has 'part 0: PHYS /' "$OUT/physcut_1.log" && has 'part 1: PHYS /' "$OUT/physcut_1.log" && has 'settings saves [1-9]' "$OUT/physcut_1.log" &&
    has 'session: saves [1-9]' "$OUT/physcut_1.log" &&
    ok "PHYS run 1 (pre-cut): both parts parked on PHYS ($(part0 "$OUT/physcut_1.log"); $(part1 "$OUT/physcut_1.log")), the settings and the session saved" ||
    bad "PHYS run 1: not parked: $(part0 "$OUT/physcut_1.log") / $(part1 "$OUT/physcut_1.log")"

sed -e 's/retired_standin/physcut_standin/' "$S/retired_check.txt" >"$OUT/physcut_check.txt"   # (the same steps as the five's run 2)
EMU_UI_LOG=1 "$EMU" --headless --flash "$PF" --script "$OUT/physcut_check.txt" --wav "$OUT/physcut_2.wav" >"$OUT/physcut_2.log" 2>&1 ||
    bad "PHYS run 2: exit status"
has 'record: current' "$OUT/physcut_2.log" && ok "PHYS run 2: the unit starts, its settings record read" || bad "PHYS run 2: record not read"
LC_ALL=C grep -a 'part 0: ' "$OUT/physcut_2.log" | head -1 | grep -q 'part 0: FM6 / TINE EP' &&
    LC_ALL=C grep -a 'part 1: ' "$OUT/physcut_2.log" | head -1 | grep -q 'part 1: VA / DEEP SUB' &&
    has '^session: applied (parts 0 0,.* bass 1,' "$OUT/physcut_2.log" &&
    ok "PHYS run 2: the chord part parked on a PHYS user sound comes up on FM6 TINE EP, the bass part parked on PHYS on VA DEEP SUB, the bass still on (the settings' and the session's PHYS sounds refused, the rest of the session applied), as a fresh unit" ||
    bad "PHYS run 2: the parts: $(LC_ALL=C grep -a 'part [01]: ' "$OUT/physcut_2.log" | head -2 | tr '\n' ' ')"
has '^load: slot 1 engine 9 (PHYS) retired -> INIT on VA, its ATK, REL' "$OUT/physcut_2.log" &&
    has '^sound: part 0 pos 26 ADG' "$OUT/physcut_2.log" &&
    ok "PHYS run 2: VA 26 is the PHYS sound ADG: the VA's init sound, a stand-in (its ATK, REL; the sends are the part's)" ||
    bad "PHYS run 2: no stand-in: $(LC_ALL=C grep -a '^load:\|^sound: part 0' "$OUT/physcut_2.log" | tail -2 | tr '\n' ' ')"
has '^popup: part 0 26 / ADG / VA ' "$OUT/physcut_2.log" && LC_ALL=C grep -a 'part 0: ' "$OUT/physcut_2.log" | tail -1 | grep -q 'part 0: VA /' &&
    [ -s "$OUT/physcut_standin.ppm" ] &&
    ok "PHYS run 2: it plays on the VA under its own name (the popup: ADG, VA 26/26): $OUT/physcut_standin.ppm" || bad "PHYS run 2: $(part0 "$OUT/physcut_2.log")"
n=$(LC_ALL=C sed -n 's/.*non-zero samples \([0-9]*\).*/\1/p' "$OUT/physcut_2.log" | tail -1)
[ "${n:-0}" -gt 1000 ] && ok "PHYS run 2: both chords sounded ($n non-zero samples)" || bad "PHYS run 2: silent (${n:-?} non-zero samples)"
b=$(LC_ALL=C grep -a 'parts busy' "$OUT/physcut_2.log" | tail -1 | sed -n 's/.*parts busy \([0-9]*\).*/\1/p')
[ "${b:-x}" = 0 ] && ok "PHYS run 2: silent 2 s after the release" || bad "PHYS run 2: ${b:-?} voices still sounding"

EMU_UI_LOG=1 "$PRE" --headless --flash "$PF" --script "$S/physcut_back.txt" >"$OUT/physcut_3.log" 2>&1 || bad "PHYS run 3: exit status"
has '^session: applied (parts 1 1,' "$OUT/physcut_3.log" && has 'part 0: PHYS /' "$OUT/physcut_3.log" &&
    has 'session chord: PHYS / ADG edited 0 kind 2 ' "$OUT/physcut_3.log" && has 'session bass: PHYS / MARIMBA ' "$OUT/physcut_3.log" &&
    LC_ALL=C grep -a 'part 1: ' "$OUT/physcut_3.log" | head -1 | grep -q 'part 1: PHYS / MARIMBA' &&
    ok "PHYS run 3 (pre-cut): both parts on PHYS again (the user sound U01 ADG, MARIMBA; the session applied): nothing rewritten, nothing deleted" ||
    bad "PHYS run 3: $(part0 "$OUT/physcut_3.log") / $(part1 "$OUT/physcut_3.log")"

[ $fail -eq 0 ] && echo "PASS" || echo "FAIL"
exit $fail
