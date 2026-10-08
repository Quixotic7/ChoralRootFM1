#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""tools/fm1_install.py against a simulated FM-1 (no hardware, no mido).
The fake device is the one in web/test_web.mjs: identity on the handshake,
then "device asks, host answers" reads of the logical image. Run from the repo root:
  python3 tests/install_test.py
Also checks logical_image/product_of against web/fm1pkg.js (needs node and
build/choralroot.fwsc, skipped otherwise)."""
import io
import queue
import shutil
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import fm1_install as I  # noqa: E402

I.DELAY.update(open=0, start=0.01, reply=0, loader=0.01, reboot=0.01, retry=0.02, poll=0.05, hs=0.1, info=0.2,
               idle_check=0.4, idle_write=0.4, wait_loader=2, wait_reboot=2)
failed = 0


def ok(cond, what):
    global failed
    print(f"{what:<64} {'ok' if cond else 'FAIL'}")
    failed += not cond


# ---------------------------------------------------------- simulated FM-1 ---

class FakeLink:
    def __init__(self, dev, gen):
        self.dev, self.gen, self.q, self.closed = dev, gen, queue.Queue(), False

    @property
    def lost(self):
        return self.gen != self.dev.gen or not self.dev.connected

    def send(self, pkt):
        if self.lost:
            return False
        self.dev.sent.append(bytes(pkt))
        self.dev.rx(bytes(pkt))
        return True

    def read(self, timeout):
        try:
            return self.q.get(timeout=max(timeout, 0))
        except queue.Empty:
            return None

    def drain(self):
        while not self.q.empty():
            self.q.get_nowait()

    def close(self):
        self.closed = True


class FakeFM1:
    """a MIDI backend with one FM-1 on it"""

    def __init__(self, image, identity="FM-1_015", name="FM-1", unplug_after=None, after_write="FM-1_900",
                 stall_after=None, bad_addr=None, bk=None):
        self.image, self.unplug_after, self.after_write = image, unplug_after, after_write
        self.bk = bk                              # bk(identity): the firmware's backup side (BackupSide) or None
        self.stall_after, self.bad_addr = stall_after, bad_addr
        self.served = self.bad = self.upgrades = 0
        self.sent, self.links, self.gen, self.lock = [], [], 0, threading.Lock()
        self.boot(identity, name)

    def boot(self, identity, name):
        with self.lock:
            self.gen += 1
            self.identity, self.name, self.connected = identity, name, True
            self.waiting, self.queue = None, []

    def input_names(self):
        return [self.name] if self.connected else []

    output_names = input_names

    def open(self, in_name, out_name):
        if in_name != self.name or not self.connected:
            raise IOError("no such port")
        link = FakeLink(self, self.gen)
        self.links.append(link)
        return link

    def tx(self, pkt):
        for link in self.links:
            if link.gen == self.gen and not link.closed:
                link.q.put(bytes(pkt))

    def rx(self, d):
        if d[:4] == I.BK_HDR:                     # the backup protocol
            side = self.bk and self.bk(self.identity)
            r = side and side.handle(d[4], list(d[5:-1]))
            if r is not None and r is not False:
                self.tx(I.BK_HDR + bytes([d[4], *r, 0xF7]))
            return
        if d == I.HS_QUERY:
            t = self.identity.encode()
            self.tx(b"\xF0" + I.pack7(bytes([0, 0x59, 0x11, 0, 0, 0]) + t + bytes(28 - len(t))) + b"\xF7")
        elif d == I.UPGRADE:
            self.upgrades += 1
            first = self.bad_addr if self.bad_addr is not None else 0
            self.queue = ([(first + k * 512, 512) for k in range(6)] + [(I.FINISH_WRITE, 8)]
                          if self.identity.startswith("ota-")
                          else [(0, 64), (0x40, 160), (0x1000, 512), (I.FINISH_CHECK, 8)])
            self.next()
        elif self.waiting:
            u = I.unpack7(d[1:-1])
            addr, n = self.waiting
            fin = addr >= I.FINISH_CHECK
            want = b"success\0" if fin else self.image[addr:addr + n]
            if u[14:14 + len(want)] != want or len(u) != 15 + len(want):
                self.bad += 1
            self.waiting = None
            self.served += 1
            if self.unplug_after and self.served >= self.unplug_after:
                self.connected = False
                return
            if self.stall_after and self.served >= self.stall_after:
                return
            if addr == I.FINISH_CHECK:
                threading.Timer(0.05, self.boot, ("ota-FM-1_900", "Felucca Update")).start()
            elif addr == I.FINISH_WRITE:
                threading.Timer(0.05, self.boot, (self.after_write, "Felucca")).start()
            else:
                self.next()

    def next(self):
        if not self.queue:
            return
        self.waiting = addr, n = self.queue.pop(0)
        u = bytearray([0, 0x59, 0x30, 0, 0, 0, 0]) + addr.to_bytes(4, "little") + bytes([n & 0xFF, n >> 8, 0])
        u.append(~sum(u[6:14]) & 0xFF)
        self.tx(b"\xF0" + I.pack7(u) + b"\xF7")


class BackupSide:
    """a firmware's backup commands (cr_backup.c / Felucca's editor_backup.c as the protocol says)"""

    def __init__(self, version, ids, objs=(), busy=0):
        self.version, self.ids, self.objs, self.busy = version, ids, dict(objs), busy
        self.log, self.staged, self.restarts = [], None, 0

    def handle(self, cmd, a):
        if cmd == I.BK_INFO:
            return [*self.version.encode(), 0, 0, 0, 0, 0, 0, 0, 0, 0x42, 1, 3]
        if cmd == I.BK_LIST:
            out = [1, 0, len(self.ids)]
            for i in self.ids:
                v = self.objs.get(i, b"")
                out += [i, *I.bk_u32(len(v)), *I.bk_u32(I.bk_crc(v) if v else 0)]
            return out
        if cmd == I.BK_GET:
            i, off, n = a[0], I.bk_r32(a, 1), a[6] | a[7] << 7
            return [i, 0, *I.bk_u32(off), n & 127, n >> 7, *I.bk_pack(self.objs[i][off:off + n])]
        if cmd == I.BK_PUT:
            op, i = a[0], a[1]
            if op == 0:
                if i not in self.ids:
                    return [op, i, 1]
                if self.busy:
                    self.busy -= 1
                    self.log.append(f"busy {i}")
                    return [op, i, 3]
                self.staged = [I.bk_r32(a, 2), I.bk_r32(a, 7), bytearray()]
                return [op, i, 0]
            if op == 1:
                self.staged[2] += I.bk_unpack(a[7:], min(256, self.staged[0] - I.bk_r32(a, 2)))
                return [op, i, 0]
            if op == 2:
                v = bytes(self.staged[2])
                rc = 0 if I.bk_crc(v) == self.staged[1] else 2
                if not rc:
                    self.objs[i] = v
                    self.log.append(i)
                return [op, i, rc]
            return [op, i, 0]
        if 11 <= cmd <= 14:
            if cmd >= 13:
                self.log.append(32 + a[0])
            return [a[0], a[1], a[2], a[3], 0] if cmd == 12 else [a[0], 0]
        if cmd == I.BK_RESTART and self.version.startswith("ChoralRoot"):
            self.restarts += 1
            return [0]
        return None


def fill(n, seed):
    return bytes((i * 31 + seed) & 255 for i in range(n))


def per(n, magic, seed):
    return magic.to_bytes(4, "little") + fill(n - 4, seed)


# ----------------------------------------------------------------- helpers ---

def package(product="FM-1_900", marker=True, size=0x2000):
    raw = bytearray((i * 7) & 0xFF for i in range(size + I.BLOCKS))
    for i in range(I.BLOCKS):
        raw[i * I.BLK + I.KEEP] = (ord(product[i]) + i + 1) & 0xFF if i < len(product) else 0x7D
    raw[0x1800:0x1800 + 16] = I.LOADER_MARK if marker else bytes(16)
    return bytes(raw)


TMP = Path(tempfile.mkdtemp(prefix="felucca-install-"))


def pkgfile(name, raw):
    p = TMP / name
    p.write_bytes(raw)
    return str(p)


def cli(args, dev, answer=True):
    out, err = io.StringIO(), io.StringIO()
    old, sys.stderr = sys.stderr, err
    try:
        rc = I.main(args, backend=dev, out=out, ask=lambda _p: answer)
    finally:
        sys.stderr = old
    return rc, out.getvalue(), err.getvalue()


# ------------------------------------------------------------------- tests ---

# which firmware ChoralRoot is installed over (docs/INSTALL-COMPAT.md): identity + INFO version -> verdict.
# The same cases run through web/fm1ota.js classifyFirmware below (against_js_guard) and in web/test_installer.mjs.
GUARD_CASES = [
    ("FM-1_015", None, "allow", "stock"),                       # the official V15
    ("FM-1_014", None, "allow", "stock"),                       # an older official firmware
    ("FM-1_920", "ChoralRoot 0.12", "allow", "choralroot"),
    ("FM-1_920", "ChoralRoot 1.0-rc1", "allow", "choralroot"),
    ("FM-1_910", "FELUCCA v1.0", "allow", "felucca"),            # Felucca 1.0 (felucca.c FELUCCA_VERSION)
    ("FM-1_910", "FELUCCA v1.0.1", "allow", "felucca"),
    ("FM-1_910", "FELUCCA 1.0", "allow", "felucca"),
    ("FM-1_911", "FELUCCA v1.1-rc1", "allow", "felucca"),
    ("FM-1_90111", "MELODEE v0.11.1", "allow", "melodee"),        # Melodee (a Felucca 1.0 fork)
    ("FM-1_910", "MELODEE v1.0", "allow", "melodee"),
    ("FM-1_909", "FELUCCA 0.9-BETA", "refuse", "felucca-beta"),  # Felucca 0.9-beta (build.py --release 0.9-beta)
    ("FM-1_905", "FELUCCA 0.5 BETA", "refuse", "felucca-beta"),
    ("FM-1_900", "FELUCCA SLOOP 2.2", "refuse", "sloop"),        # Sloop's dev build (ui.c FELUCCA_VERSION)
    ("FM-1_922", "FELUCCA 2.2 BETA", "refuse", "felucca-beta"),  # Sloop --release 2.2 (0.9-era build.py)
    ("FM-1_920", "FELUCCA 2.0 BETA", "refuse", "felucca-beta"),  # Sloop 2.0: ChoralRoot's identity, told apart by INFO
    ("FM-1_000", None, "refuse", "sloop"),                      # Sloop's rescue mode
    ("FM-1_900", None, "refuse", "unknown"),                    # a 9xx firmware without the backup protocol
    ("FM-1_920", None, "refuse", "unknown"),
    ("FM-1_500", None, "refuse", "unknown"),
    ("FM-1_910", "SOMETHING 3.0", "refuse", "unknown"),
    ("ota-FM-1_920", None, "loader", "loader"),                 # update mode: the resume path
]


def guard():
    bad = [(i, v, I.classify_firmware(i, v)[:2]) for i, v, verdict, kind in GUARD_CASES
           if I.classify_firmware(i, v)[:2] != (verdict, kind)]
    ok(not bad, f"classify_firmware: {len(GUARD_CASES)} identity / version cases{'; wrong: ' + repr(bad) if bad else ''}")
    raw = package("FM-1_920")
    image = I.logical_image(raw)
    p = pkgfile("cr_guard.fwsc", raw)

    def over(identity, version, *extra, answer=True):
        side = BackupSide(version, I.FELUCCA_IDS) if version else None
        dev = FakeFM1(image, identity=identity, after_write="FM-1_920", bk=lambda i: side if i == identity else None)
        return (*cli([p, "--yes", *extra], dev, answer), dev)

    rc, out, err, dev = over("FM-1_900", "FELUCCA SLOOP 2.2")
    ok(rc == 8 and dev.upgrades == 0 and "Installing over Sloop is not supported" in err and I.RECOVERY_URL in err
       and "official V15" in err and "--force" in err, "over Sloop: refused (exit 8), nothing written, recovery link")
    rc, out, err, dev = over("FM-1_900", "FELUCCA SLOOP 2.2", "--force")
    ok(rc == 0 and dev.identity == "FM-1_920" and dev.bad == 0 and "--force: installing over Sloop anyway" in out,
       "over Sloop with --force: installed, the override said")
    rc, out, err, dev = over("FM-1_909", "FELUCCA 0.9-BETA")
    ok(rc == 8 and dev.upgrades == 0 and "Felucca beta" in err and "0.9-BETA" in err, "over Felucca 0.9-beta: refused (exit 8)")
    rc, out, err, dev = over("FM-1_000", None)
    ok(rc == 8 and dev.upgrades == 0 and "rescue mode" in err, "over Sloop's rescue mode (FM-1_000): refused")
    rc, out, err, dev = over("FM-1_900", None)
    ok(rc == 8 and dev.upgrades == 0 and "unknown" in err, "over a 9xx firmware that does not answer INFO: refused")
    for ident, ver in (("FM-1_910", "FELUCCA v1.0.1"), ("FM-1_90111", "MELODEE v0.11.1"), ("FM-1_920", "ChoralRoot 0.12"),
                       ("FM-1_015", None)):
        rc, out, err, dev = over(ident, ver)
        ok(rc == 0 and dev.identity == "FM-1_920" and dev.bad == 0 and (not ver or ver in out),
           f"over {ver or 'the stock ' + ident}: installed")
    side = BackupSide("FELUCCA SLOOP 2.2", I.FELUCCA_IDS)
    rc, out, err = cli(["--info"], FakeFM1(image, identity="FM-1_900", bk=lambda _i: side))
    ok(rc == 0 and "FELUCCA SLOOP 2.2" in out and "not installed over Sloop" in out, "--info: version and the verdict")


def against_js_guard():
    if not shutil.which("node"):
        print("classify_firmware vs fm1ota.js: skipped (needs node)")
        return
    import json
    js = ("import { classifyFirmware } from %r; const c = JSON.parse(process.argv[1]);"
          "process.stdout.write(JSON.stringify(c.map(([i, v]) => { const r = classifyFirmware(i, v);"
          " return [r.verdict, r.kind, r.name]; })));") % str(ROOT / "web/fm1ota.js")
    r = subprocess.run(["node", "--input-type=module", "-e", js, json.dumps([[i, v] for i, v, _, _ in GUARD_CASES])],
                       capture_output=True, check=True)
    got = [tuple(x) for x in json.loads(r.stdout)]
    want = [I.classify_firmware(i, v) for i, v, _, _ in GUARD_CASES]
    ok(got == want, "classify_firmware == fm1ota.js classifyFirmware (verdict, kind, name)")
    r = subprocess.run(["node", "--input-type=module", "-e",
                        "import { refusalText } from %r; process.stdout.write(refusalText('Sloop'));" % str(ROOT / "web/fm1ota.js")],
                       capture_output=True, check=True)
    ok(r.stdout.decode() == I.refusal_text("Sloop"), "refusal_text == fm1ota.js refusalText")


def wire():
    data = bytes(range(256)) * 3
    ok(I.unpack7(I.pack7(data))[:len(data)] == data, "pack7 / unpack7 round trip")
    pkt = I.response(0x12345, b"\x01\x02\x03", fl=5)
    u = I.unpack7(pkt[1:-1])
    ok(u[:3] == b"\x00\x59\x30" and int.from_bytes(u[7:11], "little") == 0x12345 and u[14:17] == b"\x01\x02\x03"
       and ~sum(u[6:17]) & 0xFF == u[17], "response: header, address, data, checksum")
    raw = package("FM-1_906")
    ok(I.product_of(raw) == "FM-1_906" and len(I.logical_image(raw)) == len(raw) - 20, "product_of / logical_image (synthetic)")


def installs():
    raw = package()
    image = I.logical_image(raw)
    p = pkgfile("ok.fwsc", raw)

    dev = FakeFM1(image)
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 0 and dev.bad == 0 and dev.upgrades == 2 and dev.served == 11 and "done: the FM-1 runs FM-1_900" in out
       and "100%" in out, f"install: running -> loader -> Felucca ({dev.served} reads)")

    dev = FakeFM1(image, identity="ota-FM-1_900", name="Felucca Update")
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 0 and dev.bad == 0 and dev.upgrades == 1 and "update mode" in out, "install: device already in update mode -> finishes the write")

    dev = FakeFM1(image)
    rc, out, err = cli([p], dev, answer=False)
    ok(rc == 1 and dev.upgrades == 0 and "cancelled" in out, "install: answer no -> nothing sent but the handshake")

    dev = FakeFM1(image, identity="FM-1_905", name="Felucca")
    rc, out, err = cli(["--info"], dev)
    ok(rc == 0 and "FM-1_905" in out and "running" in out and dev.upgrades == 0, "--info: identity of the connected FM-1")

    dev = FakeFM1(image, identity="ota-FM-1_900", name="Felucca Update")
    rc, out, err = cli(["--info"], dev)
    ok(rc == 0 and "update loader" in out, "--info: device in update mode")


def errors():
    raw = package()
    image = I.logical_image(raw)
    p = pkgfile("ok.fwsc", raw)

    rc, out, err = cli([p, "--yes"], FakeFM1(image, name="IAC Driver Bus 1"))
    ok(rc == 3 and "not found" in err and "IAC Driver" in err, "no FM-1 port -> exit 3, lists the MIDI inputs")
    dev = FakeFM1(image)
    rc, out, err = cli([p, "--yes", "--port", "Felucca"], dev)
    ok(rc == 3 and dev.upgrades == 0, "--port that matches nothing -> exit 3")
    dev = FakeFM1(image, name="My Interface")
    rc, out, err = cli(["--info", "--port", "my int"], dev)
    ok(rc == 0 and "FM-1_015" in out, "--port picks a port the default match skips")

    t0 = time.monotonic()
    rc, out, err = cli([p, "--yes"], FakeFM1(image, unplug_after=2))
    ok(rc == 4 and "disconnected" in err and "nothing was written" in err and time.monotonic() - t0 < 3,
       "unplugged in step 1 -> exit 4 'lost', nothing written")
    t0 = time.monotonic()
    rc, out, err = cli([p, "--yes"], FakeFM1(image, identity="ota-FM-1_900", name="Felucca Update", unplug_after=3))
    ok(rc == 4 and "run the install again" in err and time.monotonic() - t0 < 3, "unplugged during the write -> exit 4 at once")
    rc, out, err = cli([p, "--yes"], FakeFM1(image, identity="ota-FM-1_900", name="Felucca Update", stall_after=3))
    ok(rc == 4 and "stopped answering" in err, "loader stops answering -> exit 4 after the idle time")
    rc, out, err = cli([p, "--yes"], FakeFM1(image, identity="ota-FM-1_900", name="Felucca Update", bad_addr=len(image)))
    ok(rc == 4 and "outside the package" in err, "read past the package -> exit 4")

    rc, out, err = cli([p, "--yes"], FakeFM1(image, after_write="FM-1_015"))
    ok(rc == 6 and "reports FM-1_015" in err, "another identity after the restart -> exit 6")
    dev = FakeFM1(image, identity="XY-9_001", name="usb-midi")
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 6 and dev.upgrades == 0, "another model -> exit 6, nothing sent")

    class Stuck(FakeFM1):          # the loader never shows up
        def boot(self, identity, name):
            super().boot(identity, name)
            if identity.startswith("ota-"):
                self.connected = False
    rc, out, err = cli([p, "--yes"], Stuck(image))
    ok(rc == 5 and "loader did not appear" in err, "no loader after step 1 -> exit 5")

    plain = pkgfile("plain.fwsc", package(marker=False))
    dev = FakeFM1(image)
    rc, out, err = cli([plain, "--yes"], dev)
    ok(rc == 2 and "no Felucca update loader" in err and dev.sent == [], "package without the loader marker -> exit 2, no MIDI")
    dev = FakeFM1(I.logical_image(package(marker=False)))
    rc, out, err = cli([plain, "--yes", "--force"], dev)
    ok(rc == 0 and dev.bad == 0, "... installs with --force")
    rc, out, err = cli([pkgfile("short.fwsc", b"\0" * 100), "--yes"], FakeFM1(image))
    ok(rc == 2 and "too short" in err, "not a package -> exit 2")
    rc, out, err = cli([str(TMP / "missing.fwsc"), "--yes"], FakeFM1(image))
    ok(rc == 2, "missing file -> exit 2")


def against_js():
    clean = ROOT / "build/choralroot.fwsc"
    if not shutil.which("node") or not clean.exists():
        print("logical image vs fm1pkg.js: skipped (needs node and build/choralroot.fwsc)")
        return
    js = ("import { logicalImage, productOf } from %r; import { readFileSync } from 'node:fs';"
          "const p = readFileSync(process.argv[1]); process.stderr.write(productOf(p));"
          "process.stdout.write(logicalImage(p));") % str(ROOT / "web/fm1pkg.js")
    r = subprocess.run(["node", "--input-type=module", "-e", js, str(clean)], capture_output=True, check=True)
    raw = clean.read_bytes()
    ok(I.logical_image(raw) == r.stdout and I.product_of(raw) == r.stderr.decode(),
       f"logical_image / product_of == fm1pkg.js ({clean.name}, {I.product_of(raw)})")
    ok(I.LOADER_MARK in raw, f"{clean.name} carries the Felucca loader marker")


def official():
    """#32: back to the official V15 from Felucca, without --force (only the unmodified file)"""
    v15 = ROOT / "FM-1_v15.fwsc"
    if not v15.exists():
        print("official V15 restore: skipped (needs FM-1_v15.fwsc in the repo root)")
        return
    raw = v15.read_bytes()
    dev = FakeFM1(I.logical_image(raw), identity="FM-1_900", name="Felucca", after_write="FM-1_015")
    rc, out, err = cli([str(v15), "--yes"], dev)
    ok(rc == 0 and dev.bad == 0 and "FM-1_015" in out and dev.identity == "FM-1_015",
       "official V15 (FM-1.fwsc) from Felucca: installed without --force, back as FM-1_015")
    bad = bytearray(raw)
    bad[-1] ^= 1
    dev = FakeFM1(I.logical_image(bytes(bad)), identity="FM-1_900", name="Felucca")
    rc, out, err = cli([pkgfile("v15mod.fwsc", bytes(bad)), "--yes"], dev)
    ok(rc == 2 and "official V15" in err and dev.sent == [], "a modified V15 is still refused (no MIDI)")


