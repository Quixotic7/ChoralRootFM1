#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
# Build ChoralRoot FM-1 on macOS (see BUILDING.md): build/choralroot.fwsc (identity FM-1_920).
#   ./build.sh [--release X.Y]      (a release: build/choralroot-X.Y.fwsc, version "ChoralRoot X.Y")
#   JIELI_TOOLCHAIN  JieLi Linux toolchain (default: ~/.jieli/toolchain)
#   AC79_SDK         JieLi AC79 SDK checkout (default: ~/fw-AC79_AIoT_SDK if present, else tools/sdk: the three files, vendored)
set -e
cd "$(dirname "$0")"
export JIELI_TOOLCHAIN="${JIELI_TOOLCHAIN:-$HOME/.jieli/toolchain}"
if [ -z "${AC79_SDK:-}" ]; then                   # an SDK checkout if there is one, else the vendored files (tools/sdk)
    if [ -f "$HOME/fw-AC79_AIoT_SDK/cpu/wl82/tools/uboot.boot" ]; then AC79_SDK="$HOME/fw-AC79_AIoT_SDK"; else AC79_SDK="$PWD/tools/sdk"; fi
fi
export AC79_SDK
# Pillow finds Homebrew's libraqm (the UI fonts' kerning). macOS strips DYLD_* from the environment of /bin/sh,
# so an exported value does not reach this script: set it here.
if [ "$(uname -s)" = Darwin ] && [ -z "$DYLD_FALLBACK_LIBRARY_PATH" ] && [ -f /opt/homebrew/lib/libraqm.dylib ]; then
    export DYLD_FALLBACK_LIBRARY_PATH=/opt/homebrew/lib
fi
PY="${PYTHON:-python3}"

"$PY" -c 'import PIL' 2>/dev/null || { echo "build.sh: $PY has no Pillow (pip3 install Pillow)"; exit 1; }
[ -x "$JIELI_TOOLCHAIN/pi32v2/bin/clang" ] || { echo "build.sh: no toolchain in $JIELI_TOOLCHAIN (run tools/get_toolchain.sh)"; exit 1; }
[ -f "$AC79_SDK/cpu/wl82/tools/uboot.boot" ] || { echo "build.sh: no JieLi AC79 SDK in $AC79_SDK"; exit 1; }
if [ "$(uname -s)" != Linux ] || [ "$(uname -m)" != x86_64 ]; then
    docker info >/dev/null 2>&1 || { echo "build.sh: Docker is not running"; exit 1; }
fi
exec "$PY" tools/build.py "$@"
