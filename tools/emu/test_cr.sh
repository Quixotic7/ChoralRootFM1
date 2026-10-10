#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Headless acceptance of ChoralRoot on the FM-1 emulator (no window, no audio device, deterministic):
#   sh tools/emu/test_cr.sh        builds build/host/emu when a source is newer, runs tools/emu/scripts/cr_*.txt
# Checks (docs/INTEGRATION.md section 9, steps 1-2): MAJ + D4 sounds D major on part 0 with the D4 F#4 A4 LEDs lit
# and the chord screen; KNOB 1 +2 revoices it (A4 D5 F#5) with the voicing line; KEY tap: Key Mode, its LED,
# "Key: C", D4 alone -> Dm; KEY held: select-key, a root sets the tonic; PERF held: the Perform picker, SELECT
# picks the mode (the roots play there); PRESETS: the sound meter and another timbre; ALGORITHM: the bass on, a bass
# note on part 1; presets per engine (cr_presets: docs/PRESETS.md);
# End: PANIC (the red screen); 3 min without input: the stripes (the screensaver), a key dismisses it; 10 min: the
# screen off (cr_screen_off: a key or a knob wakes it and acts, a loop playing does not keep it on); after every release, parts 0 and 1 silent within 2 s.
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
px_count() {     # px_count FILE X0 Y0 X1 Y1 red|ink: pixels of the box [X0,X1) x [Y0,Y1) in red / not the background
    od -An -tu1 -v -j 15 "$1" | awk -v x0="$2" -v y0="$3" -v x1="$4" -v y1="$5" -v k="$6" '
        { for (i = 1; i <= NF; i++) v[n++] = $i }
        END { c = 0; for (y = y0; y < y1; y++) for (x = x0; x < x1; x++) { o = (y * 240 + x) * 3; r = v[o]; g = v[o+1]; b = v[o+2]
              if (k == "red" ? (r > 180 && g < 100 && b < 100) : (r + g + b > 90)) c++ } print c }'
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
differ cr_perf_picker cr_perf_arp "SELECT moved the picker: $OUT/cr_perf_picker.ppm -> cr_perf_arp.ppm"
has 'perform 1 mode 2' "$OUT/cr_perf.log" && ok "SELECT +3 in the layer: Arpeggiate, performance on" || bad "mode not chosen"
has 'chord Am7 sounding 1' "$OUT/cr_perf.log" && ok "Am7 arpeggiated (MIN + m7 + A4)" || bad "no Am7"
silent_end cr_perf

echo "PRESETS: the sound"
run cr_sound --wav "$OUT/cr_sound.wav"
run cr_sound_ref --wav "$OUT/cr_sound_ref.wav"
has 'part 0: FM6 / FM BELL' "$OUT/cr_sound.log" && ok "PRESETS +1: part 0 loads the next preset of FM6's pool (TINE EP -> FM BELL)" \
                                                || bad "no sound change"
[ -s "$OUT/cr_sound_meter.ppm" ] && ok "the sound meter: $OUT/cr_sound_meter.ppm" || bad "no meter screenshot"
a=$(num "non-zero samples" "$OUT/cr_sound.log"); b=$(num "non-zero samples" "$OUT/cr_sound_ref.log")
cmp -s "$OUT/cr_sound.wav" "$OUT/cr_sound_ref.wav" && bad "the same audio with another sound" \
    || ok "the same chord, another timbre: the audio differs ($a / $b non-zero samples)"

echo "ALGORITHM: the bass"
run cr_bass
has 'bass 1 (sound 18)' "$OUT/cr_bass.log" && has 'part 1: VA / DEEP SUB' "$OUT/cr_bass.log" &&
    ok "ALGORITHM +18: bass on (OFF, then the VA's pool: 17 DEEP SUB)" || bad "bass not on"
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
has '^bass: on (sound 18, Chords Only)' "$L" && has 'expect led ENV on .*: ok' "$L" &&
    ok "BASS tapped: the bass on (DEEP SUB, Chords Only), its LED lit" || bad "BASS tap: $(grep '^bass:' "$L" | head -1)"
[ "$(pv 0 1)" = 3 ] && [ "$(pv 1 1)" = 0 ] && ok "before: MAJ + D4 on part 0 only (3 voices)" || bad "before: part 0 $(pv 0 1) part 1 $(pv 1 1)"
[ "$(pv 0 2)" = 3 ] && [ "$(pv 1 2)" = 1 ] && ok "bass on: MAJ + D4 sounds on part 0 (3 voices) AND part 1 (the bass note)" \
    || bad "bass on: part 0 $(pv 0 2) part 1 $(pv 1 2) voices (the chord part must keep sounding)"
[ "$(pv 0 3)" = 3 ] && has 'part 1: VA / PUNCH BASS' "$L" && ok "ALGORITHM +1 with the chord held: another bass sound, part 0 still 3 voices" \
    || bad "a bass sound change with the chord held: part 0 $(pv 0 3)"
has '^bass: off' "$L" && has 'expect led ENV dim .*: ok' "$L" && [ "$(pv 0 4)" = 3 ] && [ "$(pv 1 4)" = 0 ] &&
    ok "BASS tapped off: its LED back to the glow, the chord on part 0 only" || bad "bass off: part 0 $(pv 0 4) part 1 $(pv 1 4)"
o=$(orange_in "$OUT/cr_bass_both_pop.ppm"); n=$(orange_in "$OUT/cr_bass_both_on.ppm"); f=$(orange_in "$OUT/cr_bass_both_off.ppm")
differ cr_bass_both_pop cr_bass_both_on "BASS tap: the bass meter popup (17 DEEP SUB, orange): $OUT/cr_bass_both_pop.ppm"
[ "$n" -gt 40 ] && [ "$f" = 0 ] && ok "\"Bass\" in orange top right while the bass is on ($n px; off: $f): $OUT/cr_bass_both_on.ppm" \
    || bad "the Bass status: on $n off $f orange px"
has '^bass: behaviour Solo' "$L" && [ "$(pv 0 5)" = 0 ] && [ "$(pv 1 5)" = 1 ] &&
    ok "Bass Behaviour Solo (the BASS layer, G4): the bass alone, part 0 silent by design" || bad "Solo: part 0 $(pv 0 5) part 1 $(pv 1 5)"
s=$(orange_in "$OUT/cr_bass_both_solo.ppm")
[ "$s" -gt "$n" ] && ok "Solo shown as \"Bass Solo\" in orange ($s px): $OUT/cr_bass_both_solo.ppm" || bad "no Bass Solo status ($s px)"
has '^bass: behaviour Chords Only' "$L" && [ "$(pv 0 6)" = 3 ] && [ "$(pv 1 6)" = 1 ] &&
    ok "Chords Only again (D4 in the layer): both parts sound" || bad "Chords Only: part 0 $(pv 0 6) part 1 $(pv 1 6)"
silent_end cr_bass_both

echo "End: PANIC; idle: the stripes"
run cr_panic
p=$(pixel "$OUT/cr_panic.ppm" 120 200); set -- $p
[ "${1:-0}" -gt 180 ] && [ "${2:-255}" -lt 90 ] && [ "${3:-255}" -lt 90 ] && ok "the red PANIC screen ($p): $OUT/cr_panic.ppm" \
                                                                         || bad "not red ($p)"
silent_end cr_panic
p=$(pixel "$OUT/cr_idle_none.ppm" 120 150); set -- $p
[ "${1:-0}" -gt 180 ] && [ "${2:-255}" -lt 90 ] && bad "stripes after 4 s without input ($p): $OUT/cr_idle_none.ppm" \
                                                 || ok "4 s without input: the view stays, no stripes ($OUT/cr_idle_none.ppm)"
p=$(pixel "$OUT/cr_idle_again.ppm" 120 150); set -- $p
[ "${1:-0}" -gt 180 ] && [ "${2:-255}" -lt 90 ] && ok "3 min without input: the screensaver, the stripes ($OUT/cr_idle_again.ppm)" \
                                                 || bad "no stripes after 3 min without input ($p)"
p=$(pixel "$OUT/cr_idle_woke.ppm" 120 150); set -- $p
[ "${1:-0}" -gt 180 ] && [ "${2:-255}" -lt 90 ] && bad "a key did not dismiss the screensaver ($p)" \
                                                 || ok "a key: the screensaver gone at once ($OUT/cr_idle_woke.ppm)"

echo "the screen off: the logo at 3 min, the screen off at 10 min; a key / a knob wakes it and acts; a loop is not input"
run cr_screen_off
SO="$OUT/cr_screen_off.log"
lit() { od -An -tu1 -v -j 15 "$1" | tr -s ' \n' '\n\n' | grep -v '^$' | awk '$1 > 24 { c++ } END { print c + 0 }'; }   # bytes > 24
p=$(pixel "$OUT/cr_off_logo.ppm" 120 150); set -- $p
[ "${1:-0}" -gt 180 ] && [ "${2:-255}" -lt 90 ] && ok "3 min: the logo ($p): $OUT/cr_off_logo.ppm" || bad "no logo at 3 min ($p)"
[ "$(lit "$OUT/cr_off_dark.ppm")" = 0 ] && [ "$(lit "$OUT/cr_off_view.ppm")" -gt 1000 ] &&
    ok "10 min: the screen off, the panel black: $OUT/cr_off_dark.ppm" || bad "10 min: the panel not black ($(lit "$OUT/cr_off_dark.ppm") lit bytes)"
w=$(grep '^  lcd:' "$SO" | sed -n 's/.*screen \([a-z]*\), \([0-9]*\) writes.*/\1 \2/p')
set -- $w
[ "$1" = off ] && [ "$3" = off ] && [ "$2" = "$4" ] && has '^screen: off (idle)' "$SO" &&
    ok "the backlight off, no LCD write in the minute after ($2 = $4 writes)" || bad "LCD writes while off: $w"
[ "$5" = on ] && [ "$(lit "$OUT/cr_off_key.ppm")" -gt 1000 ] && ! grep -q 'FAILED' "$SO" &&
    [ "$(grep -A4 'screenshot: build/emu/test/cr_off_key.ppm' "$SO" | grep -c 'part 0: .*voices 3')" = 1 ] &&
    ok "a key: the screen on, the view back, the chord sounding: $OUT/cr_off_key.ppm" || bad "a key did not wake it and play ($5)"
[ "$7" = off ] && [ "$9" = on ] && [ "$(lit "$OUT/cr_off_knob.ppm")" -gt 1000 ] &&
    [ "$(grep -A8 'screenshot: build/emu/test/cr_off_knob.ppm' "$SO" | grep -c 'voicing 2 ')" = 1 ] &&
    ok "10 min more: off; a knob turn: on, and KNOB 1 turned (voicing 2): $OUT/cr_off_knob.ppm" || bad "a knob did not wake it / act ($7 $9)"
[ "$(lit "$OUT/cr_off_loop.ppm")" = 0 ] && [ "${13}" = off ] && grep '^  loop:' "$SO" | tail -1 | grep -q 'state 2 ' &&
    [ "$(grep -c '^expect sound.*: ok' "$SO")" = 5 ] &&
    ok "a loop playing alone: the screen off after 10 min, the loop still playing: $OUT/cr_off_loop.ppm" || bad "loop: ${13}"

echo "the other screens"
run cr_layers
for x in options options_sel fx_layer bass_layer engine_picker loop_layer bpm fx_amount view_keyboard view_notes view_geek; do
    [ -s "$OUT/cr_$x.ppm" ] || bad "no cr_$x.ppm"
done
differ cr_options cr_options_sel "Options: SELECT and KNOB 1 changed it"
differ cr_fx_layer cr_bass_layer "FX / BASS layers differ"
differ cr_engine_picker cr_loop_layer "EDIT / LOOP layers differ"
silent_end cr_layers

echo "the fx layer: a knob row (the effect over KNOB 1..4's cells), a turned cell hot, no popup"
export EMU_UI_LOG=0
run cr_fx_row
unset EMU_UI_LOG
L="$OUT/cr_fx_row.log"
has '^fx: cells Size Damp Type Amount' "$L" && ok "FX held: Reverb over Size / Damp / Type / Amount: $OUT/cr_fx_row.ppm" || bad "no Reverb cells"
has '^fx: knob 2 Reverb damp' "$L" && ok "KNOB 2: Reverb's damp ($(grep '^fx: knob 2' "$L" | head -1 | sed 's/^fx: //'))" || bad "KNOB 2: no damp change"
fr=$(grep -c '^ui: frame' "$L"); kr=$(grep -c '^ui: frame.*device): knobrow ' "$L"); mt=$(grep -c '^ui: frame.*device): meter ' "$L")
[ "$mt" = 0 ] && [ "$kr" -gt 100 ] && ok "no popup: every frame in the layer a knob row ($kr knobrow frames of $fr, 0 meters)" \
    || bad "a popup in the fx layer ($mt meter frames, $kr knobrow)"
grep -q '^ui: frame.*device): knobrow .*hot 1\.1$' "$L" && grep -q '^ui: frame.*device): knobrow .*hot 1\.3$' "$L" &&
    ok "the turned cell hot (KNOB 2: cell 1, KNOB 4: cell 3)" || bad "no hot cell in the trace"
h=$(px_count "$OUT/cr_fx_row_hot.ppm" 64 176 116 196 ink); c=$(px_count "$OUT/cr_fx_row_cool.ppm" 64 176 116 196 ink)
[ "$h" -gt $((c + 300)) ] && ok "the hot block behind Damp's value ($h ink px), gone 900 ms later ($c): $OUT/cr_fx_row_hot.ppm" \
    || bad "hot block: $h px hot, $c after"
has '^fx: cells Time Feedback Colour Amount' "$L" && has '^fx: cells Rate Depth - Amount' "$L" && has '^fx: cells - - - Amount' "$L" &&
    ok "SELECT picks the effect, the cells follow (Delay, Chorus with a dash, Drive the amount only): $OUT/cr_fx_row_delay.ppm" || bad "the cells did not follow the effect"
has '^expect sound .*: ok' "$L" && has '^expect led D4 on .*: ok' "$L" &&
    ok "MAJ + D4 in the fx layer: the chord sounds, its LEDs are the chord's (the roots play there)" || bad "the roots in the fx layer: $(grep '^expect' "$L")"
differ cr_fx_row cr_fx_row_delay "Reverb / Delay rows differ"
has '^fx: knob 4 Delay amount' "$L" && differ cr_fx_row_delay cr_fx_row_amount "KNOB 4: Delay's amount cell changed (hot): $OUT/cr_fx_row_amount.ppm"
differ cr_fx_row_chorus cr_fx_row_drive_off "Drive with FX off (the amount reads off): $OUT/cr_fx_row_drive_off.ppm"

echo "the perform layer: a knob row (the mode over its four parameters), a turned cell hot, no popup"
export EMU_UI_LOG=0
run cr_perf_row
unset EMU_UI_LOG
L="$OUT/cr_perf_row.log"
has '^perf: cells Division Direction Gate Swing' "$L" && ok "PERF held, G4: Arpeggiate over Division / Direction / Gate / Swing: $OUT/cr_perf_row.ppm" || bad "no Arpeggiate cells"
has '^perf: knob Arp division' "$L" && ok "KNOB 1: the arp's division ($(grep '^perf: knob Arp' "$L" | tail -1 | sed 's/^perf: //'))" || bad "KNOB 1: no division change"
fr=$(grep -c '^ui: frame' "$L"); kr=$(grep -c '^ui: frame.*device): knobrow ' "$L"); mt=$(grep -c '^ui: frame.*device): meter ' "$L")
[ "$mt" = 0 ] && [ "$kr" -gt 100 ] && ok "no popup: every frame in the layer a knob row ($kr knobrow frames of $fr, 0 meters)" \
    || bad "a popup in the perform layer ($mt meter frames, $kr knobrow)"
grep -q '^ui: frame.*device): knobrow .*hot 1\.0$' "$L" && grep -q '^ui: frame.*device): knobrow .*hot 1\.2$' "$L" &&
    ok "the turned cell hot (KNOB 1: cell 0, KNOB 3: cell 2)" || bad "no hot cell in the trace"
differ cr_perf_row_hot cr_perf_row_cool "the hot block gone 900 ms later: $OUT/cr_perf_row_hot.ppm"
has '^perf: cells Rate Direction Range Hold' "$L" && has '^perf: cells Amount Rate Direction Range' "$L" && has '^perf: cells Pattern Division Gate Swing' "$L" &&
    ok "SELECT picks the mode, the cells follow (Strum, Slop, Pattern): $OUT/cr_perf_row_strum.ppm" || bad "the cells did not follow the mode"
has '^expect sound .*: ok' "$L" && ok "MAJ + D4 in the perform layer: the chord sounds (performed): $OUT/cr_perf_row_keys.ppm" || bad "the roots in the perform layer"
has '^perf: knob Slop direction' "$L" && ok "KNOB 3 in Slop: its direction" || bad "KNOB 3: no Slop direction"
differ cr_perf_row cr_perf_row_strum "Arpeggiate / Strum rows differ"

echo "perform on / off: PERF tap, OCT+ in the perform layer; the arp stops when off"
run cr_perf_toggle
L="$OUT/cr_perf_toggle.log"
pc() { grep '^  cr: key' "$L" | sed -n "$1p" | sed -n 's/.* perform \([0-9]\) .* layer \([0-9]*\) .*/\1 \2/p'; }
no() { grep '^  cr: voices' "$L" | sed -n "$1p" | sed -n 's/.* notes-out \([0-9]*\) .*/\1/p'; }
grep -q '^expect led ARP on at [0-9]* ms: ok' "$L" && grep -q '^expect led ARP dim at [0-9]* ms: ok' "$L" && [ "$(pc 1)" = "0 0" ] &&
    ok "PERF tap: perform on (LED lit, no message), tap again: off (LED dim): $OUT/cr_perf_on_msg.ppm" || bad "PERF tap: $(pc 1)"
[ "$(pc 2)" = "1 2" ] && [ "$(pc 3)" = "0 2" ] && [ "$(pc 4)" = "1 2" ] &&
    ok "the perform layer: SELECT picks Arpeggiate (on), OCT+ off / on, the layer stays open: $OUT/cr_perf_layer_off.ppm" \
    || bad "the perform layer: $(pc 2) / $(pc 3) / $(pc 4)"
differ cr_perf_layer_off cr_perf_layer_on "the layer's label follows on / off"
[ "$(no 6)" -gt "$(no 5)" ] && [ "$(no 8)" = "$(no 7)" ] && [ "$(pc 7)" = "0 0" ] &&
    ok "the arp steps while on ($(no 5) -> $(no 6) notes out), PERF tap off: the chord held plain ($(no 7) -> $(no 8))" \
    || bad "the arp on / off: notes out $(no 5) $(no 6) $(no 7) $(no 8)"

echo "the arp's Hold off by default: released keys stop it; LATCH holds it; PANIC stops; perform never swaps the view"
export EMU_UI_LOG=0
run cr_arp_hold
unset EMU_UI_LOG
L="$OUT/cr_arp_hold.log"
[ "$(no 3)" = "$(no 2)" ] && ok "arp, chord released: the notes out stop within 300 ms ($(no 2) -> $(no 3))" || bad "arp kept going after release: $(no 2) -> $(no 3)"
[ "$(no 5)" -gt "$(no 4)" ] && ok "LATCH + chord: the arp continues ($(no 4) -> $(no 5))" || bad "LATCH: no arp $(no 4) -> $(no 5)"
[ "$(no 7)" = "$(no 6)" ] && ok "PANIC: the latched arp stops ($(no 6) -> $(no 7))" || bad "PANIC: $(no 6) -> $(no 7)"
fl=$(grep '^ui: frame' "$L" | tail -1)
case "$fl" in *"keyboard view 1"*"right 'Arp'"*) ok "Keyboard view + PERF on + a chord: the keyboard view stays, the top line says Arp: $OUT/cr_perf_view_kbd.ppm";;
    *) bad "perform swapped the view: $fl";; esac