def backups():
    import json
    I.DELAY.update(bk_busy=0.01)
    cr_objs = {1: per(764, I.PER5, 1), 6: fill(3080, 2), 8: fill(3472, 3), 9: fill(3536, 4), 40: fill(46, 5), 49: fill(3602, 6)}
    cr = BackupSide("ChoralRoot 0.1", I.CR_IDS, cr_objs.items())
    dev = FakeFM1(b"", identity="FM-1_920", bk=lambda _i: cr)
    path = TMP / "cr.json"
    rc, out, err = cli(["--backup", str(path)], dev)
    f = json.loads(path.read_text()) if path.exists() else {}
    ok(rc == 0 and f.get("firmware") == "ChoralRoot 0.1" and [o["id"] for o in f["objects"]] == I.CR_IDS and
       "VA patches" in out and "loop slot 10" in out, "--backup: ChoralRoot's 17 objects to a file")
    rc, out, err = cli(["--backup", str(TMP)], dev)
    ok(rc == 0 and list(TMP.glob("choralroot-backup-*.json")), "--backup DIR: a dated choralroot-backup-YYYYMMDD.json")
    blank = BackupSide("ChoralRoot 0.1", I.CR_IDS, busy=1)
    dev = FakeFM1(b"", identity="FM-1_920", bk=lambda _i: blank)
    rc, out, err = cli(["--restore", str(path), "--yes"], dev)
    ok(rc == 0 and all(blank.objs.get(i) == v for i, v in cr_objs.items()) and blank.log[-1] == 1 and blank.log[0] == "busy 6" and
       blank.restarts == 1, "--restore: every object back (busy retried, the settings last), then RESTART")
    rc, out, err = cli(["--restore", str(path)], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: BackupSide("ChoralRoot 0.1", I.CR_IDS)), answer=False)
    ok(rc == 1 and "cancelled" in out, "--restore: answer no -> nothing written")

    # Felucca's backup restored on ChoralRoot, ChoralRoot's on Felucca (the settings cut back to PER4)
    fel = BackupSide("FELUCCA 1.0", I.FELUCCA_IDS, {0: fill(3584, 7), 1: per(572, I.PER4, 8), 2: fill(3584, 9), 6: fill(3080, 10)}.items())
    fpath = TMP / "fel.json"
    rc, out, err = cli(["--backup", str(fpath)], FakeFM1(b"", identity="FM-1_910", bk=lambda _i: fel))
    onto = BackupSide("ChoralRoot 0.1", I.CR_IDS)
    rc2, out, err = cli(["--restore", str(fpath), "--yes"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: onto))
    ok(rc == 0 and rc2 == 0 and onto.objs[1] == fel.objs[1] and onto.objs[6] == fel.objs[6] and 0 not in onto.objs and
       "current music" in out and "project 1" in out, "Felucca backup -> ChoralRoot: settings and banks; music and projects kept in the file")
    back = BackupSide("FELUCCA 1.0", I.FELUCCA_IDS, {1: per(572, I.PER4, 11)}.items())
    rc, out, err = cli(["--restore", str(path), "--yes"], FakeFM1(b"", identity="FM-1_910", bk=lambda _i: back))
    ok(rc == 0 and back.objs[1] == I.PER4.to_bytes(4, "little") + cr_objs[1][4:572] and back.objs[6] == cr_objs[6] and 9 not in back.objs
       and back.restarts == 0, "ChoralRoot backup -> Felucca: settings cut back to PER4, banks; VA patches and loops kept in the file")

    # install with --backup: the backup first, then the install; --restore after it
    raw = package("FM-1_920")
    image = I.logical_image(raw)
    p = pkgfile("cr.fwsc", raw)
    fel2 = BackupSide("FELUCCA 1.0", I.FELUCCA_IDS, {0: fill(3584, 7), 1: per(572, I.PER4, 8), 6: fill(3080, 12)}.items())
    cr2 = BackupSide("ChoralRoot 0.1", I.CR_IDS)
    dev = FakeFM1(image, identity="FM-1_910", after_write="FM-1_920", bk=lambda i: fel2 if i == "FM-1_910" else cr2 if i == "FM-1_920" else None)
    bpath = TMP / "before.json"
    rc, out, err = cli([p, "--yes", "--backup", str(bpath), "--restore", str(bpath)], dev)
    ok(rc == 0 and bpath.exists() and dev.identity == "FM-1_920" and dev.bad == 0 and cr2.objs.get(6) == fel2.objs[6] and cr2.restarts == 1,
       "PACKAGE --backup F --restore F: backed up, installed, restored onto ChoralRoot")
    dev = FakeFM1(image)                          # the stock firmware: no backup protocol
    rc, out, err = cli([p, "--yes", "--backup", str(TMP / "x.json")], dev)
    ok(rc == 7 and dev.upgrades == 0 and "backup protocol" in err, "PACKAGE --backup on the stock firmware: exit 7, nothing written")
    bad = TMP / "bad.json"
    bad.write_text(json.dumps({**f, "objects": [{**f["objects"][1], "crc": f["objects"][1]["crc"] ^ 1}]}))
    rc, out, err = cli(["--restore", str(bad), "--yes"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: BackupSide("ChoralRoot 0.1", I.CR_IDS)))
    ok(rc == 2 and "damaged" in err, "--restore of a damaged file: exit 2 before any request")


