#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The user sounds in an emulator flash image (tools/emu/test_cr.sh; docs/PRESETS.md "The pool").

  pool_image.py pool IMAGE           the pool's slots (0.22: storage.c sp_*, upool.c's slot layout): one line a used slot

(0.21's slot objects and test_pool.sh's migration checks went with the migration, HISTORY 3y231.)

A line: slot (1-based), engine, name, the record's CRC-32, the patch's engine and length and CRC-32."""
import struct
import sys
import zlib

MAGIC = 0x554C4546
SECTOR = 4096
PAYLOAD = 256
ENGI_VA, ENGI_FM6, ENGI_CZ, ENGI_QUAD, ENGI_P5 = 13, 12, 14, 15, 16
BLOB = {ENGI_VA: 110, ENGI_FM6: 128, ENGI_CZ: 144, ENGI_QUAD: 80, ENGI_P5: 138}
NAMES = {ENGI_VA: 'VA', ENGI_FM6: 'FM6', ENGI_CZ: 'CZ-1', ENGI_QUAD: 'FM TONE', ENGI_P5: 'P5', 1: 'DIGITAL', 0: 'ANALOG'}
SP_BASE, SP_NSEC, SP_NBANK, SP_PER, SP_SLOT, SP_TYPE = 0xB8000, 16, 13, 10, 384, 0x4C50


def head(img, base, typ, slot, pool=False):
    h = img[base:base + 32]
    magic, t, s, seq, ln, crc, r0, r1, hcrc = struct.unpack('<IHHIIIIII', h)
    if magic != MAGIC or t != typ or zlib.crc32(h[:28]) != hcrc or ln > SECTOR - PAYLOAD:
        return None
    if (not pool and s != slot) or (pool and (s >= SP_NBANK or ln != SECTOR - PAYLOAD or r0 != 1)):
        return None
    body = img[base + PAYLOAD:base + PAYLOAD + ln]
    if zlib.crc32(body) != crc:
        return None
    return seq, s, body


def newer(a, b):
    return a != b and ((a - b) & 0xFFFFFFFF) < 0x80000000


def rec_valid(r):
    used, ver, eng, np_ = r[0], r[1], r[2], r[3]
    if not (used == 0xA5 and 1 <= ver <= 5 and eng < 17 and 8 <= np_ <= (144 if ver >= 4 else 72) and r[4]):
        return False
    return not (ver >= 4 and any(r[16 + i] > 191 for i in range(np_)))


def pool(img):
    """{slot: (record bytes, patch engine, patch bytes)} of the pool (0.22)"""
    cand = {}
    for s in range(SP_NSEC):
        h = head(img, SP_BASE + s * SECTOR, SP_TYPE, 0, pool=True)
        if h:
            seq, b, body = h
            if b not in cand or newer(seq, cand[b][0]):
                cand[b] = (seq, body)
    out = {}
    for b, (_, body) in cand.items():
        for j in range(SP_PER):
            sl = body[j * SP_SLOT:(j + 1) * SP_SLOT]
            r = sl[:192]
            if b * SP_PER + j >= 128 or not rec_valid(r):
                continue
            pe, pn = sl[192], sl[193]
            out[b * SP_PER + j + 1] = (r, pe - 1 if pe else 0, sl[196:196 + pn] if pe else b'')
    return out


def show(slots):
    for k in sorted(slots):
        r, pe, p = slots[k]
        name = r[4:16].split(b'\0')[0].decode('ascii', 'replace')
        print('U%03u %-7s %-12s rec %08x patch %s %u %08x' % (k, NAMES.get(r[2], str(r[2])), name, zlib.crc32(r),
                                                             NAMES.get(pe, '-') if pe else '-', len(p), zlib.crc32(p)))


def main():
    cmd = sys.argv[1]
    img = open(sys.argv[2], 'rb').read()
    if cmd == 'pool':
        show(pool(img))
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main()
