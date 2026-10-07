#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Headless acceptance of ChoralRoot on the FM-1 emulator (no window, no audio device, deterministic):
#   sh tools/emu/test_cr.sh        builds build/host/emu when a source is newer, runs tools/emu/scripts/cr_*.txt
# Checks (docs/INTEGRATION.md section 9, steps 1-2): MAJ + D4 sounds D major on part 0 with the D4 F#4 A4 LEDs lit
# and the chord screen; KNOB 1 +2 revoices it (A4 D5 F#5) with the voicing line; KEY tap: Key Mode, its LED,
# "Key: C", D4 alone -> Dm; KEY held: select-key, a root sets the tonic; PERF held: the Perform picker, a root
# picks the mode; PRESETS: the sound meter and another timbre; ALGORITHM: the bass on, a bass note on part 1;
# Esc: PANIC (the red screen); idle 3 s: the stripes; after every release, parts 0 and 1 silent within 2 s.
# Screenshots: build/emu/test/cr_*.ppm (and the logs).
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
EMU=build/host/emu
OUT=build/emu/test
S=tools/emu/scripts
fail=0
ok()  { echo "  ok    $*"; }
bad() { echo "  FAIL  $*"; fail=1; }

if [ ! -x "$EMU" ] || [ -n "$(find tools/emu firmware/src tests/hostsim.c -newer "$EMU" -name '*.[ch]' 2>/dev/null | head -1)" ]; then
    sh tools/emu/build.sh || { echo "build failed"; exit 1; }
fi
mkdir -p "$OUT"
rm -f "$OUT"/cr_*

run() {   # run NAME [options]: tools/emu/scripts/NAME.txt to its end, the log in $OUT/NAME.log
    name=$1; shift
    "$EMU" --headless --script "$S/$name.txt" "$@" >"$OUT/$name.log" 2>&1
    st=$?
    if [ $st -eq 0 ]; then ok "$name: exit 0 ($(grep -c '^expect .*: ok' "$OUT/$name.log") expectations met)"
    else bad "$name: exit $st (see $OUT/$name.log)"; grep 'FAILED\|script' "$OUT/$name.log" | sed 's/^/        /'; fi
}
num() { sed -n "s/.*$1 \([0-9][0-9]*\).*/\1/p" "$2" | head -1; }
has() { grep -q "$1" "$2"; }
last_busy() { grep 'parts busy' "$1" | tail -1 | sed -n 's/.*parts busy \([0-9]*\).*/\1/p'; }
silent_end() {   # the last dump of a log: parts 0 / 1 have no voice left (2 s after the release)
    b=$(last_busy "$OUT/$1.log")
    [ "${b:-x}" = 0 ] && ok "$1: every part silent 2 s after the release (no stuck note)" \
                      || bad "$1: ${b:-?} voices still sounding 2 s after the release"
}
pixel() {        # pixel FILE X Y: "R G B" of a P6 240 x 240 screenshot
    od -An -tu1 -j $((15 + ($3 * 240 + $2) * 3)) -N3 "$1" | tr -s ' ' | sed 's/^ //'
}
yellow_in() {    # yellow_in FILE Y X0 N: pixels of row Y from X0 that are ChoralRoot's yellow (242,183,5)
    od -An -tu1 -v -j $((15 + ($2 * 240 + $3) * 3)) -N$(($4 * 3)) "$1" | tr -s ' \n' '\n\n' | grep -v '^$' |
        awk '{ v[n++] = $1 } END { c = 0; for (i = 0; i + 2 < n; i += 3) if (v[i] > 200 && v[i+1] > 140 && v[i+2] < 90) c++; print c }'
}
differ() { cmp -s "$OUT/$1.ppm" "$OUT/$2.ppm" && bad "$3: $1 = $2" || ok "$3"; }

echo "MAJ held + D4: D major"
run cr_dmaj --wav "$OUT/cr_dmaj.wav"
nz=$(num "non-silent blocks" "$OUT/cr_dmaj.log")
[ "${nz:-0}" -gt 0 ] && ok "sound: $nz non-silent blocks" || bad "no sound"
has 'expect led D4 on .*: ok' "$OUT/cr_dmaj.log" && has 'expect led F#4 on .*: ok' "$OUT/cr_dmaj.log" &&
    has 'expect led A4 on .*: ok' "$OUT/cr_dmaj.log" && ok "the D4 F#4 A4 LEDs lit (the voiced notes)" || bad "voiced-note LEDs"
has 'chord D sounding 1 notes 62 66 69' "$OUT/cr_dmaj.log" && ok "the engine: D, notes 62 66 69 on part 0" || bad "not D major"
has 'part 0: .* voices 3' "$OUT/cr_dmaj.log" && ok "part 0 plays the three notes" || bad "part 0 not playing 3 voices"
[ -s "$OUT/cr_dmaj.ppm" ] && ok "screenshot: $OUT/cr_dmaj.ppm" || bad "no cr_dmaj.ppm"
has 'voicing 2 octave' "$OUT/cr_dmaj.log" && has 'notes 69 74 78' "$OUT/cr_dmaj.log" &&
    ok "KNOB 1 +2: voicing 2, notes A4 D5 F#5" || bad "KNOB 1 voicing"
differ cr_dmaj cr_voicing "KNOB 1 +2: the screen changed (the voicing line): $OUT/cr_voicing.ppm"
silent_end cr_dmaj

echo "KEY: Key Mode"
run cr_key
has 'expect led SEL on .*: ok' "$OUT/cr_key.log" && ok "KEY (printed SEL) tapped: its LED lit" || bad "KEY LED"
has 'chord Dm sounding 1' "$OUT/cr_key.log" && has 'key 1 tonic 0' "$OUT/cr_key.log" &&
    ok "Key Mode C major: D4 alone -> Dm" || bad "Key Mode chord"
y=0; for row in 8 10 12 14 16; do y=$((y + $(yellow_in "$OUT/cr_keymode.ppm" $row 6 60))); done
[ "$y" -gt 10 ] && ok "\"Key: C\" in yellow on the top line ($y yellow pixels): $OUT/cr_keymode.ppm" \
                || bad "no yellow Key: line on the top line"
differ cr_keymode cr_selectkey "KEY held: the select-key screen: $OUT/cr_selectkey.ppm"
has 'key 1 tonic 4 scale 0' "$OUT/cr_key.log" && ok "a root in the key layer (E4): tonic E" || bad "tonic not set"
silent_end cr_key

echo "PERF held: the Perform picker"
run cr_perf
differ cr_perf_picker cr_perf_arp "a root moved the picker: $OUT/cr_perf_picker.ppm -> cr_perf_arp.ppm"
has 'perform 1 mode 2' "$OUT/cr_perf.log" && ok "G4 in the layer: Arpeggiate, performance on" || bad "mode not chosen"
has 'chord Am7 sounding 1' "$OUT/cr_perf.log" && ok "Am7 arpeggiated (MIN + m7 + A4)" || bad "no Am7"
silent_end cr_perf

echo "PRESETS: the sound"
run cr_sound --wav "$OUT/cr_sound.wav"
run cr_sound_ref --wav "$OUT/cr_sound_ref.wav"
has 'part 0: FM6 / BELL' "$OUT/cr_sound.log" && ok "PRESETS +1: part 0 loads the next sound (TINE EP -> BELL)" \
                                                || bad "no sound change"
[ -s "$OUT/cr_sound_meter.ppm" ] && ok "the sound meter: $OUT/cr_sound_meter.ppm" || bad "no meter screenshot"
a=$(num "non-zero samples" "$OUT/cr_sound.log"); b=$(num "non-zero samples" "$OUT/cr_sound_ref.log")
cmp -s "$OUT/cr_sound.wav" "$OUT/cr_sound_ref.wav" && bad "the same audio with another sound" \
    || ok "the same chord, another timbre: the audio differs ($a / $b non-zero samples)"

