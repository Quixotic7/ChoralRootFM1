#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
# SMK-37 port: Copyright (C) 2026 M. Geisler (genkigenki/makkuro)
"""A ChoralRoot package for the M-Vave SMK-37 (fm1pkg_make.py --board smk37, build.py BOARD=smk37): flash.bin is the
stock PRCT size (0x9D000) and its app area carries the stock v016 region table exactly, so the SPL and a later stock
rollback see the same VM / BTIF / USRTRIM / USRFLASH / USR regions; app.bin keeps the FM-1's slot, so the boot stub,
the body and the commit marker sit where they sit on the FM-1 (docs/BOOT-SAFETY.md).
  smk_pkg_test.py PACKAGE.fwsc [PRODUCT]"""
import struct
import zlib
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import fm1_install as I  # noqa: E402
import fm1pkg_make as M  # noqa: E402

failed = 0


def ok(cond, what):
    global failed
    print(f"smk pkg: {what:<76} {'ok' if cond else 'FAIL'}")
    failed += not cond


def dec(b):
    b = bytearray(b)
    M.enc(b, 0, len(b))
    return bytes(b)


raw = Path(sys.argv[1]).read_bytes()
want_product = sys.argv[2] if len(sys.argv) > 2 else "SMK-37 Pro_920"
image = I.logical_image(raw)
ok(I.product_of(raw) == want_product, f"identity {I.product_of(raw)}")
hdr = dec(image[:0x40])
nfiles = struct.unpack_from("<H", hdr, 8)[0]
files = {}
for i in range(nfiles):
    e = dec(image[0x40 + i * 0x50:0x90 + i * 0x50])
    typ, _, crc, _, off, size = struct.unpack_from("<HHHHII", e, 0)
    files[e[0x40:0x50].rstrip(b"\0").decode()] = (off, size, crc)
ok(set(files) == {"flash.bin", "ota.bin"}, "flash.bin and ota.bin")
off, size, crc = files["flash.bin"]
flash = image[off:off + size]
ok(size in (0x9D000, 0xA9000) and M.crc16(flash) == crc,
   f"flash.bin {size:#x} B (stock PRCT 0x9D000; with the resource region to 0xA9000), its CRC")
area = bytearray(flash[0x4000:0x4000 + 0x400])
M.sfc(area, 0, len(area), 0, M.KEY)
ents = []
for k in range(1, 12):
    e = bytes(area[k * 32:(k + 1) * 32])
    if M.crc16(e[2:]) != struct.unpack_from("<H", e)[0]:
        break
    dcrc, eoff, esize, flags, resvd, index = struct.unpack_from("<HIIBBH", e, 2)
    ents.append((eoff, esize, flags, resvd, index, e[16:32]))
want = [(0x9D000, 0x23000, 0x12, 0x81, 0, bytes.fromhex("564d00ffffffffffffffffff00800000")),
        (0, 0x9D000, 0x92, 0x82, 0, bytes.fromhex("5052435400ffffffffffffff434f4445")),
        (0xC0000, 0x1000, 0x92, 0x82, 0, bytes.fromhex("4254494600ffffffffffffff4155544f")),
        (0xC1000, 0x1000, 0x92, 0x81, 0, bytes.fromhex("5553525452494d00ffffffffffffffff")),
        (0xC2000, 0x29000, 0x92, 0x81, 0, bytes.fromhex("555352464c41534800ffffffffffffff")),
        (0xF4000, 0xA000, 0x92, 0x81, 1, b"USR\0" + b"\xff" * 12)]
ok([e[5][:7] for e in ents[:2]] == [b"app.bin", b"cfg_too"], "app.bin, cfg_tool.bin first")
ok(ents[:1] and ents[0][:2] == (0x120, M.APP_SLOT), f"app.bin: the FM-1's slot ({M.APP_SLOT:#x} B from flash 0x4120)")
ok(ents[2:] == want, "region table == stock v016 (VM, PRCT, BTIF, USRTRIM, USRFLASH, USR)")
app = bytearray(flash[0x4000:0x4000 + 0x120 + M.APP_SLOT])
M.sfc(app, 0, len(app), 0, M.KEY)
app = bytes(app[0x120:])
mark = app[M.MARK_OFF:M.MARK_OFF + 20]
ok(M.has_stub(app) and struct.unpack_from("<I", mark)[0] == M.MARK_MAGIC and
   M.marker(app[M.STUB_LEN:M.STUB_LEN + struct.unpack_from("<I", mark, 8)[0]]) == mark,
   "the boot stub, the commit marker at 0x91FE0 over the body")
ok(M.crc16(app) == M.APP_CRC_FIXED, f"app.bin's CRC-16 the constant {M.APP_CRC_FIXED:#06x} (sector 0x4000 stays)")
ok(flash[0x93000:0x9D000] == b"\xff" * (0x9D000 - 0x93000), "0x93000..0x9CFFF blank (the SMK's app area past the FM-1's)")
if size > 0x9D000:                                  # the resource region (firmware/src/cr_res.h, HISTORY 3y295)
    h = flash[0x9D000:0x9D040]
    magic, fmt, n, dcrc, body_crc, bo, bl = struct.unpack_from("<7I", h)
    ok(magic == M.RES_MAGIC and fmt == M.RES_FORMAT and struct.unpack_from("<I", h, 60)[0] == zlib.crc32(h[:60]) and
       h[28:60] == bytes(32), "the region's header at 0x9D000: magic, format, own CRC-32")
    ok(0 < n <= 0xA9000 - 0x9D040 and zlib.crc32(flash[0x9D040:0x9D040 + n]) == dcrc and
       flash[0x9D040 + n:] == b"\xff" * (0xA9000 - 0x9D040 - n), f"its data from 0x9D040 ({n} B), CRC-32, blank after")
    ok(body_crc == struct.unpack_from("<I", mark, 12)[0] and (bo, bl) == (0, 0),
       "its body CRC-32 = the commit marker's (this build's region), no radio program")
ok(flash[:0x4000] != b"\xff" * 0x4000, "head present (never written by the loader, checked against the device)")
print("SMK PACKAGE TEST FAILED" if failed else "smk package test passed")
sys.exit(1 if failed else 0)
