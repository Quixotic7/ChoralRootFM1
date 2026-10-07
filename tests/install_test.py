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

I.DELAY.update(open=0, start=0.01, reply=0, loader=0.01, reboot=0.01, retry=0.02, poll=0.05, hs=0.1,
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


wire()
backups()
installs()
errors()
against_js()
official()
shutil.rmtree(TMP, ignore_errors=True)
print(f"INSTALL TESTS FAILED ({failed})" if failed else "install tests passed")
sys.exit(1 if failed else 0)