grep -q '^ui: frame.*device): arp ' "$L" && bad "an arp screen appeared" || ok "no arp screen in any frame"

echo "HOME held: the view menu; the view lock; HOME tap at home cycles only unlocked"
run cr_view_menu
L="$OUT/cr_view_menu.log"
vw() { grep '^  cr: key' "$L" | sed -n "$1p" | sed -n 's/.* view \([0-9]\) layer \([0-9]*\) .*/\1 \2/p'; }
[ "$(vw 1)" = "0 0" ] && [ "$(vw 2)" = "1 0" ] && ok "unlocked (the default): HOME tap at home, the next view" || bad "HOME tap: $(vw 1) / $(vw 2)"
[ "$(vw 3)" = "1 9" ] && has '^view: menu (Keyboard, lock off)' "$L" && ok "HOME held 600 ms: the view menu: $OUT/cr_view_menu.ppm" || bad "no view menu: $(vw 3)"
[ "$(vw 4)" = "2 9" ] && has '^view: lock on' "$L" && [ "$(vw 5)" = "2 0" ] &&
    ok "SELECT: Notes, KNOB 1: Lock On, OCT+: ok: $OUT/cr_view_menu_lock.ppm" || bad "the menu: $(vw 4) / $(vw 5)"
[ "$(vw 6)" = "2 0" ] && ok "locked: HOME tap keeps the view: $OUT/cr_view_locked_tap.ppm" || bad "locked: HOME tap changed the view: $(vw 6)"
[ "$(vw 7)" = "1 0" ] && [ "$(vw 8)" = "2 0" ] && has '^view: lock off' "$L" &&
    ok "the menu again: E4 (the second white root) Keyboard, KNOB 1 -1 unlocked, HOME closes; HOME tap cycles again" \
    || bad "unlock: $(vw 7) / $(vw 8)"

echo "the bass layer: a knob row (behaviour, register, sound, level), a turned cell hot, no popup"
export EMU_UI_LOG=0
run cr_bass_row
unset EMU_UI_LOG
L="$OUT/cr_bass_row.log"
has '^bass: cells Behaviour Register Sound Level' "$L" && ok "BASS held: Behaviour / Register / Sound / Level: $OUT/cr_bass_row.ppm" || bad "no bass cells"
has '^bass: knob 4 level' "$L" && has '^bass: knob 2 register' "$L" && has '^bass: knob 3 sound' "$L" &&
    ok "KNOB 4 level, KNOB 2 register, KNOB 3 sound ($(grep '^bass: knob 4' "$L" | tail -1 | sed 's/^bass: //'))" || bad "bass knobs: $(grep '^bass: knob' "$L" | tr '\n' ' ')"
fr=$(grep -c '^ui: frame' "$L"); kr=$(grep -c '^ui: frame.*device): knobrow ' "$L"); mt=$(grep -c '^ui: frame.*device): meter ' "$L")
[ "$mt" = 0 ] && [ "$kr" -gt 100 ] && ok "no popup: every frame in the layer a knob row ($kr knobrow frames of $fr, 0 meters)" \
    || bad "a popup in the bass layer ($mt meter frames, $kr knobrow)"
grep -q '^ui: frame.*device): knobrow .*hot 1\.3$' "$L" && grep -q '^ui: frame.*device): knobrow .*hot 1\.1$' "$L" &&
    ok "the turned cell hot (KNOB 4: cell 3, KNOB 2: cell 1)" || bad "no hot cell in the trace"
differ cr_bass_row_hot cr_bass_row_cool "the hot block gone 900 ms later: $OUT/cr_bass_row_hot.ppm"
differ cr_bass_row_cool cr_bass_row_reg "KNOB 2: the register cell changed: $OUT/cr_bass_row_reg.ppm"
differ cr_bass_row_reg cr_bass_row_sound "KNOB 3: the sound cell changed: $OUT/cr_bass_row_sound.ppm"
has '^bass: behaviour Unison Bass' "$L" && has '^bass: behaviour Bass Single Notes' "$L" &&
    ok "KNOB 1 and F4: the behaviour (Unison, Single): $OUT/cr_bass_row_unison.ppm" || bad "behaviour: $(grep '^bass: behaviour' "$L" | tr '\n' ' ')"

