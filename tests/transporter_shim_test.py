"""tools/transporter (the FM-1 Transporter's fm1_ota / fm1fw stand-ins) on synthetic packages."""
import io
import os
import sys
import tempfile
from contextlib import redirect_stdout
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "tools"))
sys.path.insert(0, str(ROOT / "tools" / "transporter" / "tools"))
import fm1_install as I  # noqa: E402
import fm1pkg_make as M  # noqa: E402
import fm1_ota  # noqa: E402
from fm1fw import Firmware  # noqa: E402
from _fm1pkg import crc16, jl_enc  # noqa: E402

IMG = 0x93000
TMP = Path(tempfile.mkdtemp(prefix="transporter-shim-"))
fails = 0


def check(name, cond):
    global fails
    print(("ok   " if cond else "FAIL ") + name)
    fails += not cond


def flash(seed=7, n=IMG):
    return bytes((i * seed + (i >> 12)) & 0xFF for i in range(n))


def package(product, marker=True, fl=None, seed=7):
    """a UFW package as tools/fm1pkg_make.py ufw() writes it: flash.bin at 0x400, ota.bin (loader) after"""
    ota = bytes(0x48) + (I.LOADER_MARK if marker else bytes(16)) + bytes(0x100)
    return M.ufw(fl if fl is not None else flash(seed), ota, product)


def remark(logical, product):
    """logical image -> .fwsc with the identity marker bytes (as fm1pkg_make.py)"""
    raw = bytearray()
    for i in range(20):
        raw += logical[i * 0x2F:(i + 1) * 0x2F] + bytes([(ord(product[i]) + i + 1) & 0xFF if i < len(product) else 0x7D])
    return bytes(raw + logical[20 * 0x2F:])


def pkgfile(name, raw):
    p = TMP / name
    p.write_bytes(raw)
    return str(p)


def review(path, v15=None):
    """-> (accepted, output)"""
    old = os.environ.pop("FM1_V15", None)
    if v15:
        os.environ["FM1_V15"] = v15
    out = io.StringIO()
    try:
        with redirect_stdout(out):
            fm1_ota.require_reviewed(path)
        return True, out.getvalue()
    except SystemExit as e:
        return False, str(e.code)
    finally:
        os.environ.pop("FM1_V15", None)
        if old is not None:
            os.environ["FM1_V15"] = old


cr = package("FM-1_920")
fw = Firmware(cr)
e = next(e for e in fw.entries if e["type"] == 0)
img = bytes(fw.raw[e["data_off"] + fw.skew:e["data_off"] + fw.skew + e["size"]])
check("Firmware type-0 entry is flash.bin at 0x400, slice == the flash image",
      e["data_off"] == 0x400 and fw.skew == 0 and img == flash() and fw.raw == I.logical_image(cr))
check("Firmware.image0() == the fm1t.py slice", fw.image0() == img)
check("Firmware.product == product_of", fw.product == I.product_of(cr) == "FM-1_920")

ok, out = review(pkgfile("cr.fwsc", cr))
check("FM-1_920 + marker accepted (rule 2, head-not-compared warning)", ok and "rule 2" in out and "WARNING" in out)
v15 = pkgfile("v15.fwsc", package("FM-1_015", marker=False))
ok, out = review(pkgfile("cr.fwsc", cr), v15)
check("FM-1_920 head equal to FM1_V15's accepted", ok and "equals V15" in out)
other = package("FM-1_015", marker=False, seed=11)
ok, out = review(pkgfile("cr.fwsc", cr), pkgfile("v15b.fwsc", other))
check("FM-1_920 head differing from FM1_V15's refused", not ok and "differs" in out)
ok, out = review(v15)
check("FM-1_015 with a wrong firmware hash refused", not ok and "sha256" in out)
ok, out = review(pkgfile("f900.fwsc", package("FM-1_900", marker=False)))
check("FM-1_900 without a marker refused", not ok and "FM-1_900" in out)
ok, out = review(pkgfile("f920nm.fwsc", package("FM-1_920", marker=False)))
check("FM-1_920 without a marker refused", not ok and "marker" in out)
ok, out = review(pkgfile("short.fwsc", package("FM-1_920", fl=flash(n=IMG - 0x1000))))
check("short flash.bin refused", not ok and "not 0x93000" in out)
lg = bytearray(I.logical_image(cr))
lg[0x50] ^= 1
ok, out = review(pkgfile("badcrc.fwsc", remark(lg, "FM-1_920")))
check("entry list CRC failure refused", not ok and "entry list CRC" in out)
lg = bytearray(I.logical_image(cr))
lg[0x10] ^= 1
ok, out = review(pkgfile("badhdr.fwsc", remark(lg, "FM-1_920")))
check("header CRC failure refused", not ok and "header CRC" in out)
lg = bytearray(I.logical_image(cr))
e0 = bytearray(jl_enc(lg[0x40:0x90]))
e0[0] = 5                                           # flash.bin's type 0 -> 5
lg[0x40:0x90] = jl_enc(e0)
hdr = bytearray(jl_enc(lg[:0x40]))
hdr[2:4] = crc16(lg[0x40:0x40 + 2 * 0x50]).to_bytes(2, "little")
hdr[0:2] = crc16(hdr[2:0x40]).to_bytes(2, "little")
lg[:0x40] = jl_enc(hdr)
ok, out = review(pkgfile("nofl.fwsc", remark(bytes(lg), "FM-1_920")))
check("no type-0 entry refused", not ok and "no flash.bin" in out)

real = ROOT / "build" / "choralroot.fwsc"
if real.exists():
    ok, out = review(str(real))
    rf = Firmware(real.read_bytes())
    i0 = rf.image0()
    check("build/choralroot.fwsc accepted under rule 2, image0 0x93000 bytes",
          ok and "rule 2" in out and len(i0) == IMG and rf.product == "FM-1_920")
    check("build/choralroot.fwsc flash.bin at 0x400, logical[0x400:0x410] == image0[:16]",
          rf.entries[0]["data_off"] == 0x400 and rf.raw[0x400:0x410] == i0[:16])
    check("build/choralroot.fwsc image0[0x4000:0x4010] is code, not erased", i0[0x4000:0x4010] != b"\xff" * 16)

print("transporter shim: " + ("all ok" if not fails else f"{fails} FAILED"))
sys.exit(1 if fails else 0)
