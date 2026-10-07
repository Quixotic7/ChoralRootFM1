# FM-1 emulator (macOS)

The FM-1 firmware built for the Mac: the 240x240 LCD, the panel with its LEDs, the firmware's audio
callback at 44.1 kHz and CoreMIDI ports. It runs the same sources as the device (`firmware/src`, the DSP
through `tests/hostsim.c`'s sound side), with a HAL in `emu_hal_fw.h`. The instrument is ChoralRoot (`emu_firmware.h`:
`cr_engine.c` ticking in the audio ISR, `cr_out.c`, `cr_ui.c` on `cr_draw.c`; docs/INTEGRATION.md), with the device's
all-synth flags: no Felucca sequencer, no SAMPLE / GRAIN / DRUM, no keycaps (only the SLICER differs: on here).

```sh
sh tools/emu/build.sh          # -> build/host/emu  (clang, SDL2 from /opt/homebrew, CoreMIDI, zlib)
sh tools/emu/test.sh           # headless acceptance (runs test_cr.sh too), prints PASS / FAIL
sh tools/emu/test_cr.sh        # ChoralRoot's: tools/emu/scripts/cr_*.txt, screenshots build/emu/test/cr_*.ppm
build/host/emu --help          # options and the key map
```

`build.sh` runs the generate step (`python3 tools/build.py`) when `build/gen` is missing; it sets
`DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib` for Pillow.

## Files