echo "KEY / LOOP / METRO held: knob rows (KEY: the keyboard as the band; LOOP: no ring)"
export EMU_UI_LOG=0
run cr_key_row
L="$OUT/cr_key_row.log"
has '^key: cells Tonic Scale Transpose Single' "$L" && ok "KEY held: the keyboard over Tonic / Scale / Transpose / Single: $OUT/cr_key_row.ppm" || bad "no key cells"
y=0; for row in 70 80 90; do y=$((y + $(yellow_in "$OUT/cr_key_row_e.ppm" $row 0 240))); done
[ "$y" -gt 5 ] && ok "E4: the tonic lit yellow in the keyboard band ($y px): $OUT/cr_key_row_e.ppm" || bad "no yellow key in the band ($y)"
has '^key: knob 3 transpose 5' "$L" && has '^key: knob 2 scale minor' "$L" && has '^key: knob 4 single split' "$L" &&
    ok "KNOB 3 +5, KNOB 2 minor, KNOB 4 split" || bad "key knobs: $(grep '^key: knob' "$L" | tr '\n' ' ')"
mt=$(grep -c '^ui: frame.*device): meter ' "$L"); kr=$(grep -c '^ui: frame.*device): knobrow ' "$L")
[ "$mt" = 0 ] && [ "$kr" -gt 50 ] && ok "no popup in the key layer ($kr knobrow frames)" || bad "a popup in the key layer ($mt meters, $kr knobrow)"
grep -q '^ui: frame.*device): knobrow .*hot 1\.2$' "$L" && ok "KNOB 3: the Transpose cell hot" || bad "no hot Transpose"
differ cr_key_row_hot cr_key_row_cool "the hot block gone 900 ms later: $OUT/cr_key_row_hot.ppm"
differ cr_key_row_minor cr_key_row_e "E4: the lit key moved: $OUT/cr_key_row_e.ppm"
run cr_loop_row
L="$OUT/cr_loop_row.log"
has '^loop: cells Quantize Count-in Level Keys' "$L" && ok "LOOP held, stopped: the PLAY menu over Quantize / Count-in / Level / Keys: $OUT/cr_loop_row.ppm" || bad "no loop cells"
has '^loop: knob 1 quantize' "$L" && has '^loop: knob 2 count-in' "$L" && has '^loop: knob 3 level' "$L" &&
    ok "KNOB 1..3: quantize, count-in, level" || bad "loop knobs: $(grep '^loop: knob' "$L" | tr '\n' ' ')"
grep -q '^ui: frame.*device): loopmenu .*hot 1\.0$' "$L" && grep -q '^ui: frame.*device): loopmenu .*hot 1\.1$' "$L" &&
    grep -q '^ui: frame.*device): loopmenu .*hot 1\.2$' "$L" && ok "the turned cells hot (1, 2, 3)" || bad "loop: no hot cells"
mt=$(grep -c '^ui: frame.*device): meter ' "$L")
[ "$mt" = 0 ] && ok "no popup in the PLAY menu" || bad "a popup in the PLAY menu ($mt meters)"
differ cr_loop_row cr_loop_row_quant "KNOB 1: the quantize cell: $OUT/cr_loop_row_quant.ppm"
differ cr_loop_row_quant cr_loop_row_countin "KNOB 2: the count-in cell: $OUT/cr_loop_row_countin.ppm"
differ cr_loop_row_countin cr_loop_row_level "KNOB 3: the level cell: $OUT/cr_loop_row_level.ppm"
grep -q "^ui: frame.*device): loopmenu .* lm 1/1/0/[0-9]* lanes 1/0 " "$L" &&
    ok "LOOP held while it plays: the timeline in the middle (1 bar, 1 lane): $OUT/cr_loop_row_playing.ppm" || bad "no timeline while playing"
for f in cr_loop_row cr_loop_row_playing; do
    re=$(px_count "$OUT/$f.ppm" 2 100 14 140 ink); rd=$(px_count "$OUT/$f.ppm" 218 0 240 26 red)
    case $f in
    *playing) [ "$re" = 0 ] && [ "$rd" -gt 20 ] && ok "playing: no ring, the dial ($rd red): $OUT/$f.ppm" || bad "$f: ring band $re, dial $rd";;
    *) [ "$re" = 0 ] && [ "$rd" = 0 ] && ok "stopped: no ring, no dial: $OUT/$f.ppm" || bad "$f: ring band $re, dial $rd";;
    esac
done
grep -q '^loop: cells' "$L" && differ cr_loop_row_playing cr_loop_row_playing_level "playing, KNOB 3: the level cell hot: $OUT/cr_loop_row_playing_level.ppm"
run cr_metro_row
L="$OUT/cr_metro_row.log"
has '^metro: cells Click - - -' "$L" && ok "METRO held: the time signature over Click: $OUT/cr_metro_row.ppm" || bad "no metro cells"
has '^metro: knob 1 click' "$L" && grep -q '^ui: frame.*device): knobrow .*hot 1\.0$' "$L" && ok "KNOB 1: the click level, its cell hot" || bad "metro knob"
mt=$(grep -c '^ui: frame.*device): meter ' "$L")
[ "$mt" = 0 ] && ok "no popup in the metro layer" || bad "a popup in the metro layer ($mt meters)"
differ cr_metro_row_hot cr_metro_row_cool "the hot block gone: $OUT/cr_metro_row_cool.ppm"
differ cr_metro_row_cool cr_metro_row_34 "SELECT: 3/4: $OUT/cr_metro_row_34.ppm"
unset EMU_UI_LOG

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
differ cr_editor_save cr_editor_back "SAVE: the save dialog over the editor ($OUT/cr_editor_save.ppm), OCT-: cancelled back to it"
cmp -s "$OUT/cr_editor_back.ppm" "$OUT/cr_editor_again.ppm" && ok "OCT-, HOME, EDIT: back in MIX row B (the group, screen and lane remembered)" \
    || bad "the editor not as left: $OUT/cr_editor_back.ppm / $OUT/cr_editor_again.ppm"
[ "$(grep -c '^edit: group MIX screen 1 lane 2 part 0' "$L")" -ge 3 ] && ok "the trace: MIX row B after SHIFT + EDIT and after EDIT again" || bad "group memory"
has '^edit: close' "$L" && differ cr_editor_home cr_editor_again "HOME left the editor: $OUT/cr_editor_home.ppm"
has '^save: part 0 overwrite slot U01 VA 01 LUSH PAD rc 0' "$L" && ok "SAVE SAVE (edited: Overwrite): LUSH PAD bound in U01 from the editor: $OUT/cr_editor_saved.ppm" || bad "SAVE SAVE: $(grep '^save:' "$L")"
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
has '^mod: not modulatable$' "$L" && ok "a platform engine (PHASE): \"not modulatable\"" || bad "not modulatable"
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
has '^param: part 0 ENV ATK 70 -> 112 ' "$L" && ok "KNOB 1 +7 (6 a detent): ATK 70 -> 112" || bad "KNOB 1: ATK not changed"
grep -q '^split\|split point' "$L" && bad "SHIFT + KNOB reached the split point" || true
has '^param: part 0 LFO WAVE 0 -> 1 ' "$L" && ok "LFO WAVE steps by one" || bad "WAVE: $(grep 'LFO WAVE' "$L")"
has '^param: part 0 MOD FLT ' "$L" && ok "the platform MOD (4 slots): KNOB 3 the amount: $OUT/cr_editor_p_mod.ppm" || bad "platform MOD"
grep '^param: part 0 MIX VCE' "$L" | grep -q -- '\([0-9]*\) -> ' && ok "MIX VOICE: $(grep '^param: part 0 MIX VCE' "$L" | head -1 | sed 's/^param: //')" || bad "no VOICE step"
cmp -s "$OUT/cr_editor_steps.wav" "$OUT/cr_editor_ref.wav" && bad "the same audio with another envelope" \
    || ok "the same chord with ATK 112 / DEC 127: the audio differs ($(num "non-zero samples" "$L") / $(num "non-zero samples" "$OUT/cr_editor_ref.log") non-zero samples)"
silent_end cr_editor_steps

echo "Shift + Up / Down: the firmware's fine mode (GLO held around the detent), Page Down to KNOB 1"
run cr_fine
L="$OUT/cr_fine.log"
d=$(grep '^deep: .* CUT ' "$L" | sed -E 's/.* CUT ([0-9]+) -> ([0-9]+).*/\1 \2/' | awk '{printf "%d ", $2 - $1}')
[ "$d" = "6 6 1 1 6 " ] && ok "CUT steps plain / Shift / plain: $d(fine = 1, the SHIFT latch untouched)" \
    || bad "fine steps: CUT deltas '$d' (want 6 6 1 1 6)"

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
    has '^engine: part 0 -> FM6' "$L" && [ "$c2" = "$o1" ] &&
    ok "KNOB 4: the roots play (D4 sounded, no engine), again: engines (D4: FM6); OCT-: the VA patch back (crc $c2): $OUT/cr_pick_roots_play.ppm" \
    || bad "picker roots / engine cancel (crc '$c2')"
[ "$(grep -c '^engine: part 0' "$L")" = 1 ] && ok "the roots off: no engine switched by D4" || bad "roots off switched an engine"
grep '^layer: open 5' "$L" | sed -n 3p | grep -q . && has '^picker: keep part 0 ' "$L" &&
    ok "PRESETS in the editor: the picker previewing the next place of the pool, OCT+ kept it: $OUT/cr_pick_presets.ppm $OUT/cr_pick_kept.ppm" || bad "PRESETS in the editor"
grep '^picker: keep' "$L" | grep -qv 'LUSH PAD' && ok "kept: $(grep '^picker: keep' "$L" | sed 's/^picker: //')" || bad "kept the old sound"
silent_end cr_editor_pick

echo "EDIT held: the engine picker"
run cr_engine --wav "$OUT/cr_engine.wav"
has '^engine: part 0 -> PHASE' "$OUT/cr_engine.log" && has 'part 0: PHASE /' "$OUT/cr_engine.log" &&
    ok "EDIT held + A4: part 0 FM6 -> PHASE: $OUT/cr_engine_switched.ppm" || bad "the engine did not switch"
has '^preset: part 0 -> PHASE / ' "$OUT/cr_engine.log" && ok "EDIT held + KNOB 1: the next place of the engine's pool" || bad "KNOB 1: no preset step"
silent_end cr_engine

echo "SAVE: the dialog, Save as new, naming, the user slot"
run cr_save
has '^save: dialog part 0 TINE EP (save as new)' "$OUT/cr_save.log" && has '^save: new part 0 FM6 26 (slot U01)' "$OUT/cr_save.log" &&
    ok "SAVE on TINE EP: the dialog, Save as new by default; OCT+: the naming screen at FM6 26: $OUT/cr_save_open.ppm $OUT/cr_save_naming.ppm" || bad "the dialog: $(grep '^save:' "$OUT/cr_save.log" | head -2)"
has '^save: part 0 slot U01 name ADG' "$OUT/cr_save.log" && ok "D4 E4 F4, OCT+: U01 \"ADG\": $OUT/cr_save_typed.ppm" || bad "not saved"
has '^sound: part 0 pos 26 ADG' "$OUT/cr_save.log" && ok "PRESETS reaches it at FM6 26, after the 25 factory presets: $OUT/cr_save_preset.ppm" \
    || bad "U01 not at FM6 26: $(grep 'ADG' "$OUT/cr_save.log" | tail -1)"
