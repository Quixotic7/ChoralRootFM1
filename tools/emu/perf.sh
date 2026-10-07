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
#   (g) a 6-note CZ-1 chord (CZ STRINGS 2) + the CZ-1 bass (CZ BASS), a chord change (docs/CZ1.md)
#   (u) USB audio streaming both ways (docs/USB-AUDIO.md): the emulator has no USB, so not a scenario but what
#       ChoralRoot In / Out add to a half on top of (a)..(g): tests/cr_usbaudio_test.c --bench counts the host
#       instructions of the capture staging, the master tap, the ring copy and the playback mix-in (the audio ISR)
#       and of the packets (TIMER5), with emu.c's device ratio; the SIE register accesses are not in it
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
for s in a b c d e f g; do
    n=perf_$s
    "$EMU" --headless --script "tools/emu/scripts/$n.txt" --wav "$OUT/$n.wav" >"$OUT/$n.log" 2>&1
    echo "== ($s) $(sed -n '1s/^# tools\/emu\/perf.sh ([a-g]): //p' "tools/emu/scripts/$n.txt")"
    grep '^audio: [0-9]* blocks\|^cpu:\|^ui:' "$OUT/$n.log" | sed 's/^/   /'
    grep 'voices: given up\|erases with' "$OUT/$n.log" | tail -2 | sed 's/^ */   /'
    python3 tools/emu/wavclicks.py "$OUT/$n.wav" --from 0.4 | tail -1 | sed 's/^/   /'
done
echo "== (u) USB audio streaming both ways (estimate: the emulator has no USB)"
cc -std=gnu11 -O2 -w -o build/host/cr_usbaudio_test tests/cr_usbaudio_test.c && build/host/cr_usbaudio_test --bench | sed 's/^/   /'
