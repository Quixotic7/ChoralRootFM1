#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# ChoralRoot FM-1 in the browser: the emulator's firmware side as a standalone WebAssembly module, and its page.
#   [CR_VERSION=1.0] sh tools/emu/web/build_web.sh
#   -> build/emu-web/{index.html, emu.js, keymap.js, worklet.js, choralroot.wasm, version.txt (the version string)}
#   -> build/emu-web-smk37/{index.html, smk.js, worklet.js, choralroot-smk37.wasm, version.txt}: the SMK-37's (SMK37.md,
#      tools/emu/emu_smk_hal.h): the BOARD_SMK37 firmware on a modelled SMK-37 panel, its own page (tools/emu/web/smk)
# Needs emcc (the Emscripten SDK: ~/emsdk/emsdk_env.sh is sourced when emcc is not on the PATH) and the firmware's
# generated headers (build/gen: the generate step of tools/build.py runs when they are missing, as build.sh does).
# CR_VERSION: the version the firmware shows ("1.0" -> "ChoralRoot 1.0", as tools/build.py --release); default
# firmware/src/choralroot.c's FELUCCA_VERSION.
set -e
ROOT=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$ROOT"
# P5's factory header follows the shipped list (assets/prophet5-factory/shipped.txt; written only when its text changes)
[ -d build/gen ] && python3 tools/gen_prophet_factory.py build/gen/melodee_prophet_factory.h >/dev/null
# the compiled layouts follow their designs (tools/gen_layout.py; written only when its text changes)
[ -d build/gen ] && python3 tools/gen_layout.py build/gen/cr_layouts.h >/dev/null
if ! command -v emcc >/dev/null 2>&1; then
    for e in "${EMSDK:-}/emsdk_env.sh" "$HOME/emsdk/emsdk_env.sh"; do
        [ -f "$e" ] && { . "$e" >/dev/null 2>&1; break; }
    done
fi
command -v emcc >/dev/null 2>&1 || { echo "emcc not found: install the Emscripten SDK (~/emsdk)"; exit 1; }
export DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib${DYLD_FALLBACK_LIBRARY_PATH:+:$DYLD_FALLBACK_LIBRARY_PATH}
for h in ui_fonts.h ui_icons.h ui_keycaps.h ui_palettes.h felucca_tables.h felucca_fm6.h felucca_samples.h cr_fonts.h cr_layouts.h; do
    if [ ! -f "build/gen/$h" ]; then
        echo "build/gen/$h missing: running the generate step (tools/build.py)"
        python3 tools/build.py
        break
    fi
done
if [ -n "${CR_VERSION:-}" ]; then
    case "$CR_VERSION" in
        ChoralRoot*) VER=$CR_VERSION ;;
        *) VER="ChoralRoot $(printf %s "$CR_VERSION" | tr A-Z a-z)" ;;
    esac
else
    VER=$(sed -n 's/^#define FELUCCA_VERSION "\(.*\)".*/\1/p' firmware/src/choralroot.c | head -1)   # (.*: the line's comment
                                                                                              # is not the version's)
fi
OUT=build/emu-web
W=tools/emu/web
mkdir -p "$OUT"
# the firmware unit as build.sh builds it (warnings off: the firmware's own code), single-threaded, no JS runtime;
# through tests/hostcc.sh: compiled again only when the command (the version is in it) or a file it was compiled from
# has changed (hashes, not dates; HOSTCC_FORCE=1: always)
sh tests/hostcc.sh emcc -O2 -w -std=gnu11 -I$W/compat -Ibuild/gen -Ifirmware/src -DEMU_WEB "-DFELUCCA_VERSION=\"$VER\"" \
    --no-entry -sSTANDALONE_WASM -sFILESYSTEM=0 -sSTACK_SIZE=1048576 -sINITIAL_MEMORY=33554432 \
    -o "$OUT/choralroot.wasm" $W/emu_web.c
cp $W/index.html $W/emu.js $W/keymap.js $W/worklet.js "$OUT/"
printf '%s\n' "$VER" > "$OUT/version.txt"        # (web/make_site.py: the landing page says so when it is a beta)
echo "emu-web: $OUT ($VER)"
for f in choralroot.wasm index.html emu.js keymap.js worklet.js; do
    printf '  %-16s %8d B\n' "$f" "$(wc -c < "$OUT/$f")"
done
# the SMK-37's module (EMU_SMK37, BOARD_SMK37: tools/emu/emu_smk_hal.h) and page, the version "... -smk37"
OUT2=build/emu-web-smk37
mkdir -p "$OUT2"
sh tests/hostcc.sh emcc -O2 -w -std=gnu11 -I$W/compat -Ibuild/gen -Ifirmware/src -DEMU_WEB -DEMU_SMK37=1 -DBOARD_SMK37=1 \
    "-DFELUCCA_VERSION=\"$VER-smk37\"" --no-entry -sSTANDALONE_WASM -sFILESYSTEM=0 -sSTACK_SIZE=1048576 \
    -sINITIAL_MEMORY=33554432 -o "$OUT2/choralroot-smk37.wasm" $W/emu_web.c
cp $W/smk/index.html $W/smk/smk.js $W/smk/worklet.js "$OUT2/"
printf '%s\n' "$VER-smk37" > "$OUT2/version.txt"
echo "emu-web-smk37: $OUT2 ($VER-smk37)"
for f in choralroot-smk37.wasm index.html smk.js worklet.js; do
    printf '  %-22s %8d B\n' "$f" "$(wc -c < "$OUT2/$f")"
done