grep -q '^edit: .*part 1' "$OUT/cr_save.log" && has '^deep: part 1 ' "$OUT/cr_save.log" &&
    ok "BASS held + EDIT: the bass sound's pages (VA DEEP SUB: its OSC stack), KNOB 1 edits part 1: $OUT/cr_edit_bass.ppm" || bad "BASS + EDIT"

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
[ "$(lf "$a" ring)" -lt "$(lf "$b" ring)" ] && ok "the loop's fraction advances ($(lf "$a" ring) -> $(lf "$b" ring) /256)" || bad "ring"
differ cr_loop_play_a cr_loop_play_b "the corner dial on screen moved: $OUT/cr_loop_play_b.ppm"
has 'expect led GREEN on .*: ok' "$OUT/cr_loop_free.log" && has 'expect led PLAY on .*: ok' "$OUT/cr_loop_free.log" &&
    ok "LOOP's LED lit (a loop) and its green LED (playing)" || bad "LOOP LEDs"
[ "$(grep -c 'expect led REC on .*: ok' "$OUT/cr_loop_free.log")" = 2 ] && ok "REC lit when armed and overdub-armed" || bad "REC LED"
[ "$(lf "$d" layers)" = 2 ] && [ "$(lf "$d" events)" = 3 ] && ok "overdub: G on a second layer (3 events): $OUT/cr_loop_overdub.ppm" || bad "overdub: $d"
e=$(L cr_loop_free 6); f=$(L cr_loop_free 7); g=$(L cr_loop_free 8)
[ $(( $(lf "$e" played) - $(lf "$d" played) )) = 3 ] && ok "the overdub plays in the next cycle (3 events in 2 s)" || bad "overdub playback"
[ "$(lf "$f" layers)" = 1 ] && [ "$(lf "$f" events)" = 2 ] && ok "REC held: undo removed the layer" || bad "undo: $f"
[ "$(lf "$g" state)" = 0 ] && [ "$(lf "$g" events)" = 0 ] && ok "LOOP held, GLO (Keys = Loops) + D#4 held 1 s: cleared: $OUT/cr_loop_layer_playing.ppm" || bad "clear: $g"
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
    ok "slot 2 in RAM at power-on (the PLAY menu: $OUT/cr_loop_load_menu.ppm), LOOP plays it: $OUT/cr_loop_loaded.ppm" || bad "load / play: $b"
[ "$(lf "$c" state)" = 1 ] && ok "panic: the loop stops, kept" || bad "panic: $c"
dr=$(px_count "$OUT/cr_loop_dial.ppm" 218 0 240 26 red); cr=$(px_count "$OUT/cr_loop_dial.ppm" 16 40 224 200 red)
de=$(px_count "$OUT/cr_loop_dial.ppm" 2 60 14 180 ink)
[ "$dr" -gt 10 ] && [ "$cr" = 0 ] && [ "$de" = 0 ] &&
    ok "the chord view while the loop plays: the corner dial ($dr red px top-right), no ring, no red in the panel: $OUT/cr_loop_dial.ppm" \
    || bad "the corner dial: $dr red in its box, $cr red in the panel, $de ink in the ring's left band"
re=$(px_count "$OUT/cr_loop_layer_dial.ppm" 2 100 14 140 ink); rd=$(px_count "$OUT/cr_loop_layer_dial.ppm" 218 0 240 26 red)
[ "$re" = 0 ] && [ "$rd" -gt 20 ] && ok "LOOP held while it plays: no ring in the loop layer, the dial in the top line ($rd red): $OUT/cr_loop_layer_dial.ppm" \
    || bad "the loop layer: $re px in the ring's left band, $rd red in the dial's box"
silent_end cr_loop_load
# a slot keeps its loop: the loop being left is saved to its own slot (docs/LOOPER.md "Slots and the record")
run cr_loop_slots --wav "$OUT/cr_loop_slots.wav"
SL="$OUT/cr_loop_slots.log"
a=$(L cr_loop_slots 1); b=$(L cr_loop_slots 2)
[ "$(lf "$a" slot)" = 1 ] && [ "$(lf "$a" events)" = 2 ] && [ "$(lf "$a" used)" = 000 ] && has '^loop: slot 1 staged ' "$SL" &&
    [ "$(lf "$b" slot)" = 2 ] && [ "$(lf "$b" events)" = 0 ] && [ "$(lf "$b" used)" = 001 ] &&
    has '^loop: auto-save slot 1 [0-9]* bytes rc 0 (at the stop)' "$SL" &&
    ok "a take in slot 1, LOOP held + PRESETS +1 while it plays: slot 1 staged, written once the loop stopped (empty slot 2): used 001" \
    || bad "switch while playing: $a / $b"
c=$(L cr_loop_slots 3); d=$(L cr_loop_slots 4); e=$(L cr_loop_slots 5)
[ "$(lf "$c" slot)" = 2 ] && [ "$(lf "$c" events)" = 1 ] && [ "$(lf "$d" slot)" = 1 ] && [ "$(lf "$d" events)" = 2 ] &&
    [ "$(lf "$d" state)" = 2 ] && [ "$(lf "$d" used)" = 001 ] && [ "$(lf "$e" used)" = 003 ] && [ "$(lf "$e" state)" = 1 ] &&
    has '^loop: auto-save slot 2 [0-9]* bytes rc 0 (at the stop)' "$SL" &&
    ok "a take in slot 2, LOOP held + PRESETS -1 while it plays: slot 1 back (2 events) playing on; LOOP stops: slot 2 written (used 003)" \
    || bad "the pending save at the stop: $c / $d / $e"
f=$(L cr_loop_slots 6); g=$(L cr_loop_slots 7); h=$(L cr_loop_slots 8); i=$(L cr_loop_slots 9)
[ "$(lf "$f" events)" = 3 ] && [ "$(lf "$g" slot)" = 2 ] && [ "$(lf "$g" events)" = 1 ] &&
    has '^loop: auto-save slot 1 [0-9]* bytes rc 0 (stopped)' "$SL" && [ "$(lf "$h" slot)" = 1 ] &&
    [ "$(lf "$h" events)" = 3 ] && [ "$(lf "$h" layers)" = 2 ] &&
    ok "stopped: an overdub on slot 1 written at the stop (no SAVE), PRESETS +1 / -1 brings it back with its 3 events, 2 layers" \
    || bad "switch while stopped: $f / $g / $h"
