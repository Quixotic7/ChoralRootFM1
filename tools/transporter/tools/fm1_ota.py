"""Stand-in for fm-1-research-lab's fm1_ota.require_reviewed: ChoralRoot's review policy for the Transporter.

Accepts (1) the official V15 (identity FM-1_015, firmware region sha256 as MvaveFM1Unbricker), or
(2) a ChoralRoot package (identity FM-1_920 with the Felucca loader marker; its head [0, 0x4000) is compared
with the V15 package's at $FM1_V15 when set, as a note only: the head is never written). Anything else, a UFW header that fails its CRCs or lacks flash.bin, or a flash.bin that is not 0x93000 bytes,
exits. Reads files only; never writes, never uses the network.
"""
import hashlib
import os
import sys

from _fm1pkg import (CR_IDENTITY, HEAD_END, IMAGE_LEN, LOADER_MARK, V15_FILE_SHA256, V15_FW_SHA256, V15_IDENTITY,
                     logical_image, product_of, ufw_flash)


def _refuse(path, why):
    sys.exit(f"fm1_ota: refusing {path}: {why}")


def _load(path):
    try:
        raw = open(path, "rb").read()
    except OSError as e:
        _refuse(path, f"cannot read it ({e.strerror})")
    try:
        product = product_of(raw)
    except ValueError as e:
        _refuse(path, str(e))
    logical = logical_image(raw)
    try:
        fl_off, fl_len = ufw_flash(logical)
    except ValueError as e:
        _refuse(path, str(e))
    if fl_len != IMAGE_LEN or fl_off + fl_len > len(logical):
        _refuse(path, f"flash.bin is {fl_len:#x} bytes at {fl_off:#x}, not {IMAGE_LEN:#x} bytes inside the package")
    return raw, product, logical, logical[fl_off:fl_off + fl_len]


def require_reviewed(path):
    raw, product, logical, img = _load(path)
    file_sha = hashlib.sha256(raw).hexdigest()
    print(f"fm1_ota: package sha256 {file_sha} ({'equals' if file_sha == V15_FILE_SHA256 else 'is not'} "
          "the official V15 FM-1.fwsc)")
    fw_sha = hashlib.sha256(img[HEAD_END:]).hexdigest()   # flash.bin region 0x4000..0x92FFF
    if product == V15_IDENTITY and fw_sha == V15_FW_SHA256:
        print("fm1_ota: accepted by rule 1: official M-VAVE V15 (firmware sha256 verified)")
        return
    if product == V15_IDENTITY:
        _refuse(path, f"identity {product} but the firmware sha256 {fw_sha[:16]}... is not the official V15's")
    if product != CR_IDENTITY:
        _refuse(path, f"identity {product!r} is neither the official V15 nor ChoralRoot ({CR_IDENTITY})")
    if LOADER_MARK not in raw:
        _refuse(path, f"identity {product} but no Felucca loader marker {LOADER_MARK.decode()}")
    v15 = os.environ.get("FM1_V15")
    if not v15:
        print("fm1_ota: accepted by rule 2: ChoralRoot package (WARNING: FM1_V15 not set, "
              "head 0x0000..0x3FFF not compared with V15)")
        return
    _, v15_product, _, v15_img = _load(v15)
    if v15_product != V15_IDENTITY:
        _refuse(path, f"FM1_V15={v15} is not a V15 package (identity {v15_product!r})")
    n = sum(a != b for a, b in zip(img[:HEAD_END], v15_img[:HEAD_END]))
    if n:
        print(f"fm1_ota: note: head 0x0000..0x3FFF differs from V15 in {n} bytes; the head is never written")
    print("fm1_ota: accepted by rule 2: ChoralRoot package" + ("" if n else " (head 0x0000..0x3FFF equals V15's)"))
