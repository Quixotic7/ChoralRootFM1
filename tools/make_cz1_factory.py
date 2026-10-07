#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Rebuild assets/cz1-factory/cz1-factory.syx: Casio's 64 CZ-1 preset tones as native CZ-1 tones.

  tools/make_cz1_factory.py CZ1ORG64_DIR VIRTUALCZ_CZ1_DIR [OUT.syx]

Two sources, neither complete on its own (see assets/cz1-factory/README.md):
  CZ1ORG64_DIR       CZ1ORGB1..4.SYX from cz1org64.zip ("64 original CZ-1 patches for CZ-101, 1000,
                     3000, 5000", the GeoCities CZ-101 page, archived by the Wayback Machine 2009-10-27):
                     Casio's 128 synthesis bytes, without the CZ-1's names, line levels and velocity.
  VIRTUALCZ_CZ1_DIR  VST2/CZ1/*.fxp from Oli Larkin's VirtualCZ_Casio_Presets.zip: the same 64 tones as
                     VirtualCZ parameters, with the CZ-1 line LEVEL and the three velocity amounts.
The 128 bytes are kept verbatim except what a CZ-1 adds: the CZ-1 DCW key-follow byte (as the device's
own 128-byte import, cz_store.c cz_sx_import), the LEVEL / velocity nibbles and the 16-character name.
Every envelope rate, level and SUS of the two sources is checked to agree.
"""
import re
import struct
import sys
from pathlib import Path

NAMES = [  # the CZ-1's LCD names, A-1 .. H-8
    "BRASS 1", "BRASS 2", "BRASS 3", "STRINGS 1", "STRINGS 2", "STRINGS 3", "STRINGS 4", "ORCHESTRA",
    "ACO.GUITAR", "JAZZ GUITAR", "ELEC.GUITAR", "SLAP BASS", "SYNTH.BASS", "ELEC.BASS 1", "ELEC.BASS 2", "HARP",
    "BRASS 4", "SAXOPHONE", "CELLO", "FLUTE", "WHISTLE", "HARMONICA", "RECORDER", "KOTO",
    "PIANO 1", "PIANO 2", "PIANO 3", "ELEC.PIANO", "HONKY-TONK", "FUNKY CLAVI.1", "FUNKY CLAVI.2", "HARPSICHORD",
    "JAZZ ORGAN 1", "JAZZ ORGAN 2", "PIPE ORGAN 1", "PIPE ORGAN 2", "ACCORDION", "VOICE 1", "VOICE 2", "VOICE 3",
    "MUSIC BOX", "VIBRAPHONE", "XYLOPHONE", "MARIMBA", "MALLET LOG", "AFRO-PERCUSSION", "BELLS", "METALLIC SOUND",
    "SYNTH.STRINGS", "FAT ENSEMBLE", "SITAR", "SYNTH.LEAD 1", "SYNTH.LEAD 2", "SYNTH.LEAD 3", "SYNTH.LEAD 4",
    "SWEEP SOUND 1", "SYNTH.DRUMS 1", "SYNTH.DRUMS 2", "CONGA", "STEEL DRUM", "SWEEP SOUND 2", "JET ROAR",
    "MOTORCYCLE", "TYPHOON SOUND"]
KW_CZ1 = (0, 31, 44, 57, 70, 83, 96, 111, 146, 255)  # cz_store.c: the CZ-1's DCW key-follow bytes
VCZ_PARAMS = 2128                                    # VirtualCZ fxp: the parameter doubles start here
# VirtualCZ parameter index per line: LEVEL, then the PITCH / DCW / AMP velocity amounts (0..15)
VCZ_EXTRA = ((126, 29, 77, 127), (151, 53, 102, 152))
VCZ_ENV = {(0, 0): 27, (0, 1): 76, (0, 2): 126, (1, 0): 51, (1, 1): 101, (1, 2): 151}   # SUS at +7, L at +10, R at +17


def casio(d):
    out = []
    for path in sorted(Path(d).glob("CZ1ORGB[1-4].SYX")):
        for m in re.split(rb"(?=\xf0)", path.read_bytes()):
            if not m:
                continue
            nib = m[7:-1]
            if m[:2] != b"\xf0\x44" or m[-1] != 0xF7 or len(nib) != 256:
                raise SystemExit(f"{path}: not a 128-byte CZ tone dump")
            out.append(bytearray(nib[2 * i] | nib[2 * i + 1] << 4 for i in range(128)))
    if len(out) != 64:
        raise SystemExit(f"{d}: {len(out)} tones, want 64")
    return out


def virtualcz(d):
    files = sorted(Path(d).glob("[A-H]-[1-8] *.fxp"))
    if len(files) != 64:
        raise SystemExit(f"{d}: {len(files)} presets, want 64")
    for f, name in zip(files, NAMES):
        if f.stem[4:] != name:
            raise SystemExit(f"{f.name}: not {name}")
    return [[struct.unpack_from("<d", f.read_bytes(), VCZ_PARAMS + 8 * i)[0] for i in range(191)] for f in files]


def unrate(e, b):
    b &= 127
    if e == 1:
        b = b - 8 if b > 8 else 0
    return min(99, b * 99 // (127 if e == 0 else 119) + 1) if b else 0


def unlevel(e, b):
    b &= 127
    a = (b - 4 if b > 63 else b) if e == 0 else (99 if b == 127 else b * 99 // 127 + 1 if b else 0) if e == 1 else \
        (b - 28 if b > 28 else 0)
    return min(a, 99)


def agree(n, t, v):
    """the envelopes of both sources: every rate, level before END and SUS"""
    for (l, e), base in VCZ_ENV.items():
        off = (71 if l else 14) + (6 if e == 2 else 23 if e == 1 else 40)
        sus = next((j for j in range(8) if t[off + 2 + 2 * j] & 128), 8)
        if v[base + 7] != (0 if sus == 8 else sus + 1):
            raise SystemExit(f"{NAMES[n]}: line {l + 1} env {e} SUS differs")
        for j in range(8):
            if v[base + 17 + j] != unrate(e, t[off + 1 + 2 * j]) or (j < 7 and v[base + 10 + j] != unlevel(e, t[off + 2 + 2 * j])):
                raise SystemExit(f"{NAMES[n]}: line {l + 1} env {e} step {j + 1} differs")


def tone(n, t, v):
    agree(n, t, v)
    d = bytearray(t)
    for l, (lev, vp, vw, va) in enumerate(VCZ_EXTRA):
        o = 71 if l else 14
        vals = [int(v[i]) for i in (lev, vp, vw, va)]
        if any(x != v[i] or not 0 <= x <= 15 for x, i in zip(vals, (lev, vp, vw, va))) or not vals[0]:
            raise SystemExit(f"{NAMES[n]}: line {l + 1} LEVEL / velocity out of range")
        if (d[o + 4] & 15) > 9:
            raise SystemExit(f"{NAMES[n]}: line {l + 1} key follow out of range")
        d[o + 2] = (d[o + 2] & 15) | (15 - vals[0]) << 4
        d[o + 5] = KW_CZ1[d[o + 4] & 15]
        for off, vel in ((o + 40, vals[1]), (o + 23, vals[2]), (o + 6, vals[3])):
            d[off] = (d[off] & 15) | (15 - vel) << 4
    name = NAMES[n]
    pad = (16 - len(name)) // 2
    return bytes(d) + (" " * pad + name).ljust(16).encode("ascii")


def sysex(raw):
    return bytes([0xF0, 0x44, 0, 0, 0x70, 0x21, 0x60]) + bytes(x for b in raw for x in (b & 15, b >> 4)) + b"\xf7"


def main(org, vcz, out):
    tones = [tone(n, t, v) for n, (t, v) in enumerate(zip(casio(org), virtualcz(vcz)))]
    Path(out).parent.mkdir(parents=True, exist_ok=True)
    Path(out).write_bytes(b"".join(sysex(t) for t in tones))
    print(f"cz1 factory: {len(tones)} tones -> {out}")


if __name__ == "__main__":
    if len(sys.argv) not in (3, 4):
        sys.exit(__doc__)
    main(sys.argv[1], sys.argv[2],
         sys.argv[3] if len(sys.argv) == 4 else Path(__file__).resolve().parent.parent / "assets/cz1-factory/cz1-factory.syx")
