#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# The resource region and the way back (HISTORY 3y234; firmware/src/cr_res.h, docs/BOOT-SAFETY.md "The resource
# region"): a unit that ran this firmware holds the region at flash 0x93000..0x9EFFF; a downgrade installs a package
# without it (flash.bin 0x93000 long: its loader leaves those sectors, tests/ldr_test.c), so the older firmware boots
# beside the region's bytes, in sectors that held 0.21's sound objects. Here, for 0.29.2 and 0.26.1 (each built from
# its tag by `git archive` into build/scratch/res/vX.Y, its emulator from its own generators):
#   1. this firmware's emulator writes a unit's data (persist_set: 137 BPM saved; cr_safe_sound: TINE EP in U001), and
#      the region goes in as the update loader writes it (tools/fm1pkg_make.py res_header + build/res.bin)
#   2. the older firmware on that image: its settings and the sound as saved, nothing in 0x93000..0x9EFFF written
#   3. the older firmware on a fresh image that holds only the region (a unit whose pool is empty): its 0.21 migration
#      (0.22 .. 0.29.2 upool_import) finds no 0.21 slot in the region's bytes, the pool stays empty, the region as it was
#   sh tools/emu/test_res.sh
set -u
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
cd "$ROOT"
export SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy
export DYLD_FALLBACK_LIBRARY_PATH="${DYLD_FALLBACK_LIBRARY_PATH:-/opt/homebrew/lib}"   # (Pillow's Raqm; /bin/sh drops it)
D=build/emu/test/res
EMU=build/host/emu
S=tools/emu/scripts
fail=0
ok()  { echo "  ok    $*"; }
bad() { echo "  FAIL  $*"; fail=1; }
mkdir -p "$D"
[ -f build/res.bin ] || { echo "no build/res.bin: run ./build.sh first"; exit 1; }
if [ ! -x "$EMU" ] || [ -n "$(find tools/emu firmware/src tests/hostsim.c -newer "$EMU" -name '*.[ch]' 2>/dev/null | head -1)" ]; then
    sh tools/emu/build.sh || { echo "build failed"; exit 1; }
fi
# the region into an image, as the loader leaves it (header at 0x93000, the data from 0x93040)
put_res() {
    python3 - "$1" <<'PY'
import sys, importlib.util
spec = importlib.util.spec_from_file_location("pm", "tools/fm1pkg_make.py")
pm = importlib.util.module_from_spec(spec); spec.loader.exec_module(pm)
img = bytearray(open(sys.argv[1], "rb").read())
img += b"\xff" * (0x100000 - len(img))
res = open("build/res.bin", "rb").read()
img[pm.RES_LO:pm.RES_HI] = b"\xff" * (pm.RES_HI - pm.RES_LO)
img[pm.RES_LO:pm.RES_DATA] = pm.res_header(res, 0x12345678)
img[pm.RES_DATA:pm.RES_DATA + len(res)] = res
open(sys.argv[1], "wb").write(img)
PY
}
rcrc() { python3 -c 'import sys, zlib; d = open(sys.argv[1], "rb").read(); print("%08x" % zlib.crc32(d[0x93000:0x9F000]))' "$1"; }

echo "the resource region and the way back (0.29.2, 0.26.1)"
DATA="$D/data.bin"
rm -f "$DATA"
"$EMU" --headless --flash "$DATA" --script "$S/persist_set.txt" > "$D/data_1.log" 2>&1
"$EMU" --headless --flash "$DATA" --script "$S/cr_safe_sound.txt" > "$D/data_2.log" 2>&1
pool0=$(python3 tools/emu/pool_image.py pool "$DATA")
put_res "$DATA"
r0=$(rcrc "$DATA")
grep -q 'settings saves 1 ' "$D/data_1.log" && grep -q '^save: part 0 slot U001 name TINE EP' "$D/data_2.log" &&
    [ -n "$pool0" ] && [ "$r0" != "$(python3 -c 'import zlib; print("%08x" % zlib.crc32(b"\xff" * 0xC000))')" ] &&
    ok "this firmware's data (137 BPM; U001 TINE EP) and the region ($(wc -c < build/res.bin | tr -d ' ') B, crc $r0) in one image" ||
    bad "the image: $(grep -m1 '^save:' "$D/data_2.log") / pool '$pool0'"
FRESH="$D/fresh.bin"
python3 -c 'import sys; open(sys.argv[1], "wb").write(b"\xff" * 0x100000)' "$FRESH"
put_res "$FRESH"

for tag in v0.29.2 v0.26.1; do
    OLD=build/scratch/res/$tag
    if [ ! -f "$OLD/firmware/src/upool.c" ]; then
        mkdir -p "$OLD" && git archive "$tag" | tar -x -C "$OLD" || { bad "$tag: git archive failed"; continue; }
    fi
    mkdir -p "$OLD/build"
    [ -d "$OLD/build/gen" ] || (cd "$OLD" && python3 -c 'import sys; sys.path.insert(0, "tools"); import build; build.generate()') \
        > "$D/${tag}_gen.log" 2>&1 || { bad "$tag: its generators failed ($D/${tag}_gen.log)"; continue; }
    [ -x "$OLD/build/host/emu" ] || sh "$OLD/tools/emu/build.sh" > "$D/${tag}_emu.log" 2>&1 ||
        { bad "$tag: its emulator did not build ($D/${tag}_emu.log)"; continue; }
    OE="$OLD/build/host/emu"
    # 2. the unit's data beside the region
    cp "$DATA" "$D/$tag.bin"
    "$OE" --headless --flash "$D/$tag.bin" --script "$OLD/tools/emu/scripts/persist_check.txt" > "$D/$tag.log" 2>&1
    st=$?
    [ $st = 0 ] && grep -q 'bpm 137 ' "$D/$tag.log" && [ "$(python3 tools/emu/pool_image.py pool "$D/$tag.bin")" = "$pool0" ] &&
        [ "$(rcrc "$D/$tag.bin")" = "$r0" ] &&
        ok "$tag on it: 137 BPM, U001 TINE EP as saved, 0x93000..0x9EFFF unwritten" ||
        bad "$tag on the image: exit $st, $(grep -m1 -o 'bpm [0-9]*' "$D/$tag.log"), pool '$(python3 tools/emu/pool_image.py pool "$D/$tag.bin" | head -1)', region $(rcrc "$D/$tag.bin")"
    # 3. an empty pool beside the region: no 0.21 slot taken from its bytes
    cp "$FRESH" "$D/${tag}_fresh.bin"
    "$OE" --headless --flash "$D/${tag}_fresh.bin" --script "$OLD/tools/emu/scripts/persist_check.txt" > "$D/${tag}_fresh.log" 2>&1
    st=$?
    [ $st = 0 ] && [ -z "$(python3 tools/emu/pool_image.py pool "$D/${tag}_fresh.bin")" ] &&
        [ "$(rcrc "$D/${tag}_fresh.bin")" = "$r0" ] &&
        ok "$tag on a fresh unit with the region: no sound migrated from its bytes, the pool empty, the region as it was" ||
        bad "$tag fresh: exit $st, pool '$(python3 tools/emu/pool_image.py pool "$D/${tag}_fresh.bin" | head -1)', region $(rcrc "$D/${tag}_fresh.bin")"
done
[ $fail -eq 0 ] && echo "PASS" || echo "FAIL"
exit $fail
