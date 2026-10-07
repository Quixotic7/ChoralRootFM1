"""FM-1 .fwsc package helpers shared by the fm1_ota / fm1fw stand-ins (standard library only).

Mirrors tools/fm1_install.py product_of / logical_image and MvaveFM1Unbricker parse_fwsc: the first
BLOCKS blocks of the package are KEEP data bytes plus one identity marker byte; the logical image (the
package without the marker bytes) is the flash image from address 0.
"""
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F
IMAGE_LEN = 0x93000            # flash image from address 0: head [0, 0x4000) + firmware [0x4000, 0x93000)
HEAD_END = 0x4000
LOADER_MARK = b"FELUCCA-LOADER-1"
V15_IDENTITY = "FM-1_015"
V15_FW_SHA256 = "6edf3c37fb5bbbc33607c89375ee024d5477c17914d72221c8c68e58a8255686"   # logical [0x4000, 0x93000)
CR_IDENTITY = "FM-1_920"


def product_of(raw):
    if len(raw) < BLOCKS * BLK:
        raise ValueError("not an FM-1 package (too short)")
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def logical_image(raw):
    out = bytearray()
    for i in range(BLOCKS):
        out += raw[i * BLK:i * BLK + KEEP]
    return bytes(out + raw[BLOCKS * BLK:])


def crc16(data, c=0):
    """firmware/src/ota.c ota_crc16: poly 0x1021, init 0"""
    for b in data:
        c ^= b << 8
        for _ in range(8):
            c = ((c << 1) ^ 0x1021) if c & 0x8000 else c << 1
        c &= 0xFFFF
    return c


def jl_enc(buf):
    """firmware/src/ota.c ota_jl_enc: UFW header cipher, key 0xFFFF (its own inverse)"""
    out, k = bytearray(buf), 0xFFFF
    for i in range(len(out)):
        out[i] ^= k & 0xFF
        k = ((k << 1) ^ (0x1021 if k & 0x8000 else 0)) & 0xFFFF
    return bytes(out)


def _rd16(p, o):
    return p[o] | p[o + 1] << 8


def _rd32(p, o):
    return _rd16(p, o) | _rd16(p, o + 2) << 16


def ufw_flash(logical):
    """firmware/src/ota.c ota_ufw on the logical image -> (fl_off, fl_len) of the type-0 entry (flash.bin).
    Raises ValueError on a bad header / entry-list CRC or no flash.bin."""
    if len(logical) < 0x400:
        raise ValueError("not a UFW package (shorter than its 0x400-byte header)")
    hdr = jl_enc(logical[:0x40])
    nent = _rd16(hdr, 8)
    if crc16(hdr[2:0x40]) != _rd16(hdr, 0):
        raise ValueError("UFW header CRC fails")
    if nent == 0 or nent > 11:
        raise ValueError(f"UFW header lists {nent} entries")
    if crc16(logical[0x40:0x40 + nent * 0x50]) != _rd16(hdr, 2):
        raise ValueError("UFW entry list CRC fails")
    fl = None
    for i in range(nent):
        e = jl_enc(logical[0x40 + i * 0x50:0x90 + i * 0x50])
        if _rd16(e, 0) == 0:
            fl = (_rd32(e, 8), _rd32(e, 12))
    if fl is None:
        raise ValueError("no flash.bin (type 0) entry in the UFW header")
    return fl
