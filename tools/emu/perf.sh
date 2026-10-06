#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# ChoralRoot's worst cases on the emulator, headless (tools/emu/scripts/perf_*.txt, docs/INTEGRATION.md Performance):
#   sh tools/emu/perf.sh [EMU]        (EMU: default build/host/emu, built when a source is newer)
# For each scenario: the audio block's cost (host instructions per 128-frame block and the device estimate from
# them, see emu.c), the UI frame's, the voices given up / restarted, the flash erases, and tools/emu/wavclicks.py
# on its WAV (jumps, silent holes, high-frequency bursts). Outputs in build/emu/perf/.
#   (a) a 6-note chord held 6 s on TINE EP        (b) the same on FM PAD (the heaviest FM6 sound of the bank)
#   (c) FM PAD chord + bass + a playing loop + the arp at 200 BPM   (d) (c) on the SCOPE view
#   (e) (c) while KNOB 1..4 turn on an EDIT page every 30 ms
#   (f) a 6-note VA chord (ENSEMBLE STR) + the VA bass (PUNCH BASS), a chord change (docs/VA.md)
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
EMU=${1:-build/host/emu}
OUT=build/emu/perf
if [ "$EMU" = build/host/emu ] && { [ ! -x "$EMU" ] || [ -n "$(find tools/emu firmware/src tests/hostsim.c -newer "$EMU" -name '*.[ch]' 2>/dev/null | head -1)" ]; }; then
    sh tools/emu/build.sh || { echo "build failed"; exit 1; }
fi
mkdir -p "$OUT"
for s in a b c d e f; do
    n=perf_$s
    "$EMU" --headless --script "tools/emu/scripts/$n.txt" --wav "$OUT/$n.wav" >"$OUT/$n.log" 2>&1
    echo "== ($s) $(sed -n '1s/^# tools\/emu\/perf.sh ([a-f]): //p' "tools/emu/scripts/$n.txt")"
    grep '^audio: [0-9]* blocks\|^cpu:\|^ui:' "$OUT/$n.log" | sed 's/^/   /'
    grep 'voices: given up\|erases with' "$OUT/$n.log" | tail -2 | sed 's/^ */   /'
    python3 tools/emu/wavclicks.py "$OUT/$n.wav" --from 0.4 | tail -1 | sed 's/^/   /'
done
