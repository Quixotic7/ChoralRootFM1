# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Minimal LZ4 *block* format (no frame), written for the Felucca loader
wrapper: greedy hash-chain-free compressor and a decoder for self-checks.
Spec: https://github.com/lz4/lz4/blob/dev/doc/lz4_Block_format.md
(min match 4, last 5 bytes literals, last match starts >= 12 bytes before the end)."""


def _len_bytes(n):
    out = bytearray()
    while n >= 255:
        out.append(255)
        n -= 255
    out.append(n)
    return out


def compress(src: bytes) -> bytes:
    n, out, table = len(src), bytearray(), {}
    anchor, i = 0, 0
    limit = n - 12                                  # matches must start before this
    while i < limit:
        key = src[i:i + 4]
        cand = table.get(key)
        table[key] = i
        if cand is None or i - cand > 65535:
            i += 1
            continue
        mlen = 4                                    # extend; leave the last 5 bytes as literals
        while i + mlen < n - 5 and src[cand + mlen] == src[i + mlen]:
            mlen += 1
        lit = src[anchor:i]
        tok = (min(len(lit), 15) << 4) | min(mlen - 4, 15)
        out.append(tok)
        if len(lit) >= 15:
            out += _len_bytes(len(lit) - 15)
        out += lit
        off = i - cand
        out += bytes((off & 0xFF, off >> 8))
        if mlen - 4 >= 15:
            out += _len_bytes(mlen - 4 - 15)
        i += mlen
        anchor = i
    lit = src[anchor:]                              # final literal run
    out.append(min(len(lit), 15) << 4)
    if len(lit) >= 15:
        out += _len_bytes(len(lit) - 15)
    out += lit
    return bytes(out)


def decompress(blk: bytes, prefix: bytes = b"") -> bytes:
    out, i = bytearray(prefix), 0
    while i < len(blk):
        tok = blk[i]
        i += 1
        ll = tok >> 4
        if ll == 15:
            while True:
                b = blk[i]
                i += 1
                ll += b
                if b != 255:
                    break
        out += blk[i:i + ll]
        i += ll
        if i >= len(blk):
            break
        off = blk[i] | blk[i + 1] << 8
        i += 2
        ml = tok & 15
        if ml == 15:
            while True:
                b = blk[i]
                i += 1
                ml += b
                if b != 255:
                    break
        ml += 4
        for _ in range(ml):
            out.append(out[-off])
    return bytes(out[len(prefix):])
