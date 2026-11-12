#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# ChoralRoot's worst cases on the emulator, headless (tools/emu/scripts/perf_*.txt, docs/INTEGRATION.md Performance):
#   sh tools/emu/perf.sh [EMU]        (EMU: default build/host/emu, built when a source is newer)
# For each scenario: the audio block's cost (host instructions per 128-frame block and the device estimate from
# them, see emu.c), the UI frame's, the voices given up / restarted, the flash erases, and tools/emu/wavclicks.py
# on its WAV (jumps, silent holes, high-frequency bursts). Outputs in build/emu/perf/.
#   (a) a 6-note chord held 6 s on TINE EP        (b) the same on FM PAD (the heaviest FM6 sound of the bank)
#   (c) FM PAD chord + bass (VA DEEP SUB) + a playing loop + the arp at 200 BPM   (d) (c) on the SCOPE view
#   (e) (c) while KNOB 1..4 turn on an EDIT page every 30 ms
#   (f) a 6-note VA chord (ENSEMBLE STR) + the VA bass (PUNCH BASS), a chord change (docs/VA.md)
#   (g) a 6-note CZ-1 chord (CZ STRINGS 2) + the CZ-1 bass (CZ BASS), a chord change (docs/CZ1.md)
#   (h) the FX layer's knob row: a chord held, KNOB 1..4 turned every 30 ms for 2 s (Reverb), then 1 s (Delay)
#   (i) (c) on FM TONE (code name QUAD): a 6-note STRINGS chord (the heaviest FM TONE preset) + the bass + a playing loop + the arp (docs/QUAD.md)
#   (j) an 8-note VA chord (a C 13th over MIDI, ENSEMBLE STR) + the VA bass (PUNCH BASS: the bass part is MONO, its 4 MIDI
#       notes one voice): 9 voices, more than one core's shared budget (docs/DUALCORE.md)
#   (l) (c) with the delay and the reverb fully on (0.18, docs/FX.md): the chord part's sends at 127, Decay 13 s,
#       Feedback up: the heaviest sound scenario with both effects' loops running in every block (budget: both effects
#       <= 350 us a block, no block over 80 %)
#   (p) a 6-key P5 chord (01 ITS PROPHET5: the SSI filter, its envelope sweeping; four voices, the cap) + the
#       P5 bass (BASSMENT, place 85 of the shipped 115) with the default sends, a chord change (docs/PROPHET.md "Cost on the unit": the
#       unit's figures are counted from its listing, tools/p5_cost.py; these are the emulator's estimates)
#   (q) (p) with the VA bass (PUNCH BASS)
# The heavy scenarios (c, d, e, i, l) also get a "keys" line each (tools/emu/keylat.py, docs/INTEGRATION.md "Keys while
# a frame draws"): 40 taps of G5 at pseudo-random times while the chord is held, with a redraw's strip taking 2.7 ms of
# the device's clock (the emulator's worst frame: 16 ms for six strips) and 7.8 ms (2.9 times that: the upper end of the
# unit's unknown factor): the note-on's way from the key's scan to the engine, average and worst, as the firmware is
# (the keys read inside the redraw) and with the keys read between frames only (EMU_PUMP=0: as it was); and a third
# line: the longest stretch of a UI frame of the scenario itself in which no key is read (EMU_UI_GAP=1, the device
# estimate from the host's instructions), both ways.
# Every scenario runs twice: as above (the figures), then with EMU_STAGES=1 for the audio ISR's stages (firmware/src/
# cr_cpu1.h cr_dbg: the engine tick, part 0 (chord), part 1 (bass: what the second core takes over), the rest of the mix
# and the FX, the master; device us per half, avg / max).
# These are a fresh unit's: since 0.22 Options > Dual Core is On by default, so each part has its budget and P5's chord
# voices are shared out between the cores. THE EMULATOR RUNS CORE 1's JOB INLINE: its "block" is the two cores' work
# added up, one after the other. The unit's half is the tick + the LONGER of part0 (core 0's own render) and part1
# (core 1's job: the bass part and, with P5 on the chord part, core 1's share of its voices) + fx + master: the stages
# line says both. Then every scenario again with Dual Core Off ("== (x) Dual Core Off"): on a copy of a flash whose
# settings say Off (tools/emu/scripts/cr_dualcore_opt.txt on a fresh flash file, the Options path): one core's block,
# its voices, its stages and whether its WAV is the default's.
#   (k) the global save (0.18, docs/SETTINGS.md "Manual and automatic"): a chord, then a session change (the voicing) and
#       SAVE held, against the same run with an Option changed (Play Style) and the automatic settings save
#       0.17 had: the audio block's worst case (the state is captured in the main loop, at the save: never in the
#       audio ISR), the erases (each holds the ISR 45 ms = 16 blocks, into silence: a save waits for it) and the
#       flash writes of each save
#   (u) USB audio recording (docs/USB-AUDIO.md): the emulator has no USB, so not a scenario but what ChoralRoot In
#       adds to a half on top of (a)..(g): tests/cr_usbaudio_test.c --bench counts the host instructions of the
#       capture staging, the master tap and the ring copy (the audio ISR) and of the packets (TIMER5), with emu.c's
#       device ratio; the SIE register accesses are not in it. Two lines: the stream at 44.1 kHz, and what 48 kHz
#       adds (the resampler of six channels in the audio ISR, packets of 48 frames). The unit's own figure for the
#       resampler is counted from its disassembly (docs/USB-AUDIO.md "48 kHz": about 185 us a half) and read from
#       the unit (fm1_install.py --debug: ua_rs_avg_us x 4)
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
for s in a b c d e f g h i j l p q; do
    n=perf_$s
    "$EMU" --headless --script "tools/emu/scripts/$n.txt" --wav "$OUT/$n.wav" >"$OUT/$n.log" 2>&1
    echo "== ($s) $(sed -n '1s/^# tools\/emu\/perf.sh ([a-q]): //p' "tools/emu/scripts/$n.txt")"
    grep '^audio: [0-9]* blocks\|^cpu:\|^ui:' "$OUT/$n.log" | sed 's/^/   /'
    grep 'voices: given up\|erases with' "$OUT/$n.log" | tail -2 | sed 's/^ */   /'
    python3 tools/emu/wavclicks.py "$OUT/$n.wav" --from 0.4 | tail -1 | sed 's/^/   /'
    EMU_STAGES=1 "$EMU" --headless --script "tools/emu/scripts/$n.txt" >"$OUT/${n}_stages.log" 2>&1
    LC_ALL=C grep -a '^cpu: stages\|^cpu: fx parts\|^cpu: each core' "$OUT/${n}_stages.log" | sed 's/^cpu: /   /'
    case $s in c|d|e|i|l) python3 tools/emu/keylat.py --emu "$EMU" "tools/emu/scripts/$n.txt" ;; esac