gr=$(od -An -tu1 -v -j 15 "$OUT/cr_loop_slots_saved.ppm" | awk '{ for (k = 1; k <= NF; k++) v[n++] = $k }
     END { c = 0; for (o = 0; o + 2 < n; o += 3) if (v[o] < 100 && v[o+1] > 140 && v[o+2] > 90 && v[o+2] < 160) c++; print c }')
[ "$gr" -gt 1000 ] && ok "the message \"saved to slot 1\" at the stop, in green ($gr px): $OUT/cr_loop_slots_saved.ppm" || bad "no green message ($gr px)"
[ "$(lf "$i" state)" = 2 ] && [ $(( $(lf "$i" played) - $(lf "$h" played) )) -ge 3 ] && grep -q '^expect sound .*: ok' "$SL" &&
    [ "$(num "non-silent blocks" "$SL")" -gt 0 ] &&
    ok "LOOP plays slot 1 again: its events sound ($(( $(lf "$i" played) - $(lf "$h" played) )) played in 1.5 s; the WAV: $OUT/cr_loop_slots.wav)" \
    || bad "slot 1 replayed: $i"
j=$(L cr_loop_slots 10); k=$(L cr_loop_slots 11)
[ "$(lf "$j" slot)" = 2 ] && [ "$(lf "$j" state)" = 2 ] && has '^loop: pending save flushed while playing' "$SL" &&
    has '^loop: auto-save slot 1 [0-9]* bytes rc 0 (a second switch)' "$SL" && [ "$(lf "$k" slot)" = 1 ] &&
    [ "$(lf "$k" events)" = 4 ] && [ "$(lf "$k" layers)" = 3 ] && [ "$(lf "$k" state)" = 2 ] &&
    ok "a second switch while playing: slot 1's staged save written at once (traced), slot 1 back with its overdub (4 events)" \
    || bad "second switch: $j / $k"
m=$(L cr_loop_slots 13)
has '^loop: switch to slot 2 cancelled, slot 1 stays' "$SL" && [ "$(lf "$m" slot)" = 1 ] && [ "$(lf "$m" events)" = 5 ] &&
    [ "$(grep -c '^loop: auto-save slot 1 [0-9]* bytes rc 0 (at the stop)' "$SL")" = 2 ] &&
    ok "a switch cancelled by a stop before the cycle's end: slot 1 stays selected, its overdub written at the stop (5 events)" \
    || bad "cancelled switch: $m"
silent_end cr_loop_slots
# the PLAY menu (docs/LOOPER-MODES.md, design/choralroot-fm1-looper-screens.png): the knobs, GLO, the strip, the modes
export EMU_UI_LOG=0
run cr_loop_menu
LM="$OUT/cr_loop_menu.log"
a=$(L cr_loop_menu 2); b=$(L cr_loop_menu 3)
has '^loop: length 3' "$LM" && grep -aq "loopmenu .*title '4 bars'" "$LM" && ok "SELECT +3 in the menu: 4 bars, big in the middle (state 1): $OUT/cr_lm_1.ppm" || bad "the menu's length"
has 'chord D sounding 1' "$LM" && ok "Keys = Play: MAJ + D4 plays D with the menu open" || bad "Keys = Play: no D"
has '^loop: keys Loops' "$LM" && has 'expect led GLO on .*: ok' "$LM" && has 'expect led D#4 on .*: ok' "$LM" &&
    has 'expect led D#4 dim .*: ok' "$LM" && ! has 'expect.*FAILED' "$LM" &&
    ok "GLO in the menu: Keys = Loops (GLO lit, the slot map: CLEAR lit; state 2: $OUT/cr_lm_2.ppm), GLO / KNOB 4 back: Play" || bad "the menu's keys: $(grep -a 'expect.*FAILED' "$LM" | head -2)"
has '^loop: mode Overdub' "$LM" && grep -aq "loopmenu .*item 'Overdub'" "$LM" && ok "ALGORITHM +2: the record mode Overdub (state 3): $OUT/cr_lm_3.ppm" || bad "the mode picker"
has '^loop: slot 5 turned' "$LM" && grep -aq "loopmenu .*slot 5 jump 0 sub '' right 'empty'" "$LM" &&
    ok "PRESETS +4: slot 5 (empty: \"empty\" at the top right; state 4): $OUT/cr_lm_4.ppm" || bad "the slot strip"
[ "$(lf "$a" cap)" = 1 ] && grep -aq "loopmenu .*title 'Free' .*sub 'the first chord starts the take' right 'Rec . Overwrite'" "$LM" &&
    [ "$(lf "$b" cap)" = 0 ] && ok "REC in the menu: armed, \"Rec · Overwrite\" at the top right (state 5: $OUT/cr_lm_5.ppm); REC again: cancelled"     || bad "armed in the menu: $a / $b"
grep -q 'layer 8' "$LM" && grep -q 'layer 0 options' "$LM" && ok "OCT+ in the menu: nothing; OCT- closes it" || bad "the menu's OCT"
# the record modes
run cr_loop_modes --wav "$OUT/cr_loop_modes.wav"
unset EMU_UI_LOG
LM="$OUT/cr_loop_modes.log"
m() { L cr_loop_modes "$1"; }
grep -aq "loopmenu .* lm 1/2/2/[0-9]* lanes 1/1 slot 1 jump 0 sub '0:0[0-9]' right 'Rec 2\.[0-9]'" "$LM" &&
    ok "recording Free in the menu: the timeline, bar 2 growing, the elapsed time under it: $OUT/cr_lm_6.ppm" || bad "the Free take's timeline"
[ "$(lf "$(m 3)" cap)" = 1 ] && [ "$(lf "$(m 3)" state)" = 1 ] && [ "$(lf "$(m 3)" events)" = 1 ] && [ "$(lf "$(m 4)" events)" = 1 ] &&
    [ "$(lf "$(m 4)" cap)" = 0 ] && ok "Overwrite: REC while playing stops and arms, the loop kept; REC again cancels, kept" || bad "Overwrite arm / cancel: $(m 3) / $(m 4)"
[ "$(lf "$(m 5)" cap)" = 3 ] && [ "$(lf "$(m 5)" events)" = 0 ] && [ "$(lf "$(m 6)" events)" = 1 ] && [ "$(lf "$(m 6)" state)" = 2 ] &&
    ok "Overwrite: the first chord starts the take and the loop goes then; the take replaces it" || bad "Overwrite take: $(m 5) / $(m 6)"
[ "$(lf "$(m 7)" slot)" = 2 ] && [ "$(lf "$(m 7)" used)" = 001 ] && [ "$(lf "$(m 7)" cap)" = 1 ] &&
    has '^loop: advance slot 1 -> 2 (saved)' "$LM" && grep -aq "loopmenu .*slot 2 jump 2 sub 'slot 1 saved . next: slot 2'" "$LM" &&
    [ "$(lf "$(m 8)" slot)" = 2 ] && [ "$(lf "$(m 8)" events)" = 1 ] && [ "$(lf "$(m 8)" state)" = 2 ] &&
    ok "Advance: REC on slot 1 saves it and arms slot 2 (the hop, \"slot 1 saved · next: slot 2\": $OUT/cr_lm_10.ppm); F recorded there"     || bad "Advance: $(m 7) / $(m 8)"
[ "$(lf "$(m 9)" len)" = 1536 ] && [ "$(lf "$(m 9)" events)" = 4 ] && [ "$(lf "$(m 10)" layers)" = 2 ] &&
    grep -aq "loopmenu .* lm 1/4/0/[0-9]* lanes 2/0 " "$LM" &&
    ok "4 bars synced, then an overdub: the timeline's 4 bars and 2 lanes while it plays: $OUT/cr_lm_7.ppm" || bad "4 bars, 2 layers: $(m 9) / $(m 10)"
[ "$(lf "$(m 11)" hidden)" -ge 1 ] && [ "$(lf "$(m 11)" cap)" = 5 ] && grep -aq "right 'Rep 3\.[0-9]'" "$LM" &&
    [ "$(lf "$(m 12)" layers)" = 3 ] && [ "$(lf "$(m 12)" hidden)" -ge 1 ] &&
    ok "Replace: F held over bar 3 hides Ab (hidden $(lf "$(m 11)" hidden), \"Rep 3.x\", the span: $OUT/cr_lm_9.ppm); REC ends it (3 layers)" \
    || bad "Replace: $(m 11) / $(m 12)"
[ "$(lf "$(m 13)" layers)" = 2 ] && [ "$(lf "$(m 13)" hidden)" = 0 ] && [ "$(lf "$(m 13)" events)" = 5 ] &&
    ok "REC held: undo of the replace layer brings Ab back (2 layers, 5 events, none hidden)" || bad "Replace undo: $(m 13)"
[ "$(lf "$(m 14)" cap)" = 6 ] && [ "$(lf "$(m 14)" state)" = 1 ] && [ "$(lf "$(m 15)" step)" = 3 ] && [ "$(lf "$(m 15)" events)" = 8 ] &&
    grep -aq "loopmenu .* lm 2/1/0/2 .*sub 'G#' right 'Rec . Step'" "$LM" &&
    ok "Step: REC enters it (the loop stops); D, Em, Ab at steps 1-3, OCT+ a rest, OCT- back: the cursor on step 4 ($OUT/cr_lm_11.ppm, $OUT/cr_lm_12.ppm)" \
    || bad "Step entry: $(m 14) / $(m 15)"
[ "$(lf "$(m 16)" events)" = 3 ] && [ "$(lf "$(m 16)" len)" = 384 ] && [ "$(lf "$(m 16)" layers)" = 1 ] && [ "$(lf "$(m 16)" state)" = 1 ] &&
    [ $(( $(lf "$(m 18)" played) - $(lf "$(m 17)" played) )) = 3 ] && has 'expect sound .*: ok' "$LM" &&
    ok "Step: REC commits a 1-bar loop of the 3 chords; LOOP plays them (3 a cycle)" || bad "Step commit / play: $(m 16) / $(m 17) / $(m 18)"
# the WAV: the step loop's second cycle (found after its silence): E4 (Em's root) comes in 1/16 (125 ms) after the
# start, G#4 (Ab's) 2/16; D4 leads (Goertzel over 90 ms windows; the ratios late / early)
wv=$(python3 - "$OUT/cr_loop_modes.wav" <<'PY'
import sys, wave, struct, math
w = wave.open(sys.argv[1]); sr = w.getframerate(); ch = w.getnchannels()
raw = w.readframes(w.getnframes()); x = struct.unpack('<%dh' % (len(raw) // 2), raw)
n = len(x) // ch
def band(t0, t1, f):
    a, b = int(t0 * sr), int(t1 * sr); k = 2 * math.cos(2 * math.pi * f / sr); s1 = s2 = 0.0
    for i in range(a, b):
        s = (x[i * ch] + x[i * ch + ch - 1]) / 2 + k * s1 - s2; s2 = s1; s1 = s
    return math.sqrt(max(s1 * s1 + s2 * s2 - k * s1 * s2, 0)) / (b - a)
# the second cycle of the step loop: the last onset after a silence, 4..2 s before the end
f = int(0.005 * sr); t0 = None
i = n - int(4.4 * sr)
while i < n - int(1.5 * sr):
    if sum(abs(x[k * ch]) for k in range(i, i + f)) / f > 300 and sum(abs(x[k * ch]) for k in range(i - 4 * f, i - 3 * f)) / f < 30:
        t0 = i / sr; break
    i += f
if t0 is None: print('none'); sys.exit()
def r(f0, early, late): return band(t0 + late, t0 + late + 0.09, f0) / max(band(t0 + early, t0 + early + 0.09, f0), 1e-3)
print('%.2f %.1f %.1f %.1f' % (t0, r(329.63, 0.02, 0.14), r(415.30, 0.14, 0.27), band(t0+0.02,t0+0.11,293.66)/max(band(t0+0.02,t0+0.11,415.30),1e-3)))
PY
)
set -- $wv
[ $# = 4 ] && awk "BEGIN { exit !($2 > 3 && $3 > 3 && $4 > 3) }" &&
    ok "the WAV: the steps at their ticks (from $1 s: E4 x$2 at +125 ms, G#4 x$3 at +250 ms, D4 / G#4 x$4 at the start)" \
    || bad "the step loop's WAV: '$wv'"
silent_end cr_loop_modes
"$EMU" --headless --script "$S/cr_loop_free.txt" --wav "$OUT/cr_again/cr_loop_free.wav" >/dev/null 2>&1
cmp -s "$OUT/cr_loop_free.wav" "$OUT/cr_again/cr_loop_free.wav" && ok "the looper is deterministic (the same audio twice)" || bad "looper audio differs"

echo "the loop-length layer, the count-in, undo"
run cr_countin
has '^loop: length 1' "$OUT/cr_countin.log" && ok "LOOP held + SELECT +1: the length (Free -> 1 bar), no meter: $OUT/cr_loop_length_1bar.ppm" \
    || bad "SELECT in the PLAY menu"
differ cr_loop_length cr_loop_length_1bar "the length changed: $OUT/cr_loop_length.ppm"
differ cr_countin_4 cr_countin_3 "the count-in counts down huge in red: $OUT/cr_countin_4.ppm -> cr_countin_3.ppm"
p=$(pixel "$OUT/cr_countin_4.ppm" 120 130); set -- $p
[ "${1:-0}" -gt 180 ] && [ "${2:-255}" -lt 90 ] && ok "the beat number is red ($p)" || bad "count-in number not red ($p)"
has '^undo: 1 layers left' "$OUT/cr_countin.log" && ok "REC held: undo, the layers left huge: $OUT/cr_undo.ppm" || bad "undo trace"
silent_end cr_countin

echo "REC in the corner dial: no ring round the screens; the count-in and undo keep theirs"
ring12() { px_count "$1" 2 100 14 140 ink; }      # the ring's band at 9 o'clock (nothing else draws there: the top
                                                  # line's right text "Rec \302\267 Overwrite" reaches 12 o'clock)
dialred() { px_count "$1" 218 0 240 26 red; }     # red in the corner dial's box
redpx() { set -- $(pixel "$1" "$2" "$3"); [ "${1:-0}" -gt 180 ] && [ "${2:-255}" -lt 100 ]; }   # redpx FILE X Y
export EMU_UI_LOG=0
run cr_rec_dial
unset EMU_UI_LOG
f=$OUT/cr_rec_armed.ppm
[ "$(ring12 "$f")" = 0 ] && [ "$(dialred "$f")" -gt 10 ] && [ "$(dialred "$f")" -lt 45 ] && redpx "$f" 229 12 && ! redpx "$f" 220 11 &&
    ok "REC armed, no loop: no ring; the dial's grey track and the REC dot lit in its middle ($(dialred "$f") red px): $f" \
    || bad "armed: ring band $(ring12 "$f"), $(dialred "$f") red in the dial, dot $(pixel "$f" 229 12), track $(pixel "$f" 220 11)"
f=$OUT/cr_rec_rec.ppm
[ "$(ring12 "$f")" = 0 ] && redpx "$f" 229 12 && redpx "$f" 220 11 &&
    ok "the first take recording: no ring; the dial's track red past the arc, the REC dot ($(dialred "$f") red px): $f" \
    || bad "recording: ring band $(ring12 "$f"), dot $(pixel "$f" 229 12), track $(pixel "$f" 220 11)"
f=$OUT/cr_rec_od.ppm
[ "$(ring12 "$f")" = 0 ] && redpx "$f" 229 12 && [ "$(dialred "$f")" -gt 60 ] &&
    ok "overdubbing: no ring; the loop's arc and the REC dot ($(dialred "$f") red px): $f" \
    || bad "overdubbing: ring band $(ring12 "$f"), $(dialred "$f") red in the dial, dot $(pixel "$f" 229 12)"
L="$OUT/cr_rec_dial.log"
grep -q 'dial armed/0/1 ' "$L" && grep -q 'dial rec/[0-9]*/0 ' "$L" && grep -q 'dial rec/[0-9]*/1 ' "$L" &&
    grep -q 'dial od/[0-9]*/0 ' "$L" && grep -q 'dial od/[0-9]*/1 ' "$L" && ! grep -q 'dial armed/[0-9]*/0 ' "$L" &&
    ok "the REC dot follows the REC LED: lit while armed, blinking while recording and overdubbing" || bad "the dot's blink: $(grep -o 'dial [a-z]*/[0-9]*/[01]' "$L" | sort -u | head -5 | tr '\n' ' ')"
for f in cr_loop_rec_free cr_loop_rec cr_loop_overdub; do
    [ "$(ring12 "$OUT/$f.ppm")" = 0 ] && [ "$(dialred "$OUT/$f.ppm")" -gt 10 ] && ok "$f: no ring, the dial: $OUT/$f.ppm" \
        || bad "$f: ring band $(ring12 "$OUT/$f.ppm"), $(dialred "$OUT/$f.ppm") red in the dial"
done
for f in cr_loop_countin cr_countin_4 cr_undo; do
    [ "$(ring12 "$OUT/$f.ppm")" -gt 0 ] && [ "$(dialred "$OUT/$f.ppm")" = 0 ] && ok "$f: the ring (the loop's own screen), no dial: $OUT/$f.ppm" \
        || bad "$f: ring band $(ring12 "$OUT/$f.ppm"), $(dialred "$OUT/$f.ppm") red in the dial's box"
done
silent_end cr_rec_dial

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

echo "user presets: Overwrite, delete, the naming keys"
run cr_save_del
has '^save: dialog part 0 ADG (overwrite)' "$OUT/cr_save_del.log" && has '^save: part 0 overwrite slot U01 FM6 26 ADG rc 0' "$OUT/cr_save_del.log" &&
    ok "SAVE on a user preset: Overwrite by default, U01 rewritten, its name and place kept: $OUT/cr_save_overwrite.ppm" || bad "overwrite: $(grep '^save:' "$OUT/cr_save_del.log" | tr '\n' ' ')"
[ "$(grep -c '^save: delete ADG?' "$OUT/cr_save_del.log")" = 2 ] && has '^save: delete slot U01 rc 0' "$OUT/cr_save_del.log" &&
    has '^save: now part 0 FM6 25 PIANO' "$OUT/cr_save_del.log" &&
    ok "SAVE held 1 s in the dialog: delete? (OCT- kept it), OCT+ deleted U01, the part on FM6 25: $OUT/cr_save_delete.ppm" || bad "delete"
[ "$(grep -c '^expect led .*: ok' "$OUT/cr_save_del.log")" = 6 ] && ok "naming: the typing keys lit (F#4: delete), G#4 dark, OCT- lit" || bad "naming LEDs"

echo "presets per engine (docs/PRESETS.md): PRESETS in the engine's pool, OPT + PRESETS the engine, Save as new, Overwrite, reset"
run cr_presets
PL="$OUT/cr_presets.log"
pop() { grep -q "^popup: part 0 $1\$" "$PL"; }
pop '00 / INIT / FM6 · 00/26' && pop '25 / PIANO / FM6 · 25/26' && pop '05 / FM PAD / FM6 · 05/26' &&
    ok "PRESETS turns inside FM6's pool, INIT at 00, wrapping (00 INIT, 25 PIANO, 05 \"FM PAD\" \"FM6 · 05/26\"): $OUT/cr_presets_init.ppm $OUT/cr_presets_meter.ppm" \
    || bad "the FM6 pool: $(grep '^popup:' "$PL" | head -3 | tr '\n' ' ')"
[ "$(grep -c '^sound: part 0 engine' "$PL")" = 4 ] && has '^engine pick: VA (25 presets)' "$PL" && has '^sound: part 0 engine VA pos 1 LUSH PAD' "$PL" &&
    ok "OPT + PRESETS: the engine picker (VA, 25 presets), OPT released: VA 01 LUSH PAD: $OUT/cr_presets_engine.ppm" || bad "OPT + PRESETS: $(grep '^sound: part 0 engine' "$PL" | head -1)"
[ "$(grep -c '^sound: part 0 engine FM6 pos 5 FM PAD' "$PL")" = 2 ] && has '^sound: part 0 engine VA pos 4 SLOW STRINGS' "$PL" &&
    ok "each engine remembers its place: FM6 back on 05 FM PAD, the VA on 04 SLOW STRINGS" || bad "the per-engine memory: $(grep '^sound: part 0 engine' "$PL" | tr '\n' ' ')"
has '^save: dialog part 0 FM PAD (save as new)' "$PL" && has '^save: new part 0 FM6 26 (slot U01)' "$PL" && has '^save: part 0 slot U01 name ADG rc 0' "$PL" &&
    pop '26 / ADG / FM6 · 26/27' && ok "Save as new: the next place (FM6 26), PRESETS sits on it: $OUT/cr_presets_dialog.ppm $OUT/cr_presets_naming.ppm $OUT/cr_presets_new.ppm" \
    || bad "Save as new: $(grep '^save:' "$PL" | head -3 | tr '\n' ' ')"
has '^save: part 0 overwrite slot U02 FM6 02 FM BELL rc 0' "$PL" && pop '02 / FM BELL (mark) / FM6 · 02/27' &&
    ok "Overwrite on a factory preset: bound in U02, the number and the name kept, the mark: $OUT/cr_presets_overwrite.ppm $OUT/cr_presets_mark.ppm" || bad "Overwrite: $(grep 'overwrite' "$PL")"
has '^save: reset FM BELL?' "$PL" && has '^save: reset slot U02 rc 0' "$PL" && has '^save: now part 0 FM6 02 FM BELL' "$PL" && pop '02 / FM BELL / FM6 · 02/27' &&
    ok "SAVE held 1 s: reset to factory?, OCT+: the factory FM BELL back at 02 (no mark): $OUT/cr_presets_reset.ppm $OUT/cr_presets_factory.ppm" || bad "reset: $(grep '^save: reset' "$PL")"
[ "$(grep -c '^save: part 0 slot U[0-9]* name .* rc 0' "$PL")" = 32 ] && has '^save: no free slot' "$PL" &&
    ok "32 user presets fill the slots; the next Save as new: \"no free slot\": $OUT/cr_presets_full.ppm" || bad "no free slot: $(grep -c 'name .* rc 0' "$PL") saves"

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

echo "VA (eng_va.c, docs/VA.md): its first preset, a held 6-note chord, a chord change"
rm -f "$OUT"/va_chord*
run va_chord --wav "$OUT/va_chord.wav"
has 'part 0: VA / LUSH PAD' "$OUT/va_chord.log" && ok "OPT + PRESETS: the VA, its pool's 01 LUSH PAD" || bad "va_chord: not the VA's LUSH PAD"
[ "$(grep -c '^expect .*: ok' "$OUT/va_chord.log")" = 3 ] && ok "va_chord: sound held, the tails, then silence" ||
    bad "va_chord: $(grep -c '^expect .*: ok' "$OUT/va_chord.log") of 3 expectations"
wc=$(python3 tools/emu/wavclicks.py "$OUT/va_chord.wav" --from 0.4 | tail -1)
echo "$wc" | grep -q ': 0 jumps > 0.5 FS, 0 silent holes mid-sound, 0 clicks' && ok "va_chord: $wc" || bad "va_chord: $wc"
[ -s "$OUT/va_chord_held.ppm" ] && ok "screenshot: $OUT/va_chord_held.ppm" || bad "no va_chord_held.ppm"

echo "all-synth: the sounds that were sample-based (PIANO, CLOUD PAD, SHIMMER) are synth sounds and play"
run cr_allsynth
AL="$OUT/cr_allsynth.log"
has 'part 0: FM6 / PIANO' "$AL" && has 'part 0: VA / CLOUD PAD' "$AL" && has 'part 0: VA / SHIMMER' "$AL" &&
    ok "FM6 25 PIANO, VA 24 CLOUD PAD and VA 25 SHIMMER" || bad "all-synth sounds: $(grep 'part 0:' "$AL" | tr '\n' ' ')"
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

echo "CZ-1: Melodee's engine, Casio's tones, the deep pages, the cz band"
run cr_cz --wav "$OUT/cr_cz.wav"
ZL="$OUT/cr_cz.log"
has 'part 0: CZ-1 / BRASS 1' "$ZL" && ok "OPT + PRESETS +3: the CZ-1's 01 BRASS 1 (Casio's A-1)" || bad "not CZ-1 BRASS 1: $(grep -m1 'part 0:' "$ZL")"
has '^deep: part 0 page 11 DCW 1+ col 0 L1 [0-9]* -> 0 ' "$ZL" && ok "ENV screen 4 (DCW 1), SELECT +1, KNOB 1: $(grep -m1 '^deep: part 0 page 11' "$ZL")" ||
    bad "no DCW 1 L1 edit: $(grep -m1 '^deep:' "$ZL")"
[ "$(grep -c '^expect sound .*: ok' "$ZL")" = 2 ] && ok "the chord sounds before and after the edit" || bad "CZ-1 chord: $(grep -c '^expect sound .*: ok' "$ZL") of 2"
br=$(python3 - "$OUT/cr_cz.wav" <<'PY'
import sys, wave, struct
w = wave.open(sys.argv[1]); fs = w.getframerate(); ch = w.getnchannels()
x = struct.unpack("<%dh" % (w.getnframes() * ch), w.readframes(w.getnframes()))
m = [x[i] for i in range(0, len(x), ch)]
def bright(t0, t1):          # high-frequency share: the first difference's energy over the signal's
    s = m[int(t0 * fs):int(t1 * fs)]
    e = sum(v * v for v in s) or 1
    d = sum((s[i] - s[i - 1]) ** 2 for i in range(1, len(s)))
    return d / e
b0, b1 = bright(1.16, 2.36), bright(7.07, 8.27)
print("%.4f %.4f %d" % (b0, b1, b1 < 0.8 * b0))
PY
)
set -- $br
[ "${3:-0}" = 1 ] && ok "DCW 1 L1 0 changes the sound: brightness (HF share) $1 -> $2 (the timbre envelope closed)" || bad "the sound did not change: $br"
white=$(od -An -tu1 -v -j $((15 + 30 * 240 * 3)) -N$((240 * 70 * 3)) "$OUT/cr_cz_env.ppm" | tr -s ' \n' '\n\n' | grep -v '^$' |
    awk '{ v[n++] = $1 } END { c = 0; for (i = 0; i + 2 < n; i += 3) if (v[i] > 200 && v[i+1] > 200 && v[i+2] > 200) c++; print c }')
blue() { od -An -tu1 -v -j $((15 + 30 * 240 * 3)) -N$((240 * 90 * 3)) "$1" | tr -s ' \n' '\n\n' | grep -v '^$' |
    awk '{ v[n++] = $1 } END { c = 0; for (i = 0; i + 2 < n; i += 3) if (v[i] < 110 && v[i+1] < 160 && v[i+2] > 200) c++; print c }'; }
z0=$(blue "$OUT/cr_cz_env.ppm"); z1=$(blue "$OUT/cr_cz_env_hot.ppm")
[ "${white:-0}" -gt 300 ] && ok "DCW 1: the cz band drawn ($white white pixels in the band): $OUT/cr_cz_env.ppm" || bad "no cz band: $white"
[ "${z1:-0}" -gt $((${z0:-0} + 20)) ] && ok "KNOB 1 (L1): step 1 lit blue ($z0 -> $z1 pixels): $OUT/cr_cz_env_hot.ppm" || bad "no lit step: $z0 -> $z1"
has '^edit: group ENV screen 6 ' "$ZL" && has '^edit: group FILT screen 1 ' "$ZL" && has '^edit: group LFO screen 1 ' "$ZL" &&
    has '^edit: group MOD screen 1 ' "$ZL" && ok "CZ-1's groups: ENV DCW 1 (1-4, 5-8, END), FILT (DCW), LFO (VIB), MOD (TONE)" ||
    bad "CZ-1 groups: $(grep -c '^edit: group' "$ZL")"

echo "FM TONE (code name QUAD): the engine picker, the pool, a chord, the editor's screens (docs/QUAD.md)"
run cr_quad --wav "$OUT/cr_quad.wav"
QL="$OUT/cr_quad.log"
has '^engine: part 0 -> FM TONE' "$QL" && has 'part 0: FM TONE / EP' "$QL" && ok "EDIT held + E4 (the second white root): FM TONE, 01 EP" ||
    bad "not FM TONE from the picker: $(grep -m1 '^engine:' "$QL")"
has '^popup: part 0 03 / BASS / FM TONE · 03/17$' "$QL" && ok "PRESETS +2: the pool, FM TONE · 03/17 (INIT + 16 presets): $OUT/cr_quad_pool.ppm" ||
    bad "pool: $(grep -m1 '^popup:' "$QL")"
has 'part 0: FM TONE / BELL, voices 3' "$QL" && has '^expect sound .*: ok' "$QL" && ok "MAJ + D4 on 02 BELL: a chord of three FM TONE voices sounds" ||
    bad "FM TONE chord: $(grep 'part 0: FM TONE / BELL' "$QL" | head -1)"
qa=$(sed -n 's/^deep: part 0 page 0 SYN 1 col 0 ALGO \([1-7]\) -> \([2-8]\) .*/\1 \2/p' "$QL" | head -1)
[ -n "$qa" ] && [ $(( ${qa% *} + 1 )) -eq "${qa#* }" ] && ok "SYN 1 on BELL, KNOB 1: Algo ${qa% *} -> ${qa#* } (one step a detent)" || bad "no Algo edit: $(grep -m1 '^deep:' "$QL")"
band=$(python3 -c "
import sys; a, b = (open(f, 'rb').read() for f in sys.argv[1:3]); o = 15 + 30 * 240 * 3; n = 90 * 240 * 3
print(sum(x != y for x, y in zip(a[o:o + n], b[o:o + n])))" "$OUT/cr_quad_syn1.ppm" "$OUT/cr_quad_syn1_algo.ppm")
[ "${band:-0}" -gt 300 ] && ok "the algorithm band redrawn for Algo ${qa#* } ($band bytes differ): $OUT/cr_quad_syn1_algo.ppm" || bad "the band did not change: $band"
for n in syn2 filter filter_off filter_lp24 filter2 envab env2 env3 amp lfo1 lfo1_fade lfo2 mod; do [ -s "$OUT/cr_quad_$n.ppm" ] || bad "no shot cr_quad_$n"; done
has '^deep: part 0 page 4 FILTER+ col 2 TYPE [0-9] -> 0 (OFF)' "$QL" && has '^deep: part 0 page 4 FILTER+ col 2 TYPE 0 -> 3 (LP24)' "$QL" &&
    ok "FILTER row B, KNOB 3: TYPE OFF, then LP24 (OFF LP12 HP12 LP24)" || bad "filter TYPE: $(grep -m2 'TYPE' "$QL" | tr '\n' ' ')"
fband() {        # fband FILE: the filter band's orange curve: its lowest and highest row (x 8..232, y 28..114)
    python3 -c "
import sys; d = open(sys.argv[1], 'rb').read(); o = len(d) - 240 * 240 * 3
ys = [y for y in range(28, 114) for x in range(8, 232) if d[o + (y * 240 + x) * 3] > 200 and 60 < d[o + (y * 240 + x) * 3 + 1] < 170 and d[o + (y * 240 + x) * 3 + 2] < 90]
print(max(ys) - min(ys) if ys else -1)" "$1"
}
fo=$(fband "$OUT/cr_quad_filter_off.ppm"); f24=$(fband "$OUT/cr_quad_filter_lp24.ppm")
[ "${fo:-99}" -ge 0 ] && [ "${fo:-99}" -le 5 ] && [ "${f24:-0}" -gt 12 ] && ok "the band: OFF a flat line ($fo rows), LP24 a slope ($f24 rows): $OUT/cr_quad_filter_off.ppm" ||
    bad "the filter band's curve: OFF $fo rows, LP24 $f24"
has '^deep: part 0 page 9 ENV 2 col 3 PHRT [0-4] -> [0-4] ([A-Z+0-9]*)' "$QL" && ok "ENV 2, KNOB 4: PHRT, a name (OFF ALL C A+B A+B2)" ||
    bad "PHRT: $(grep -m1 'ENV 2 col 3' "$QL")"
e3=$(python3 -c "
import sys; d = open(sys.argv[1], 'rb').read(); o = len(d) - 240 * 240 * 3; b = d[o + (30 * 240 + 238) * 3:o + (30 * 240 + 239) * 3]
ink = lambda x0, x1: sum(1 for y in range(28, 120) for x in range(x0, x1) if d[o + (y * 240 + x) * 3:o + (y * 240 + x) * 3 + 3] != b)
print(ink(124, 176), ink(184, 236))" "$OUT/cr_quad_env3.ppm")
has '^deep: part 0 page 11 ENV 3 col 2 B2 KEY [0-9]* -> ' "$QL" && [ "${e3% *}" -gt 100 ] && [ "${e3#* }" -lt 20 ] &&
    ok "ENV 3: three cells (A Key, B1 Key, B2 Key: KNOB 3 turns B2 KEY; ink $e3 in columns 3, 4): $OUT/cr_quad_env3.ppm" ||
    bad "ENV 3: $(grep -m1 'ENV 3' "$QL"), ink $e3"
has '^deep: part 0 page 14 LFO 1 col 1 MULT [0-9]* -> [0-9]* (F[0-9k]*)' "$QL" && has '^deep: part 0 page 14 LFO 1 col 2 FADE -*[0-9]* -> -' "$QL" &&
    ok "LFO 1: MULT to the fixed-120 half (F..), FADE below 0 (bipolar): $OUT/cr_quad_lfo1_fade.ppm" ||
    bad "LFO 1 MULT / FADE: $(grep 'LFO 1 col [12]' "$QL" | tr '\n' ' ')"
has '^edit: group OSC screen 2 ' "$QL" && has '^edit: group FILT screen 2 ' "$QL" && has '^edit: group ENV screen 4 ' "$QL" &&
    has '^edit: group LFO screen 2 ' "$QL" && has '^edit: group MOD screen 1 ' "$QL" &&
    ok "FM TONE's groups: OSC SYN 1 / SYN 2, FILT FILTER / FILTER 2, ENV A/B, 2, 3, AMP, LFO 1..3 a screen each, MOD the platform's" ||
    bad "FM TONE groups: $(grep -c '^edit: group' "$QL")"
has '^deep: part 0 page 15 LFO 1+ col 1 PHASE 0 -> ' "$QL" && ok "LFO 1 row B, KNOB 2: the start phase in the Wave · Phase cell: $OUT/cr_quad_lfo1_phase.ppm" ||
    bad "no phase edit"
blue_q=$(od -An -tu1 -v -j $((15 + 180 * 240 * 3)) -N$((240 * 40 * 3)) "$OUT/cr_quad_lfo1_phase.ppm" | tr -s ' \n' '\n\n' | grep -v '^$' |
    awk '{ v[n++] = $1 } END { c = 0; for (i = 0; i + 2 < n; i += 3) if (v[i] < 110 && v[i+1] < 160 && v[i+2] > 200) c++; print c }')
[ "${blue_q:-0}" -gt 2000 ] && ok "the span cell hot over two columns (KNOB 2 -> its cell 0, $blue_q blue pixels)" || bad "span cell not hot: $blue_q"
silent_end cr_quad

echo "FM TONE's RATIO B heard: a detent steps B2 (the top), Shift steps B1 (the bottom: Y's pitch), the fraction uncut"
run cr_quad_ratiob --wav "$OUT/cr_quad_ratiob.wav"
QL="$OUT/cr_quad_ratiob.log"
has '^deep: part 0 page 0 SYN 1 col 0 ALGO 1 -> 2 ' "$QL" && has '^deep: part 0 page 1 SYN 1+ col 3 MIX -*[0-9]* -> 63 ' "$QL" &&
    has '^deep: part 0 page 8 ENV B col 3 B LEV 0 -> [1-9][0-9]* ' "$QL" &&
    ok "BELL on algorithm 2 (B2 > B1 on Y), MIX +63 (Y only), B LEV a small index" || bad "algo / MIX / B LEV: $(grep -c '^deep:' "$QL") edits"
has '^deep: part 0 page 0 SYN 1 col 3 RATIO B [0-9]* -> 0 (0.25/0.25)' "$QL" && has '^deep: part 0 page 0 SYN 1 col 3 RATIO B 0 -> 57 (0.25/1.00)' "$QL" &&
    has '^deep: part 0 page 0 SYN 1 col 3 RATIO B 57 -> 61 (2.00/1.00)' "$QL" &&
    ok "RATIO B to 2.00/1.00: the text B2/B1 (index B1 x 19 + B2), Shift +3 steps B1 by 19s" || bad "RATIO B setup: $(grep 'RATIO B' "$QL" | head -4 | tr '\n' ' ')"
has '^deep: part 0 page 0 SYN 1 col 3 RATIO B 61 -> 62 (3.00/1.00)' "$QL" && ok "KNOB 4 +1: B2 2.00 -> 3.00, the top number (one step a detent)" ||
    bad "B2 edit: $(grep 'RATIO B' "$QL" | sed -n 5p)"
has '^deep: part 0 page 0 SYN 1 col 3 RATIO B 62 -> 81 (3.00/2.00)' "$QL" && ok "OPT / Shift held + KNOB 4 +1: B1 1.00 -> 2.00, B2 kept" ||
    bad "Shift on RATIO B: $(grep 'RATIO B' "$QL" | sed -n 6p)"
has '^deep: part 0 page 0 SYN 1 col 3 RATIO B 81 -> 347 (3.00/16.00)' "$QL" && has '^deep: part 0 page 0 SYN 1 col 3 RATIO B 347 -> 5 (3.00/0.25)' "$QL" &&
    ok "Shift + KNOB 4 +-30: B1 stops at 16.00 and 0.25, B2 kept" || bad "B1's ends: $(grep 'RATIO B' "$QL" | tail -2 | tr '\n' ' ')"
rbn=$(python3 tools/emu/wavpitch.py "$OUT/cr_quad_ratiob.wav")
rbp=$(echo "$rbn" | sed -n 's/.*peak \([0-9.]*\) Hz/\1/p' | tr '\n' ' ')
rbc=$(echo "$rbn" | sed -n 's/.*cent \([0-9.]*\) Hz.*/\1/p' | tr '\n' ' ')
python3 -c "import sys; p = [float(x) for x in sys.argv[1:]]; sys.exit(not (len(p) == 3 and abs(p[0] - 262) < 10 and
    abs(p[1] / p[0] - 1) < 0.03 and abs(p[2] / p[1] - 2) < 0.05))" $rbp &&
    ok "the WAV: C4's Y at $rbp Hz (B1 1.00; B2 stepped: the same pitch; B1 2.00: an octave up)" || bad "Y's pitch per note: $rbp (want ~262, x1, x2)"
python3 -c "import sys; c = [float(x) for x in sys.argv[1:]]; sys.exit(not (len(c) == 3 and abs(c[1] / c[0] - 1) > 0.05))" $rbc &&
    ok "the WAV: B2 2.00 -> 3.00 changes Y's timbre (spectral centroid $rbc Hz)" || bad "B2 did not change the timbre: centroids $rbc"
frac() {         # frac FILE Y0 Y1: RATIO B's cell (column 4, x 184..236) rows Y0..Y1 as one string (a part of the fraction)
    python3 -c "
import sys; d = open(sys.argv[1], 'rb').read(); o = len(d) - 240 * 240 * 3; y0, y1 = int(sys.argv[2]), int(sys.argv[3])
print(b''.join(d[o + (y * 240 + 184) * 3:o + (y * 240 + 237) * 3] for y in range(y0, y1)).hex())" "$1" "$2" "$3"
}
[ "$(frac "$OUT/cr_quad_ratiob_b0.ppm" 160 174)" = "$(frac "$OUT/cr_quad_ratiob_b1.ppm" 160 174)" ] &&
    [ "$(frac "$OUT/cr_quad_ratiob_b0.ppm" 138 157)" != "$(frac "$OUT/cr_quad_ratiob_b1.ppm" 138 157)" ] &&
    ok "the fraction: a detent changes the top number (2.00 -> 3.00), the bottom (1.00) kept: $OUT/cr_quad_ratiob_b1.ppm" ||
    bad "the top / bottom of the fraction after a plain detent"
den() {          # den FILE: ink columns of RATIO B's denominator (column 4, the hot block, rows 160..173)
    python3 -c "
import sys; d = open(sys.argv[1], 'rb').read(); o = len(d) - 240 * 240 * 3
px = lambda x, y: d[o + (y * 240 + x) * 3:o + (y * 240 + x) * 3 + 3]
b = px(186, 150); print(sum(1 for x in range(184, 237) if any(px(x, y) != b for y in range(160, 174))))" "$1"
}
d2=$(den "$OUT/cr_quad_ratiob_b2.ppm"); d16=$(den "$OUT/cr_quad_ratiob_b16.ppm")
[ "${d2:-0}" -gt 20 ] && [ "${d16:-0}" -gt $((d2 + 3)) ] && ok "the fraction: 3.00 over 2.00 ($d2 columns), over 16.00 ($d16): the denominator whole: $OUT/cr_quad_ratiob_b16.ppm" ||
    bad "the denominator: $d2 / $d16 ink columns"
differ cr_quad_ratiob_b1 cr_quad_ratiob_b2 "Shift: the title line reads \"fine · B1\", the denominator 2.00: $OUT/cr_quad_ratiob_b2.ppm"
silent_end cr_quad_ratiob

echo "FM TONE's release INF: AMP REL 127 reads INF, a released note sounds on; REL 61 decays; PANIC ends a held one"
run cr_quad_inf --wav "$OUT/cr_quad_inf.wav"
QL="$OUT/cr_quad_inf.log"
has '^deep: part 0 page 12 AMP col 3 REL 62 -> 127 (INF)' "$QL" && has '^deep: part 0 page 12 AMP col 3 REL 127 -> 61 (83ms)' "$QL" &&
    ok "AMP: KNOB 4 to the top shows INF, back down a time (61: 83ms): $OUT/cr_quad_inf_amp.ppm" ||
    bad "AMP REL INF text: $(grep '^deep:' "$QL" | head -2 | tr '\n' ' ')"
qv() { grep "part 0:" "$QL" | sed -n "${1}p" | sed -n 's/.*voices \([0-9]*\),.*/\1/p'; }
[ "$(qv 1)" = 1 ] && [ "$(qv 2)" = 0 ] && [ "$(qv 3)" = 1 ] && [ "$(qv 4)" = 0 ] &&
    ok "voices: INF held 1.5 s after the release (1), REL 61 ended (0), INF held again (1), PANIC ended it (0)" ||
    bad "voices after the releases: $(qv 1) $(qv 2) $(qv 3) $(qv 4) (want 1 0 1 0)"
# the WAV (its clock: note 1's onset, script 3.3 s): RMS before each release (script 3.7..4.0 s, 8.4..8.7 s) and
# 1.2..1.5 s after it (5.2..5.5 s, 9.9..10.2 s)
iv=$(python3 - "$OUT/cr_quad_inf.wav" <<'PY'
import sys, wave, struct, math
w = wave.open(sys.argv[1]); sr = w.getframerate(); ch = w.getnchannels()
raw = w.readframes(w.getnframes()); x = struct.unpack('<%dh' % (len(raw) // 2), raw)
def rms(t0, t1):
    a, b = int(t0 * sr), int(t1 * sr)
    return math.sqrt(sum(x[i * ch] ** 2 for i in range(a, b)) / max(b - a, 1))
t0 = next(t / 100 for t in range(0, int(len(x) / ch / sr * 100)) if rms(t / 100, t / 100 + 0.01) > 50) - 3.3
print(*('%d' % rms(t0 + a, t0 + a + 0.3) for a in (3.7, 5.2, 8.4, 9.9)))
PY
)
set -- $iv
[ "${1:-0}" -gt 100 ] && [ $((${2:-0} * 10)) -gt $((${1:-0} * 3)) ] && [ "${3:-0}" -gt 100 ] && [ $((${4:-999} * 20)) -lt "${3:-0}" ] &&
    ok "the WAV: INF 1.5 s after the release RMS ${2} (held: ${1} before), REL 61 RMS ${4} (${3} before)" ||
    bad "the WAV: INF ${1:-?} -> ${2:-?}, REL 61 ${3:-?} -> ${4:-?} (want held, then silent)"
silent_end cr_quad_inf

echo "SAFE MODE: the boot guard (firmware/src/cr_bootguard.h)"
SF="$OUT/cr_safe_flash.bin"
rm -f "$SF"
"$EMU" --headless --flash "$SF" --script "$S/persist_set.txt" >"$OUT/cr_safe_data.log" 2>&1   # data: 137 BPM, Advanced
grep -q 'settings saves 1 ' "$OUT/cr_safe_data.log" && ok "a flash with data (persist_set: 137 BPM saved)" || bad "no data saved"
cp "$SF" "$OUT/cr_safe_flash_before.bin"
run cr_safe --flash "$SF" --boot-fail 1 --reset-reason wdt --boot-stage 13 --wav "$OUT/cr_safe.wav"
L="$OUT/cr_safe.log"
has '^boot: safe reset wdt failed 2 pending 1 counted 1 prev_stage 13 (fm6 bank)' "$L" &&
    ok "a crash-type reset with one failed boot before: SAFE MODE, breadcrumb 13 (fm6 bank)" || bad "not SAFE: $(grep -m1 '^boot:' "$L")"
has 'record: defaults' "$L" && has 'bpm 120 ' "$L" && ! has 'bpm 137' "$L" &&
    ok "the settings record not loaded (defaults: 120 BPM, not the saved 137)" || bad "safe mode loaded the record"
has 'part 0: FM6 / TINE EP, voices 3' "$L" && has '^expect sound .*: ok' "$L" &&
    ok "plays in SAFE MODE: D major on the factory TINE EP" || bad "no sound in safe mode"
cmp -s "$SF" "$OUT/cr_safe_flash_before.bin" && ok "the flash untouched by a safe session (no save)" || bad "safe mode wrote the flash"
has 'boot: safe reset wdt failed 0 pending 0' "$L" && ok "30 s up: failed and pending cleared, the session stays safe" || bad "no 30 s clear"
for n in cr_safe_splash cr_safe_play cr_safe_options cr_safe_erase_entry cr_safe_erase_ask; do
    [ -s "$OUT/$n.ppm" ] || bad "no $n.ppm"
done
y=$(yellow_in "$OUT/cr_safe_splash.ppm" 120 0 240)
[ "${y:-0}" -gt 100 ] && ok "the SAFE MODE screen (yellow panel, $y px in row 120): $OUT/cr_safe_splash.ppm" || bad "no SAFE MODE screen: $y"
differ cr_safe_options cr_safe_erase_entry "Options opens on Safe Mode; SELECT +1: Flash Data: $OUT/cr_safe_erase_entry.ppm"
differ cr_safe_erase_entry cr_safe_erase_ask "OCT+ once: the confirmation (OCT+ again): $OUT/cr_safe_erase_ask.ppm"
"$EMU" --headless --flash "$SF" --script "$S/cr_safe_erase.txt" --boot-fail 1 --reset-reason wdt >"$OUT/cr_safe_erase.log" 2>&1
st=$?
[ $st = 0 ] && has '^safe: flash data erased, 56 sectors, 0 failed: reboot' "$OUT/cr_safe_erase.log" &&
    has '^reboot: guard failed 0 pending 0' "$OUT/cr_safe_erase.log" &&
    ok "OCT+ twice: 56 data sectors erased (36 + the CZ-1's 18 + FM TONE's 2), reboot with the guard clear" || bad "erase: exit $st, $(grep -m1 'safe:\|reboot' "$OUT/cr_safe_erase.log")"
ff=$(python3 -c "
import sys; d = open(sys.argv[1], 'rb').read()
r = [(0x97000, 9), (0xA0000, 20), (0xC8000, 20), (0xDC000, 4), (0xFC000, 3)]
print(int(all(d[a:a + n * 4096] == b'\xff' * (n * 4096) for a, n in r) and d[:0x93000] == open(sys.argv[2], 'rb').read()[:0x93000]))" "$SF" "$OUT/cr_safe_flash_before.bin")
[ "$ff" = 1 ] && ok "the data regions read erased, the firmware area as before" || bad "flash after the erase"
"$EMU" --headless --flash "$SF" --script "$S/persist_check.txt" >"$OUT/cr_safe_after.log" 2>&1
has '^boot: normal reset power-on failed 0' "$OUT/cr_safe_after.log" && has 'bpm 120 ' "$OUT/cr_safe_after.log" &&
    ok "the next power-on: NORMAL on the erased flash (120 BPM)" || bad "after the erase: $(grep -m1 '^boot:' "$OUT/cr_safe_after.log")"
"$EMU" --headless --script "$S/cr_safe.txt" --boot-fail 1 --reset-reason poweron >"$OUT/cr_safe_pwr.log" 2>&1
has '^boot: normal reset power-on failed 0 pending 1 counted 0' "$OUT/cr_safe_pwr.log" &&
    ok "a power-on after failed boots: NORMAL, the guard clear" || bad "power-on: $(grep -m1 '^boot:' "$OUT/cr_safe_pwr.log")"
"$EMU" --headless --script "$S/cr_safe.txt" --boot-fail 3 --reset-reason wdt >"$OUT/cr_safe_uboot.log" 2>&1
st=$?
[ $st = 3 ] && has '^boot: UBOOT (ROM boot): reset wdt' "$OUT/cr_safe_uboot.log" &&
    ok "SAFE MODE crashed twice more (failed 4): UBOOT" || bad "uboot: exit $st"

echo "the second core (docs/DUALCORE.md): the bass part's job, GEEK OUT's c1 line, 8 + 1 voices, the fail-safes"
c1line() { LC_ALL=C grep -a -o "line3 '[^']*'" "$1" | tail -1; }
c1gu() { LC_ALL=C grep -a 'voices: given up' "$1" | tail -1 | sed -n 's/.*given up \([0-9]*\).*/\1/p'; }
export EMU_STAGES=1 EMU_UI_LOG=0
run cr_dualcore --wav "$OUT/cr_dualcore.wav"
unset EMU_STAGES EMU_UI_LOG
[ -s "$OUT/cr_dualcore_geek.ppm" ] && cp "$OUT/cr_dualcore_geek.ppm" "$OUT/cr_dualcore_geek_ok.ppm" || bad "no cr_dualcore_geek.ppm"
l=$(c1line "$OUT/cr_dualcore.log"); g=$(c1gu "$OUT/cr_dualcore.log")
echo "$l" | grep -q "c1 ok [1-9]" && ok "GEEK OUT: $l ($OUT/cr_dualcore_geek_ok.ppm)" || bad "GEEK OUT's c1 line: ${l:-none}"
[ "${g:-x}" = 0 ] && ok "core 1 alive: the 8-note chord and the bass all sound (none given up)" || bad "core 1 alive: ${g:-?} given up"
LC_ALL=C grep -aq '^cpu: stages .* c1 ok, jobs [1-9]' "$OUT/cr_dualcore.log" &&
    ok "$(LC_ALL=C grep -a '^cpu: stages' "$OUT/cr_dualcore.log" | cut -c6-)" || bad "no stage profile"
for m in nostart hang; do
    want="failed"; [ $m = hang ] && want="gave up"
    EMU_C1=$m EMU_UI_LOG=0 "$EMU" --headless --script "$S/cr_dualcore.txt" --wav "$OUT/cr_dualcore_$m.wav" >"$OUT/cr_dualcore_$m.log" 2>&1
    l=$(c1line "$OUT/cr_dualcore_$m.log"); g=$(c1gu "$OUT/cr_dualcore_$m.log")
    echo "$l" | grep -q "c1 $want" && [ "${g:-0}" -ge 1 ] && ok "EMU_C1=$m: $l, one core's shared budget ($g given up)" ||
        bad "EMU_C1=$m: ${l:-none}, ${g:-?} given up"
done
EMU_C1=nostart "$EMU" --headless --script "$S/cr_dmaj.txt" --wav "$OUT/cr_dmaj_onecore.wav" >/dev/null 2>&1
cmp -s "$OUT/cr_dmaj.wav" "$OUT/cr_dmaj_onecore.wav" && ok "cr_dmaj: the split's WAV = one core's, bit for bit" ||
    bad "cr_dmaj: the split's WAV differs from one core's"
cmp -s "$OUT/cr_dualcore_hang.wav" "$OUT/cr_dualcore_nostart.wav" && ok "a hung job from the first block: one core's WAV" ||
    bad "a hung job's WAV differs from one core's"

echo "determinism"
mkdir -p "$OUT/cr_again"
cp "$OUT/cr_dmaj.ppm" "$OUT/cr_again/first.ppm"
"$EMU" --headless --script "$S/cr_dmaj.txt" --wav "$OUT/cr_again/cr_dmaj.wav" >/dev/null 2>&1
cmp -s "$OUT/cr_dmaj.wav" "$OUT/cr_again/cr_dmaj.wav" && ok "the same audio on a second run" || bad "audio differs"
cmp -s "$OUT/cr_dmaj.ppm" "$OUT/cr_again/first.ppm" && ok "the same LCD on a second run" || bad "LCD differs"

[ $fail -eq 0 ] && echo "PASS" || echo "FAIL"
exit $fail