def sound_rec(engine, name, ver=4, np=40, used=0xA5, seed=0):
    """an up_rec_t (docs/SOUNDS.md): used, ver, engine, np, name[12], packed[144] (+64), note[16], flags[16]"""
    vals = bytes(64 + (k * 7 + seed) % 100 for k in range(144)) if ver >= 4 else \
        b"".join(((k * 5 + seed) % 50 - 20).to_bytes(2, "little", signed=True) for k in range(72))
    return bytes([used, ver, engine, np]) + name.encode().ljust(12, b"\0") + vals + fill(32, seed)


def sound_bank(recs):
    b = bytearray(0x31425055.to_bytes(4, "little") + (192).to_bytes(2, "little") + (16).to_bytes(2, "little") + bytes(16 * 192))
    for k, r in recs.items():
        b[8 + k * 192:8 + (k + 1) * 192] = r
    return bytes(b)


def sound_store(kind, half, blobs):
    """the VA store (half 0) or an FM6 / CZ-1 half with {index: blob}"""
    size = {"va": 110, "fm6": 128, "cz": 144}[kind]
    used = sum(1 << k for k in blobs)
    if kind == "va":
        head = (0x31534156).to_bytes(4, "little") + bytes([3, 0, 32, 0]) + used.to_bytes(4, "little") + bytes([110, 0, 0, 0])
        n = 32
    else:
        head = ((0x55364D46 if kind == "fm6" else 0x55315A43).to_bytes(4, "little") + bytes([1, 0, 16, 0, 16 * half, 0, size, 0])
                + used.to_bytes(4, "little"))
        n = 16
    b = bytearray(head + bytes(n * size))
    for k, v in blobs.items():
        b[16 + k * size:16 + (k + 1) * size] = v
    return bytes(b)