| file | what |
|---|---|
| `emu.c` | SDL window, panel drawing, mouse, the script runner, headless mode, audio callback, timing |
| `keymap.c` | the computer-key map (change it there; `--help` and the panel hints follow) |
| `emu_hooks.h` | the boundary: `emu_hal` (keys, buttons, knobs, LEDs, LCD) and the `emu_fw_*` hooks |
| `emu_fw.c`, `emu_firmware.h`, `emu_hal_fw.h` | the firmware side: its sources, the HAL, power-on, frame, audio ISR |
| `emu_midi.c` | CoreMIDI: source "ChoralRoot FM-1" (out), destination "ChoralRoot FM-1 In" (in) |
| `emu_img.c` | LCD to PNG / PPM |
| `perf.sh`, `scripts/perf_*.txt`, `wavclicks.py` | the worst cases for the CPU and the pops (docs/INTEGRATION.md, Performance): the audio block's cost and its device estimate, the UI frame's, voices stolen, flash erases, and the WAV checked for jumps, silent holes and clicks |
| `scripts/`, `test.sh`, `test_cr.sh` | headless scripts and their checks (`sel.txt`, `knobs.txt`, `acceptance.txt`: Felucca's UI, no longer run) |

## Windowed mode: never in front by default

`build/host/emu` starts as a **background app**: `SDL_HINT_MAC_BACKGROUND_APP=1` (no Dock icon, no
activation, it never takes the keyboard focus from the app you are using), and its window opens at the
**bottom-right corner of the main display**. Click the window to play it with the keyboard.

`--front` turns that off: a normal app with a Dock icon, the window centred and in front.

The window: the LCD x3 (smaller if the screen is too small; `--scale N`), below it the FM-1 panel drawn in
the geometry of `ChoralRootFM1Designer/index.html` (BODY, SCREEN, KEYS, BUTTONS, ENCODERS) with the live
LEDs: key and button LEDs lit / dim from `fm1_led` / `fm1_led_dim`, REC red, PLAY orange plus its green LED.
Keys and buttons are clickable (right-click / ctrl-click latches one down), the knobs turn with the mouse
wheel over them or a vertical drag; a click selects a knob for Up / Down. At exit it prints the audio block
render time (avg / max us), the device callback gaps and the UI frame time.

Other options: `--flash PATH` / `--no-flash` / `--save-on-exit` (the flash file: settings, user sounds, loops), `--demo` (ignored by ChoralRoot), `--quit-after S`, `--midi-log`, `--no-midi`, `--no-audio`,
`--shot PATH` (the LCD at exit). A flash erase plays as on the device: 45 ms of silence with the audio ISR not run
(`emu_hal_fw.h` `st_erase`; the dump's `flash erases with the audio stalled` line counts them).

## Key map (physical US-layout positions; `keymap.c`)

Firmware key index = MIDI note - 53 (0 = F3 .. 26 = G5).

| computer keys | FM-1 |
|---|---|
| `A S D F G H J K L ; '` | white root keys D4 E4 F4 G4 A4 B4 C5 D5 E5 F5 G5 (keys 9 11 12 14 16 18 19 21 23 24 26) |
| `W E T Y I O [` | black root keys D#4 F#4 G#4 A#4 C#5 D#5 F#5 (keys 10 13 15 17 20 22 25) |
| `F1 F2 F3 F4` | F#3 G#3 A#3 C#4 (keys 1 3 5 8) |
| `2 3 4 5` | F3 G3 A3 C4 (keys 0 2 4 7) |
| `1` | B3 (key 6: ChoralRoot's LOCK) |
| `Z X C V B N` | FX SEL ENV LFO EDIT GLO |
| `,` `.` `/` Right-Shift Return Backspace | HOME SAVE ARP SEQ PLAY REC |
| Left / Right | OCT- / OCT+ |
| Esc | OCT- and OCT+ together |
| `Q R U P` | select the knob MASTER, SELECT, PRESETS, ALGORITHM |
| `6 7 8 9` | select KNOB1 .. KNOB4 |
| Up / Down | turn the selected knob one detent clockwise / counter-clockwise (repeats while held; MASTER: 32 of 1023) |
| mouse wheel over a knob | turn that knob |
| F10 | LCD screenshot: `build/emu/shot_NNN.png` |
| F11 | record every LCD frame to `build/emu/rec/NNNN.ppm` (again to stop) |
| F12 | print the input state (`fm1_in`, the LEDs, the track, the song) |

On a Mac keyboard the F keys may need `fn`. Cmd shortcuts stay the system's; losing the focus releases
every held key (no stuck notes).

## Headless mode (what every automated check uses)

```sh
build/host/emu --headless --script FILE [--frames N] [--shot build/emu/x.ppm] [--wav out.wav]
```

No window and no audio device: `SDL_Init(0)` with `SDL_VIDEODRIVER=dummy` and `SDL_AUDIODRIVER=dummy` set.
Time is simulated (the 1 ms timer, a UI frame every 15 ms, the 128-frame audio blocks as they fall due), so
a run is deterministic: the same script gives the same LCD and the same audio, bit for bit. It runs N
frames of 15 ms (`--frames N`; without it, to the end of the script, else 600 frames), then prints the
peak / rms, the block render time, the host instructions per audio block and UI frame with the device estimate
(`EMU_CPU_LOG=PCT` lists every block above PCT % of the device's budget; `EMU_UI_LOG=N` every UI frame above N
million host instructions, with its index, time, device estimate and the screen it drew: kind, view, name / item /
title, animation clock, ring, message, strips blitted), **the number of non-silent blocks and
non-zero samples**, and exits
1 if an `expect` failed (2 on a script error). `--frames` alone implies `--headless`.

### Script grammar

One command a line; `#` at the start of a word begins a comment (so `F#4` is a name). The script has its
own clock in ms from power-on; commands run in order and only `wait` (and the timed presses) move it.

| command | meaning |
|---|---|
| `wait MS` | move the clock on: `200`, `200ms`, `1.5s` (`frames N` = N x 15 ms) |
| `key KEY down` / `key KEY up` | a computer key of the map, as if typed: `key A down`, `key RETURN up`, `key UP down` |
| `key KEY [MS]` | press KEY for MS (default 100) ms; the clock moves on by MS |
| `btn NAME [down\|up\|MS]` | a panel control by name: `FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+` or a note key `F3`..`G5` (`D#4`); default a 100 ms tap |
| `knob NAME +-N` | turn `SELECT ALGORITHM PRESETS KNOB1..KNOB4` N detents (+ clockwise); `MASTER`: N x 16 of 1023 |
| `master ADC` | set the MASTER pot, 0..1023 |
| `shot NAME` | the LCD to `build/emu/NAME.png` and `.ppm`; a NAME with `/` or ending `.ppm` / `.png` is a path |
| `expect led NAME on\|dim\|off` | check a key's or button's LED now (`on` = lit); `GREEN` is PLAY's green LED |
| `expect sound` / `expect silence` | non-zero samples since the previous `expect sound\|silence` (or power-on) |
| `dump` | print the input state (as F12) |
| `rec` | start / stop recording the LCD frames (as F11) |
| `quit` | stop here |

Older forms still work: `press KEY [MS]`, `tap`, `hold` / `down`, `release` / `up`, `turn KNOB N`, and an
absolute time in front of a command (`1200 down A`). For `key`/`press`/`hold` a computer key name wins,
for `btn`/`knob`/`expect` a panel name; `note:F3` always means the note F3.

Example (`scripts/key_a.txt`):

```
wait 500
shot build/emu/test/home.ppm   # the power-on screen
key A down                      # D4
wait 150
expect led A on
wait 150
key A up
wait 100
expect sound
```

### test.sh

`sh tools/emu/test.sh` rebuilds when a source is newer, then checks in headless mode: A held 300 ms
lights key D4's LED and gives non-silent blocks; the power-on screen is written; a second run gives the same
LCD and WAV; then `test_cr.sh`: MAJ + D4 (D major: sound, the D4 F#4 A4 LEDs, the chord screen), KNOB 1
(voicing), KEY tap / hold (Key Mode, select-key), PERF hold (the Perform picker), PRESETS (another sound),
ALGORITHM (the bass on part 1), Esc (PANIC), idle (the stripes), Options and the other layers' screens, no
stuck notes (every part silent 2 s after the release). Outputs and logs go to `build/emu/test/`.

## Realtime smoke test without a window

The windowed code path (timer thread, SDL audio callback, renderer) also runs under SDL's dummy drivers,
which open no window and no audio device:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy build/host/emu --no-midi --demo --quit-after 3
```

## Not done / stubbed

- Flash is RAM only (`FELUCCA_FLASH 0`): SAVE and user presets do not persist across runs.
- The CPU meter reads ~0 %: the Mac renders a block in a few tens of us against the device's budget.
- ChoralRoot's roles of the printed buttons: `X` (SEL) = KEY, `B` (EDIT) = EDIT, `Z` FX, `C` (ENV) BASS, `V` (LFO)
  LATCH, `N` (GLO) OPT, `/` (ARP) PERF, Right-Shift (SEQ) METRO, Return (PLAY) LOOP; B3 (`1`) = LOCK.
  A layer button held 300 ms locks its layer open: scripts close it with `btn OCT-` (or `btn HOME`).
- Sound editing: `B` (EDIT) tap = the sound editor (`C` held + `B` the bass's), held = the engine picker;
  `.` (SAVE) = naming (the white roots type, Left deletes, Right saves). User sounds are RAM only here.
- ChoralRoot's looper and metronome are not there yet (their screens
  show; docs/INTEGRATION.md "Status").

## Browser build

`tools/emu/web/` builds the same firmware side (`emu_fw.c` through `emu_firmware.h`) into a standalone WebAssembly
module played from a web page (no Emscripten JS runtime, no COOP/COEP headers: it runs on GitHub Pages):

```sh
sh tools/emu/web/build_web.sh          # -> build/emu-web/{index.html, emu.js, keymap.js, worklet.js, choralroot.wasm}
node tools/emu/web/test_web_emu.mjs    # boots the wasm in node, plays scripts/cr_dmaj.txt, compares with build/host/emu
```

Needs `emcc` (the Emscripten SDK; `~/emsdk/emsdk_env.sh` is sourced when it is not on the PATH); `CR_VERSION=1.0`
sets the version shown. `emu_web.c` runs the device clock exactly as the headless loop (tick, frame / idle, the audio
blocks due, per millisecond) inside an AudioWorklet (`worklet.js`), so a script's input gives the native emulator's
samples bit for bit (the test checks it). The CPU lock is a no-op (`web/compat/os/lock.h`: one thread); the flash
file is the page's IndexedDB copy (loaded before power-on, saved a second after the last erase / program). The page
(`index.html`, `emu.js`): the LCD, the panel with its LEDs (pointer, multi-touch, right-click latches, wheel / drag
on the knobs), the key map of `keymap.c` (`web/keymap.js`), Web MIDI in and out, Reset flash, a load meter.