echo "ALGORITHM: the bass"
run cr_bass
has 'bass 1 (sound 1)' "$OUT/cr_bass.log" && ok "ALGORITHM +1: bass on (sound 01)" || bad "bass not on"
has 'part 1: .* voices 1' "$OUT/cr_bass.log" && ok "a bass note on part 1" || bad "no bass note on part 1"
silent_end cr_bass

echo "BASS tap: both parts sound (the chord part is not silenced), the status, Solo"
run cr_bass_both --wav "$OUT/cr_bass_both.wav"
L=$OUT/cr_bass_both.log
# the WAV: the chord's F#4 (370 Hz, Goertzel over 0.4 s of each held chord) with the bass on vs the reference without
# it, in Solo, and back in Chords Only
wv=$(python3 - "$OUT/cr_bass_both.wav" <<'PY'
import sys, wave, struct, math
w = wave.open(sys.argv[1]); sr = w.getframerate(); ch = w.getnchannels()
raw = w.readframes(w.getnframes()); x = struct.unpack('<%dh' % (len(raw) // 2), raw)
def band(t0, t1, f=369.99):
    a, b = int(t0 * sr), int(t1 * sr); k = 2 * math.cos(2 * math.pi * f / sr); s1 = s2 = 0.0
    for i in range(a, b):
        s = (x[i * ch] + x[i * ch + ch - 1]) / 2 + k * s1 - s2; s2 = s1; s1 = s
    return math.sqrt(max(s1 * s1 + s2 * s2 - k * s1 * s2, 0)) / (b - a)
r = band(0.4, 0.8)
print(*('%d' % (100 * band(t, t + 0.4) / r) for t in (3.76, 12.78, 16.65)))
PY
)
set -- $wv
[ "${1:-0}" -ge 50 ] && [ "${3:-0}" -ge 50 ] && [ "${2:-100}" -lt 10 ] &&
    ok "the WAV: part 0's F#4 with the bass on ${1}% of the chord alone, back in Chords Only ${3}%, in Solo ${2}%" \
    || bad "the WAV: part 0's F#4 with the bass on ${1:-?}%, Chords Only again ${3:-?}%, Solo ${2:-?}% (of the chord alone)"
pv() { grep "part $1:" "$L" | sed -n "${2}p" | sed -n 's/.*voices \([0-9]*\),.*/\1/p'; }   # pv PART DUMP#: its voices
orange_in() {    # orange_in FILE: ChoralRoot's orange pixels in the status slot (top right: rows 4..19, x 150..237)
    for row in 4 6 8 10 12 14 16 18; do
        od -An -tu1 -v -j $((15 + ($row * 240 + 150) * 3)) -N$((88 * 3)) "$1" | tr -s ' \n' '\n\n' | grep -v '^$'
    done | awk '{ v[n++] = $1 } END { c = 0; for (i = 0; i + 2 < n; i += 3) if (v[i] > 200 && v[i+1] > 80 && v[i+1] < 170 && v[i+2] < 90) c++; print c }'
}
has '^bass: on (sound 1, Chords Only)' "$L" && has 'expect led ENV on .*: ok' "$L" &&
    ok "BASS tapped: the bass on (SUB BASS, Chords Only), its LED lit" || bad "BASS tap: $(grep '^bass:' "$L" | head -1)"
[ "$(pv 0 1)" = 3 ] && [ "$(pv 1 1)" = 0 ] && ok "before: MAJ + D4 on part 0 only (3 voices)" || bad "before: part 0 $(pv 0 1) part 1 $(pv 1 1)"
[ "$(pv 0 2)" = 3 ] && [ "$(pv 1 2)" = 1 ] && ok "bass on: MAJ + D4 sounds on part 0 (3 voices) AND part 1 (the bass note)" \
    || bad "bass on: part 0 $(pv 0 2) part 1 $(pv 1 2) voices (the chord part must keep sounding)"
[ "$(pv 0 3)" = 3 ] && has 'part 1: ANALOG / SQR BASS' "$L" && ok "ALGORITHM +1 with the chord held: another bass sound, part 0 still 3 voices" \
    || bad "a bass sound change with the chord held: part 0 $(pv 0 3)"
has '^bass: off' "$L" && has 'expect led ENV dim .*: ok' "$L" && [ "$(pv 0 4)" = 3 ] && [ "$(pv 1 4)" = 0 ] &&
    ok "BASS tapped off: its LED back to the glow, the chord on part 0 only" || bad "bass off: part 0 $(pv 0 4) part 1 $(pv 1 4)"
o=$(orange_in "$OUT/cr_bass_both_pop.ppm"); n=$(orange_in "$OUT/cr_bass_both_on.ppm"); f=$(orange_in "$OUT/cr_bass_both_off.ppm")
differ cr_bass_both_pop cr_bass_both_on "BASS tap: the bass meter popup (01 SUB BASS, orange): $OUT/cr_bass_both_pop.ppm"
[ "$n" -gt 40 ] && [ "$f" = 0 ] && ok "\"Bass\" in orange top right while the bass is on ($n px; off: $f): $OUT/cr_bass_both_on.ppm" \
    || bad "the Bass status: on $n off $f orange px"
has '^bass: behaviour Solo' "$L" && [ "$(pv 0 5)" = 0 ] && [ "$(pv 1 5)" = 1 ] &&
    ok "Bass Behaviour Solo (the BASS layer, G4): the bass alone, part 0 silent by design" || bad "Solo: part 0 $(pv 0 5) part 1 $(pv 1 5)"
s=$(orange_in "$OUT/cr_bass_both_solo.ppm")
[ "$s" -gt "$n" ] && ok "Solo shown as \"Bass Solo\" in orange ($s px): $OUT/cr_bass_both_solo.ppm" || bad "no Bass Solo status ($s px)"
has '^bass: behaviour Chords Only' "$L" && [ "$(pv 0 6)" = 3 ] && [ "$(pv 1 6)" = 1 ] &&
    ok "Chords Only again (D4 in the layer): both parts sound" || bad "Chords Only: part 0 $(pv 0 6) part 1 $(pv 1 6)"
silent_end cr_bass_both

echo "Esc: PANIC; idle: the stripes"
run cr_panic
p=$(pixel "$OUT/cr_panic.ppm" 120 200); set -- $p
[ "${1:-0}" -gt 180 ] && [ "${2:-255}" -lt 90 ] && [ "${3:-255}" -lt 90 ] && ok "the red PANIC screen ($p): $OUT/cr_panic.ppm" \
                                                                         || bad "not red ($p)"
silent_end cr_panic
p=$(pixel "$OUT/cr_idle_again.ppm" 120 150); set -- $p
[ "${1:-0}" -gt 180 ] && [ "${2:-255}" -lt 90 ] && ok "3 s idle: the stripes again ($OUT/cr_idle_again.ppm)" \
                                                 || bad "no stripes after 3 s idle ($p)"

echo "the other screens"
run cr_layers
for x in options options_sel fx_layer bass_layer engine_picker loop_layer bpm fx_amount view_keyboard view_notes view_geek; do
    [ -s "$OUT/cr_$x.ppm" ] || bad "no cr_$x.ppm"
done
differ cr_options cr_options_sel "Options: SELECT and KNOB 1 changed it"
differ cr_fx_layer cr_bass_layer "FX / BASS layers differ"
differ cr_engine_picker cr_loop_layer "EDIT / LOOP layers differ"
silent_end cr_layers

echo "EDIT: the sound editor (cr_edit.c): groups, screens, lanes, knobs, memory"
export EMU_UI_LOG=5
run cr_editor --wav "$OUT/cr_editor.wav"
unset EMU_UI_LOG
L="$OUT/cr_editor.log"
has '^edit: open part 0' "$L" && has '^edit: group OSC screen 1 lane 1 part 0' "$L" &&
    ok "EDIT tap: the editor on the chord sound, OSC screen 1 (the four oscillators): $OUT/cr_editor_osc.ppm" || bad "EDIT tap: no editor"
has '^edit: group OSC screen 1 lane 2' "$L" && has '^deep: part 0 page 2 OSC 2 col 2 COARSE 0 -> -4 .* edited' "$L" &&
    ok "SELECT +1: OSC 2; KNOB 3 -2: its COARSE two detents, the sound edited: $OUT/cr_editor_osc2.ppm" || bad "SELECT / KNOB 3"
has '^edit: group OSC screen 2 lane 2' "$L" && ok "OSC (FX) tap: screen 2, the \"+\" stack, OSC 2 kept: $OUT/cr_editor_osc_b.ppm" || bad "OSC tap: no screen 2"
differ cr_editor_osc2 cr_editor_osc_b "screen 2 drawn"
has '^edit: group OSC screen 3 lane 1' "$L" && has '^deep: part 0 page 2 OSC 2 col [0-9] LEVEL' "$L" &&
    ok "OSC tap again: the oscillator mixer (one lane, the four LEVELs), KNOB 2: OSC 2's level: $OUT/cr_editor_osc_mix.ppm" || bad "the mixer"
has '^edit: group FILT screen 1 lane 1' "$L" && has '^deep: part 0 page [0-9]* FILTER col 0 CUT' "$L" && has '^edit: group FILT screen 1 lane 2' "$L" &&
    ok "FILT (SEL): the filter (CUT RES FTYPE FENV), KNOB 1 the cutoff, SELECT: row B: $OUT/cr_editor_filt.ppm, $OUT/cr_editor_filt_b.ppm" || bad "FILT"
cmp -s "$OUT/cr_editor_filt_turn.ppm" "$OUT/cr_editor_filt.ppm" && ok "the filter curve jumps to the new cutoff (no tween: 60 ms after the detent = 460 ms after)" \
    || bad "the band still moving after the detent: $OUT/cr_editor_filt_turn.ppm / cr_editor_filt.ppm"
has '^deep: part 0 page [0-9]* ENV 1 col 1 DEC 90 -> 96' "$L" && has '^edit: group ENV screen 1 lane 2' "$L" &&
    has '^edit: group ENV screen 2 lane 1' "$L" && has '^edit: group ENV screen 2 lane 2' "$L" &&
    ok "ENV: ENV 1 (KNOB 2: DEC), SELECT: ENV 1 B -> ENV 2 A -> ENV 2 B (across the screens): $OUT/cr_editor_env1.ppm $OUT/cr_editor_env2.ppm $OUT/cr_editor_env2_b.ppm" || bad "ENV"
grep '^edit: group ENV' "$L" | tail -1 | grep -q 'screen 1 lane 1' && ok "SELECT -3: back to ENV 1 A (wrapping back across the screens)" || bad "SELECT back: $(grep '^edit: group ENV' "$L" | tail -1)"
differ cr_editor_env1 cr_editor_env2 "ENV 2 drawn with its own envelope"
has '^edit: group LFO screen 1 lane 1' "$L" && has '^edit: group LFO screen 2 lane 1' "$L" &&
    ok "LFO, tap: screen 2 (sync): $OUT/cr_editor_lfo.ppm $OUT/cr_editor_lfo_b.ppm" || bad "LFO"
has '^edit: group MOD screen 1 lane 3 ' "$L" && has '^deep: part 0 page [0-9]* MOD [0-9] col 2 AMT' "$L" &&
    ok "MOD (SEQ), SELECT +2: slot 3, its source, destination, amount: $OUT/cr_editor_mod.ppm" || bad "MOD"
has '^edit: group FX ' "$L" && has '^edit: group MIX screen 1 lane 2 ' "$L" &&
    ok "FX (PLAY): the sends; MIX (REC), SELECT: row B: $OUT/cr_editor_fx.ppm $OUT/cr_editor_mix_b.ppm" || bad "FX / MIX"
has '^param: part 0 MIX DTUNE 40 -> 46 ' "$L" && has '^param: part 0 MIX DTUNE 46 -> 47 ' "$L" &&
    ok "MIX row B KNOB 2: a detent (40 -> 46), SHIFT held: one step (46 -> 47)" || bad "SHIFT fine step"
has '^edit: shift latched' "$L" && has '^param: part 0 MIX DTUNE 47 -> 48 ' "$L" && has '^edit: shift off' "$L" &&
    has '^expect led GLO on .*: ok' "$L" && has '^expect led GLO dim .*: ok' "$L" &&
    ok "GLO tap: SHIFT latched (LED lit, \"fine\"), KNOB 2 one step (47 -> 48), tap: off: $OUT/cr_editor_fine.ppm" || bad "SHIFT latch"
has '^edit: part 1' "$L" && has '^edit: group OSC screen 1 lane 1 part 1' "$L" && has '^edit: part 0' "$L" &&
    ok "SHIFT + EDIT: the bass sound and back: $OUT/cr_editor_bass.ppm" || bad "SHIFT + EDIT"
[ "$(grep -c '^layer: open 2 (locked)' "$L")" = 2 ] && [ "$(grep -c '^layer: close 2' "$L")" = 2 ] &&
    cmp -s "$OUT/cr_editor_pre_perf.ppm" "$OUT/cr_editor_perf_back.ppm" && cmp -s "$OUT/cr_editor_pre_perf.ppm" "$OUT/cr_editor_perf_home.ppm" &&
    ok "PERF held from the editor: the perform layer ($OUT/cr_editor_perf.ppm); OCT- and HOME: back to the editor as it was" \
    || bad "PERF from the editor: $OUT/cr_editor_pre_perf.ppm / cr_editor_perf_back / cr_editor_perf_home"
differ cr_editor_save cr_editor_back "SAVE: the naming screen over the editor ($OUT/cr_editor_save.ppm), OCT-: cancelled back to it"
cmp -s "$OUT/cr_editor_back.ppm" "$OUT/cr_editor_again.ppm" && ok "OCT-, HOME, EDIT: back in MIX row B (the group, screen and lane remembered)" \
    || bad "the editor not as left: $OUT/cr_editor_back.ppm / $OUT/cr_editor_again.ppm"
[ "$(grep -c '^edit: group MIX screen 1 lane 2 part 0' "$L")" -ge 3 ] && ok "the trace: MIX row B after SHIFT + EDIT and after EDIT again" || bad "group memory"
has '^edit: close' "$L" && differ cr_editor_home cr_editor_again "HOME left the editor: $OUT/cr_editor_home.ppm"
has '^save: part 0 slot U01 name LUSH PAD rc 0' "$L" && ok "SAVE SAVE: saved to U01 from the editor: $OUT/cr_editor_saved.ppm" || bad "SAVE SAVE: $(grep '^save:' "$L")"
has '^expect sound .*: ok' "$L" && ok "the root keys audition in the editor" || bad "no audition"
has '^expect led FX on .*: ok' "$L" && has '^expect led SEL dim .*: ok' "$L" && has '^expect led REC on .*: ok' "$L" &&
    ok "LEDs: the group's button lit, the others dim" || bad "editor LEDs"
u=$(grep -E '^ui: frame .* M instructions .*\): (edit8|stack) ' "$L" | sed -n 's/^ui: frame .*: \([0-9.]*\) M instructions.*/\1/p' | sort -n | tail -1)
[ "${u:-0}" = 0 ] || [ "${u%.*}" -lt 10 ] && ok "UI frames on the sound pages: max ${u:-<5} M host instructions (< 10)" || bad "a UI frame of $u M instructions"
silent_end cr_editor

echo "the editor: no slide, responsiveness (a detent drawn in the next frame), the quick modulation mapping"
run cr_editor_noslide
same3() {   # same3 A B C what: the three shots identical
    cmp -s "$OUT/$1.ppm" "$OUT/$2.ppm" && cmp -s "$OUT/$1.ppm" "$OUT/$3.ppm" && ok "$4" || bad "$4: $OUT/$1.ppm $2 $3 differ"
}
same3 cr_noslide_grp_a cr_noslide_grp_b cr_noslide_grp_c "FILT tapped: the first two frames are the settled screen (no slide): $OUT/cr_noslide_grp_a.ppm"
same3 cr_noslide_sel_a cr_noslide_sel_b cr_noslide_sel_c "SELECT (row B): the bars at once (no slide)"
cmp -s "$OUT/cr_noslide_scr_a.ppm" "$OUT/cr_noslide_scr_c.ppm" && ok "OSC tapped: the stack at once" || bad "OSC: $OUT/cr_noslide_scr_a.ppm / scr_c"
same3 cr_noslide_scr2_a cr_noslide_scr2_b cr_noslide_scr2_c "OSC again (screen 2): at once"
differ cr_noslide_grp_c cr_noslide_sel_c "SELECT drew row B active"
export EMU_UI_LOG=0
run cr_editor_lag
unset EMU_UI_LOG
L="$OUT/cr_editor_lag.log"
lag=$(awk '   # per CUT / LEVEL detent: the last frame (1 = the next) before the next detent that drew (blits B/D, D > 0)
    /^deep: .* (CUT|LEVEL) / { if (p) out(); p = 1; nf = 0; lc = 0; b1 = 0; cut = ($0 ~ / CUT /); next }
    /^ui: frame/ {
        split($0, a, " "); mi = a[7] + 0; d = $0; sub(/.* blits /, "", d); split(d, bd, /[\/ ]/)
        if (p) { nf++; if (bd[2] + 0 > 0) lc = nf; if (nf == 1) b1 = bd[1] + 0
                 if (nf == 1 && cut && mi > cmax) cmax = mi; if (nf == 1 && !cut && mi > smax) smax = mi }
        next }
    /^edit: group OSC/ { if (p) out() }
    function out() { n++; if (lc > lmax) lmax = lc; if (lc == 1 && b1 > 0) one++; p = 0 }
    END { if (p) out(); printf "%d %d %d %.1f %.1f\n", n, one, lmax, cmax, smax }' "$L")
set -- $lag
[ "${1:-0}" = 30 ] && [ "$2" = 30 ] && ok "a detent is drawn in the next frame and stays: $2 of $1 detents (20 cutoff, 10 OSC level) changed the band / cell in the frame after it, none later (lag max $3 frame)" \
    || bad "lag: $2 of $1 detents drawn in the next frame, max $3 frames (EMU_UI_LOG=0: $L)"
awk -v c="$4" -v s="$5" 'BEGIN { exit !(c <= 4.0 && s <= 4.0) }' && ok "UI frame of a detent: cutoff on FILTER max $4 M host instructions, OSC level on the stack max $5 M (<= 4 M, ~15 ms device)" \
    || bad "a detent's UI frame: cutoff $4 M, OSC level $5 M (> 4 M)"
run cr_editor_map
L="$OUT/cr_editor_map.log"
has '^mod: ENV2 -> CUT +12 slot 1$' "$L" && ok "ENV held + KNOB 1 +2 on FILTER: mod: ENV2 -> CUT +12 slot 1 (ENV 2 by default): $OUT/cr_map_hot.ppm" || bad "ENV mapping: $(grep -m1 '^mod:' "$L")"
yel() { set -- "$(pixel "$OUT/$1.ppm" $2 $3)" "$4"; set -- "$(echo $1)" "$2"; [ "$1" = "$2" ]; }   # yel SHOT X Y "R G B"
yel cr_map_mark 55 126 "247 182 0" && ok "the Cutoff cell's mark: a yellow square (ENV): $OUT/cr_map_mark.ppm" || bad "no yellow mark on Cutoff: $(pixel "$OUT/cr_map_mark.ppm" 55 126)"
yel cr_map_filt 55 126 "8 8 8" && ok "no mark before the mapping" || bad "a mark before: $(pixel "$OUT/cr_map_filt.ppm" 55 126)"
has '^mod: LFO1 -> RES +6 slot 2$' "$L" && yel cr_map_mark2 115 126 "231 56 41" && ok "LFO held + KNOB 2: LFO1 -> RES slot 2, a red mark on Reso: $OUT/cr_map_mark2.ppm" || bad "LFO mapping: $(grep '^mod: LFO1 -> RES' "$L") $(pixel "$OUT/cr_map_mark2.ppm" 115 126)"
has '^mod: LFO1 -> CUT -6 slot 3$' "$L" && yel cr_map_white 55 126 "247 243 239" && ok "LFO1 -> CUT too: two sources, a white mark: $OUT/cr_map_white.ppm" || bad "white mark: $(pixel "$OUT/cr_map_white.ppm" 55 126)"
differ cr_map_hot cr_map_mark "the hot cell showed \"ENV2 +12\" in yellow ($OUT/cr_map_hot.ppm), then the cutoff again"
has '^mod: clear CUT, 2 slots$' "$L" && has '^expect led OCT- dim .*: ok' "$L" && ! yel cr_map_after 55 126 "247 243 239" && ! yel cr_map_after 55 126 "247 182 0" &&
    ok "OCT- held + KNOB 1: CUT's slots cleared (\"cleared\": $OUT/cr_map_cleared.ppm), the mark gone, no octave step" || bad "clear: $(grep '^mod: clear' "$L")"
[ "$(grep -c '^edit: group FILT' "$L")" = 5 ] && [ "$(grep -c '^edit: group ENV' "$L")" = 1 ] &&
    ok "ENV / LFO released after a turn, or held 600 ms with none: no tap (FILT stays); ENV tapped: the ENV group" || bad "taps: $(grep -c '^edit: group' "$L") group lines"
has '^mod: LFO1 -> LVL1 +6 slot 1$' "$L" && ok "on the OSC stack: LFO held + KNOB 2 (Level): LFO1 -> LVL1, its mark: $OUT/cr_map_stack.ppm" || bad "stack mapping"
has '^mod: ENV1 -> CUT +6 slot 3$' "$L" && has '^mod: ENV1 -> DRIVE +6 slot 8$' "$L" && ok "ENV 1 shown last: ENV held maps ENV1 (CUT .. DRIVE, slots 3..8)" || bad "ENV1 mapping"
has '^mod: LFO1 -> CUT matrix full$' "$L" && ok "the ninth: \"matrix full\": $OUT/cr_map_full.ppm" || bad "matrix full"
has '^mod: not modulatable$' "$L" && ok "a platform engine (ANALOG): \"not modulatable\"" || bad "not modulatable"
export EMU_UI_LOG=5
run cr_editor_modes --wav "$OUT/cr_editor_modes.wav"
unset EMU_UI_LOG
L="$OUT/cr_editor_modes.log"
region_differs() {   # region_differs A B X0 Y0 X1 Y1: the two shots differ inside the box
    python3 - "$OUT/$1.ppm" "$OUT/$2.ppm" "$3" "$4" "$5" "$6" <<'PY'
import sys
from PIL import Image
a, b = (Image.open(f).convert("RGB").crop(tuple(int(v) for v in sys.argv[3:7])) for f in sys.argv[1:3])
sys.exit(0 if a.tobytes() != b.tobytes() else 1)
PY
}
has '^deep: part 0 page 0 OSC 1 col 0 MORPH ' "$L" && region_differs cr_modes_morph_a cr_modes_morph_b 20 42 80 74 &&
    ok "MORPH: KNOB 1 moves OSC 1's position, its Wave cell's morphed wave changes ($(grep -m1 '^deep: part 0 page 0 OSC 1 col 0 MORPH' "$L" | sed 's/.*MORPH //')): $OUT/cr_modes_morph_a.ppm -> $OUT/cr_modes_morph_b.ppm" \
    || bad "MORPH glyph: $OUT/cr_modes_morph_a.ppm / cr_modes_morph_b.ppm"
region_differs cr_modes_morph_b cr_modes_morph_c 20 42 80 74 && ok "MORPH: one more detent, the glyph follows frame by frame: $OUT/cr_modes_morph_c.ppm" || bad "MORPH glyph: one detent"
has '^deep: part 0 page [0-9]* FILTER col 2 FTYPE 0 -> ' "$L" && region_differs cr_modes_ftype_a cr_modes_ftype_b 8 44 232 118 &&
    ok "FTYPE from LP toward BP ($(grep -m1 'FILTER col 2 FTYPE' "$L" | sed 's/.*FTYPE //')): the band's curve morphs: $OUT/cr_modes_ftype_a.ppm -> $OUT/cr_modes_ftype_b.ppm" \
    || bad "FTYPE band: $(grep -m1 'FTYPE' "$L")"
# the battery shows on the Options page only: none in the editor's title lines
for shot in cr_modes_mix; do                      # (the FILTER shot has its right text under that pixel)
    [ "$(pixel "$OUT/$shot.ppm" 230 12)" = "$(pixel "$OUT/$shot.ppm" 2 2)" ] &&
        ok "$shot: no battery in the editor's title line" || bad "$shot: a battery in the editor's title line"
done
[ -f "$OUT/cr_glow_options.ppm" ] && { [ "$(pixel "$OUT/cr_glow_options.ppm" 230 12)" != "$(pixel "$OUT/cr_glow_options.ppm" 2 2)" ] &&
    ok "Options: the battery at the top line's right end: $OUT/cr_glow_options.ppm" || bad "Options: no battery on the Options page"; }
has '^edit: group OSC screen 1 lane 1 part 0' "$L" && ok "VINYL KEYS: OSC 4's NOISE VINYL glyph: $OUT/cr_modes_noise.ppm" || bad "VINYL KEYS OSC"
u=$(grep -E '^ui: frame .* M instructions .*\): (edit8|stack) ' "$L" | sed -n 's/^ui: frame .*: \([0-9.]*\) M instructions.*/\1/p' | sort -n | tail -1)
[ "${u:-0}" = 0 ] || [ "${u%.*}" -lt 10 ] && ok "UI frames with the morph / noise glyphs and the FTYPE band: max ${u:-<5} M host instructions (< 10)" || bad "a UI frame of $u M instructions"
silent_end cr_editor_modes
run cr_editor_steps --wav "$OUT/cr_editor_steps.wav"
run cr_editor_ref --wav "$OUT/cr_editor_ref.wav"
L="$OUT/cr_editor_steps.log"
has '^param: part 0 ENV DEC 90 -> 96 ' "$L" && ok "a platform engine's ENV: a detent: DEC 90 -> 96 (5% of 0..127): $OUT/cr_editor_p_env.ppm" || bad "coarse step: $(grep -m1 '^param:' "$L")"
has '^param: part 0 ENV DEC 96 -> 97 ' "$L" && ok "SHIFT held + KNOB 2: DEC 96 -> 97 (fine)" || bad "fine step"
grep '^param: part 0 ENV DEC' "$L" | tail -1 | grep -q -- '-> 127 ' && ok "KNOB 2 +30: DEC clamps at 127" || bad "no clamp at 127"
has '^param: part 0 ENV ATK 80 -> 122 ' "$L" && ok "KNOB 1 +7 (6 a detent): ATK 80 -> 122" || bad "KNOB 1: ATK not changed"
grep -q '^split\|split point' "$L" && bad "SHIFT + KNOB reached the split point" || true
has '^param: part 0 LFO WAVE 0 -> 1 ' "$L" && ok "LFO WAVE steps by one" || bad "WAVE: $(grep 'LFO WAVE' "$L")"
has '^param: part 0 MOD FLT ' "$L" && ok "the platform MOD (4 slots): KNOB 3 the amount: $OUT/cr_editor_p_mod.ppm" || bad "platform MOD"
grep '^param: part 0 MIX VCE' "$L" | grep -q -- '\([0-9]*\) -> ' && ok "MIX VOICE: $(grep '^param: part 0 MIX VCE' "$L" | head -1 | sed 's/^param: //')" || bad "no VOICE step"
cmp -s "$OUT/cr_editor_steps.wav" "$OUT/cr_editor_ref.wav" && bad "the same audio with another envelope" \
    || ok "the same chord with ATK 122 / DEC 127: the audio differs ($(num "non-zero samples" "$L") / $(num "non-zero samples" "$OUT/cr_editor_ref.log") non-zero samples)"
silent_end cr_editor_steps

echo "EDIT held: the engine picker as a preview (cancel / keep), its roots, PRESETS in the editor"
run cr_editor_pick --wav "$OUT/cr_editor_pick.wav"
L="$OUT/cr_editor_pick.log"
o1=$(sed -n 's/^picker: open part 0 LUSH PAD crc \([0-9a-f]*\) .*/\1/p' "$L" | sed -n 1p)
c1=$(sed -n 's/^picker: cancel part 0 -> LUSH PAD crc \([0-9a-f]*\)$/\1/p' "$L" | sed -n 1p)
has '^preset: part 0 -> VA / ' "$L" && [ -n "$o1" ] && [ "$o1" = "$c1" ] &&
    ok "KNOB 1 previewed a preset, OCT- cancelled: LUSH PAD restored (crc $c1): $OUT/cr_pick_preview.ppm, $OUT/cr_pick_cancel.ppm" \
    || bad "picker cancel: open '$o1' cancel '$c1'"
cmp -s "$OUT/cr_pick_editor.ppm" "$OUT/cr_pick_cancel.ppm" && ok "OCT-: back in the editor as it was" || bad "not back in the editor: $OUT/cr_pick_cancel.ppm"
c2=$(sed -n 's/^picker: cancel part 0 -> LUSH PAD crc \([0-9a-f]*\)$/\1/p' "$L" | sed -n 2p)
has '^picker: roots play' "$L" && has '^expect sound .*: ok' "$L" && has '^picker: roots engines' "$L" &&
    has '^engine: part 0 -> ANALOG' "$L" && [ "$c2" = "$o1" ] &&
    ok "KNOB 4: the roots play (D4 sounded, no engine), again: engines (D4: ANALOG); OCT-: the VA patch back (crc $c2): $OUT/cr_pick_roots_play.ppm" \
    || bad "picker roots / engine cancel (crc '$c2')"
[ "$(grep -c '^engine: part 0' "$L")" = 1 ] && ok "the roots off: no engine switched by D4" || bad "roots off switched an engine"
grep '^layer: open 5' "$L" | sed -n 3p | grep -q . && has '^picker: keep part 0 ' "$L" &&
    ok "PRESETS in the editor: the picker previewing the next preset, OCT+ kept it: $OUT/cr_pick_presets.ppm $OUT/cr_pick_kept.ppm" || bad "PRESETS in the editor"
grep '^picker: keep' "$L" | grep -qv 'LUSH PAD' && ok "kept: $(grep '^picker: keep' "$L" | sed 's/^picker: //')" || bad "kept the old sound"
silent_end cr_editor_pick

echo "EDIT held: the engine picker"
run cr_engine --wav "$OUT/cr_engine.wav"
has '^engine: part 0 -> ANALOG' "$OUT/cr_engine.log" && has 'part 0: ANALOG /' "$OUT/cr_engine.log" &&
    ok "EDIT held + D4: part 0 FM6 -> ANALOG: $OUT/cr_engine_switched.ppm" || bad "the engine did not switch"
has '^preset: part 0 -> ANALOG / ' "$OUT/cr_engine.log" && ok "EDIT held + KNOB 1: the engine's next preset" || bad "KNOB 1: no preset step"
silent_end cr_engine

echo "SAVE: naming, the user slot"
run cr_save
has '^save: part 0 slot U01 name ADG' "$OUT/cr_save.log" && ok "SAVE, D4 E4 F4, OCT+: U01 \"ADG\": $OUT/cr_save_typed.ppm" || bad "not saved"
has '^sound: part 0 pos [0-9]* ADG' "$OUT/cr_save.log" && ok "PRESETS reaches U01 after the bank sounds ($(sed -n 's/^sound: part 0 pos \([0-9]*\) ADG.*/\1/p' "$OUT/cr_save.log" | head -1) of them): $OUT/cr_save_preset.ppm" \
    || bad "U01 not on PRESETS"
grep -q '^edit: .*part 1' "$OUT/cr_save.log" && has '^param: part 1 ' "$OUT/cr_save.log" &&
    ok "BASS held + EDIT: the bass sound's pages, KNOB 1 edits part 1: $OUT/cr_edit_bass.ppm" || bad "BASS + EDIT"

mkdir -p "$OUT/cr_again"
echo "LOOP / REC: the looper"
L() { grep '^  loop:' "$OUT/$1.log" | sed -n "$2p"; }
lf() { echo "$1" | sed -n "s/.* $2 \([0-9A-F]*\) .*/\1/p"; }
run cr_loop_free --wav "$OUT/cr_loop_free.wav"
a=$(L cr_loop_free 2); b=$(L cr_loop_free 3); c=$(L cr_loop_free 4); d=$(L cr_loop_free 5)
[ "$(lf "$a" layers)" = 1 ] && [ "$(lf "$a" events)" = 2 ] && [ "$(lf "$a" len)" = 384 ] && [ "$(lf "$a" state)" = 2 ] &&
    ok "Free: D + Em, REC at 2 s: a 1-bar loop (384 ticks), 2 events, playing at once: $OUT/cr_loop_play_a.ppm" || bad "Free take: $a"
[ "$(lf "$a" played)" = 1 ] && [ "$(lf "$b" played)" = 2 ] && [ "$(lf "$c" played)" = 3 ] &&
    ok "playback: D at the commit, Em 1 s later, D again at 2 s (played 1 / 2 / 3 at +0.4 / +1.1 / +2.1 s)" \
    || bad "playback times: $(lf "$a" played) $(lf "$b" played) $(lf "$c" played)"
[ "$(lf "$a" ring)" -lt "$(lf "$b" ring)" ] && ok "the ring advances ($(lf "$a" ring) -> $(lf "$b" ring) /256)" || bad "ring"
differ cr_loop_play_a cr_loop_play_b "the ring on screen moved: $OUT/cr_loop_play_b.ppm"
has 'expect led GREEN on .*: ok' "$OUT/cr_loop_free.log" && has 'expect led PLAY on .*: ok' "$OUT/cr_loop_free.log" &&
    ok "LOOP's LED lit (a loop) and its green LED (playing)" || bad "LOOP LEDs"
[ "$(grep -c 'expect led REC on .*: ok' "$OUT/cr_loop_free.log")" = 2 ] && ok "REC lit when armed and overdub-armed" || bad "REC LED"
[ "$(lf "$d" layers)" = 2 ] && [ "$(lf "$d" events)" = 3 ] && ok "overdub: G on a second layer (3 events): $OUT/cr_loop_overdub.ppm" || bad "overdub: $d"
e=$(L cr_loop_free 6); f=$(L cr_loop_free 7); g=$(L cr_loop_free 8)
[ $(( $(lf "$e" played) - $(lf "$d" played) )) = 3 ] && ok "the overdub plays in the next cycle (3 events in 2 s)" || bad "overdub playback"
[ "$(lf "$f" layers)" = 1 ] && [ "$(lf "$f" events)" = 2 ] && ok "REC held: undo removed the layer" || bad "undo: $f"
[ "$(lf "$g" state)" = 0 ] && [ "$(lf "$g" events)" = 0 ] && ok "LOOP held + D#4 held 1 s: cleared: $OUT/cr_loop_layer_playing.ppm" || bad "clear: $g"
silent_end cr_loop_free
run cr_loop_sync --flash "$OUT/cr_loop_flash.bin"
a=$(L cr_loop_sync 1); b=$(L cr_loop_sync 2); c=$(L cr_loop_sync 4)
[ "$(lf "$a" cap)" = 2 ] && has 'expect sound .*: ok' "$OUT/cr_loop_sync.log" &&
    ok "2 bars synced: REC starts a one-bar count-in, its click audible with nothing played: $OUT/cr_loop_countin.ppm" || bad "count-in: $a"
[ "$(lf "$b" state)" = 2 ] && [ "$(lf "$b" len)" = 768 ] && [ "$(lf "$b" events)" = 2 ] &&
    ok "it recorded 2 bars (768 ticks) and committed by itself, playing: $OUT/cr_loop_rec.ppm" || bad "sync take: $b"
has '^loop: save slot 2 ' "$OUT/cr_loop_sync.log" && [ "$(lf "$c" used)" = 002 ] &&
    ok "SAVE held + E4 + OCT+ while playing: saved to slot 2 at stop: $OUT/cr_loop_save.ppm" || bad "slot 2 not saved"
silent_end cr_loop_sync
run cr_loop_load --flash "$OUT/cr_loop_flash.bin"
a=$(L cr_loop_load 1); b=$(L cr_loop_load 2); c=$(L cr_loop_load 4)
[ "$(lf "$a" slot)" = 2 ] && [ "$(lf "$a" events)" = 2 ] && ok "relaunched with --flash: slot 2 back at power-on" || bad "boot: $a"
[ "$(lf "$b" state)" = 2 ] && [ "$(lf "$b" len)" = 768 ] && [ "$(lf "$b" played)" -ge 1 ] &&
    ok "LOOP held + E4 loads slot 2, LOOP plays it: $OUT/cr_loop_loaded.ppm" || bad "load / play: $b"
[ "$(lf "$c" state)" = 1 ] && ok "panic: the loop stops, kept" || bad "panic: $c"
silent_end cr_loop_load
"$EMU" --headless --script "$S/cr_loop_free.txt" --wav "$OUT/cr_again/cr_loop_free.wav" >/dev/null 2>&1
cmp -s "$OUT/cr_loop_free.wav" "$OUT/cr_again/cr_loop_free.wav" && ok "the looper is deterministic (the same audio twice)" || bad "looper audio differs"

echo "the loop-length layer, the count-in, undo"
run cr_countin
has '^loop: length 1' "$OUT/cr_countin.log" && ok "LOOP held + KNOB 1 +1: the length picker moved (Free -> 1 bar), no meter: $OUT/cr_loop_length_1bar.ppm" \
    || bad "KNOB 1 in the loop layer"
differ cr_loop_length cr_loop_length_1bar "the picker's selection changed: $OUT/cr_loop_length.ppm"
differ cr_countin_4 cr_countin_3 "the count-in counts down huge in red: $OUT/cr_countin_4.ppm -> cr_countin_3.ppm"
p=$(pixel "$OUT/cr_countin_4.ppm" 120 130); set -- $p
[ "${1:-0}" -gt 180 ] && [ "${2:-255}" -lt 90 ] && ok "the beat number is red ($p)" || bad "count-in number not red ($p)"
has '^undo: 1 layers left' "$OUT/cr_countin.log" && ok "REC held: undo, the layers left huge: $OUT/cr_undo.ppm" || bad "undo trace"
silent_end cr_countin

echo "the loop's notes glow; MIDI start / stop"
run cr_loop_glow
[ "$(grep -c 'expect led .* dim .*: ok' "$OUT/cr_loop_glow.log")" = 4 ] &&
    ok "LEDs Stock: D4 F#4 A4 dim while the loop sounds D, lit when it is silent: $OUT/cr_glow_playing.ppm" || bad "loop glow"
has '^midi: FA start' "$OUT/cr_loop_glow.log" && has '^midi: FC stop' "$OUT/cr_loop_glow.log" &&
    ok "MIDI Clock Out: 0xFA at the commit, 0xFC at LOOP stop" || bad "MIDI start / stop"
[ "$(grep -c '^midi:' "$OUT/cr_loop_glow.log")" = 2 ] && ok "MIDI Clock Off: LOOP played and stopped, no start / stop sent" \
    || bad "MIDI start / stop sent with MIDI Clock Off"
silent_end cr_loop_glow

echo "B3 = LOCK: the chord block latches"
run cr_lock
L="$OUT/cr_lock.log"
[ "$(grep -c '^expect .*: ok' "$L")" = 12 ] && ok "LOCK lit; latched keys lit, toggled ones dim; LOCK off: dark ($(grep -c '^expect .*: ok' "$L") LED checks)" \
    || bad "cr_lock: $(grep -c '^expect .*: ok' "$L") of 12 LED checks"
c=$(grep '^chord: [^-]' "$L" | sed 's/^chord: //' | tr '\n' ',' | sed 's/,$//')
[ "$c" = "Dm,Dm6,Dm6 9,Dm6,D,D (1 note)" ] && ok "MIN latched: Dm; 6: Dm6; 9: Dm6/9; 9 again: Dm6; MAJ: D; LOCK off: one note ($c): $OUT/cr_lock_dm69.ppm" \
    || bad "LOCK chords: $c"
has '^lock: on' "$L" && has '^lock: off' "$L" && has '^lock: latched none' "$L" && ok "the trace: lock on / off, the latch cleared" || bad "lock trace"
silent_end cr_lock

echo "a layer locks open on a hold; OCT- / HOME close it, another hold switches"
run cr_layer_lock
L="$OUT/cr_layer_lock.log"
[ "$(grep -c '^expect .*: ok' "$L")" = 11 ] && ok "FX held, released: open (OCT- lit); OCT- closed it; BASS held: switched; HOME closed; KEY held, OCT-: closed" \
    || bad "cr_layer_lock: $(grep -c '^expect .*: ok' "$L") of 11 checks"
[ "$(grep '^layer:' "$L" | tr '\n' ',')" = "layer: open 3 (locked),layer: close 3,layer: open 3 (locked),layer: open 4 (locked),layer: open 1 (locked),layer: close 1," ] &&
    ok "the trace: fx open, closed; fx, bass (switched); key open, closed" || bad "layer trace: $(grep '^layer:' "$L" | tr '\n' ' ')"
grep 'octave' "$L" | grep -v 'octave 0' | grep -q . && bad "OCT- in a layer moved the octave" || ok "OCT- in a layer: back, the octave untouched"
grep -q 'fx_on\|layer 4' "$L" && ok "screens: $OUT/cr_lock_fx.ppm, cr_lock_bass.ppm, cr_lock_key.ppm" || bad "no bass layer in the dump"
differ cr_lock_fx cr_lock_bass "the bass layer replaced the fx layer"

echo "user sound slots: rename, delete, the naming keys"
run cr_save_del
has '^save: part 0 slot U01 rename J' "$OUT/cr_save_del.log" && ok "saving over its own unedited slot renames it: $OUT/cr_save_renamed.ppm" || bad "rename"
[ "$(grep -c '^save: delete slot 1?' "$OUT/cr_save_del.log")" = 2 ] && has '^save: delete slot U01 rc 0' "$OUT/cr_save_del.log" &&
    ok "SAVE held 1 s: delete? (OCT- kept it), OCT+ deleted U01: $OUT/cr_save_delete.ppm" || bad "delete"
[ "$(grep -c '^expect led .*: ok' "$OUT/cr_save_del.log")" = 6 ] && ok "naming: the typing keys lit (F#4: delete), G#4 dark, OCT- lit" || bad "naming LEDs"

echo "the power-on splash: the idle stripes slide in, the name lands, the version under them"
run cr_splash
grep -qi 'error' "$OUT/cr_splash.log" && bad "cr_splash: an error in the log" || ok "cr_splash: no error in the log"
for t in 150 400 1000 1500; do [ -s "$OUT/cr_splash_$t.ppm" ] || bad "no shot $OUT/cr_splash_$t.ppm"; done
p=$(pixel "$OUT/cr_splash_150.ppm" 200 180); set -- $p     # the orange band, 150 ms: still sliding in
[ "${1:-255}" -lt 60 ] && ok "150 ms: the orange band not yet across ($p): $OUT/cr_splash_150.ppm" || bad "150 ms: orange band already in ($p)"
p=$(pixel "$OUT/cr_splash_400.ppm" 200 154); set -- $p
[ "${1:-0}" -gt 180 ] && [ "${2:-255}" -lt 120 ] && ok "400 ms: the red band in ($p): $OUT/cr_splash_400.ppm" || bad "400 ms: no red band ($p)"
p=$(pixel "$OUT/cr_splash_400.ppm" 200 206); set -- $p
[ "${1:-0}" -gt 200 ] && [ "${3:-0}" -gt 200 ] && ok "400 ms: the white band in ($p)" || bad "400 ms: no white band ($p)"
differ cr_splash_150 cr_splash_400 "the stripes moved between 150 and 400 ms"
differ cr_splash_400 cr_splash_1500 "the name landed and the idle stripes go on: $OUT/cr_splash_1500.ppm"

echo "MIDI in: clock In sets the tempo, program change, CC 7 / 91, notes on the CHORD channel"
run cr_midi_in
ML="$OUT/cr_midi_in.log"
has '^bpm: 100 (clock in)' "$ML" && ok "clock in at 25 ms a pulse: 100 BPM" || bad "clock in: no 100 BPM"
[ "$(grep '^bpm:' "$ML" | tail -1)" = "bpm: 140 (clock in)" ] && has 'bpm 140 view' "$ML" &&
    ok "140 BPM pulses: the tempo (and the screen's) follow; a SELECT turn is re-asserted" || bad "clock in: not 140"
o=$(grep 'parts busy' "$ML" | sed -n 's/.* out \([0-9]*\).*/\1/p' | sort -u | wc -l | tr -d ' ')
[ "$o" = 1 ] && ok "clock In: nothing sent out" || bad "clock In: MIDI out moved"
has '^sound: part 0 pos 3 .*(program change)' "$ML" && has '^sound: part 1 pos 2 .*(program change)' "$ML" &&
    [ "$(grep -c '(program change)' "$ML")" = 2 ] && ok "program change: chord sound 3, bass sound 2, 127 ignored" || bad "program change"
has 'part 0: .*level 64' "$ML" && has 'part 1: .*level 32' "$ML" && has '^send: part 0 Reverb 80' "$ML" &&
    ok "CC 7 on channels 1 / 2: the parts' levels; CC 91: part 0's reverb" || bad "CC 7 / 91"
has 'expect sound .*: ok' "$ML" && has 'part 0: .*voices 1' "$ML" && ok "a note on channel 1 plays part 0" || bad "note in"
silent_end cr_midi_in

echo "SCOPE view: HOME x4, a held chord as one bold line; silence a flat line"
run cr_scope
SL="$OUT/cr_scope.log"
has 'expect sound .*: ok' "$SL" && ok "the chord sounds in the SCOPE view" || bad "SCOPE: no sound"
p=$(pixel "$OUT/cr_scope_flat.ppm" 60 134); set -- $p
[ "${1:-0}" -gt 200 ] && [ "${3:-0}" -gt 200 ] && ok "quiet: the trace is flat on the centre line ($p): $OUT/cr_scope_flat.ppm" || bad "quiet: no flat line ($p)"
w=$(od -An -tu1 -v -j $((15 + 60 * 240 * 3)) -N$((60 * 240 * 3)) "$OUT/cr_scope.ppm" | tr -s ' \n' '\n\n' | grep -v '^$' |
    awk '{ v[n++] = $1 } END { c = 0; for (i = 0; i + 2 < n; i += 3) if (v[i] > 200 && v[i+1] > 200 && v[i+2] > 200) c++; print c }')
[ "${w:-0}" -gt 100 ] && ok "sounding: the trace swings above the centre ($w white px in rows 60..119): $OUT/cr_scope.ppm" || bad "sounding: no trace ($w)"
differ cr_scope cr_scope_flat "the scope is live (sounding != quiet)"
silent_end cr_scope

echo "Calibration: Options > Calibrate, OCT+; each button pressed, each knob turned, OCT+ keeps"
run cr_calib
CL="$OUT/cr_calib.log"
[ "$(grep -c '^calib: .* = ' "$CL")" = 21 ] && ok "21 steps taught (14 buttons, 7 knobs)" || bad "calib: not 21 steps"
has '^calib: FX = button 2' "$CL" && has '^calib: OCT+ = button 1' "$CL" && has '^calib: PRESETS = encoder 6 dir 1' "$CL" &&
    has '^calib: KNOB 4 = encoder 5 dir 1' "$CL" && ok "the table taught = the emulator's (panel.c PANEL_DEFAULT)" || bad "calib: wrong table"
has '^calib: saved' "$CL" && ok "OCT+ kept it (settings_save)" || bad "calib: not saved"
differ cr_calib_01 cr_calib_15 "a step at a time: $OUT/cr_calib_01.ppm, _14, _15, _21, _done, _saved"
p=$(pixel "$OUT/cr_calib_done.ppm" 120 8); set -- $p
[ "${1:-0}" -gt 200 ] && [ "${3:-255}" -lt 90 ] && ok "done: the ring full, yellow ($p)" || bad "done: no full ring ($p)"
has 'expect sound .*: ok' "$CL" && ok "a chord plays after the calibration" || bad "calib: no sound after"
silent_end cr_calib

echo "MIDI start / continue / stop in (Clock In) drive the loop; ignored with Clock Out"
run cr_midi_transport
ML="$OUT/cr_midi_transport.log"
[ "$(grep -c '^expect led GREEN .*: ok' "$ML")" = 9 ] &&
    ok "LOOP stop, FC nothing, FA plays, FC stops, FB plays, FA restarts (still playing), FC stops; Out: FA ignored" ||
    bad "MIDI transport in: $(grep -c '^expect led GREEN .*: ok' "$ML") of 9 GREEN checks"
silent_end cr_midi_transport

echo "VA (eng_va.c, docs/VA.md): a bank row, a held 6-note chord, a chord change"
rm -f "$OUT"/va_chord*
run va_chord --wav "$OUT/va_chord.wav"
has 'part 0: VA / LUSH PAD' "$OUT/va_chord.log" && ok "PRESETS +24: LUSH PAD on the VA" || bad "va_chord: not the VA's LUSH PAD"
[ "$(grep -c '^expect .*: ok' "$OUT/va_chord.log")" = 3 ] && ok "va_chord: sound held, the tails, then silence" ||
    bad "va_chord: $(grep -c '^expect .*: ok' "$OUT/va_chord.log") of 3 expectations"
wc=$(python3 tools/emu/wavclicks.py "$OUT/va_chord.wav" --from 0.4 | tail -1)
echo "$wc" | grep -q ': 0 jumps > 0.5 FS, 0 silent holes mid-sound, 0 clicks' && ok "va_chord: $wc" || bad "va_chord: $wc"
[ -s "$OUT/va_chord_held.ppm" ] && ok "screenshot: $OUT/va_chord_held.ppm" || bad "no va_chord_held.ppm"

echo "all-synth: the rows that were sample-based (PIANO, CLOUD PAD, SHIMMER) are synth sounds and play"
run cr_allsynth
AL="$OUT/cr_allsynth.log"
has 'part 0: FM6 / PIANO' "$AL" && has 'part 0: VA / CLOUD PAD' "$AL" && has 'part 0: VA / SHIMMER' "$AL" &&
    ok "PRESETS 03 PIANO on FM6, 15 CLOUD PAD and 16 SHIMMER on the VA" || bad "all-synth rows: $(grep 'part 0:' "$AL" | tr '\n' ' ')"
[ "$(grep -c '^expect .*: ok' "$AL")" = 5 ] && ok "all-synth: each sounds, SHIMMER's long tail, then silence: $OUT/cr_allsynth_piano.ppm" ||
    bad "all-synth: $(grep -c '^expect .*: ok' "$AL") of 5 expectations"

echo "FM6: Melodee's engine, the deep pages, the dx band"
run cr_fm6 --wav "$OUT/cr_fm6.wav"
FL="$OUT/cr_fm6.log"
has 'part 0: FM6 / TINE EP' "$FL" && ok "the power-on chord sound: FM6 / TINE EP" || bad "not TINE EP: $(grep -m1 'part 0:' "$FL")"
has '^deep: part 0 page 1 OP 2 col 0 LEVEL [0-9]* -> 99 ' "$FL" && ok "OSC stack, OP 2 (SELECT +1), KNOB 1: $(grep -m1 '^deep: part 0 page 1 OP 2' "$FL")" ||
    bad "no OP 2 LEVEL edit: $(grep -m1 '^deep:' "$FL")"
[ "$(grep -c '^expect sound .*: ok' "$FL")" = 2 ] && ok "the chord sounds before and after the edit" || bad "FM6 chord: $(grep -c '^expect sound .*: ok' "$FL") of 2"
br=$(python3 - "$OUT/cr_fm6.wav" <<'PY'
import sys, wave, struct
w = wave.open(sys.argv[1]); fs = w.getframerate(); ch = w.getnchannels()
x = struct.unpack("<%dh" % (w.getnframes() * ch), w.readframes(w.getnframes()))
m = [x[i] for i in range(0, len(x), ch)]
def bright(t0, t1):          # high-frequency share: the first difference's energy over the signal's
    s = m[int(t0 * fs):int(t1 * fs)]
    e = sum(v * v for v in s) or 1
    d = sum((s[i] - s[i - 1]) ** 2 for i in range(1, len(s)))
    return d / e
b0, b1 = bright(1.16, 2.36), bright(7.07, 8.27)   # (the notes at 1.06 s and 6.97 s: the same 1.2 s after each)
print("%.4f %.4f %d" % (b0, b1, b1 > 1.25 * b0))
PY
)
set -- $br
[ "${3:-0}" = 1 ] && ok "OP 2 LEVEL 99 changes the sound: brightness (HF share) $1 -> $2 (the modulator up)" || bad "the sound did not change: $br"
white=$(od -An -tu1 -v -j $((15 + 30 * 240 * 3)) -N$((240 * 70 * 3)) "$OUT/cr_fm6_env.ppm" | tr -s ' \n' '\n\n' | grep -v '^$' |
    awk '{ v[n++] = $1 } END { c = 0; for (i = 0; i + 2 < n; i += 3) if (v[i] > 200 && v[i+1] > 200 && v[i+2] > 200) c++; print c }')
orange() { od -An -tu1 -v -j $((15 + 30 * 240 * 3)) -N$((240 * 70 * 3)) "$1" | tr -s ' \n' '\n\n' | grep -v '^$' |
    awk '{ v[n++] = $1 } END { c = 0; for (i = 0; i + 2 < n; i += 3) if (v[i] > 200 && v[i+1] > 80 && v[i+1] < 170 && v[i+2] < 90) c++; print c }'; }
o0=$(orange "$OUT/cr_fm6_env.ppm"); o1=$(orange "$OUT/cr_fm6_env_hot.ppm")
[ "${white:-0}" -gt 300 ] && ok "ENV 1: the dx band drawn ($white white pixels in the band): $OUT/cr_fm6_env.ppm" || bad "no dx band: $white"
[ "${o1:-0}" -gt $((${o0:-0} + 100)) ] && ok "KNOB 2 (R2): its segment lit orange ($o0 -> $o1 pixels): $OUT/cr_fm6_env_hot.ppm" || bad "no lit segment: $o0 -> $o1"
has '^deep: part 0 page 19 ENV 1 col 1 R2 ' "$FL" && has '^edit: group ENV screen 2 ' "$FL" && has '^edit: group FILT screen 1 ' "$FL" &&
    has '^edit: group LFO screen 1 ' "$FL" && has '^edit: group MOD screen 1 ' "$FL" && has '^edit: group OSC screen 3 ' "$FL" &&
    ok "FM6's groups: ENV 1 / ENV 2, FILT (ALGO), LFO, MOD (FUNC), OSC screens 1..3 (OP n, OP n+, SCALE n)" || bad "FM6 groups: $(grep -c '^edit: group' "$FL")"

echo "determinism"
mkdir -p "$OUT/cr_again"
cp "$OUT/cr_dmaj.ppm" "$OUT/cr_again/first.ppm"
"$EMU" --headless --script "$S/cr_dmaj.txt" --wav "$OUT/cr_again/cr_dmaj.wav" >/dev/null 2>&1
cmp -s "$OUT/cr_dmaj.wav" "$OUT/cr_again/cr_dmaj.wav" && ok "the same audio on a second run" || bad "audio differs"
cmp -s "$OUT/cr_dmaj.ppm" "$OUT/cr_again/first.ppm" && ok "the same LCD on a second run" || bad "LCD differs"

[ $fail -eq 0 ] && echo "PASS" || echo "FAIL"
exit $fail
