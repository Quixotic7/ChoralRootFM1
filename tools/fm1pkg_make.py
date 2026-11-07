#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Build a complete FM-1 update package (.fwsc) from scratch, with no vendor
material in it:

  flash.bin  head [0, 0x4000): flash header (JieLi SDK default values), JLFS
             entries, the SDK SPL (uboot.boot, Apache-2.0), an isd_config blob
             of our own that decodes to the chip key; the head is never written
             by Felucca's loader, it only has to look like a package head
             app area 0x4000..: app_area_head, Felucca app.bin, the SDK
             cfg_tool.bin and eq_cfg_hw.bin, the region descriptors, SFC-encrypted
             with the chip key
  ota.bin    Felucca's own update loader (firmware/loader)
  (FM-1, --res: flash.bin runs on to 0x9F000 with the resource region after the app area, plain: its header at
  0x93000, the data from 0x93040; firmware/src/cr_res.h, HISTORY 3y234)

The layout values (offsets, flags, descriptor addresses, header fields) are
facts of the FM-1 flash layout and the SDK file formats.

An app that starts with ChoralRoot's boot stub (firmware/stub: "CRSTUB" at app.bin + 8; docs/BOOT-SAFETY.md "As
built") gets the commit marker in app.bin's last whole sector (flash 0x91FE0: magic, format, the body's length and
CRC-32, the marker's own CRC-32) and four compensation bytes behind it, solved so that app.bin's CRC-16 is the
constant APP_CRC_FIXED: the directory in sector 0x4000 (app.bin's CRC, the app_area_head block CRC) is then the
same bytes for every build with the same stub, and the loader never rewrites that sector. Any other app (Felucca,
a ChoralRoot before the stub) is packed as before.

The three SDK files (uboot.boot, cfg_tool.bin, eq_cfg_hw.bin) are Apache-2.0, not GPL:
ship LICENSES/Apache-2.0.txt with every package (LICENSING.md; make_site.py and
build.py --release do).

  fm1pkg_make.py APP.bin OTA.bin OUT.fwsc [--board fm1|smk37] [--product FM-1_9XY] [--key 0x980F]

--board smk37 (M-Vave SMK-37, same AC791N, SPL and loader) selects the SMK flash layout: PRCT 0x9D000 and the stock
v016 region table (VM, BTIF, USRTRIM, USRFLASH, USR). app.bin's slot, the stub and the marker stay at the FM-1's
places, so the stub's layout is the same on both boards.
"""
import argparse
import os
import struct
import zlib
from pathlib import Path

APP_SLOT = 0x8DFBC
FLASH_SIZE = 0x93000
BOARD = "fm1"
# the region descriptors after app.bin / cfg_tool.bin: (offset, size, flags, resvd, index, name16 or name).
# The offsets are ABSOLUTE flash offsets. The 16 name bytes after the names are SDK packer fields.
REGIONS = {
    "fm1": [(0x93000, 352256, 0x12, 0x80, 0, bytes.fromhex("564d00ffffffffffffffffff00800000")),     # VM
            (0, None, 0x92, 0x82, 0, bytes.fromhex("5052435400ffffffffffffff434f4445")),         # PRCT = FLASH_SIZE
            (0xE9000, 0x1000, 0x92, 0x80, 0, bytes.fromhex("4254494600ffffffffffffff4155544f")),  # BTIF
            (0xEA000, 0x12000, 0x92, 0x81, 1, "USR")],
    # SMK-37 v016, as the stock package's region table
    "smk37": [(0x9D000, 0x23000, 0x12, 0x81, 0, bytes.fromhex("564d00ffffffffffffffffff00800000")),    # VM
              (0, None, 0x92, 0x82, 0, bytes.fromhex("5052435400ffffffffffffff434f4445")),        # PRCT = FLASH_SIZE
              (0xC0000, 0x1000, 0x92, 0x82, 0, bytes.fromhex("4254494600ffffffffffffff4155544f")), # BTIF
              (0xC1000, 0x1000, 0x92, 0x81, 0, bytes.fromhex("5553525452494d00ffffffffffffffff")), # USRTRIM
              (0xC2000, 0x29000, 0x92, 0x81, 0, bytes.fromhex("555352464c41534800ffffffffffffff")), # USRFLASH
              (0xF4000, 0xA000, 0x92, 0x81, 1, "USR")],
}
# per board: (FLASH_SIZE = PRCT = the end of what the update loader writes, the identity's model text)
BOARDS = {"fm1": (0x93000, "FM-1"),
          "smk37": (0x9D000, "SMK-37 Pro")}


def set_board(board):
    """select the flash layout of a board ("fm1" default, "smk37")"""
    global BOARD, FLASH_SIZE
    if board not in BOARDS:
        raise SystemExit(f"fm1pkg_make: unknown board {board!r} (fm1, smk37)")
    BOARD = board
    FLASH_SIZE = BOARDS[board][0]


KEY = 0x980F
# the boot stub's layout (firmware/stub/stub_core.h), as app.bin offsets (app.bin offset 0 = flash 0x4120)
STUB_LEN = 0xEE0                    # the stub; the body (the app proper) follows: flash 0x5000, XIP 0x02001000
STUB_SIG = b"CRSTUB"                # at app.bin + 8 (crt0_stub.S), then the stub's version (u16)
BODY_MAX = 0x8C000                  # the body ends below the marker's sector (flash 0x91000)
MARK_OFF = 0x91FE0 - 0x4120         # the marker: the last 32 bytes of flash sector 0x91000
MARK_MAGIC, MARK_FORMAT = 0x4D425243, 1     # "CRBM"
COMP_OFF = MARK_OFF + 20            # the four compensation bytes
# the resource region (firmware/src/cr_res.h, hal/fm1_flash_map.h FL_RES_LO; FM-1 only, HISTORY 3y234): flash.bin runs
# on past FLASH_SIZE (PRCT, the app area's end: unchanged) with the region's sectors, plain (no SFC: the app reads them
# through the plain XIP window), the 64-byte header at RES_LO, the data (build/res.bin, the app's .res) from RES_DATA.
# The update loader writes them with the body, before the commit marker (firmware/loader/ldr_core.c)
RES_LO, RES_HI = 0x93000, 0x9F000
RES_DATA = RES_LO + 0x40
RES_MAGIC, RES_FORMAT = 0x53455243, 1   # "CRES"
APP_CRC_FIXED = 0x4352              # app.bin's CRC-16 in every stub package ("CR")
# flash header: JieLi SDK defaults (burner_size 544, VID "0.01", flash_size 0xFF000,
# fs_ver 0x10, PID "AC791N_STORY"), plain, without its CRC
FLASH_HDR = bytes.fromhex("20022f104cc900f00f001010a4ff4e5d0b41e0cd781dc856417fb367cf9f")
UFW_CHIP = b"AC791N"
SDK = None


def _crc_table():
    t = []
    for i in range(256):
        c = i << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) if c & 0x8000 else (c << 1)
        t.append(c & 0xFFFF)
    return t


_T = _crc_table()


def crc16(data, crc=0):
    for b in data:
        crc = ((crc << 8) & 0xFFFF) ^ _T[((crc >> 8) ^ b) & 0xFF]
    return crc


def enc(buf, off, size, key=0xFFFF):
    """JieLi ENC stream cipher (in place)"""
    for i in range(size):
        buf[off + i] ^= key & 0xFF
        key = ((key << 1) ^ (0x1021 if key & 0x8000 else 0)) & 0xFFFF


def sfc(buf, off, size, base, key):
    """SFC cipher: ENC per 32-byte block, key mixed with the block address"""
    for i in range(0, size, 32):
        enc(buf, off + i, min(size - i, 32), key ^ ((off + i - base) >> 2))


def chipkey_decode(d):
    """chip key from the 32-byte isd_config blob"""
    thesum = sum(d[:16]) & 0xFF
    thesum = 0xAA if thesum >= 0xE0 else 0x55 if thesum <= 0x10 else thesum
    return sum(1 << i for i in range(16) if (d[16 + i] ^ d[15 - i]) < thesum)


def entry(dcrc, offset, size, flags, resvd, index, name, fill=0xFF):
    if isinstance(name, bytes):                     # 16 raw bytes: name + the packer's trailing fields
        nm = name
    else:
        nm = name.encode() + b"\0"
        nm = nm + bytes([fill]) * (16 - len(nm))
    body = struct.pack("<HIIBBH16s", dcrc, offset & 0xFFFFFFFF, size & 0xFFFFFFFF, flags, resvd, index, nm)
    return struct.pack("<H", crc16(body)) + body


def key_blob(key):
    """32 bytes that chipkey_decode() turns into `key` (+ CRC16)"""
    d = bytearray([0x10] * 16) + bytearray(16)        # sum 0 -> threshold 0x55
    for i in range(16):
        d[16 + i] = d[15 - i] ^ (0x00 if key >> i & 1 else 0xFF)
    assert chipkey_decode(bytes(d)) == key
    return bytes(d) + struct.pack("<H", crc16(d))


def sdk_file(rel):
    root = Path(SDK or os.environ.get("AC79_SDK", ""))
    path = root / "cpu" / "wl82" / "tools" / rel
    try:
        return path.read_bytes()
    except OSError as e:
        raise SystemExit(f"fm1pkg_make: {path}: {e.strerror}. Set AC79_SDK to a JieLi AC79 SDK checkout.")


def has_stub(app):
    return len(app) >= STUB_LEN and app[8:8 + len(STUB_SIG)] == STUB_SIG


def stub_version(app):
    return struct.unpack_from("<H", app, 8 + len(STUB_SIG))[0] if has_stub(app) else None


def marker(body):
    """the 20-byte commit marker of a body (app.bin from STUB_LEN, padded to words)"""
    m = struct.pack("<IIII", MARK_MAGIC, MARK_FORMAT, len(body), zlib.crc32(body))
    return m + struct.pack("<I", zlib.crc32(m))


def compensate(app, off, want):
    """4 bytes for app[off:off + 4] (zero on entry) that make crc16(app) == want.

    The CRC-16 (init 0, no final xor) is linear over GF(2): crc(a ^ b) = crc(a) ^ crc(b) for equal lengths. So
    crc(app with bytes v at off) = crc(app) ^ E(v), E(v) = crc(v + zeros to the end): 16 equations in 32 unknown
    bits, solved by elimination over the 32 basis vectors (the free bits stay 0)."""
    tail = bytes(len(app) - off - 4)
    need = crc16(app) ^ want
    rows = []                                           # (E(bit k), 1 << k), reduced as they come
    for k in range(32):
        e, m = crc16((1 << k).to_bytes(4, "little") + tail), 1 << k
        for pe, pm in rows:
            if e ^ pe < e:                              # pe's top bit is set in e
                e, m = e ^ pe, m ^ pm
        if e:
            rows.append((e, m))
            rows.sort(reverse=True)
    v = 0
    for pe, pm in rows:
        if need ^ pe < need:
            need, v = need ^ pe, v ^ pm
    if need:
        raise SystemExit("fm1pkg_make: no compensation bytes give the fixed CRC-16")
    return v.to_bytes(4, "little")


def crc16_slow(data):
    """bit by bit, no table: the check of compensate() and of both directory CRCs"""
    c = 0
    for b in data:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) & 0xFFFF if c & 0x8000 else c << 1
    return c


def commit(app, marker_of=marker):
    """a stub app padded to the slot, with its marker and compensation bytes (marker_of: tools/make_stub_test_pkg.py
    packs a deliberately wrong one)"""
    body = app[STUB_LEN:]
    body += b"\xFF" * (-len(body) % 4)
    if len(body) > BODY_MAX:
        raise SystemExit(f"the app body is {len(body)} B, the room below the marker's sector is {BODY_MAX} B")
    if not body:
        raise SystemExit("the app has a stub and no body")
    a = bytearray(app[:STUB_LEN] + body + b"\xFF" * (APP_SLOT - STUB_LEN - len(body)))
    a[MARK_OFF:MARK_OFF + 20] = marker_of(body)
    a[COMP_OFF:COMP_OFF + 4] = bytes(4)
    a[COMP_OFF:COMP_OFF + 4] = compensate(bytes(a), COMP_OFF, APP_CRC_FIXED)
    if crc16_slow(a) != APP_CRC_FIXED:
        raise SystemExit("fm1pkg_make: the compensated app.bin CRC-16 is wrong")
    return bytes(a)


def res_header(data, body_crc, ble=(0, 0)):
    """the resource region's 64-byte header (cr_res.h cr_res_hdr_t): magic, format, length and CRC-32 of the data, the
    body's CRC-32 (its commit marker's: which build the region is for), the radio program's slot, 8 x 0, own CRC-32"""
    h = struct.pack("<7I", RES_MAGIC, RES_FORMAT, len(data), zlib.crc32(data), body_crc, *ble) + bytes(32)
    return h + struct.pack("<I", zlib.crc32(h))