def va_blob(seed):
    return bytes([0x56, 3]) + fill(108, seed)


def fm6_blob(seed):
    b = bytearray(fill(128, seed))
    b[112:114] = b"\x46\x01"
    return bytes(b)


def sounds():
    import base64
    import json
    I.DELAY.update(bk_busy=0.01)
    pad, tine, long_, warm, bell = (sound_rec(13, "MY PAD", seed=1), sound_rec(12, "TINE 2", seed=2),
                                    sound_rec(14, "ABCDEFGHIJKL", seed=3), sound_rec(0, "WARM", seed=5),
                                    sound_rec(2, "BELL", ver=3, np=30, seed=6))
    broken = sound_rec(13, "BROKEN", np=200, seed=4)              # np > 144: up_valid fails -> empty
    vpad, vstale3, vstale6, f6stale3, f6stale20, czlong = (va_blob(1), va_blob(9), va_blob(10), fm6_blob(11),
                                                           fm6_blob(12), fill(144, 13))

    def objects():
        return {6: sound_bank({0: pad, 1: tine, 2: long_, 3: broken, 4: warm}), 7: sound_bank({0: bell}),
                9: sound_store("va", 0, {0: vpad, 2: vstale3, 5: vstale6}),
                10: sound_store("fm6", 0, {2: f6stale3}), 11: sound_store("fm6", 1, {3: f6stale20}),
                12: sound_store("cz", 0, {2: czlong}), 13: b""}

    def device(busy=0, objs=None):
        side = BackupSide("ChoralRoot 0.14", I.CR_IDS, (objs or objects()).items(), busy=busy)
        return FakeFM1(b"", identity="FM-1_920", bk=lambda _i: side), side

    def rec_of(side, slot):
        b = side.objs[6 + (slot - 1) // 16]
        return b[8 + (slot - 1) % 16 * 192:8 + ((slot - 1) % 16 + 1) * 192]

    def usage(args):
        try:
            return cli(args, FakeFM1(b"", identity="FM-1_920"))[0]
        except SystemExit as e:
            return e.code

    dev, side = device()
    rc, out, err = cli(["--sounds"], dev)
    lines = {ln[:3]: ln for ln in out.splitlines() if ln[:1] == "U"}
    ok(rc == 0 and len(lines) == 32 and lines["U01"].split() == ["U01", "MY", "PAD", "VA", "patch", "+"] and
       lines["U02"].split() == ["U02", "TINE", "2", "FM6", "no", "patch", "+"] and
       lines["U03"].split() == ["U03", "ABCDEFGHIJKL", "CZ-1", "patch", "+"] and lines["U04"] == "U04  (empty)" and
       lines["U05"].split() == ["U05", "WARM", "ANALOG", "+"] and lines["U06"] == "U06  (empty)" and
       lines["U17"].split() == ["U17", "BELL", "PHASE", "+"] and lines["U20"] == "U20  (empty)" and
       len({ln.index(w) for ln, w in ((lines["U01"], "VA"), (lines["U02"], "FM6"), (lines["U03"], "CZ-1"))}) == 1 and
       not any(isinstance(x, int) for x in side.log), "--sounds: 32 slots, names, engines, patch / no patch, invalid -> (empty)")

    rc, out, err = cli(["--export-sound", "1", str(TMP)], dev)
    f1 = TMP / "choralroot-sound-U01-MY_PAD.json"
    snd = I.read_sound_file(f1.read_text()) if f1.exists() else None
    ok(rc == 0 and snd and snd[0] == pad and snd[1] == "MY PAD" and snd[2] == 13 and snd[3] == ("va", vpad),
       "--export-sound 1 DIR: choralroot-sound-U01-MY_PAD.json, record and VA patch")
    f3 = TMP / "long.json"
    rc, out, err = cli(["--export-sound", "3", str(f3)], dev)
    snd3 = I.read_sound_file(f3.read_text()) if f3.exists() else None
    ok(rc == 0 and snd3 and snd3[0] == long_ and snd3[3] == ("cz", czlong) and json.loads(f3.read_text())["engineName"] == "CZ-1",
       "--export-sound 3 FILE: a CZ-1 sound with a 12-character name")
    f2 = TMP / "tine.json"
    rc, out, err = cli(["--export-sound", "2", str(f2)], dev)
    ok(rc == 0 and json.loads(f2.read_text())["patch"] is None, "--export-sound: an FM6 sound without its blob: patch null")
    rc, out, err = cli(["--export-sound", "4", str(TMP / "x.json")], dev)
    ok(rc != 0 and "empty" in err and not (TMP / "x.json").exists(), "--export-sound of an empty slot: an error, no file")

    dev, side = device()
    rc, out, err = cli(["--import-sound", "8", str(f1)], dev, answer=False)
    ok(rc == 0 and side.log == [9, 6] and rec_of(side, 8) == pad and I.parse_sound_objects(side.objs)[7]["blob"] == vpad
       and "U08  MY PAD" in out, "--import-sound into an empty slot: the VA store, then the bank (no question); re-read shows it")
    rc, out, err = cli(["--sounds"], dev)
    ok("U08  MY PAD" in out and "U01  MY PAD" in out, "--sounds after the import lists U08")

    dev, side = device()
    rc, out, err = cli(["--import-sound", "5", str(f1)], dev, answer=False)
    ok(rc == 1 and "cancelled" in out and not any(isinstance(x, int) for x in side.log), "--import-sound over a used slot, answer no: nothing written")
    rc, out, err = cli(["--import-sound", "5", str(f1), "--yes"], dev, answer=False)
    ok(rc == 0 and side.log == [9, 6] and rec_of(side, 5) == pad, "--import-sound over a used slot with --yes: written")
    rc, out, err = cli(["--import-sound", "3", str(f1), "--yes"], dev)
    t = I.parse_sound_objects(side.objs)
    ok(rc == 0 and side.log[2:] == [9, 10, 12, 6] and t[2]["engineName"] == "VA" and t[2]["blob"] == vpad and
       side.objs[12][16 + 2 * 144:16 + 3 * 144] == bytes(144) and side.objs[10][16 + 2 * 128:16 + 3 * 128] == bytes(128),
       "--import-sound VA over a CZ-1 sound: its CZ-1 tone and a stale FM6 blob cleared, the bank last")

    renamed = TMP / "renamed.json"
    renamed.write_text(json.dumps({**json.loads(f1.read_text()), "name": "Night Pad 2"}))
    dev, side = device()
    rc, out, err = cli(["--import-sound", "9", str(renamed)], dev)
    ok(rc == 0 and rec_of(side, 9)[4:16] == b"Night Pad 2\0" and rec_of(side, 9)[16:] == pad[16:] and "Night Pad 2" in out,
       "--import-sound: the JSON's name renames the sound")

    dev, side = device()
    rc, out, err = cli(["--rename-sound", "17", "CHIME"], dev)
    ok(rc == 0 and side.log == [7] and rec_of(side, 17) == bell[:4] + b"CHIME".ljust(12, b"\0") + bell[16:] and "U17  CHIME" in out,
       "--rename-sound: only the bank, only the name")
    rc, out, err = cli(["--rename-sound", "4", "X"], dev)
    ok(rc != 0 and "empty" in err and side.log == [7], "--rename-sound of an empty slot: an error, nothing written")

    dev, side = device()
    rc, out, err = cli(["--delete-sound", "3"], dev, answer=False)
    ok(rc == 1 and not any(isinstance(x, int) for x in side.log), "--delete-sound, answer no: nothing written")
    rc, out, err = cli(["--delete-sound", "3", "--yes"], dev)
    st = side.objs
    ok(rc == 0 and side.log == [9, 10, 12, 6] and rec_of(side, 3) == bytes(192) and
       not (int.from_bytes(st[9][8:12], "little") >> 2 & 1) and st[9][16 + 220:16 + 330] == bytes(110) and
       not (int.from_bytes(st[10][12:16], "little") >> 2 & 1) and st[10][16 + 256:16 + 384] == bytes(128) and
       not (int.from_bytes(st[12][12:16], "little") >> 2 & 1) and st[12][16 + 288:16 + 432] == bytes(144) and
       st[9][16:126] == vpad and "U03  (empty)" in out, "--delete-sound: record zeroed, the three stores' slot cleared, the bank last")

    dev, side = device(busy=1)
    rc, out, err = cli(["--import-sound", "8", str(f1)], dev)
    ok(rc == 0 and side.log == ["busy 9", 9, 6], "a busy FM-1 (the loop plays): the write is retried")

    empty = {6: b"", 7: b"", 9: b"", 10: b"", 11: b"", 12: b"", 13: b""}
    dev, side = device(objs=empty)
    rc, out, err = cli(["--sounds"], dev)
    ok(rc == 0 and out.count("(empty)") == 32, "--sounds on an FM-1 that never saved a sound: 32 empty slots")
    rc, out, err = cli(["--import-sound", "20", str(f3)], dev)
    t = I.parse_sound_objects(side.objs)
    ok(rc == 0 and side.log == [13, 7] and t[19]["name"] == "ABCDEFGHIJKL" and t[19]["blob"] == czlong and
       len(side.objs[13]) == 2320 and len(side.objs[7]) == 3080, "--import-sound into empty objects: the CZ-1 half and the bank created")

    # bad sound files: exit 2 before any request
    bk = TMP / "a-backup.json"
    bk.write_text(json.dumps({"format": "felucca-backup", "version": 1, "objects": []}))
    dmg = TMP / "damaged.json"
    dmg.write_text(json.dumps({**json.loads(f1.read_text()), "record": "%%%" + json.loads(f1.read_text())["record"][3:]}))
    kind = TMP / "kind.json"
    kind.write_text(json.dumps({**json.loads(f1.read_text()), "patch": {"kind": "fm6", "data": json.loads(f1.read_text())["patch"]["data"]}}))
    for path, what, word in ((bk, "a felucca-backup", "whole backup"), (dmg, "damaged base64", "base64"),
                             (kind, "a wrong patch kind", "fm6")):
        dev, side = device()
        rc, out, err = cli(["--import-sound", "8", str(path), "--yes"], dev)
        ok(rc == 2 and word in err and dev.sent == [], f"--import-sound of {what}: exit 2, nothing sent")
    base = json.loads(f1.read_text())
    bad = []
    cases = [({"record": base["record"][:-8]}, "bytes, not 192"),
             ({"record": base64.b64encode(broken).decode()}, "not a valid user sound"),
             ({"engine": 12}, "engine"), ({"name": "THIRTEEN CHAR"}, "1 to 12"), ({"name": "café"}, "ASCII"),
             ({"patch": {"kind": "va", "data": base["patch"]["data"][:-4]}}, "bytes, not 110"),
             ({"patch": {"kind": "va", "data": base64.b64encode(b"X" + vpad[1:]).decode()}}, "not a VA patch"),
             ({"format": "something"}, "not a ChoralRoot sound file")]
    for ch, word in cases:
        try:
            I.read_sound_file({**base, **ch})
            bad.append((ch, "accepted"))
        except I.InstallError as e:
            if e.code != "badsound" or word not in str(e):
                bad.append((ch, str(e)))
    fm6 = {**json.loads(f2.read_text()), "patch": {"kind": "fm6", "data": base64.b64encode(fill(128, 1)).decode()}}
    try:
        I.read_sound_file(fm6)
        bad.append(("fm6 magic", "accepted"))
    except I.InstallError as e:
        if "FM6" not in str(e):
            bad.append(("fm6 magic", str(e)))
    ok(not bad and I.read_sound_file({**base, "name": None})[1] == "MY PAD",
       f"read_sound_file: each malformed case refused with its message{'; wrong: ' + repr(bad) if bad else ''}")

    # QUAD (the "quad" patch kind, object 22): tests/sound_templates.c's fixture, QUAD's EP as the firmware packs it
    qb = base64.b64decode("UQMBAwMDGgwAImRkZGQAThRCADQAPgABAQEAAQFkKB4AQAAofwABQAAAAH8AYCg+SFADQAAAAABAUANAAAAAAEBQA0AAAAAAQBAeAAAAAAA=")
    qrec = sound_rec(15, "MY QUAD", seed=7)
    fq = TMP / "quad.json"
    fq.write_text(json.dumps({"format": "choralroot-sound", "version": 1, "slot": 10, "name": "MY QUAD", "engine": 15,
                              "record": base64.b64encode(qrec).decode(), "patch": {"kind": "quad", "data": base64.b64encode(qb).decode()}}))
    dev, side = device(objs=dict(empty))
    rc, out, err = cli(["--import-sound", "10", str(fq)], dev)
    q = side.objs.get(22, b"")
    ok(rc == 0 and len(qb) == 80 and side.log == [22, 6] and len(q) == 2576 and q[:4] == b"QUDS" and q[4:8] == bytes([1, 0, 32, 0])
       and int.from_bytes(q[8:12], "little") == 1 << 9 and q[12:16] == bytes([80, 0, 0, 0]) and q[16 + 9 * 80:16 + 10 * 80] == qb
       and "U10  MY QUAD" in out and "FM TONE" in out, "--import-sound of a QUAD sound: the QUAD store (22) created, then the bank")
    rc, out, err = cli(["--export-sound", "10", str(TMP / "quad2.json")], dev)
    e = json.loads((TMP / "quad2.json").read_text()) if (TMP / "quad2.json").exists() else {}
    ok(rc == 0 and e.get("engineName") == "FM TONE" and e.get("patch", {}).get("kind") == "quad" and
       base64.b64decode(e["patch"]["data"]) == qb, "--export-sound of a QUAD sound: patch kind quad, its 80 bytes")
    rc, out, err = cli(["--delete-sound", "10", "--yes"], dev)
    ok(rc == 0 and side.log[2:] == [22, 6] and not int.from_bytes(side.objs[22][8:12], "little"), "--delete-sound of a QUAD sound: its bit cleared, the bank last")
    badq = []
    for data, word in ((b"R" + qb[1:], "FM TONE"), (qb[:1] + b"\x04" + qb[2:], "FM TONE"), (qb[:79], "bytes, not 80")):
        try:
            I.read_sound_file({**json.loads(fq.read_text()), "patch": {"kind": "quad", "data": base64.b64encode(data).decode()}})
            badq.append((word, "accepted"))
        except I.InstallError as ex:
            if word not in str(ex):
                badq.append((word, str(ex)))
    ok(not badq, f"read_sound_file: a QUAD blob's magic / version / length refused{'; wrong: ' + repr(badq) if badq else ''}")
    qv1 = base64.b64decode("UQEBAwMjGgoAP2RkZGQAUBhIAC4ALAABAQEAAQFkACgAQAAofwAAQAAAAH8AYCg+SFADAAAAAABAUAMAAAAAAEBQAwAAAAAAQAAAAAAAAAA=")   # a version 1 blob (a 0.14 dev build's EP)
    ok(qb[1] == 3 and all(I.read_sound_file({**json.loads(fq.read_text()), "patch": {"kind": "quad", "data": base64.b64encode(d).decode()}})[3][1][1] == d[1]
                          for d in (qv1, qb[:1] + b"\x02" + qb[2:], qb)), "read_sound_file: QUAD blob versions 1..3 accepted (the fixture is version 3)")

    fel = BackupSide("FELUCCA 1.0", I.FELUCCA_IDS, {6: sound_bank({0: pad})}.items())
    dev = FakeFM1(b"", identity="FM-1_910", bk=lambda _i: fel)
    rc, out, err = cli(["--sounds"], dev)
    ok(rc == 7 and "ChoralRoot" in err and "FELUCCA 1.0" in err, "--sounds on Felucca: exit 7, needs ChoralRoot")

    ok(usage(["--delete-sound", "33"]) == 2 and usage(["--export-sound", "0", "x.json"]) == 2 and
       usage(["--rename-sound", "a", "X"]) == 2, "N outside 1..32: a usage error (exit 2)")
    ok(usage(["--sounds", pkgfile("s.fwsc", package())]) == 2 and usage(["--sounds", "--backup", "x.json"]) == 2 and
       usage(["--sounds", "--delete-sound", "1"]) == 2 and usage(["--rename-sound", "1", "THIRTEEN CHAR"]) == 2,
       "--sounds with a package / --backup / another sound option, a bad new name: usage errors")


# tests/sound_templates.c's fixtures (factory F1 TINE EP as the blob and the VCED, the packed function settings of
# that blob: FM6_FNDEF packed, Casio's A-1 BRASS 1).
SYX_FIX = {
    "fm6_blob": "XygePGNQACeAAAA4DDQAXx4UPGNaAKeAgAAwhAIA4b6ovGMAACeAgAA7xJwAXxSUsl+AACcAAIAI2oKAXzKjY0sAACcAALscOoKAYBlDY0sAACcAgLsIYgKA4+PjMrIyMgSiIQCAKRjUTkUgRVCgIEYBMwBgHAAAAEAAAAAAAAA=",
    "fm6_vced": "XygePGNQAAAnAAAAAAAAAzQAAQAHXx4UPGNaAAAnAAAAAAAAAU4AAQAGYT4oPGM8AAAnAAAAAAMABkQADgAHXxQUMmNfAAAnAAAAAAIAAloAAQAIXzIjTmNLAAAnAAAAAAMABzoAAQAHYBkZQ2NLAAAnAAAAAAMAAmIAAQAHY2NjYzIyMjIEAwEiIQAAAQQCGFRJTkUgRVAgICA=",
    "fm6_fn": "MwBgHAAAAEA=",
    "cz_tone": "CgEUAAgAAAAy4AkAAQCgIAlfAADjYn/o/sxrwgA8ADwAPAA8ALNif7heNOCnAEQARABEAEQAQV0h1wBAAEAAQABAAEAAQACgAAlfAADjYn/o/sxrwgA8ADwAPAA8ALNif7heNOCnAEQARABEAEQA8V0h1wBAAEAAQABAAEAAQAAgICAgQlJBU1MgMSAgICAg",
}


def syx():
    import base64
    F = {k: base64.b64decode(v) for k, v in SYX_FIX.items()}
    blob, vced, tone = F["fm6_blob"], F["fm6_vced"], F["cz_tone"]

    def refused(data, word):
        try:
            I.parse_syx(data)
            return False
        except I.InstallError as e:
            return e.code == "badsound" and word in str(e)

    ok(I.FM6_FN_DEFAULT == F["fm6_fn"] == bytes.fromhex("3300601c00000040") and blob[114:122] == F["fm6_fn"],
       "FM6_FN_DEFAULT: FM6_FNDEF packed, the fixture's")
    ok(I.fm6_blob_to_vced(blob) == vced, "fm6_blob_to_vced: the F1 blob -> the fixture VCED")
    nb = I.vced_to_fm6_blob(vced)
    ok(nb == blob,
       "vced_to_fm6_blob: the voice as the F1 blob, 'F' 1, the function defaults, zeros")
    wild = bytearray(vced)
    wild[0], wild[134], wild[144], wild[145] = 120, 40, 60, 5
    wb = I.fm6_blob_to_vced(I.vced_to_fm6_blob(bytes(wild)))
    ok(wb[0] == 99 and wb[134] == 31 and wb[144] == 48 and wb[145] == 32, "vced_to_fm6_blob clamps as the firmware (rate, ALG, TRNSP, name)")

    s = I.fm6_vced_syx(vced)
    ok(len(s) == 163 and s[:6] == bytes([0xF0, 0x43, 0, 0, 1, 0x1B]) and s[-1] == 0xF7 and (sum(s[6:162]) & 0x7F) == 0
       and I.parse_syx(s) == ("fm6", [vced]), "fm6_vced_syx: 163 bytes, the checksum, parsed back")
    ch5 = s[:2] + b"\x05" + s[3:]
    ok(I.parse_syx(ch5) == ("fm6", [vced]) and I.parse_syx(s + b"\r\n" + s) == ("fm6", [vced, vced]),
       "parse_syx: any channel; two frames in one file")
    vmem = I._fm6_unpack7(blob[:112])
    seconds = [bytearray(vmem) for _k in range(32)]
    seconds[2][118:128] = b"THIRD ONE "
    body = b"".join(bytes(x) for x in seconds)
    bank = bytes([0xF0, 0x43, 0, 9, 0x20, 0]) + body + bytes([-sum(body) & 0x7F, 0xF7])
    k, vs = I.parse_syx(bank)
    ok(k == "fm6" and len(vs) == 32 and vs[0] == vced and vs[2][145:155] == b"THIRD ONE " and len(bank) == 4104,
       "parse_syx: a DX7 32-voice bank")
    ok(I.parse_syx(vced) == ("fm6", [vced]) and len(I.parse_syx(body)[1]) == 32 and I.parse_syx(tone) == ("cz", [tone]),
       "parse_syx: raw 155 / 4096 / 144-byte files")
    c = I.cz_tone_syx(tone)
    ok(len(c) == 295 and c[:6] == bytes([0xF0, 0x44, 0, 0, 0x70, 0x30]) and c[6] == tone[0] & 15 and c[7] == tone[0] >> 4
       and c[-1] == 0xF7 and I.parse_syx(c) == ("cz", [tone]) and I.parse_syx(c[:4] + b"\x73" + c[5:]) == ("cz", [tone]),
       "cz_tone_syx: 295 bytes, the low nibble first; parsed back (any channel)")
    cz101 = bytes([0xF0, 0x44, 0, 0, 0x70, 0x30]) + bytes(256) + b"\xF7"
    badsum = s[:161] + bytes([(s[161] + 1) & 0x7F]) + s[162:]
    ok(refused(cz101, "CZ-101") and refused(badsum, "checksum") and refused(b"\xF0\x7E\x00\xF7", "no DX7") and
       refused(bytes(100), "not a .syx") and refused(s + c, "mixes") and refused(s[:-1], "F7"),
       "parse_syx refuses a CZ-101 tone, a bad checksum, a foreign / raw-unknown / mixed / unfinished file")

    rf, nf, ef, pf = I.sound_from_syx("fm6", vced)
    rc_, nc, ec, pc = I.sound_from_syx("cz", tone)
    ok(I.sound_record_valid(rf) and ef == 12 and rf[2] == 12 and nf == "TINE EP" and rf[4:16] == b"TINE EP".ljust(12, b"\0")
       and pf == ("fm6", nb) and I.sound_record_valid(rc_) and ec == 14 and nc == "BRASS 1" and pc == ("cz", tone),
       "sound_from_syx: template records (engines 12 / 14), the names TINE EP / BRASS 1")
    blank = bytearray(vced)
    blank[145:155] = b"\x01" * 10
    ok(I.sound_from_syx("fm6", bytes(blank))[1] == "FM6 VOICE" and I.syx_file_name(5, "MY PAD") == "choralroot-sound-U05-MY_PAD.syx",
       "an empty name -> FM6 VOICE; syx_file_name")

    # the CLI against a simulated ChoralRoot
    objs = {6: sound_bank({0: sound_rec(12, "TINE 2", seed=2), 1: sound_rec(14, "HORN", seed=3),
                           2: sound_rec(13, "MY PAD", seed=1), 3: sound_rec(12, "NO BLOB", seed=4)}),
            7: sound_bank({}), 9: sound_store("va", 0, {2: va_blob(1)}), 10: sound_store("fm6", 0, {0: blob}),
            11: b"", 12: sound_store("cz", 0, {1: tone}), 13: b""}

    def device():
        side = BackupSide("ChoralRoot 0.14", I.CR_IDS, objs.items())
        return FakeFM1(b"", identity="FM-1_920", bk=lambda _i: side), side

    dev, side = device()
    rc, out, err = cli(["--export-syx", "1", str(TMP)], dev)
    f1 = TMP / "choralroot-sound-U01-TINE_2.syx"
    ok(rc == 0 and f1.exists() and f1.read_bytes() == s, "--export-syx of an FM6 slot: 163 bytes, the voice")
    fc = TMP / "horn.syx"
    rc, out, err = cli(["--export-syx", "2", str(fc)], dev)
    ok(rc == 0 and fc.exists() and fc.read_bytes() == c, "--export-syx of a CZ-1 slot: 295 bytes, the tone")
    rc3, _o, err3 = cli(["--export-syx", "3", str(TMP / "va.syx")], dev)
    rc4, _o, err4 = cli(["--export-syx", "4", str(TMP / "nb.syx")], dev)
    rc5, _o, err5 = cli(["--export-syx", "5", str(TMP / "e.syx")], dev)
    ok(rc3 == 1 and "VA" in err3 and rc4 == 1 and "no patch" in err4 and rc5 == 1 and "empty" in err5 and
       not any((TMP / n).exists() for n in ("va.syx", "nb.syx", "e.syx")),
       "--export-syx of a VA slot / an FM6 slot without its blob / an empty slot: exit 1, no file")

    bankf = TMP / "bank.syx"
    bankf.write_bytes(bank)
    dev, side = device()
    rc, out, err = cli(["--import-syx", "20", str(bankf), "--voice", "3"], dev, answer=False)
    t = I.parse_sound_objects(side.objs)
    ok(rc == 0 and side.log == [11, 7] and t[19]["name"] == "THIRD ONE" and t[19]["engine"] == 12 and
       t[19]["blob"] == I.vced_to_fm6_blob(vs[2]) and "U20  THIRD ONE" in out and "patch" in out.splitlines()[-1],
       "--import-syx --voice 3 of a bank into an empty slot: the FM6 half, then the bank; re-read")
    dev, side = device()
    rc, out, err = cli(["--import-syx", "6", str(fc)], dev, answer=False)
    t = I.parse_sound_objects(side.objs)
    ok(rc == 0 and side.log == [12, 6] and t[5]["name"] == "BRASS 1" and t[5]["engineName"] == "CZ-1" and
       out.splitlines()[-1].split() == ["U06", "BRASS", "1", "CZ-1", "patch", "+"] and t[5]["added"] and
       t[5]["record"][175] == 0xA6 and t[5]["record"][191] == 0,
       "--import-syx of a CZ-1 tone: the CZ-1 store, then the bank; marked as an added preset")
    rc, out, err = cli(["--import-syx", "3", str(f1)], dev, answer=False)
    ok(rc == 1 and "cancelled" in out and not any(isinstance(x, int) for x in side.log[2:]),
       "--import-syx over a used slot, answer no: nothing written")
    rc, out, err = cli(["--import-syx", "3", str(f1), "--yes"], dev, answer=False)
    t = I.parse_sound_objects(side.objs)
    ok(rc == 0 and side.log[2:] == [9, 10, 6] and t[2]["engineName"] == "FM6" and t[2]["name"] == "TINE EP",
       "--import-syx FM6 over a VA sound with --yes: the VA blob cleared, the FM6 blob set, the bank last")

    def code(args):
        try:
            return cli(args, device()[0])
        except SystemExit as e:
            return e.code, "", ""
    one = TMP / "one.syx"
    one.write_bytes(s)
    bad = TMP / "bad.syx"
    bad.write_bytes(cz101)
    r1, r2, r3 = code(["--import-syx", "1", str(one), "--voice", "2"]), code(["--import-syx", "1", str(bad)]), \
        code(["--import-syx", "1", str(bankf), "--voice", "33"])
    ok(r1[0] == 2 and "1 voice" in r1[2] and r2[0] == 2 and "CZ-101" in r2[2] and r3[0] == 2 and
       code(["--voice", "2", "--sounds"])[0] == 2 and code(["--import-syx", "1", str(one), "--export-syx", "2", "x"])[0] == 2
       and code(["--export-syx", "33", "x"])[0] == 2, "--voice beyond the file / a CZ-101 file / bad options: exit 2")


def bindings():
    """docs/SOUNDS.md "The binding": note[15] (byte 175) = 0xA6, flags[15] (byte 191) = factory index + 1 / 0 added"""
    import json
    import re

    def bind(r, f):
        r = bytearray(r)
        r[175], r[191] = 0xA6, f
        return bytes(r)
    fp, ff = I.FACTORY_PRESETS, I.FACTORY_FIRST
    ok(fp["12"][1] == "FM BELL" and len(fp["12"]) == 25 and fp["14"][0] == "INIT TONE" and len(fp["14"]) == 65 and
       ff["14"] == 1 and ff["12"] == 0 and not any(e in fp for e in ("0", "1", "4", "8", "10")) and I.pool_of(1) == 12 and
       I.pool_of(0) == 13 and I.pool_of(8) == 13 and I.pool_of(14) == 14,
       "binding: the factory table (the firmware's, --check), retired engines' pools (ANALOG's: the VA's)")
    js = (ROOT / "web" / "fm1sounds.js").read_text()
    jf = re.search(r"^export const FACTORY_PRESETS = (.*);$", js, re.M)
    js1 = re.search(r"^export const FACTORY_FIRST = (.*);$", js, re.M)
    ok(jf and js1 and json.loads(jf.group(1)) == fp and json.loads(js1.group(1)) == ff,
       "binding: fm1_install.py and fm1sounds.js embed the same factory table")
    unmarked = bytearray(sound_rec(12, "OLD", seed=4))
    unmarked[191] = 4
    recs = {0: bind(sound_rec(12, "MY BELL", seed=2), 2),        # U01 over FM6 02 FM BELL
            1: bind(sound_rec(12, "ADDED", seed=3), 0),          # U02 marked, bound to nothing: added
            2: bytes(unmarked),                                  # U03 no mark: added
            3: bind(sound_rec(12, "BELL TOO", seed=5), 2),       # U04 FM BELL again: the first wins, added
            4: bind(sound_rec(14, "MY BRASS", seed=6), 2),       # U05 CZ-1 over BRASS 1 = pool 01
            5: bind(sound_rec(14, "OVER INIT", seed=7), 1),      # U06 INIT TONE (the pool's INIT): added
            6: bind(sound_rec(12, "TOO FAR", seed=8), 26),       # U07 past the 25 FM6 presets: added
            7: bind(sound_rec(1, "DIGI", seed=9), 3),            # U08 DIGITAL in FM6's pool, over FM BASS
            8: bind(sound_rec(0, "GRID", ver=5, seed=10), 1)}    # U09 a drum grid record: never bound
    objs = {6: sound_bank(recs), 7: b"", 9: b"", 10: b"", 11: b"", 12: b"", 13: b""}
    t = I.parse_sound_objects(objs)
    b0 = t[0]["bound"]
    ok(b0 and b0["index"] == 1 and b0["pos"] == 2 and b0["label"] == "FM6 02" and b0["name"] == "FM BELL" and not t[0]["added"],
       "binding: a bound FM6 record -> over FM6 02 FM BELL")
    ok(all(t[k]["bound"] is None and t[k]["added"] for k in (1, 2, 3, 5, 6, 8)),
       "binding: marked 0, unmarked, a second binding, INIT TONE, past the presets, a grid record -> added")
    ok(t[4]["bound"]["label"] == "CZ-1 01" and t[4]["bound"]["name"] == "BRASS 1" and t[7]["bound"]["label"] == "FM6 03" and
       t[7]["bound"]["name"] == "FM BASS" and not t[20]["added"] and t[20]["bound"] is None,
       "binding: CZ-1 over BRASS 1 = pool 01, DIGITAL in FM6's pool; empty slots neither")
    ln = I.sound_line(t[0]).split()
    ok(ln == ["U01", "MY", "BELL", "FM6", "no", "patch", "over", "FM6", "02", "FM", "BELL"] and
       I.sound_line(t[1]).split()[-1] == "+" and I.sound_line(t[8]).split() == ["U09", "GRID", "ANALOG", "+"],
       "binding: --sounds lines: 'over FM6 02 FM BELL' / '+'")
    e1, e2 = I.export_sound(objs, 1, "ChoralRoot 0.14"), I.export_sound(objs, 4, "ChoralRoot 0.14")
    ok(e1["binding"] == {"overwrites": 2, "name": "FM BELL"} and e2["binding"] is None and
       json.loads(json.dumps(e1))["binding"]["overwrites"] == 2, "binding: the sound file's binding {overwrites, name} / null")
    without = {k: v for k, v in e1.items() if k != "binding"}
    stale = {**e1, "binding": {"overwrites": 9, "name": "NOPE"}}
    ok(I.read_sound_file(without)[0] == recs[0] and I.read_sound_file(json.dumps(stale))[0] == recs[0] and
       I.read_sound_file(e1)[0] == recs[0], "binding: files with, without or with a stale binding read the record as it is")
    new = dict(objs)
    for i, b in I.import_sound(objs, 12, I.read_sound_file(e1)):
        new[i] = b
    t2 = I.parse_sound_objects(new)
    ok(t2[11]["record"] == recs[0] and t2[11]["bound"] is None and t2[11]["added"] and t2[0]["bound"]["name"] == "FM BELL",
       "binding: import keeps the bytes; a later slot binding FM BELL again is added (the first wins)")
    new = {}
    for i, b in I.import_sound({}, 12, I.read_sound_file(e1)):
        new[i] = b
    ok(I.parse_sound_objects(new)[11]["bound"]["label"] == "FM6 02", "binding: imported alone, U12 is over FM6 02")
    rec, _n, _e, _p = I.sound_from_syx("cz", bytes(144))
    ok(rec[175] == 0xA6 and rec[191] == 0 and I.sound_record_valid(rec) and
       I.sound_bindings([rec]) == [None], "binding: a .syx import's record: the mark, bound to nothing (added)")
    ok("added" in I.__doc__ and "over FM6 02 FM BELL" in I.__doc__, "binding: --help says what --sounds shows")


wire()
backups()
sounds()
syx()
bindings()
installs()
errors()
against_js()
guard()
against_js_guard()
official()
shutil.rmtree(TMP, ignore_errors=True)
print(f"INSTALL TESTS FAILED ({failed})" if failed else "install tests passed")
sys.exit(1 if failed else 0)
