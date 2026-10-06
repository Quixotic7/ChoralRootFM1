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
has 'expect led EDIT on .*: ok' "$OUT/cr_key.log" && ok "KEY tapped: its LED lit" || bad "KEY LED"
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

echo "EDIT: the sound pages"
run cr_edit --wav "$OUT/cr_edit.wav"
run cr_edit_ref --wav "$OUT/cr_edit_ref.wav"
has '^edit: part 0 page ENV' "$OUT/cr_edit.log" && ok "EDIT tap: the chord sound's ENV page: $OUT/cr_edit_env.ppm" || bad "EDIT tap: no ENV page"
has '^param: part 0 ENV ATK 80 -> 120' "$OUT/cr_edit.log" && ok "KNOB 1 +20: ATK 80 -> 120 ($(sed -n 's/^param: .*(\(.*\))/\1/p' "$OUT/cr_edit.log"))" \
    || bad "KNOB 1: ATK not changed"
differ cr_edit_env cr_edit_atk "the ENV page redrawn with the new attack: $OUT/cr_edit_atk.ppm"
differ cr_edit_turn cr_edit_atk "the turned glyph eases (mid-tween $OUT/cr_edit_turn.ppm)"
has '^page: part 0 LFO 5/8' "$OUT/cr_edit.log" && ok "SELECT +1: the LFO page 5/8: $OUT/cr_edit_lfo.ppm" || bad "SELECT: no page turn"
cmp -s "$OUT/cr_edit.wav" "$OUT/cr_edit_ref.wav" && bad "the same audio with another attack" \
    || ok "the same chord with ATK 120: the audio differs ($(num "non-zero samples" "$OUT/cr_edit.log") / $(num "non-zero samples" "$OUT/cr_edit_ref.log") non-zero samples)"
silent_end cr_edit

echo "EDIT held: the engine picker"
run cr_engine --wav "$OUT/cr_engine.wav"
has '^engine: part 0 -> ANALOG' "$OUT/cr_engine.log" && has 'part 0: ANALOG /' "$OUT/cr_engine.log" &&
    ok "EDIT held + D4: part 0 FM6 -> ANALOG: $OUT/cr_engine_switched.ppm" || bad "the engine did not switch"
has '^preset: part 0 -> ANALOG / ' "$OUT/cr_engine.log" && ok "EDIT held + KNOB 1: the engine's next preset" || bad "KNOB 1: no preset step"
silent_end cr_engine

echo "SAVE: naming, the user slot"
run cr_save
has '^save: part 0 slot U01 name ADG' "$OUT/cr_save.log" && ok "SAVE, D4 E4 F4, OCT+: U01 \"ADG\": $OUT/cr_save_typed.ppm" || bad "not saved"
has '^sound: part 0 pos 24 ADG' "$OUT/cr_save.log" && ok "PRESETS reaches U01 after the 24 bank sounds: $OUT/cr_save_preset.ppm" \
    || bad "U01 not on PRESETS"
has '^edit: part 1 page' "$OUT/cr_save.log" && has '^param: part 1 ENV ATK' "$OUT/cr_save.log" &&
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

echo "determinism"
mkdir -p "$OUT/cr_again"
cp "$OUT/cr_dmaj.ppm" "$OUT/cr_again/first.ppm"
"$EMU" --headless --script "$S/cr_dmaj.txt" --wav "$OUT/cr_again/cr_dmaj.wav" >/dev/null 2>&1
cmp -s "$OUT/cr_dmaj.wav" "$OUT/cr_again/cr_dmaj.wav" && ok "the same audio on a second run" || bad "audio differs"
cmp -s "$OUT/cr_dmaj.ppm" "$OUT/cr_again/first.ppm" && ok "the same LCD on a second run" || bad "LCD differs"

[ $fail -eq 0 ] && echo "PASS" || echo "FAIL"
exit $fail