def flash_image(app, key, marker_of=marker, res=None, ble=(0, 0)):
    spl = sdk_file("uboot.boot")                         # SDK SPL, BANKCB-encoded as in the SDK
    cfg_tool = sdk_file("cfg_tool.bin")
    eq = sdk_file("cfg/eq_cfg_hw.bin")
    isd = key_blob(key) + b"[FELUCCA]\r\n"
    f = bytearray(b"\xFF" * FLASH_SIZE)
    # ---- head
    spl_off = 0xA0
    isd_off = spl_off + len(spl)
    assert isd_off + len(isd) <= 0x4000
    f[spl_off:spl_off + len(spl)] = spl
    f[isd_off:isd_off + len(isd)] = isd
    top = (entry(crc16(spl), spl_off, len(spl), 0x00, 0x00, 0, "uboot.boot") +
           entry(crc16(isd), isd_off, len(isd), 0x02, 0x80, 0, "isd_config.ini") +
           entry(0xFFFF, 0x4000, 0xFFFFFFFF, 0x81, 0xFF, 0, "app_dir_head") +
           entry(0xFFFF, 0xFF000, 0x1000, 0x12, 0x01, 0xFFFF, "key_mac"))
    hdr = struct.pack("<H", crc16(FLASH_HDR)) + FLASH_HDR
    head = bytearray(hdr + top)
    for o in range(0, len(head), 32):                   # header and each JLFS entry: ENC 0xFFFF
        enc(head, o, 32)
    f[0:len(head)] = head
    # ---- app area (plain first, then SFC)
    if len(app) > APP_SLOT:
        raise SystemExit(f"app is {len(app)} B, the slot is {APP_SLOT} B")
    stub = has_stub(app)
    app = commit(app, marker_of) if stub else app + b"\xFF" * (APP_SLOT - len(app))
    blk = 0x120 + APP_SLOT + len(cfg_tool)              # app_area_head block size
    area = bytearray(b"\xFF" * blk)
    area[0x120:0x120 + APP_SLOT] = app
    area[0x120 + APP_SLOT:blk] = cfg_tool
    ents = [entry(crc16(app), 0x120, APP_SLOT, 0x82, 0xFF, 0, "app.bin"),
            entry(crc16(cfg_tool), 0x120 + APP_SLOT, len(cfg_tool), 0x82, 0xFF, 0, "cfg_tool.bin"),
            # region descriptors; the bytes after the names are SDK packer fields (VM size, "CODE", "AUTO")
            *(entry(0xFFFF, o, FLASH_SIZE if sz is None else sz, fl, rs, ix, nm)
              for o, sz, fl, rs, ix, nm in REGIONS[BOARD])]
    area[32:32 + len(b"".join(ents))] = b"".join(ents)
    area[0:32] = entry(crc16(area[32:blk]), 0x02000120, blk, 0x83, 0xFF, 0, "app_area_head")
    if stub:                                            # every CRC of the directory the slow way: still valid
        if (crc16_slow(area[0x120:0x120 + APP_SLOT]) != struct.unpack_from("<H", area, 34)[0] or
                crc16_slow(area[32:blk]) != struct.unpack_from("<H", area, 2)[0] or
                crc16_slow(area[2:32]) != struct.unpack_from("<H", area, 0)[0] or
                crc16_slow(area[34:64]) != struct.unpack_from("<H", area, 32)[0]):
            raise SystemExit("fm1pkg_make: a directory CRC-16 does not hold")
    # nested cfg directory right after the block: SDK default EQ table
    eq_e = entry(crc16(eq), 0x40, len(eq), 0x82, 0xFF, 1, "eq_cfg_hw.bin")
    cfg_body = eq_e + eq
    cfg = entry(crc16(cfg_body), 0x20, 0x20 + len(cfg_body), 0x83, 0xFF, 1, "cfg") + cfg_body
    region = bytearray(area + cfg)
    sfc(region, 0, len(region), 0, key)
    f[0x4000:0x4000 + len(region)] = region
    if res is not None:                                 # the resource region: after the app area, plain
        if BOARD != "fm1" or not stub or FLASH_SIZE != RES_LO:
            raise SystemExit("fm1pkg_make: a resource region needs the FM-1 layout and an app with the boot stub")
        if not 0 < len(res) <= RES_HI - RES_DATA:
            raise SystemExit(f"fm1pkg_make: the resource data is {len(res)} B (1 .. {RES_HI - RES_DATA})")
        body_crc = struct.unpack_from("<I", app, MARK_OFF + 12)[0]   # (the marker commit() wrote: its CRC-32 field)
        f += b"\xFF" * (RES_HI - len(f))
        if ble[1] and ble[0] + ble[1] > len(res):
            raise SystemExit(f"fm1pkg_make: the radio program's slot {ble} lies outside the region's data")
        f[RES_LO:RES_DATA] = res_header(res, body_crc, ble)
        f[RES_DATA:RES_DATA + len(res)] = res
    return bytes(f)


