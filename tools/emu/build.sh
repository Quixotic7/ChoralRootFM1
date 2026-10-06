#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Build the FM-1 emulator: build/host/emu (clang, SDL2 from Homebrew, CoreMIDI, zlib from the SDK).
#   sh tools/emu/build.sh            (from anywhere)
# The firmware's generated headers (build/gen) must exist: python3 tools/build.py generates them
# (Pillow needs: export DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib); this script runs the generate
# step itself when build/gen is missing.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
export DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib${DYLD_FALLBACK_LIBRARY_PATH:+:$DYLD_FALLBACK_LIBRARY_PATH}
for h in ui_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h felucca_tables.h felucca_fm6.h felucca_samples.h; do
    if [ ! -f "build/gen/$h" ]; then
        echo "build/gen/$h missing: running the generate step (tools/build.py)"
        python3 tools/build.py
        break
    fi
done
SDL_CFLAGS=$(/opt/homebrew/bin/sdl2-config --cflags 2>/dev/null || echo "-I/opt/homebrew/include/SDL2 -D_THREAD_SAFE")
SDL_LIBS=$(/opt/homebrew/bin/sdl2-config --libs 2>/dev/null || echo "-L/opt/homebrew/lib -lSDL2")
CC=${CC:-clang}
OUT=build/host
OBJ=build/host/emu_obj
mkdir -p "$OUT" "$OBJ" build/emu
# the firmware: one unit, as the host tests build it (warnings off: it is the firmware's own code)
$CC -O2 -w -Ibuild/gen -Ifirmware/src -c tools/emu/emu_fw.c -o "$OBJ/emu_fw.o"
# the emulator
for f in emu keymap emu_img; do
    $CC -O2 -Wall -Wextra -Wno-unused-parameter $SDL_CFLAGS -c "tools/emu/$f.c" -o "$OBJ/$f.o"
done
$CC -O2 -Wall -Wno-deprecated-declarations -c tools/emu/emu_midi.c -o "$OBJ/emu_midi.o" $SDL_CFLAGS
$CC -o "$OUT/emu" "$OBJ/emu.o" "$OBJ/keymap.o" "$OBJ/emu_img.o" "$OBJ/emu_midi.o" "$OBJ/emu_fw.o" \
    $SDL_LIBS -lz -lm -framework CoreMIDI -framework CoreFoundation
echo "built $OUT/emu"
