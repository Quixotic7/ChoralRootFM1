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
# screen off (cr_screen_off: a black frame with PA2 low, the LEDs lit; a key, a knob or MIDI in wakes it and acts, a
# loop playing does not keep it on; SysEx 74 SCREEN; cr_pa2: the emulator's PA2 model, high = the key matrix dead);
# after every release, parts 0 and 1 silent within 2 s.
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

# One script is a third of this file's time (cr_screen_off: the idle minutes, about 12 s of the emulator): it starts here, in the
# background, and `run cr_screen_off` below waits for it and reads its exit status as if it had run there. Nothing
# else reads or writes its files ($OUT/cr_screen_off.log, $OUT/cr_off_*.ppm: only its own block), and a headless
# run without --flash keeps no state.
ahead() { "$EMU" --headless --script "$S/$1.txt" >"$OUT/$1.log" 2>&1 & eval "ahead_$1=\$!"; }
ahead cr_screen_off
run() {   # run NAME [options]: tools/emu/scripts/NAME.txt to its end, the log in $OUT/NAME.log
    name=$1; shift
    eval "st=\${ahead_$name:-}"
    if [ -n "$st" ] && [ $# -eq 0 ]; then
        eval "ahead_$name="
        wait "$st"
    else
        "$EMU" --headless --script "$S/$name.txt" "$@" >"$OUT/$name.log" 2>&1
    fi
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

echo "the version: one string (choralroot.c's FELUCCA_VERSION) in Options > Version, the SysEx identity, the splash"
run cr_version
L="$OUT/cr_version.log"
want=$(sed -n 's/^#define FELUCCA_VERSION "\(.*\)".*/\1/p' firmware/src/choralroot.c | head -1)
[ -n "${CR_VERSION:-}" ] && want=$(grep -a -m1 "^  version:" "$L" | sed -n "s/.* build '\(.*\)'$/\1/p")   # (a release build of the emulator)
vo=$(grep -a -m1 '^  version:' "$L" | sed -n "s/.*options '\([^']*\)'.*/\1/p")
vi=$(grep -a -m1 '^  version:' "$L" | sed -n "s/.*identity '\([^']*\)'.*/\1/p")
[ -n "$want" ] && [ "$vo" = "$want" ] && [ "$vi" = "$want" ] && grep -aq "^  options: open 1 entry 31 of 3[0-9] 'Version' value '$want'" "$L" &&
    ok "Options > Version '$vo' = the identity over SysEx '$vi' = the build's ($want): $OUT/cr_version.ppm" \
    || bad "the version: Options '$vo', identity '$vi', choralroot.c '$want'"
lits=$(grep -n '"ChoralRoot [0-9]' firmware/src/*.c firmware/src/*.h firmware/hal/*.h tools/emu/*.c tools/emu/*.h tools/emu/web/*.c 2>/dev/null | grep -v '^firmware/src/choralroot.c:[0-9]*:#define FELUCCA_VERSION ')
[ -z "$lits" ] && ok "no other version literal in the firmware or the emulators (one #define, firmware/src/choralroot.c)" || bad "a version literal of its own: $lits"

echo "MAJ held + D4: D major"
run cr_dmaj --wav "$OUT/cr_dmaj.wav"
nz=$(num "non-silent blocks" "$OUT/cr_dmaj.log")
[ "${nz:-0}" -gt 0 ] && ok "sound: $nz non-silent blocks" || bad "no sound"
has 'expect led D4 on .*: ok' "$OUT/cr_dmaj.log" && has 'expect led F#4 on .*: ok' "$OUT/cr_dmaj.log" &&
    has 'expect led A4 on .*: ok' "$OUT/cr_dmaj.log" && has 'expect led D5 on .*: ok' "$OUT/cr_dmaj.log" && ! has 'expect.*FAILED' "$OUT/cr_dmaj.log" &&
    ok "the D4 F#4 A4 LEDs lit, and D5 F#5 (every root of a sounding note's name)" || bad "the sounding notes' LEDs"
has 'chord DM sounding 1 notes 62 66 69' "$OUT/cr_dmaj.log" && ok "the engine: DM (a major triad reads with its M), notes 62 66 69 on part 0" || bad "not D major"
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

echo "the screen off: the logo at 3 min, a black frame at 10 min (the backlight on: PA2 low); a key / a knob / MIDI in wakes it and acts; a loop is not input; SysEx 74 SCREEN"
run cr_screen_off
SO="$OUT/cr_screen_off.log"
lit() { od -An -tu1 -v -j 15 "$1" | tr -s ' \n' '\n\n' | grep -v '^$' | awk '$1 > 24 { c++ } END { print c + 0 }'; }   # bytes > 24
ink() { od -An -tu1 -v -j 15 "$1" | tr -s ' \n' '\n\n' | grep -v '^$' | awk '$1 > 0 { c++ } END { print c + 0 }'; }    # bytes > 0 (0: the black frame)
blk() {          # blk FILE: pixels 255 0 255, the script's lcdfill before each wake, where the wake did not redraw (no
                 # view draws magenta; MOD's background is black, so the black frame itself cannot mark them)
    od -An -tu1 -v -j 15 "$1" | tr -s ' \n' '\n\n' | grep -v '^$' |
        awk '{ v[NR % 3] = $1; if (NR % 3 == 0) { if (v[1] == 255 && v[2] == 0 && v[0] == 255) c++ } } END { print c + 0 }'
}
dmp() {          # dmp SHOT PATTERN: log $DL's first line matching PATTERN after the shot $DP SHOT (its dump, its expects)
    awk -v s="screenshot: build/emu/test/$DP$1.ppm" -v p="$2" 'f && $0 ~ p { print; exit } $0 == s { f = 1 }' "$DL"
}
DL=$SO; DP=cr_off_
scr() { dmp "$1" '^  lcd:' | sed -n 's/.*screen \([a-z]*\).*/\1/p'; }                # on / off
wrs() { dmp "$1" '^  lcd:' | sed -n 's/.* \([0-9][0-9]*\) writes.*/\1/p'; }         # LCD writes so far
met() { dmp "$1" "^expect $2" | grep -q ': ok'; }                                    # that shot's next "expect $2" held
# PA2 is not the backlight alone: high, the key matrix / 595 chain is dead (hal/fm1_lcd_hw.h; the unit, 2026-10-10: the
# screen off of 0.14 / 0.15 raised it and no key sounded or woke it). The emulator models it (emu_hooks.h pa2)
nd=$(grep -c '^  pa2: ' "$SO")
[ "$nd" = 13 ] && [ "$(grep -c '^  pa2: low.* high for 0 ms' "$SO")" = "$nd" ] &&
    ok "PA2 low in all $nd dumps, never raised: the backlight on, the key matrix, the knobs and the LEDs alive" ||
    bad "PA2 driven high ($(grep -c '^  pa2: HIGH' "$SO") of $nd dumps): on the unit no key, button, knob or LED works then (hal/fm1_lcd_hw.h)"
p=$(pixel "$OUT/cr_off_logo.ppm" 120 150); set -- $p
[ "${1:-0}" -gt 180 ] && [ "${2:-255}" -lt 90 ] && ok "3 min: the logo ($p): $OUT/cr_off_logo.ppm" || bad "no logo at 3 min ($p)"
[ "$(scr dark)" = off ] && [ "$(ink "$OUT/cr_off_dark.ppm")" = 0 ] && [ "$(lit "$OUT/cr_off_view.ppm")" -gt 1000 ] &&
    has '^screen: off (idle)' "$SO" &&
    ok "10 min: the screen off: every pixel of the panel black: $OUT/cr_off_dark.ppm" ||
    bad "10 min: screen $(scr dark), the panel not black ($(ink "$OUT/cr_off_dark.ppm") bytes above 0)"
[ "$(scr dark2)" = off ] && [ -n "$(wrs dark)" ] && [ "$(wrs dark)" = "$(wrs dark2)" ] && [ "$(ink "$OUT/cr_off_dark2.ppm")" = 0 ] &&
    ok "no LCD write in the minute after ($(wrs dark) = $(wrs dark2) writes), still black" || bad "LCD writes while off: $(wrs dark) $(wrs dark2)"
met dark2 'led C4 faint' && met dark2 'led FX on' && met loop 'led GREEN on' && met sx_midi 'led C4 faint' &&
    ok "the LEDs while it is off: a chord key's faint glow, FX lit, PLAY's green LED with the loop" || bad "an LED dark while the screen is off"
[ "$(scr key)" = on ] && [ "$(lit "$OUT/cr_off_key.ppm")" -gt 1000 ] && [ "$(blk "$OUT/cr_off_key.ppm")" = 0 ] &&
    dmp key '^  part 0:' | grep -q 'voices 3' && met key sound && met key 'led D4 on' &&
    ok "a key: the screen on, the whole view back (no black left), the chord sounding, its LED lit: $OUT/cr_off_key.ppm" ||
    bad "a key did not wake it and play (screen $(scr key),$(dmp key '^  part 0:' | sed 's/.*,\( voices [0-9]*\).*/\1/'))"
[ "$(scr again)" = off ] && [ "$(ink "$OUT/cr_off_again.ppm")" = 0 ] && [ "$(scr knob)" = on ] && [ "$(lit "$OUT/cr_off_knob.ppm")" -gt 1000 ] &&
    [ "$(blk "$OUT/cr_off_knob.ppm")" = 0 ] && dmp knob '^  cr: key' | grep -q 'voicing 2 ' &&
    ok "10 min more: off; a knob turn: on, and KNOB 1 turned (voicing 2): $OUT/cr_off_knob.ppm" || bad "a knob did not wake it / act ($(scr again) $(scr knob))"
[ "$(scr loop)" = off ] && [ "$(ink "$OUT/cr_off_loop.ppm")" = 0 ] && dmp looping '^  loop:' | grep -q 'state 2 ' &&
    dmp loop '^  loop:' | grep -q 'state 2 ' && met loop sound &&
    ok "a loop playing alone: the screen off after 10 min, the loop still playing: $OUT/cr_off_loop.ppm" || bad "loop: screen $(scr loop)"
[ "$(scr midi)" = on ] && [ "$(lit "$OUT/cr_off_midi.ppm")" -gt 1000 ] && [ "$(blk "$OUT/cr_off_midi.ppm")" = 0 ] &&
    ok "MIDI in (a note): the idle screen off ends, the view back: $OUT/cr_off_midi.ppm" || bad "MIDI in did not wake the idle screen off ($(scr midi))"
[ "$(scr sx)" = off ] && [ "$(ink "$OUT/cr_off_sx.ppm")" = 0 ] && has '^screen: off (sysex)' "$SO" && dmp sx '^  loop:' | grep -q 'state 1 ' &&
    ok "SysEx 74 SCREEN 0: off at once, the panel black: $OUT/cr_off_sx.ppm" || bad "SCREEN 0: screen $(scr sx)"
[ "$(scr sx_midi)" = off ] && [ -n "$(wrs sx)" ] && [ "$(wrs sx)" = "$(wrs sx_midi)" ] && [ "$(ink "$OUT/cr_off_sx_midi.ppm")" = 0 ] && met sx_midi sound &&
    ok "MIDI in while SCREEN 0: the note plays, the screen stays off ($(wrs sx) = $(wrs sx_midi) writes)" ||
    bad "MIDI in while SCREEN 0: screen $(scr sx_midi), $(wrs sx) $(wrs sx_midi) writes"
[ "$(scr sx_key)" = on ] && [ "$(lit "$OUT/cr_off_sx_key.ppm")" -gt 1000 ] && [ "$(blk "$OUT/cr_off_sx_key.ppm")" = 0 ] &&
    dmp sx_key '^  part 0:' | grep -q 'voices 3' && met sx_key sound && met sx_key 'led D4 on' &&
    ok "a key while SCREEN 0: the screen on, the chord sounding, its LED lit: $OUT/cr_off_sx_key.ppm" ||
    bad "a key did not wake SCREEN 0 and play (screen $(scr sx_key),$(dmp sx_key '^  part 0:' | sed 's/.*,\( voices [0-9]*\).*/\1/'))"
[ "$(scr sx2)" = off ] && [ "$(ink "$OUT/cr_off_sx2.ppm")" = 0 ] && [ "$(scr sx_on)" = on ] && [ "$(lit "$OUT/cr_off_sx_on.ppm")" -gt 1000 ] &&
    [ "$(blk "$OUT/cr_off_sx_on.ppm")" = 0 ] &&
    ok "SCREEN 0 again, then SCREEN 1: off, on: $OUT/cr_off_sx_on.ppm" || bad "SCREEN 1: screen $(scr sx2) $(scr sx_on)"

echo "the emulator's PA2, the model itself (the script drives it high by hand, as the screen off of 0.14 / 0.15 did)"
run cr_pa2
DL="$OUT/cr_pa2.log"; DP=cr_pa2_
dmp before '^  pa2:' | grep -q 'low.* high for 0 ms' && dmp before '^  part 0:' | grep -q 'voices 3' && [ "$(lit "$OUT/cr_pa2_before.ppm")" -gt 1000 ] &&
    ok "PA2 low: the view on the panel, the chord sounding" || bad "PA2 low: $(dmp before '^  pa2:')"
dmp high '^  pa2:' | grep -q 'HIGH' && [ "$(ink "$OUT/cr_pa2_high.ppm")" = 0 ] && met high 'led C4 off' && met high 'led FX off' &&
    ok "PA2 high: the dump says it, the panel black (the backlight off), no LED lit" || bad "PA2 high: $(dmp high '^  pa2:')"
dmp dead '^fm1_in:' | grep -q 'notes 0000000 buttons 0000 ' && dmp dead '^  part 0:' | grep -q 'voices 0' &&
    dmp dead '^  cr: key' | grep -q 'voicing 0 ' && met dead silence &&
    ok "PA2 high: a held chord and a knob turn are not read (no key in fm1_in, no voice, voicing 0, silence)" ||
    bad "PA2 high: the matrix still reads ($(dmp dead '^fm1_in:' | cut -c1-40))"
dmp low '^  pa2:' | grep -q 'low' && [ "$(lit "$OUT/cr_pa2_low.ppm")" -gt 1000 ] && met low 'led C4 faint' && met low 'led FX on' &&
    dmp after '^  part 0:' | grep -q 'voices 3' && met after sound && met after 'led D4 on' &&
    ok "PA2 low again: the panel, the LEDs and the keys back" || bad "PA2 low again: $(dmp low '^  pa2:')"
silent_end cr_pa2

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
has '^fx: cells Size Damp Decay Send' "$L" && ok "FX held, SELECT +1: Reverb over Size / Damp / Decay / Send: $OUT/cr_fx_row.ppm" || bad "no Reverb cells"
has '^fx: knob 2 Reverb damp' "$L" && ok "KNOB 2: Reverb's damp ($(grep '^fx: knob 2' "$L" | head -1 | sed 's/^fx: //'))" || bad "KNOB 2: no damp change"
fr=$(grep -c '^ui: frame' "$L"); kr=$(grep -c '^ui: frame.*device): knobrow ' "$L"); mt=$(grep -c '^ui: frame.*device): meter ' "$L")
[ "$mt" = 0 ] && [ "$kr" -gt 100 ] && ok "no popup: every frame in the layer a knob row ($kr knobrow frames of $fr, 0 meters)" \
    || bad "a popup in the fx layer ($mt meter frames, $kr knobrow)"
grep -q '^ui: frame.*device): knobrow .*hot 1\.1$' "$L" && grep -q '^ui: frame.*device): knobrow .*hot 1\.3$' "$L" &&
    ok "the turned cell hot (KNOB 2: cell 1, KNOB 4: cell 3)" || bad "no hot cell in the trace"
h=$(px_count "$OUT/cr_fx_row_hot.ppm" 64 176 116 196 ink); c=$(px_count "$OUT/cr_fx_row_cool.ppm" 64 176 116 196 ink)
[ "$h" -gt $((c + 300)) ] && ok "the hot block behind Damp's value ($h ink px), gone 900 ms later ($c): $OUT/cr_fx_row_hot.ppm" \
    || bad "hot block: $h px hot, $c after"
has '^fx: cells Time Spread Feedback Send' "$L" && has '^fx: cells Rate Depth Width Send' "$L" && has '^fx: knob 3 Chorus width 126$' "$L" && has '^fx: cells - - - Send' "$L" &&
    ok "SELECT picks the effect, the cells follow (Delay, Chorus with its Width on KNOB 3 (0.22: 127 -> 126 a slow detent, cr_pages.c kn_lin), Drive the send only): $OUT/cr_fx_row_delay.ppm" || bad "the cells did not follow the effect"
has '^fx: effect Reverb 2/2' "$L" && has '^fx: cells Pre-delay Mod Low cut Width' "$L" && has '^fx: effect Delay 2/2' "$L" &&
    has '^fx: cells Sync Mode Tone Wow' "$L" && [ "$(grep -c '^fx: effect' "$L")" = 7 ] &&
    [ "$(grep -a '^fx: cells' "$L" | head -1)" = "fx: cells Drive Chorus Delay Reverb" ] &&
    ok "SELECT steps the pages after Sends: Reverb 1, Reverb 2 (Pre-delay / Mod / Low cut / Width), Chorus, Delay 1, Delay 2 (Sync / Mode / Tone / Wow), Drive: $OUT/cr_fx_row_reverb2.ppm, $OUT/cr_fx_row_delay2.ppm" \
    || bad "the FX layer's pages: $(grep -c '^fx: effect' "$L") page changes"
differ cr_fx_row cr_fx_row_reverb2 "Reverb 1 / Reverb 2 pages differ"
differ cr_fx_row_delay cr_fx_row_delay2 "Delay 1 / Delay 2 pages differ"
has '^fx: knob 1 Delay sync 0' "$L" && has '^fx: knob 2 Delay mode 0' "$L" && differ cr_fx_row_delay2 cr_fx_row_delay2_free "Delay 2: Sync to Free (the clock face), Mode to Stereo: $OUT/cr_fx_row_delay2_free.ppm"
# Time's knob: clockwise is FASTER (the traces of KNOB 1 on Delay 1, in order: synced +3, -3, then free +1; the
# division's index and the free time's 0..127 are the stored values, larger = longer)
tm=$(sed -n 's/^fx: knob 1 Delay time \([0-9]*\)$/\1/p' "$L" | tr '\n' ' ')
[ "$tm" = "3 6 90 " ] && ok "Delay's Time: KNOB 1 clockwise is faster (synced 1/8 -> 1/16 and back: division 6 -> 3 -> 6, a division a detent; free 91 -> 90, a slow detent: fewer ms)" \
    || bad "Delay's Time: KNOB 1 +3, -3 synced and +1 free gave '$tm' (3 6 90)"
bar2() {   # bar2 FILE: on row 170 (the bars' feet, above the baseline) of the Time cell, the x where the second bar starts
    od -An -tu1 -v -j $((15 + 170 * 240 * 3)) -N $((60 * 3)) "$1" | awk '
        { for (i = 1; i <= NF; i++) v[n++] = $i }
        END { r = 0; for (x = 0; x < 60; x++) { k = v[3*x] + v[3*x+1] + v[3*x+2] > 90; if (k && !on) r++; if (r == 2) { print x; exit } on = k } print 0 }'
}
ws=$(bar2 "$OUT/cr_fx_row_amount.ppm"); wf=$(bar2 "$OUT/cr_fx_row_delay_faster.ppm")
[ "$wf" -gt 0 ] && [ "$wf" -lt "$ws" ] && ok "the Time cell's echoes: closer together three divisions faster (the second bar at x $ws -> $wf): $OUT/cr_fx_row_delay_faster.ppm" \
    || bad "the Time cell's echoes at 1/8 and 1/16: the second bar at x $ws -> $wf (nearer expected)"
differ cr_fx_row_amount cr_fx_row_delay_free "Delay 1 with Sync off: Time and Spread are the free ones (milliseconds, a ratio): $OUT/cr_fx_row_delay_free.ppm"
has '^expect sound .*: ok' "$L" && has '^expect led D4 on .*: ok' "$L" &&
    ok "MAJ + D4 in the fx layer: the chord sounds, its LEDs are the chord's (the roots play there)" || bad "the roots in the fx layer: $(grep '^expect' "$L")"
differ cr_fx_row cr_fx_row_delay "Reverb / Delay rows differ"
has '^fx: knob 4 Delay send' "$L" && differ cr_fx_row_delay cr_fx_row_amount "KNOB 4: Delay's send cell changed (hot): $OUT/cr_fx_row_amount.ppm"
differ cr_fx_row_chorus cr_fx_row_drive_off "Drive with FX off (the send reads off): $OUT/cr_fx_row_drive_off.ppm"

echo "the effects and the sends are the instrument's: no sound load touches them (docs/FX.md)"
run cr_fx_global
L="$OUT/cr_fx_global.log"
fxl() { grep -a '^  session fx:' "$L" | sed -n "$1p"; }
fxpar() { fxl "$1" | sed 's/.* par //'; }
fxamt() { fxl "$1" | sed -n 's/.* amt \([0-9 ]*\) sends.*/\1/p'; }
fxsnd() { fxl "$1" | sed -n 's/.* sends \([0-9 ]*\) par.*/\1/p'; }
has '^fx: knob 1 Reverb size 118' "$L" && has '^fx: knob 3 Reverb decay 76' "$L" && has '^fx: knob 3 Delay feedback 34' "$L" &&
    fxpar 1 | grep -q ' 13=34 .* 18=118 .* 20=76 ' &&
    ok "the reverb's Size and Decay and the delay's Feedback turned (+5, +4, -4 in a frame each: quick, 6 a detent): in the session's FX block (tags 18, 20, 13)" || bad "the FX parameters: $(fxpar 1)"
[ -n "$(fxpar 1)" ] && [ "$(fxpar 1)" = "$(fxpar 2)" ] && [ "$(fxamt 1)" = "53 45 25 0" ] && [ "$(fxamt 2)" = "$(fxamt 1)" ] &&
    [ "$(fxamt 2)" = "$(fxsnd 2)" ] && has '^sound: part 0 pos 4 BRASS' "$L" && has '^sound: part 0 engine VA' "$L" &&
    fxpar 2 | grep -q ' 26=10 27=0 28=0 29=0 ' &&
    ok "PRESETS +3 (another sound), OPT + PRESETS +2 (another engine): every effect parameter and the sends as they were ($(fxamt 2); the bass part's: reverb 10)" \
    || bad "a sound load and the effects: par $(fxpar 1) -> $(fxpar 2), amt $(fxamt 1) -> $(fxamt 2), sends $(fxsnd 2)"
[ "$(fxsnd 3)" = "0 0 0 0" ] && [ "$(fxamt 3)" = "$(fxamt 2)" ] && [ "$(fxsnd 4)" = "$(fxamt 2)" ] && [ "$(fxpar 3)" = "$(fxpar 1)" ] &&
    ok "FX tapped off: the sends closed, the amounts and the effects' parameters kept; on again: the sends back" \
    || bad "FX off / on: sends $(fxsnd 3) / $(fxsnd 4), amt $(fxamt 3)"

echo "a part's send is one value: the FX layer's Sends page, the editor's FX group, CC 91, FM TONE's Drive (docs/FX.md)"
run cr_fx_sends
L="$OUT/cr_fx_sends.log"
fxl() { grep -a '^  session fx:' "$L" | sed -n "$1p"; }
fxamt() { fxl "$1" | sed -n 's/.* amt \([0-9 ]*\) sends.*/\1/p'; }
fxsnd() { fxl "$1" | sed -n 's/.* sends \([0-9 ]*\) par.*/\1/p'; }
edc() { grep -a '^  editor fx chord:' "$L" | sed -n "$1p" | sed 's/^  editor fx chord: //; s/ track.*//'; }   # as the editor's cells read
edb() { grep -a '^  editor fx bass:' "$L" | sed -n "$1p" | sed 's/^  editor fx bass: //; s/ track.*//'; }
[ "$(grep -a '^fx: cells' "$L" | head -1)" = "fx: cells Drive Chorus Delay Reverb" ] && fxl 1 | grep -q '(Sends 1/1)' &&
    ok "FX held: the layer opens on Sends, Drive / Chorus / Delay / Reverb on KNOB 1..4 (the editor's FX group's order and names): $OUT/cr_fx_sends.ppm" \
    || bad "the FX layer's first page: $(grep -a '^fx: cells' "$L" | head -1), $(fxl 1 | cut -c1-48)"
has '^fx: knob 1 Drive send 12' "$L" && has '^fx: knob 2 Chorus send 39' "$L" && has '^fx: knob 3 Delay send 31' "$L" &&
    has '^fx: knob 4 Reverb send 53' "$L" && [ "$(fxamt 1)" = "53 39 31 12" ] && [ "$(fxsnd 1)" = "53 39 31 12" ] &&
    ! cmp -s "$OUT/cr_fx_sends.ppm" "$OUT/cr_fx_sends_hot.ppm" &&
    ok "the Sends page: KNOB 1..4 set the chord part's four sends (the editor's step; reverb chorus delay drive $(fxamt 1)): $OUT/cr_fx_sends_hot.ppm" \
    || bad "the Sends page's knobs: amt $(fxamt 1), sends $(fxsnd 1)"
has '^fx: knob 4 Reverb send 59' "$L" && [ "$(fxamt 2)" = "59 39 31 12" ] &&
    [ "$(edc 2)" = "DST 12 9% CHO 39 31% DLY 31 24% REV 59 46%" ] && ! cmp -s "$OUT/cr_fx_sends_hot.ppm" "$OUT/cr_fx_sends_back.ppm" &&
    ok "Reverb 1's Send cell (KNOB 4) and the Sends page are the same value; the editor's FX group reads the four ($(edc 2)): $OUT/cr_fx_sends_edit.ppm" \
    || bad "the FX layer's sends in the editor: amt $(fxamt 2), the editor: $(edc 2)"
has '^param: part 0 FX CHO 39 -> 45' "$L" && [ "$(fxamt 3)" = "59 45 31 12" ] && [ "$(edc 3)" = "DST 12 9% CHO 45 35% DLY 31 24% REV 59 46%" ] &&
    ! cmp -s "$OUT/cr_fx_sends_back.ppm" "$OUT/cr_fx_sends_from_edit.ppm" &&
    ok "the other way: the editor's KNOB 2 (chorus 39 -> 45), the Sends page reads it: $OUT/cr_fx_sends_from_edit.ppm" \
    || bad "the editor's send in the FX layer: amt $(fxamt 3), the editor: $(edc 3)"
[ "$(fxsnd 4)" = "0 0 0 0" ] && [ "$(fxamt 4)" = "59 45 31 12" ] && [ "$(edc 4)" = "$(edc 3)" ] &&
    cmp -s "$OUT/cr_fx_sends_edit2.ppm" "$OUT/cr_fx_sends_edit_off.ppm" &&
    ok "FX off (the track's sends closed): the editor's FX group still reads the part's four ($(edc 4)), the same screen as with FX on: $OUT/cr_fx_sends_edit_off.ppm" \
    || bad "FX off: the editor's FX group reads '$(edc 4)' (the sends are $(fxamt 4): '$(edc 3)')"
has '^param: part 0 FX REV 59 -> 65' "$L" && [ "$(fxamt 5)" = "65 45 31 12" ] && [ "$(fxsnd 5)" = "65 45 31 12" ] &&
    ok "FX off, the editor's KNOB 4: one step from the send as it was (reverb 59 -> 65), FX on again, the other three as they were" \
    || bad "FX off, a send turned in the editor: $(grep -a '^param: part 0 FX REV' "$L" | head -1), amt $(fxamt 5)"
has '^send: part 0 Reverb 64 (cc)' "$L" && has '^send: part 1 Reverb 32 (cc)' "$L" &&
    [ "$(edc 6)" = "DST 12 9% CHO 45 35% DLY 31 24% REV 64 50%" ] && [ "$(edb 6)" = "DST 0 0% CHO 0 0% DLY 0 0% REV 32 25%" ] &&
    [ "$(fxamt 7)" = "64 45 31 12" ] && [ "$(edc 7)" = "$(edc 6)" ] && has '^sound: part 0 pos 2 FM BELL' "$L" &&
    ok "CC 91 on the chord and the bass channel: the editor reads both (reverb 64, the bass part's 32); PRESETS +1: the sends stay" \
    || bad "CC 91 / a sound change: chord '$(edc 6)' -> '$(edc 7)', bass '$(edb 6)'"
has '^param: part 0 ENV DRIVE 0 -> 18' "$L" && [ "$(fxamt 8)" = "64 45 31 12" ] && [ "$(fxsnd 8)" = "64 45 31 18" ] &&
    [ "$(fxamt 9)" = "64 45 31 12" ] && [ "$(fxsnd 9)" = "0 0 0 18" ] && cmp -s "$OUT/cr_fx_sends_tone.ppm" "$OUT/cr_fx_sends_tone_off.ppm" &&
    ok "FM TONE's AMP Drive is the sound's (01 EP: 0 -> 18): in the track, the part's drive send (12) untouched; FX off: still in the track, its cell reads the same: $OUT/cr_fx_sends_tone.ppm" \
    || bad "FM TONE's Drive: amt $(fxamt 8) sends $(fxsnd 8); FX off: amt $(fxamt 9) sends $(fxsnd 9)"
[ "$(fxamt 10)" = "35 45 25 0" ] && [ "$(fxsnd 10)" = "35 45 25 0" ] && [ "$(edc 10)" = "DST 0 0% CHO 45 35% DLY 25 20% REV 35 28%" ] &&
    [ "$(edb 10)" = "DST 0 0% CHO 0 0% DLY 0 0% REV 10 8%" ] && fxl 10 | grep -q 'on 1 .*(Sends 1/1)' &&
    cmp -s "$OUT/cr_fx_sends.ppm" "$OUT/cr_fx_sends_reverted.ppm" &&
    ok "GLO + SAVE (the revert, nothing saved): both parts' sends a new unit's on both screens, the layer on Sends: $OUT/cr_fx_sends_reverted.ppm" \
    || bad "the revert: amt $(fxamt 10) sends $(fxsnd 10), the editor: '$(edc 10)' / '$(edb 10)'"

echo "the FX layer's Routing page: the signal flow as boxes, the black keys hold a block, KNOB 1 moves it, A#4 / KNOB 2 the mode (GitHub issue 21, second design; docs/FX.md \"The routing\")"
run cr_fx_route
L="$OUT/cr_fx_route.log"
rr() { grep -a '^  screen route:' "$L" | sed -n "$1p" | sed 's/^  screen route: //'; }              # "chain Chorus .. held none fx 1 filter 0"
rpar() { grep -a '^  session fx:' "$L" | sed -n "$1p" | sed -n 's/.* 30=[0-9]*\(.*\)$/\1/p'; }      # the routing's tags
[ "$(rr 1)" = "parallel Chorus Delay Reverb Filter held none fx 1 filter 0" ] && [ -z "$(rpar 1)" ] &&
    grep -a '^  session fx:' "$L" | head -1 | grep -q '(Routing 1/1)' && has '^fx: routing parallel Chorus > Delay > Reverb > Filter' "$L" &&
    grep -a '^  screen foot:' "$L" | head -1 | grep -q "hold C# D# F# G# + KNOB 1" &&
    ok "SELECT to the last page, Routing: Parallel, Chorus Delay Reverb Filter (the defaults, not written to the session): $OUT/cr_fx_route.ppm" \
    || bad "the Routing page: '$(rr 1)' par '$(rpar 1)'"
[ "$(rr 2)" = "chain Chorus Delay Reverb Filter held none fx 1 filter 0" ] && [ "$(rpar 2)" = " 36=1" ] &&
    ok "A#4 tapped: Chain (tag 36): $OUT/cr_fx_route_chain.ppm" || bad "A#4: '$(rr 2)' par '$(rpar 2)'"
[ "$(rr 3)" = "chain Chorus Delay Reverb Filter held Delay fx 1 filter 0" ] && has '^fx: routing hold Delay' "$L" &&
    [ "$(rr 4)" = "chain Chorus Reverb Delay Filter held Delay fx 1 filter 0" ] && [ "$(rpar 4)" = " 35=2 36=1" ] &&
    [ "$(rr 5)" = "chain Chorus Reverb Delay Filter held none fx 1 filter 0" ] && has '^fx: routing release Delay' "$L" &&
    ! cmp -s "$OUT/cr_fx_route_chain.ppm" "$OUT/cr_fx_route_lift.ppm" &&
    ok "D#4 held: Delay lifts; KNOB 1 +1: Chorus Reverb Delay Filter (Order 2, tag 35); D#4 up: it lands: $OUT/cr_fx_route_lift.ppm, $OUT/cr_fx_route_moving.ppm" \
    || bad "the Delay's move: '$(rr 3)' / '$(rr 4)' / '$(rr 5)' par '$(rpar 4)'"
[ "$(rr 6)" = "chain Chorus Filter Reverb Delay held Filter fx 1 filter 0" ] && [ "$(rpar 6)" = " 35=5 36=1" ] &&
    [ "$(rr 7)" = "chain Chorus Filter Reverb Delay held none fx 1 filter 0" ] && ! has '^touch: key 9 ' "$L" &&
    ok "G#4 held, KNOB 1 -2: Chorus Filter Reverb Delay (Order 5); D4 tapped: nothing plays, nothing moves: $OUT/cr_fx_route_cfrd.ppm" \
    || bad "the Filter's move / a white key: '$(rr 6)' / '$(rr 7)' par '$(rpar 6)'"
[ "$(rr 8)" = "parallel Chorus Filter Reverb Delay held none fx 1 filter 0" ] && [ "$(rpar 8)" = " 35=5" ] &&
    ok "KNOB 2 -1: Parallel, the order kept: $OUT/cr_fx_route_parallel.ppm" || bad "KNOB 2: '$(rr 8)' par '$(rpar 8)'"
[ "$(rr 9)" = "parallel Chorus Filter Reverb Delay held none fx 0 filter 0" ] &&
    [ "$(rr 10)" = "parallel Chorus Filter Reverb Delay held none fx 1 filter 0" ] && has '^fx: off' "$L" && has '^fx: on' "$L" &&
    grep -a '^  session fx:' "$L" | sed -n 10p | grep -q 'on 1 .*(Routing 1/1)' &&
    ok "FX off and on: the page stays, grey while off: $OUT/cr_fx_route_off.ppm" \
    || bad "FX off / on on the Routing page: '$(rr 9)' / '$(rr 10)'"

echo "a send's read-out and its step are ONE: per cent, 6 a detent, on the Sends page, an effect's Send cell, the editor, KNOB 4's popup, a CC's (docs/FX.md)"
run cr_fx_send_scale
L="$OUT/cr_fx_send_scale.log"
rd() { grep -a '^  screen reads:' "$L" | sed -n "$1p"; }
pop() { rd "$1" | sed -n "s/^  screen reads: value '\([^']*\)' sub '\([^']*\)' label '\([^']*\)'.*/\1\2 \3/p"; }   # "32% reverb"
cel() { rd "$1" | sed -n "s/.* [01]\.[0-3]:$2=\([^ ]*\).*/\1/p"; }                                         # cell LABEL's value
fxamt() { grep -a '^  session fx:' "$L" | sed -n "$1p" | sed -n 's/.* amt \([0-9 ]*\) sends.*/\1/p'; }
bsr() { grep -a '^  editor fx bass:' "$L" | sed -n "$1p" | sed -n 's/.* REV \([0-9]*\) .*/\1/p'; }
# the reverb send, a detent in each place: 35 -> 41 (KNOB 4 of the main view) -> 47 (the Sends page) -> 53 (Reverb 1's
# Send cell) -> 59 (the editor) -> 65 (KNOB 4 again); CC 91 = 59
[ "$(pop 1)" = "32% reverb" ] && [ "$(fxamt 1)" = "41 45 25 0" ] && [ "$(cel 2 Reverb)" = "32%" ] && [ "$(cel 2 Chorus)" = "35%" ] &&
    [ "$(cel 2 Delay)" = "20%" ] && [ "$(cel 2 Drive)" = "0%" ] &&
    ok "KNOB 4 of the main view, a detent: reverb 35 -> 41, its popup reads $(pop 1 | cut -d' ' -f1); the Sends page reads the same ($(cel 2 Reverb)): $OUT/cr_fx_scale_pop.ppm" \
    || bad "KNOB 4's popup and the Sends page: '$(pop 1)' / '$(cel 2 Reverb)', amt $(fxamt 1)"
[ "$(fxamt 3)" = "47 45 25 0" ] && [ "$(cel 3 Reverb)" = "37%" ] && [ "$(cel 4 Send)" = "37%" ] && [ "$(fxamt 5)" = "53 45 25 0" ] &&
    [ "$(cel 5 Send)" = "42%" ] && [ "$(cel 6 Reverb)" = "42%" ] &&
    ok "the Sends page's KNOB 4, a detent: 47, 37%; Reverb 1's Send cell reads 37% too; its KNOB 4, the same detent: 53, 42% in both: $OUT/cr_fx_scale_reverb.ppm" \
    || bad "the Sends page and Reverb 1's Send cell: $(cel 3 Reverb) / $(cel 4 Send), then $(cel 5 Send) / $(cel 6 Reverb) (amt $(fxamt 3), $(fxamt 5))"
[ "$(cel 7 Reverb)" = "42%" ] && [ "$(fxamt 8)" = "59 45 25 0" ] && [ "$(cel 8 Reverb)" = "46%" ] && [ "$(pop 9)" = "51% reverb" ] &&
    [ "$(fxamt 9)" = "65 45 25 0" ] && [ "$(pop 10)" = "46% reverb" ] && [ "$(fxamt 10)" = "59 45 25 0" ] &&
    ok "the editor's FX group reads 42%; its KNOB 4, the same detent: 59, 46%; KNOB 4 of the main view: 65, the popup 51%; CC 91 = 59: the popup 46%" \
    || bad "the editor, KNOB 4 and CC 91: $(cel 7 Reverb) / $(cel 8 Reverb) / '$(pop 9)' / '$(pop 10)' (amt $(fxamt 8), $(fxamt 9), $(fxamt 10))"
# both ends from each place (dumps 11 .. 22): the popup, the Send cell, the Sends page, the editor
[ "$(pop 11)" = "100% reverb" ] && [ "$(fxamt 11)" = "127 45 25 0" ] && [ "$(pop 12)" = "0% reverb" ] && [ "$(fxamt 12)" = "0 45 25 0" ] &&
    [ "$(cel 13 Send)" = "0%" ] && [ "$(cel 14 Send)" = "100%" ] && [ "$(fxamt 14)" = "127 45 25 0" ] && [ "$(cel 15 Send)" = "0%" ] && [ "$(fxamt 15)" = "0 45 25 0" ] &&
    [ "$(cel 16 Reverb)" = "0%" ] && [ "$(cel 17 Reverb)" = "100%" ] && [ "$(fxamt 17)" = "127 45 25 0" ] && [ "$(cel 18 Reverb)" = "0%" ] &&
    [ "$(cel 19 Reverb)" = "0%" ] && [ "$(cel 20 Reverb)" = "100%" ] && [ "$(fxamt 20)" = "127 45 25 0" ] && [ "$(cel 21 Reverb)" = "0%" ] && [ "$(fxamt 21)" = "0 45 25 0" ] &&
    ok "0% and 100% are reached from each place and are the stored 0 and 127: KNOB 4's popup, Reverb 1's Send cell, the Sends page, the editor: $OUT/cr_fx_scale_full.ppm" \
    || bad "the ends: popup '$(pop 11)' '$(pop 12)', Send $(cel 13 Send) $(cel 14 Send) $(cel 15 Send), Sends $(cel 16 Reverb) $(cel 17 Reverb) $(cel 18 Reverb), editor $(cel 19 Reverb) $(cel 20 Reverb) $(cel 21 Reverb)"
# the other effects' Send cells (dumps 22 .. 28): a detent each, then the Sends page and the popup of the effect picked
[ "$(cel 22 Send)" = "35%" ] && [ "$(cel 23 Send)" = "40%" ] && [ "$(cel 24 Send)" = "20%" ] && [ "$(cel 25 Send)" = "24%" ] &&
    [ "$(cel 26 Send)" = "0%" ] && [ "$(cel 27 Send)" = "5%" ] && [ "$(fxamt 27)" = "0 51 31 6" ] &&
    [ "$(cel 28 Drive)" = "5%" ] && [ "$(cel 28 Chorus)" = "40%" ] && [ "$(cel 28 Delay)" = "24%" ] && [ "$(pop 29)" = "9% drive" ] && [ "$(fxamt 29)" = "0 51 31 12" ] &&
    ok "the Send cells of Chorus, Delay 1 and Drive: 35% -> 40%, 20% -> 24%, 0% -> 5% (6 a detent: chorus delay drive 51 31 6); the Sends page reads the three; KNOB 4's popup: drive 9%" \
    || bad "the other Send cells: chorus $(cel 22 Send) $(cel 23 Send), delay $(cel 24 Send) $(cel 25 Send), drive $(cel 26 Send) $(cel 27 Send); Sends $(cel 28 Drive) $(cel 28 Chorus) $(cel 28 Delay); popup '$(pop 29)' (amt $(fxamt 27), $(fxamt 29))"
[ "$(grep -a '^  screen reads:' "$L" | grep -c ':Send=[0-9][0-9]*%')" = 11 ] && ! grep -a '^  screen reads:' "$L" | grep -q ':Send=[0-9][0-9]*\( \|$\)' &&
    ok "no Send cell reads a bare number in any dump (the 0..99 read-out is gone)" || bad "a Send cell without its per cent"
# the bass part's: its editor's FX group and CC 91 on its channel (dumps 30 .. 34)
[ "$(cel 30 Reverb)" = "8%" ] && [ "$(bsr 30)" = 10 ] && [ "$(cel 31 Reverb)" = "13%" ] && [ "$(bsr 31)" = 16 ] && [ "$(cel 32 Reverb)" = "100%" ] && [ "$(bsr 32)" = 127 ] &&
    [ "$(cel 33 Reverb)" = "0%" ] && [ "$(bsr 33)" = 0 ] && [ "$(pop 34)" = "50% reverb" ] && [ "$(bsr 34)" = 64 ] && [ "$(fxamt 34)" = "0 51 31 12" ] &&
    ok "the bass part's sends read and step the same in its editor (reverb 10: 8%; a detent: 16, 13%; both ends); CC 91 on its channel: the popup 50%, the chord part's untouched" \
    || bad "the bass part's sends: $(cel 30 Reverb) $(bsr 30), $(cel 31 Reverb) $(bsr 31), $(cel 32 Reverb), $(cel 33 Reverb), popup '$(pop 34)' ($(bsr 34))"

echo "FM TONE's Drive is its preset's and sounds with FX off; another engine's is the part's send (docs/FX.md \"FM TONE's Drive\")"
echo "the master filter (0.22, GitHub issue 16; docs/FX.md \"The master filter\"): the FX layer's Filter page, the sound through it"
run cr_fx_filter --wav "$OUT/cr_fx_filter.wav"
L="$OUT/cr_fx_filter.log"
has '^fx: filter' "$L" && has '^fx: cells Low cut High cut Slope Filter' "$L" &&
    ok "FX held, SELECT +7: the Filter page, Low cut / High cut / Slope / Filter on KNOB 1..4: $OUT/cr_fx_filter.ppm" || bad "no Filter page"
has '^fx: knob 2 Filter high cut 0' "$L" && differ cr_fx_filter cr_fx_filter_high "KNOB 2 to the end: the high cut in at 200 Hz (its cell hot), the Filter cell On by itself: $OUT/cr_fx_filter_high.ppm"
has '^fx: knob 4 Filter filter 0' "$L" && differ cr_fx_filter_high cr_fx_filter_off "KNOB 4 -1: Filter Off, the cutoffs kept: $OUT/cr_fx_filter_off.ppm"
has '^fx: knob 1 Filter low cut 72' "$L" && differ cr_fx_filter_off cr_fx_filter_low "KNOB 1 +12: the low cut in at 277 Hz, Filter On again: $OUT/cr_fx_filter_low.ppm"
has '^fx: knob 3 Filter slope 0' "$L" && differ cr_fx_filter_low cr_fx_filter_slope "KNOB 3 -1: Slope 6 dB: $OUT/cr_fx_filter_slope.ppm"
differ cr_fx_filter_slope cr_fx_filter_fxoff "FX tapped off: the Filter cell reads Off (the filter is out with the effects): $OUT/cr_fx_filter_fxoff.ppm"
grep -a '^  session fx:' "$L" | tail -1 | grep -q ' 31=72 32=0 33=0 34=1' &&
    ok "the session's FX block holds the four (tags 31 .. 34: low cut 72, high cut 0, slope 6 dB, on), written only off their defaults" \
    || bad "the filter's tags: $(grep -a '^  session fx:' "$L" | tail -1 | sed 's/.* par //')"
# the WAV: the D major chord (TINE EP: its three notes, little above them) held with the filter Off (0.4 s from 0.2 s
# into the hold), then through the 200 Hz high cut at 12 dB: D4, F#4 and A4 (Goertzel over 0.4 s) as a part of
# themselves without the filter: the formula's 53, 41 and 33 % (two poles at 311 Hz), falling with the pitch
wv=$(python3 - "$OUT/cr_fx_filter.wav" <<'PY'
import sys, wave, struct, math
w = wave.open(sys.argv[1]); sr = w.getframerate(); ch = w.getnchannels()
raw = w.readframes(w.getnframes()); x = struct.unpack('<%dh' % (len(raw) // 2), raw)
def band(t0, t1, f):
    a, b = int(t0 * sr), int(t1 * sr); k = 2 * math.cos(2 * math.pi * f / sr); s1 = s2 = 0.0
    for i in range(a, b):
        s = (x[i * ch] + x[i * ch + ch - 1]) / 2 + k * s1 - s2; s2 = s1; s1 = s
    return math.sqrt(max(s1 * s1 + s2 * s2 - k * s1 * s2, 0)) / (b - a)
print(*('%d' % (100 * band(4.58, 4.98, f) / max(band(1.74, 2.14, f), 1e-9)) for f in (293.66, 369.99, 440.0)), '%.0f' % band(1.74, 2.14, 369.99))
PY
)
set -- $wv
[ "${1:-100}" -ge 45 ] && [ "${1:-100}" -le 70 ] && [ "${2:-100}" -ge 30 ] && [ "${2:-100}" -le 55 ] && [ "${3:-100}" -ge 20 ] && [ "${3:-100}" -le 42 ] &&
    [ "${3:-100}" -lt "${2:-100}" ] && [ "${2:-100}" -lt "${1:-100}" ] && [ "${4:-0}" -ge 200 ] &&
    ok "the WAV: through the 200 Hz high cut the chord's D4 is ${1}% of itself without the filter, F#4 ${2}%, A4 ${3}% (the formula: 53, 41, 33)" \
    || bad "the WAV: D4 ${1:-?}%, F#4 ${2:-?}%, A4 ${3:-?}% of the chord without the filter (F#4 there: ${4:-?})"
silent_end cr_fx_filter

run cr_quad_drive
L="$OUT/cr_quad_drive.log"
drv() { grep -a '^  drive:' "$L" | sed -n "$1p" | sed 's/^  drive: //'; }
drc() { drv "$1" | sed 's/ | .*//'; }          # the chord part's half
drb() { drv "$1" | sed 's/.* | //'; }          # the bass part's
fxl() { grep -a '^  session fx:' "$L" | sed -n "$1p"; }
fxsnd() { fxl "$1" | sed -n 's/.* sends \([0-9 ]*\) par.*/\1/p'; }
rd() { grep -a '^  screen reads:' "$L" | sed -n "$1p" | sed 's/.* cells //'; }
[ "$(drc 1)" = "chord track 12 send 12 edited 0" ] && [ "$(drc 2)" = "chord FM TONE track 0 send 12 edited 0" ] &&
    [ "$(drc 3)" = "chord FM TONE track 10 send 12 edited 0" ] && [ "$(drc 4)" = "chord FM TONE track 0 send 12 edited 0" ] &&
    has '^sound: part 0 pos 10 WOBBLE' "$L" && has '^sound: part 0 pos 9 METAL' "$L" &&
    ok "two FM TONE presets with different Drive: FM6 with the part's send 12 -> 01 EP: 0 -> 10 WOBBLE: 10 -> 09 METAL: 0 (the part's send, 12, waits)" \
    || bad "FM TONE's presets' Drive: '$(drc 1)' / '$(drc 2)' / '$(drc 3)' / '$(drc 4)'"
has '^param: part 0 ENV DRIVE 10 -> 28' "$L" && [ "$(drc 5)" = "chord FM TONE track 28 send 12 edited 1" ] && rd 5 | grep -q '1.2:Drive=28$' &&
    has '^save: part 0 overwrite slot U001 FM TONE 10 WOBBLE rc 0' "$L" && [ "$(drc 6)" = "chord FM TONE track 28 send 12 edited 0" ] &&
    [ "$(drc 7)" = "chord FM TONE track 0 send 12 edited 0" ] && [ "$(drc 8)" = "chord FM TONE track 28 send 12 edited 0" ] &&
    ok "the AMP page's Drive (0..127: 10 -> 28) marks the sound edited; saved (U01, over WOBBLE), METAL: 0, back: 28 again: $OUT/cr_quad_drive_amp.ppm" \
    || bad "edit, save, away and back: '$(drc 5)' / '$(drc 6)' / '$(drc 7)' / '$(drc 8)' ($(rd 5))"
[ "$(fxsnd 9)" = "0 0 0 28" ] && [ "$(rd 9)" = "0.0:Drive=22% 0.1:Chorus=off 0.2:Delay=off 0.3:Reverb=off" ] &&
    has '^fx: knob 1 Drive send 34' "$L" && [ "$(drc 10)" = "chord FM TONE track 34 send 12 edited 1" ] && [ "$(fxsnd 10)" = "0 0 0 34" ] &&
    fxl 10 | grep -q 'fx: on 0 ' && [ "$(rd 10)" = "0.0:Drive=27% 0.1:Chorus=off 0.2:Delay=off 0.3:Reverb=off" ] &&
    ok "FX off: FM TONE's Drive stays in the track (28, the three sends 0); the Sends page: Drive 22%, the others 'off'; its KNOB 1: 34, the sound edited, FX still off: $OUT/cr_quad_drive_sends_off.ppm" \
    || bad "FX off on FM TONE: sends '$(fxsnd 9)' cells '$(rd 9)'; turned: '$(drc 10)' sends '$(fxsnd 10)' cells '$(rd 10)'"
[ "$(drc 11)" = "chord track 0 send 12 edited 0" ] && [ "$(fxsnd 11)" = "0 0 0 0" ] && [ "$(drc 12)" = "chord track 12 send 12 edited 0" ] &&
    [ "$(drc 13)" = "chord FM TONE track 28 send 12 edited 0" ] &&
    ok "the engine switched away (the VA), FX off: its drive is the part's send, gated (0); FX on: 12, as before FM TONE; FM TONE again (WOBBLE as saved): 28" \
    || bad "away and back: '$(drc 11)' ($(fxsnd 11)) / '$(drc 12)' / '$(drc 13)'"
has '^param: part 1 FX DST 0 -> 18' "$L" && [ "$(drb 14)" = "bass track 18 send 18 edited 0" ] && [ "$(drb 15)" = "bass FM TONE track 0 send 18 edited 0" ] &&
    [ "$(drb 16)" = "bass FM TONE track 28 send 18 edited 0" ] && [ "$(drb 17)" = "bass FM TONE track 28 send 18 edited 0" ] && fxl 17 | grep -q 'fx: on 0 ' &&
    has '^param: part 1 ENV DRIVE 28 -> 34' "$L" && [ "$(drb 18)" = "bass FM TONE track 34 send 18 edited 1" ] && [ "$(drb 19)" = "bass track 18 send 18 edited 0" ] &&
    ok "the bass part: its send 18 on the VA; FM TONE 01 EP: 0; the saved WOBBLE: 28, with FX off too; its AMP Drive: 34, edited; the VA again: 18: $OUT/cr_quad_drive_bass_amp.ppm" \
    || bad "the bass part: '$(drb 14)' / '$(drb 15)' / '$(drb 16)' / '$(drb 17)' / '$(drb 18)' / '$(drb 19)'"
has '^picker: open part 0 WOBBLE' "$L" && [ "$(drc 20)" = "chord track 0 send 12 edited 1" ] && has '^picker: cancel part 0 -> WOBBLE' "$L" &&
    [ "$(drc 21)" = "chord FM TONE track 28 send 12 edited 0" ] &&
    ok "the picker's preview from FM TONE (FX off): the VA with the part's gated send (0); OCT-: WOBBLE with its Drive 28 again" \
    || bad "the picker: '$(drc 20)' / '$(drc 21)'"
for n in tone_hi tone_lo fm6_hi fm6_lo; do run cr_quad_drive_wav_$n --wav "$OUT/cr_quad_drive_wav_$n.wav"; done
wr=$(python3 - "$OUT/cr_quad_drive_wav_tone_hi.wav" "$OUT/cr_quad_drive_wav_tone_lo.wav" <<'PY'
import sys, wave, struct, math
r = []
for f in sys.argv[1:]:
    w = wave.open(f); n = w.getnframes()
    d = struct.unpack('<%dh' % (n * w.getnchannels()), w.readframes(n))
    r.append(math.sqrt(sum(x * x for x in d) / max(1, len(d))))
print("%d %d %d" % (r[0], r[1], 100 * r[0] / max(1.0, r[1])))
PY
)
grep -aq '^  drive: chord FM TONE track 127 ' "$OUT/cr_quad_drive_wav_tone_hi.log" && grep -aq '^  drive: chord FM TONE track 0 ' "$OUT/cr_quad_drive_wav_tone_lo.log" &&
    [ "${wr##* }" -gt 120 ] && ok "heard, FX off: FM TONE 01 EP with Drive 127 against Drive 0, MAJ + D4: RMS ${wr%% *} against $(echo "$wr" | cut -d' ' -f2) (${wr##* } %): $OUT/cr_quad_drive_wav_tone_hi.wav" \
    || bad "FM TONE's Drive with FX off is not heard: RMS $wr"
grep -aq '^  drive: chord track 0 send 127 ' "$OUT/cr_quad_drive_wav_fm6_hi.log" && grep -aq '^  drive: chord track 0 send 0 ' "$OUT/cr_quad_drive_wav_fm6_lo.log" &&
    cmp -s "$OUT/cr_quad_drive_wav_fm6_hi.wav" "$OUT/cr_quad_drive_wav_fm6_lo.wav" &&
    ok "heard, FX off: FM6 with the part's drive send at 127 and at 0: the same samples (another engine's drive is gated as before)" \
    || bad "FM6's drive with FX off: the renders differ"

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

echo "perform on / off: PERF tap, in the perform layer too (OCT+ closes it); the arp stops when off"
run cr_perf_toggle
L="$OUT/cr_perf_toggle.log"
pc() { grep '^  cr: key' "$L" | sed -n "$1p" | sed -n 's/.* perform \([0-9]\) .* layer \([0-9]*\) .*/\1 \2/p'; }
no() { grep '^  cr: voices' "$L" | sed -n "$1p" | sed -n 's/.* notes-out \([0-9]*\) .*/\1/p'; }
grep -q '^expect led ARP on at [0-9]* ms: ok' "$L" && grep -q '^expect led ARP dim at [0-9]* ms: ok' "$L" && [ "$(pc 1)" = "0 0" ] &&
    ok "PERF tap: perform on (LED lit, no message), tap again: off (LED dim): $OUT/cr_perf_on_msg.ppm" || bad "PERF tap: $(pc 1)"
[ "$(pc 2)" = "1 2" ] && [ "$(pc 3)" = "0 2" ] && [ "$(pc 4)" = "1 2" ] &&
    ok "the perform layer: SELECT picks Arpeggiate (on), a PERF tap off / on, the layer stays open: $OUT/cr_perf_layer_off.ppm" \
    || bad "the perform layer: $(pc 2) / $(pc 3) / $(pc 4)"
[ "$(pc 5)" = "1 0" ] && [ "$(pc 6)" = "1 2" ] && [ "$(pc 7)" = "1 0" ] &&
    ok "OCT+ in the perform layer closes it (perform stays on); PERF held again closes it too (no tap: still on)" \
    || bad "closing the perform layer: $(pc 5) / $(pc 6) / $(pc 7)"
differ cr_perf_layer_off cr_perf_layer_on "the layer's label follows on / off"
[ "$(no 9)" -gt "$(no 8)" ] && [ "$(no 11)" = "$(no 10)" ] && [ "$(pc 10)" = "0 0" ] &&
    ok "the arp steps while on ($(no 8) -> $(no 9) notes out), PERF tap off: the chord held plain ($(no 10) -> $(no 11))" \
    || bad "the arp on / off: notes out $(no 8) $(no 9) $(no 10) $(no 11)"

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
grep '^ui: frame.*device): arp ' "$L" | grep -qv 'device): arp view 0 ' && bad "an arp screen on another view than Chord" ||
    ok "the arp screen only on the Chord view ($(grep -c '^ui: frame.*device): arp view 0 ' "$L") frames), none on the Keyboard view"

echo "Strum's Hold lights LATCH; a LATCH tap turns Hold off (the notes go); the next tap latches as before"
export EMU_UI_LOG=0
run cr_strum_hold
unset EMU_UI_LOG
L="$OUT/cr_strum_hold.log"
has '^latch: Strum hold off' "$L" && grep -q '^expect led LFO dim .*: ok' "$L" &&
    ok "LATCH tapped with Strum's Hold on: Hold off (the trace), the LED back to its idle glow (the engine no longer latching)" ||
    bad "LATCH with Hold on did not turn Hold off (or the LED stayed lit)"
[ "$(grep -c '^expect led LFO on .*: ok' "$L")" = 2 ] && ok "the next LATCH tap: the sticky latch as before (the LED lit with Hold off)" ||
    bad "the second LATCH tap did not latch"

echo "the arp screen: the Chord view's content while an arpeggio plays (the hop), and only then"
export EMU_UI_LOG=0
run cr_arp_view
unset EMU_UI_LOG
L="$OUT/cr_arp_view.log"
seg() { awk -v n="$1" '/^fm1_in:/ { d++ } d == n - 1 && /^ui: frame/' "$L"; }   # the frames up to dump N (after dump N - 1)
knd() { seg "$1" | tail -1 | sed -n 's/.*device): \([a-z0-9]*\) view \([0-9]\) name .\([^|]*\)|.*/\1 \2 \3/p'; }   # the last: "arp 0 D"
arps() { seg "$1" | grep -c 'device): arp '; }
hops() { seg "$1" | sed -n 's/.*device): arp .* pos \([0-9][0-9]*\) of .*/\1/p' | sort -u | wc -l | tr -d ' '; }   # the hop's places seen
md() { grep '^  cr: key' "$L" | sed -n "$1p" | sed -n 's/.* perform \([0-9]\) mode \([0-9]\) .* view \([0-9]\) layer \([0-9]*\) .*/\1 \2 \3 \4/p'; }
[ "$(md 1)" = "1 2 0 0" ] && [ "$(knd 2)" = "arp 0 D" ] && [ "$(hops 2)" -ge 3 ] && seg 2 | tail -1 | grep -q "right 'Arp'.* line 'arp 1/8'" &&
    ok "Chord view + Arpeggiate + D held: the arp screen, the note hopping ($(hops 2) places), the top line Arp, 'arp 1/8': $OUT/cr_arp_view_a.ppm" \
    || bad "no arp screen: $(md 1) / $(knd 2) / $(hops 2) places"
differ cr_arp_view_a cr_arp_view_b "a step later the hop is elsewhere"
seg 2 | grep 'device): arp ' | sed 1d | grep -qv 'blits [0-9]*/[0-3] ' && bad "a hop composed more than its 3 strips" ||
    ok "a hop composes its strips only (3 of 6: $(seg 2 | grep -c 'blits [0-9]*/3 ') frames; the others nothing)"
[ "$(knd 3)" = "knobrow 0 " ] && [ "$(md 3)" = "1 2 0 2" ] && [ "$(no 3)" -gt "$(no 2)" ] && [ "$(knd 4)" = "arp 0 D" ] &&
    ok "PERF held with the arp running: the perform layer (the arp goes on: $(no 2) -> $(no 3) notes), closed: the arp screen: $OUT/cr_arp_view_layer.ppm" \
    || bad "the layer over the arp screen: $(knd 3) / $(md 3) / $(knd 4)"
[ "$(knd 5)" = "chord 0 D" ] && [ "$(arps 5)" -le 12 ] &&
    ok "keys released: the chord view back ($(arps 5) frames of the arp screen left: its 150 ms linger): $OUT/cr_arp_view_rel.ppm" \
    || bad "released: $(knd 5), $(arps 5) arp frames"
differ cr_arp_view_a cr_arp_view_rel "the chord view is not the arp screen"
[ "$(knd 6)" = "arp 0 D" ] && [ "$(hops 6)" -ge 3 ] && [ "$(no 6)" -gt "$(no 5)" ] &&
    ok "LATCH + the chord released: the arp screen stays, hopping ($(hops 6) places): $OUT/cr_arp_view_latch_a.ppm" || bad "LATCH: $(knd 6) / $(hops 6)"
differ cr_arp_view_latch_a cr_arp_view_latch_b "latched: a step later the hop is elsewhere"
[ "$(knd 7)" = "chord 0 " ] && ok "PANIC: the chord view" || bad "PANIC: $(knd 7)"
[ "$(knd 8)" = "keyboard 1 D" ] && [ "$(arps 8)" = 0 ] && [ "$(no 8)" -gt "$(no 7)" ] &&
    ok "the Keyboard view with the arp running ($(no 7) -> $(no 8) notes): unchanged, no arp screen: $OUT/cr_arp_view_kbd.ppm" \
    || bad "the Keyboard view: $(knd 8), $(arps 8) arp frames"
vc() { grep '^  cr: voices' "$L" | sed -n "$1p" | sed -n 's/.*cr: voices \([0-9]*\) .*/\1/p'; }
[ "$(vc 9)" = 0 ] && ok "PANIC took LATCH off in the engine too: the chord released after it ends" || bad "after PANIC a released chord still sounds ($(vc 9) voices)"
[ "$(md 9)" = "1 2 0 0" ] && [ "$(knd 10)" = "arp 0 D" ] && [ "$(hops 10)" -ge 3 ] && [ "$(no 10)" -gt "$(no 9)" ] &&
    ok "Options > Motion Off: the arp screen all the same, hopping ($(hops 10) places, $(no 9) -> $(no 10) notes): $OUT/cr_arp_view_motion_off.ppm" \
    || bad "Motion Off: $(md 9) / $(knd 10), $(hops 10) places"
[ "$(knd 11)" = "arp 0 D" ] && [ "$(hops 11)" -ge 3 ] && ok "Motion Calm: the arp screen" || bad "Motion Calm: $(knd 11)"
[ "$(md 12)" = "1 0 0 0" ] && [ "$(knd 13)" = "chord 0 D" ] && [ "$(arps 13)" = 0 ] && [ "$(no 13)" -gt "$(no 12)" ] &&
    ok "Strum: the chord view, no arp screen in any frame: $OUT/cr_arp_view_strum.ppm" || bad "Strum: $(md 12) / $(knd 13), $(arps 13) arp frames"
[ "$(md 14)" = "1 4 0 0" ] && [ "$(knd 15)" = "chord 0 D" ] && [ "$(arps 15)" = 0 ] && [ "$(no 15)" -gt "$(no 14)" ] &&
    ok "Harp (one run): the chord view, no arp screen in any frame" || bad "Harp: $(md 14) / $(knd 15), $(arps 15) arp frames"
[ "$(md 16)" = "1 3 0 0" ] && [ "$(knd 16)" = "arp 0 D" ] && seg 16 | tail -1 | grep -q " line 'pattern " &&
    ok "Pattern: the arp screen" || bad "Pattern: $(md 16) / $(knd 16)"
lp() { grep '^  loop: state' "$L" | sed -n "$1p" | sed -n 's/.*loop: state \([0-9]*\) .*/\1/p'; }
kd() { grep '^fm1_in:' "$L" | sed -n "$1p" | sed -n 's/^fm1_in: notes \([0-9A-F]*\) .*/\1/p'; }
case "$(knd 17)" in "arp 0 "?) a17=1;; *) a17=0;; esac
[ "$a17" = 1 ] && [ "$(kd 17)" = 0000000 ] && [ "$(lp 17)" != "$(lp 18)" ] && [ "$(no 17)" -gt "$(no 16)" ] && [ "$(hops 17)" -ge 3 ] &&
    ok "a playing loop, no key held: the arp screen follows the loop's chords ($(knd 17)): $OUT/cr_arp_view_loop.ppm" \
    || bad "the loop's arp: $(knd 17), keys $(kd 17), loop state $(lp 17) -> $(lp 18)"
case "$(knd 18)" in "chord 0 "?) ok "the loop stopped: the chord view";; *) bad "the loop stopped: $(knd 18)";; esac
silent_end cr_arp_view

echo "the arp screen from the owner's own settings record (2026-10-10: Key Mode, Arpeggiate, Motion Off)"
AF="$OUT/cr_arp_unit_flash.bin"
rm -f "$AF"
"$EMU" --headless --flash "$AF" --script "$S/persist_set.txt" >"$OUT/cr_arp_unit_seed.log" 2>&1   # a settings object to seed
python3 tools/emu/seed_settings.py "$AF" 435253310c00c00026aa43ec0000010104010001056400000100020300017800320006000000020064003200010001000000000000002800060000000100640032000100010000000f00000028000800010001000a0032000100010000000000000028000600000001004600320001000100000000000000080006000000030064003200010001000000000000000000016401010000010201000002ff0000010002000d00004606000001010205010001010101010101010001030101010101 >>"$OUT/cr_arp_unit_seed.log" 2>&1 ||
    bad "the record was not seeded (see $OUT/cr_arp_unit_seed.log)"
export EMU_UI_LOG=0
run cr_arp_unit --flash "$AF"
unset EMU_UI_LOG
L="$OUT/cr_arp_unit.log"
ks() { grep '^  cr: key' "$L" | sed -n "$1p" | sed -n 's/.*cr: key \([0-9]\) tonic \([0-9]*\) scale \([0-9]\) perform \([0-9]\) mode \([0-9]\) .* view \([0-9]\) layer \([0-9]*\) .*/\1 \2 \3 \4 \5 \6 \7/p'; }
has '(record: migrated)' "$L" && [ "$(ks 1)" = "1 4 1 0 2 0 0" ] && grep -q 'style 0 single 1 split 5' "$L" &&
    ok "the record (settings v12) read as the unit stored it (Key Mode E minor, Split at F, Arpeggiate picked at 1/16, perform off, the Chord view)" \
    || bad "the seeded record: $(ks 1)"
[ "$(ks 2)" = "1 4 1 1 2 0 0" ] && [ "$(knd 3)" = "arp 0 D" ] && [ "$(hops 3)" -ge 3 ] && seg 3 | tail -1 | grep -q "right 'Arp'.* line 'arp 1/16'" &&
    ok "PERF tap, one root alone (the scale's chord), Motion Off: the arp screen, the note hopping ($(hops 3) places): $OUT/cr_arp_unit.ppm" \
    || bad "no arp screen from the unit's record: $(ks 2) / $(knd 3) / $(hops 3) places"
[ "$(knd 4)" = "chord 0 D" ] && ok "released: the chord view" || bad "released: $(knd 4)"
case "$(knd 5)" in "arp 0 "*) [ "$(hops 5)" -ge 3 ] && ok "LATCH, a root tapped: the arp screen stays, hopping ($(hops 5) places)" || bad "LATCH: $(hops 5) places";;
    *) bad "LATCH: $(knd 5)";; esac
case "$(knd 6)" in "chord 0 "*) ok "PERF tap (off): the chord view";; *) bad "perform off: $(knd 6)";; esac
silent_end cr_arp_unit

echo "the perform layer's band: the picker for 2 s after it opens and after SELECT, then the arp while notes are stepped"
export EMU_UI_LOG=0
run cr_perf_arp
unset EMU_UI_LOG
L="$OUT/cr_perf_arp.log"
bnd() { seg "$1" | tail -1 | sed -n "s/.*device): knobrow .* item '\\([^']*\\)' .* hot [0-9]\\.[0-9]\\( band arp\\)\\{0,1\\}.*/\\1\\2/p"; }   # "Arpeggiate" (the picker) / "Arpeggiate band arp"
bas() { seg "$1" | grep -c ' band arp '; }                         # frames of the arp band up to dump N
pks() { seg "$1" | grep 'device): knobrow ' | grep -vc ' band arp '; }   # .. of the picker
bhp() { seg "$1" | sed -n 's/.* band arp pos \([0-9][0-9]*\) of .*/\1/p' | sort -u | wc -l | tr -d ' '; }
[ "$(bnd 1)" = "Arpeggiate" ] && [ "$(bnd 2)" = "Arpeggiate" ] && [ "$(bas 2)" = 0 ] &&
    ok "PERF held, SELECT +3: the picker (Arpeggiate); nothing playing 2.5 s later: still the picker" || bad "the picker: $(bnd 1) / $(bnd 2), $(bas 2) arp frames"
[ "$(bnd 3)" = "Arpeggiate band arp" ] && [ "$(bhp 3)" -ge 3 ] && seg 3 | tail -1 | grep -q "name 'D|M|'.* label 'Arpeggiate . on'" &&
    ok "MAJ + D4 held: the band is the arp (DM, the note hopping: $(bhp 3) places), the mode's name in the label: $OUT/cr_perf_arp_a.ppm" \
    || bad "no arp band: $(bnd 3), $(bhp 3) places"
seg 3 | grep ' band arp ' | sed 1d | grep -qv 'blits [0-9]*/[0-2] ' && bad "a hop in the layer composed more than the band's 2 strips" ||
    ok "a hop composes the band's strips only (2 of 6: $(seg 3 | grep ' band arp ' | grep -c 'blits [0-9]*/2 ') frames; the others nothing): $(seg 3 | grep ' band arp ' | grep 'blits [0-9]*/2 ' | sed -n 's/.*(\([0-9]*\) us device).*/\1/p' | awk '{ s += $1; if ($1 > m) m = $1 } END { printf "%d us avg, %d max (device estimate)", NR ? s / NR : 0, m }')"
tail -c 82080 "$OUT/cr_perf_arp_a.ppm" >"$OUT/cr_perf_arp_a.row"; tail -c 82080 "$OUT/cr_perf_arp_b.ppm" >"$OUT/cr_perf_arp_b.row"   # rows 126..239
cmp -s "$OUT/cr_perf_arp_a.row" "$OUT/cr_perf_arp_b.row" && ! cmp -s "$OUT/cr_perf_arp_a.ppm" "$OUT/cr_perf_arp_b.ppm" &&
    ok "a hop later: the band differs, the knob row's and the footer's pixels are the same" || bad "the knob row changed across a hop (or no hop between the shots)"
[ "$(pks 4)" = 0 ] && seg 4 | grep -q 'hot 1\.0 band arp ' && [ "$(bnd 4)" = "Arpeggiate band arp" ] &&
    ok "KNOB 1 in the row: its cell hot, the arp band stays (no picker frame): $OUT/cr_perf_arp_hot.ppm" || bad "a knob of the row: $(pks 4) picker frames, $(bnd 4)"
[ "$(bnd 5)" = "Arp 2 Octaves" ] && [ "$(bas 5)" -le 2 ] && [ "$(bnd 6)" = "Arp 2 Octaves band arp" ] && seg 6 | tail -1 | grep -q "label 'Arp 2 Octaves . on'" &&
    ok "SELECT +1 under the playing arp: the picker at once with the new name (Arp 2 Octaves), still there 1.65 s later; past 2 s the arp band again: $OUT/cr_perf_arp_pick.ppm" \
    || bad "SELECT: $(bnd 5) ($(bas 5) arp frames) / $(bnd 6)"
[ "$(bnd 7)" = "Arp 2 Octaves" ] && ok "keys up: the picker" || bad "keys up: $(bnd 7)"
[ "$(bnd 8)" = "Arp 2 Octaves band arp" ] && [ "$(bhp 8)" -ge 3 ] && seg 8 | tail -1 | grep -q "name 'E|M|'" &&
    ok "LATCH + a chord tapped: the arp band, hands off ($(bhp 8) places)" || bad "latched: $(bnd 8)"
[ "$(bnd 9)" = "Arp 2 Octaves" ] && [ "$(seg 9 | tail -60 | grep -c ' band arp ')" = 0 ] && [ "$(bnd 10)" = "Arp 2 Octaves band arp" ] &&
    ok "the layer opened again over the latched arp: the picker first (its first second: no arp frame), the arp band after 2 s" || bad "reopened: $(bnd 9) / $(bnd 10)"
[ "$(bnd 11)" = "Arp 2 Octaves band arp" ] && ok "Options > Motion Off: the arp band too (the Chord view's rule)" || bad "Motion Off: $(bnd 11)"
[ "$(bnd 12)" = "Strum" ] && ok "Strum (runs once): the picker stays" || bad "Strum: $(bnd 12)"
silent_end cr_perf_arp

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

echo "the key layer: MAJ / MIN pick the scale with LOCK off or on, their LEDs show it, the latched chord is kept"
run cr_key_scale
L="$OUT/cr_key_scale.log"
kk() { grep -a '^  cr: key' "$L" | sed -n "$1p" | sed -n 's/.*cr: key \([0-9]\) tonic \([0-9]*\) scale \([0-9]\) .* layer \([0-9]*\) .*/\1 \2 \3 \4/p'; }
[ "$(grep -c '^expect .*: ok' "$L")" = 32 ] && ! has 'expect.*FAILED' "$L" &&
    ok "the layer's chord-key LEDs: MAJ / MIN at half, the selected scale's lit, the six others dark, B3 LOCK's own; out of the layer the keys' own state; Stock: the selected one lit alone: $OUT/cr_key_scale_minor.ppm" \
    || bad "the key layer's LEDs: $(grep -a 'expect.*FAILED' "$L" | head -3)"
[ "$(kk 1)" = "0 0 1 1" ] && [ "$(kk 2)" = "1 4 1 1" ] && [ "$(kk 3)" = "1 4 0 1" ] && [ "$(kk 4)" = "1 5 1 1" ] && [ "$(kk 5)" = "1 5 1 1" ] &&
    ok "LOCK off: MIN tapped: minor; a root alone: the tonic, the scale kept; MAJ: major; MIN held + a root: minor; DIM and 9 do nothing" \
    || bad "the scale keys: $(kk 1) / $(kk 2) / $(kk 3) / $(kk 4) / $(kk 5)"
[ "$(kk 7)" = "1 5 0 1" ] && [ "$(kk 8)" = "1 5 1 1" ] && [ "$(kk 9)" = "1 5 0 0" ] &&
    ok "LOCK on with MIN latched: MAJ tapped: major, MIN tapped: minor (the owner's case), MAJ held across the layer's end: major" \
    || bad "the scale keys with LOCK: $(kk 7) / $(kk 8) / $(kk 9)"
[ "$(grep -a '^lock:' "$L" | sed -n 1,4p | tr '\n' ',')" = "lock: on,lock: latched MIN,lock: latched none,lock: off," ] && has 'chord Dm sounding 1' "$L" &&
    ok "the latch behind the layer untouched (MIN alone, before and after): a root alone still plays Dm" \
    || bad "the latch changed in the key layer: $(grep -a '^lock:' "$L" | tr '\n' ' ')"
silent_end cr_key_scale

echo "the key layer's keys: pick / play, toggled with OPT (GLO) in the layer"
run cr_key_play
L="$OUT/cr_key_play.log"
kp() { grep -a '^  cr: key' "$L" | sed -n "$1p" | sed -n 's/.*cr: key \([0-9]\) tonic \([0-9]*\) .* layer \([0-9]*\) options \([0-9]\) kkeys \([0-9]\)$/\1 \2 \3 \4 \5/p'; }
sp() { grep -a '^  loop:' "$L" | sed -n "$1p" | sed -n 's/.* split \([0-9]*\)$/\1/p'; }
[ "$(grep -c '^expect .*: ok' "$L")" = 18 ] && ! has 'expect.*FAILED' "$L" &&
    ok "pick: a root is silent; play: GLO lit, the chord keys' and the roots' LEDs their own, MAJ + E4 sounds; pick again: the scale keys' LEDs" \
    || bad "the key layer's keys: $(grep -a 'expect.*FAILED' "$L" | head -3)"
[ "$(kp 1)" = "0 0 1 0 0" ] && [ "$(kp 2)" = "1 2 1 0 0" ] && [ "$(kp 3)" = "1 2 1 0 1" ] && has '^key: keys play' "$L" &&
    ok "KEY held: keys pick (the default), D4: the tonic; GLO: keys play, the layer stays, no Options: $OUT/cr_key_pick.ppm" \
    || bad "pick / play: $(kp 1) / $(kp 2) / $(kp 3)"
grep -a '^  cr: voices' "$L" | sed -n 4p | grep -q 'chord EM sounding 1' && [ "$(kp 4)" = "1 2 1 0 1" ] && [ "$(kp 5)" = "1 4 1 0 1" ] &&
    ok "play: MAJ + E4 is E major with the tonic still D; KNOB 1 turns the tonic while it sounds: $OUT/cr_key_play.ppm" \
    || bad "playing in the layer: $(kp 4) / $(kp 5)"
[ "$(sp 5)" = 5 ] && [ "$(sp 6)" = 6 ] && [ "$(kp 6)" = "1 4 1 0 1" ] && [ "$(kp 7)" = "1 4 1 0 0" ] &&
    ok "OPT held + KNOB 1 in the layer: the split point as outside (a combo: no toggle, no Options); GLO: pick again" \
    || bad "OPT + KNOB 1: split $(sp 5) -> $(sp 6), $(kp 6) / $(kp 7)"
[ "$(kp 8)" = "1 4 0 0 1" ] && [ "$(kp 9)" = "1 4 0 1 1" ] && [ "$(kp 10)" = "1 4 0 0 0" ] &&
    ok "closed with keys play: remembered; outside the layer GLO opens the Options; the layer again: still play" \
    || bad "after the layer: $(kp 8) / $(kp 9) / $(kp 10)"
differ cr_key_pick cr_key_play "the label and the footer say pick / play"
silent_end cr_key_play
run cr_loop_row
L="$OUT/cr_loop_row.log"
has '^loop: cells Length Rate Quantize Count-in' "$L" && ok "LOOP held, stopped: the PLAY menu's page 1 over Length / Rate / Quantize / Count-in: $OUT/cr_loop_row.ppm" || bad "no loop cells"
has '^loop: length 1' "$L" && has '^loop: knob 2 rate x3/4' "$L" && has '^loop: knob 3 quantize 1/4' "$L" && has '^loop: knob 4 count-in off' "$L" &&
    ok "page 1, KNOB 1..4: length (1 bar), rate (x3/4), quantize (1/4), count-in (off)" || bad "loop knobs: $(grep '^loop: knob\|^loop: length' "$L" | tr '\n' ' ')"
has '^loop: page 2' "$L" && has '^loop: cells Level Metronome - -' "$L" && has '^loop: knob 1 level 90' "$L" && has '^loop: page 1' "$L" &&
    [ "$(grep -c '^loop: knob [234] ' "$L")" = 8 ] && has '^loop: knob 2 metronome on' "$L" && has '^loop: knob 2 metronome off' "$L" &&
    grep -a '^  loop2:' "$L" | sed -n 1p | grep -q ' page 2 ' && grep -a '^  loop:' "$L" | sed -n 1p | grep -q ' metro 1 ' &&
    ok "SELECT turns the page (not the length): page 2 is Level on KNOB 1, Metronome on KNOB 2 (on, off), KNOB 3 / 4 nothing there; SELECT back: page 1: $OUT/cr_loop_row_p2.ppm" \
    || bad "the PLAY menu's pages: $(grep '^loop: page\|^loop: cells' "$L" | tr '\n' ' ')"
grep -q "^ui: frame.*device): loopmenu .*foot '1/2 . keys: play . GLO: loops'" "$L" && grep -q "^ui: frame.*device): loopmenu .*foot '2/2 . keys: play . GLO: loops'" "$L" &&
    ok "the bottom line: the page (1/2, 2/2), what the keys do, what GLO makes them" || bad "the PLAY menu's bottom line: $(grep -o "loopmenu .*foot '[^']*'" "$L" | sed "s/.*foot //" | sort -u | tr '\n' ' ')"
grep -q '^ui: frame.*device): loopmenu .*hot 1\.0$' "$L" && grep -q '^ui: frame.*device): loopmenu .*hot 1\.1$' "$L" &&
    grep -q '^ui: frame.*device): loopmenu .*hot 1\.2$' "$L" && grep -q '^ui: frame.*device): loopmenu .*hot 1\.3$' "$L" && ok "the turned cells hot (1, 2, 3, 4)" || bad "loop: no hot cells"
mt=$(grep -c '^ui: frame.*device): meter ' "$L")
[ "$mt" = 0 ] && ok "no popup in the PLAY menu" || bad "a popup in the PLAY menu ($mt meters)"
differ cr_loop_row cr_loop_row_len "KNOB 1: the length cell (and the length big in the middle): $OUT/cr_loop_row_len.ppm"
differ cr_loop_row_len cr_loop_row_rate "KNOB 2: the rate cell: $OUT/cr_loop_row_rate.ppm"
differ cr_loop_row_rate cr_loop_row_quant "KNOB 3: the quantize cell: $OUT/cr_loop_row_quant.ppm"
differ cr_loop_row_quant cr_loop_row_countin "KNOB 4: the count-in cell: $OUT/cr_loop_row_countin.ppm"
differ cr_loop_row_p2 cr_loop_row_level "page 2, KNOB 1: the level cell: $OUT/cr_loop_row_level.ppm"
grep -q "^ui: frame.*device): loopmenu .* lm 1/1/0/[0-9]* lanes 1/0 " "$L" &&
    ok "LOOP held while it plays: the timeline in the middle (1 bar, 1 lane): $OUT/cr_loop_row_playing.ppm" || bad "no timeline while playing"
for f in cr_loop_row cr_loop_row_playing; do
    re=$(px_count "$OUT/$f.ppm" 2 100 14 140 ink); rd=$(px_count "$OUT/$f.ppm" 218 0 240 26 red)
    case $f in
    *playing) [ "$re" = 0 ] && [ "$rd" -gt 20 ] && ok "playing: no ring, the dial ($rd red): $OUT/$f.ppm" || bad "$f: ring band $re, dial $rd";;
    *) [ "$re" = 0 ] && [ "$rd" = 0 ] && ok "stopped: no ring, no dial: $OUT/$f.ppm" || bad "$f: ring band $re, dial $rd";;
    esac
done
grep -q '^loop: cells' "$L" && differ cr_loop_row_playing cr_loop_row_playing_level "playing, page 2, KNOB 1: the level cell hot: $OUT/cr_loop_row_playing_level.ppm"
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
has '^param: part 0 MIX DTUNE 40 -> 46 ' "$L" && has '^param: part 0 MIX DTUNE 46 -> 48 ' "$L" &&
    ok "MIX row B KNOB 2: a detent (40 -> 46), SHIFT held: the snap (0..127: 0 16 .. 112 127; 46 -> 48)" || bad "SHIFT snap step"
has '^edit: shift latched' "$L" && has '^param: part 0 MIX DTUNE 48 -> 64 ' "$L" && has '^edit: shift off' "$L" &&
    has '^expect led GLO on .*: ok' "$L" && has '^expect led GLO dim .*: ok' "$L" &&
    ok "GLO tap: SHIFT latched (LED lit, \"snap\"), KNOB 2 the next snap (48 -> 64), tap: off: $OUT/cr_editor_fine.ppm" || bad "SHIFT latch"
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
has '^save: part 0 overwrite slot U001 VA 01 LUSH PAD rc 0' "$L" && ok "SAVE SAVE (edited: Overwrite): LUSH PAD bound in U01 from the editor: $OUT/cr_editor_saved.ppm" || bad "SAVE SAVE: $(grep '^save:' "$L")"
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
run cr_expr
L="$OUT/cr_expr.log"
has '^edit: group MOD screen 3 lane 1 part 0$' "$L" && has '^param: part 0 MOD AMT3 63 -> 61' "$L" &&
    ok "EXPR on FM6: the MOD group's third screen; KNOB 3 on the unset VEL slot takes its default (AMT 63) then the turn (two detents at once: a turn's first two count one): $OUT/cr_expr_fm6_vel.ppm" ||
    bad "EXPR on FM6: $(grep -a '^param: part 0 MOD AMT3' "$L" | head -1)"
has '^edit: group MOD screen 3 lane 2 part 0$' "$L" && has '^param: part 0 MOD DST4 2 -> 3 (SHP)$' "$L" &&
    ok "SELECT: the AT lane; KNOB 2 takes its default (DST CUT) then steps it to SHP: $OUT/cr_expr_fm6_at.ppm" ||
    bad "EXPR AT lane: $(grep -a '^param: part 0 MOD DST4' "$L" | head -1)"
[ "$(grep -ac '^param: part 0 MOD AMT4 40 -> 39' "$L")" = 2 ] &&
    ok "EXPR on the VA (after its matrix) and FM TONE (after the platform routes): the AT slot's default amount 40 by Aftertouch Sens Mid, one detent one unit" ||
    bad "EXPR on the VA / FM TONE: $(grep -ac '^param: part 0 MOD AMT4' "$L") AMT4 lines"
run cr_touch
L="$OUT/cr_touch.log"
tk() { grep -a "^touch: key $1 vel " "$L" | sed -n "$2p" | sed 's/^touch: key [0-9]* vel //'; }
atmax() { grep -a '^touch: at ' "$L" | sed 's/touch: at //' | sort -n | tail -1; }
[ "$(tk 9 1)" = "100 fixed" ] && [ "$(tk 9 2)" = "30 sensed" ] && [ "$(tk 9 3)" = "120 sensed" ] &&
    ok "the keys' velocity in the emulator: Key Touch Fixed plays D4 at Options > Velocity (100); Velocity + Aftertouch: D4 at 30 plays 30, at 120 plays 120 (the sensing's own path, cr_sense.c fed by emu_touch_feed)" ||
    bad "touch velocity: '$(tk 9 1)' / '$(tk 9 2)' / '$(tk 9 3)'"
echo "$(tk 26 1)" | grep -q ' sensed$' && [ "$(tk 26 1 | cut -d' ' -f1)" -ge 56 ] && [ "$(tk 26 1 | cut -d' ' -f1)" -le 78 ] &&
    ok "G5 (the top row): its velocity through the PA10 probe's model, 64 read as $(tk 26 1 | cut -d' ' -f1) (the probe's curve, quantised)" || bad "the top row's velocity: '$(tk 26 1)'"
[ "$(atmax)" -ge 65 ] && [ "$(atmax)" -le 85 ] && [ "$(grep -a '^touch: at ' "$L" | tail -1)" = "touch: at 0" ] &&
    ok "a lean of 60 % on the held D4 after it settled: aftertouch up to $(atmax) (60 % of the span, smoothed), eased: back to 0" ||
    bad "aftertouch: max $(atmax), last $(grep -a '^touch: at ' "$L" | tail -1)"
[ "$(tk 9 5 | cut -d' ' -f1)" -ge 10 ] && [ "$(tk 9 5 | cut -d' ' -f1)" -le 22 ] &&
    ok "Velocity Curve +3: the same press (30 on Velo's scale) reads $(tk 9 5 | cut -d' ' -f1)" || bad "Velocity Curve +3: '$(tk 9 5)'"
run cr_expr_knob
L="$OUT/cr_expr_knob.log"
am() { grep -a '^param: part 0 MOD AMT3 ' "$L" | sed -n 's/^param: part 0 MOD AMT3 [0-9]* -> \([0-9]*\) .*/\1/p' | tr '\n' ' '; }
[ "$(am)" = "62 61 60 59 58 52 46 40 34 33 34 35 41 47 45 44 43 42 " ] &&
    ok "EXPR's amount steps as every value: one unit a slow detent (62 61 60 59), six a quick one (58 then 52 46 40 34), one after a rest (33); a quick reversal's first two detents one (34 35) then six (41 47); two detents in one frame after a rest one each (45); a hand turn at 90 ms one each (44 43 42): $OUT/cr_expr_knob_slow.ppm" ||
    bad "EXPR's amount steps: $(am)"
[ "$(grep -ac '^param: part 0 MOD DST3 ' "$L")" = 3 ] && has '^param: part 0 MOD DST3 6 -> 7 (CHO)$' "$L" &&
    ok "EXPR's destination: a named choice, one item a quick detent (PAN DIST CHO)" || bad "EXPR's destination: $(grep -a '^param: part 0 MOD DST3' "$L" | tail -1)"
has '^param: part 0 MOD AMT4 40 -> 39' "$L" && has '^param: part 0 MOD AMT4 39 -> 38' "$L" &&
    ok "the AT lane's amount the same (40 39 38): $OUT/cr_expr_knob_at.ppm" || bad "EXPR AT amount: $(grep -a '^param: part 0 MOD AMT4' "$L" | tail -1)"
run cr_editor_map
L="$OUT/cr_editor_map.log"
has '^mod: ENV2 -> CUT +12 slot 1$' "$L" && ok "ENV held + KNOB 1 +2 on FILTER: mod: ENV2 -> CUT +12 slot 1 (ENV 2 by default): $OUT/cr_map_hot.ppm" || bad "ENV mapping: $(grep -m1 '^mod:' "$L")"
yel() { set -- "$(pixel "$OUT/$1.ppm" $2 $3)" "$4"; set -- "$(echo $1)" "$2"; [ "$1" = "$2" ]; }   # yel SHOT X Y "R G B"
yel cr_map_mark 55 126 "247 182 0" && ok "the Cutoff cell's mark: a yellow square (ENV): $OUT/cr_map_mark.ppm" || bad "no yellow mark on Cutoff: $(pixel "$OUT/cr_map_mark.ppm" 55 126)"
yel cr_map_filt 55 126 "0 0 0" && ok "no mark before the mapping" || bad "a mark before: $(pixel "$OUT/cr_map_filt.ppm" 55 126)"
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
has '^mod: not modulatable$' "$L" && ok "a platform engine (WHEEL): \"not modulatable\"" || bad "not modulatable"
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
has '^param: part 0 ENV DEC 64 -> 70 ' "$L" && ok "a platform engine's ENV (WHEEL SOFT FLUTE): a detent: DEC 64 -> 70 (5% of 0..127): $OUT/cr_editor_p_env.ppm" || bad "coarse step: $(grep -m1 '^param:' "$L")"
has '^param: part 0 ENV DEC 70 -> 80 ' "$L" && ok "SHIFT held + KNOB 2: DEC 70 -> 80 (the snap: 0..127 on 16s)" || bad "snap step"
grep '^param: part 0 ENV DEC' "$L" | tail -1 | grep -q -- '-> 127 ' && ok "KNOB 2 +30: DEC clamps at 127" || bad "no clamp at 127"
has '^param: part 0 ENV ATK 0 -> 42 ' "$L" && ok "KNOB 1 +7 (6 a detent): ATK 0 -> 42" || bad "KNOB 1: ATK not changed"
grep -q '^split\|split point' "$L" && bad "SHIFT + KNOB reached the split point" || true
has '^param: part 0 LFO WAVE 0 -> 1 ' "$L" && ok "LFO WAVE steps by one" || bad "WAVE: $(grep 'LFO WAVE' "$L")"
has '^param: part 0 MOD FLT ' "$L" && ok "the platform MOD (4 slots): KNOB 3 the amount: $OUT/cr_editor_p_mod.ppm" || bad "platform MOD"
grep '^param: part 0 MIX VCE' "$L" | grep -q -- '\([0-9]*\) -> ' && ok "MIX VOICE: $(grep '^param: part 0 MIX VCE' "$L" | head -1 | sed 's/^param: //')" || bad "no VOICE step"
cmp -s "$OUT/cr_editor_steps.wav" "$OUT/cr_editor_ref.wav" && bad "the same audio with another envelope" \
    || ok "the same chord with ATK 42 / DEC 127: the audio differs ($(num "non-zero samples" "$L") / $(num "non-zero samples" "$OUT/cr_editor_ref.log") non-zero samples)"
silent_end cr_editor_steps

echo "Shift + Up / Down: the firmware's OPT snap (GLO held around the detent), Page Down to KNOB 1; slow detents: the finest step"
run cr_fine
L="$OUT/cr_fine.log"
d=$(grep '^deep: .* CUT ' "$L" | sed -E 's/.* CUT ([0-9]+) -> ([0-9]+).*/\1 \2/' | awk '{printf "%d ", $2 - $1}')
[ "$d" = "1 1 8 16 1 " ] && ok "CUT steps plain (slow: 1) / Shift (snap: 70 72 -> 80 -> 96) / plain: $d(the SHIFT latch untouched)" \
    || bad "snap steps: CUT deltas '$d' (want 1 1 8 16 1)"

echo "knob acceleration and OPT snap (0.22, docs/EDITOR.md): slow and quick detents, GLO held: the snap rules"
run cr_knob_accel
L="$OUT/cr_knob_accel.log"
dv() { grep -a "^deep: .* $1 [-0-9]* -> " "$L" | sed -E "s/.* $1 (-?[0-9]+) -> (-?[0-9]+) .*/\2/" | tr '\n' ' '; }
pv() { grep -a "^param: part 0 MIX $1 " "$L" | sed -E 's/.* (-?[0-9]+) -> (-?[0-9]+) .*/\2/' | tr '\n' ' '; }
c=$(dv CUT)
[ "$c" = "71 72 73 74 80 86 92 98 104 112 127 112 " ] &&
    ok "VA's cutoff (0..127, an integer): 3 slow detents (300 ms apart) 1 each, a slow one after the rest, 5 a frame apart 6 each (the range / 20); GLO held: 112 127, back 112" \
    || bad "VA's cutoff: '$c' (want 71 72 73 74 80 86 92 98 104 112 127 112)"
[ "$(pv PAN)" = "8 16 " ] && [ "$(pv VCE)" = "1 " ] &&
    ok "GLO held: Pan (-64..63) snaps 0 -> 8 -> 16; Voice (a named choice) one item (POLY -> MONO)" || bad "Pan / Voice under GLO: '$(pv PAN)' / '$(pv VCE)'"
o=$(dv 'OFS C')
[ "$o" = "1 2 3 4 14 24 34 44 54 64 74 84 94 100 0 -100 " ] &&
    ok "FM TONE's OFS C (-1.00 .. +1.00, 0.01 a stored step: hi-res style): slow 0.01 a detent, quick 0.10; GLO held: whole numbers (1.00, 0.00, -1.00)" \
    || bad "OFS C: '$o' (want 1 2 3 4 14 24 34 44 54 64 74 84 94 100 0 -100)"
[ "$(dv SPEED)" = "6399 6399 3200 1600 800 " ] &&
    ok "FM TONE's LFO 1 SPEED (bipolar): GLO held snaps along the speed list x 100, hundredths since FM TONE's ranges (from the INIT's 32.00, the Digitone's: -> 63.99, the end, 63.99; back 32.00 16.00 8.00)" || bad "LFO SPEED under GLO: '$(dv SPEED)'"
has '^sound: part 0 engine VA pos 1 LUSH PAD' "$L" && has '^sound: part 0 engine FM TONE pos 1 EP' "$L" &&
    ok "GLO + PRESETS still changes the engine (FM6 -> VA, VA -> FM TONE)" || bad "GLO + PRESETS: no engine change"

echo "FM TONE's hi-res values (hundredths, F_FIX2) under knob acceleration and OPT snap (real timing)"
run cr_quad_hires_knobs
L="$OUT/cr_quad_hires_knobs.log"
f=$(dv FREQ)
[ "$f" = "12699 12698 12697 12062 11427 11400 11500 11600 " ] &&
    ok "FREQ (0.00..127.00, from its top 127.00): slow detents 0.01 each, quick 6.35 (the range / 20); GLO held: 114.00 115.00 116.00 (whole numbers)" \
    || bad "FREQ: '$f' (want 12699 12698 12697 12062 11427 11400 11500 11600)"
s=$(dv SPEED)
[ "$s" = "1600 800 -800 -1600 -3200 -6400 -3200 -1600 -800 800 1600 3200 6399 6399 6399 " ] &&
    ok "LFO 1 SPEED (hi-res, min < 0; from the INIT's 32.00): GLO held snaps along +-8.00 16.00 32.00 64.00 (the ends -64.00, +63.99)" \
    || bad "SPEED under GLO: '$s'"
r=$(dv REL)
[ "$r" = "15 16 22 28 34 40 46 52 58 64 70 76 82 88 94 100 106 112 118 124 127 112 96 112 " ] &&
    has 'REL 124 -> 127 (INF)' "$L" && has 'REL 127 -> 112 (112)' "$L" && has 'REL 96 -> 112 (112)' "$L" &&
    ok "AMP REL (0..126 + INF, an integer since version 5): 1 slow, 6 quick to the end (127: INF); GLO held: the 16 grid 112 96, back 112: $OUT/cr_quad_hires_rel.ppm" \
    || bad "AMP REL: '$r'"

echo "VA's synced LFO RATE: the list one item a detent both ways, GLO held the straight ones; FENV in hundredths"
run cr_va_sync_list
L="$OUT/cr_va_sync_list.log"
r=$(dv RATE)
[ "$r" = "15 14 13 12 11 10 9 10 11 12 10 7 4 7 " ] && has 'SYNC1 0 -> 1 (ON)' "$L" && has 'RATE 16 -> 15 (1/2D)' "$L" &&
    has 'RATE 12 -> 11 (2BART)' "$L" && has 'RATE 7 -> 4 (4BAR)' "$L" &&
    ok "LFO 1 SYNC on (RATE 50 -> 1/2); slow and quick detents one item each, down and up; GLO held: 2BAR 3BAR 4BAR, back 3BAR" \
    || bad "synced RATE: '$r' (want 15 14 13 12 11 10 9 10 11 12 10 7 4 7)"
f=$(dv FENV)
[ "$f" = "999 998 1000 " ] && has 'FENV 999 -> 998 (+9.98)' "$L" &&
    ok "VA FENV (F_FIX2, -64.00..+63.99): slow detents 0.01, GLO held the whole number (+10.00)" || bad "FENV: '$f'"
run cr_va_mod_fine
L="$OUT/cr_va_mod_fine.log"
a=$(dv AMT)
[ "$a" = "290 280 270 260 270 280 900 1500 1400 1300 " ] && has 'AMT 300 -> 290 (+2.90)' "$L" && has 'AMT 1500 -> 1400 (+14.00)' "$L" &&
    ok "VA matrix AMT (issue 13: hundredths, tenths below 6): slow detents a tenth (2.90 .. 2.60), a reversal's first two a tenth, quick ones 6.35 to whole numbers (9 15), slow above 6 a whole (14), GLO held whole (13)" \
    || bad "VA AMT: '$a' (want 290 280 270 260 270 280 900 1500 1400 1300)"
has '^mod: LFO1 -> RES +0.1 slot 3$' "$L" && ok "LFO held + KNOB 2, one slow detent: mod: LFO1 -> RES +0.1 (the hot cell's tenths)" \
    || bad "fine mapping: $(grep -a '^mod:' "$L")"

echo "EDIT held: the engine picker as a preview (cancel / keep), its roots, PRESETS in the editor"
run cr_editor_pick --wav "$OUT/cr_editor_pick.wav"
L="$OUT/cr_editor_pick.log"
o1=$(sed -n 's/^picker: open part 0 LUSH PAD crc \([0-9a-f]*\)$/\1/p' "$L" | sed -n 1p)
c1=$(sed -n 's/^picker: cancel part 0 -> LUSH PAD crc \([0-9a-f]*\)$/\1/p' "$L" | sed -n 1p)
has '^preset: part 0 -> VA / ' "$L" && [ -n "$o1" ] && [ "$o1" = "$c1" ] &&
    ok "KNOB 1 previewed a preset, OCT- cancelled: LUSH PAD restored (crc $c1): $OUT/cr_pick_preview.ppm, $OUT/cr_pick_cancel.ppm" \
    || bad "picker cancel: open '$o1' cancel '$c1'"
cmp -s "$OUT/cr_pick_editor.ppm" "$OUT/cr_pick_cancel.ppm" && ok "OCT-: back in the editor as it was" || bad "not back in the editor: $OUT/cr_pick_cancel.ppm"
c2=$(sed -n 's/^picker: cancel part 0 -> LUSH PAD crc \([0-9a-f]*\)$/\1/p' "$L" | sed -n 2p)
! has '^picker: roots' "$L" && has '^expect sound .*: ok' "$L" && has '^expect silence .*: ok' "$L" &&
    has '^engine: part 0 -> FM6' "$L" && [ "$c2" = "$o1" ] && has '^fav: part 0 LUSH PAD on (tag 13 id 0, 1 marked)' "$L" &&
    ok "the picker's roots play (D4 sounded; KNOB 4 there marks the sound as a favourite since 0.22 and changes nothing of the sound: the read-out with the star); SELECT -2: FM6; OCT-: the VA patch back (crc $c2): $OUT/cr_pick_roots_play.ppm" \
    || bad "picker roots / engine cancel (crc '$c2')"
[ "$(grep -c '^engine: part 0' "$L")" = 1 ] && ok "no engine switched by D4 in the picker (one switch: SELECT's)" || bad "a root switched an engine"
grep '^layer: open 5' "$L" | sed -n 3p | grep -q . && has '^picker: keep part 0 ' "$L" &&
    ok "PRESETS in the editor: the picker previewing the next place of the pool, OCT+ kept it: $OUT/cr_pick_presets.ppm $OUT/cr_pick_kept.ppm" || bad "PRESETS in the editor"
grep '^picker: keep' "$L" | grep -qv 'LUSH PAD' && ok "kept: $(grep '^picker: keep' "$L" | sed 's/^picker: //')" || bad "kept the old sound"
silent_end cr_editor_pick

echo "EDIT held: the engine picker"
run cr_engine --wav "$OUT/cr_engine.wav"
has '^engine: part 0 -> WHEEL' "$OUT/cr_engine.log" && has 'part 0: WHEEL /' "$OUT/cr_engine.log" &&
    ok "EDIT held + SELECT +5 (the sixth of six engines): part 0 FM6 -> WHEEL: $OUT/cr_engine_switched.ppm" || bad "the engine did not switch"
has '^preset: part 0 -> WHEEL / ' "$OUT/cr_engine.log" && ok "EDIT held + KNOB 1: the next place of the engine's pool" || bad "KNOB 1: no preset step"
silent_end cr_engine

echo "SAVE: the dialog, Save as new, naming, the user slot"
run cr_save
has '^save: dialog part 0 TINE EP (save as new)' "$OUT/cr_save.log" && has '^save: new part 0 FM6 26 (slot U001)' "$OUT/cr_save.log" &&
    ok "SAVE on TINE EP: the dialog, Save as new by default; OCT+: the naming screen at FM6 26: $OUT/cr_save_open.ppm $OUT/cr_save_naming.ppm" || bad "the dialog: $(grep '^save:' "$OUT/cr_save.log" | head -2)"
has '^save: part 0 slot U001 name ADG' "$OUT/cr_save.log" && ok "D4 E4 F4, OCT+: U01 \"ADG\": $OUT/cr_save_typed.ppm" || bad "not saved"
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
a=$(L cr_loop_sync 1); b=$(L cr_loop_sync 2); c=$(L cr_loop_sync 4); c3=$(L cr_loop_sync 3)
[ "$(lf "$a" cap)" = 2 ] && has 'expect sound .*: ok' "$OUT/cr_loop_sync.log" &&
    ok "2 bars synced: REC starts a one-bar count-in, its click audible with nothing played: $OUT/cr_loop_countin.ppm" || bad "count-in: $a"
[ "$(lf "$b" state)" = 2 ] && [ "$(lf "$b" len)" = 768 ] && [ "$(lf "$b" events)" = 2 ] &&
    ok "it recorded 2 bars (768 ticks) and committed by itself, playing: $OUT/cr_loop_rec.ppm" || bad "sync take: $b"
has '^save: global, at the stop' "$OUT/cr_loop_sync.log" && [ "$(lf "$c3" used)" = 000 ] && [ "$(lf "$c3" state)" = 2 ] &&
    grep -a '^  loop2:' "$OUT/cr_loop_sync.log" | sed -n 3p | grep -q 'save-pending 1' &&
    grep -a '^  flash:' "$OUT/cr_loop_sync.log" | sed -n 3p | grep -q ', 0 writes,' &&
    has '^loop: save slot 1 ' "$OUT/cr_loop_sync.log" && has '^save: global rc 0' "$OUT/cr_loop_sync.log" && [ "$(lf "$c" used)" = 001 ] &&
    ok "SAVE held while it plays: \"saves at stop\", nothing written while the loop plays ($OUT/cr_loop_save.ppm); LOOP stops it: saved, slot 1 with it ($OUT/cr_loop_saved.ppm)" \
    || bad "the global save at the stop: $c3 / $c"
silent_end cr_loop_sync
run cr_loop_load --flash "$OUT/cr_loop_flash.bin"
a=$(L cr_loop_load 1); b=$(L cr_loop_load 2); c=$(L cr_loop_load 4)
[ "$(lf "$a" slot)" = 1 ] && [ "$(lf "$a" events)" = 2 ] && ok "relaunched with --flash: slot 1 back at power-on" || bad "boot: $a"
[ "$(lf "$b" state)" = 2 ] && [ "$(lf "$b" len)" = 768 ] && [ "$(lf "$b" played)" -ge 1 ] &&
    ok "slot 1 in RAM at power-on (the PLAY menu: $OUT/cr_loop_load_menu.ppm), LOOP plays it: $OUT/cr_loop_loaded.ppm" || bad "load / play: $b"
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
[ "$(lf "$f" events)" = 3 ] && grep -a '^  loop2:' "$SL" | sed -n 6p | grep -q ' dirty 1 ' && [ "$(lf "$g" slot)" = 2 ] && [ "$(lf "$g" events)" = 1 ] &&
    ! has '^loop: auto-save slot 1 [0-9]* bytes rc 0 (stopped)' "$SL" && ! has '(idle)' "$SL" &&
    has '^loop: auto-save slot 1 [0-9]* bytes rc 0 (leaving it)' "$SL" && [ "$(lf "$h" slot)" = 1 ] &&
    [ "$(lf "$h" events)" = 3 ] && [ "$(lf "$h" layers)" = 2 ] &&
    ok "stopped: an overdub on slot 1 is NOT written at the stop (not saved, 1 s later); PRESETS +1 leaves the slot: written then; PRESETS -1 brings it back with its 3 events, 2 layers" \
    || bad "switch while stopped: $f / $g / $h"
gr=$(od -An -tu1 -v -j 15 "$OUT/cr_loop_slots_saved.ppm" | awk '{ for (k = 1; k <= NF; k++) v[n++] = $k }
     END { c = 0; for (o = 0; o + 2 < n; o += 3) if (v[o] < 100 && v[o+1] > 140 && v[o+2] > 90 && v[o+2] < 160) c++; print c }')
[ "$gr" -gt 1000 ] && ok "the message \"loop 2 . saved 1\" when the slot is left, in green ($gr px): $OUT/cr_loop_slots_saved.ppm" || bad "no green message ($gr px)"
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
    ok "a switch cancelled by a stop before the cycle's end: slot 1 stays selected, the copy staged for the switch written at the stop (5 events)" \
    || bad "cancelled switch: $m"
silent_end cr_loop_slots
# a Split Bass bass-side note is recorded as one and plays on the bass part (0.18; docs/LOOPER.md "What is recorded")
run cr_loop_bass
LB="$OUT/cr_loop_bass.log"
lvp() { grep -a '^  loopv:' "$LB" | sed -n "$1p" | sed -n 's/.*chord \([0-9]*\) bass \([0-9]*\) bass-side-events \([0-9]*\) stream-notes main \([0-9]*\) bass \([0-9]*\)$/\1 \2 \3 \4 \5/p'; }
lpv() { grep -a "^  part $2:" "$LB" | sed -n "$1p" | sed -n 's/.* voices \([0-9]*\),.*/\1/p'; }
lkb() { grep -a '^  kbd:' "$LB" | sed -n "$1p" | sed -n 's/.*keyboard \([a-z]*\) single \([0-9]\) .*/\1 \2/p'; }
lbz() { grep -a 'parts busy' "$LB" | sed -n "$1p" | sed -n 's/.*parts busy \([0-9]*\).*/\1/p'; }
a=$(L cr_loop_bass 2)
[ "$(lkb 1)" = "full 2" ] && [ "$(lf "$a" events)" = 3 ] && [ "$(lf "$a" state)" = 2 ] && [ "$(lvp 2)" = "0 1 2 0 1" ] && [ "$(lpv 2 1)" -ge 1 ] &&
    [ "$(lvp 3)" = "1 0 2 1 0" ] && [ "$(lpv 3 0)" -ge 1 ] && [ "$(lvp 4)" = "0 1 2 0 1" ] && [ "$(lpv 4 1)" -ge 1 ] &&
    ok "the Full keyboard + Split Bass: F3 A4 E4 recorded (2 bass-side events); the loop plays F3 and E4 on the bass part alone (part 1, the bass stream), A4 on the chord part" \
    || bad "a looped bass line: $(lkb 1) / $a / $(lvp 2) p1 $(lpv 2 1) / $(lvp 3) p0 $(lpv 3 0) / $(lvp 4)"
[ "$(lkb 5)" = "full 0" ] && [ "$(lvp 5)" = "0 1 2 0 1" ] && [ "$(lpv 5 1)" -ge 1 ] &&
    ok "Single = Full while it plays: the looped F3 still on the bass part (recorded so)" || bad "Single = Full at playback: $(lkb 5) / $(lvp 5) p1 $(lpv 5 1)"
[ "$(lvp 6)" = "0 1 2 0 2" ] && [ "$(lvp 7)" = "1 0 2 1 1" ] && [ "$(lpv 7 1)" -ge 1 ] && [ "$(lvp 8)" = "1 0 2 1 0" ] && [ "$(lbz 9)" = 0 ] &&
    [ "$(lvp 9)" = "0 0 2 0 0" ] &&
    ok "live over the loop: a live C4 with the looped F3 (2 notes on the bass stream); a live F3 held past the looped F3's end still sounds, let go: off; stopped: silent (no stuck voice)" \
    || bad "live bass over a looped bass: $(lvp 6) / $(lvp 7) p1 $(lpv 7 1) / $(lvp 8) / busy $(lbz 9) $(lvp 9)"
b=$(L cr_loop_bass 10); c=$(L cr_loop_bass 11)
has '^loop: auto-save slot 1 39 bytes rc 0 (leaving it)' "$LB" && [ "$(lf "$b" slot)" = 2 ] && [ "$(lf "$b" events)" = 0 ] && has '^loop: slot 1 loaded' "$LB" &&
    [ "$(lf "$c" slot)" = 1 ] && [ "$(lf "$c" events)" = 3 ] && [ "$(lvp 11)" = "0 0 2 0 0" ] && [ "$(lkb 11)" = "full 0" ] &&
    [ "$(lvp 12)" = "0 1 2 0 1" ] && [ "$(lpv 12 1)" -ge 1 ] && [ "$(lpv 12 0)" = 0 ] && [ "$(lvp 13)" = "1 0 2 1 0" ] && [ "$(lbz 14)" = 0 ] &&
    ok "slot 1 written as it is left (39 bytes: as big as before), read back from its record after slot 2: 2 bass-side events; Single = Full: F3 on the bass part alone, A4 on the chord part" \
    || bad "the slot's record: $b / $c / $(lvp 11) / $(lvp 12) p0 $(lpv 12 0) p1 $(lpv 12 1) / $(lvp 13)"
d=$(L cr_loop_bass 15)
[ "$(lkb 15)" = "chords 2" ] && [ "$(lf "$d" events)" = 2 ] && [ "$(lvp 15)" = "1 0 1 3 0" ] && [ "$(lvp 16)" = "0 1 1 0 1" ] && [ "$(lpv 16 1)" -ge 1 ] &&
    ok "the Chords layout + Split Bass (the split point F): G major a chord (3 notes on the main stream), D4 a bass-side note: the loop plays it on the bass part alone" \
    || bad "the Chords layout: $(lkb 15) / $d / $(lvp 15) / $(lvp 16) p1 $(lpv 16 1)"
silent_end cr_loop_bass
# the PLAY menu (docs/LOOPER-MODES.md, design/choralroot-fm1-looper-screens.png): the knobs, GLO, the strip, the modes
export EMU_UI_LOG=0
run cr_loop_menu
LM="$OUT/cr_loop_menu.log"
a=$(L cr_loop_menu 2); b=$(L cr_loop_menu 3)
has '^loop: length 3' "$LM" && grep -aq "loopmenu .*title '4 bars'" "$LM" && ok "KNOB 1 +3 in the menu: 4 bars, big in the middle (state 1): $OUT/cr_lm_1.ppm" || bad "the menu's length"
has 'chord DM sounding 1' "$LM" && ok "Keys = Play: MAJ + D4 plays DM with the menu open" || bad "Keys = Play: no D"
has '^loop: keys Loops' "$LM" && has 'expect led GLO on .*: ok' "$LM" && has 'expect led D#4 on .*: ok' "$LM" &&
    has 'expect led D#4 off .*: ok' "$LM" && ! has 'expect.*FAILED' "$LM" &&
    [ "$(grep -ac '^loop: keys ' "$LM")" = 2 ] && grep -aq "loopmenu .*foot '1/2 . keys: loops . GLO: play'" "$LM" &&
    ok "GLO in the menu: Keys = Loops (GLO lit, the slot map: CLEAR lit, the bottom line says so; state 2: $OUT/cr_lm_2.ppm), GLO back: Play; KNOB 4 is no Keys cell any more (GLO alone)" || bad "the menu's keys: $(grep -a 'expect.*FAILED' "$LM" | head -2)"
has '^loop: mode Overdub' "$LM" && grep -aq "loopmenu .*item 'Overdub'" "$LM" && ok "ALGORITHM +2: the record mode Overdub (state 3): $OUT/cr_lm_3.ppm" || bad "the mode picker"
has '^loop: slot 5 turned' "$LM" && grep -aq "loopmenu .*slot 5 jump 0 sub '' right 'empty'" "$LM" &&
    ok "PRESETS +4: slot 5 (empty: \"empty\" at the top right; state 4): $OUT/cr_lm_4.ppm" || bad "the slot strip"
[ "$(lf "$a" cap)" = 1 ] && grep -aq "loopmenu .*title 'Free' .*sub 'the first chord starts the take' right 'Rec . Overwrite'" "$LM" &&
    [ "$(lf "$b" cap)" = 0 ] && ok "REC in the menu: armed, \"Rec · Overwrite\" at the top right (state 5: $OUT/cr_lm_5.ppm); REC again: cancelled"     || bad "armed in the menu: $a / $b"
ly() { grep -a '^  cr: key' "$LM" | sed -n "$1p" | sed -n 's/.* layer \([0-9]*\) .*/\1/p'; }
[ "$(ly 3)" = 8 ] && [ "$(ly 4)" = 0 ] && [ "$(ly 5)" = 8 ] && [ "$(ly 6)" = 0 ] && ok "OCT- closes the menu, OCT+ closes it too (nothing to confirm)" \
    || bad "the menu's OCT: layers $(ly 3) $(ly 4) $(ly 5) $(ly 6)"
[ "$(ly 7)" = 8 ] && [ "$(ly 8)" = 0 ] && [ "$(lf "$(L cr_loop_menu 7)" state)" = "$(lf "$(L cr_loop_menu 8)" state)" ] &&
    ok "LOOP held again in its menu: closed (no play / stop)" || bad "LOOP held again: layers $(ly 7) $(ly 8)"
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
    grep -aq "loopmenu .* lm 2/1/0/2 .*sub 'G#M' right 'Rec . Step'" "$LM" &&
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
# 0.18: SAVE held is the global save; the SAVE-held loop map is gone
run cr_save_hold
SHL="$OUT/cr_save_hold.log"
ly() { grep -a '^  cr: key' "$1" | sed -n "$2p" | sed -n 's/.* layer \([0-9]*\) options \([0-9]\).*/\1 \2/p'; }
l2() { grep -a '^  loop2:' "$1" | sed -n "$2p" | sed -n "s/.* $3 \([0-9]*\).*/\1/p"; }
a=$(L cr_save_hold 1); b=$(L cr_save_hold 2); c=$(L cr_save_hold 3)
[ "$(lf "$a" used)" = 000 ] && [ "$(l2 "$SHL" 1 dirty)" = 1 ] && has '^save: global rc 0' "$SHL" && [ "$(lf "$b" used)" = 001 ] && [ "$(l2 "$SHL" 2 dirty)" = 0 ] &&
    [ "$(ly "$SHL" 2)" = "0 0" ] && has 'expect led D#4 off .*: ok' "$SHL" &&
    ok "SAVE held past the hold time: the global save (the loop to its slot with it), the message \"saved\"; no layer, no slot map on the roots: $OUT/cr_save_hold.ppm" \
    || bad "SAVE held: $a / $b / layer $(ly "$SHL" 2)"
[ "$(lf "$c" slot)" = 1 ] && has 'expect sound .*: ok' "$SHL" && grep -a '^  cr: voices' "$SHL" | sed -n 3p | grep -q 'sounding 1' &&
    ok "a root pressed with SAVE down plays (it picked a slot until 0.17); the slot stays 1" || bad "SAVE held + a root: $c"
[ "$(grep -ac '^save: dialog' "$SHL")" = 1 ] && [ "$(grep -ac '^save: global' "$SHL")" = 1 ] && ! cmp -s "$OUT/cr_save_hold_after.ppm" "$OUT/cr_save_tap.ppm" &&
    ok "SAVE let go after the hold: nothing more; a SAVE tap is still the save-preset dialog: $OUT/cr_save_tap.ppm" || bad "SAVE tap after the hold: $(grep -a '^save:' "$SHL" | tr '\n' ' ')"
silent_end cr_save_hold
# 0.18: the loop's Rate
run cr_loop_rate
RL="$OUT/cr_loop_rate.log"
pl() { lf "$(L cr_loop_rate "$1")" played; }
rt=""; for k in 2 3 4 5 6 7 8; do rt="$rt $(( $(pl $k) - $(pl $((k - 1))) ))"; done
[ "$rt" = " 1 2 3 4 6 8 16" ] && has '^loop: knob 2 rate x1/4' "$RL" && has '^loop: knob 2 rate x4' "$RL" &&
    ok "Rate x1/4 x1/2 x3/4 x1 x1.5 x2 x4: a 1-bar loop starts 1 2 3 4 6 8 16 times in 4 bars of the tempo: $OUT/cr_loop_rate_q.ppm, cr_loop_rate_4.ppm" \
    || bad "the Rate against the clock: starts in 4 bars:$rt (want 1 2 3 4 6 8 16)"
[ "$(l2 "$RL" 9 rate) $(l2 "$RL" 9 want)" = "7 9" ] && [ "$(l2 "$RL" 10 rate)" = 9 ] && [ $(( $(pl 11) - $(pl 10) )) = 2 ] &&
    ok "the Rate changed while it plays: x1 until the loop's start, x2 from there (2 starts in the next 1.9 s): never in the middle of a cycle" \
    || bad "a rate change while playing: $(l2 "$RL" 9 rate)/$(l2 "$RL" 9 want) -> $(l2 "$RL" 10 rate), $(( $(pl 11) - $(pl 10) )) starts"
[ $(( $(pl 14) - $(pl 13) )) = 2 ] && [ $(( $(pl 15) - $(pl 14) )) = 2 ] && [ "$(l2 "$RL" 13 rate)" = 2 ] && [ "$(l2 "$RL" 15 rate)" = 0 ] &&
    has '^loop: knob 2 rate x1/6' "$RL" && has '^loop: knob 2 rate x1/8' "$RL" && has '^loop: knob 2 rate x1/12' "$RL" &&
    [ "$(grep -c '^loop: knob 2 rate x1/16' "$RL")" = 2 ] &&
    ok "the slow Rates x1/6 x1/8 x1/12 x1/16: the 1-bar loop starts twice in 17 s at x1/8 and twice in 33.5 s at x1/16 (a step is a bar); the knob ends at x1/16: $OUT/cr_loop_rate_16.ppm" \
    || bad "the slow Rates: $(( $(pl 14) - $(pl 13) )) starts at x1/8 (want 2), $(( $(pl 15) - $(pl 14) )) at x1/16 (want 2), rate $(l2 "$RL" 13 rate) / $(l2 "$RL" 15 rate) (want 2 / 0): $(grep -c '^loop: knob 2 rate x1/16' "$RL") x1/16 traces"
silent_end cr_loop_rate
# 0.18: Step with Length Free; the loop editor (a step sequencer since 2026-10-12: sixteenth steps, a step's length
# is its note's)
export EMU_UI_LOG=0
run cr_loop_edit
unset EMU_UI_LOG
EL="$OUT/cr_loop_edit.log"
e() { L cr_loop_edit "$1"; }
[ "$(lf "$(e 1)" events)" = 3 ] && [ "$(lf "$(e 1)" len)" = 72 ] && [ "$(lf "$(e 1)" state)" = 1 ] &&
    ok "Step, Length Free, Quantize none: 3 chords are a loop 3 steps long (72 ticks: 3 sixteenths, not a bar, not 3 quarter notes)" || bad "the 3-step loop: $(e 1)"
has '^loopedit: open rc 0' "$EL" && ! has '^loopedit: open rc [1-9]' "$EL" && [ "$(l2 "$EL" 2 edit)" = 1 ] && [ "$(l2 "$EL" 2 grid)" = 24 ] &&
    has '^loopedit: step 1/3 notes 1 long 1 len 72' "$EL" && has '^loop: cells Note len Steps Chance Velocity' "$EL" && [ "$(ly "$EL" 2)" = "8 0" ] &&
    ok "EDIT with Quantize none: the loop editor opens (no \"set Quantize to edit\"): the PLAY menu's screen (Note len / Steps / Chance / Velocity), step 1 of 3 sixteenths: $OUT/cr_le_1.ppm" \
    || bad "the editor: $(grep -a '^loopedit:' "$EL" | sed -n 1,2p | tr '\n' ' ')"
has '^loopedit: knob 2 loop length 384 ok' "$EL" && [ "$(lf "$(e 3)" len)" = 384 ] && [ "$(lf "$(e 3)" events)" = 3 ] &&
    ok "KNOB 2 +13: the loop 16 steps (384), its three chords where they were" || bad "Steps: $(e 3)"
has '^loopedit: step 2/16 notes 1 long 1 len 384' "$EL" && has 'expect sound .*: ok' "$EL" &&
    grep -aq "loopmenu .*sub 'Am' " "$EL" && has '^loopedit: knob 1 note length 4 ok' "$EL" && has '^loopedit: step 2/16 notes 1 long 4 len 384' "$EL" &&
    [ "$(lf "$(e 4)" len)" = 384 ] && [ "$(lf "$(e 4)" events)" = 3 ] && [ "$(l2 "$EL" 4 long)" = 4 ] && has '^loopedit: step 3/16 notes 1 long 1 len 384' "$EL" &&
    ok "PRESETS: step 2; a chord played (Am) replaces the step's notes and sounds; KNOB 1: its NOTE 4 steps long: nothing moves (G still on step 3, one long; the loop still 384): $OUT/cr_le_2.ppm, cr_le_3.ppm" \
    || bad "replace / note length: $(e 4) / $(grep -a '^loopedit: knob 1' "$EL" | tr '\n' ' ')"
differ cr_le_2 cr_le_3 "the long note is drawn (the steps it sounds on into)"
has '^loopedit: clear step 3 (1 events)' "$EL" && [ "$(lf "$(e 5)" events)" = 2 ] && [ "$(lf "$(e 5)" cap)" = 0 ] && [ "$(lf "$(e 5)" len)" = 384 ] &&
    has '^loopedit: step 3/16 notes 0 long 0 len 384' "$EL" &&
    ok "REC in the editor clears the selected step (no recording: no capture opened); the step stays, empty: $OUT/cr_le_4.ppm" || bad "REC in the editor: $(e 5)"
has '^loopedit: knob 2 loop length 48 ok' "$EL" && [ "$(lf "$(e 6)" len)" = 48 ] && [ "$(l2 "$EL" 6 ext)" = 384 ] &&
    has '^loopedit: knob 2 loop length 72 ok' "$EL" && has '^loopedit: step 2/2 ' "$EL" &&
    ok "KNOB 2: the loop 2 steps (48; the rest kept behind the end), then 3 again (72), then 2" || bad "loop length: $(e 6) / $(grep -a '^loopedit: knob 2' "$EL" | tr '\n' ' ')"
has '^loopedit: knob 4 velocity 92' "$EL" && [ "$(lf "$(e 7)" state)" = 2 ] && [ "$(l2 "$EL" 7 edit)" = 1 ] && [ "$(lf "$(e 7)" slot)" = 1 ] &&
    [ "$(lf "$(e 7)" mode)" = 4 ] && [ "$(lf "$(e 7)" keys)" = 0 ] &&
    ok "PRESETS wraps to step 1; KNOB 4: its velocity (100 -> 92); LOOP plays in the editor; a GLO tap does nothing there (slot 1, the mode, the keys as they were)" \
    || bad "velocity / play / the other knobs: $(e 7) / $(grep -a '^loopedit: knob 4' "$EL")"
[ "$(l2 "$EL" 8 edit) $(ly "$EL" 8)" = "0 8 0" ] && [ "$(lf "$(e 8)" events)" = 2 ] && [ "$(lf "$(e 8)" len)" = 48 ] &&
    [ "$(l2 "$EL" 9 edit) $(ly "$EL" 9)" = "1 8 0" ] && [ "$(grep -ac '^loopedit: octave' "$EL")" = 2 ] && [ "$(l2 "$EL" 10 edit) $(ly "$EL" 10)" = "0 8 0" ] && [ "$(l2 "$EL" 11 edit) $(ly "$EL" 11)" = "0 8 0" ] &&
    [ "$(l2 "$EL" 12 edit)" = 1 ] && [ "$(l2 "$EL" 13 edit) $(ly "$EL" 13)" = "0 8 0" ] && [ "$(grep -ac '^loopedit: close' "$EL")" -ge 5 ] &&
    ! has '^picker: open' "$EL" &&
    ok "EDIT tapped and EDIT held again each close the editor, back to the PLAY menu (no engine picker from EDIT held there); OCT+ / OCT- do not (the octave); the loop as edited (2 events, 48)" \
    || bad "closing the editor: $(l2 "$EL" 8 edit) $(ly "$EL" 8) / $(l2 "$EL" 9 edit) / $(l2 "$EL" 10 edit) $(ly "$EL" 10) / $(l2 "$EL" 11 edit) $(ly "$EL" 11) / $(l2 "$EL" 12 edit) / $(l2 "$EL" 13 edit)"
tl=$(lf "$(e 14)" len)
[ "$(lf "$(e 14)" slot)" = 2 ] && [ "$(lf "$(e 14)" events)" = 1 ] && [ "${tl:-0}" -gt 100 ] && [ $((tl % 24)) != 0 ] &&
    [ "$(l2 "$EL" 15 edit)" = 1 ] && [ "$(lf "$(e 15)" len)" = "$tl" ] && [ "$(lf "$(e 15)" events)" = 1 ] && [ "$(l2 "$EL" 15 dirty)" = "$(l2 "$EL" 14 dirty)" ] &&
    [ "$(l2 "$EL" 16 edit)" = 0 ] && [ "$(lf "$(e 16)" len)" = "$tl" ] && [ "$(lf "$(e 16)" events)" = 1 ] &&
    has "^loopedit: step 1/$(( (tl + 23) / 24 )) notes 1 long [0-9]* len $tl" "$EL" &&
    ok "a loop played free (slot 2, Quantize none, $tl ticks): EDIT opens it on the sixteenth grid ($(( (tl + 23) / 24 )) steps), nothing moved; closed: the take as it was: $OUT/cr_le_take.ppm" \
    || bad "a free-timed loop in the editor: $(e 14) / $(e 15) / $(e 16)"
[ "$(lf "$(e 17)" slot)" = 3 ] && [ "$(l2 "$EL" 17 edit)" = 1 ] && [ "$(lf "$(e 17)" len)" = 384 ] && [ "$(lf "$(e 17)" events)" = 0 ] &&
    has '^loopedit: step 1/16 notes 0 long 0 len 384' "$EL" &&
    [ "$(lf "$(e 18)" state)" = 0 ] && [ "$(lf "$(e 18)" len)" = 0 ] && [ "$(l2 "$EL" 18 dirty)" = 0 ] &&
    ok "an empty slot: EDIT opens an empty pattern of a bar (16 steps; 4 until the steps were sixteenths): $OUT/cr_le_empty.ppm; closed with nothing entered: the slot empty again, nothing to save" \
    || bad "the editor on an empty slot: $(e 17) / $(e 18)"
silent_end cr_loop_edit
# 0.18: the two faults of Step entry the owner reported (2026-10-12), as they reproduce: both fail on 6619937
run cr_loop_stepfaults
SF="$OUT/cr_loop_stepfaults.log"
f() { L cr_loop_stepfaults "$1"; }
[ "$(lf "$(f 1)" step)" = 6 ] && [ "$(lf "$(f 2)" events)" = 6 ] && [ "$(lf "$(f 2)" len)" = 144 ] &&
    ok "fault 1 (\"capped to 4 total steps\" with Quantize none): a step is a sixteenth whatever Quantize says: six chords, Length Free: six steps, 144 ticks (a quarter note each before: 576, four to the bar)" \
    || bad "Step, Free, Quantize none, six chords: $(f 1) / $(f 2)"
[ "$(lf "$(f 3)" step)" = 6 ] && [ "$(lf "$(f 3)" events)" = 6 ] && [ "$(l2 "$SF" 3 step-len)" = 384 ] && [ "$(lf "$(f 4)" len)" = 384 ] &&
    ok "fault 1: Length 1 bar, Quantize none: the bar has 16 steps: six chords, the cursor on step 7 (it wrapped after the 4th before: step 3): $OUT/cr_sf_bar.ppm" \
    || bad "Step, 1 bar, Quantize none: $(f 3) / $(f 4)"
[ "$(lf "$(f 5)" step)" = 5 ] && [ "$(lf "$(f 5)" events)" = 6 ] && [ "$(lf "$(f 6)" step)" = 6 ] && [ "$(lf "$(f 7)" events)" = 6 ] && [ "$(lf "$(f 7)" len)" = 144 ] &&
    ok "fault 2 (\"only records to the first step\", Quantize 1/16): the chord key held, six roots tapped: six steps, 144 ticks (all on step 1 before: 24): $OUT/cr_sf_held.ppm" \
    || bad "Step with the chord key held: $(f 5) / $(f 6) / $(f 7)"
[ "$(lf "$(f 8)" step)" = 3 ] && [ "$(lf "$(f 8)" events)" = 4 ] && [ "$(lf "$(f 9)" len)" = 96 ] && has 'expect sound .*: ok' "$SF" &&
    ok "fault 2: LOCK on, MAJ latched, four roots tapped: four steps, 96 ticks (one step before)" || bad "Step with LOCK latched: $(f 8) / $(f 9)"
[ "$(grep -ac '^loop: knob 3 rests (Step: 1/16)' "$SF")" = 1 ] && has '^loop: knob 3 quantize 1/16' "$SF" &&
    ok "Quantize while the record mode is Step: the cell reads 1/16, dim, KNOB 3 says \"Step: always 1/16\" and leaves the takes' Quantize as it was: $OUT/cr_sf_quant.ppm" \
    || bad "KNOB 3 in Step mode: $(grep -a '^loop: knob 3' "$SF" | tr '\n' ' ')"
[ "$(l2 "$SF" 10 step-len)" = 1536 ] && [ "$(lf "$(f 10)" cap)" = 6 ] && has '^loop: step entry: the Length cut to 64 steps' "$SF" &&
    ok "Length 8 bars in Step mode: a pattern is 64 steps at most: \"Step: 4 bars (64 steps)\", the cursor wraps there: $OUT/cr_sf_8bars.ppm" \
    || bad "Step with Length 8 bars: $(f 10) / step-len $(l2 "$SF" 10 step-len)"
silent_end cr_loop_stepfaults
# 0.18: the loop editor's pages (Step / Trig), the cursor on PRESETS, GLO + PRESETS, GLO + Steps, the octave keys, a
# step's part, chance, ratchet, condition and voicing, the ways out (the owner, 2026-10-13)
export EMU_UI_LOG=0
run cr_loop_trig
unset EMU_UI_LOG
TL="$OUT/cr_loop_trig.log"
t() { L cr_loop_trig "$1"; }
l3() { grep -a '^  loop3:' "$TL" | sed -n "$1p" | sed -n "s/.* $2 \(-*[0-9]*\).*/\1/p"; }
oc() { grep -a '^  cr: key' "$TL" | sed -n "$1p" | sed -n 's/.* octave \(-*[0-9]*\) .*/\1/p'; }
[ "$(l2 "$TL" 1 edit)" = 1 ] && [ "$(l3 1 epage)" = 1 ] && [ "$(l3 1 cursor)" = 1 ] && [ "$(lf "$(t 1)" events)" = 4 ] && [ "$(lf "$(t 1)" len)" = 96 ] &&
    has '^loop: cells Note len Steps Chance Velocity' "$TL" &&
    ok "the editor opens on page 1 \"Step\": Note len / Steps / Chance / Velocity, the cursor on step 1: $OUT/cr_lt_step.ppm" || bad "the editor's first page: $(t 1)"
[ "$(l3 2 cursor)" = 3 ] && [ "$(lf "$(t 2)" slot)" = 1 ] && ! has '^sound: part 0' "$TL" &&
    ok "PRESETS is the step cursor (step 3): never the slot, never the chord sound (no sound change in the whole script)" || bad "PRESETS in the editor: $(t 2)"
[ "$(lf "$(t 3)" len)" = 384 ] && [ "$(lf "$(t 4)" len)" = 1536 ] && has '^loopedit: snap 0 steps refused' "$TL" && [ "$(lf "$(t 5)" len)" = 1152 ] && [ "$(lf "$(t 6)" len)" = 384 ] &&
    [ "$(grep -ac '^loopedit: snap [0-9]* steps ok' "$TL")" = 8 ] &&
    ok "GLO held + KNOB 2 (Steps) snaps: 4 -> 8 -> 16 -> 32 -> 48 -> 64, no further; back: 48 -> 32 -> 16" || bad "the snap: $(grep -a '^loopedit: snap' "$TL" | tr '\n' ' ')"
[ "$(l3 7 cursor)" = 11 ] && [ "$(l3 8 cursor)" = 3 ] && [ "$(l3 9 cursor)" = 11 ] && [ "$(ly "$TL" 9)" = "8 0" ] && [ "$(lf "$(t 9)" keys)" = 0 ] &&
    ok "GLO held + PRESETS jumps 8 steps, wrapping at the loop's end (3 -> 11 -> 3; back: 11); GLO let go: no Options, the keys still play" \
    || bad "the jump: $(l3 7 cursor) $(l3 8 cursor) $(l3 9 cursor) / $(ly "$TL" 9)"
[ "$(l3 10 cursor)" = 1 ] && [ "$(l3 10 frac)" = 3 ] && [ "$(l3 11 frac)" = 1 ] && has '^loopedit: knob 1 note length 3/4 ok' "$TL" && has '^loopedit: knob 1 note length 1/4 ok' "$TL" &&
    ok "KNOB 1 below a step: 3/4, 1/2, 1/4 (the cell reads 1/4: $OUT/cr_lt_quarter.ppm)" || bad "the parts of a step: $(l3 10 frac) $(l3 11 frac)"
[ "$(l3 12 cursor)" = 3 ] && [ "$(l3 12 chance)" = 4 ] && [ "$(l3 12 props)" = 1 ] && [ "$(l3 13 chance)" = 0 ] && [ "$(l3 13 props)" = 0 ] &&
    ok "KNOB 3: the step's chance 50 % (the grid's notch: $OUT/cr_lt_chance.ppm), and 100 % again: no property left" || bad "chance: $(l3 12 chance) $(l3 12 props) / $(l3 13 chance) $(l3 13 props)"
differ cr_lt_quarter cr_lt_chance "the chance cell and the step's notch are drawn"
[ "$(l3 14 epage)" = 2 ] && [ "$(l3 14 cursor)" = 1 ] && [ "$(l3 14 ratchet)" = 2 ] && has '^loop: cells Ratchet Condition Voicing' "$TL" &&
    [ "$(l3 15 cursor)" = 2 ] && [ "$(l3 15 cond)" = 10 ] && [ "$(l3 16 voicing)" = 3 ] && [ "$(l3 17 voicing)" = 15 ] && [ "$(l3 17 props)" = 3 ] &&
    grep -aq "loopmenu .*item 'Trig' .*foot '2/2 " "$TL" && grep -aq "loopmenu .*item 'Step' .*foot '1/2 " "$TL" &&
    ok "SELECT: page 2 \"Trig\": KNOB 1 ratchet x3 (step 1), KNOB 2 condition 1st (step 2), KNOB 3 voicing -10 and ALGORITHM on to +2 (step 3, the same value): $OUT/cr_lt_trig.ppm" \
    || bad "the Trig page: $(l3 14 epage) $(l3 14 ratchet) / $(l3 15 cond) / $(l3 16 voicing) $(l3 17 voicing)"
[ "$(l3 18 epage)" = 2 ] && [ "$(l3 19 epage)" = 1 ] && [ "$(l3 19 voicing)" = 14 ] && grep -aq "loopmenu .*msg 'step voicing: +1'" "$TL" &&
    ok "SELECT is clamped (two pages); ALGORITHM is the step's voicing on page 1 too (\"step voicing: +1\")" || bad "SELECT / ALGORITHM: $(l3 18 epage) $(l3 19 epage) $(l3 19 voicing)"
[ "$(oc 20)" = 1 ] && [ "$(l2 "$TL" 20 edit)" = 1 ] && [ "$(oc 21)" = 0 ] && [ "$(l2 "$TL" 21 edit)" = 1 ] && has '^loopedit: step 4/16 .* root 74$' "$TL" &&
    ok "OCT+ / OCT- change the octave and leave the editor open; a chord entered at +1 is written an octave up (D: root 74)" || bad "the octave keys: $(oc 20) $(l2 "$TL" 20 edit) / $(oc 21)"
[ "$(lf "$(t 22)" played)" = 6 ] && [ "$(lf "$(t 23)" played)" = 11 ] && [ "$(l3 23 pass)" = 1 ] && [ "$(lf "$(t 24)" state)" = 1 ] && [ "$(l3 24 loopv)" = 0 ] &&
    ok "played: the first pass 6 hits (step 1 three times, 2 3 4 once), the second 5 (step 2 is 1st only); stopped: no loop voice kept" \
    || bad "ratchet / condition played: $(lf "$(t 22)" played) $(lf "$(t 23)" played) loopv $(l3 24 loopv)"
[ "$(lf "$(t 25)" events)" = 3 ] && [ "$(l3 25 props)" = 2 ] && [ "$(l3 25 ratchet)" = -1 ] &&
    ok "REC clears the step: its notes and its ratchet (an empty step has none)" || bad "REC on a ratcheted step: $(t 25) / props $(l3 25 props)"
[ "$(l2 "$TL" 26 edit) $(ly "$TL" 26)" = "0 8 0" ] && has '^loop: auto-save slot 1 46 bytes rc 0' "$TL" && [ "$(lf "$(t 27)" events)" = 3 ] && [ "$(l3 27 props)" = 2 ] &&
    [ "$(l2 "$TL" 28 edit)" = 1 ] && [ "$(l3 28 cond)" = 10 ] && [ "$(l2 "$TL" 29 edit) $(ly "$TL" 29)" = "0 8 0" ] &&
    ok "EDIT tapped closes it (the PLAY menu); the loop to its slot (46 bytes: 3 events + 2 steps' entries) and back from flash: the properties kept; HOME closes it too" \
    || bad "closing / the record: $(l2 "$TL" 26 edit) $(ly "$TL" 26) / $(t 27) props $(l3 27 props) / $(l3 28 cond) / $(l2 "$TL" 29 edit) $(ly "$TL" 29)"
silent_end cr_loop_trig
# 0.18 (2026-10-11): the loop editor while its loop plays: only the keys played go into the step (a latched chord is
# written when its keys go up; on 86d162f step 3 stays empty at dump 3 and step 7 gets the chord still sounding at dump
# 6), the name line is the selected step's (on 86d162f it reads the latched chord between the loop's notes: dump 4),
# the playing name at the left of the page's name; Transpose (Trig, KNOB 4); GLO + the voicing: Follow / 0
export EMU_UI_LOG=0
run cr_loop_edplay
unset EMU_UI_LOG
EP="$OUT/cr_loop_edplay.log"
p() { L cr_loop_edplay "$1"; }
l4() { grep -a '^  loop4:' "$EP" | sed -n "$1p" | sed -n 's/.*step-events \{0,1\}\(.*\) hit.*/\1/p'; }   # "72/20 count 1"
l3p() { grep -a '^  loop3:' "$EP" | sed -n "$1p" | sed -n "s/.* $2 \(-*[0-9]*\).*/\1/p"; }
esg() { awk -v n="$1" '/^fm1_in:/ { d++ } d == n - 1 && /^ui: frame/' "$EP"; }   # the frames up to dump N
subs() { esg "$1" | sed -n "s/.* sub '\([^']*\)' right.*/[\1]/p" | sort -u | tr -d '\n'; }   # the name line's texts seen
nows() { esg "$1" | sed -n "s/.* now '\([^']*\)' hot.*/[\1]/p" | sort -u | tr -d '\n'; }      # the playing names seen
[ "$(l4 1)" = "69/30 count 1" ] && [ "$(lf "$(p 1)" events)" = 4 ] && [ "$(nows 1)" = "[]" ] && esg 1 | tail -1 | grep -aq "sub 'AM' " &&
    ok "stopped: D E G A (major triads) entered; the name line is the selected step's (AM: a major triad reads with its M); no playing name" || bad "the editor stopped: $(l4 1) / now $(nows 1) / $(subs 1)"
[ "$(l4 2)" = "72/20 count 1" ] && [ "$(lf "$(p 2)" events)" = 5 ] && [ "$(lf "$(p 2)" state)" = 2 ] &&
    ok "the loop playing, MIN + C5 held 700 ms into step 15: Cm alone in it (none of the loop's own chords)" || bad "keys into a step while it plays: $(l4 2), $(lf "$(p 2)" events) events"
[ "$(l4 3)" = "65/30 count 1" ] && [ "$(lf "$(p 3)" events)" = 6 ] &&
    ok "LATCH on: F played into step 3 is in it when its keys go up (it sounds on, latched)" || bad "LATCH: step 3 after the keys went up: '$(l4 3)'"
[ "$(subs 4)" = "[-]" ] && [ "$(l4 4)" = "count 0" ] &&
    ok "the latched F sounding over the loop, no key down, an empty step selected: the name line reads '-' in every frame (the selected step)" \
    || bad "the name line while a latched chord sounds: $(subs 4)"
esg 5 | tail -1 | grep -aq "sub 'Bm' " && [ "$(l4 5)" = "count 0" ] &&
    ok "MIN + B4 down: the name line reads Bm, the chord being played (not yet in the step)" || bad "the name line with a key down: $(subs 5)"
[ "$(l4 6)" = "71/20 count 1" ] && [ "$(l4 7)" = "71/20 count 1" ] && [ "$(lf "$(p 7)" events)" = 7 ] && [ "$(subs 7)" = "[Bm]" ] &&
    ok "let go: step 7 is Bm alone (not the F that was still sounding); LATCH off: nothing more written" || bad "LATCH: step 7: '$(l4 6)' / '$(l4 7)', $(lf "$(p 7)" events) events"
case "$(nows 8)" in *"[]"*) bad "the playing name is empty in a frame while the loop plays: $(nows 8)";; *"[AM]"*"[DM]"*"[EM]"*"[GM]"*)
    ok "playing, page Step: the playing name at the left of the page's name: $(nows 8), never empty: $OUT/cr_le_now_step.ppm";; *) bad "the playing names: $(nows 8)";; esac
case "$(nows 9)" in *"[]"*) bad "page Trig: the playing name is empty in a frame: $(nows 9)";; *"[AM]"*"[DM]"*"[EM]"*"[GM]"*)
    esg 9 | tail -1 | grep -aq "item 'Trig' " && ok "playing, page Trig: the same names: $OUT/cr_le_now_trig.ppm" || bad "page Trig: $(esg 9 | tail -1 | cut -c1-120)";; *) bad "the playing names, Trig: $(nows 9)";; esac
[ "$(px_count "$OUT/cr_le_now_step.ppm" 6 62 84 80 ink)" -ge 20 ] && [ "$(px_count "$OUT/cr_le_now_step.ppm" 84 56 90 92 ink)" = 0 ] &&
    ok "the playing name is drawn at x 6 .. 83 and leaves 6 px clear before the page's name" || bad "the playing name's pixels: $(px_count "$OUT/cr_le_now_step.ppm" 6 62 84 80 ink) / $(px_count "$OUT/cr_le_now_step.ppm" 84 56 90 92 ink)"
esg 10 | tail -1 | grep -aq " now '' " && [ "$(lf "$(p 10)" state)" = 1 ] && ok "stopped: no playing name" || bad "stopped: the playing name stays: $(nows 10)"
[ "$(l3p 11 epage) $(l3p 11 cursor) $(l4 11)" = "2 1 62/30 count 1" ] && [ "$(l4 12)" = "63/30 count 1" ] && [ "$(l4 13)" = "60/30 count 1" ] &&
    has '^loop: cells Ratchet Condition Voicing Transpose' "$EP" && grep -aq "loopmenu .*hot 1\.3" "$EP" && esg 12 | tail -1 | grep -aq "sub 'D#M' " && esg 13 | tail -1 | grep -aq "sub 'CM' " &&
    ok "Trig, KNOB 4 = Transpose: step 1's DM a semitone up (D#M), three down (CM): the name line follows, the cell hot: $OUT/cr_le_tr_hot.ppm, $OUT/cr_le_tr.ppm" \
    || bad "Transpose: $(l3p 11 epage) $(l3p 11 cursor) '$(l4 11)' '$(l4 12)' '$(l4 13)'"
differ cr_le_tr_hot cr_le_tr "the Transpose cell: the turn's sum while hot, then the root note"
[ "$(l4 14)" = "count 0" ] && has '^loopedit: transpose: the step is empty' "$EP" && [ "$(lf "$(p 14)" events)" = 7 ] &&
    ok "Transpose on an empty step: nothing" || bad "Transpose, an empty step: $(l4 14)"
[ "$(l4 15)" = "127/30 count 1" ] && has '^loopedit: transpose 67 ok' "$EP" && [ "$(l4 16)" = "127/30 count 1" ] && has '^loopedit: transpose 1 refused' "$EP" &&
    grep -aq "msg 'no higher'" "$EP" && [ "$(l4 17)" = "60/30 count 1" ] &&
    ok "Transpose stops at the range's end: +70 in one turn goes as far as 127, one more is refused (\"no higher\", no wrap); -67: C again" \
    || bad "Transpose's range: '$(l4 15)' '$(l4 16)' '$(l4 17)'"
[ "$(l3p 18 voicing) $(l3p 19 voicing) $(l3p 20 voicing)" = "13 0 13" ] && [ "$(l3p 21 voicing) $(l3p 22 voicing)" = "16 0" ] && [ "$(l3p 23 voicing) $(l3p 24 voicing)" = "5 13" ] &&
    [ "$(l3p 25 voicing) $(l3p 25 epage)" = "0 1" ] && [ "$(grep -ac '^loopedit: voicing snap [0-9]* ok' "$EP")" = 6 ] && grep -aq "msg 'step voicing: Follow'" "$EP" && ! grep -aq 'bass level' "$EP" &&
    ok "GLO held + the voicing (KNOB 3, ALGORITHM): Follow <-> 0 either way; from +3 down: Follow; from -8 up (a turn of 2): 0, one snap; page 1: \"step voicing: Follow\"; no bass level" \
    || bad "the voicing's snap: $(l3p 18 voicing) $(l3p 19 voicing) $(l3p 20 voicing) / $(l3p 21 voicing) $(l3p 22 voicing) / $(l3p 23 voicing) $(l3p 24 voicing) / $(l3p 25 voicing)"
[ "$(l4 26)" = "76/20 count 1" ] && [ "$(l4 27)" = "77/20 count 1" ] && [ "$(lf "$(p 27)" events)" = 9 ] &&
    ok "LOCK with MIN latched: a root alone into a step is that one chord (Em, Fm)" || bad "LOCK latched in the editor: '$(l4 26)' '$(l4 27)'"
silent_end cr_loop_edplay
# 0.22: the loop editor's step preview (GitHub issue 8; the owner, 2026-10-15): the cursor moved onto a step while the
# loop is stopped plays the step once; an empty step is silence; while the loop plays nothing; a key played by hand,
# REC's clear and closing the editor end it; nothing is written, armed, counted or moved
run cr_loop_preview
PV="$OUT/cr_loop_preview.log"
v() { L cr_loop_preview "$1"; }
l3v() { grep -a '^  loop3:' "$PV" | sed -n "$1p" | sed -n "s/.* $2 \(-*[0-9]*\).*/\1/p"; }
l4v() { grep -a '^  loop4:' "$PV" | sed -n "$1p" | sed -n 's/.*step-events \{0,1\}\(.*\) hit.*/\1/p'; }
pbv() { grep -a 'parts busy' "$PV" | sed -n "$1p" | sed -n 's/.*parts busy \([0-9]*\).*/\1/p'; }
[ "$(lf "$(v 1)" events)" = 3 ] && [ "$(lf "$(v 1)" state)" = 1 ] && [ "$(l3v 2 cursor)" = 1 ] && [ "$(l3v 2 pre)" = 0 ] && [ "$(l3v 2 loopv)" = 0 ] &&
    [ "$(lf "$(v 2)" len)" = 384 ] && [ "$(grep -a '^loopedit: preview' "$PV" | head -1)" = "loopedit: preview step 2 notes 1 stopped" ] &&
    ok "D Em G entered by steps, the editor opened on them (16 steps), the cursor on step 1: nothing previewed on opening" || bad "the editor opened: $(v 2) / $(l3v 2 pre)"
[ "$(l3v 3 cursor)" = 2 ] && [ "$(l3v 3 pre)" = 1 ] && [ "$(l3v 3 loopv)" = 1 ] && has '^loopedit: preview step 2 notes 1 stopped' "$PV" &&
    [ "$(lf "$(v 3)" played)" = 0 ] && [ "$(lf "$(v 3)" state)" = 1 ] &&
    ok "PRESETS onto step 2 (Em) with the loop stopped: the step sounds on a loop voice (pre 1), not counted as played, the loop still stopped" \
    || bad "the preview: $(v 3) / $(grep -a '^  loop3:' "$PV" | sed -n 3p)"
[ "$(l3v 4 cursor)" = 1 ] && [ "$(l3v 4 pre)" = 1 ] && [ "$(l3v 4 loopv)" = 1 ] &&
    ok "back onto step 1 (D): heard, the Em's preview ended (one voice)" || bad "the second move: $(grep -a '^  loop3:' "$PV" | sed -n 4p)"
[ "$(l3v 5 cursor)" = 2 ] && [ "$(l3v 5 pre)" = 1 ] && [ "$(l3v 5 loopv)" = 1 ] && [ "$(lf "$(v 5)" events)" = 3 ] && [ "$(lf "$(v 5)" cap)" = 0 ] &&
    [ "$(lf "$(v 5)" ring)" = 0 ] && [ "$(lf "$(v 5)" played)" = 0 ] && [ "$(l2 "$PV" 5 dirty)" = "$(l2 "$PV" 4 dirty)" ] &&
    ok "step 2's note made 8 steps long: its preview sounds on at 500 ms, the step's own length; nothing recorded (3 events), REC not armed, the position at 0" \
    || bad "the long preview: $(v 5) / $(grep -a '^  loop3:' "$PV" | sed -n 5p)"
[ "$(l3v 6 cursor)" = 4 ] && [ "$(l3v 6 pre)" = 0 ] && [ "$(l3v 6 loopv)" = 0 ] && has '^loopedit: preview step 4 notes 0 stopped' "$PV" && [ "$(pbv 7)" = 0 ] &&
    ok "onto an empty step: the preview ends at once; 3 s on no part is busy (silence)" || bad "the empty step: $(grep -a '^  loop3:' "$PV" | sed -n 6p) / parts busy $(pbv 7)"
[ "$(lf "$(v 8)" state)" = 2 ] && [ "$(l3v 8 cursor)" = 2 ] && [ "$(l3v 8 pre)" = 0 ] && has '^loopedit: preview step 2 notes 1 playing' "$PV" &&
    ok "while the loop plays PRESETS moves the cursor onto a step with notes and previews nothing (the loop is heard)" || bad "playing: $(v 8) / $(grep -a '^  loop3:' "$PV" | sed -n 8p)"
[ "$(lf "$(v 9)" state)" = 1 ] && [ "$(l3v 9 loopv)" = 0 ] && [ "$(l3v 9 pre)" = 0 ] && ok "stopped: no loop voice kept" || bad "stopped: $(v 9)"
[ "$(l3v 10 cursor)" = 2 ] && [ "$(l3v 10 pre)" = 0 ] && [ "${pb10:=$(pbv 10)}" -ge 1 ] &&
    ok "a chord played by hand over the preview ends it (pre 0; the key's own voice sounds: parts busy $pb10)" || bad "a key over the preview: $(grep -a '^  loop3:' "$PV" | sed -n 10p) / parts busy $(pbv 10)"
[ "$(l4v 11)" = "69/30 count 1" ] && [ "$(lf "$(v 11)" events)" = 3 ] &&
    ok "the chord played went into the step as ever (step 2: A, 69/30), the preview did not" || bad "the step after the key: $(l4v 11) / $(v 11)"
[ "$(l3v 12 cursor)" = 1 ] && [ "$(l3v 12 pre)" = 0 ] && [ "$(lf "$(v 12)" events)" = 2 ] && has '^loopedit: clear step 1 (1 events)' "$PV" &&
    ok "REC clears step 1 and ends its preview" || bad "REC over the preview: $(grep -a '^  loop3:' "$PV" | sed -n 12p) / $(v 12)"
[ "$(l2 "$PV" 13 edit)" = 0 ] && [ "$(lf "$(v 13)" events)" = 2 ] && [ "$(lf "$(v 13)" cap)" = 0 ] && [ "$(lf "$(v 13)" state)" = 1 ] &&
    ok "closed: the loop as edited by hand alone (2 events), no capture opened by any of it" || bad "closed: $(v 13)"
silent_end cr_loop_preview
# 0.18: "sometimes in the play menu turning the preset knob ends up changing the preset": LOOP held again past the hold
# time closed the menu under the hand; fails on 722fe71 (dump 2: layer 0, slot 2, "sound: part 0 pos 2")
run cr_loop_presetknob
PK="$OUT/cr_loop_presetknob.log"
k() { L cr_loop_presetknob "$1"; }
[ "$(lf "$(k 1)" slot) $(ly "$PK" 1)" = "2 8 0" ] && [ "$(lf "$(k 2)" slot) $(ly "$PK" 2)" = "3 8 0" ] && [ "$(lf "$(k 3)" slot) $(ly "$PK" 3)" = "4 8 0" ] &&
    ok "LOOP held + PRESETS is the slot whenever the knob is turned: opening the menu (slot 2), the menu open and LOOP held 600 ms (slot 3, the menu open again), turned early (slot 4)" \
    || bad "LOOP held + PRESETS: $(lf "$(k 1)" slot) $(ly "$PK" 1) / $(lf "$(k 2)" slot) $(ly "$PK" 2) / $(lf "$(k 3)" slot) $(ly "$PK" 3)"
[ "$(l2 "$PK" 4 edit)" = 1 ] && [ "$(l2 "$PK" 5 edit)" = 0 ] && [ "$(lf "$(k 5)" slot) $(ly "$PK" 5)" = "4 8 0" ] && [ "$(lf "$(k 6)" slot)" = 5 ] &&
    ok "the loop editor closed by EDIT held: PRESETS with EDIT still down does nothing (slot 4 stays; it switched the slot before); EDIT up: the slot again (5)" \
    || bad "EDIT held + PRESETS: $(l2 "$PK" 5 edit) $(lf "$(k 5)" slot) / $(lf "$(k 6)" slot)"
[ "$(ly "$PK" 7)" = "0 0" ] && [ "$(grep -ac '^sound: part 0 pos' "$PK")" = 1 ] && [ "$(lf "$(k 8)" slot)" = 5 ] &&
    ok "LOOP held again with no knob still closes the menu; the one sound change of the script is PRESETS on the view afterwards" \
    || bad "the preset knob: layer $(ly "$PK" 7), $(grep -ac '^sound: part 0 pos' "$PK") sound change(s)"
# 0.18: SEQ is the loop editor's key (the view -> the editor -> the PLAY menu -> the view); the metronome: the PLAY
# menu's page 2 (KNOB 2), GLO + SEQ
export EMU_UI_LOG=0
run cr_seq
unset EMU_UI_LOG
SQ="$OUT/cr_seq.log"
q() { L cr_seq "$1"; }
sq() { echo "$(ly "$SQ" "$1") $(l2 "$SQ" "$1" edit) $(lf "$(q "$1")" metro)"; }   # layer options edit metro
[ "$(sq 1)" = "8 0 1 0" ] && [ "$(sq 2)" = "8 0 0 0" ] && [ "$(sq 3)" = "0 0 0 0" ] && [ "$(grep -ac '^loopedit: from the view' "$SQ")" = 3 ] &&
    has 'expect led SEQ on .*: ok' "$SQ" && [ "$(grep -ac 'expect led SEQ dim .*: ok' "$SQ")" = 2 ] && has 'expect led OCT- dim .*: ok' "$SQ" &&
    grep -aq "loopmenu .*item 'Step' .*foot '1/2 . PRESETS: step . REC: clear . SEQ: back'" "$SQ" &&
    ok "SEQ's cycle: the view -> the loop editor (its bottom line 'SEQ: back') -> the PLAY menu (SEQ's LED lit) -> the view (dim): $OUT/cr_seq_editor.ppm" \
    || bad "SEQ's cycle: $(sq 1) / $(sq 2) / $(sq 3), $(grep -ac '^loopedit: from the view' "$SQ") openings from the view"
[ "$(sq 4)" = "8 0 0 0" ] && [ "$(sq 5)" = "0 0 0 0" ] && [ "$(grep -a '^  cr: key' "$SQ" | sed -n 5p | sed -n 's/.* view \([0-9]*\) .*/\1/p')" = 0 ] &&
    ok "the editor opened by SEQ, EDIT: the PLAY menu; EDIT there: the editor; HOME: the view (it does not step the view)" || bad "SEQ then EDIT / HOME: $(sq 4) / $(sq 5)"
[ "$(sq 6)" = "0 0 0 0" ] && [ "$(sq 7)" = "8 0 0 0" ] &&
    ok "the PLAY menu entered by LOOP held: SEQ leaves to the view; the menu > EDIT, then SEQ: back in the PLAY menu" || bad "SEQ in the PLAY menu: $(sq 6) / $(sq 7)"
has '^loop: cells Level Metronome - -' "$SQ" && [ "$(sq 8)" = "8 0 0 1" ] && [ "$(sq 9)" = "8 0 0 0" ] && grep -aq "loopmenu .*hot 1\.1" "$SQ" &&
    ok "the PLAY menu's page 2, KNOB 2: Metronome On / Off, its cell hot: $OUT/cr_seq_p2_on.ppm" || bad "the metronome cell: $(sq 8) / $(sq 9)"
differ cr_seq_p2_off cr_seq_p2_on "the Metronome cell shows On"
[ "$(sq 10)" = "0 0 0 1" ] && [ "$(sq 11)" = "0 0 0 0" ] && grep -aq "msg 'metronome on'" "$SQ" && grep -aq "msg 'metronome off'" "$SQ" &&
    [ "$(grep -ac '^metro: o' "$SQ")" = 2 ] &&
    ok "GLO held + SEQ: the metronome on / off with its message (no Options, no editor); SEQ's LED does not show the click; SEQ alone never toggled it (the 2 toggles of the script are these)" \
    || bad "GLO + SEQ: $(sq 10) / $(sq 11), $(grep -ac '^metro: o' "$SQ") toggles"
[ "$(sq 12)" = "3 0 0 0" ] && [ "$(sq 13)" = "3 0 0 0" ] && [ "$(sq 14)" = "0 1 0 0" ] && ! has '^fx: o' "$SQ" &&
    ok "SEQ in the FX layer and in the Options: nothing (the layer stays, no editor, no click)" || bad "SEQ in a layer: $(sq 12) / $(sq 13) / $(sq 14)"
[ "$(lf "$(q 15)" cap)" != 0 ] && [ "$(sq 15)" = "0 0 0 0" ] && has '^loopedit: open rc 1' "$SQ" && grep -aq "msg 'end the take first'" "$SQ" &&
    ok "SEQ with a take open: refused (end the take first), the view stays" || bad "SEQ during a capture: cap $(lf "$(q 15)" cap), $(sq 15)"
[ "$(sq 17)" = "7 0 0 0" ] && [ "$(sq 18)" = "8 0 0 0" ] && [ "$(lf "$(q 18)" slot)" = 1 ] && [ "$(sq 19)" = "8 0 1 0" ] &&
    [ "$(grep -a '^  loop3:' "$SQ" | sed -n 19p | sed -n 's/.* cursor \([0-9]*\) .*/\1/p')" = 2 ] &&
    ok "SEQ held: the METRO layer on the view, as before; in the editor it closes it to the PLAY menu and PRESETS with SEQ still down does nothing (slot 1); turned inside the hold: the step cursor (2), the editor stays" \
    || bad "SEQ held + PRESETS: $(sq 17) / $(sq 18) slot $(lf "$(q 18)" slot) / $(sq 19)"
[ "$(sq 20)" = "8 0 0 0" ] && [ "$(lf "$(q 20)" slot)" = 2 ] && [ "$(sq 21)" = "8 0 1 0" ] && [ "$(sq 22)" = "8 0 0 0" ] && [ "$(sq 23)" = "0 0 0 0" ] &&
    [ "$(grep -ac '^sound: part 0 pos' "$SQ")" = 0 ] &&
    ok "LOOP held in the editor: the PLAY menu (not the view), PRESETS with LOOP still down: its slot (2); a LOOP tap there: the editor stays; the sound never changed" \
    || bad "LOOP held in the editor: $(sq 20) slot $(lf "$(q 20)" slot) / $(sq 21) / $(sq 22) / $(sq 23), $(grep -ac '^sound: part 0 pos' "$SQ") sound change(s)"
# 0.18: GLO held + REC steps the record mode (the dump's mode: 0 Overwrite 1 Advance 2 Overdub 3 Replace 4 Step)
export EMU_UI_LOG=0
run cr_glo_rec
unset EMU_UI_LOG
GR="$OUT/cr_glo_rec.log"
gq() { L cr_glo_rec "$1"; }
gm() { lf "$(gq "$1")" mode; }
gc() { lf "$(gq "$1")" cap; }
gmsg() { grep -aq "msg 'Rec: $1'" "$GR"; }
[ "$(gm 1) $(gm 2) $(gm 3) $(gm 4) $(gm 5) $(gm 6)" = "0 1 2 3 4 0" ] && gmsg Advance && gmsg Overdub && gmsg Replace && gmsg Step && gmsg Overwrite &&
    [ "$(gc 2)$(gc 3)$(gc 4)$(gc 5)$(gc 6)" = 00000 ] && [ "$(lf "$(gq 6)" state)" = 0 ] && [ "$(ly "$GR" 6)" = "0 0" ] &&
    ok "GLO held + REC: Advance, Overdub, Replace, Step, Overwrite again (the wrap), each with its message; nothing armed, no take, no Options: $OUT/cr_glo_rec_msg.ppm" \
    || bad "GLO + REC's cycle: modes $(gm 1) $(gm 2) $(gm 3) $(gm 4) $(gm 5) $(gm 6), caps $(gc 2)$(gc 3)$(gc 4)$(gc 5)$(gc 6)"
[ "$(gm 7)" = 1 ] && [ "$(gm 8)" = 0 ] && [ "$(gc 8)" = 0 ] && [ "$(ly "$GR" 8)" = "0 0" ] &&
    ok "GLO held past the hold time, then REC: the same; four REC under one hold: four steps" || bad "GLO held long + REC: modes $(gm 7) $(gm 8)"
[ "$(gc 9)" = 1 ] && [ "$(gc 10)" = 0 ] && [ "$(gm 10)" = 0 ] && [ "$(grep -ac '^loop: rec: play to start' "$GR")" = 3 ] &&
    [ "$(grep -ac '^loop: cancelled' "$GR")" = 1 ] && [ "$(grep -ac '^loop: loop recorded' "$GR")" = 2 ] &&
    ok "REC alone: as ever (armed, REC again: cancelled); the script's 3 armings, 1 cancel and 2 commits are its 6 REC taps alone, none GLO + REC's" \
    || bad "REC alone: caps $(gc 9) $(gc 10), $(grep -ac '^loop: rec: play to start' "$GR") armings"
[ "$(ly "$GR" 11)" = "8 0" ] && [ "$(gm 11)" = 1 ] && [ "$(lf "$(gq 11)" keys)" = 0 ] && grep -aq "loopmenu .*item 'Advance'" "$GR" &&
    [ "$(ly "$GR" 12)" = "3 0" ] && [ "$(gm 12)" = 2 ] &&
    ok "in the PLAY menu (its picker follows: Advance; the keys' Play / Loops untouched) and in the FX layer: the same: $OUT/cr_glo_rec_menu.ppm" \
    || bad "GLO + REC in a layer: $(ly "$GR" 11) mode $(gm 11) keys $(lf "$(gq 11)" keys) / $(ly "$GR" 12) mode $(gm 12)"
[ "$(l2 "$GR" 13 edit)" = 1 ] && [ "$(gm 13)" = 3 ] && [ "$(l2 "$GR" 14 edit)" = 1 ] && [ "$(gm 14)" = 3 ] && [ "$(grep -ac '^loopedit: clear' "$GR")" = 1 ] &&
    ok "in the loop editor: the mode steps (Replace), no step cleared; REC alone there: its clear, as before (the script's one)" \
    || bad "GLO + REC in the loop editor: edit $(l2 "$GR" 13 edit) mode $(gm 13), $(grep -ac '^loopedit: clear' "$GR") clear(s)"
[ "$(gm 15)" = 3 ] && [ "$(gm 16)" = 0 ] && [ "$(grep -ac '^loop: mode .* (GLO + REC)' "$GR")" = 20 ] &&
    ok "in the sound editor (GLO is SHIFT there): nothing; the script's 20 steps are its 20 GLO + REC elsewhere" \
    || bad "GLO + REC in the sound editor: modes $(gm 15) $(gm 16), $(grep -ac '^loop: mode .* (GLO + REC)' "$GR") steps"
[ "$(gc 17)" = 3 ] && [ "$(gc 18)" = 3 ] && [ "$(gm 18)" = 1 ] && [ "$(gc 19)" = 0 ] && [ "$(lf "$(gq 19)" state)" = 2 ] && [ "$(lf "$(gq 19)" events)" = 1 ] &&
    [ "$(lf "$(gq 19)" slot)" = 1 ] && [ "$(gc 20)" = 3 ] && [ "$(gm 20)" = 1 ] && [ "$(gc 21)" = 0 ] && [ "$(grep -ac '^loop: mode Advance$' "$GR")" = 1 ] &&
    ok "during a take: taken as the PLAY menu's ALGORITHM knob is (the take goes on, the mode set for the next REC; REC ends the take as it began: a loop playing on slot 1, no Advance jump)" \
    || bad "GLO + REC during a take: caps $(gc 17) $(gc 18) $(gc 19), mode $(gm 18); the knob: cap $(gc 20) mode $(gm 20)"
silent_end cr_glo_rec
# 0.18: the sound read-out on the home pages: the engine over the number, "Hold OPT to change engine" under the stripes
export EMU_UI_LOG=0
run cr_pool_hint
unset EMU_UI_LOG
PH="$OUT/cr_pool_hint.log"
phm() { grep -a '^ui: frame.* meter ' "$PH" | grep -aq "value '$1' .*sub '$2' .*label '$3' over '$4'"; }
phm 02 'FM BELL' 'Hold OPT to change engine' FM6 && phm 00 INIT 'Hold OPT to change engine' FM6 &&
    has "^home read-out: part 0 over 'FM6' line 'Hold OPT to change engine'$" "$PH" && has '^popup: part 0 00 / INIT / FM6 · 00/25$' "$PH" &&
    ok "PRESETS on the view: FM6 over the number, 'Hold OPT to change engine' under the stripes (02 FM BELL, 00 INIT): $OUT/cr_pool_hint_chord.ppm $OUT/cr_pool_hint_init.ppm" \
    || bad "the home read-out: $(grep -a '^popup:' "$PH" | head -2 | tr '\n' ' ')"
has '^engine pick: FM TONE' "$PH" && has '^sound: part 0 engine FM TONE pos 1 EP' "$PH" && phm 01 EP 'Hold OPT to change engine' 'FM TONE' &&
    grep -a 'part 0: ' "$PH" | head -1 | grep -q 'part 0: FM TONE /' &&
    ok "the text is true: OPT held + PRESETS, OPT let go: the chord part on FM TONE, its read-out names it: $OUT/cr_pool_hint_engine.ppm" || bad "OPT + PRESETS from the view"
phm 01 'LUSH PAD' 'Hold OPT to change engine' VA &&
    ok "ALGORITHM: the bass part's read-out: its engine over the number, 'Hold OPT to change engine' under the stripes as the chord part's: $OUT/cr_pool_hint_bass.ppm" || bad "the bass read-out"
phm 02 BELL 'FM TONE . 02/16' '' && [ "$(grep -ac "^home read-out: part . over " "$PH")" = 6 ] &&
    ok "the sound editor's picker (no OPT + PRESETS there): the engine and the place as before, nothing over the number: $OUT/cr_pool_hint_picker.ppm" \
    || bad "the picker's read-out: $(grep -ac "^home read-out: part . over " "$PH") home read-outs of 6"
has '^save: new part 0 FM TONE 17 (slot U001)' "$PH" && phm 17 BELL 'Hold OPT to change engine' 'FM TONE' &&
    ok "a user's preset (slot U001, FM TONE 17): the same two lines: $OUT/cr_pool_hint_user.ppm" || bad "a user preset's read-out"
differ cr_pool_hint_anim cr_pool_hint_chord "the read-out while its stripes fill differs from the settled one (both carry the two lines)"
# 0.18: OPT + ALGORITHM is the bass part's engine (as OPT + PRESETS the chord part's), BASS held + ALGORITHM the bass level
export EMU_UI_LOG=1
run cr_bass_engine
unset EMU_UI_LOG
BE="$OUT/cr_bass_engine.log"
bp() { grep -a "^  part $1: " "$BE" | sed -n "$2p" | sed -n 's/^  part [01]: \(.*\), voices.* level \([0-9]*\),.*/\1 \2/p'; }   # "VA / LUSH PAD 92"
bc() { grep -a '^  cr: key' "$BE" | sed -n "$1p" | sed -n 's/.* bass \([0-9]\) (sound \([0-9]*\)).* layer \([0-9]*\) .*/\1 \2 \3/p'; }   # "on sound layer"
bem() { grep -a '^ui: frame.* meter ' "$BE" | grep -aq "value '$1' .*sub '$2' .*label '$3' over '$4'"; }
[ "$(bp 1 1)" = "VA / LUSH PAD 92" ] && bem 01 'LUSH PAD' 'Hold OPT to change engine' VA && ! grep -aq 'hold EDIT' "$BE" &&
    ok "the bass part's home read-out: VA over 01 LUSH PAD, 'Hold OPT to change engine' (the old 'BASS + EDIT, hold EDIT' line is gone): $OUT/cr_bass_engine_popup.ppm" \
    || bad "the bass read-out's hint: $(bp 1 1)"
has '^engine pick: bass CZ-1 (64 presets)' "$BE" && has '^sound: part 1 engine CZ-1 pos 1 BRASS 1' "$BE" && [ "$(bp 1 2)" = "CZ-1 / BRASS 1 92" ] &&
    [ "$(bp 0 2)" = "FM6 / TINE EP 92" ] && bem 01 'BRASS 1' 'Hold OPT to change engine' CZ-1 && [ "$(bc 2)" = "1 2 0" ] &&
    grep -a '^ui: frame.* picker ' "$BE" | grep -aq "item 'CZ-1' value '64 presets'" &&
    ok "GLO held + ALGORITHM: the bass engine's picker, on GLO's release the bass part on CZ-1 at its first preset with the pool read-out; the chord part (FM6 TINE EP) and the bass level (92) as they were: $OUT/cr_bass_engine_pick.ppm $OUT/cr_bass_engine_new.ppm" \
    || bad "GLO + ALGORITHM: part 1 '$(bp 1 2)' part 0 '$(bp 0 2)' cr '$(bc 2)'"
[ "$(bp 1 3)" = "VA / LUSH PAD 92" ] && has '^sound: part 1 engine VA pos 1 LUSH PAD' "$BE" &&
    ok "GLO held + ALGORITHM back: VA at the place it had there (01 LUSH PAD)" || bad "GLO + ALGORITHM back: $(bp 1 3)"
[ "$(bp 0 4)" = "FM TONE / EP 92" ] && [ "$(bp 1 4)" = "VA / LUSH PAD 92" ] && [ "$(grep -ac '^engine pick: FM TONE' "$BE")" = 1 ] &&
    ok "GLO held + PRESETS still changes the chord part's engine alone (FM TONE EP; the bass part VA LUSH PAD)" || bad "GLO + PRESETS: $(bp 0 4) / $(bp 1 4)"
[ "$(bp 1 5)" = "VA / LUSH PAD 100" ] && [ "$(bp 0 5)" = "FM TONE / EP 92" ] && [ "$(bc 5)" = "1 2 0" ] && has '^bass: level 100 (BASS + ALGORITHM)$' "$BE" &&
    grep -a '^ui: frame.* meter ' "$BE" | grep -aq "value '100' .*label 'bass level'" &&
    ok "BASS held + ALGORITHM +3 (three detents in a frame: a turn's first two count one, the third quick, 6): the bass level 92 -> 100 with the 'bass level' meter; no layer, the bass still on (no tap), its engine and sound as they were: $OUT/cr_bass_engine_level.ppm" \
    || bad "BASS + ALGORITHM: $(bp 1 5) cr '$(bc 5)'"
[ "$(bp 1 6)" = "VA / LUSH PAD 98" ] && [ "$(bc 6)" = "1 2 4" ] && has '^bass: level 98 (BASS + ALGORITHM, layer)$' "$BE" &&
    ok "BASS held past the hold time (its layer open) + ALGORITHM -2 with BASS still down: the level in the layer (a reversal: one each, 100 -> 98), never the bass sound: $OUT/cr_bass_engine_layer.ppm" \
    || bad "BASS held past the hold time + ALGORITHM: $(bp 1 6) cr '$(bc 6)'"
[ "$(bp 1 7)" = "VA / WARM PAD 98" ] && [ "$(bc 7)" = "1 3 4" ] &&
    ok "the BASS layer open, BASS not held: ALGORITHM is the bass sound's as before (02 WARM PAD)" || bad "ALGORITHM in the BASS layer: $(bp 1 7)"
[ "$(bp 1 8)" = "VA / WARM PAD 99" ] && [ "$(bc 8)" = "1 3 4" ] && [ "$(grep -ac '^layer: close 4' "$BE")" -ge 1 ] &&
    ok "BASS held again past the hold time (its layer closes) + ALGORITHM with BASS still down: the layer opens again, the level 99 (a slow detent: 1), the sound not moved" \
    || bad "BASS held again + ALGORITHM: $(bp 1 8) cr '$(bc 8)'"
[ "$(lf "$(L cr_bass_engine 9)" mode)" = 1 ] && [ "$(bp 1 9)" = "VA / WARM PAD 99" ] && [ "$(grep -ac '^engine pick: bass' "$BE")" = 3 ] &&
    ok "the PLAY menu: GLO + ALGORITHM is the record mode (ALGORITHM's own there: Advance), no bass engine picker, no level change" \
    || bad "GLO + ALGORITHM in the PLAY menu: mode $(lf "$(L cr_bass_engine 9)" mode) $(bp 1 9)"
[ "$(bp 1 10)" = "VA / WARM PAD 100" ] && [ "$(bc 10)" = "1 3 4" ] && has '^bass: behaviour ' "$BE" &&
    [ "$(grep -a '^  cr: key' "$BE" | sed -n 10p | sed -n 's/.* voicing \(-*[0-9]*\) .*/\1/p')" = 0 ] &&
    ok "BASS held + ALGORITHM, then KNOB 1 in the same hold: the level (100), then the BASS layer opens and KNOB 1 is its Behaviour (the chord's voicing not turned)" \
    || bad "BASS + ALGORITHM then KNOB 1: $(bp 1 10) cr '$(bc 10)'"
[ "$(bc 11 | cut -d' ' -f1)" = 0 ] && [ "$(bp 1 11)" = "CZ-1 / BRASS 1 100" ] &&
    ok "the bass off: GLO + ALGORITHM changes its engine (CZ-1 BRASS 1), the bass stays off" || bad "GLO + ALGORITHM with the bass off: $(bp 1 11) cr '$(bc 11)'"
# 0.18: a pattern of 64 steps: entered, refused past the 64th, played, edited
export EMU_UI_LOG=0
run cr_loop_step
unset EMU_UI_LOG
SL="$OUT/cr_loop_step.log"
g() { L cr_loop_step "$1"; }
[ "$(lf "$(g 1)" cap)" = 6 ] && [ "$(lf "$(g 2)" step)" = 4 ] && [ "$(lf "$(g 3)" step)" = 16 ] && [ "$(lf "$(g 3)" events)" = 16 ] &&
    ok "step entry: 4 chords: the cursor on step 5; 16: on step 17, the second row: $OUT/cr_st_1.ppm, cr_st_5.ppm, cr_st_17.ppm" || bad "steps 1 .. 17: $(g 2) / $(g 3)"
[ "$(lf "$(g 4)" events)" = 63 ] && [ "$(lf "$(g 4)" step)" = 62 ] && [ "$(lf "$(g 5)" step)" = 63 ] && [ "$(lf "$(g 6)" events)" = 64 ] &&
    [ "$(lf "$(g 6)" step)" = 64 ] && [ "$(l2 "$SL" 6 steps-used)" = 64 ] &&
    ok "47 roots under a held chord key: steps 17 .. 63, one each; the 64th: 64 events on 64 steps: $OUT/cr_st_64.ppm" || bad "steps 17 .. 64: $(g 4) / $(g 5) / $(g 6)"
[ "$(lf "$(g 7)" events)" = 64 ] && [ "$(l2 "$SL" 7 refused)" = 1 ] && has '^loop: step refused (64 steps)' "$SL" && [ "$(lf "$(g 8)" step)" = 64 ] &&
    grep -aq "loopmenu .*msg '64 steps: full'" "$SL" &&
    ok "a 65th chord: refused, \"64 steps: full\" (nothing written; a rest there: nothing): $OUT/cr_st_full.ppm" || bad "the 65th: $(g 7) / $(g 8)"
[ "$(lf "$(g 9)" events)" = 64 ] && [ "$(lf "$(g 9)" len)" = 1536 ] && [ "$(lf "$(g 9)" state)" = 1 ] && has 'expect sound .*: ok' "$SL" &&
    [ $(( $(lf "$(g 10)" played) - $(lf "$(g 9)" played) )) -ge 63 ] && [ $(( $(lf "$(g 10)" played) - $(lf "$(g 9)" played) )) -le 64 ] &&
    ok "REC: a loop of 64 events, 1536 ticks (4 bars); LOOP: the 64 steps in 8 s at 120 BPM ($(( $(lf "$(g 10)" played) - $(lf "$(g 9)" played) )) played in 7.9 s)" \
    || bad "the 64-step loop: $(g 9) / $(g 10)"
has '^loopedit: step 1/64 notes 1 long 1 len 1536' "$SL" && grep -aq "loopmenu .* lm 2/4/0/" "$SL" && has '^loopedit: knob 1 note length 16 ok' "$SL" &&
    has '^loopedit: step 21/64 notes 1 long 16 len 1536' "$SL" && [ "$(lf "$(g 12)" events)" = 64 ] && [ "$(lf "$(g 12)" len)" = 1536 ] && [ "$(l2 "$SL" 12 long)" = 16 ] &&
    ok "the editor on the 64 steps (four rows of 16: $OUT/cr_st_ed64.ppm); step 21's note 16 steps long: 64 events, 1536 ticks still: $OUT/cr_st_long.ppm" \
    || bad "the editor on 64 steps: $(g 12) / $(grep -a '^loopedit: knob' "$SL" | tr '\n' ' ')"
differ cr_st_ed64 cr_st_long "the selected step's long note is drawn over the steps it sounds on into"
ph=$(grep -a "loopmenu .* lm 2/4/0/" "$SL" | sed -n 's/.* lm 2\/4\/0\/\([0-9]*\) .*/\1/p' | awk '$1 >= 64 { print int($1 / 64) }' | sort -u | wc -l | tr -d ' ')
[ "${ph:-0}" -ge 25 ] && [ "$(lf "$(g 13)" state)" = 2 ] && [ "$(l2 "$SL" 13 edit)" = 1 ] &&
    ok "LOOP in the editor: the playhead walks the steps ($ph of them drawn as the step being played in 3.7 s): $OUT/cr_st_ph1.ppm, cr_st_ph2.ppm" \
    || bad "the editor's playhead: $ph steps shown playing"
differ cr_st_ph1 cr_st_ph2 "the playhead moved"
silent_end cr_loop_step
"$EMU" --headless --script "$S/cr_loop_free.txt" --wav "$OUT/cr_again/cr_loop_free.wav" >/dev/null 2>&1
cmp -s "$OUT/cr_loop_free.wav" "$OUT/cr_again/cr_loop_free.wav" && ok "the looper is deterministic (the same audio twice)" || bad "looper audio differs"

echo "the loop-length layer, the count-in, undo"
run cr_countin
has '^loop: length 1' "$OUT/cr_countin.log" && ok "LOOP held + KNOB 1 +1: the length (Free -> 1 bar), no meter: $OUT/cr_loop_length_1bar.ppm" \
    || bad "KNOB 1 in the PLAY menu"
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
[ "$(grep -c 'expect led .* dim .*: ok' "$OUT/cr_loop_glow.log")" = 6 ] && ! has 'expect.*FAILED' "$OUT/cr_loop_glow.log" &&
    ok "LEDs Stock: D4 F#4 A4 and D5 F#5 (every root of the loop's note names) dim while the loop sounds D, lit when it is silent: $OUT/cr_glow_playing.ppm" || bad "loop glow"
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
[ "$c" = "Dm,Dm6,Dm6 9,Dm6,DM,D (1 note)" ] && ok "MIN latched: Dm; 6: Dm6; 9: Dm6/9; 9 again: Dm6; MAJ: DM; LOCK off: one note ($c): $OUT/cr_lock_dm69.ppm" \
    || bad "LOCK chords: $c"
has '^lock: on' "$L" && has '^lock: off' "$L" && has '^lock: latched none' "$L" && ok "the trace: lock on / off, the latch cleared" || bad "lock trace"
silent_end cr_lock

echo "a layer locks open on a hold; OCT- / HOME close it, another hold switches"
run cr_layer_lock
L="$OUT/cr_layer_lock.log"
[ "$(grep -c '^expect .*: ok' "$L")" = 28 ] && ! has 'expect.*FAILED' "$L" &&
    ok "FX held, released: open (OCT- lit); OCT- closed it; BASS held: switched; HOME closed; KEY held, OCT-: closed" \
    || bad "cr_layer_lock: $(grep -c '^expect .*: ok' "$L") of 28 checks: $(grep -a 'expect.*FAILED' "$L" | head -3)"
[ "$(grep '^layer:' "$L" | sed -n 1,6p | tr '\n' ',')" = "layer: open 3 (locked),layer: close 3,layer: open 3 (locked),layer: open 4 (locked),layer: open 1 (locked),layer: close 1," ] &&
    ok "the trace: fx open, closed; fx, bass (switched); key open, closed" || bad "layer trace: $(grep '^layer:' "$L" | tr '\n' ' ')"
[ "$(grep '^layer:\|^fx: o\|^view: menu\|^picker: [kc]' "$L" | sed 1,7d | sed 's/^picker: \([a-z]*\) .*/picker: \1/; s/^view: menu.*/view: menu/' | tr '\n' ',')" = "layer: open 3 (locked),layer: close 3,layer: open 3 (locked),fx: on,layer: close 3,layer: open 1 (locked),layer: open 1 (locked),layer: close 1,layer: open 4 (locked),layer: close 4,layer: open 7 (locked),layer: close 7,layer: open 5 (locked),picker: keep,layer: close 5,view: menu,layer: close 9," ] &&
    ok "OCT+ closes the fx layer (no toggle); a FX tap in it still toggles; each layer's own button held again closes it (FX KEY BASS METRO, EDIT: the sound kept, HOME: the view menu); KEY held + a root in the open key layer: a combo, it stays" \
    || bad "the layers' own buttons: $(grep '^layer:\|^fx: o\|^view: menu\|^picker: [kc]' "$L" | sed 1,7d | tr '\n' ' ')"
grep 'octave' "$L" | grep -v 'octave 0' | grep -q . && bad "OCT- in a layer moved the octave" || ok "OCT- in a layer: back, the octave untouched"
grep -q 'fx_on\|layer 4' "$L" && ok "screens: $OUT/cr_lock_fx.ppm, cr_lock_bass.ppm, cr_lock_key.ppm" || bad "no bass layer in the dump"
differ cr_lock_fx cr_lock_bass "the bass layer replaced the fx layer"

echo "the LOCK layer (B3 held): Type key Clears / Keeps, Ext keys Toggle / Pushed; its open / close gestures"
run cr_lock_menu
L="$OUT/cr_lock_menu.log"
lk() { grep -a '^  lock: on' "$L" | sed -n "$1p" | sed -n 's/.*lock: on \([01]\) latched \(..\) type-key \([a-z]*\) ext-keys \([a-z]*\)$/\1 \2 \3 \4/p'; }
ll() { grep -a '^  cr: key' "$L" | sed -n "$1p" | sed -n 's/.* layer \([0-9]*\) .*/\1/p'; }
[ "$(lk 1)" = "0 00 clears toggle" ] && [ "$(ll 1)" = 10 ] && [ "$(lk 2)" = "1 00 clears toggle" ] && [ "$(ll 2)" = 10 ] && has '^lock: cells Type key Ext keys - -' "$L" &&
    ok "B3 held: the LOCK layer (Type key / Ext keys, the defaults Clears / Toggle), LOCK not toggled by the hold; a B3 tap in it: LOCK on: $OUT/cr_lock_menu.ppm" \
    || bad "the LOCK layer: $(lk 1) layer $(ll 1) / $(lk 2) layer $(ll 2)"
[ "$(lk 3)" = "1 94 clears toggle" ] && has 'chord D6 9 sounding 1' "$L" && has '^expect sound .*: ok' "$L" &&
    ok "the chord keys and the roots play in the layer: MAJ + 6 + 9 latched, D4: D6 9 sounds" || bad "playing in the LOCK layer: $(lk 3)"
[ "$(lk 4)" = "1 02 clears toggle" ] && [ "$(lk 5)" = "1 12 clears toggle" ] && [ "$(lk 6)" = "1 02 clears toggle" ] &&
    ok "Clears + Toggle (as before): MIN clears 6 and 9; 6 on; 6 again off" || bad "Clears + Toggle: $(lk 4) / $(lk 5) / $(lk 6)"
[ "$(lk 8)" = "1 94 keeps toggle" ] && [ "$(lk 9)" = "1 92 keeps toggle" ] && [ "$(lk 10)" = "1 12 keeps toggle" ] &&
    ok "Keeps + Toggle: MAJ -> MIN with 6 and 9 latched: they stay; 9 tapped: off: $OUT/cr_lock_menu_keeps.ppm" || bad "Keeps + Toggle: $(lk 8) / $(lk 9) / $(lk 10)"
[ "$(lk 12)" = "1 22 keeps pushed" ] && [ "$(lk 13)" = "1 22 keeps pushed" ] && [ "$(lk 14)" = "1 02 keeps pushed" ] && [ "$(lk 15)" = "1 22 keeps pushed" ] && [ "$(lk 16)" = "1 A2 keeps pushed" ] &&
    ok "Pushed, the owner's sequence: 6 on, 7: the set is 7; 7 again alone (the only one on): off at its release (on while held); 7: on; 7 held + 9: both: $OUT/cr_lock_menu_pushed.ppm" \
    || bad "Pushed: $(lk 12) / $(lk 13) / $(lk 14) / $(lk 15) / $(lk 16)"
[ "$(lk 17)" = "1 22 keeps pushed" ] && [ "$(lk 18)" = "1 82 keeps pushed" ] && [ "$(lk 19)" = "1 84 keeps pushed" ] &&
    ok "Pushed: 7 and 9 on, 7 alone: the set is 7 (a lone push clears only the one extension on); 9 alone: 9; Keeps: MAJ changes the type only" \
    || bad "Pushed, two on: $(lk 17) / $(lk 18) / $(lk 19)"
[ "$(lk 21)" = "1 92 clears pushed" ] && [ "$(lk 22)" = "1 04 clears pushed" ] &&
    ok "Clears + Pushed: 6 + 9 pushed; MAJ alone clears them" || bad "Clears + Pushed: $(lk 21) / $(lk 22)"
[ "$(ll 7)" = 0 ] && [ "$(ll 8)" = 10 ] && [ "$(ll 11)" = 0 ] && [ "$(ll 20)" = 0 ] && [ "$(lk 20)" = "1 82 keeps pushed" ] && [ "$(ll 23)" = 0 ] && [ "$(lk 24)" = "0 00 clears toggle" ] &&
    ok "OCT-, OCT+, HOME and B3 held again close the layer; a hold never toggles LOCK; a B3 tap outside: LOCK off, the latch cleared" \
    || bad "the LOCK layer's gestures: layers $(ll 7) $(ll 8) $(ll 11) $(ll 20) $(ll 23), $(lk 20) / $(lk 24)"
silent_end cr_lock_menu

# LOCK, the top row's lone repeat (the owner, 2026-10-11): the only type latched, pushed again alone, goes off at its
# release (single notes); the latched extensions stay, lit, and apply again with the next type
echo "LOCK: a type key pushed again goes off; the KEY layer's page 2; Key Mode's lights; the Full keyboard"
run cr_lock_type
L="$OUT/cr_lock_type.log"
lt() { grep -a '^  lock: on' "$L" | sed -n "$1p" | sed -n 's/.*lock: on \([01]\) latched \(..\) .*/\1 \2/p'; }
ch() { grep -a '^  cr: voices' "$L" | sed -n "$1p" | sed -n 's/.* chord \(.*\) sounding \([01]\) notes\(.*\)$/\1|\2|\3/p'; }
[ "$(lt 1)" = "1 44" ] && [ "$(ch 1)" = "DM7|1| 62 66 69 73" ] && [ "$(lt 2)" = "1 44" ] && [ "$(lt 3)" = "1 40" ] && [ "$(ch 3)" = "D|1| 62" ] &&
    [ "$(grep -c '^expect .*: ok' "$L")" = 2 ] && ! has 'expect.*FAILED' "$L" &&
    ok "MAJ + M7 latched: DM7; MAJ again alone: on while held, off at its release: D4 is a single note, M7 still latched and lit: $OUT/cr_lock_type_single.ppm" \
    || bad "the lone type repeat: $(lt 1) $(ch 1) / $(lt 2) / $(lt 3) $(ch 3)"
[ "$(lt 4)" = "1 44" ] && [ "$(ch 4)" = "DM7|1| 62 66 69 73" ] &&
    ok "MAJ again: the type back to the extension waiting there: DM7 (Clears: not cleared)" || bad "the type back: $(lt 4) $(ch 4)"
[ "$(lt 5)" = "1 54" ] && [ "$(lt 6)" = "1 02" ] && [ "$(lt 7)" = "1 00" ] && [ "$(ch 8)" = "E|1| 64" ] && [ "$(lt 9)" = "1 02" ] &&
    ok "MAJ held + 6: not a lone push, both stay; another type: MIN alone (Clears as before); MIN again: nothing latched, E4 a single note; MIN: on" \
    || bad "the group / the other type: $(lt 5) / $(lt 6) / $(lt 7) $(ch 8) / $(lt 9)"
[ "$(lt 10)" = "1 02" ] && [ "$(ch 10)" = "Em|1| 64 67 71" ] && [ "$(lt 11)" = "1 00" ] &&
    ok "MIN held with a root played in the push: Em while held, off at its release (as the extension rule: a root is not another chord key)" \
    || bad "the push with a root: $(lt 10) $(ch 10) / $(lt 11)"
silent_end cr_lock_type

# LOCK in the three play styles (the owner, 2026-10-12: Free + "Ext keys: Pushed" did not behave as Simple / Advanced
# and had to be "toggled off by pressing twice"): the latch is the same set in every style, the engine's keys down
# and the chord keys' LEDs are that set, and what sounds follows it: Simple the next root, Advanced / Free the roots
# held too, Free the last root again while a chord key is held (never after it). cr_lock_push.txt behind a preamble:
# Play Style x Type key (Clears / Keeps), Ext keys Pushed; cr_lock_tog.txt: Play Style, Ext keys Toggle.
# A dump's signature: latched/down/active/the voices' own type and extensions (f: Free's re-sounded chord)
echo "LOCK: Pushed and Toggle mean the same set in Simple, Advanced and Free"
lp_sig() {
    grep -a '^  lock: on\|^  mods:' "$1" | paste - - |
        sed -n 's/.*latched \(..\) .*mods: down \(..\) active \(..\) voices\(.*\) free [0-9]*$/\1\/\2\/\3\/\4/p' |
        sed 's/\/ /\//; s/ /+/g; s/\/$/\/-/' | tr '\n' ' ' | sed 's/ $//'
}
lp_latch() { lp_sig "$1" | tr ' ' '\n' | cut -d/ -f1 | tr '\n' ' '; }
lp_run() {   # lp_run NAME BODY STYLE PUSHED KEEPS: the preamble (the Classic chord keys, Options > Play Style, the LOCK layer on B3, LOCK on) + the body
    {
        printf 'keys classic\nwait 300\nbtn GLO\nwait 200\nknob KNOB1 +%s\nwait 200\nbtn GLO\nwait 200\nbtn B3 600\nwait 300\n' "$3"
        [ "$4" = 1 ] && printf 'knob KNOB2 +1\nwait 200\n'
        [ "$5" = 1 ] && printf 'knob KNOB1 +1\nwait 200\n'
        printf 'btn OCT-\nwait 300\nbtn B3\nwait 200\n'
        cat "$S/$2.txt"
    } >"$OUT/$1.txt"
    "$EMU" --headless --script "$OUT/$1.txt" >"$OUT/$1.log" 2>&1 || bad "$1: exit $?"
}
LP0='04/04/04/- 14/14/14/- 24/24/24/- 24/24/24/- 04/04/04/- 24/24/24/- 24/A4/A4/- A4/A4/A4/- 24/24/24/- 84/84/84/- 84/84/84/- 04/04/04/- 04/04/04/62:q3/x0 24/24/24/- 20/20/20/- 20/20/20/62:q0/x2 24/24/24/- 24/24/24/62:q3/x2 04/04/04/62:q3/x2 24/24/24/62:q3/x2 84/84/84/62:q3/x2 A4/A4/A4/62:q3/x2 24/24/24/62:q3/x2 20/20/20/62:q3/x2 24/24/24/62:q3/x2 24/24/24/- 00/00/00/-'
LP1='04/04/04/- 14/14/14/- 24/24/24/- 24/24/24/- 04/04/04/- 24/24/24/- 24/A4/A4/- A4/A4/A4/- 24/24/24/- 84/84/84/- 84/84/84/- 04/04/04/- 04/04/04/62:q3/x0 24/24/24/- 20/20/20/- 20/20/20/62:q0/x2 24/24/24/- 24/24/24/62:q3/x2 04/04/04/62:q3/x0 24/24/24/62:q3/x2 84/84/84/62:q3/x8 A4/A4/A4/62:q3/xA 24/24/24/62:q3/x2 20/20/20/62:q0/x2 24/24/24/62:q3/x2 24/24/24/- 00/00/00/-'
LP2='04/04/04/- 14/14/14/- 24/24/24/- 24/24/24/62:q3/x2f 04/04/04/- 24/24/24/- 24/A4/A4/62:q3/xAf A4/A4/A4/- 24/24/24/- 84/84/84/- 84/84/84/62:q3/x8f 04/04/04/- 04/04/04/62:q3/x0 24/24/24/62:q3/x2f 20/20/20/- 20/20/20/62:q0/x2 24/24/24/- 24/24/24/62:q3/x2 04/04/04/62:q3/x0 24/24/24/62:q3/x2 84/84/84/62:q3/x8 A4/A4/A4/62:q3/xA 24/24/24/62:q3/x2 20/20/20/62:q0/x2 24/24/24/62:q3/x2 24/24/24/- 00/00/00/-'
LT0='34/34/34/- 14/14/14/- 14/14/14/- 14/14/14/62:q3/x1 04/04/04/62:q3/x1 84/84/84/62:q3/x1 04/04/04/62:q3/x1 84/84/84/62:q3/x1 80/80/80/62:q3/x1 82/82/82/62:q3/x1 04/04/04/62:q3/x1 04/04/04/- 04/04/04/62:q3/x0 84/84/84/62:q3/x0 04/04/04/62:q3/x0 00/00/00/-'
LT1='34/34/34/- 14/14/14/- 14/14/14/- 14/14/14/62:q3/x1 04/04/04/62:q3/x0 84/84/84/62:q3/x8 04/04/04/62:q3/x0 84/84/84/62:q3/x8 80/80/80/62:q0/x8 82/82/82/62:q2/x8 04/04/04/62:q3/x0 04/04/04/- 04/04/04/62:q3/x0 84/84/84/62:q3/x8 04/04/04/62:q3/x0 00/00/00/-'
LT2='34/34/34/- 14/14/14/62:q3/x1f 14/14/14/- 14/14/14/62:q3/x1 04/04/04/62:q3/x0 84/84/84/62:q3/x8 04/04/04/62:q3/x0 84/84/84/62:q3/x8 80/80/80/62:q0/x8 82/82/82/62:q2/x8 04/04/04/62:q3/x0 04/04/04/- 04/04/04/62:q3/x0 84/84/84/62:q3/x8 04/04/04/62:q3/x0 00/00/00/-'
for ps in 0 1 2; do
    eval "want=\$LP$ps"
    for tk in 0 1; do
        n=cr_lock_push_${ps}${tk}
        lp_run $n cr_lock_push $ps 1 $tk
        got=$(lp_sig "$OUT/$n.log")
        st=$(sed -n 's/.* style \([0-9]*\) .*/\1/p' "$OUT/$n.log" | tail -1)
        grep -aq "lock: on 1 latched .. type-key $([ $tk = 1 ] && echo keeps || echo clears) ext-keys pushed" "$OUT/$n.log" && [ "$st" = $ps ] && [ "$got" = "$want" ] &&
            ok "Pushed, play style $ps, Type key $([ $tk = 1 ] && echo Keeps || echo Clears): 27 dumps: latch, engine, LEDs and voices as specified" \
            || bad "Pushed, play style $ps (got $st), type key $tk: $got"
        b=$(last_busy "$OUT/$n.log"); [ "${b:-x}" = 0 ] || bad "$n: ${b:-?} voices still sounding at the end"
    done
    eval "want=\$LT$ps"
    n=cr_lock_tog_$ps
    lp_run $n cr_lock_tog $ps 0 0
    got=$(lp_sig "$OUT/$n.log")
    no() { grep -a '^  cr: voices' "$OUT/$n.log" | sed -n "$1p" | sed -n 's/.* notes-out \([0-9]*\) .*/\1/p'; }
    [ "$got" = "$want" ] && grep -aq 'ext-keys toggle' "$OUT/$n.log" &&
        ok "Toggle, play style $ps: 16 dumps: a latched extension pressed once is off (the roots held follow in Advanced / Free)" \
        || bad "Toggle, play style $ps: $got"
    if [ $ps != 0 ]; then
        [ $(($(no 14) - $(no 13))) = 4 ] && [ $(($(no 15) - $(no 14))) = 3 ] &&
            ok "Extension Addition = Play Chord, play style $ps: the chord struck once per push (4 notes with the 9, 3 without)" \
            || bad "Play Chord, play style $ps: notes out $(no 13) $(no 14) $(no 15)"
    fi
    b=$(last_busy "$OUT/$n.log"); [ "${b:-x}" = 0 ] || bad "$n: ${b:-?} voices still sounding at the end"
done
[ "$(lp_latch "$OUT/cr_lock_push_00.log")" = "$(lp_latch "$OUT/cr_lock_push_10.log")" ] && [ "$(lp_latch "$OUT/cr_lock_push_00.log")" = "$(lp_latch "$OUT/cr_lock_push_20.log")" ] &&
    [ "$(lp_latch "$OUT/cr_lock_tog_0.log")" = "$(lp_latch "$OUT/cr_lock_tog_1.log")" ] && [ "$(lp_latch "$OUT/cr_lock_tog_0.log")" = "$(lp_latch "$OUT/cr_lock_tog_2.log")" ] &&
    ok "the latch after every gesture is the same in the three play styles (Pushed and Toggle)" || bad "the latch differs between play styles"

# Key Mode's lights (the key leds: line: 0 off 1 faint 2 dim 3 mid 4 lit; fm1_led_faint < fm1_led_dim < fm1_led_dim2 < fm1_led)
run cr_key_leds
L="$OUT/cr_key_leds.log"
kl() { grep -a '^  key leds:' "$L" | sed -n "$1p" | sed -n 's/.*key leds:\([0-9]*\) \([0-9]*\) .*/\2/p'; }
ck() { grep -a '^  key leds:' "$L" | sed -n "$1p" | sed -n 's/.*key leds:\([0-9]*\) \([0-9]*\) .*/\1/p'; }
[ "$(kl 1)" = 000000000000000000 ] && [ "$(kl 7)" = 000000000000000000 ] && [ "$(ck 1)" = 111111111 ] && [ "$(kl 10)" = 400000000000400000 ] &&
    ok "Key Mode off: the roots dark (no idle glow), a single note D lit on both D alone; the nine chord keys faint, under the glow: $OUT/cr_key_leds_off.ppm" \
    || bad "Key Mode off: $(kl 1) / $(kl 7) chord keys $(ck 1) / held $(kl 10)"
[ "$(kl 2)" = 202202020230202202 ] && [ "$(kl 4)" = 302022020202302022 ] && [ "$(kl 5)" = 302202022020302202 ] && [ "$(ck 2)" = 111111111 ] &&
    ok "Key Mode on: C major, D major (the tonic changed), D minor: the scale's roots dim, the tonic's mid, the others dark (white keys too), the chord keys faint under them: $OUT/cr_key_leds_c.ppm" \
    || bad "the scale lights: $(kl 2) / $(kl 4) / $(kl 5)"
[ "$(kl 3)" = 302022020202302022 ] && ok "the KEY layer (keys: pick): the same picture" || bad "the KEY layer's roots: $(kl 3)"
[ "$(kl 6)" = 402402042020402402 ] &&
    ok "Dm sounding: every D, F and A lit over the glow (both D over the tonic's mid), the rest of the scale dim: $OUT/cr_key_leds_dm.ppm" || bad "a sounding chord over the scale: $(kl 6)"
[ "$(kl 8)" = 404404044040404404 ] && [ "$(ck 8)" = 444444444 ] && [ "$(kl 11)" = 444444444444444444 ] && [ "$(ck 11)" = 444444444 ] &&
    [ "$(grep -c '^expect .*: ok' "$L")" = 39 ] && ! has 'expect.*FAILED' "$L" &&
    ok "Stock (two levels, as it was): the scale's roots lit, the tonic as the rest, the others dark; a sounding note dark (every root of its name); Key Mode off: every root and chord key lit" \
    || bad "Stock / the LED expectations: $(kl 8) $(ck 8) / $(kl 11) $(ck 11) $(grep -a 'expect.*FAILED' "$L" | head -3)"
silent_end cr_key_leds

# the KEY layer's page 2 (SELECT) and Key plays: Chords / Notes
run cr_key_page
L="$OUT/cr_key_page.log"
kb() { grep -a '^  kbd:' "$L" | sed -n "$1p" | sed -n 's/.*kbd: plays \([a-z]*\) keyboard \([a-z]*\) single \([0-9]\) split-note \([0-9]*\) page \([0-9]\) (engine: notes \([01]\) full \([01]\) single \([0-9]\) snap [0-9])$/\1 \2 \3 \5 \6\7\8/p'; }
cv() { grep -a '^  cr: voices' "$L" | sed -n "$1p" | sed -n 's/.*cr: voices \([0-9]*\) .* notes-out \([0-9]*\) chord \(.*\) sounding \([01]\) notes\(.*\)$/\1|\2|\3|\4|\5/p'; }
bp() { grep -a '^  cr: key' "$L" | sed -n "$1p" | sed -n 's/.* bpm \([0-9]*\) .*/\1/p'; }
[ "$(kb 1)" = "chords chords 0 1 000" ] && [ "$(kb 2)" = "chords chords 0 2 000" ] && [ "$(bp 2)" = 120 ] && [ "$(kb 4)" = "notes chords 0 1 100" ] &&
    has '^key: cells Key plays Keyboard Snap Numerals' "$L" && [ "$(grep -ac '^key: page' "$L")" = 3 ] &&
    ok "KEY held: page 1; SELECT: page 2 (Key plays, Keyboard; the tempo untouched), past the last page: stays; back: page 1: $OUT/cr_key_page2.ppm" \
    || bad "the page turn: $(kb 1) / $(kb 2) bpm $(bp 2) / $(kb 4)"
differ cr_key_page1 cr_key_page2 "the two pages differ on screen"
differ cr_key_page2 cr_key_page2_notes "KNOB 1 on page 2: Key plays Notes: $OUT/cr_key_page2_notes.ppm"
[ "$(cv 5)" = "0|0||0|" ] && [ "$(cv 6)" = "0|0||0|" ] && [ "$(cv 9)" = "0|4|Em|0| 64 67 71" ] &&
    ok "Notes, Key Mode on (C major): D#4 does nothing (no voice, no note out, dark), with MIN held neither" \
    || bad "a root out of the key: $(cv 5) / $(cv 6) / $(cv 9)"
[ "$(cv 7)" = "1|1|D|1| 62" ] && [ "$(cv 8)" = "1|4|Em|1| 64 67 71" ] &&
    ok "Notes: D4 is a single note (no scale chord), MIN + E4 is Em: the chord keys as with Key Mode off" || bad "Notes, in the key: $(cv 7) / $(cv 8)"
[ "$(cv 11)" = "1|7|Dm|1| 62 65 69" ] && [ "$(cv 12 | cut -d'|' -f1,4)" = "1|1" ] && [ "$(kb 11)" = "chords chords 0 2 000" ] &&
    ok "Chords again: D4 is Dm, D#4 its altered chord" || bad "back to Chords: $(cv 11) / $(cv 12)"
[ "$(grep -c '^expect .*: ok' "$L")" = 6 ] && ! has 'expect.*FAILED' "$L" && ok "its sound and LED expectations" || bad "cr_key_page expectations"
silent_end cr_key_page
# Key Mode's Snap (0.22, GitHub issue 18): page 2's KNOB 3; a root out of the scale plays the nearest note of the scale
# (ties down) with Key plays Notes, glows faint for it; Chord with nothing sounding falls back to the scale; Off: as before
run cr_key_snap
L="$OUT/cr_key_snap.log"
ks() { grep -a '^  kbd:' "$L" | sed -n "$1p" | sed -n 's/.*(engine: notes \([01]\) full \([01]\) single \([0-9]\) snap \([0-9]\))$/\1\4/p'; }
cv() { grep -a '^  cr: voices' "$L" | sed -n "$1p" | sed -n 's/.*cr: voices \([0-9]*\) .* notes-out \([0-9]*\) chord \(.*\) sounding \([01]\) notes\(.*\)$/\1|\2|\3|\4|\5/p'; }
kl() { grep -a '^  key leds:' "$L" | sed -n "$1p" | sed -n 's/.*key leds:\([0-9]*\) \([0-9]*\) .*/\2/p'; }
[ "$(ks 1)" = 11 ] && [ "$(ks 6)" = 12 ] && [ "$(ks 7)" = 12 ] && [ "$(ks 9)" = 10 ] &&
    has '^key: knob 3 snap Scale' "$L" && has '^key: knob 3 snap Chord' "$L" && has '^key: knob 3 snap Off' "$L" &&
    has '^key: cells Key plays Keyboard Snap Numerals' "$L" &&
    ok "KEY page 2, KNOB 3: Snap Scale, Chord (the last value: stays), Off, in the UI and in the engine: $OUT/cr_key_snap_page.ppm" \
    || bad "the Snap knob: $(ks 1) / $(ks 6) / $(ks 7) / $(ks 9)"
[ "$(kl 2)" = 212212121231212212 ] && ok "Snap on, C major: the roots out of the scale glow faint (they play now), the scale dim, the tonic mid" \
    || bad "the Snap glow: $(kl 2)"
[ "$(cv 3)" = "1|1|D|1| 62" ] && [ "$(cv 4 | cut -d'|' -f1,4)" = "0|0" ] && [ "$(cv 5)" = "1|2|D|1| 62" ] && [ "$(kl 3)" = 412212121231412212 ] &&
    ok "Scale: D#4 plays D (ties go down): one voice, the note out, D's keys lit over its faint; up: off; D4 itself as ever" \
    || bad "Scale: $(cv 3) / $(cv 4) / $(cv 5) / leds $(kl 3)"
[ "$(cv 8)" = "1|3|F|1| 65" ] && ok "Chord with nothing sounding: F#4 plays the scale's F" || bad "Chord's fallback: $(cv 8)"
[ "$(cv 10 | cut -d'|' -f1,2,4)" = "0|3|0" ] && [ "$(kl 9)" = 202202020230202202 ] && [ "$(grep -c '^expect .*: ok' "$L")" = 10 ] && ! has 'expect.*FAILED' "$L" &&
    ok "Snap Off again: D#4 dark and silent (no voice, no note out), as before" || bad "Snap Off: $(cv 10) / leds $(kl 9) / $(grep -a 'expect.*FAILED' "$L" | head -3)"
silent_end cr_key_snap

# the KEY layer's Numerals (0.22, GitHub issue 1): page 2, KNOB 4; the chord's Roman numeral in blue above the chord
# name on the Chord view (3y34), counted from the root as played (Transpose taken out)
run cr_numerals
L="$OUT/cr_numerals.log"
nm() { grep -a '^  numerals:' "$L" | sed -n "$1p" | sed -n 's/.*numerals: \([a-z]*\) shown "\(.*\)"$/\1|\2/p'; }
cv() { grep -a '^  cr: voices' "$L" | sed -n "$1p" | sed -n 's/.*cr: voices \([0-9]*\) .* notes-out \([0-9]*\) chord \(.*\) sounding \([01]\) notes\(.*\)$/\3|\4|\5/p'; }
rows() {   # rows A B: the first and the last screen row where two shots differ ("" when they are the same)
    cmp -l "$OUT/$1.ppm" "$OUT/$2.ppm" | awk '{ r = int(int(($1 - 16) / 3) / 240); if (!n++ || r < a) a = r; if (r > b) b = r } END { if (n) print a, b }'
}
[ "$(nm 1)" = "off|" ] && has '^key: knob 4 numerals on' "$L" && [ "$(nm 2)" = "on|" ] &&
    ok "Numerals is Off by default (G in C major: no numeral); KEY page 2, KNOB 4 turns it on: $OUT/cr_numerals_page2.ppm" \
    || bad "the option: $(nm 1) / $(nm 2)"
[ "$(nm 3)" = "on|V" ] && [ "$(nm 4)" = "on|ii" ] && [ "$(nm 8)" = "on|" ] && [ "$(nm 9)" = "on|iv" ] &&
    ok "C major: G is V, Dm ii; nothing sounding: none; A minor: Dm is iv: $OUT/cr_numerals_major.ppm, $OUT/cr_numerals_minor.ppm" \
    || bad "the numerals: $(nm 3) / $(nm 4) / $(nm 8) / $(nm 9)"
[ "$(nm 5)|$(cv 5 | cut -d'|' -f1)" = "$(printf 'on|vii\260|Bdim')" ] && [ "$(nm 6)|$(cv 6 | cut -d'|' -f1)" = "on|VI|Asus" ] &&
    ok "Bdim is vii with the degree sign; a key outside the scale is Key Mode's sus chord on a scale note (A#4: Asus, VI): $OUT/cr_numerals_dim.ppm" \
    || bad "dim / outside the scale: $(nm 5) $(cv 5) / $(nm 6) $(cv 6)"
[ "$(nm 7)|$(cv 7 | cut -d'|' -f1)" = "on|I|C#M" ] &&
    ok "Transpose +1, the tonic chord played: the name is what sounds (C#M), the numeral the degree played (I): $OUT/cr_numerals_transposed.ppm" \
    || bad "Transpose: $(nm 7) $(cv 7)"
[ "$(cv 1)" = "$(cv 3)" ] && [ "$(cv 3 | cut -d'|' -f1,2)" = "GM|1" ] && [ "$(nm 11)" = "on|" ] &&
    ok "display only: G's notes are the same with it on; Key Mode off: no numeral" || bad "$(cv 1) / $(cv 3) / $(nm 11)"
cmp -s "$OUT/cr_numerals_off_idle.ppm" "$OUT/cr_numerals_idle.ppm" &&
    ok "Numerals On, nothing sounding: the Chord view is the one with Numerals Off, pixel for pixel (the name does not move)" \
    || bad "Numerals On with nothing sounding moved something: rows $(rows cr_numerals_off_idle cr_numerals_idle)"
r=$(rows cr_numerals_off cr_numerals_major)
[ -n "$r" ] && [ "${r% *}" -ge 28 ] && [ "${r#* }" -le 47 ] &&
    ok "Numerals On against Off, the same chord: only rows $r differ (the gap under the top line, above the name)" \
    || bad "the numeral on the Chord view: rows '$r'"
[ "$(nm 10)" = "on|iv" ] && ok "the arp stepping on the Chord view: the numeral with it (iv): $OUT/cr_numerals_arp.ppm" || bad "the arp: $(nm 10)"
differ cr_numerals_arp cr_numerals_minor "the arp screen is not the chord view"
silent_end cr_numerals

# Numerals = Solfege (GitHub issue 15): KNOB 4's third value; the numeral's degree as a movable-do syllable in its place
run cr_solfege
L="$OUT/cr_solfege.log"
nm() { grep -a '^  numerals:' "$L" | sed -n "$1p" | sed -n 's/.*numerals: \([a-z]*\) shown "\(.*\)"$/\1|\2/p'; }
cv() { grep -a '^  cr: voices' "$L" | sed -n "$1p" | sed -n 's/.*cr: voices \([0-9]*\) .* notes-out \([0-9]*\) chord \(.*\) sounding \([01]\) notes\(.*\)$/\3|\4|\5/p'; }
has '^key: knob 4 numerals on' "$L" && [ "$(grep -ac '^key: knob 4 numerals solfege' "$L")" = 2 ] && [ "$(nm 1)" = "solfege|" ] &&
    ok "KEY page 2, KNOB 4: Off, On, Solfege, and it stays at Solfege: $OUT/cr_solfege_page2.ppm" || bad "the picker: $(nm 1)"
[ "$(nm 2)|$(cv 2 | cut -d'|' -f1)" = "solfege|SOL|GM" ] && [ "$(nm 3)" = "solfege|re" ] &&
    [ "$(nm 4)|$(cv 4 | cut -d'|' -f1)" = "$(printf 'solfege|ti\260|Bdim')" ] &&
    ok "C major: G is SOL, Dm re, Bdim ti with the degree sign: $OUT/cr_solfege_major.ppm, $OUT/cr_solfege_dim.ppm" \
    || bad "C major: $(nm 2) $(cv 2) / $(nm 3) / $(nm 4)"
[ "$(nm 5)|$(cv 5 | cut -d'|' -f1)" = "solfege|DO|C#M" ] &&
    ok "Transpose +1, the tonic chord played: C#M sounds, DO (movable do: the degree played)" || bad "Transpose: $(nm 5) $(cv 5)"
[ "$(nm 6)" = "solfege|fa" ] && [ "$(nm 7)|$(cv 7 | cut -d'|' -f1)" = "solfege|MI|CM" ] &&
    ok "A minor: Do is A; Dm is fa, C is MI (the scale's third degree): $OUT/cr_solfege_minor.ppm" || bad "A minor: $(nm 6) / $(nm 7)"
[ "$(nm 8)" = "on|iv" ] && [ "$(cv 8)" = "$(cv 6)" ] &&
    ok "KNOB 4 back to On: the Roman numeral (iv) for the same Dm, the same notes (display only)" || bad "back to On: $(nm 8) $(cv 8) / $(cv 6)"
silent_end cr_solfege

# the Full keyboard
run cr_fullkbd
L="$OUT/cr_fullkbd.log"
kb() { grep -a '^  kbd:' "$L" | sed -n "$1p" | sed -n 's/.*kbd: plays \([a-z]*\) keyboard \([a-z]*\) single \([0-9]\) split-note \([0-9]*\) page \([0-9]\) (engine: notes \([01]\) full \([01]\) single \([0-9]\) snap [0-9])$/\2 \3 \4 \7\8/p'; }
cv() { grep -a '^  cr: voices' "$L" | sed -n "$1p" | sed -n 's/.*cr: voices \([0-9]*\) .* notes-out \([0-9]*\) chord \(.*\) sounding \([01]\) notes\(.*\)$/\1|\2|\3|\4|\5/p'; }
lt() { grep -a '^  lock: on' "$L" | sed -n "$1p" | sed -n 's/.*lock: on \([01]\) latched \(..\) .*/\1 \2/p'; }
pv() { grep -a "^  part $2:" "$L" | sed -n "$1p" | sed -n 's/.* voices \([0-9]*\),.*/\1/p'; }
kl() { grep -a '^  key leds:' "$L" | sed -n "$1p" | sed -n 's/.*key leds:\([0-9]*\) \([0-9]*\) .*/\1 \2/p'; }
ky() { grep -a '^  cr: key' "$L" | sed -n "$1p" | sed -n 's/.* voicing \(-*[0-9]*\) octave \(-*[0-9]*\) .* layer \([0-9]*\) .*/\1 \2 \3/p'; }
n=0; i=3; note=53
while [ $i -le 29 ]; do
    [ "$(cv $i | cut -d'|' -f1,4,5)" = "1|1| $note" ] && n=$((n + 1))
    i=$((i + 1)); note=$((note + 1))
done
[ "$(kb 1)" = "chords 0 65 00" ] && [ "$(kb 2)" = "full 0 65 10" ] && [ $n = 27 ] &&
    ok "Keyboard = Full (page 2, KNOB 2): the 27 keys F3..G5 are the single notes 53..79, one voice each: $OUT/cr_fullkbd_page2.ppm" \
    || bad "the Full keyboard: $(kb 1) / $(kb 2), $n of 27 keys chromatic"
[ "$(cv 9 | cut -d'|' -f3,5)" = "B| 59" ] && [ "$(lt 9)" = "1 44" ] && [ "$(ky 9 | cut -d' ' -f3)" = 0 ] && [ "$(lt 2)" = "1 44" ] &&
    ok "B3 is a note (B, 59): LOCK not toggled, no LOCK layer however long it is held; the latch behind (MAJ + M7) kept, and ignored: every key a single note" \
    || bad "B3 on the Full keyboard: $(cv 9) lock $(lt 9) layer $(ky 9)"
[ "$(cv 30 | cut -d'|' -f1)" = 2 ] && ok "two keys held: two notes, both keys lit (and every key of their names): $OUT/cr_fullkbd_two.ppm" || bad "two keys: $(cv 30)"
[ "$(ky 31)" = "1 0 0" ] && [ "$(cv 31 | cut -d'|' -f5)" = " 53" ] && [ "$(ky 32)" = "0 0 0" ] && [ "$(kb 31)" = "full 0 65 10" ] &&
    ok "KNOB 1 on the view: the voicing since 0.24 (+1; the octave stays 0, OCT- / OCT+'s; a single note F3 plays 53 as ever): $OUT/cr_fullkbd_octave.ppm" \
    || bad "KNOB 1 = voicing: $(ky 31) $(cv 31) / $(ky 32) / $(kb 31)"
[ "$(kl 33)" = "202020230 202202020230202202" ] && [ "$(kl 30)" = "400040000 000400040000000400" ] && [ "$(cv 35 | cut -d'|' -f1,4)" = "1|1" ] && [ "$(cv 34 | cut -d'|' -f1)" = 0 ] &&
    [ "$(cv 36 | cut -d'|' -f1,3,5)" = "1|GM| 55 59 62" ] &&
    ok "Key Mode on (C major), Key plays Chords: the scale on all 27 keys (tonic mid, scale dim, the rest dark; Key Mode off: all 27 dark but the F and A keys of the two held); F#3 plays its alternate chord; G3 the key's G major (0.22): $OUT/cr_fullkbd_key.ppm" \
    || bad "Key Mode on the Full keyboard: $(kl 33) / $(cv 34) / $(cv 35) / $(cv 36)"
[ "$(kb 37)" = "full 2 65 12" ] && [ "$(pv 38 0)" = 0 ] && [ "$(pv 38 1)" = 1 ] && [ "$(pv 39 1)" = 0 ] && [ "$(pv 39 0)" -ge 1 ] && [ "$(cv 38 | cut -d'|' -f5)" = " 64" ] &&
    ok "Split Bass: the split at F4: E4 plays the bass part alone (the Bass off: ignored), F4 the chord part alone: $OUT/cr_fullkbd_splitbass.ppm" \
    || bad "Split Bass: $(kb 37), E4 parts $(pv 38 0)/$(pv 38 1), F4 parts $(pv 39 0)/$(pv 39 1)"
[ "$(kb 40)" = "full 1 65 11" ] && [ "$(cv 40 | cut -d'|' -f5)" = " 52" ] && [ "$(cv 41 | cut -d'|' -f5)" = " 65" ] && [ "$(pv 40 1)" = 0 ] &&
    ok "Split Oct: E4 sounds an octave lower (52) on the chord part, F4 at pitch (65)" || bad "Split Oct: $(kb 40) $(cv 40) / $(cv 41)"
[ "$(kb 42)" = "chords 0 65 00" ] && [ "$(lt 42)" = "1 44" ] && [ "$(cv 43 | cut -d'|' -f3,5)" = "DM7| 62 66 69 73" ] &&
    [ "$(grep -c '^expect .*: ok' "$L")" = 18 ] && ! has 'expect.*FAILED' "$L" &&
    ok "Keyboard = Chords again: the latch as it was (LOCK on, MAJ + M7 lit), D4 is DM7" || bad "leaving the Full keyboard: $(kb 42) lock $(lt 42) $(cv 43)"
silent_end cr_fullkbd

echo "user presets: Overwrite, delete, the naming keys"
run cr_save_del
has '^save: dialog part 0 ADG (overwrite)' "$OUT/cr_save_del.log" && has '^save: part 0 overwrite slot U001 FM6 26 ADG rc 0' "$OUT/cr_save_del.log" &&
    ok "SAVE on a user preset: Overwrite by default, U01 rewritten, its name and place kept: $OUT/cr_save_overwrite.ppm" || bad "overwrite: $(grep '^save:' "$OUT/cr_save_del.log" | tr '\n' ' ')"
[ "$(grep -c '^save: delete ADG?' "$OUT/cr_save_del.log")" = 2 ] && has '^save: delete slot U001 rc 0' "$OUT/cr_save_del.log" &&
    has '^save: now part 0 FM6 25 PIANO' "$OUT/cr_save_del.log" &&
    ok "SAVE held 1 s in the dialog: delete? (OCT- kept it), OCT+ deleted U01, the part on FM6 25: $OUT/cr_save_delete.ppm" || bad "delete"
[ "$(grep -c '^expect led .*: ok' "$OUT/cr_save_del.log")" = 6 ] && ok "naming: the typing keys lit (F#4: delete), G#4 dark, OCT- lit" || bad "naming LEDs"

echo "presets per engine (docs/PRESETS.md): PRESETS in the engine's pool, OPT + PRESETS the engine, Save as new, Overwrite, reset"
run cr_presets
PL="$OUT/cr_presets.log"
pop() { grep -q "^popup: part 0 $1\$" "$PL"; }
stripes() {      # stripes FILE: "LIT/ALL", a meter's stripes on row 201 (through their middle, cr_draw.c cr_p_meter:
                 # a run of ink is a stripe, a bright one is lit)
    od -An -tu1 -v -j $((15 + 201 * 240 * 3)) -N720 "$1" | tr -s ' \n' '\n\n' | grep -v '^$' |
        awk '{ v[n++] = $1 } END { lit = all = run = hot = 0
               for (i = 0; i <= 240; i++) { s = i < 240 ? v[3*i] + v[3*i+1] + v[3*i+2] : 0
                   if (s > 90) { run = 1; if (v[3*i] > 180) hot = 1 } else if (run) { all++; lit += hot; run = hot = 0 } }
               print lit "/" all }'
}
pop '00 / INIT / FM6 · 00/25' && pop '25 / PIANO / FM6 · 25/25' && pop '05 / FM PAD / FM6 · 05/25' &&
    ok "PRESETS turns inside FM6's pool, INIT at 00 and not counted, wrapping (00 INIT, 25 PIANO, 05 \"FM PAD\" \"FM6 · 05/25\"): $OUT/cr_presets_init.ppm $OUT/cr_presets_meter.ppm" \
    || bad "the FM6 pool: $(grep '^popup:' "$PL" | head -3 | tr '\n' ' ')"
s0=$(stripes "$OUT/cr_presets_init.ppm"); s5=$(stripes "$OUT/cr_presets_meter.ppm"); sl=$(stripes "$OUT/cr_presets_last.ppm")
[ "$s0" = 0/25 ] && [ "$s5" = 5/25 ] && [ "$sl" = 25/25 ] &&
    ok "the meter: a stripe per preset (25), none lit on INIT, 5 on 05, every one on 25 PIANO, the last: $OUT/cr_presets_last.ppm" \
    || bad "the meter's stripes (lit/all): INIT $s0 (0/25), 05 $s5 (5/25), 25 the last $sl (25/25)"
[ "$(grep -c '^sound: part 0 engine' "$PL")" = 4 ] && has '^engine pick: VA (25 presets)' "$PL" && has '^sound: part 0 engine VA pos 1 LUSH PAD' "$PL" &&
    ok "OPT + PRESETS: the engine picker (VA, 25 presets), OPT released: VA 01 LUSH PAD: $OUT/cr_presets_engine.ppm" || bad "OPT + PRESETS: $(grep '^sound: part 0 engine' "$PL" | head -1)"
[ "$(grep -c '^sound: part 0 engine FM6 pos 5 FM PAD' "$PL")" = 2 ] && has '^sound: part 0 engine VA pos 4 SLOW STRINGS' "$PL" &&
    ok "each engine remembers its place: FM6 back on 05 FM PAD, the VA on 04 SLOW STRINGS" || bad "the per-engine memory: $(grep '^sound: part 0 engine' "$PL" | tr '\n' ' ')"
has '^save: dialog part 0 FM PAD (save as new)' "$PL" && has '^save: new part 0 FM6 26 (slot U001)' "$PL" && has '^save: part 0 slot U001 name ADG rc 0' "$PL" &&
    pop '26 / ADG / FM6 · 26/26' && [ "$(stripes "$OUT/cr_presets_new.ppm")" = 26/26 ] &&
    ok "Save as new: the next place (FM6 26), PRESETS sits on it (26/26, the meter's 26 stripes lit): $OUT/cr_presets_dialog.ppm $OUT/cr_presets_naming.ppm $OUT/cr_presets_new.ppm" \
    || bad "Save as new: $(grep '^save:' "$PL" | head -3 | tr '\n' ' ') stripes $(stripes "$OUT/cr_presets_new.ppm")"
has '^save: part 0 overwrite slot U002 FM6 02 FM BELL rc 0' "$PL" && pop '02 / FM BELL (mark) / FM6 · 02/26' &&
    ok "Overwrite on a factory preset: bound in U02, the number and the name kept, the mark: $OUT/cr_presets_overwrite.ppm $OUT/cr_presets_mark.ppm" || bad "Overwrite: $(grep 'overwrite' "$PL")"
has '^save: reset FM BELL?' "$PL" && has '^save: reset slot U002 rc 0' "$PL" && has '^save: now part 0 FM6 02 FM BELL' "$PL" && pop '02 / FM BELL / FM6 · 02/26' &&
    ok "SAVE held 1 s: reset to factory?, OCT+: the factory FM BELL back at 02 (no mark): $OUT/cr_presets_reset.ppm $OUT/cr_presets_factory.ppm" || bad "reset: $(grep '^save: reset' "$PL")"
[ "$(grep -c '^save: part 0 slot U[0-9]* name .* rc 0' "$PL")" = 128 ] && has '^save: no free slot' "$PL" &&
    ok "128 user sounds fill the pool's slots (0.22); the next Save as new: \"no free slot\": $OUT/cr_presets_full.ppm" || bad "no free slot: $(grep -c 'name .* rc 0' "$PL") saves"
s0=$(stripes "$OUT/cr_presets_long_init.ppm"); s1=$(stripes "$OUT/cr_presets_long_first.ppm")
sb=$(stripes "$OUT/cr_presets_long_before.ppm"); sl=$(stripes "$OUT/cr_presets_long_last.ppm")
pop '00 / INIT / FM6 · 00/153' && pop '01 / TINE EP / FM6 · 01/153' && pop '152 / FM BELL / FM6 · 152/153' && pop '153 / FM BELL / FM6 · 153/153' &&
    [ "$s0" = 0/32 ] && [ "$s1" = 1/32 ] && [ "$sb" = 31/32 ] && [ "$sl" = 32/32 ] &&
    ok "a pool longer than the meter (153 presets, 32 stripes): none lit on INIT, one on 001, 31 on 152, every one only on 153, the last: $OUT/cr_presets_long_last.ppm" \
    || bad "the long pool's stripes (lit/all): INIT $s0 (0/32), 001 $s1 (1/32), 152 $sb (31/32), 153 the last $sl (32/32); $(grep '^popup:' "$PL" | tail -4 | tr '\n' ' ')"

echo "the sound read-out's stripes move from the place shown, not from none (cr_ui.c cu_animate, cr_draw.c cr_fill_at)"
export EMU_UI_LOG=0
run cr_meter_step
unset EMU_UI_LOG
ML="$OUT/cr_meter_step.log"
ms() {           # ms N: "MIN MAX FIRST LAST ALL SETTLED" of the stripes lit in the read-out's frames after the Nth
                 # "popup:" line (a pool's meter: 25 or 32 stripes; SETTLED: the frame it stood still from), "MONO 0"
                 # appended when a frame moved against the step's direction
    grep -a '^popup:\|^ui: frame.*device): meter ' "$ML" | awk -v want="$1" '
        /^popup:/ { n++; next }
        n == want && / of (25|32) from / {
            for (i = 1; i <= NF; i++) if ($i == "fill") { f = $(i+1) + 0; all = $(i+3); to = $(i+7) + 0 }
            if (!c++) { lo = hi = first = last = f; bad = 0 }
            if (f < lo) lo = f; if (f > hi) hi = f
            if (f != last) { settle = c; if ((f > last) != (to > last)) bad = 1 }
            last = f }
        END { print lo, hi, first, last, all, settle + 0 (bad ? " MONO 0" : "") }'
}
m1=$(ms 1); m2=$(ms 2); m3=$(ms 3)
[ "${m1% *}" = "0 5 0 5 25" ] && ok "a part's first read-out (FM6 01 -> 05): the stripes fill from none ($m1: min max first last all settled-frame)" || bad "the first read-out: $m1"
[ "${m2% *}" = "5 6 5 6 25" ] && [ "${m2##* }" -le 4 ] && ok "05 -> 06 (the read-out shown again after it went): no frame with fewer than 5 stripes, the sixth lit by frame ${m2##* } ($m2)" || bad "05 -> 06: $m2 (5 6 5 6 25)"
[ "${m3% *}" = "5 6 6 5 25" ] && [ "${m3##* }" -le 4 ] && ok "06 -> 05: no frame with more than 6, the sixth dark by frame ${m3##* } ($m3)" || bad "06 -> 05: $m3 (5 6 6 5 25)"
m4=$(ms 4); m5=$(ms 5); m6=$(ms 6)
[ "${m4% *}" = "0 5 5 0 25" ] && [ "${m5% *}" = "0 25 0 25 25" ] && [ "${m6% *}" = "0 25 25 0 25" ] && [ "${m5##* }" -le 30 ] && [ "${m6##* }" -le 30 ] &&
    ok "a wrap: INIT -> 25, the last, lights the 25 in ${m5##* } frames, 25 -> INIT darkens them in ${m6##* } (15 ms a frame: inside 420 ms, cr_draw.c CR_FILL_MAX; the read-out lives 900)" \
    || bad "the wrap: 05 -> INIT $m4 / INIT -> 25 $m5 / 25 -> INIT $m6"
m7=$(ms 7); m8=$(ms 8); m9=$(ms 9); m10=$(ms 10); m11=$(ms 11)
set -- $m8; a8=$3; set -- $m9; a9=$3; set -- $m10
[ "${a8:-0}" -ge 1 ] && [ "${a9:-0}" -ge "${a8:-9}" ] && [ "$4" = 2 ] && [ "$2" -le 5 ] && [ "$1" = 2 ] && [ "${m11% *}" = "2 3 2 3 25" ] &&
    ok "turned on while the stripes move (a detent every 40 ms: 03 04 05, back to 02): each step goes on from where they stand ($a8, $a9), never from none; after another meter (the fx amount) 02 -> 03 from 2" \
    || bad "steps under way: $m7 / $m8 / $m9 / $m10 / after another meter $m11"
m12=$(ms 12); m13=$(ms 13); m14=$(ms 14); m15=$(ms 15); m16=$(ms 16)
[ "${m12% *}" = "0 1 0 1 32" ] && [ "${m13% *}" = "1 1 1 1 32" ] && [ "${m14% *}" = "1 16 1 16 32" ] && [ "${m15% *}" = "16 32 16 32 32" ] && [ "${m16% *}" = "31 32 32 31 32" ] &&
    [ "${m14##* }" -le 30 ] && [ "${m15##* }" -le 30 ] &&
    ok "another engine (OPT + PRESETS: CZ-1, 64 presets on 32 stripes): from none; then 02 (still one), 32 (1 -> 16), 64 (16 -> 32, in ${m15##* } frames), 63 (one dark)" \
    || bad "the long pool: $m12 / $m13 / $m14 / $m15 / $m16"
m17=$(ms 17); m18=$(ms 18); m19=$(ms 19); m20=$(ms 20); m21=$(ms 21)
[ "${m17% *}" = "0 17 0 17 25" ] && [ "${m18% *}" = "17 18 17 18 25" ] && [ "${m19% *}" = "0 18 18 0 25" ] && [ "${m20% *}" = "0 1 0 1 25" ] && [ "${m21% *}" = "31 31 31 31 32" ] &&
    ok "the other part (ALGORITHM, the bass): its first read-out from none, 17 -> 18 one stripe, off: its 18 go dark, on again from none; the chord part's read-out then from its own place (31)" \
    || bad "the bass read-out: $m17 / $m18 / $m19 / $m20 / the chord part again $m21"
n=0; for i in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21; do case "$(ms $i)" in *MONO*) n=$((n + 1));; esac; done
[ $n = 0 ] && ok "no frame of the 21 read-outs moves against its step (up: only more stripes, down: only fewer)" || bad "$n read-outs with a frame against the step's direction"

echo "a sounding note lights every root of its note name (cr_ui.c cr_leds)"
run cr_note_leds
L="$OUT/cr_note_leds.log"
kl() { grep -a '^  key leds:' "$L" | sed -n "$1p" | sed -n 's/.*key leds:\([0-9]*\) \([0-9]*\) .*/\2/p'; }
ck() { grep -a '^  key leds:' "$L" | sed -n "$1p" | sed -n 's/.*key leds:\([0-9]*\) \([0-9]*\) .*/\1/p'; }
nt() { grep -a '^  cr: voices' "$L" | sed -n "$1p" | sed -n 's/.* sounding \([01]\) notes\(.*\)$/\1\2/p'; }
[ "$(kl 1)" = 400040040000400040 ] && [ "$(ck 1)" = 111114111 ] && [ "$(nt 1)" = "1 62 66 69" ] &&
    ok "D major (62 66 69): every D, F# and A root lit, in both octaves; no chord key but MAJ, held (the others and B3 idle, faint)" || bad "D major: $(ck 1) $(kl 1) notes $(nt 1)"
[ "$(kl 2)" = 400040040000400040 ] && [ "$(nt 2)" = "1 93 98 102" ] && [ "$(kl 3)" = 400040040000400040 ] && [ "$(nt 3)" = "1 30 33 38" ] && [ "$(ck 3)" = 111114111 ] &&
    ok "voiced out of the keys' range (KNOB 1 +8: 93 98 102; -8: 30 33 38): the same keys lit (until 0.17: none)" || bad "voiced out of range: $(kl 2) notes $(nt 2) / $(kl 3) notes $(nt 3)"
[ "$(kl 4)" = 000000000000000000 ] && [ "$(kl 25)" = 000000000000000000 ] && ok "released: the roots dark" || bad "released: $(kl 4) / $(kl 25)"
[ "$(kl 5)" = 000004000000000004 ] && [ "$(nt 5)" = "1 67" ] && [ "$(kl 6)" = 400004000400400004 ] && [ "$(nt 6)" = "1 67 71 74" ] &&
    ok "Transpose +5: D4 alone sounds G: the two G lit, not the key under the finger; MAJ + D4: every G, B and D" || bad "Transpose +5: $(kl 5) notes $(nt 5) / $(kl 6) notes $(nt 6)"
[ "$(kl 7)" = 400000000000400000 ] && [ "$(nt 7)" = "1 86" ] && ok "OCT+ twice (D4 plays 86): both D lit" || bad "the octave: $(kl 7) notes $(nt 7)"
[ "$(kl 8)" = 402402040230402402 ] && ok "Key Mode on (C major), Dm: its D, F and A lit over the scale's glow (dim), the tonic mid, out of the key dark" || bad "Key Mode: $(kl 8)"
i=9; full=0; part=0; odd=0
while [ $i -le 24 ]; do
    case "$(kl $i)" in
    400040040000400040) full=$((full + 1));;                                    # the chord, whole
    000040040000000040|400000040000400000|400040000000400040) part=$((part + 1));;   # all D, all F# or the A dark
    *) odd=$((odd + 1));;
    esac
    i=$((i + 1))
done
[ $odd = 0 ] && [ $part -ge 3 ] && [ $full -ge 3 ] &&
    ok "the arp's stepping note blinks on every key of its name, the rest of the chord lit (16 dumps: $full whole, $part with one note name dark in both octaves, 0 other)" \
    || bad "the arp: $full whole, $part one name dark, $odd other pictures"
[ "$(grep -c '^expect .*: ok' "$L")" = 45 ] && ! has 'expect.*FAILED' "$L" && ok "its 45 LED expectations" || bad "cr_note_leds expectations: $(grep -c '^expect .*: ok' "$L") ok, $(grep -a 'expect.*FAILED' "$L" | head -3)"
silent_end cr_note_leds

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

echo "P5: Melodee's Prophet-5 engine, Sequential's programs (the shipped list), the deep pages, SAVE refused"
# the places' names come from the shipped list (assets/prophet5-factory/shipped.txt): a list changed changes no line here
P5N=$(python3 tools/gen_prophet_factory.py --list | wc -l | tr -d ' ')
p5name() { python3 tools/gen_prophet_factory.py --list | awk -v p="$1" '$1 == p { sub(/^ *[0-9]+ +[0-9]+ +[U-] +/, ""); print }'; }
P5A=$(p5name 1); P5B=$(p5name 3); P5C=$(p5name 4)
run cr_prophet --wav "$OUT/cr_prophet.wav"
PL="$OUT/cr_prophet.log"
[ "$P5N" -ge 15 ] && has "^engine pick: P5 ($P5N presets)" "$PL" && has "part 0: P5 / $P5A" "$PL" &&
    ok "OPT + PRESETS +4: P5, the fifth of six engines, $P5N presets (the shipped list); its place 01 $P5A: $OUT/cr_prophet_pick.ppm" ||
    bad "not P5 $P5A of $P5N: $(grep -m1 'part 0:' "$PL")"
grep -a '^  screen reads:' "$PL" | sed -n 1p | grep -q "value '01' sub '$P5A' " &&
    ok "the pool's read-out shows a name whole (01 $P5A)" || bad "the read-out: $(grep -a -m1 '^  screen reads:' "$PL" | cut -c1-80)"
has "^sound: part 0 pos 3 $P5B" "$PL" && has "^sound: part 0 pos 4 $P5C" "$PL" &&
    ok "PRESETS steps the pool: 03 $P5B, 04 $P5C: $OUT/cr_prophet_pool.ppm" || bad "the pool: $(grep -c '^sound: part 0' "$PL") loads"
[ "$(grep -c '^expect sound .*: ok' "$PL")" = 4 ] && ok "the chord sounds on the three programs and after the edit" ||
    bad "P5 chord: $(grep -c '^expect sound .*: ok' "$PL") of 4"
p5c=$(sed -n 's/^deep: part 0 page 5 FILTER col 0 CUT \([0-9]*\) -> \([0-9]*\) .*/\1 \2/p' "$PL" | head -1)
[ -n "$p5c" ] && [ "${p5c#* }" -lt "${p5c% *}" ] && ok "FILT (the rows' FILTER, one lane under the curve), KNOB 1 -8: $(grep -m1 '^deep: part 0 page 5' "$PL")" ||
    bad "no CUT edit: $(grep -m1 '^deep:' "$PL")"
p5next() {   # p5next RE: the first trace line matching RE and the next "screen reads" line after it (the dump's)
    awk -v re="$1" '$0 ~ re && !f { print; f = 1; next } f && /^  screen reads:/ { print; exit }' "$PL"
}
# P5's rows (the compiled classic layout; the trace names the built-in page and column, the engine's address): the LFO's
# first lane, KNOB 1 +2 turns FREQ (LFO col 0 RATE), the cell 0.0 reads its value; KNOB 2 +1 the saw's button (LFO+ col 0
# SAW), the cell 0.1 reads On; OSC A's second lane, KNOB 2 -4 Mix A (MIXER col 0), the cell 1.1 reads it
p5f=$(p5next '^deep: part 0 page 11 LFO col 0 RATE ' | sed -n 's/^deep: part 0 page 11 LFO col 0 RATE [0-9]* -> \([0-9]*\) .*/\1/p; s/^  screen reads: .* 0\.0:FREQ=\([^ ]*\) .*/\1/p' | tr '\n' ' ')
p5s=$(p5next '^deep: part 0 page 12 LFO\\+ col 0 SAW ' | sed -n 's/^deep: part 0 page 12 LFO+ col 0 SAW \([0-9]\) -> \([0-9]\) .*/\1 \2/p; s/^  screen reads: .* 0\.1:SAW=\([^ ]*\) .*/\1/p' | tr '\n' ' ')
p5m=$(p5next '^deep: part 0 page 4 MIXER col 0 MIX A ' | sed -n 's/^deep: part 0 page 4 MIXER col 0 MIX A \([0-9]*\) -> \([0-9]*\) .*/\1 \2/p; s/^  screen reads: .* 1\.1:MIX A=\([^ ]*\) .*/\1/p' | tr '\n' ' ')
set -- $p5f
[ $# = 2 ] && [ "$1" = "$2" ] && ok "LFO, the first lane: KNOB 1 +2 FREQ (LFO col 0 RATE) -> $1, the cell 0.0 reads '$2': $OUT/cr_prophet_lfo_freq.ppm" ||
    bad "the LFO's KNOB 1: $p5f / $(grep -a -m1 '^deep: part 0 page 11' "$PL")"
set -- $p5s
[ $# = 3 ] && [ "$1" != "$2" ] && { [ "$2.$3" = 1.On ] || [ "$2.$3" = 0.Off ]; } &&
    ok "LFO, the first lane: KNOB 2 +1 the saw's button (LFO+ col 0 SAW) $1 -> $2, the cell 0.1 reads '$3': $OUT/cr_prophet_lfo_saw.ppm" ||
    bad "the LFO's KNOB 2: $p5s / $(grep -a -m1 '^deep: part 0 page 12' "$PL")"
set -- $p5m
[ $# = 3 ] && [ "$2" -lt "$1" ] && [ "$2" = "$3" ] && ok "OSC A, the second lane: KNOB 2 -4 Mix A (MIXER col 0) $1 -> $2, the cell 1.1 reads '$3': $OUT/cr_prophet_osc_mix.ppm" ||
    bad "OSC A's KNOB 2: $p5m / $(grep -a -m1 '^deep: part 0 page 4' "$PL")"
br=$(python3 - "$OUT/cr_prophet.wav" "$PL" <<'PY'
import re, sys, wave, struct
w = wave.open(sys.argv[1]); fs = w.getframerate(); ch = w.getnchannels()
x = struct.unpack("<%dh" % (w.getnframes() * ch), w.readframes(w.getnframes()))
m = [x[i] for i in range(0, len(x), ch)]
t = [int(v) / 1000 for v in re.findall(r"^expect sound at (\d+) ms", open(sys.argv[2], errors="replace").read(), re.M)]
def bright(t1):              # the 1.2 s before a check: the first difference's energy over the signal's
    s = m[int((t1 - 1.3) * fs):int((t1 - 0.1) * fs)]
    e = sum(v * v for v in s) or 1
    return sum((s[i] - s[i - 1]) ** 2 for i in range(1, len(s))) / e
b0, b1 = bright(t[2]), bright(t[3])
print("%.4f %.4f %d" % (b0, b1, b1 < 0.5 * b0))
PY
)
set -- $br
[ "${3:-0}" = 1 ] && ok "the cutoff down changes the sound: brightness (HF share) $1 -> $2 (the filter closing)" || bad "the sound did not change: $br"
# P5's editor is the owner's rows (the compiled classic layout, docs/LAYOUT-TOOL.md "Step 4"): FILTER's one lane under the
# curve (CUTOFF RES ENV AMT REV, rows 124 .. 223) in the knob colours (CUTOFF blue, RES orange), the amplifier's (ATTACK
# DECAY ..) too; fifteen shots, all different; the pixels themselves are web/test_layout.mjs's (the emulator against the
# module, and the module's rows against the design loaded)
p5col() {   # p5col FILE: orange and blue pixels in rows 124 .. 223 (one lane under a band)
    od -An -tu1 -v -j $((15 + 124 * 240 * 3)) -N$((240 * 100 * 3)) "$1" | tr -s ' \n' '\n\n' | grep -v '^$' |
        awk '{ v[n++] = $1 } END { o = 0; b = 0; for (i = 0; i + 2 < n; i += 3) { if (v[i] > 200 && v[i+1] > 80 && v[i+1] < 170 && v[i+2] < 90) o++; if (v[i] < 80 && v[i+1] < 110 && v[i+2] > 160) b++ } print o, b }'
}
set -- $(p5col "$OUT/cr_prophet_filter.ppm") $(p5col "$OUT/cr_prophet_env1.ppm")
p5diff=$(for a in osc_a osc_mix osc_b filter filter_cut env2 env2p env1 env1p lfo lfo_freq lfo_saw wheel polymod voice; do cksum < "$OUT/cr_prophet_$a.ppm"; done | sort | uniq -d | wc -l | tr -d ' ')
[ "${1:-0}" -gt 60 ] && [ "${2:-0}" -gt 30 ] && [ "${3:-0}" -gt 60 ] && [ "${4:-0}" -gt 30 ] && [ "$p5diff" = 0 ] &&
    ok "the rows' lanes in the knob colours: FILTER $1 orange / $2 blue pixels, the amplifier $3 / $4 (one lane under the band); fifteen different screens: $OUT/cr_prophet_filter.ppm cr_prophet_env1.ppm" ||
    bad "the rows' screens: FILTER $1 / $2, the amplifier $3 / $4, $p5diff pairs alike"
has '^edit: group OSC screen 2 ' "$PL" && has '^edit: group FILT screen 3 ' "$PL" && has '^edit: group ENV screen 2 ' "$PL" &&
    has '^edit: group LFO screen 2 ' "$PL" && has '^edit: group MOD screen 2 ' "$PL" && ! has '^edit: group OSC screen 3 ' "$PL" &&
    ! has '^edit: group FILT screen 4 ' "$PL" &&
    ok "P5's groups, the rows' screens: OSC (OSC A, OSC B), FILT (FILTER, ENV 2 · filter, ENV 2 +), ENV (ENV 1 · amp, ENV 1 +), LFO (LFO, WHEEL), MOD (POLYMOD, VOICE): $OUT/cr_prophet_*.ppm" ||
    bad "P5 groups: $(grep -c '^edit: group' "$PL")"
p5id=$(python3 tools/gen_prophet_factory.py --list | awk '$1 == 4 { print $2 }')
p5fav=$(grep -a '^  favs:' "$PL" | tail -1)
has "^save: dialog part 0 $P5C\* (overwrite)" "$PL" && [ "$(grep -c '^save: choice overwrite' "$PL")" = 1 ] &&
    has "^fav: part 0 $P5C on (tag 16 id $p5id, 1 marked)" "$PL" && ! has '^save: part 0 P5 refused' "$PL" &&
    has "^save: part 0 overwrite slot U001 P5 04 $P5C rc 0" "$PL" && has '^p5: save slot 1 part 0 patch crc ' "$PL" &&
    echo "$p5fav" | grep -q "favs: 1 marked.* : 16:$p5id\$" &&
    [ "$(yellow_in "$OUT/cr_prophet_fav.ppm" 0 0 57600)" -ge 10 ] && [ "$(yellow_in "$OUT/cr_prophet_save.ppm" 0 0 57600)" = 0 ] &&
    ! cmp -s "$OUT/cr_prophet_fav.ppm" "$OUT/cr_prophet_refused.ppm" &&
    ok "SAVE on a P5 part (0.22, the sound pool): the dialog's five entries, on Overwrite (edited); KNOB 4 marks place 04 by Sequential's number ($p5id), the star; OCT+: the edited program saved into U001 over place 04 (its 138 bytes with the record), the mark kept: $OUT/cr_prophet_save.ppm cr_prophet_fav.ppm cr_prophet_refused.ppm (the saved message)" ||
    bad "P5's SAVE dialog: $(grep -a '^save:\|^fav:' "$PL" | tr '\n' ' ') / $p5fav / yellow $(yellow_in "$OUT/cr_prophet_fav.ppm" 0 0 57600)"
has "^picker: open part 0 $P5C" "$PL" && has "^picker: cancel part 0 -> $P5C" "$PL" &&
    ok "EDIT held: the engine picker with P5 among the six, OCT- leaves the sound: $OUT/cr_prophet_epk.ppm" || bad "the picker in the editor"
silent_end cr_prophet

echo "P5, the owner's gesture (3y44, docs/PROPHET.md The crackle): minor + 6 + 9, then minor + 9 struck again over the release; and a unit that does not keep up, modelled"
# Sequential's program 16 where this build ships it (P5_SHIPPED=all: place 16, the owner's), else place 15
p5pos=$(python3 tools/gen_prophet_factory.py --pos 16)
[ "${p5pos:-0}" -ge 2 ] || p5pos=15
P5G=$(p5name "$p5pos")
sed "s/@STEPS@/$((p5pos - 1))/" "$S/cr_prophet_crackle.txt" > "$OUT/cr_p5_crackle.txt"
p5voices() { grep -a '^  part 0: P5' "$1" | sed -n 's/.*voices \([0-9]*\),.*/\1/p' | tr '\n' ' '; }
p5sheds() { grep -a 'overload sheds' "$1" | tail -1 | sed -n 's/.*overload sheds \([0-9]*\)).*/\1/p'; }
"$EMU" --headless --script "$OUT/cr_p5_crackle.txt" --wav "$OUT/cr_p5_crackle.wav" >"$OUT/cr_p5_crackle.log" 2>&1 &&
    ok "cr_prophet_crackle: exit 0 ($(grep -c '^expect .*: ok' "$OUT/cr_p5_crackle.log") expectations met)" || bad "cr_prophet_crackle: see $OUT/cr_p5_crackle.log"
PL="$OUT/cr_p5_crackle.log"
has "^sound: part 0 pos $p5pos $P5G" "$PL" && ok "the chord part on P5 place $p5pos, $P5G" || bad "not P5 place $p5pos: $(grep -a '^sound: part 0' "$PL" | tail -1)"
has 'chord Dm6 9 sounding 1 notes 62 65 69 71 76' "$PL" && has 'chord Dm9 sounding 1 notes 62 65 69 76' "$PL" &&
    [ "$(p5voices "$PL")" = "0 5 4 4 0 " ] && [ "$(p5sheds "$PL")" = 0 ] &&
    ok "two cores (0.22's default): Dm6/9 has its five voices (eight are the chord part's: no voice is stolen for the fifth note), Dm9 struck twice over the release its four, nothing shed" ||
    bad "the gesture's voices: $(p5voices "$PL")/ sheds $(p5sheds "$PL") / $(grep -a -c 'chord Dm' "$PL") chord lines"
# .. and with ONE core (EMU_C1=nostart: core 1 does not answer at power-on, as Options > Dual Core Off plays): four
# voices, the fifth note takes one (the owner's unit of 3y44 had four)
EMU_C1=nostart "$EMU" --headless --script "$OUT/cr_p5_crackle.txt" --wav "$OUT/cr_p5_crackle_one.wav" >"$OUT/cr_p5_crackle_one.log" 2>&1 ||
    bad "cr_prophet_crackle with one core: see $OUT/cr_p5_crackle_one.log"
[ "$(p5voices "$OUT/cr_p5_crackle_one.log")" = "0 4 4 4 0 " ] && [ "$(p5sheds "$OUT/cr_p5_crackle_one.log")" = 0 ] &&
    ok "one core: Dm6/9 (five notes) and Dm9 struck twice over the release: four voices each time, never more, nothing shed" ||
    bad "the gesture's voices with one core: $(p5voices "$OUT/cr_p5_crackle_one.log")/ sheds $(p5sheds "$OUT/cr_p5_crackle_one.log")"
wc=$(python3 tools/emu/wavclicks.py "$OUT/cr_p5_crackle_one.wav" --from 3.4 --both-sides | tail -1)
echo "$wc" | grep -q ': 0 jumps > 0.5 FS, 0 silent holes mid-sound, 0 clicks' && ok "no crackle with one core either: $wc" || bad "cr_prophet_crackle, one core: $wc"
# (from 3.4 s: after the first chord's attack out of silence; a burst is a click when it stands 12 times above the
# sound before it AND the sound after it, as for CZ-1's re-strikes)
wc=$(python3 tools/emu/wavclicks.py "$OUT/cr_p5_crackle.wav" --from 3.4 --both-sides | tail -1)
echo "$wc" | grep -q ': 0 jumps > 0.5 FS, 0 silent holes mid-sound, 0 clicks' && ok "no crackle on the host: $wc" || bad "cr_prophet_crackle: $wc"
silent_end cr_p5_crackle
# a unit that does not keep up with four voices (README "Overload"): audio.c's shed fades one voice of each chord
# (two halves in a row above 85 %), and that is all: three voices, three sheds, no voice shed and struck again
EMU_C1=nostart EMU_SHED_OVER=4 "$EMU" --headless --script "$OUT/cr_p5_crackle.txt" --wav "$OUT/cr_p5_crackle_over.wav" >"$OUT/cr_p5_crackle_over.log" 2>&1 ||
    bad "cr_prophet_crackle with EMU_SHED_OVER=4: see $OUT/cr_p5_crackle_over.log"
PL="$OUT/cr_p5_crackle_over.log"
gu=$(grep -a 'voices: given up' "$PL" | tail -1 | sed -n 's/.*given up \([0-9]*\).*/\1/p')
[ "$(p5voices "$PL")" = "0 3 3 3 0 " ] && [ "$(p5sheds "$PL")" = 3 ] && [ "${gu:-99}" -le 6 ] &&
    LC_ALL=C grep -aq '^  shed: halves over 85 % 6, last voice 1[0-9A-F]* why 2 ' "$PL" &&
    ok "overload modelled (one core, four voices do not fit): three voices a chord, 3 voices shed in 6 halves over 85 %, each a held voice; $gu voices faded in all (no circle)" ||
    bad "the overload model: voices $(p5voices "$PL")/ sheds $(p5sheds "$PL") / given up ${gu:-?} / $(LC_ALL=C grep -a '^  shed:' "$PL" | tail -1)"
wc=$(python3 tools/emu/wavclicks.py "$OUT/cr_p5_crackle_over.wav" --from 3.4 --both-sides | tail -1)
echo "$wc" | grep -q ': 0 jumps > 0.5 FS, 0 silent holes mid-sound, 0 clicks' && ok "the shed's fades are no crackle: $wc" || bad "cr_prophet_crackle, shed: $wc"
silent_end cr_p5_crackle_over

echo "P5 on two cores (0.22, 3y46, docs/PROPHET.md Voices): eight voices on the chord part, four rendered by each core; a P5 bass gives way to the chord down to its one voice; one core: four, and a P5 bass one"
# a UNISON program with a stack of four for the bass part: Sequential's 111 (LAGOON BASS) where the list ships it
p5b=$(python3 tools/gen_prophet_factory.py --pos 111)
if [ "${p5b:-0}" -ge 1 ]; then
    sed "s/@BASS@/$((p5b + 1))/" "$S/cr_prophet_eight.txt" > "$OUT/cr_p5_eight.txt"
    pv() { grep -a "^  part $2: P5" "$1" | sed -n 's/.*voices \([0-9]*\),.*/\1/p' | tr '\n' ' '; }
    EMU_STAGES=1 "$EMU" --headless --script "$OUT/cr_p5_eight.txt" --wav "$OUT/cr_p5_eight.wav" >"$OUT/cr_p5_eight.log" 2>&1 &&
        ok "cr_prophet_eight: exit 0" || bad "cr_prophet_eight: see $OUT/cr_p5_eight.log"
    PL="$OUT/cr_p5_eight.log"
    [ "$(pv "$PL" 0)" = "0 8 8 0 0 6 7 0 " ] && [ "$(pv "$PL" 1)" = "4 2 1 0 " ] && [ "$(p5sheds "$PL")" = 0 ] &&
        LC_ALL=C grep -aq '^cpu: stages .* c1 ok, jobs [1-9]' "$PL" &&
        ok "a fresh unit (two cores): eight MIDI notes sound eight P5 voices, a ninth takes one (eight still); the bass's UNISON stack: four alone, two under a chord of six, one under a chord of eight, which then sounds seven (3y53: the bass keeps one voice); nothing shed" ||
        bad "P5 on two cores: the chord part's voices $(pv "$PL" 0)/ the bass part's $(pv "$PL" 1)/ sheds $(p5sheds "$PL")"
    LC_ALL=C grep -a '^cpu: stages' "$PL" | cut -c6- | sed 's/^/      /'
    silent_end cr_p5_eight
    EMU_C1=thread "$EMU" --headless --script "$OUT/cr_p5_eight.txt" --wav "$OUT/cr_p5_eight_thread.wav" >"$OUT/cr_p5_eight_thread.log" 2>&1
    "$EMU" --headless --script "$OUT/cr_p5_eight.txt" --wav "$OUT/cr_p5_eight_inline.wav" >"$OUT/cr_p5_eight_inline.log" 2>&1
    [ -s "$OUT/cr_p5_eight_inline.wav" ] && cmp -s "$OUT/cr_p5_eight_inline.wav" "$OUT/cr_p5_eight_thread.wav" &&
        [ "$(pv "$OUT/cr_p5_eight_thread.log" 0)" = "0 8 8 0 0 6 7 0 " ] &&
        ok "EMU_C1=thread: core 1's share of the chord's voices and the bass part on a second thread: the WAV is the inline job's, bit for bit" ||
        bad "EMU_C1=thread, P5's chord voices on two threads: the WAV differs from the inline job's"
    EMU_C1=nostart "$EMU" --headless --script "$OUT/cr_p5_eight.txt" --wav "$OUT/cr_p5_eight_one.wav" >"$OUT/cr_p5_eight_one.log" 2>&1
    [ "$(pv "$OUT/cr_p5_eight_one.log" 0)" = "0 4 4 0 0 3 3 0 " ] && [ "$(pv "$OUT/cr_p5_eight_one.log" 1)" = "1 1 1 0 " ] &&
        ok "one core (EMU_C1=nostart): the chord part four voices; the UNISON bass ONE voice, and under it the chord three (four P5 voices on the one core)" ||
        bad "P5 on one core: the chord part's voices $(pv "$OUT/cr_p5_eight_one.log" 0)/ the bass part's $(pv "$OUT/cr_p5_eight_one.log" 1)"
    EMU_C1=hang "$EMU" --headless --script "$OUT/cr_p5_eight.txt" --wav "$OUT/cr_p5_eight_hang.wav" >"$OUT/cr_p5_eight_hang.log" 2>&1
    # (the give-up turns Dual Core Off and the settings save it once the unit is quiet after the last release: one
    # erase, played as 45 ms of silence about 12.4 s in (docs/DUALCORE.md "The persistent fallback"; a fresh unit is
    # On since 0.22): the WAVs are compared up to 12 s, and the save is asserted)
    cmp -s -n 2116844 "$OUT/cr_p5_eight_hang.wav" "$OUT/cr_p5_eight_one.wav" &&
        LC_ALL=C grep -aq '^  flash: .* settings saves 1 ' "$OUT/cr_p5_eight_hang.log" &&
        LC_ALL=C grep -aq '^  settings: .* dualcore 0 ' "$OUT/cr_p5_eight_hang.log" &&
        ok "core 1 never answering a job (EMU_C1=hang): given up in the first block, one core's rules and one core's WAV (the first 12 s), Dual Core Off saved" ||
        bad "EMU_C1=hang on P5: the WAV differs from one core's in the first 12 s, or Dual Core Off was not saved"
else
    bad "P5 on two cores: the shipped list has no program 111 (LAGOON BASS): name another UNISON program with a stack of four here"
fi
# 3y50: the reverb on core 1 (fx.c mix_fx_split): eight P5 voices with the three sends up, inline and on a thread
FX8="$OUT/cr_p5_eight_fx"
EMU_STAGES=1 "$EMU" --headless --script "$S/cr_prophet_eight_fx.txt" --wav "${FX8}_inline.wav" >"${FX8}_inline.log" 2>&1
EMU_C1=thread "$EMU" --headless --script "$S/cr_prophet_eight_fx.txt" --wav "${FX8}_thread.wav" >"${FX8}_thread.log" 2>&1
rj=$(LC_ALL=C grep -a '^cpu: stages' "${FX8}_inline.log" | sed -n 's/.*reverb jobs \([0-9]*\).*/\1/p')
[ -s "${FX8}_inline.wav" ] && cmp -s "${FX8}_inline.wav" "${FX8}_thread.wav" && [ "${rj:-0}" -ge 1000 ] &&
    [ "$(grep -a "^  part 0: P5" "${FX8}_inline.log" | sed -n 's/.*voices \([0-9]*\),.*/\1/p' | tr '\n' ' ')" = "8 0 " ] &&
    ok "eight P5 voices, three sends up: the reverb on core 1 ($rj blocks), the WAV inline and with EMU_C1=thread the same, bit for bit" ||
    bad "the reverb on core 1: ${rj:-no} reverb jobs, or the inline and the thread WAVs differ (${FX8}_*.log)"
silent_end cr_p5_eight_fx_inline

echo "the VA chord's voices on two cores (3y177, docs/VA.md \"Two cores\"): eight MORPH PAD voices over PUNCH BASS, shared out between the cores; one core: the chord seven under the bass"
VA8="$OUT/cr_va_eight"
vv() { grep -a "^  part $2: VA" "$1" | sed -n 's/.*voices \([0-9]*\),.*/\1/p' | tr '\n' ' '; }
EMU_STAGES=1 "$EMU" --headless --script "$S/cr_va_eight.txt" --wav "${VA8}_inline.wav" >"${VA8}_inline.log" 2>&1 &&
    ok "cr_va_eight: exit 0" || bad "cr_va_eight: see ${VA8}_inline.log"
st=$(LC_ALL=C grep -a '^cpu: stages' "${VA8}_inline.log")
p0=$(echo "$st" | sed -n 's/.* part0 \([0-9]*\) .*/\1/p'); p1=$(echo "$st" | sed -n 's/.* part1 \([0-9]*\) .*/\1/p')
[ "$(vv "${VA8}_inline.log" 0)" = "0 8 8 8 0 " ] && [ "$(vv "${VA8}_inline.log" 1)" = "0 0 1 1 0 " ] &&
    echo "$st" | grep -q 'c1 ok, jobs [1-9]' && [ "${p0:-0}" -ge 1 ] && [ "$((2 * ${p1:-0}))" -ge "${p0:-0}" ] &&
    ok "two cores: eight VA voices and the bass; core 1's job (the bass and its share of the chord) averages $p1 us a half against core 0's $p0 (at least half of it: the chord is shared)" ||
    bad "the VA chord on two cores: the chord's voices $(vv "${VA8}_inline.log" 0)/ the bass's $(vv "${VA8}_inline.log" 1)/ part0 ${p0:-?} part1 ${p1:-?} ($st)"
echo "$st" | cut -c6- | sed 's/^/      /'
silent_end cr_va_eight_inline
EMU_C1=thread "$EMU" --headless --script "$S/cr_va_eight.txt" --wav "${VA8}_thread.wav" >"${VA8}_thread.log" 2>&1
[ -s "${VA8}_inline.wav" ] && cmp -s "${VA8}_inline.wav" "${VA8}_thread.wav" && [ "$(vv "${VA8}_thread.log" 0)" = "0 8 8 8 0 " ] &&
    ok "EMU_C1=thread: core 1's share of the VA chord (its mid and its SPREAD side) and the bass part on a second thread: the WAV is the inline job's, bit for bit" ||
    bad "EMU_C1=thread, the VA chord's voices on two threads: the WAV differs from the inline job's"
EMU_C1=nostart "$EMU" --headless --script "$S/cr_va_eight.txt" --wav "${VA8}_one.wav" >"${VA8}_one.log" 2>&1
[ "$(vv "${VA8}_one.log" 0)" = "0 8 7 7 0 " ] && [ "$(vv "${VA8}_one.log" 1)" = "0 0 1 1 0 " ] &&
    ok "one core (EMU_C1=nostart): the chord eight alone, seven under the bass (one core's shared budget), every voice on core 0" ||
    bad "the VA chord on one core: the chord part's voices $(vv "${VA8}_one.log" 0)/ the bass part's $(vv "${VA8}_one.log" 1)"
EMU_STAGES=1 EMU_C1=hang "$EMU" --headless --script "$S/cr_va_eight.txt" --wav "${VA8}_hang.wav" >"${VA8}_hang.log" 2>&1
# (the give-up's Dual Core Off is saved once the unit is quiet after the release: its erase, played as silence, is
# about 7.95 s in: the WAVs are compared up to 7.9 s)
cmp -s -n 1393604 "${VA8}_hang.wav" "${VA8}_one.wav" && LC_ALL=C grep -aq 'c1 gave up' "${VA8}_hang.log" &&
    LC_ALL=C grep -aq '^  settings: .* dualcore 0 ' "${VA8}_hang.log" &&
    ok "core 1 never answering a job under the VA chord (EMU_C1=hang): given up in the first block, core 0 renders the job itself, one core's WAV (the first 7.9 s), Dual Core Off" ||
    bad "EMU_C1=hang on the VA chord: the WAV differs from one core's in the first 7.9 s, or core 1 was not given up"

echo "FM6's chord voices on two cores (3y187, docs/FM6.md \"Two cores\"): eight TINE EP voices over DEEP SUB (the power-on sounds), shared out between the cores; one core: the same, bit for bit"
F8="$OUT/cr_fm6_eight"
fv() { grep -a "^  part $2: " "$1" | sed -n 's/.*voices \([0-9]*\),.*/\1/p' | tr '\n' ' '; }
EMU_STAGES=1 "$EMU" --headless --script "$S/cr_fm6_eight.txt" --wav "${F8}_inline.wav" >"${F8}_inline.log" 2>&1 &&
    ok "cr_fm6_eight: exit 0" || bad "cr_fm6_eight: see ${F8}_inline.log"
st=$(LC_ALL=C grep -a '^cpu: stages' "${F8}_inline.log")
p0=$(echo "$st" | sed -n 's/.* part0 \([0-9]*\) .*/\1/p'); p1=$(echo "$st" | sed -n 's/.* part1 \([0-9]*\) .*/\1/p')
grep -aq '^  part 0: FM6 / TINE EP' "${F8}_inline.log" && grep -aq '^  part 1: VA / DEEP SUB' "${F8}_inline.log" &&
    [ "$(fv "${F8}_inline.log" 0)" = "0 8 8 8 0 " ] && [ "$(fv "${F8}_inline.log" 1)" = "0 0 1 1 0 " ] &&
    echo "$st" | grep -q 'c1 ok, jobs [1-9]' && [ "${p0:-0}" -ge 1 ] && [ "$((2 * ${p1:-0}))" -ge "${p0:-0}" ] &&
    ok "two cores: eight FM6 voices and the bass; core 1's job (the bass and its share of the chord) averages $p1 us a half against core 0's $p0 (at least half of it: the chord is shared)" ||
    bad "FM6's chord on two cores: the chord's voices $(fv "${F8}_inline.log" 0)/ the bass's $(fv "${F8}_inline.log" 1)/ part0 ${p0:-?} part1 ${p1:-?} ($st)"
echo "$st" | cut -c6- | sed 's/^/      /'
silent_end cr_fm6_eight_inline
EMU_C1=thread "$EMU" --headless --script "$S/cr_fm6_eight.txt" --wav "${F8}_thread.wav" >"${F8}_thread.log" 2>&1
[ -s "${F8}_inline.wav" ] && cmp -s "${F8}_inline.wav" "${F8}_thread.wav" &&
    ok "EMU_C1=thread: core 1's share of the FM6 chord (its own scratch) and the bass part on a second thread: the WAV is the inline job's, bit for bit" ||
    bad "EMU_C1=thread, FM6's chord voices on two threads: the WAV differs from the inline job's"
EMU_C1=nostart "$EMU" --headless --script "$S/cr_fm6_eight.txt" --wav "${F8}_one.wav" >"${F8}_one.log" 2>&1
[ "$(fv "${F8}_one.log" 0)" = "0 8 8 8 0 " ] && cmp -s "${F8}_inline.wav" "${F8}_one.wav" &&
    ok "one core (EMU_C1=nostart): the same eight voices and the bass (an FM6 voice is one budget unit), the WAV the two cores' bit for bit" ||
    bad "FM6's chord on one core: the voices $(fv "${F8}_one.log" 0)/ or the WAV differs from the two cores'"
EMU_STAGES=1 EMU_C1=hang "$EMU" --headless --script "$S/cr_fm6_eight.txt" --wav "${F8}_hang.wav" >"${F8}_hang.log" 2>&1
# (the give-up's Dual Core Off is saved once the unit is quiet: its erase, played as silence, about 6.5 s in)
cmp -s -n 1128964 "${F8}_hang.wav" "${F8}_one.wav" && LC_ALL=C grep -aq 'c1 gave up' "${F8}_hang.log" &&
    LC_ALL=C grep -aq '^  settings: .* dualcore 0 ' "${F8}_hang.log" &&
    ok "core 1 never answering a job under the FM6 chord (EMU_C1=hang): given up in the first block, one core's WAV (the first 6.4 s), Dual Core Off" ||
    bad "EMU_C1=hang on the FM6 chord: the WAV differs from one core's in the first 6.4 s, or core 1 was not given up"

echo "P5, the click at a short release's end (3y44): the part's DC blocker discharges after the last voice, it is not cut"
p5pos=$(python3 tools/gen_prophet_factory.py --pos 168)          # PERC ORGAN: a release of 20 ms, in the tree's list
if [ "${p5pos:-0}" -ge 2 ]; then
    P5G=$(p5name "$p5pos")
    sed "s/@STEPS@/$((p5pos - 1))/" "$S/cr_prophet_relclick.txt" > "$OUT/cr_p5_relclick.txt"
    "$EMU" --headless --script "$OUT/cr_p5_relclick.txt" --wav "$OUT/cr_p5_relclick.wav" >"$OUT/cr_p5_relclick.log" 2>&1 &&
        ok "cr_prophet_relclick: exit 0" || bad "cr_prophet_relclick: see $OUT/cr_p5_relclick.log"
    has "^sound: part 0 pos $p5pos $P5G" "$OUT/cr_p5_relclick.log" && ok "the chord part on P5 place $p5pos, $P5G" ||
        bad "not P5 place $p5pos: $(grep -a '^sound: part 0' "$OUT/cr_p5_relclick.log" | tail -1)"
    # (four stabs, let go at 3.36, 4.36, 5.36 and 6.36 s; 74eae04: a click 22 ms after each, into the silence: 4 here)
    wc=$(python3 tools/emu/wavclicks.py "$OUT/cr_p5_relclick.wav" --from 3.3 --both-sides | tail -1)
    echo "$wc" | grep -q ': 0 jumps > 0.5 FS, 0 silent holes mid-sound, 0 clicks' && ok "four stabs, no click after a release: $wc" ||
        bad "cr_prophet_relclick: $wc"
    silent_end cr_p5_relclick
else
    bad "cr_prophet_relclick: the shipped list has no program 168 (PERC ORGAN): name another with a short release here"
fi

echo "CZ-1's pops (docs/CZ1.md Pops): a chord struck again while it sounds, chord changes with common tones, the CZ-1 bass"
rm -f "$OUT"/cz_pop*
"$EMU" --headless --script "$S/cr_cz_pop.txt" --wav "$OUT/cz_pop.wav" >"$OUT/cz_pop.log" 2>&1 && ok "cr_cz_pop: exit 0" || bad "cr_cz_pop: see $OUT/cz_pop.log"
PL="$OUT/cz_pop.log"
has 'part 0: CZ-1 / STRINGS 2' "$PL" && has 'part 1: CZ-1 / SYNTH.BASS' "$PL" && ok "the chord part on CZ-1 STRINGS 2, the bass on CZ-1 SYNTH.BASS" ||
    bad "cr_cz_pop: $(grep -m2 'part [01]:' "$PL" | tr '\n' ' ')"
has '^expect sound .*: ok' "$PL" && ok "the chord sounds" || bad "cr_cz_pop: no sound"
# (from 3.4 s: after the first chord's attack, whose bright bass onset wavclicks reads as high-frequency bursts in any
# build; the three re-strikes from 4.46 s and the two chord changes from 6.3 s are the pops' places)
wc=$(python3 tools/emu/wavclicks.py "$OUT/cz_pop.wav" --from 3.4 | tail -1)
echo "$wc" | grep -q ': 0 jumps > 0.5 FS, 0 silent holes mid-sound, 0 clicks' && ok "cr_cz_pop: $wc" || bad "cr_cz_pop: $wc"
silent_end cz_pop

echo "CZ-1 BRASS 1 with the CZ-1 bass in Output Clean, struck again 60 and 20 ms after its release, then the unit's arp (docs/CZ1.md Pops, the second round)"
rm -f "$OUT"/cz_brass*
"$EMU" --headless --script "$S/cr_cz_brass.txt" --wav "$OUT/cz_brass.wav" >"$OUT/cz_brass.log" 2>&1 && ok "cr_cz_brass: exit 0" || bad "cr_cz_brass: see $OUT/cz_brass.log"
PL="$OUT/cz_brass.log"
has 'part 0: CZ-1 / BRASS 1' "$PL" && has 'part 1: CZ-1 / SYNTH.BASS' "$PL" && ok "the chord part on CZ-1 BRASS 1, the bass on CZ-1 SYNTH.BASS" ||
    bad "cr_cz_brass: $(grep -m2 'part [01]:' "$PL" | tr '\n' ' ')"
LC_ALL=C grep -aq "master: output Clean (chain 1, want 1, fade 0)" "$PL" && ok "Output Clean" || bad "cr_cz_brass: the output is not Clean: $(LC_ALL=C grep -a -m1 "master: output" "$PL")"
[ "$(grep -c '^expect sound .*: ok' "$PL")" -eq 2 ] && ok "the chord sounds, and the arp" || bad "cr_cz_brass: no sound"
has 'perform 1 mode 2 bass 1' "$PL" && ok "Arpeggiate with the bass on" || bad "cr_cz_brass: $(grep -m1 'perform' "$PL")"
# (the four re-strikes, 3.4 .. 9.5 s: a burst is a click when it stands 12 times above the sound before it AND the
# sound after it; wavclicks' one-sided measure counts the first bright window of the two 60 ms re-attacks, 2 in
# 778a6ad as now: BRASS 1's DCW is still open when its DCA comes back, the tone's own edges, docs/CZ1.md)
wc=$(python3 tools/emu/wavclicks.py "$OUT/cz_brass.wav" --from 3.4 --to 9.5 --both-sides | tail -1)
echo "$wc" | grep -q ': 0 jumps > 0.5 FS, 0 silent holes mid-sound, 0 clicks' && ok "cr_cz_brass, the chord struck again: $wc" || bad "cr_cz_brass: $wc"
# (the arp: every note is an attack of its own, so no click count; no jump, no hole)
wc=$(python3 tools/emu/wavclicks.py "$OUT/cz_brass.wav" --from 9.5 | tail -1)
echo "$wc" | grep -q ': 0 jumps > 0.5 FS, 0 silent holes mid-sound' && ok "cr_cz_brass, the arp: no jump, no hole" || bad "cr_cz_brass, the arp: $wc"
silent_end cz_brass

echo "FM TONE (code name QUAD): the engine picker, the pool, a chord, the editor's screens (docs/QUAD.md)"
run cr_quad --wav "$OUT/cr_quad.wav"
QL="$OUT/cr_quad.log"
has '^engine: part 0 -> FM TONE' "$QL" && has 'part 0: FM TONE / EP' "$QL" && ok "EDIT held + E4 (the second white root): FM TONE, 01 EP" ||
    bad "not FM TONE from the picker: $(grep -m1 '^engine:' "$QL")"
has '^popup: part 0 03 / BASS / FM TONE · 03/16$' "$QL" && ok "PRESETS +2: the pool, FM TONE · 03/16 (16 presets; INIT, 00, is not counted): $OUT/cr_quad_pool.ppm" ||
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

echo "FM TONE's LFO DEST: the Digitone's list in its order (2026-10-14, eng_quad.c QUAD_DEST_ORDER)"
run cr_quad_dest
QL="$OUT/cr_quad_dest.log"
dl=$(grep '^deep: part 0 page 16 LFO 2 col 3 DEST ' "$QL" | grep -v ' DEST 57 -> 57 ' | sed 's/.* -> [0-9]* (\([^)]*\)).*/\1/' | tr '\n' '|')
want="L1SPD|L1MUL|L1FAD|L1WAV|L1SPH|L1MOD|L1DEP|ALGO|RAT C|RAT A|RAT B|HARM|DTUN|FDBK|MIX|A ATK|A DEC|A END|A LEV|B ATK|B DEC|B END|B LEV|A DLY|B DLY|PITCH|P AB2|R ALL|ABLEV|ABATK|ABDEC|ABEND|ABDLY|FREQ|RESO|FENV|F ATK|F DEC|F SUS|F REL|BASE|WIDTH|AMP A|AMP D|AMP S|AMP R|DRIVE|PAN|LEVEL|REV|DEL|CHO|OFS C|OFS A|OFSB1|OFSB2|F DLY|NONE|"
[ "$dl" = "$want" ] && has '^deep: part 0 page 16 LFO 2 col 3 DEST 57 -> 0 (NONE)' "$QL" &&
    ok "LFO 2 DEST, KNOB 4 a detent at a time: NONE, LFO 1's SPEED .. DEPTH, ALGO .. F DLY (the Digitone's order; the end holds), -57: NONE: $OUT/cr_quad_dest_l1spd.ppm $OUT/cr_quad_dest_rall.ppm $OUT/cr_quad_dest_fdly.ppm" ||
    bad "LFO 2 DEST order: $dl"

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
# (--floor 0.003: the notes end where their own sound does. At wavpitch's 0.001 the delay's last repeats, which hover
# at that level, were a fourth "note" of 0.13 s in one build of the effects and not in the next. --left: the centroid
# is read beside the chorus's comb (the default send). The two channels' average has both of the stereo chorus's taps
# in it since 0.19 and the two notes' centroids came out 4.6 % apart there (5.8 % with 0.18's one tap: the comb's
# moment, not the ratio); one channel has one tap: 440 against 352 Hz)
rbn=$(python3 tools/emu/wavpitch.py "$OUT/cr_quad_ratiob.wav" --floor 0.003 --left)
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
differ cr_quad_ratiob_b1 cr_quad_ratiob_b2 "GLO held (the editor's SHIFT): KNOB 4 steps B1, the denominator 2.00 (B2 kept): $OUT/cr_quad_ratiob_b2.ppm"
silent_end cr_quad_ratiob

echo "FM TONE's release INF: AMP REL 127 reads INF, a released note sounds on; REL 19 decays; PANIC ends a held one"
run cr_quad_inf --wav "$OUT/cr_quad_inf.wav"
QL="$OUT/cr_quad_inf.log"
has '^deep: part 0 page 12 AMP col 3 REL 14 -> 127 (INF)' "$QL" && has '^deep: part 0 page 12 AMP col 3 REL 127 -> 19 (19)' "$QL" &&
    ok "AMP: KNOB 4 to the top shows INF, back down a number (19: AMP REL 0..126 + INF, F_INTI; the script's \"knobs quick\": 6 a detent, the range / 20): $OUT/cr_quad_inf_amp.ppm" ||
    bad "AMP REL INF text: $(grep '^deep:' "$QL" | head -2 | tr '\n' ' ')"
qv() { grep "part 0:" "$QL" | sed -n "${1}p" | sed -n 's/.*voices \([0-9]*\),.*/\1/p'; }
[ "$(qv 1)" = 1 ] && [ "$(qv 2)" = 0 ] && [ "$(qv 3)" = 1 ] && [ "$(qv 4)" = 0 ] &&
    ok "voices: INF held 1.5 s after the release (1), REL 19 ended (0), INF held again (1), PANIC ended it (0)" ||
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
    ok "the WAV: INF 1.5 s after the release RMS ${2} (held: ${1} before), REL 19 RMS ${4} (${3} before)" ||
    bad "the WAV: INF ${1:-?} -> ${2:-?}, REL 19 ${3:-?} -> ${4:-?} (want held, then silent)"
silent_end cr_quad_inf

echo "SAFE MODE: the boot guard (firmware/src/cr_bootguard.h)"
SF="$OUT/cr_safe_flash.bin"
rm -f "$SF"
"$EMU" --headless --flash "$SF" --script "$S/persist_set.txt" >"$OUT/cr_safe_data.log" 2>&1   # data: 137 BPM, Advanced
grep -q 'settings saves 1 ' "$OUT/cr_safe_data.log" && ok "a flash with data (persist_set: 137 BPM saved)" || bad "no data saved"
"$EMU" --headless --flash "$SF" --script "$S/cr_safe_sound.txt" >"$OUT/cr_safe_sound.log" 2>&1   # .. and a sound in the pool
pool_before=$(python3 tools/emu/pool_image.py pool "$SF")
has '^save: part 0 slot U001 name TINE EP' "$OUT/cr_safe_sound.log" && [ -n "$pool_before" ] &&
    ok "a user sound in the pool too (U01 TINE EP: $(echo "$pool_before" | head -1))" || bad "no sound saved: $(grep -m1 '^save:' "$OUT/cr_safe_sound.log")"
cp "$SF" "$OUT/cr_safe_flash_before.bin"
run cr_safe --flash "$SF" --boot-fail 1 --reset-reason wdt --boot-stage 13 --wav "$OUT/cr_safe.wav"
L="$OUT/cr_safe.log"
has '^boot: safe reset wdt failed 2 pending 1 counted 1 prev_stage 13 (fm6 bank)' "$L" &&
    ok "a crash-type reset with one failed boot before: SAFE MODE, breadcrumb 13 (fm6 bank)" || bad "not SAFE: $(grep -m1 '^boot:' "$L")"
has 'record: defaults' "$L" && has 'bpm 120 ' "$L" && ! has 'bpm 137' "$L" && has ' style 0 ' "$L" && ! has ' style 1 ' "$L" &&
    has ' style 1 ' "$OUT/cr_safe_data.log" &&
    ok "the settings record not loaded (defaults: 120 BPM and Play Style Simple, not the saved 137 and Advanced)" || bad "safe mode loaded the record"
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
[ $st = 0 ] && has '^safe: flash data erased, 76 sectors, 0 failed: reboot' "$OUT/cr_safe_erase.log" &&
    has '^reboot: guard failed 0 pending 0' "$OUT/cr_safe_erase.log" &&
    ok "OCT+ twice: 76 data sectors erased (36 + the CZ-1's 18 + FM TONE's 2 + the session's 2 + the favourites' 2 + the sound pool's 16), reboot with the guard clear" || bad "erase: exit $st, $(grep -m1 'safe:\|reboot' "$OUT/cr_safe_erase.log")"
ff=$(python3 -c "
import sys; d = open(sys.argv[1], 'rb').read()
r = [(0x97000, 9), (0xA0000, 40), (0xC8000, 20), (0xDC000, 4), (0xFC000, 3)]   # (0xB4000: the session, 0xB6000 the favourites, 0xB8000.. the pool)
print(int(all(d[a:a + n * 4096] == b'\xff' * (n * 4096) for a, n in r) and d[:0x93000] == open(sys.argv[2], 'rb').read()[:0x93000]))" "$SF" "$OUT/cr_safe_flash_before.bin")
[ "$ff" = 1 ] && ok "the data regions read erased, the firmware area as before" || bad "flash after the erase"
[ -z "$(python3 tools/emu/pool_image.py pool "$SF")" ] && ok "the sound pool holds no sound after it (0.24: the pool was left out until 0.24-beta2)" ||
    bad "the pool after the erase: $(python3 tools/emu/pool_image.py pool "$SF" | head -1)"
"$EMU" --headless --flash "$SF" --script "$S/persist_check.txt" >"$OUT/cr_safe_after.log" 2>&1
has '^boot: normal reset power-on failed 0' "$OUT/cr_safe_after.log" && has 'bpm 120 ' "$OUT/cr_safe_after.log" &&
    has ' style 0 ' "$OUT/cr_safe_after.log" && ! has ' style 1 ' "$OUT/cr_safe_after.log" &&
    ok "the next power-on: NORMAL on the erased flash (120 BPM, Play Style Simple: Advanced was saved before)" || bad "after the erase: $(grep -m1 '^boot:' "$OUT/cr_safe_after.log")"
"$EMU" --headless --script "$S/cr_safe.txt" --boot-fail 1 --reset-reason poweron >"$OUT/cr_safe_pwr.log" 2>&1
has '^boot: normal reset power-on failed 0 pending 1 counted 0' "$OUT/cr_safe_pwr.log" &&
    ok "a power-on after failed boots: NORMAL, the guard clear" || bad "power-on: $(grep -m1 '^boot:' "$OUT/cr_safe_pwr.log")"
"$EMU" --headless --script "$S/cr_safe.txt" --boot-fail 3 --reset-reason wdt >"$OUT/cr_safe_uboot.log" 2>&1
st=$?
[ $st = 3 ] && has '^boot: UBOOT (ROM boot): reset wdt' "$OUT/cr_safe_uboot.log" &&
    ok "SAFE MODE crashed twice more (failed 4): UBOOT" || bad "uboot: exit $st"

echo "Options > Dual Core (settings v12; 0.22: On on a fresh unit: c1 ok, each part its budget); Off is the option, at the next power-on (c1 off, the shared budget)"
c1line() { LC_ALL=C grep -a -o "line3 '[^']*'" "$1" | tail -1; }
c1gu() { LC_ALL=C grep -a 'voices: given up' "$1" | tail -1 | sed -n 's/.*given up \([0-9]*\).*/\1/p'; }
EMU_UI_LOG=0 "$EMU" --headless --script "$S/cr_dualcore.txt" --wav "$OUT/cr_dualcore_fresh.wav" >"$OUT/cr_dualcore_fresh.log" 2>&1
l=$(c1line "$OUT/cr_dualcore_fresh.log"); g=$(c1gu "$OUT/cr_dualcore_fresh.log")
echo "$l" | grep -q "c1 ok [1-9]" && [ "${g:-x}" = 0 ] && ok "a fresh unit (0.22): $l, the 8-note chord and the bass all sound (none given up)" ||
    bad "a fresh unit: ${l:-none}, ${g:-?} given up"
[ -s "$OUT/cr_dualcore_geek.ppm" ] && mv "$OUT/cr_dualcore_geek.ppm" "$OUT/cr_dualcore_geek_fresh.ppm" || bad "no cr_dualcore_geek.ppm"
DCOFF="$OUT/cr_dualcore_off_flash.bin"        # a flash whose settings say Dual Core Off, by the Options path
rm -f "$DCOFF"
EMU_UI_LOG=1 "$EMU" --headless --flash "$DCOFF" --script "$S/cr_dualcore_opt.txt" >"$OUT/cr_dualcore_seed_off.log" 2>&1
LC_ALL=C grep -aq "item 'Dual Core' value 'On'" "$OUT/cr_dualcore_seed_off.log" &&
    ok "a fresh unit's Options > Dual Core: On" || bad "a fresh unit's Dual Core is not On"
LC_ALL=C grep -aq "item 'Dual Core' value 'Off .* restart'" "$OUT/cr_dualcore_seed_off.log" &&
    ok "KNOB 1 -1: Off, the value says restart ($OUT/cr_dualcore_opt.ppm)" || bad "Dual Core Off: no restart note"
EMU_UI_LOG=0 "$EMU" --headless --flash "$DCOFF" --script "$S/cr_dualcore.txt" --wav "$OUT/cr_dualcore_offrun.wav" >"$OUT/cr_dualcore_offrun.log" 2>&1
l=$(c1line "$OUT/cr_dualcore_offrun.log"); g=$(c1gu "$OUT/cr_dualcore_offrun.log")
echo "$l" | grep -q "c1 off" && [ "${g:-0}" -ge 1 ] && ok "relaunched with Dual Core Off: $l, one core's shared budget ($g given up)" ||
    bad "relaunched with Dual Core Off: ${l:-none}, ${g:-?} given up"
[ -s "$OUT/cr_dualcore_geek.ppm" ] && mv "$OUT/cr_dualcore_geek.ppm" "$OUT/cr_dualcore_geek_off.ppm" || bad "no cr_dualcore_geek.ppm"
DCON="$OUT/cr_dualcore_on_flash.bin"          # .. and one whose settings say On again (KNOB 1 +1 on that flash): every
cp "$DCOFF" "$DCON"                           # run below that is about the second core starts from a copy of it
EMU_UI_LOG=1 "$EMU" --headless --flash "$DCON" --script "$S/cr_dualcore_on.txt" >"$OUT/cr_dualcore_seed.log" 2>&1
LC_ALL=C grep -aq "item 'Dual Core' value 'Off'" "$OUT/cr_dualcore_seed.log" &&
    ok "that unit's Options > Dual Core: Off" || bad "the Off flash's Dual Core is not Off"
LC_ALL=C grep -aq "item 'Dual Core' value 'On .* restart'" "$OUT/cr_dualcore_seed.log" &&
    ok "KNOB 1 +1: On, the value says restart ($OUT/cr_dualcore_opt_on.ppm)" || bad "Dual Core On: no restart note"
dcflash() { cp "$DCON" "$OUT/cr_dualcore_run_flash.bin"; }   # (a run saves its own settings: a copy each)

echo "the second core (docs/DUALCORE.md), Dual Core On: the bass part's job, GEEK OUT's c1 line, 8 + 1 voices, the fail-safes"
dcflash
export EMU_STAGES=1 EMU_UI_LOG=0
run cr_dualcore --flash "$OUT/cr_dualcore_run_flash.bin" --wav "$OUT/cr_dualcore.wav"
unset EMU_STAGES EMU_UI_LOG
[ -s "$OUT/cr_dualcore_geek.ppm" ] && cp "$OUT/cr_dualcore_geek.ppm" "$OUT/cr_dualcore_geek_ok.ppm" || bad "no cr_dualcore_geek.ppm"
l=$(c1line "$OUT/cr_dualcore.log"); g=$(c1gu "$OUT/cr_dualcore.log")
echo "$l" | grep -q "c1 ok [1-9]" && ok "GEEK OUT: $l ($OUT/cr_dualcore_geek_ok.ppm)" || bad "GEEK OUT's c1 line: ${l:-none}"
[ "${g:-x}" = 0 ] && ok "core 1 alive: the 8-note chord and the bass all sound (none given up)" || bad "core 1 alive: ${g:-?} given up"
LC_ALL=C grep -aq '^cpu: stages .* c1 ok, jobs [1-9]' "$OUT/cr_dualcore.log" &&
    ok "$(LC_ALL=C grep -a '^cpu: stages' "$OUT/cr_dualcore.log" | cut -c6-)" || bad "no stage profile"
for m in nostart hang; do
    want="failed"; [ $m = hang ] && want="gave up"
    dcflash
    EMU_C1=$m EMU_UI_LOG=0 "$EMU" --headless --flash "$OUT/cr_dualcore_run_flash.bin" --script "$S/cr_dualcore.txt" --wav "$OUT/cr_dualcore_$m.wav" >"$OUT/cr_dualcore_$m.log" 2>&1
    l=$(c1line "$OUT/cr_dualcore_$m.log"); g=$(c1gu "$OUT/cr_dualcore_$m.log")
    echo "$l" | grep -q "c1 $want" && [ "${g:-0}" -ge 1 ] && ok "EMU_C1=$m: $l, one core's shared budget ($g given up)" ||
        bad "EMU_C1=$m: ${l:-none}, ${g:-?} given up"
done
cp "$OUT/cr_dualcore_run_flash.bin" "$OUT/cr_dualcore_hang_flash.bin"   # (the hang run saved Dual Core Off into it: below)
# EMU_C1=thread (3y45): the bass part's job on a real second thread, at the same time as the chord part's render, as
# on the unit: the WAV must be the inline job's. The VA pair of cr_dualcore.txt, and P5 on both parts (perf_p.txt)
dcflash
EMU_C1=thread EMU_UI_LOG=0 "$EMU" --headless --flash "$OUT/cr_dualcore_run_flash.bin" --script "$S/cr_dualcore.txt" --wav "$OUT/cr_dualcore_thread.wav" >"$OUT/cr_dualcore_thread.log" 2>&1
l=$(c1line "$OUT/cr_dualcore_thread.log")
echo "$l" | grep -q "c1 ok [1-9]" && cmp -s "$OUT/cr_dualcore.wav" "$OUT/cr_dualcore_thread.wav" &&
    ok "EMU_C1=thread: the job on a second thread ($l), the WAV is the inline job's, bit for bit" ||
    bad "EMU_C1=thread: ${l:-none}, or the WAV differs from the inline job's"
dcflash
EMU_UI_LOG=0 "$EMU" --headless --flash "$OUT/cr_dualcore_run_flash.bin" --script "$S/perf_p.txt" --wav "$OUT/cr_p5_dualcore.wav" >"$OUT/cr_p5_dualcore.log" 2>&1
dcflash
EMU_C1=thread EMU_UI_LOG=0 "$EMU" --headless --flash "$OUT/cr_dualcore_run_flash.bin" --script "$S/perf_p.txt" --wav "$OUT/cr_p5_dualcore_thread.wav" >"$OUT/cr_p5_dualcore_thread.log" 2>&1
[ -s "$OUT/cr_p5_dualcore.wav" ] && cmp -s "$OUT/cr_p5_dualcore.wav" "$OUT/cr_p5_dualcore_thread.wav" &&
    ok "EMU_C1=thread, P5 on the chord and on the bass part (perf_p.txt): the WAV is the inline job's, bit for bit" ||
    bad "EMU_C1=thread, P5 on both parts: the WAV differs from the inline job's"
dcflash
EMU_STAGES=1 EMU_UI_LOG=0 "$EMU" --headless --flash "$OUT/cr_dualcore_run_flash.bin" --script "$S/cr_dmaj.txt" --wav "$OUT/cr_dmaj_dualcore.wav" >"$OUT/cr_dmaj_dualcore.log" 2>&1
LC_ALL=C grep -aq '^cpu: stages .* c1 ok, jobs [1-9]' "$OUT/cr_dmaj_dualcore.log" && cmp -s "$OUT/cr_dmaj.wav" "$OUT/cr_dmaj_dualcore.wav" &&
    ok "cr_dmaj: the WAV of a flash set to Dual Core On = the fresh unit's (On by default), bit for bit" ||
    bad "cr_dmaj: the WAV of a flash set to Dual Core On differs from the fresh unit's (or core 1 did not run)"
EMU_C1=nostart EMU_UI_LOG=0 "$EMU" --headless --script "$S/cr_dmaj.txt" --wav "$OUT/cr_dmaj_onecore.wav" >"$OUT/cr_dmaj_onecore.log" 2>&1
cmp -s "$OUT/cr_dmaj.wav" "$OUT/cr_dmaj_onecore.wav" &&
    ok "cr_dmaj: the split's WAV (two cores) = one core's (EMU_C1=nostart), bit for bit" ||
    bad "cr_dmaj: the split's WAV differs from one core's"
# a hung job from the first block: core 0 renders it, one core's samples; and (0.22, docs/DUALCORE.md "The persistent
# fallback") the give-up turns Options > Dual Core Off and the settings save it once the chord is released and the
# unit is quiet: one erase, played as 45 ms of silence (4.7 s in), so the WAVs are compared up to there (4.5 s)
cmp -s -n 800000 "$OUT/cr_dualcore_hang.wav" "$OUT/cr_dualcore_nostart.wav" && ok "a hung job from the first block: one core's WAV (the first 4.5 s)" ||
    bad "a hung job's WAV differs from one core's in the first 4.5 s"
LC_ALL=C grep -aq '^  flash: .* settings saves 1 ' "$OUT/cr_dualcore_hang.log" && LC_ALL=C grep -aq '^  settings: .* dualcore 0 ' "$OUT/cr_dualcore_hang.log" &&
    LC_ALL=C grep -aq '^  flash erases with the audio stalled 1 ' "$OUT/cr_dualcore_hang.log" &&
    ok "the give-up saved Dual Core Off (one settings save, one erase)" || bad "the give-up did not save Dual Core Off"
EMU_UI_LOG=0 "$EMU" --headless --flash "$OUT/cr_dualcore_hang_flash.bin" --script "$S/cr_dualcore.txt" >"$OUT/cr_dualcore_after_hang.log" 2>&1
l=$(c1line "$OUT/cr_dualcore_after_hang.log")
echo "$l" | grep -q "c1 off" && ok "the power-on after the give-up: $l (the record says Off)" || bad "after the give-up: ${l:-none}"
cmp -s "$OUT/cr_dualcore_offrun.wav" "$OUT/cr_dualcore_nostart.wav" && ok "Dual Core Off: the WAV of a core 1 that did not start" ||
    bad "Dual Core Off: the WAV differs from a core 1 that did not start"
cmp -s "$OUT/cr_dualcore_fresh.wav" "$OUT/cr_dualcore.wav" && ok "a fresh unit: the WAV of a flash set to Dual Core On" ||
    bad "a fresh unit's WAV differs from a flash set to Dual Core On"

echo "GEEK OUT 2 (the sixth view): CORE 0, CORE 1, RAM, POOL as tall bars; one core: CORE 1 dim; six HOME taps round"
g2c() { LC_ALL=C grep -a '^  screen reads:' "$1" | sed -n "$2p" | sed -n 's/.* cells //p'; }
g2v() { LC_ALL=C grep -a '^  cr: key' "$1" | sed -n "$2p" | sed -n 's/.* view \([0-9]\) .*/\1/p'; }
cp "$DCOFF" "$OUT/cr_geek2_off_flash.bin"      # (0.22: a fresh unit is two cores; one core is the Off flash's)
EMU_STAGES=1 EMU_UI_LOG=0 "$EMU" --headless --flash "$OUT/cr_geek2_off_flash.bin" --script "$S/cr_geek2.txt" >"$OUT/cr_geek2_one.log" 2>&1
[ -s "$OUT/cr_geek2.ppm" ] && mv "$OUT/cr_geek2.ppm" "$OUT/cr_geek2_one.ppm"
c=$(g2c "$OUT/cr_geek2_one.log" 1)
echo "$c" | grep -q '^0.0:CORE 0=[1-9][0-9]*% 0.1:CORE 1=off 0.2:RAM=n/a 0.3:POOL=n/a$' &&
    ok "one core (Dual Core Off): $c ($OUT/cr_geek2_one.ppm)" || bad "one core: ${c:-no cells}"
dcflash
export EMU_STAGES=1 EMU_UI_LOG=0
run cr_geek2 --flash "$OUT/cr_dualcore_run_flash.bin"
unset EMU_STAGES EMU_UI_LOG
L="$OUT/cr_geek2.log"
c=$(g2c "$L" 1)
has '^view: Geek Out 2' "$L" && [ "$(g2v "$L" 1)" = 5 ] && echo "$c" | grep -q '^0.0:CORE 0=[1-9][0-9]*% 0.1:CORE 1=[1-9][0-9]*% 0.2:RAM=n/a 0.3:POOL=n/a$' &&
    ok "the view menu, B4 (the sixth white root): view: Geek Out 2, two cores: $c" || bad "Geek Out 2: view $(g2v "$L" 1), ${c:-no cells}"
b=$(echo $(pixel "$OUT/cr_geek2.ppm" 30 202)); o=$(echo $(pixel "$OUT/cr_geek2.ppm" 90 203))
echo "$b" | awk '{ exit !($3 > 150 && $1 < 100) }' && echo "$o" | awk '{ exit !($1 > 200 && $2 > 80 && $2 < 160 && $3 < 80) }' &&
    ok "CORE 0's bar blue ($b), CORE 1's orange ($o): $OUT/cr_geek2.ppm" || bad "the bars' colours: CORE 0 $b, CORE 1 $o"
[ "$(g2v "$L" 2)" = 4 ] && [ "$(g2v "$L" 3)" = 5 ] && ok "HOME tapped: five taps Scope, the sixth Geek Out 2 again (six views)" ||
    bad "the HOME cycle: views $(g2v "$L" 2) / $(g2v "$L" 3)"

echo "Options > Dual Core back Off at the next power-on (c1 off, the shared budget), On again (c1 ok)"
DF="$OUT/cr_dualcore_flash.bin"
cp "$DCON" "$DF"
EMU_UI_LOG=1 "$EMU" --headless --flash "$DF" --script "$S/cr_dualcore_opt.txt" >"$OUT/cr_dualcore_opt.log" 2>&1
LC_ALL=C grep -aq "item 'Dual Core' value 'Off .* restart'" "$OUT/cr_dualcore_opt.log" &&
    ok "Options > Dual Core Off: the value says restart ($OUT/cr_dualcore_opt.ppm)" || bad "Dual Core Off: no restart note"
EMU_UI_LOG=0 "$EMU" --headless --flash "$DF" --script "$S/cr_dualcore.txt" >"$OUT/cr_dualcore_off.log" 2>&1
l=$(c1line "$OUT/cr_dualcore_off.log"); g=$(c1gu "$OUT/cr_dualcore_off.log")
echo "$l" | grep -q "c1 off" && [ "${g:-0}" -ge 1 ] && ok "relaunched Off: $l, one core's shared budget ($g given up)" ||
    bad "relaunched Off: ${l:-none}, ${g:-?} given up"
EMU_UI_LOG=1 "$EMU" --headless --flash "$DF" --script "$S/cr_dualcore_on.txt" >"$OUT/cr_dualcore_on.log" 2>&1
LC_ALL=C grep -aq "item 'Dual Core' value 'On .* restart'" "$OUT/cr_dualcore_on.log" && ok "back On: the value says restart" ||
    bad "Dual Core On: no restart note"
EMU_UI_LOG=0 "$EMU" --headless --flash "$DF" --script "$S/cr_dualcore.txt" >"$OUT/cr_dualcore_back.log" 2>&1
l=$(c1line "$OUT/cr_dualcore_back.log"); g=$(c1gu "$OUT/cr_dualcore_back.log")
echo "$l" | grep -q "c1 ok [1-9]" && [ "${g:-x}" = 0 ] && ok "relaunched On: $l, none given up" || bad "relaunched On: ${l:-none}, ${g:-?} given up"
# 0.22, On by default: the boot that follows a counted failed boot (a watchdog reset within 30 s of the boot before:
# a start of core 1 that hung is one) does not start core 1: one core, "c1 failed", a normal boot that loads and saves
# (cr_cpu1.h cr_c1_power_on); the power-on after it starts the core again
EMU_UI_LOG=0 "$EMU" --headless --script "$S/cr_dualcore.txt" --boot-fail 0 --reset-reason wdt >"$OUT/cr_dualcore_afterfail.log" 2>&1
l=$(c1line "$OUT/cr_dualcore_afterfail.log"); g=$(c1gu "$OUT/cr_dualcore_afterfail.log")
has '^boot: normal reset wdt failed 1 pending 1 counted 1' "$OUT/cr_dualcore_afterfail.log" && echo "$l" | grep -q "c1 failed" && [ "${g:-0}" -ge 1 ] &&
    ok "the boot after a failed one (a fresh unit, Dual Core On): NORMAL, $l: one core for this boot ($g given up)" ||
    bad "the boot after a failed one: $(grep -m1 '^boot:' "$OUT/cr_dualcore_afterfail.log") / ${l:-none}"

echo "SysEx 76 SELECT: the chord part to CZ-1 position 2, the bass to FM6 position 3, a bad engine refused"
EMU_UI_LOG=1 "$EMU" --headless --no-flash --script "$S/cr_select.txt" >"$OUT/cr_select.log" 2>&1
LC_ALL=C grep -aq "select: part 0 engine CZ-1 pos 2" "$OUT/cr_select.log" && ok "SELECT chord CZ-1 2 ($OUT/cr_select.ppm)" ||
    bad "SELECT chord CZ-1 2: no trace"
LC_ALL=C grep -aq "select: part 1 engine FM6 pos 3" "$OUT/cr_select.log" && ok "SELECT bass FM6 3" || bad "SELECT bass FM6 3: no trace"
[ "$(LC_ALL=C grep -ac "^select: " "$OUT/cr_select.log")" = 2 ] && ok "a bad engine: refused (no third load)" ||
    bad "SELECT: $(LC_ALL=C grep -ac "^select: " "$OUT/cr_select.log") loads, want 2"

# 0.18: a 0.17 unit's flash (a user sound on PHASE, the chord part parked on NOISE; then both parts on PHYS, a user
# sound on it, the session) on today's build (test_retired.sh)
# GRAIN (engine 8, eng_grain.c, docs/GRAIN.md): the seventh engine from the picker, its pool, a chord of grains, the
# editor's screens (the wave band on SRC 1 / SRC 2 and GRAIN, the VA's filter band, ENV 1 / ENV 2 under the AHDSR, the
# LFO and SYNC stacks, the matrix), the key up, silence
echo "GRAIN: the engine, its screens (the wave band), a chord of grains"
run cr_grain
GL="$OUT/cr_grain.log"
has '^engine: part 0 -> GRAIN' "$GL" && has 'part 0: GRAIN / CLOUD PAD' "$GL" && ok "EDIT held + SELECT +6: GRAIN (the seventh engine), 01 CLOUD PAD" ||
    bad "not GRAIN from the picker: $(grep -m1 '^engine:' "$GL")"
has 'part 0: GRAIN / SHIMMER' "$GL" && has '^expect sound .*: ok' "$GL" && ok "PRESETS +1: 02 SHIMMER; D4 held: a chord of grains sounds" ||
    bad "SHIMMER / sound: $(grep -m1 'part 0: GRAIN' "$GL") / $(grep -m1 '^expect sound' "$GL")"
for f in src1 src2 grain morph pos filter env1 env2 lfo lfosyn mod; do
    [ -s "$OUT/cr_grain_$f.png" ] || bad "no screenshot cr_grain_$f.png"
done
[ -s "$OUT/cr_grain_mod.png" ] && ok "the editor's screens shot: SRC 1 and SRC 2 under the wave band, GRAIN / GRAIN+ (both buffers, MORPH, POS), the filter, ENV 1 / ENV 2, LFO, LFO SYN, MOD: $OUT/cr_grain_grain.png cr_grain_morph.png"
[ "$(grep -c 'parts busy' "$GL")" -ge 3 ] && [ "$(last_busy "$GL")" = 0 ] && ok "cr_grain: every part silent 2 s after the release" ||
    bad "cr_grain: $(last_busy "$GL") voices still sounding 2 s after the release"

sh tools/emu/test_retired.sh >"$OUT/test_retired.log" 2>&1 || fail=1
LC_ALL=C grep -a '^retired\|^  ok\|^  FAIL' "$OUT/test_retired.log"

echo "determinism"
mkdir -p "$OUT/cr_again"
cp "$OUT/cr_dmaj.ppm" "$OUT/cr_again/first.ppm"
"$EMU" --headless --script "$S/cr_dmaj.txt" --wav "$OUT/cr_again/cr_dmaj.wav" >/dev/null 2>&1
cmp -s "$OUT/cr_dmaj.wav" "$OUT/cr_again/cr_dmaj.wav" && ok "the same audio on a second run" || bad "audio differs"
cmp -s "$OUT/cr_dmaj.ppm" "$OUT/cr_again/first.ppm" && ok "the same LCD on a second run" || bad "LCD differs"

echo "OCT- + OCT+ + HOME held 5 s: update mode (0.18; the two OCT keys alone: PANIC, nothing more)"
# cr_backup.c cr_update_poll; the emulator's hooks (emu_firmware.h) print what the unit would do and end the run.
# EMU_STUB=3: the boot stub ran before this boot, as on a unit installed by 0.16 or later
U=$OUT/cr_update.log
EMU_STUB=3 "$EMU" --headless --script "$S/cr_update.txt" >"$U" 2>&1
st=$?
ul() { grep -a '^update:\|^restart:' "$1" | tr '\n' '|'; }
[ $st = 0 ] && [ "$(ul "$U")" = "update: 3 (update mode)|update: 2 (update mode)|update: 1 (update mode)|update: go (the stub's rescue request)|update: screen 'UPDATE MODE'|restart: stub request 52514552 (guard pending 0)|" ] &&
    ok "the three held 5 s: the countdown 3 2 1, then the boot stub's RESCUE request (52514552, the staged update loader: what SysEx 77 STUB op 2 asks for), the boot guard told" \
    || bad "the update gesture: exit $st: $(ul "$U")"
[ "$(grep -ac '^  cr: key' "$U")" = 1 ] && ok "the unit left at 5 s (the script's later dump never ran)" || bad "the unit did not leave: $(grep -ac '^  cr: key' "$U") dumps"
p=$(pixel "$OUT/cr_update_3.ppm" 40 120); set -- $p
[ "${1:-0}" -gt 180 ] && [ "${2:-255}" -lt 100 ] && [ "$(px_count "$OUT/cr_update_3.ppm" 90 90 150 160 red)" -lt 3300 ] &&
    ! cmp -s "$OUT/cr_update_3.ppm" "$OUT/cr_update_2.ppm" &&
    ok "the countdown screen: red, a digit in its middle, another one a second later: $OUT/cr_update_3.ppm, cr_update_2.ppm" \
    || bad "the countdown screen ($p; $(px_count "$OUT/cr_update_3.ppm" 90 90 150 160 red) red px in the middle)"
grep -a '^  session: saves' "$U" | sed -n 1p | grep -q 'saves 0 .* pending 1  voicing 2 ' && ! grep -aq '^save: global' "$U" &&
    ok "the update gesture is no global save (0.18: saving is manual): what was changed and not saved (the voicing) is not written on the way out" \
    || bad "the session before the update: $(grep -a '^  session: saves' "$U" | sed -n 1p)"
"$EMU" --headless --script "$S/cr_update.txt" >"$OUT/cr_update_nostub.log" 2>&1
st=$?
[ $st = 0 ] && [ "$(ul "$OUT/cr_update_nostub.log")" = "update: 3 (USB boot)|update: 2 (USB boot)|update: 1 (USB boot)|update: go (no stub: the ROM's USB boot)|update: screen 'USB BOOT'|restart: ROM USB boot (fm1_enter_uboot; guard pending 0)|" ] &&
    ok "no boot stub ran before this boot (another loader's install): the screen says USB boot and the unit enters the ROM's, as the gesture did until 0.17" \
    || bad "the gesture without a stub: exit $st: $(ul "$OUT/cr_update_nostub.log")"
EMU_STUB=3 "$EMU" --headless --script "$S/cr_update.txt" --boot-fail 1 --reset-reason wdt >"$OUT/cr_update_safe.log" 2>&1
st=$?
[ $st = 0 ] && has '^boot: safe ' "$OUT/cr_update_safe.log" && has '^restart: stub request 52514552' "$OUT/cr_update_safe.log" &&
    grep -a '^  flash:' "$OUT/cr_update_safe.log" | sed -n 1p | grep -q ', 0 writes' &&
    ok "in SAFE MODE: the same gesture, the same request; nothing written" || bad "the gesture in SAFE MODE: exit $st: $(ul "$OUT/cr_update_safe.log")"
N=$OUT/cr_update_not.log
EMU_STUB=3 "$EMU" --headless --script "$S/cr_update_not.txt" >"$N" 2>&1
st=$?
uc() { grep -a '^  cr: key' "$N" | sed -n "$1p" | sed -n 's/.* octave \(-*[0-9]*\) .* view \([0-9]\) layer \([0-9]*\) options \([0-9]\).*/octave \1 view \2 layer \3 options \4/p'; }
[ $st = 0 ] && ! has '^restart:' "$N" && [ "$(grep -ac '^  cr: key' "$N")" = 7 ] && ok "cr_update_not: no restart, the script ran to its end" || bad "cr_update_not: exit $st"
n1=$(grep -an '^  cr: key' "$N" | sed -n 1p | cut -d: -f1)
[ -n "$n1" ] && ! sed -n "1,${n1}p" "$N" | grep -aq '^update:' && [ "$(uc 1)" = "octave 0 view 0 layer 0 options 0" ] && [ "$(uc 2)" = "octave 0 view 0 layer 0 options 0" ] &&
    ok "OCT- + OCT+ alone held 6 s (the gesture until 0.17): no countdown, nothing after their PANIC; let go: no octave step" \
    || bad "the two OCT keys held: $(sed -n "1,${n1:-1}p" "$N" | grep -a '^update:' | head -1) / $(uc 1) / $(uc 2)"
p=$(pixel "$OUT/cr_update_panic.ppm" 120 200); set -- $p
[ "${1:-0}" -gt 180 ] && [ "${2:-255}" -lt 90 ] && ! cmp -s "$OUT/cr_update_panic.ppm" "$OUT/cr_update_two.ppm" &&
    ok "PANIC as it was: its red screen at the press ($p), gone again while the two are still held" || bad "PANIC at the press ($p)"
[ "$(ul "$N")" = "update: 3 (update mode)|update: 2 (update mode)|update: cancelled|update: 3 (update mode)|update: 2 (update mode)|update: cancelled|" ] &&
    [ "$(uc 3)" = "octave 0 view 0 layer 0 options 0" ] && [ "$(uc 4)" = "octave 0 view 0 layer 0 options 0" ] && [ "$(uc 5)" = "octave 0 view 0 layer 0 options 0" ] &&
    ok "the three let go at 3.9 s (and HOME first, then the OCT keys, 3 s): the countdown ran, cancelled; no HOME tap (the view stays), no view menu, no octave step" \
    || bad "the cancel: $(ul "$N") / $(uc 3) / $(uc 4) / $(uc 5)"
[ "$(uc 6)" = "octave 0 view 0 layer 9 options 0" ] && [ "$(uc 7)" = "octave 0 view 0 layer 0 options 0" ] &&
    ok "HOME alone held: the view menu, as before" || bad "HOME held: $(uc 6) / $(uc 7)"

# ---- the keys while a frame draws (cr_ui.c cr_ui_pump, docs/INTEGRATION.md "Keys while a frame draws") ----
# cr_keylat.txt: the SCOPE view with a note sounding (six strips every frame), eight keys struck as a frame begins,
# just past its scan. A frame takes no time by default (the WAV is then the same with and without the pump); with
# EMU_STRIP_US a strip takes that long on the device's clock: the note-on's way to the engine is a strip's time, a
# tick and an audio block with the keys read between the strips, the whole redraw without (EMU_PUMP=0: as it was)
kl() {   # kl STRIP_US PUMP: the log in $OUT/cr_keylat_US_PUMP.log, the WAV beside it
    EMU_STRIP_US=$1 EMU_PUMP=$2 "$EMU" --headless --script "$S/cr_keylat.txt" --wav "$OUT/cr_keylat_$1_$2.wav" >"$OUT/cr_keylat_$1_$2.log" 2>&1
}
klw() { sed -n 's/^keys: 9 note-ons, from the key.s scan to the engine avg [0-9.]* ms, worst \([0-9]*\)\.[0-9] ms.*/\1/p' "$OUT/cr_keylat_$1_$2.log"; }
kl 0 1; st=$?
kl 0 0
[ $st = 0 ] && [ "$(grep -c '^strike: .* a redraw of 6 strips' "$OUT/cr_keylat_0_1.log")" = 8 ] && cmp -s "$OUT/cr_keylat_0_1.wav" "$OUT/cr_keylat_0_0.wav" &&
    ok "cr_keylat: eight keys struck as a frame of six strips began; with frames that take no time the sound is the same with and without the key pump" \
    || bad "cr_keylat: exit $st, $(grep -c '^strike: .* a redraw of 6 strips' "$OUT/cr_keylat_0_1.log") strikes into a full redraw, or the WAVs differ"
kl 2700 1; kl 2700 0; kl 7800 1; kl 7800 0
a=$(klw 2700 1); b=$(klw 2700 0); c=$(klw 7800 1); d=$(klw 7800 0)
[ "${a:-99}" -le 5 ] && [ "${b:-0}" -ge 15 ] && [ "${c:-99}" -le 10 ] && [ "${d:-0}" -ge 40 ] &&
    ok "a key struck while a frame draws: to the engine in $a ms at most with a 16 ms redraw (2.7 ms a strip), $c ms with a 47 ms one; read between frames only (as it was) $b and $d ms" \
    || bad "the keys while a frame draws: worst ${a:-?} ms (16 ms redraw; <= 5), ${c:-?} ms (47 ms redraw; <= 10); without the pump ${b:-?} (>= 15), ${d:-?} (>= 40)"

echo "Options > Output: Classic / Clean (0.19, docs/AUDIO-QUALITY.md): switched under a held chord, saved, Classic from the first block"
EMU_UI_LOG=1 "$EMU" --headless --script "$S/cr_output.txt" --wav "$OUT/cr_output.wav" >"$OUT/cr_output.log" 2>&1 || bad "cr_output: exit $?"
ml() { LC_ALL=C grep -a '^  master:' "$1" | sed -n "$2p" | sed 's/^  master: //; s/ lowcut.*//'; }
[ "$(ml "$OUT/cr_output.log" 1)" = "output Clean (chain 1, want 1, fade 0)" ] &&
    ok "a new unit: Output Clean, the Clean chain from the first block" || bad "Output at power-on: $(ml "$OUT/cr_output.log" 1)"
LC_ALL=C grep -aq "item 'Output' value 'Clean'" "$OUT/cr_output.log" && LC_ALL=C grep -aq "item 'Output' value 'Classic'" "$OUT/cr_output.log" &&
    [ "$(ml "$OUT/cr_output.log" 2)" = "output Classic (chain 0, want 0, fade 0)" ] &&
    [ "$(ml "$OUT/cr_output.log" 3)" = "output Clean (chain 1, want 1, fade 0)" ] &&
    ok "Options > Output, KNOB 1: Classic (0.18's master runs, the fade over), then Clean again ($OUT/cr_output_classic.ppm)" \
    || bad "Output switched: $(ml "$OUT/cr_output.log" 2) / $(ml "$OUT/cr_output.log" 3)"
differ cr_output_clean cr_output_classic "Output: the two values' screens differ"
wc_=$(python3 tools/emu/wavclicks.py "$OUT/cr_output.wav" --from 1.0 | tail -1)
echo "$wc_" | grep -q ": 0 jumps > 0.5 FS, 0 silent holes mid-sound, 0 clicks" &&
    ok "both changes under the held chord: no jump, no hole, no click ($(echo "$wc_" | sed 's/.*wav: //; s/:.*//'))" || bad "Output switched: $wc_"
OF=$OUT/cr_output_flash.bin
rm -f "$OF"
"$EMU" --headless --flash "$OF" --script "$S/cr_output_classic.txt" >"$OUT/cr_output_set.log" 2>&1 || bad "cr_output_classic: exit $?"
"$EMU" --headless --flash "$OF" --script "$S/persist_check.txt" >"$OUT/cr_output_relaunch.log" 2>&1 || bad "cr_output relaunch: exit $?"
grep -q "settings saves 1 " "$OUT/cr_output_set.log" && [ "$(ml "$OUT/cr_output_relaunch.log" 1)" = "output Classic (chain 0, want 0, fade 0)" ] &&
    ok "Output Classic is saved by itself (an Option) and is the chain from the first block after a relaunch" \
    || bad "Output saved: $(ml "$OUT/cr_output_relaunch.log" 1)"
EMU_OUTPUT=classic "$EMU" --headless --script "$S/perf_a.txt" --wav "$OUT/cr_output_pin.wav" >"$OUT/cr_output_pin.log" 2>&1
"$EMU" --headless --flash "$OF" --script "$S/perf_a.txt" --wav "$OUT/cr_output_set.wav" >/dev/null 2>&1
"$EMU" --headless --script "$S/perf_a.txt" --wav "$OUT/cr_output_def.wav" >/dev/null 2>&1
cmp -s "$OUT/cr_output_pin.wav" "$OUT/cr_output_set.wav" && ! cmp -s "$OUT/cr_output_pin.wav" "$OUT/cr_output_def.wav" &&
    ok "a script on the flash that says Classic = the same with EMU_OUTPUT=classic, bit for bit (what tools/emu/classic_exact.sh compares with 0.18's), and not the Clean default's" \
    || bad "Output Classic from the settings and EMU_OUTPUT=classic: the WAVs differ, or equal the default's"
# Clean's limiter looks ahead: its output is the same passage 32 samples (0.73 ms) late, and never over its threshold
# (26000 of 32768: with the look-ahead no sample overshoots into the knee)
LC_ALL=C grep -aq "^  clean: look-ahead 32 samples" "$OUT/cr_output.log" && ok "the Clean chain's look-ahead: 32 samples" \
    || bad "the dump's look-ahead: $(LC_ALL=C grep -a '^  clean:' "$OUT/cr_output.log" | head -1)"
wl=$(python3 tools/emu/wavlag.py "$OUT/cr_output_pin.wav" "$OUT/cr_output_def.wav")
echo "$wl" | grep -Eq "^lag 32 samples \(0\.73 ms\), correlation (0\.99|1\.00)" &&
    ok "the same script through Clean is Classic's 32 samples late and nothing else moves ($wl)" || bad "Clean against Classic: $wl"
EMU_UI_LOG=1 "$EMU" --headless --script "$S/cr_output_loud.txt" --wav "$OUT/cr_output_loud.wav" >"$OUT/cr_output_loud.log" 2>&1 || bad "cr_output_loud: exit $?"
EMU_OUTPUT=classic "$EMU" --headless --script "$S/cr_output_loud.txt" --wav "$OUT/cr_output_loud_classic.wav" >/dev/null 2>&1
wl=$(python3 tools/emu/wavlag.py "$OUT/cr_output_loud_classic.wav" "$OUT/cr_output_loud.wav")
pk=$(echo "$wl" | sed 's/.*, B \([0-9]*\) of.*/\1/')
# (the ceiling in the WAV: 26000 x MASTER, 4080 / 4096 at the pot's top in the emulator = 25898: a peak that sits on it
# to a unit was put there by the limiter; one over it would have got past)
[ "${pk:-99999}" -le 25899 ] && [ "${pk:-0}" -ge 25890 ] &&
    ok "a loud chord with the bass at full MASTER, held 5 s: the peak sits on Clean's ceiling and no sample is over it (peak $pk, ceiling 25898; Classic's on the same: $(echo "$wl" | sed 's/.*peak A \([0-9]*\),.*/\1/'))" \
    || bad "the loud chord through Clean: peak ${pk:-?} (the ceiling is 25898: 25890 .. 25899): $wl"
wc_=$(python3 tools/emu/wavclicks.py "$OUT/cr_output_loud.wav" --from 1.0 | tail -1)
echo "$wc_" | grep -q ": 0 jumps > 0.5 FS, 0 silent holes mid-sound, 0 clicks" &&
    ok "and no jump, no hole, no click in it" || bad "the loud chord through Clean: $wc_"

echo "0.19's audio: the chorus in stereo, the click lined up with Clean, MASTER read before the audio starts (docs/FX.md, docs/AUDIO-QUALITY.md \"Round 3\")"
run cr_chorus --wav "$OUT/cr_chorus.wav"
EMU_CHORUS=mono "$EMU" --headless --script "$S/cr_chorus.txt" --wav "$OUT/cr_chorus_mono.wav" >"$OUT/cr_chorus_mono.log" 2>&1
LC_ALL=C grep -aq "session fx: on 1 .* amt 0 45 0 0 " "$OUT/cr_chorus.log" || bad "cr_chorus: the sends are not reverb 0, chorus 45, delay 0: $(LC_ALL=C grep -a 'session fx' "$OUT/cr_chorus.log" | head -1)"
ws=$(python3 tools/emu/wavstat.py stereo "$OUT/cr_chorus.wav")
sd=$(echo "$ws" | sed -n 's/^side -\([0-9]*\)\..*/\1/p')
[ "${sd:-99}" -le 20 ] && [ "${sd:-0}" -ge 3 ] &&
    ok "a note with only its chorus send: left and right differ, the stereo chorus ($ws)" || bad "the chorus is not stereo: $ws"
ws=$(python3 tools/emu/wavstat.py stereo "$OUT/cr_chorus_mono.wav")
echo "$ws" | grep -q "^side -inf dB re mid; left differs from right on 0 of " &&
    ok "the same with EMU_CHORUS=mono (0.18's return, the test switch): the same word on both sides" || bad "EMU_CHORUS=mono: $ws"
wm=$(python3 tools/emu/wavstat.py mono "$OUT/cr_chorus.wav" "$OUT/cr_chorus_mono.wav")
echo "$wm" | grep -Eq "^mono sum [+-]0\.[0-9]* dB re B's; left [+-]0\.[0-4][0-9]* dB" &&
    ok "the stereo chorus's left is as loud as 0.18's return, its mono sum within 1 dB of it ($wm)" || bad "the chorus's level: $wm"
run cr_click --wav "$OUT/cr_click.wav"
EMU_OUTPUT=classic "$EMU" --headless --script "$S/cr_click.txt" --wav "$OUT/cr_click_classic.wav" >/dev/null 2>&1
wl=$(python3 tools/emu/wavlag.py "$OUT/cr_click_classic.wav" "$OUT/cr_click.wav")
echo "$wl" | grep -Eq "^lag 32 samples \(0\.73 ms\), correlation 1\.000; peak A ([0-9]*), B \1 of" &&
    ok "the count-in's clicks through Clean are Classic's, 32 samples later: lined up with Clean's sound, their level untouched ($wl)" \
    || bad "the click in Clean against Classic: $wl"
bm() {   # bm CHAIN ADC: cr_boot_master with the pot at ADC at power-on, through CHAIN
    if [ "$1" = classic ]; then
        EMU_OUTPUT=classic EMU_MASTER=$2 "$EMU" --headless --script "$S/cr_boot_master.txt" --wav "$OUT/cr_boot_$1_$2.wav" >"$OUT/cr_boot_$1_$2.log" 2>&1
    else
        EMU_MASTER=$2 "$EMU" --headless --script "$S/cr_boot_master.txt" --wav "$OUT/cr_boot_$1_$2.wav" >"$OUT/cr_boot_$1_$2.log" 2>&1
    fi
}
for c in clean classic; do
    bm $c 160; bm $c 1023
    lv=$(python3 tools/emu/wavstat.py level "$OUT/cr_boot_${c}_160.wav" "$OUT/cr_boot_${c}_1023.wav" 0 10 200 250)
    grep -aq "master_q12 100 " "$OUT/cr_boot_${c}_160.log" && echo "$lv" | grep -Eq "^level -32\.[0-4][0-9]* dB re B's \(0 \.\. 10 ms\); -32\.[0-4][0-9]* dB " &&
        ok "MASTER at power-on, $c: the pot at 160 (MASTER 100 / 4096): the first note's first 10 ms are 32.2 dB under the top position's, as 200 ms later ($lv)" \
        || bad "MASTER at power-on, $c: $lv ($(grep -a 'master_q12' "$OUT/cr_boot_${c}_160.log" | head -1))"
done

echo "the Options menu leaves the keys alone (0.19): they play as on the view, the menu is SELECT, KNOB 1 and the buttons"
run cr_opt_keys --wav "$OUT/cr_opt_keys.wav"
L="$OUT/cr_opt_keys.log"
dl() { LC_ALL=C grep -a "^  $1" "$L" | sed -n "$2p"; }   # dl PREFIX N: the Nth dump's line that starts with PREFIX
oe() { dl 'options:' "$1" | sed 's/^  options: //'; }
[ "$(oe 1)" = "open 1 entry 2 of 33 'Secret Chords' value 'Simple'" ] && dl 'cr: voices' 1 | grep -q 'notes-out 0 ' &&
    dl 'cr: voices' 2 | grep -q '^  cr: voices 1 pending 0 notes-out 3 chord DM sounding 1 notes 62 66 69' &&
    dl 'loopv:' 2 | grep -q 'stream-notes main 3 ' && ! has 'expect.*FAILED' "$L" &&
    ok "MAJ + D4 with the menu open: D major sounds (62 66 69), three notes out on MIDI" \
    || bad "a chord in the Options: $(dl 'cr: voices' 2) / $(dl 'loopv:' 2)"
[ "$(oe 2)" = "$(oe 1)" ] && [ "$(oe 3)" = "$(oe 1)" ] &&
    ok "the selection did not move: Secret Chords under D4, E4, A4 and F5 (they were the shortcuts to entries 0, 1, 4 and 9)" \
    || bad "a key moved the Options' selection: '$(oe 2)' / '$(oe 3)'"
dl 'key leds:' 2 | grep -q '^  key leds:111114111 400040040000400040 ' && has 'expect led GLO on .*: ok' "$L" && has 'expect led E4 off .*: ok' "$L" &&
    ok "the key LEDs show the playing state (MAJ lit, D F# A lit on both octaves, the other roots dark), OPT lit" \
    || bad "the key LEDs in the Options: $(dl 'key leds:' 2)"
dl 'lock:' 4 | grep -q '^  lock: on 1 ' && dl 'lock:' 5 | grep -q '^  lock: on 0 ' && ok "B3 in the menu: LOCK on, and off again" \
    || bad "B3 in the Options: $(dl 'lock:' 4)"
om() { dl 'master:' "$1" | sed 's/^  master: //; s/ lowcut.*//'; }
ov() { dl 'screen reads:' "$1" | sed -n "s/.*value '\([^']*\)'.*/\1/p"; }
[ "$(om 5)" = "output Clean (chain 1, want 1, fade 0)" ] && [ "$(om 6)" = "output Classic (chain 0, want 0, fade 0)" ] &&
    [ "$(om 7)" = "output Clean (chain 1, want 1, fade 0)" ] && [ "$(ov 5)/$(ov 6)/$(ov 7)" = "Clean/Classic/Clean" ] &&
    dl 'part 0:' 6 | grep -q 'voices 3,' && dl 'part 0:' 7 | grep -q 'voices 3,' &&
    ok "a chord struck in the menu, Output Clean -> Classic -> Clean by KNOB 1: the chain and the screen's value 60 ms after each turn, the three voices held" \
    || bad "Output under a chord: $(om 5) / $(om 6) / $(om 7), the screen $(ov 5)/$(ov 6)/$(ov 7)"
differ cr_opt_keys_classic cr_opt_keys_clean "Output in the menu with a chord held: the two values' screens differ"
# (the chord is struck at 4.78 s and let go at 8.4 s; the turns are at 6.28 and 7.34 s. The window leaves the strike
# out: wavclicks reads TINE EP's attack as a click, as every bright onset)
wc_=$(python3 tools/emu/wavclicks.py "$OUT/cr_opt_keys.wav" --from 5.3 --to 8.3 | tail -1)
echo "$wc_" | grep -q ": 0 jumps > 0.5 FS, 0 silent holes mid-sound, 0 clicks" &&
    ok "no jump, no silent hole, no click at either change (5.3 .. 8.3 s)" || bad "Output switched under a chord struck in the menu: $wc_"
sh_() { dl 'settings:' "$1" | sed -n 's/.* now \([0-9A-F]*\) .*/\1/p'; }
echo "$(oe 8)" | grep -q "'Reset Settings' value 'OCT+ again: reset'" && [ "$(oe 9)" = "$(oe 8)" ] && [ -n "$(sh_ 8)" ] && [ "$(sh_ 9)" = "$(sh_ 8)" ] &&
    echo "$(oe 10)" | grep -q "^open 0 " && [ "$(oe 45)" = "open 1 entry 2 of 33 'Secret Chords' value 'Simple'" ] &&
    ! grep -aq 'settings reset' "$L" &&
    ok "Reset Settings asked (OCT+ once): five keys neither confirm nor cancel it; OCT- leaves, nothing was reset (Secret Chords still Simple)" \
    || bad "Reset Settings and the keys: '$(oe 8)' / '$(oe 9)' / '$(oe 10)' / '$(oe 45)'"
echo "$(oe 11)" | grep -q "^open 1 entry 31 of 33 'Calibrate'" && ! grep -aq '^calib:' "$L" && ok "Calibrate: no key starts it" \
    || bad "Calibrate and the keys: '$(oe 11)' $(grep -a -m1 '^calib:' "$L")"
n=0; i=0
while [ $i -le 32 ]; do
    echo "$(oe $((12 + i)))" | grep -q "^open 1 entry $i of 33 '" && n=$((n + 1))
    i=$((i + 1))
done
[ $n = 33 ] && echo "$(oe 44)" | grep -q "'Reset Settings'" && echo "$(oe 18)" | grep -q "entry 6 of 33 '6th with 7th' value 'In place'" &&
    echo "$(oe 20)" | grep -q "entry 8 of 33 'Key Touch' value 'Fixed'" && echo "$(oe 22)" | grep -q "entry 10 of 33 'Velocity Curve' value '0'" &&
    echo "$(oe 23)" | grep -q "entry 11 of 33 'Aftertouch Sens' value 'Mid'" &&
    ok "every entry by SELECT alone: 33 of 33, Play Style to Reset Settings (Chord Keys, Full K Shorts, MIDI Keys after Secret Chords since 0.24; the SUS Window listed in FM-1 only) (6th with 7th the fourth; Key Touch, Velocity Sens, Velocity Curve, Aftertouch Sens after Velocity)" \
    || bad "the entries by SELECT: $n of 33"
grep -aq '^loop: loop recorded (state 2 layers 1 events 1 ' "$L" && ok "a take played with the menu open is recorded (armed on the view, one chord)" \
    || bad "the looper with the Options open: $(grep -a '^loop:' "$L" | tail -2 | tr '\n' ' ')"
silent_end cr_opt_keys

echo "Options > USB Rate (0.19): 48 kHz is opt-in; 44.1 kHz, the default, is 0.18's device (the USB side: tests/cr_usbrate_test.c)"
run cr_usb_rate
L="$OUT/cr_usb_rate.log"
ur() { dl 'usb-rate:' "$1" | sed 's/^  usb-rate: //; s/ (.*pick_roots \([0-9]*\))/ \1/'; }
ul() { dl 'screen reads:' "$1" | sed -n "s/.*label '\([^']*\)'.*/\1/p"; }
[ "$(oe 1)" = "open 1 entry 27 of 33 'USB Rate' value '44.1 kHz'" ] && [ "$(ur 1)" = "48k 0 0" ] && [ "$(ul 1)" = "48 kHz: not offered" ] &&
    ok "a new unit: USB Rate is the entry after USB Level, 44.1 kHz ('48 kHz: not offered'), the record's bit clear" \
    || bad "USB Rate on a new unit: '$(oe 1)' / $(ur 1) / '$(ul 1)'"
[ "$(oe 2)" = "open 1 entry 27 of 33 'USB Rate' value '44.1 + 48 kHz'" ] && [ "$(ur 2)" = "48k 1 4" ] &&
    [ "$(ul 2)" = "the computer picks the rate" ] && [ "$(oe 3)" = "$(oe 1)" ] && [ "$(ur 3)" = "48k 0 0" ] &&
    ok "KNOB 1 +1: 44.1 + 48 kHz ('the computer picks the rate'), bit 2 of the record's byte; -1: 44.1 kHz again" \
    || bad "USB Rate turned: '$(oe 2)' / $(ur 2) / '$(ul 2)' then '$(oe 3)' / $(ur 3)"
echo "$(oe 4)" | grep -q "^open 0 " && [ "$(ur 4)" = "48k 1 4" ] && ok "set and the menu closed: kept (the two values end the range)" \
    || bad "USB Rate after the menu: '$(oe 4)' / $(ur 4)"
differ cr_usb_rate_44 cr_usb_rate_48 "USB Rate: the two values' screens differ"

echo "Options > Chord Names (0.20): Orchid, the default, or Standard (docs/CHORD-NAMES.md): the names only, never the notes"
EMU_UI_LOG=1 "$EMU" --headless --script "$S/cr_chord_names.txt" >"$OUT/cr_chord_names.log" 2>&1 || bad "cr_chord_names: exit $?"
L="$OUT/cr_chord_names.log"
cn() { dl 'cr: voices' "$1" | sed -n 's/.* chord \(.*\) sounding \([01]\) notes \(.*\)$/\1|\2|\3/p'; }       # "A7 9|1|69 73 76 79 83"
cb() { dl 'chord-names:' "$1" | sed -n "s/^  chord-names: standard \([01]\) .* engine's \([01]\); the record's bit \([01]\))$/\1 \2 \3/p"; }
sn_() { LC_ALL=C grep -aq " name '$1' " "$L"; }                    # the name a frame drew (root|quality|superscript)
[ "$(cn 1)" = "A7 9|1|69 73 76 79 83" ] && [ "$(cn 2)" = "A9|1|69 73 76 83" ] && [ "$(cn 3)" = "Asusm7 9|1|69 74 76 79 83" ] &&
    [ "$(cb 1)" = "0 0 0" ] && sn_ 'A||7 9' && sn_ 'A||9' && sn_ 'A|sus|m7 9' &&
    ok "a new unit names as the Orchid does: A + 7 + 9 'A7 9', A + 9 'A9', Asus + 7 + 9 'Asus m7 9' (the engine's and the screen's)" \
    || bad "Orchid names: '$(cn 1)' / '$(cn 2)' / '$(cn 3)' / $(cb 1)"
[ "$(oe 4)" = "open 1 entry 22 of 33 'Chord Names' value 'Orchid'" ] && [ "$(ul 4)" = "A7 9 $(printf '\267') A9 $(printf '\267') Asus m7 9" ] &&
    [ "$(oe 5)" = "open 1 entry 22 of 33 'Chord Names' value 'Standard'" ] && [ "$(ul 5)" = "A9 $(printf '\267') Aadd9 $(printf '\267') A9sus4" ] &&
    [ "$(cb 5)" = "1 1 1" ] &&
    ok "Chord Names is the entry after Motion: Orchid; KNOB 1 +1: Standard (the engine told, bit 3 of the record's byte), the same three chords under it" \
    || bad "the Chord Names entry: '$(oe 4)' / '$(ul 4)' then '$(oe 5)' / '$(ul 5)' / $(cb 5)"
[ "$(cn 6)" = "A9|1|69 73 76 79 83" ] && [ "$(cn 7)" = "Aadd9|1|69 73 76 83" ] && [ "$(cn 8)" = "A9sus4|1|69 74 76 79 83" ] &&
    sn_ 'A||add9' && sn_ 'A||9sus4' &&
    ok "Standard: A + 7 + 9 'A9', A + 9 'Aadd9', Asus + 7 + 9 'A9sus4', each with the notes it had under Orchid" \
    || bad "Standard names: '$(cn 6)' / '$(cn 7)' / '$(cn 8)'"
[ "$(oe 9)" = "open 1 entry 22 of 33 'Chord Names' value 'Orchid'" ] && [ "$(cb 9)" = "0 0 0" ] && [ "$(cn 10)" = "A9|1|69 73 76 83" ] &&
    ok "KNOB 1 -1: Orchid again, A + 9 reads 'A9' again" || bad "back to Orchid: '$(oe 9)' / $(cb 9) / '$(cn 10)'"
differ cr_chord_names_opt_orchid cr_chord_names_opt_standard "Chord Names: the two values' screens differ"
differ cr_chord_names_orchid cr_chord_names_standard "A + 7 + 9: the Chord view differs under the two namings"
silent_end cr_chord_names
NF=$OUT/cr_chord_names_flash.bin
rm -f "$NF"
"$EMU" --headless --flash "$NF" --script "$S/cr_chord_names_set.txt" >"$OUT/cr_chord_names_set.log" 2>&1 || bad "cr_chord_names_set: exit $?"
"$EMU" --headless --flash "$NF" --script "$S/cr_chord_names_check.txt" >"$OUT/cr_chord_names_relaunch.log" 2>&1 || bad "cr_chord_names relaunch: exit $?"
L="$OUT/cr_chord_names_relaunch.log"
grep -q "settings saves 1 " "$OUT/cr_chord_names_set.log" && [ "$(cb 1)" = "1 1 1" ] && [ "$(cn 1)" = "Aadd9|1|69 73 76 83" ] &&
    ok "Chord Names Standard is saved by itself (an Option) and is the naming after a relaunch: A + 9 reads 'Aadd9'" \
    || bad "Chord Names saved: $(cb 1) / '$(cn 1)'"
"$EMU" --headless --flash "$NF" --script "$S/perf_a.txt" --wav "$OUT/cr_chord_names_std.wav" >/dev/null 2>&1
"$EMU" --headless --script "$S/perf_a.txt" --wav "$OUT/cr_chord_names_def.wav" >/dev/null 2>&1
cmp -s "$OUT/cr_chord_names_std.wav" "$OUT/cr_chord_names_def.wav" &&
    ok "a script played on the flash that says Standard sounds as on a new unit, bit for bit (the names are display only)" \
    || bad "Chord Names Standard changed the sound: cr_chord_names_std.wav differs from cr_chord_names_def.wav"

echo "Options > 6th with 7th (0.20): In place, the default, or Octave up: the 6th of a chord with a seventh an octave higher (docs/CHORD-NAMES.md)"
EMU_UI_LOG=1 "$EMU" --headless --script "$S/cr_sixth.txt" >"$OUT/cr_sixth.log" 2>&1 || bad "cr_sixth: exit $?"
L="$OUT/cr_sixth.log"
sx() { dl 'sixth:' "$1" | sed -n "s/^  sixth: up \([01]\) .* engine's \([01]\); the record's bit \([01]\))$/\1 \2 \3/p"; }
kl() { dl 'key leds:' "$1" | sed -n 's/^  key leds:\([0-9]* [0-9]*\) .*/\1/p'; }
[ "$(cn 1)" = "A7 6|1|69 73 76 78 79" ] && [ "$(sx 1)" = "0 0 0" ] && sn_ 'A||7 6' &&
    ok "a new unit plays the 6th in place, as the Orchid does: A + 7 + 6 is A C# E F# G (69 73 76 78 79), named 'A7 6'" \
    || bad "In place: '$(cn 1)' / $(sx 1)"
[ "$(oe 2)" = "open 1 entry 6 of 33 '6th with 7th' value 'In place'" ] && [ "$(ul 2)" = "6 + 7: the 6th under the 7th" ] &&
    [ "$(oe 3)" = "open 1 entry 6 of 33 '6th with 7th' value 'Octave up'" ] && [ "$(ul 3)" = "6 + 7: the 6th on top, a 13th" ] && [ "$(sx 3)" = "1 1 1" ] &&
    ok "6th with 7th is the entry after Secret Chords: In place; KNOB 1 +1: Octave up (the engine told, bit 4 of the record's byte), each with its line" \
    || bad "the 6th with 7th entry: '$(oe 2)' / '$(ul 2)' then '$(oe 3)' / '$(ul 3)' / $(sx 3)"
[ "$(cn 4)" = "A7 6|1|69 73 76 78 79" ] && dl 'part 0:' 4 | grep -q 'voices 5,' && [ "$(dl 'parts busy' 5 | sed -n 's/.*parts busy \([0-9]*\).*/\1/p')" = 0 ] &&
    ok "the chord held through the change keeps its five notes, and its release leaves nothing sounding 2.5 s later" \
    || bad "held through the change: '$(cn 4)' / $(dl 'part 0:' 4) / $(dl 'parts busy' 5)"
[ "$(cn 6)" = "A7 6|1|69 73 76 79 90" ] && [ "$(cn 7)" = "AM7 6|1|69 73 76 80 90" ] && [ "$(cn 8)" = "A6|1|69 73 76 78" ] &&
    [ "$(cn 9)" = "AJAZZ|1|69 73 76 79 83 90" ] &&
    ok "Octave up: A + 7 + 6 is 69 73 76 79 90 (the F# an octave up), A + M7 + 6 69 73 76 80 90, A + 7 + 6 + 9 with the 13th above the 9th; A + 6 alone as ever; the names the Orchid's" \
    || bad "Octave up: '$(cn 6)' / '$(cn 7)' / '$(cn 8)' / '$(cn 9)'"
[ -n "$(kl 1)" ] && [ "$(kl 6)" = "$(kl 1)" ] &&
    ok "the key LEDs light the same keys under either setting (the sounding notes' keys in both octaves: the F# is an F# still): $(kl 6)" \
    || bad "the key LEDs: '$(kl 1)' / '$(kl 6)'"
[ "$(cn 10)" = "A7 6|1|69 73 76 78 79" ] && [ "$(cn 11)" = "A7 6|1|69 73 76 79 90" ] &&
    ok "KNOB 1 -1 on the view: the voicing takes the top note, the 13th, down an octave; +1: back" || bad "the voicing: '$(cn 10)' / '$(cn 11)'"
[ "$(oe 12)" = "open 1 entry 6 of 33 '6th with 7th' value 'In place'" ] && [ "$(sx 12)" = "0 0 0" ] && [ "$(cn 13)" = "A7 6|1|69 73 76 78 79" ] &&
    ok "KNOB 1 -1 in the Options: In place again, A + 7 + 6 as at first" || bad "back to In place: '$(oe 12)' / $(sx 12) / '$(cn 13)'"
differ cr_sixth_opt_off cr_sixth_opt_on "6th with 7th: the two values' screens differ"
differ cr_sixth_off cr_sixth_on "A + 7 + 6: the Chord view's notes differ under the two settings"
silent_end cr_sixth
XF=$OUT/cr_sixth_flash.bin
rm -f "$XF"
"$EMU" --headless --flash "$XF" --script "$S/cr_sixth_set.txt" >"$OUT/cr_sixth_set.log" 2>&1 || bad "cr_sixth_set: exit $?"
"$EMU" --headless --flash "$XF" --script "$S/cr_sixth_check.txt" --wav "$OUT/cr_sixth_on.wav" >"$OUT/cr_sixth_relaunch.log" 2>&1 || bad "cr_sixth relaunch: exit $?"
"$EMU" --headless --script "$S/cr_sixth_check.txt" --wav "$OUT/cr_sixth_off.wav" >"$OUT/cr_sixth_fresh.log" 2>&1 || bad "cr_sixth fresh: exit $?"
L="$OUT/cr_sixth_relaunch.log"
grep -q "settings saves 1 " "$OUT/cr_sixth_set.log" && [ "$(sx 1)" = "1 1 1" ] && [ "$(cn 1)" = "A7 6|1|69 73 76 79 90" ] &&
    ok "6th with 7th Octave up is saved by itself (an Option) and is the setting after a relaunch: A + 7 + 6 sounds 69 73 76 79 90" \
    || bad "6th with 7th saved: $(sx 1) / '$(cn 1)'"
silent_end cr_sixth_relaunch
cmp -s "$OUT/cr_sixth_on.wav" "$OUT/cr_sixth_off.wav" && bad "A + 7 + 6 sounds the same on the flash that says Octave up: cr_sixth_on.wav = cr_sixth_off.wav" \
    || ok "A + 7 + 6 on the flash that says Octave up is another sound than on a new unit: $OUT/cr_sixth_on.wav, cr_sixth_off.wav"
"$EMU" --headless --flash "$XF" --script "$S/cr_chord_names_check.txt" --wav "$OUT/cr_sixth_perf_on.wav" >/dev/null 2>&1
"$EMU" --headless --script "$S/cr_chord_names_check.txt" --wav "$OUT/cr_sixth_perf_def.wav" >/dev/null 2>&1
cmp -s "$OUT/cr_sixth_perf_on.wav" "$OUT/cr_sixth_perf_def.wav" &&
    ok "a chord without a 6 + 7 (A + 9, cr_chord_names_check.txt) played on that flash sounds as on a new unit, bit for bit (Octave up touches nothing else)" \
    || bad "Octave up changed a sound without a 6 + 7 chord: cr_sixth_perf_on.wav differs from cr_sixth_perf_def.wav"

echo "GLO + ALGORITHM is the bass engine, GLO + PRESETS the chord engine, wherever the knob alone is the part's sound (0.19)"
export EMU_UI_LOG=0
run cr_glo_knobs
unset EMU_UI_LOG
GK="$OUT/cr_glo_knobs.log"
gp() { grep -a "^  part $1: " "$GK" | sed -n "$2p" | sed -n 's/^  part [01]: \(.*\), voices.*/\1/p'; }      # gp PART N: "VA / LUSH PAD"
gl() { grep -a '^  cr: key' "$GK" | sed -n "$1p" | sed -n 's/.* bass \([0-9]\) (sound \([0-9]*\)).* layer \([0-9]*\) .*/\3/p'; }   # the layer open
gb() { grep -a '^  cr: key' "$GK" | sed -n "$1p" | sed -n 's/.* bass \([0-9]\) (sound.*/\1/p'; }               # the bass on
go() { grep -a '^  options:' "$GK" | sed -n "$1p" | sed -n 's/^  options: open \([0-9]\).*/\1/p'; }
A="VA / LUSH PAD"; B="CZ-1 / BRASS 1"; P="FM6 / TINE EP"; Q="FM TONE / EP"
# a "yes" context: dump N after GLO + ALGORITHM (the bass part on $2), dump N + 1 after GLO + PRESETS (the chord part on
# $3; the bass part still on $2), the layer $4 open at both
gyes() {
    n=$1; m=$(($1 + 1))
    [ "$(gp 1 $n)" = "$2" ] && [ "$(gp 1 $m)" = "$2" ] && [ "$(gp 0 $m)" = "$3" ] && [ "$(gl $n)" = "$4" ] && [ "$(gl $m)" = "$4" ] &&
        ok "$5: GLO + ALGORITHM the bass engine ($2), GLO + PRESETS the chord engine ($3), the place still open" \
        || bad "$5: bass '$(gp 1 $n)' chord '$(gp 0 $m)' layer $(gl $n)/$(gl $m) (wanted '$2', '$3', layer $4)"
}
[ "$(gp 0 1)" = "$P" ] && [ "$(gp 1 1)" = "$A" ] && ok "the start: the chord part FM6 TINE EP, the bass on VA LUSH PAD" || bad "the start: $(gp 0 1) / $(gp 1 1)"
gyes 2 "$B" "$Q" 0 "home"
gyes 4 "$A" "$P" 0 "the Options"
[ "$(go 4)" = 1 ] && [ "$(go 5)" = 1 ] && ok "  the Options stay open under it (GLO held is no tap)" || bad "the Options after GLO + a knob: open $(go 4) / $(go 5)"
gyes 6 "$B" "$Q" 1 "the KEY layer"
gyes 8 "$A" "$P" 2 "the PERF layer"
gyes 10 "$B" "$Q" 3 "the FX layer"
gyes 12 "$A" "$P" 4 "the BASS layer"
gyes 14 "$B" "$Q" 7 "the SEQ layer (metronome)"
gyes 16 "$A" "$P" 10 "the LOCK layer"
gyes 18 "$B" "$Q" 9 "the view menu"
gyes 20 "$A" "$P" 2 "a layer over the sound editor (PERF)"
[ "$(gp 1 22)" = "$B" ] && [ "$(gp 0 22)" = "$P" ] && [ "$(gl 22)" = 0 ] && [ "$(gl 23)" = 5 ] && [ "$(gp 0 23)" = "FM6 / FM BELL" ] &&
    [ "$(gp 1 23)" = "$B" ] && has '^picker: open part 0 TINE EP' "$GK" &&
    ok "the sound editor (chord part): GLO + ALGORITHM the bass engine ($B); GLO + PRESETS opens the editor's own picker, previewing (PRESETS's job there), no engine picker" \
    || bad "the sound editor: bass '$(gp 1 22)' layer $(gl 22), then chord '$(gp 0 23)' layer $(gl 23)"
[ "$(gp 1 24)" = "$A" ] && [ "$(gp 0 24)" = "$P" ] && [ "$(gl 24)" = 0 ] && has '^edit: open part 1' "$GK" &&
    ok "the sound editor on the bass part: GLO + ALGORITHM the bass engine ($A); HOME in the picker had given the chord sound back" \
    || bad "the bass sound's editor: bass '$(gp 1 24)' chord '$(gp 0 24)' layer $(gl 24)"
[ "$(lf "$(L cr_glo_knobs 25)" mode)" = 1 ] && [ "$(gp 1 25)" = "$A" ] && [ "$(gp 0 26)" = "$P" ] && [ "$(gl 25)" = 8 ] && [ "$(gl 26)" = 8 ] &&
    has '^loop: mode Advance' "$GK" && has '^loop: slot 2 turned' "$GK" &&
    ok "NOT in the PLAY menu: GLO + ALGORITHM is the record mode (Advance), GLO + PRESETS the slot (2); both sounds as they were" \
    || bad "the PLAY menu: mode $(lf "$(L cr_glo_knobs 25)" mode), bass '$(gp 1 25)' chord '$(gp 0 26)'"
[ "$(gp 1 27)" = "VA / WARM PAD" ] && [ "$(gl 27)" = 5 ] && [ "$(gp 0 28)" = "FM6 / FM BELL" ] && [ "$(gl 28)" = 5 ] &&
    ok "NOT in the editor's picker (EDIT held): GLO + ALGORITHM is the bass sound (VA 02), GLO + PRESETS the picker's presets, as the knobs alone" \
    || bad "the editor's picker: bass '$(gp 1 27)' chord '$(gp 0 28)' layer $(gl 27)/$(gl 28)"
[ "$(gp 1 29)" = "VA / GLASS PAD" ] && [ "$(gp 0 29)" = "$P" ] && [ "$(gp 0 30)" = "FM6 / FM BELL" ] && has '^save: dialog part 0 TINE EP' "$GK" &&
    ok "NOT on the save dialog: GLO + ALGORITHM is the bass sound (VA 03), GLO + PRESETS the chord sound, as the knobs alone (HOME in the picker had given TINE EP back)" \
    || bad "the save dialog: bass '$(gp 1 29)' chord '$(gp 0 29)' -> '$(gp 0 30)'"
[ "$(grep -ac '^engine pick: bass' "$GK")" = 13 ] && [ "$(grep -ac '^engine pick: F' "$GK")" = 10 ] &&
    ok "the engine picker opened 13 times for the bass part and 10 for the chord part: once per place above, never in the PLAY menu, the editor's picker or the save dialog" \
    || bad "engine pickers: $(grep -ac '^engine pick: bass' "$GK") bass (13), $(grep -ac '^engine pick: F' "$GK") chord (10)"
[ "$(grep -ac "^home read-out: part 1 over 'CZ-1' line 'Hold OPT to change engine'" "$GK")" = 7 ] &&
    [ "$(grep -ac "^home read-out: part 0 over 'FM TONE' line 'Hold OPT to change engine'" "$GK")" = 5 ] &&
    ! grep -a -A1 '^popup: part 1 02 / WARM PAD' "$GK" | grep -aq '^home read-out' &&
    ! grep -a -A1 '^popup: part 1 03 / GLASS PAD' "$GK" | grep -aq '^home read-out' &&
    ok "the same read-out everywhere it works (the engine over the place, 'Hold OPT to change engine'); in the editor's picker and the save dialog the old line (VA · 02/25): OPT does nothing there" \
    || bad "the read-out's hint: $(grep -ac "^home read-out: part 1 over 'CZ-1'" "$GK") / $(grep -ac "^home read-out: part 0 over 'FM TONE'" "$GK")"
grep -a '^ui: frame.* picker ' "$GK" | grep -aq "item 'CZ-1' value '64 presets'" && grep -a '^ui: frame.* picker ' "$GK" | grep -aq "item 'FM TONE' value '16 presets'" &&
    ok "the picker itself is the home pages': the engine big, its presets' count: $OUT/cr_glo_knobs_key_pick.ppm (the KEY layer), cr_glo_knobs_edit_pick.ppm (the editor), cr_glo_knobs_opt.ppm" \
    || bad "the engine picker's screen in a layer"
[ "$(gp 1 31)" = "VA / SLOW STRINGS" ] && [ "$(gl 31)" = 7 ] && [ "$(gp 0 31)" = "FM6 / FM BELL" ] &&
    ok "the held-layer rule: SEQ held past the hold time and still down, GLO pressed too, ALGORITHM: the bass sound (VA 04), SEQ's layer open; no engine picker (GLO is the shift only when it is the button held)" \
    || bad "SEQ held + GLO + ALGORITHM: bass '$(gp 1 31)' layer $(gl 31)"
[ "$(gb 32)" = 0 ] && [ "$(gp 1 32)" = "$B" ] && [ "$(gl 32)" = 3 ] &&
    ok "a bass that is off, in the FX layer: GLO + ALGORITHM changes its engine ($B), the bass stays off (as on the home pages)" \
    || bad "the bass off in a layer: on $(gb 32) '$(gp 1 32)' layer $(gl 32)"
differ cr_glo_knobs_key_pick cr_glo_knobs_key_pop "the picker in the KEY layer, then the pool read-out on GLO's release"

echo "favourites (0.22, docs/PRESETS.md): the SAVE dialog's third choice, KNOB 4 there and in the editor's browser, the star"
run cr_favs
FL="$OUT/cr_favs.log"
fv() { grep -a '^  favs:' "$FL" | sed -n "$1p"; }
star() { yellow_in "$OUT/$1.ppm" 0 0 57600; }      # the yellow pixels of a shot: the star's (nothing else is yellow there)
has '^save: dialog part 0 TINE EP (save as new)' "$FL" && [ "$(grep -m1 '^save: choice' "$FL")" = "save: choice favorite" ] &&
    ! cmp -s "$OUT/cr_favs_save2.ppm" "$OUT/cr_favs_not.ppm" && [ "$(star cr_favs_not)" = 0 ] &&
    ok "SAVE on TINE EP: the dialog on Save as new as before; SELECT +1: the third choice, \"Not a favorite\", no star: $OUT/cr_favs_save2.ppm cr_favs_not.ppm" ||
    bad "the third choice: $(grep -m2 '^save:' "$FL" | tr '\n' ' ')"
has '^fav: part 0 TINE EP on (tag 12 id 0, 1 marked)' "$FL" && fv 1 | grep -q 'favs: 1 marked, saves 0, pending 1 .* : 12:0$' &&
    [ "$(star cr_favs_on)" -ge 10 ] && ! cmp -s "$OUT/cr_favs_not.ppm" "$OUT/cr_favs_on.ppm" &&
    ok "KNOB 4 right on Favorite: TINE EP marked at once (FM6's preset 0), \"Favorite\" and the star before the name ($(star cr_favs_on) yellow px in its row): $OUT/cr_favs_on.ppm" ||
    bad "KNOB 4 +1: $(grep -m1 '^fav:' "$FL") / $(fv 1) / yellow $(star cr_favs_on)"
has '^save: favorite, nothing to save' "$FL" && ! has '^save: part 0 .*slot' "$FL" && ! has '^save: new part 0' "$FL" &&
    has '^fav: part 0 TINE EP off (tag 12 id 0, 0 marked)' "$FL" && fv 2 | grep -q 'favs: 0 marked' &&
    ok "OCT+ on Favorite: nothing saved, no naming page, no slot; KNOB 4 left: the mark off" || bad "OCT+ / KNOB 4 -1: $(grep '^save:\|^fav:' "$FL" | sed -n 3,6p | tr '\n' ' ')"
[ "$(star cr_favs_save1)" -ge 10 ] && [ "$(grep -c '^save: choice save as new' "$FL")" = 1 ] &&
    ok "a turn left from Favorite: Save as new (a turn is a step, however fast); a marked sound's star shows under the other choices too: $OUT/cr_favs_save1.ppm" ||
    bad "the star on Save as new: $(star cr_favs_save1)"
! has '^save: global' "$FL" && [ "$(grep -c '^save: dialog part 0' "$FL")" = 2 ] &&
    has '^popup: part 0 01 / TINE EP (star) / ' "$FL" && has '^popup: part 0 02 / FM BELL / ' "$FL" &&
    [ "$(star cr_favs_pop01)" -ge 10 ] && [ "$(star cr_favs_pop02)" = 0 ] &&
    ok "SAVE again on Favorite closes the dialog; PRESETS: the pool read-out of 01 TINE EP with the star, 02 FM BELL without: $OUT/cr_favs_pop01.ppm cr_favs_pop02.ppm" ||
    bad "the read-out: $(grep '^popup: part 0 0[12]' "$FL" | head -3 | tr '\n' ' ') yellow $(star cr_favs_pop01) / $(star cr_favs_pop02)"
has '^picker: open part 0 TINE EP' "$FL" && has '^fav: part 0 FM BELL on (tag 12 id 1, 2 marked)' "$FL" &&
    has '^popup: part 0 02 / FM BELL (star) / FM6 .* 02/25' "$FL" && ! has '^fav: part 0 FM BASS on' "$FL" &&
    has '^picker: cancel part 0 -> TINE EP' "$FL" && fv 3 | grep -q 'favs: 2 marked, saves [1-9][0-9]*, pending 0 .* : 12:0 12:1$' &&
    [ "$(star cr_favs_browser_star)" -ge 10 ] && [ "$(star cr_favs_browser)" = 0 ] && [ "$(star cr_favs_browser_next)" = 0 ] &&
    ok "the editor's browser (PRESETS in the editor): KNOB 4 right marks FM BELL under the cursor, the browser stays, the star beside the name; the next sound has none; OCT- gives TINE EP back, both marks stay and are in flash: $OUT/cr_favs_browser_star.ppm" ||
    bad "the browser: $(grep '^fav:\|^picker:' "$FL" | tr '\n' ' ') / $(fv 3) / yellow $(star cr_favs_browser_star)"
[ "$(grep -c 'cannot be marked$' "$FL")" = 1 ] && fv 4 | grep -q 'favs: 2 marked' &&
    ok "INIT cannot be a favourite: KNOB 4 on it says so, nothing changes: $OUT/cr_favs_init.ppm" || bad "INIT: $(grep -c 'cannot be marked' "$FL") refusals, $(fv 4)"
has '^save: dialog part 1 ' "$FL" && grep -q '^fav: part 1 .* on (tag 13 id [0-9]*, 3 marked)' "$FL" && fv 5 | grep -q 'favs: 3 marked.*pending 0 .* 13:' &&
    [ "$(star cr_favs_bass)" -ge 10 ] &&
    ok "the bass part's dialog (BASS + EDIT, SAVE): its sound marked the same way, the star on the orange name: $OUT/cr_favs_bass.ppm" ||
    bad "the bass: $(grep '^fav: part 1' "$FL") / $(fv 5)"

echo "the SAVE menu's grammar, the favourites' filter and type (0.22, docs/PRESETS.md \"Favourites\")"
# 3y48: a mark made while an arp plays, a backup LIST and the debug reads during it: nothing written until it is quiet
run cr_favs_arp_list
AL="$OUT/cr_favs_arp_list.log"
al() { grep -a '^  flash erases with the audio stalled' "$AL" | sed -n "$1p" | awk '{print $7}'; }
af() { grep -a '^  favs:' "$AL" | sed -n "$1p"; }
[ "$(al 1)" = 0 ] && [ "$(al 2)" = 0 ] && [ "$(al 3)" = 0 ] && af 3 | grep -q 'pending 1' && [ "$(al 4)" = 0 ] && af 4 | grep -q 'saves 1, pending 0' &&
    ok "a mark while an arp plays: LIST, DEBUG, PEAK, USB and 30 s of the arp write nothing; quiet: the poll writes it (3y52: into the blank spare, program only, no erase)" ||
    bad "the arp and the reads: erases $(al 1) $(al 2) $(al 3) $(al 4); $(af 3) / $(af 4)"
run cr_favfilter
FF="$OUT/cr_favfilter.log"
ffv() { grep -a '^  favs:' "$FF" | sed -n "$1p"; }
fstar() { yellow_in "$OUT/$1.ppm" 0 0 57600; }
fline() { yellow_in "$OUT/$1.ppm" 196 0 7200; }    # the yellow of rows 196..225: the read-out's line under the stripes
fsnd() { grep -a '^sound: ' "$FF" | sed -n "$1,$2p" | sed 's/^sound: part //' | tr '\n' ';'; }
[ "$(grep -a '^save: choice' "$FF" | head -5 | tr '\n' ';')" = "save: choice overwrite;save: choice save as new;save: choice favorite;save: choice filter;save: choice type;" ] &&
    has '^save: knob 1 on save as new: no value' "$FF" && has '^save: knob 1 on overwrite: no value' "$FF" &&
    cmp -s "$OUT/cr_ff_e2.ppm" "$OUT/cr_ff_e2_k1.ppm" && ! cmp -s "$OUT/cr_ff_e2.ppm" "$OUT/cr_ff_e1.ppm" &&
    ok "the menu: SELECT picks the entry (Overwrite, Save as new, Favorite, Favorites filter, Favorite type); KNOB 1 on the two saves changes nothing (the screen pixel for pixel): $OUT/cr_ff_e1.ppm cr_ff_e2.ppm" ||
    bad "the menu's entries: $(grep -a '^save: choice\|^save: knob' "$FF" | head -8 | tr '\n' ' ')"
[ "$(fstar cr_ff_e1)" = 0 ] && [ "$(fstar cr_ff_e1_star)" -ge 10 ] && [ "$(fstar cr_ff_e3_off)" = 0 ] && [ "$(fstar cr_ff_e3_on)" -ge 10 ] &&
    [ "$(grep -a -c '^fav: part 0 TINE EP on (tag 12 id 0, 1 marked)' "$FF")" = 2 ] && ! cmp -s "$OUT/cr_ff_e3_off.ppm" "$OUT/cr_ff_e3_on.ppm" &&
    ok "KNOB 4 on Overwrite marks the sound (the star; until this menu: the chord part's send); on Favorite KNOB 1 changes the value: \"Not a favorite\" -> \"Favorite\": $OUT/cr_ff_e1_star.ppm cr_ff_e3_off.ppm cr_ff_e3_on.ppm" ||
    bad "the mark: stars $(fstar cr_ff_e1) $(fstar cr_ff_e1_star) $(fstar cr_ff_e3_off) $(fstar cr_ff_e3_on) / $(grep -a '^fav:' "$FF" | head -3 | tr '\n' ' ')"
! cmp -s "$OUT/cr_ff_e4_off.ppm" "$OUT/cr_ff_e4_on.ppm" && ! cmp -s "$OUT/cr_ff_e5_both.ppm" "$OUT/cr_ff_e5_chord.ppm" &&
    ! cmp -s "$OUT/cr_ff_e5_chord.ppm" "$OUT/cr_ff_e5_bass.ppm" && ! cmp -s "$OUT/cr_ff_e5_chord.ppm" "$OUT/cr_ff_e5_none.ppm" &&
    [ "$(grep -a '^fav: part 0 TINE EP type' "$FF" | head -4 | sed 's/.*type \([A-Za-z]*\).*/\1/' | tr '\n' ' ')" = "Chord Bass Bass Chord " ] &&
    [ "$(fstar cr_ff_e5_chord)" -ge 10 ] && [ "$(fstar cr_ff_e5_none)" = 0 ] &&
    has '^fav: part 0 TINE EP type Bass, marked by it (tag 12 id 0, 1 marked)' "$FF" && ffv 1 | grep -q 'favs: 1 marked, .* : 12:0c$' &&
    [ "$(grep -a -c '^save: favorite, nothing to save' "$FF")" = 1 ] &&
    ok "Favorite type: KNOB 1 a step a turn, Both -> Chord -> Bass, +3: Bass still, -1: Chord; OCT+ saves nothing; KNOB 4 -1 takes the mark off and the type stays Chord (no star); the next turn of the type marks it again: $OUT/cr_ff_e5_both.ppm cr_ff_e5_chord.ppm cr_ff_e5_bass.ppm cr_ff_e5_none.ppm" ||
    bad "the type: $(grep -a '^fav: part 0 TINE EP type' "$FF" | tr '\n' ' ') / $(ffv 1)"
has '^fav: part 0 FM BASS type Chord, marked by it (tag 12 id 2, 2 marked)' "$FF" && [ "$(grep -a -c '^fav: part 0 FM BASS type Bass (' "$FF")" = 2 ] &&
    has '^fav: part 0 FM BASS off' "$FF" && [ "$(fstar cr_ff_k3)" -ge 500 ] && [ "$(fstar cr_ff_k3_after)" -ge 10 ] && [ "$(fstar cr_ff_k3_after)" -lt 200 ] &&
    ffv 2 | grep -q 'favs: 3 marked, .*, filter on : 12:0c 12:2b 12:3$' && ! cmp -s "$OUT/cr_ff_e4_off.ppm" "$OUT/cr_ff_e4_on.ppm" &&
    ok "KNOB 3 on Save as new: the sound's type (Chord, and the mark; the message \"Type: Chord\"), +1: Bass, +1: Bass; KNOB 4 -1 +1 leaves it Bass; KNOB 4 on Save as new marks BRASS (Both); the filter On: $OUT/cr_ff_k3.ppm cr_ff_k3_after.ppm cr_ff_e4_on.ppm" ||
    bad "KNOB 3 / the marks: $(grep -a '^fav: part 0 FM BASS' "$FF" | tr '\n' ' ') / $(ffv 2) / yellow $(fstar cr_ff_k3) $(fstar cr_ff_k3_after)"
[ "$(fsnd 3 6)" = "0 pos 1 TINE EP;0 pos 4 BRASS;0 pos 1 TINE EP;0 pos 4 BRASS;" ] &&
    has "^home read-out: part 0 over 'FM6' line 'favorites 01/02'" "$FF" && has "^home read-out: part 0 over 'FM6' line 'favorites 02/02'" "$FF" &&
    [ "$(fline cr_ff_pop)" -ge 40 ] && [ "$(fstar cr_ff_pop)" -gt "$(fline cr_ff_pop)" ] &&
    ok "the filter on, the chord part (FM6: 01 Chord, 03 Bass, 04 Both): PRESETS +1 from 04: 01 (round; 03 is the bass part's), +1: 04, -1: 01, -1: 04; the read-out's line is the place among them, \"favorites 01/02\", in the star's colour ($(fline cr_ff_pop) px): $OUT/cr_ff_pop.ppm" ||
    bad "the chord part's turns: $(fsnd 3 6) / line $(fline cr_ff_pop)"
[ "$(fsnd 7 8)" = "1 pos 1 INIT;1 pos 2 LUSH PAD;" ] && has "^home read-out: part 1 over 'VA' line 'no favorites: all'" "$FF" && [ "$(fline cr_ff_pop_none)" -ge 40 ] &&
    ok "the bass part on VA, which has no favourite: ALGORITHM turns VA's whole pool, the read-out says \"no favorites: all\": $OUT/cr_ff_pop_none.ppm" ||
    bad "the engine without favourites: $(fsnd 7 8) / line $(fline cr_ff_pop_none)"
[ "$(fsnd 9 15)" = "1 engine FM6 pos 3 FM BASS;1 pos 5 BRASS;1 pos 0 off;1 pos 4 FM BASS;1 pos 0 off;1 pos 5 BRASS;1 pos 4 FM BASS;" ] &&
    has '^popup: part 1 03 / FM BASS (star) / FM6 .* favorites 01/02' "$FF" && [ "$(fline cr_ff_pop_bass)" -ge 40 ] &&
    ok "GLO + ALGORITHM to FM6: the bass part lands on FM6's first favourite of it, 03 FM BASS (01 is the chord part's); ALGORITHM +1: 04, +1: OFF, +1: 03, -1: OFF, -1: 04 (round), -1: 03: $OUT/cr_ff_pop_bass.ppm" ||
    bad "the bass part's turns: $(fsnd 9 15)"
ffv 3 | grep -q 'favs: 2 marked, .*, filter on : 12:0c 12:2b$' && grep -a -B30 '^  favs: 2 marked' "$FF" | grep -a 'part 0:' | tail -1 | grep -q 'BRASS' &&
    [ "$(fsnd 16 17)" = "0 pos 1 TINE EP;0 pos 1 TINE EP;" ] && has "line 'favorites 01/01'" "$FF" &&
    ok "the mark of the sound playing taken off (04 BRASS): it plays on; PRESETS -1: the nearest favourite that way, 01 TINE EP; +1: 01 still (the chord part's only one)" ||
    bad "un-marking the current sound: $(ffv 3) / $(fsnd 16 17) / $(grep -a -B30 '^  favs: 2 marked' "$FF" | grep -a 'part 0:' | tail -1)"
[ "$(grep -a '^fav: filter' "$FF" | sed 's/^fav: filter \([a-z]*\).*/\1/' | tr '\n' ' ')" = "on off on off " ] && [ "$(fstar cr_ff_k2)" -ge 500 ] &&
    ok "KNOB 2 on Save as new: the favourites filter Off (the message \"Filter off\"), +1: On again (until this menu: the bass register): $OUT/cr_ff_k2.ppm" ||
    bad "KNOB 2: $(grep -a '^fav: filter' "$FF" | tr '\n' ' ') / yellow $(fstar cr_ff_k2)"
has '^save: new part 0 FM6 26 (slot U001)' "$FF" && grep -a -q '^fav: part 0 .* on (tag 128 id 0, 3 marked)' "$FF" &&
    [ "$(fsnd 18 21 | sed 's/pos 26 [A-Z0-9 ]*;/pos 26;/g')" = "0 pos 1 TINE EP;0 pos 26;0 pos 1 TINE EP;0 pos 26;" ] &&
    ok "a user sound (Save as new: FM6 26, U01) marked by its slot is one of the favourites: PRESETS turns 01, 26, 01, 26" ||
    bad "the user sound: $(fsnd 18 21) / $(grep -a '^fav: part 0 .* (tag 128' "$FF")"
[ "$(fsnd 22 25)" = "0 engine FM TONE pos 1 EP;0 pos 2 BELL;0 pos 3 BASS;0 engine FM6 pos 1 TINE EP;" ] &&
    has "^home read-out: part 0 over 'FM TONE' line 'no favorites: all'" "$FF" && [ "$(fline cr_ff_pop_other)" -ge 40 ] && [ "$(fline cr_ff_pop_land)" -ge 40 ] &&
    ffv 4 | grep -q 'favs: 3 marked, .*, filter on : 128:0 12:0c 12:2b$' &&
    ok "an engine change with the filter on: GLO + PRESETS to FM TONE (no favourite: its remembered place, the whole pool, \"no favorites: all\"); back to FM6: its first favourite, 01 TINE EP, not 26 where it was left: $OUT/cr_ff_pop_other.ppm cr_ff_pop_land.ppm" ||
    bad "the engine change: $(fsnd 22 25) / $(ffv 4)"
[ "$(fsnd 26 27 | sed 's/pos 26 [A-Z0-9 ]*;/pos 26;/g')" = "0 pos 4 BRASS (program change);0 pos 26;" ] && has "line 'favorites --/02'" "$FF" &&
    ok "not filtered: a program change loads FM6 04 BRASS, no favourite (the read-out: \"favorites --/02\"); PRESETS +1 from it: the nearest favourite after it, 26" ||
    bad "the program change: $(fsnd 26 27)"
has '^fav: filter off' "$FF" && [ "$(fsnd 28 28)" = "0 pos 0 INIT;" ] && [ "$(fline cr_ff_pop_off)" = 0 ] &&
    has "^popup: part 0 00 / INIT / FM6 .* 00/26" "$FF" && ffv 5 | grep -q 'favs: 3 marked, saves [1-9][0-9]*, pending 0 (at power-on: none) : 128:0 12:0c 12:2b$' &&
    ok "the filter Off again (KNOB 1 -1 on its entry): PRESETS +1 from 26: 00 INIT, the whole pool, the read-out as ever (no yellow line); the marks, the types and the filter are in flash" ||
    bad "the filter off: $(fsnd 28 28) / line $(fline cr_ff_pop_off) / $(ffv 5)"

echo "naming a sound with a cursor (0.22): SELECT the cursor, KNOB 1 the letter, KNOB 4 left the backspace, the other knobs nothing"
run cr_naming
NM="$OUT/cr_naming.log"
nml() { grep -a '^name: ' "$NM" | sed -n "$1p" | sed "s/^name: \([a-z]*\)[^']*\('.*\)/\1 \2/"; }
nmd() { grep -a "$1" "$NM" | sed -n "$2p"; }
nred() { px_count "$OUT/$1.ppm" 0 110 240 125 red; }   # the cursor's bar (rows 110..124 hold nothing else red)
[ "$(grep -a -c '^name: nothing' "$NM")" = 7 ] && [ "$(nml 7)" = "nothing 'TINE EP' cursor 6 of 7" ] && cmp -s "$OUT/cr_nm_open.ppm" "$OUT/cr_nm_inert.ppm" &&
    [ "$(nmd '^  part 0:' 1)" = "$(nmd '^  part 0:' 2)" ] && [ "$(nmd '^  part 1:' 1)" = "$(nmd '^  part 1:' 2)" ] && [ "$(nmd '^  cr: key' 1)" = "$(nmd '^  cr: key' 2)" ] &&
    nmd '^  part 0:' 2 | grep -q 'FM6 / TINE EP' && ! has '^sound:' "$NM" && ! has '^popup:' "$NM" && ! has '^engine pick' "$NM" && ! has '^send:' "$NM" &&
    [ "$(nred cr_nm_open)" -ge 30 ] &&
    ok "the naming page opens with the cursor on the last character (P of TINE EP, a red bar under it); PRESETS, ALGORITHM, KNOB 2, KNOB 3, KNOB 4 right and GLO + PRESETS / + ALGORITHM do nothing: the name, the sound (FM6 TINE EP), the engines, the bass, the tempo and the screen (pixel for pixel) as they were: $OUT/cr_nm_open.ppm cr_nm_inert.ppm" ||
    bad "the inert knobs: $(grep -a -c '^name: nothing' "$NM") / $(nml 7) / $(nmd '^  part 0:' 2) / $(grep -a '^sound:\|^popup:\|^engine pick' "$NM" | head -3 | tr '\n' ' ')"
[ "$(nml 8)" = "letter 'TINE EQ' cursor 6 of 7" ] && [ "$(nml 10)" = "cursor 'TINE EP' cursor 5 of 7" ] && [ "$(nml 11)" = "letter 'TINE FP' cursor 5 of 7" ] &&
    [ "$(nml 13)" = "backspace 'TINEFP' cursor 3 of 6" ] && [ "$(nml 15)" = "cursor 'TINEFP' cursor 5 of 6" ] && [ "$(nml 16)" = "backspace 'TINEF' cursor 4 of 5" ] &&
    [ "$(nml 17)" = "cursor 'TINEFA' cursor 5 of 6" ] && [ "$(nml 18)" = "letter 'TINEFC' cursor 5 of 6" ] && [ "$(nred cr_nm_cursor)" -ge 30 ] &&
    ! cmp -s "$OUT/cr_nm_open.ppm" "$OUT/cr_nm_cursor.ppm" && ! cmp -s "$OUT/cr_nm_cursor.ppm" "$OUT/cr_nm_letter.ppm" &&
    ok "the knobs alone: KNOB 1 +1 changes the last letter at once (P -> Q, as KNOB 2 did); SELECT -1: the cursor on the second E; KNOB 1 +1: F; SELECT -1, KNOB 4 -1: the space under the cursor goes, the cursor on the E before it (TINEFP); KNOB 4 -1 on the last character: TINEF; SELECT +1 from the last character: a new one, A, the cursor on it; KNOB 1 +2: C: $OUT/cr_nm_cursor.ppm cr_nm_letter.ppm" ||
    bad "the knobs: $(grep -a '^name: ' "$NM" | sed -n 8,18p | sed "s/^name: \([a-z]*\)[^']*/\1 /" | tr '\n' '/') red $(nred cr_nm_cursor)"
[ "$(nml 19)" = "key 'TINEFCA' cursor 6 of 7" ] && [ "$(nml 23)" = "key 'TINEDFCA' cursor 4 of 8" ] && [ "$(nml 24)" = "key 'TINEFCA' cursor 3 of 7" ] &&
    ok "the keys still type: D4 with the cursor on the last character appends A, the cursor on it; E4 with the cursor on the E puts D after it and the cursor on the D (TINEDFCA); F#4 is the backspace: $OUT/cr_nm_insert.ppm" ||
    bad "the keys: $(nml 19) / $(nml 23) / $(nml 24)"
[ "$(grep -a -c "'TINEFCAAAAAA' cursor 11 of 12" "$NM")" = 2 ] && ! grep -a -q "cursor 12 of" "$NM" &&
    [ "$(px_count "$OUT/cr_nm_full.ppm" 200 86 240 110 ink)" -lt "$(px_count "$OUT/cr_nm_full.ppm" 10 86 50 110 ink)" ] && [ "$(nred cr_nm_full)" -ge 20 ] &&
    has '^save: part 0 slot U001 name TIN rc 0' "$NM" &&
    ok "the maximum: SELECT right from the last character adds characters up to 12 and stops there (the name set smaller so that none is cut, the cursor on the twelfth: $OUT/cr_nm_full.ppm); nine backspaces: TIN; OCT+: saved as FM6 26 TIN" ||
    bad "the maximum: $(grep -a "cursor 1[12] of 12" "$NM" | tr '\n' ' ') / $(grep -a '^save: part' "$NM" | tr '\n' ' ')"
grep -a -q "^name: backspace .*'' cursor 0 of 0" "$NM" && has '^save: part 0 slot U002 name  rc 0' "$NM" && [ "$(nred cr_nm_empty)" = 0 ] &&
    ok "an empty name (three backspaces): no character, no cursor; OCT+ saves it as an empty name always was (the slot's automatic name): $OUT/cr_nm_empty.ppm" ||
    bad "the empty name: $(grep -a '^save: part 0 slot U002' "$NM") / red $(nred cr_nm_empty)"

echo "Options > Chord Keys FM-1 (0.24, docs/CHORD-LAYOUT.md): the FM-1 layout, the SUS Window, LOCK on LFO, LATCH on GLO + LFO"
EMU_UI_LOG=1 "$EMU" --headless --script "$S/cr_fm1_keys.txt" >"$OUT/cr_fm1_keys.log" 2>&1 || bad "cr_fm1_keys: exit $?"
L="$OUT/cr_fm1_keys.log"
dl() { LC_ALL=C grep -a "^  $1" "$L" | sed -n "$2p"; }
fk() { dl 'cr: voices' "$1" | sed -n 's/.* chord \(.*\) sounding \([01]\) notes\(.*\)$/\1|\2|\3/p'; }    # "Dsus|1| 62 67 69"
fo() { dl 'cr: voices' "$1" | sed -n 's/.* notes-out \([0-9]*\) .*/\1/p'; }
fc() { dl 'chord keys:' "$1" | sed 's/^  chord keys: //'; }
fl() { fc "$1" | sed -n 's/.* latch \([01]\) lfo-led \([0-4]\)$/\1 \2/p'; }
oe() { dl 'options:' "$1" | sed 's/^  options: //'; }
[ "$(oe 1)" = "open 1 entry 3 of 34 'Chord Keys' value 'FM-1'" ] && [ "$(oe 2)" = "open 1 entry 4 of 34 'SUS Window' value '80 ms'" ] &&
    ok "Chord Keys is the entry after Secret Chords: FM-1; the SUS Window is listed after it in FM-1 only (34 entries; 33 in Classic), 30 ms by default, set to 80 ms" \
    || bad "the Chord Keys / SUS Window entries: '$(oe 1)' / '$(oe 2)'"
[ "$(fk 3)" = "Dsus|1| 62 67 69" ] && ok "G#3 + A#3, then D4: Dsus (62 67 69)" || bad "the pair: '$(fk 3)'"
[ "$(dl 'cr: voices' 4 | sed -n 's/.*cr: voices \([0-9]*\) .*/\1/p')" = 0 ] && fc 4 | grep -q 'gesture 1 kwait 0000200 ' &&
    [ "$(fk 5)" = "Dsus|1| 62 67 69" ] && [ "$(fo 5)" = $(( $(fo 4) + 3 )) ] &&
    ok "G#3, D4 25 ms later: nothing sounds yet (D4 waits for the gesture); A#3 at 50 ms: Dsus, three notes out once (no D minor before it)" \
    || bad "the root waiting for the pair: $(dl 'cr: voices' 4) / $(fc 4) / '$(fk 5)' notes-out $(fo 4) -> $(fo 5)"
fc 6 | grep -q 'gesture 1 kwait 0000200 ' && [ "$(fk 7)" = "Dm|1| 62 65 69" ] &&
    ok "G#3 and D4 with no partner: D4 waits, then Dm when the window closes" || bad "no partner: $(fc 6) / '$(fk 7)'"
[ "$(fk 8)" = "Dm|1| 62 65 69" ] && fc 8 | grep -q 'gesture 0 kwait 0000000 ' &&
    ok "G#3 held from before the window, then D4: Dm at once" || bad "a held G#3: '$(fk 8)' / $(fc 8)"
[ "$(fk 9)" = "D|1| 62" ] && [ "$(fk 10)" = "C|1| 60" ] && [ "$(fk 11)" = "C#|1| 61" ] &&
    ok "a root alone at once (D 62); C4 and C#4 are roots in FM-1 (60, 61)" || bad "the roots: '$(fk 9)' / '$(fk 10)' / '$(fk 11)'"
[ "$(fk 12)" = "D9|1| 62 66 69 76" ] && ok "A#3 (MAJ) + B3 (the 9 in FM-1) + D4: D 9 (62 66 69 76)" || bad "B3 = 9: '$(fk 12)'"
[ "$(fk 13)" = "D|1| 62" ] && ok "Simple: D4 first, then the pair: the single D stays (a type key after the root changes nothing)" || bad "root first: '$(fk 13)'"
dl 'lock:' 14 | grep -q '^  lock: on 1 latched 00 ' && dl 'lock:' 15 | grep -q '^  lock: on 1 latched 08 ' && [ "$(fk 16)" = "Dsus|1| 62 67 69" ] &&
    dl 'lock:' 17 | grep -q '^  lock: on 1 latched 00 ' && dl 'cr: key' 18 | grep -q ' layer 10 ' &&
    has 'expect led LFO on .*: ok' "$L" && has 'expect led G#3 on .*: ok' "$L" && has 'expect led A#3 on .*: ok' "$L" && has 'expect led G#3 faint .*: ok' "$L" &&
    ok "LFO tap: LOCK on (the LFO light lit); the pair let go latches SUS (G#3 and A#3 lit), D4 plays Dsus; the same pair again alone: off; LFO held: the LOCK layer" \
    || bad "LOCK on LFO: $(dl 'lock:' 14) / $(dl 'lock:' 15) / '$(fk 16)' / $(dl 'lock:' 17) / $(dl 'cr: key' 18)"
dl 'screen reads:' 18 | grep -q "label 'lock" && dl 'screen foot:' 18 | grep -q "'LFO: on/off" &&
    ok "the LOCK layer's footer names LFO in FM-1" || bad "the LOCK layer's footer: $(dl 'screen foot:' 18)"
p="$(fl 19) $(fl 20) $(fl 21) $(fl 22)"
[ "$(oe 19 | cut -c1-6)" = "open 0" ] && echo "$p" | grep -q '^1 [34] 1 [34] 1 [34] 1 [34]$' && echo "$p" | grep -q '1 3' && echo "$p" | grep -q '1 4' &&
    [ "$(fl 23)" = "0 4" ] && [ "$(fl 24)" = "0 2" ] && [ "$(oe 25 | cut -c1-6)" = "open 0" ] &&
    ok "GLO held + LFO: LATCH, no Options; with LOCK on the LFO light pulses lit / middle ($p); LATCH off: lit (LOCK); LOCK off: the glow; GLO held alone 700 ms opens no Options" \
    || bad "LATCH on GLO + LFO: $p / $(fl 23) / $(fl 24) / '$(oe 19)' / '$(oe 25)'"
dl 'cr: key' 26 | grep -q ' scale 1 ' && dl 'cr: key' 27 | grep -q ' scale 0 ' &&
    ok "the KEY layer (pick): G#3 then A#3 25 ms later picks minor once (the second key ignored); A#3 alone later: major" \
    || bad "the KEY layer's pick: $(dl 'cr: key' 26) / $(dl 'cr: key' 27)"
[ "$(fk 28)" = "Dsus|1| 62 67 69" ] && fc 28 | grep -q '^layout classic ' &&
    ok "back to Classic: C#4 is SUS again (C#4 + D4: Dsus)" || bad "back to Classic: '$(fk 28)' / $(fc 28)"
silent_end cr_fm1_keys
echo "  the FM-1 type keys with LOCK on: each pushed alone replaces the latch (3y164)"
"$EMU" --headless --script "$S/cr_fm1_lock.txt" >"$OUT/cr_fm1_lock.log" 2>&1 || bad "cr_fm1_lock: exit $?"
lt=$(LC_ALL=C grep -a '^  lock:' "$OUT/cr_fm1_lock.log" | sed -n '1,9p' | sed -n 's/.* latched \([0-9A-F]*\) .*/\1/p' | tr '\n' ' ')
[ "$lt" = "02 01 04 08 01 02 04 01 00 " ] &&
    ok "LOCK on: G#3 MIN, F#3 DIM, A#3 MAJ, the pair SUS, F#3 DIM, G#3 MIN, A#3 MAJ, F#3 DIM: each type key alone replaces the latch; F#3 again alone: off" \
    || bad "LOCK's type keys: the latch $lt"

echo "Options > MIDI Keys and Full K Shorts (0.24): MIDI notes 53 .. 79 press the keys; F#5 / G5 on the Full keyboard"
EMU_UI_LOG=1 "$EMU" --headless --script "$S/cr_fm1_midi_full.txt" >"$OUT/cr_fm1_midi_full.log" 2>&1 || bad "cr_fm1_midi_full: exit $?"
L="$OUT/cr_fm1_midi_full.log"
[ "$(oe 1)" = "open 1 entry 6 of 34 'MIDI Keys' value 'ch 5'" ] && [ "$(oe 2)" = "open 1 entry 5 of 34 'Full K Shorts' value 'On'" ] &&
    ok "MIDI Keys (KNOB 1 +5 from Off: ch 5, the parts' ch 1 and 2 skipped) and Full K Shorts On" || bad "the entries: '$(oe 1)' / '$(oe 2)'"
[ "$(fk 3)" = "Dsus|1| 62 67 69" ] && fc 3 | grep -q 'kmidi 0000228 ' && fc 4 | grep -q 'kmidi 0000000 ' &&
    [ "$(dl 'cr: voices' 5 | sed -n 's/.*cr: voices \([0-9]*\) .*/\1/p')" = 0 ] &&
    ok "over MIDI (ch 5): 56 + 58 then 62 is Dsus, as the keys; all let go: no key held; 52 and 80: no key" \
    || bad "MIDI Keys: '$(fk 3)' / $(fc 3) / $(fc 4) / $(dl 'cr: voices' 5)"
fc 6 | grep -q 'gesture 1 kwait 0000200 ' && [ "$(fk 7)" = "Dsus|1| 62 67 69" ] &&
    ok "MIDI's G#3 then D4 25 ms later: D4 waits for the window; A#3: Dsus at the pair" || bad "MIDI and the window: $(fc 6) / '$(fk 7)'"
[ "$(fk 8)" = "C|1| 60" ] && fc 9 | grep -q 'kmidi 0000000 ' && [ "$(fk 10)" = "D|1| 62" ] && fc 10 | grep -q 'kmidi 0000200 ' &&
    fc 11 | grep -q 'kmidi 0000000 ' && [ "$(dl 'cr: voices' 11 | sed -n 's/.*cr: voices \([0-9]*\) .*/\1/p')" = 0 ] &&
    ok "MIDI C4: 60; CC 123 lets every MIDI key go; D4 held on the panel and over MIDI is one press, let go at the last" \
    || bad "MIDI and the panel: '$(fk 8)' / $(fc 9) / '$(fk 10)' / $(fc 11)"
dl 'part 0:' 12 | grep -q 'voices 1,' && fc 12 | grep -q 'kmidi 0000000 ' &&
    ok "a note on the MIDI Perform channel still plays the sound directly (no key pressed)" || bad "MIDI Perform: $(dl 'part 0:' 12) / $(fc 12)"
[ "$(fk 14)" = "F|1| 53" ] && fc 15 | grep -q 'full-chk 1 ' && dl 'kbd:' 15 | grep -q '(engine: notes 0 full 0 ' &&
    [ "$(fk 16)" = "Dsus|1| 62 67 69" ] && [ "$(fk 17)" = "F|1| 77" ] && fc 18 | grep -q 'full-chk 0 ' && dl 'kbd:' 18 | grep -q '(engine: notes 0 full 1 ' &&
    ok "the Full keyboard: F3 plays 53; F#5: the chord keys show (the engine's Chords keyboard), G#3 + A#3 + D4 Dsus, F5 the last root (77); F#5 again: notes" \
    || bad "F#5: '$(fk 14)' / $(fc 15) / '$(fk 16)' / '$(fk 17)' / $(fc 18)"
fc 19 | grep -q 'full-all 1 ' && [ "$(fk 20)" = "F|1| 53" ] && [ "$(fk 21)" = "FM|1| 53 57 60" ] &&
    fc 22 | grep -q 'full-all 0 ' && fc 23 | grep -q 'full-all 1 ' &&
    ok "G5: chords on every key; with nothing latched F3 is a single note as normal; with LOCK's MAJ latched F3 plays F major; G5 held: off only while held" \
    || bad "G5: $(fc 19) / '$(fk 20)' / '$(fk 21)' / $(fc 22) / $(fc 23)"
silent_end cr_fm1_midi_full

[ $fail -eq 0 ] && echo "PASS" || echo "FAIL"
exit $fail