def ufw(flash, ota, product):
    files = [(0, "flash.bin", flash), (100, "ota.bin", ota)]
    off, ents, data = 0x400, [], bytearray()
    for i, (typ, name, d) in enumerate(files):
        off = (off + 0xFF) & ~0xFF
        e = bytearray(0x50)
        struct.pack_into("<HHHHIII", e, 0, typ, i, crc16(d), 0, off, len(d), (len(d) + 31) & ~31)
        e[0x40:0x40 + len(name)] = name.encode()
        ents.append((e, off, d))
        off += len(d)
    total = off
    hdr = bytearray(0x40)
    struct.pack_into("<IH", hdr, 4, total, len(files))
    struct.pack_into("<HHH", hdr, 10, 0x0004, 0x0200, 0)     # header fields as written by the SDK packer
    hdr[16:16 + len(UFW_CHIP)] = UFW_CHIP
    lst = bytearray()
    for e, _, _ in ents:
        x = bytearray(e)
        enc(x, 0, 0x50)
        lst += x
    struct.pack_into("<H", hdr, 2, crc16(lst))
    struct.pack_into("<H", hdr, 0, crc16(hdr[2:0x40]))
    enc(hdr, 0, 0x40)
    logical = bytearray(b"\xFF" * total)
    logical[0:0x40] = hdr
    logical[0x40:0x40 + len(lst)] = lst
    for e, o, d in ents:
        logical[o:o + len(d)] = d
    # .fwsc: one marker byte after each of the first 20 0x2F-byte blocks (the identity)
    if len(product) > 20 or any((ord(c) + i + 1) & 0xFF == 0x7D for i, c in enumerate(product)):
        raise SystemExit(f"product {product!r}: at most 20 characters, none encoding to the unused marker")
    raw = bytearray()
    for i in range(20):
        ch = product[i] if i < len(product) else None
        m = (ord(ch) + i + 1) & 0xFF if ch else 0x7D
        raw += logical[i * 0x2F:(i + 1) * 0x2F] + bytes([m])
    raw += logical[20 * 0x2F:]
    return bytes(raw)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("app", type=Path)
    ap.add_argument("ota", type=Path)
    ap.add_argument("out", type=Path)
    ap.add_argument("--board", choices=sorted(BOARDS), default="fm1")
    ap.add_argument("--product", help="package identity (default: FM-1_900; smk37: SMK-37 Pro_900)")
    ap.add_argument("--key", type=lambda s: int(s, 0), default=KEY)
    ap.add_argument("--res", type=Path, help="the resource region's data (build/res.bin; FM-1, an app with the stub)")
    ap.add_argument("--sdk", type=Path, help="JieLi AC79 SDK checkout (default: $AC79_SDK)")
    a = ap.parse_args()
    global SDK
    SDK = a.sdk
    set_board(a.board)
    a.product = a.product or BOARDS[a.board][1] + "_900"
    pkg = ufw(flash_image(a.app.read_bytes(), a.key, res=a.res.read_bytes() if a.res else None), a.ota.read_bytes(),
              a.product)
    a.out.write_bytes(pkg)
    print(f"package {a.out}: {len(pkg)} B, identity {a.product}, chip key {a.key:#06x}")


if __name__ == "__main__":
    main()