done
DCON="$OUT/dualcore_off_flash.bin"            # Options > Dual Core Off, saved: the flash of the runs below (a copy each)
rm -f "$DCON"
"$EMU" --headless --flash "$DCON" --script tools/emu/scripts/cr_dualcore_opt.txt >"$OUT/dualcore_off.log" 2>&1 ||
    { echo "Dual Core Off: the Options script failed (see $OUT/dualcore_off.log)"; exit 1; }
for s in a b c d e f g h i j l p q; do
    n=perf_$s
    cp "$DCON" "$OUT/dualcore_run_flash.bin"
    "$EMU" --headless --flash "$OUT/dualcore_run_flash.bin" --script "tools/emu/scripts/$n.txt" --wav "$OUT/${n}_dc.wav" >"$OUT/${n}_dc.log" 2>&1
    echo "== ($s) Dual Core Off"
    grep '^cpu: host instructions per 128-frame block' "$OUT/${n}_dc.log" | sed 's/^/   /'
    grep 'voices: given up' "$OUT/${n}_dc.log" | tail -1 | sed 's/^ */   /'
    cp "$DCON" "$OUT/dualcore_run_flash.bin"
    EMU_STAGES=1 "$EMU" --headless --flash "$OUT/dualcore_run_flash.bin" --script "tools/emu/scripts/$n.txt" >"$OUT/${n}_dc_stages.log" 2>&1
    LC_ALL=C grep -a '^cpu: stages' "$OUT/${n}_dc_stages.log" | sed 's/^cpu: /   /'
    LC_ALL=C grep -aq '^cpu: stages .* c1 off' "$OUT/${n}_dc_stages.log" || echo "   CORE 1 RAN: this run was not one core"
    cmp -s "$OUT/$n.wav" "$OUT/${n}_dc.wav" && echo "   wav: the default's (two cores), bit for bit" ||
        echo "   wav: differs from the default's (two cores)"
done
for n in perf_k_session perf_k_settings; do
    "$EMU" --headless --script "tools/emu/scripts/$n.txt" --wav "$OUT/$n.wav" >"$OUT/$n.log" 2>&1
    echo "== (k) $(sed -n '1s/^# tools\/emu\/perf.sh (k), //p' "tools/emu/scripts/$n.txt")"
    grep '^audio: [0-9]* blocks\|^cpu:\|^ui:' "$OUT/$n.log" | sed 's/^/   /'
    grep -a 'erases with\|^  flash:\|^  session: saves' "$OUT/$n.log" | tail -3 | sed 's/^ */   /; s/ now .*//'
    python3 tools/emu/wavclicks.py "$OUT/$n.wav" --from 0.4 | tail -1 | sed 's/^/   /'
done
a=$(sed -n 's/^cpu: host instructions per 128-frame block avg [0-9]*, max \([0-9]*\) .*/\1/p' "$OUT/perf_k_session.log")
b=$(sed -n 's/^cpu: host instructions per 128-frame block avg [0-9]*, max \([0-9]*\) .*/\1/p' "$OUT/perf_k_settings.log")
echo "   (k) the worst audio block: $a host instructions with a global save, $b with an automatic settings save"
echo "== (u) USB audio recording (estimate: the emulator has no USB)"
cc -std=gnu11 -O2 -w -o build/host/cr_usbaudio_test tests/cr_usbaudio_test.c && build/host/cr_usbaudio_test --bench | sed 's/^/   /'
echo "   (the unit, counted from build/choralroot.dis at 205 instructions a us: 44.1 kHz as before; 48 kHz +185 us a half, docs/USB-AUDIO.md)"
