#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""tools/fm1_install.py against a simulated FM-1 (no hardware, no mido).
The fake device is the one in web/test_web.mjs: identity on the handshake,
then "device asks, host answers" reads of the logical image. Run from the repo root:
  python3 tests/install_test.py
Also checks logical_image/product_of against web/fm1pkg.js (needs node and
build/choralroot.fwsc, skipped otherwise). stuck() and midi_side(): MIDI calls that never return (a simulated
backend's; the tool's own worker processes with tests/fake_rtmidi as their rtmidi: no CoreMIDI, no real port)."""
import io
import os
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
                 stall_after=None, bad_addr=None, bk=None, name_after="Felucca",
                 loader="ota-FM-1_900"):
        self.image, self.unplug_after, self.after_write = image, unplug_after, after_write
        self.loader = loader                      # the loader step 1 starts (an SMK-37's: "ota-SMK-37 Pro_900")
        self.bk = bk                              # bk(identity): the firmware's backup side (BackupSide) or None
        self.stall_after, self.bad_addr, self.name_after = stall_after, bad_addr, name_after
        self.served = self.bad = self.upgrades = 0
        self.sent, self.links, self.gen, self.lock = [], [], 0, threading.Lock()
        self.bus = None                           # a FakeBus it is on (several FM-1s)
        self.boot(identity, name)

    def boot(self, identity, name):
        with self.lock:
            self.gen += 1
            self.identity, self.name, self.connected = identity, name, True
            self.waiting, self.queue = None, []
        if self.bus:                              # (a restarted unit is a new device: its ports come last)
            self.bus.again(self)

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
                threading.Timer(0.05, self.boot, (self.loader, "Felucca Update")).start()
            elif addr == I.FINISH_WRITE:
                threading.Timer(0.05, self.boot, (self.after_write, self.name_after)).start()
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

    def __init__(self, version, ids, objs=(), busy=0, uid=None):
        self.version, self.ids, self.objs, self.busy = version, ids, dict(objs), busy
        self.log, self.staged, self.restarts = [], None, 0
        self.uid = uid                            # the unit's ID (3y66): INFO's tags 43 01 03 and 49 08 ID after 42

    def handle(self, cmd, a):
        if cmd == I.BK_INFO:
            return [*self.version.encode(), 0, 0, 0, 0, 0, 0, 0, 0, 0x42, 1, 3] + \
                ([0x43, 1, 3, 0x49, 8, *self.uid.encode()] if self.uid else [])
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
        if cmd == I.BK_STUB and getattr(self, "stub", None) is not None and len(a) == 1:
            rc = self.stub(a[0])                  # (None: no answer)
            return None if rc is None else [rc]
        if cmd == I.BK_DEBUG and getattr(self, "debug", None) is not None and not a:
            d = self.debug() if callable(self.debug) else self.debug     # (None: no answer)
            return d and [1, len(d)] + [b for v in d for b in I.bk_u32(v)]
        if cmd == I.BK_PEAK and getattr(self, "peak", None) is not None and not a:
            return [1, len(self.peak)] + [b for v in self.peak for b in I.bk_u32(v)]
        if cmd == I.BK_USB and getattr(self, "usb", None) is not None and not a:   # (0.19; None: "unsupported")
            return [1, len(self.usb)] + [b for v in self.usb for b in I.bk_u32(v)]
        if cmd == I.BK_SCREEN and self.version.startswith("ChoralRoot") and len(a) == 1 and a[0] <= 1:
            self.log.append(f"screen {a[0]}")
            return [0]
        if cmd == I.BK_SELECT and self.version.startswith("ChoralRoot") and len(a) in (3, 4):
            self.log.append(f"select {a[0]} {a[1]} {a[2] | (a[3] << 7 if len(a) > 3 else 0)}")
            return [2 if a[2] > 60 else 0]
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
    ("FM-1_920", "ChoralRoot 0.22 1-core", "allow", "choralroot"),   # the single-core package (docs/DUALCORE.md)
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
    # RemoteFM1 0.2 on (fm1-base's identity: "FM-1_52" + "00" or + XXYYZZ, no INFO); its neighbours keep their verdicts
    ("FM-1_52000201", None, "allow", "remotefm1"),              # RemoteFM1 0.2.1
    ("FM-1_52000200", None, "allow", "remotefm1"),              # RemoteFM1 0.2.0
    ("FM-1_5200", None, "allow", "remotefm1"),                  # a RemoteFM1 development build
    ("FM-1_5201", None, "refuse", "unknown"),                   # not fm1-base's scheme
    ("FM-1_520002", None, "refuse", "unknown"),
    ("FM-1_520", None, "refuse", "unknown"),
    ("FM-1_4600", None, "refuse", "unknown"),                   # FIZZ (fm1-base, another firmware): not ours to judge
    ("FM-1_46000120", None, "refuse", "unknown"),
    ("ota-FM-1_52000201", None, "loader", "loader"),
    # the M-Vave SMK-37 (SMK37.md): the official firmware, ChoralRoot and the other ports on the SMK board layer;
    # ChanceTheMaker's SMK firmwares (SMK-37_9NN) have their own updater
    ("SMK-37 Pro_016", None, "allow", "stock"),
    ("MKE-P37_016", None, "allow", "stock"),
    ("SMK-37 Pro_920", "ChoralRoot 0.22", "allow", "choralroot"),
    ("SMK-37 Pro_900", "FELUCCA v1.6", "allow", "smk"),
    ("SMK-37 Elite_920", None, "allow", "smk"),
    ("SMK-37 Pro_710", None, "allow", "smk"),
    ("SMK-37_931", None, "refuse", "other"),
    ("SMK-37 Pro_500", None, "refuse", "unknown"),
    ("ota-SMK-37 Pro_900", None, "loader", "loader"),
]
# with the MIDI port's name: Bubba Machine answers FM-1_003 on the port "FM-1 Bubba Machine" (its updater refuses
# every package without M-VAVE's own loader); the same identity on the FM-1's own port is the old official firmware
PORT_CASES = [
    ("FM-1_003", None, "FM-1 Bubba Machine", "refuse", "bubba"),
    ("FM-1_003", None, "FM-1 Bubba Machine:FM-1 Bubba Machine MIDI 1 20:0", "refuse", "bubba"),   # (Linux ALSA)
    ("FM-1_003", None, "FM-1", "allow", "stock"),
    ("FM-1_015", None, "FM-1", "allow", "stock"),
    ("FM-1_003", None, None, "allow", "stock"),
    ("FM-1_920", "ChoralRoot 0.25.1", "FM-1", "allow", "choralroot"),
    ("FM-1_52000201", None, "FM-1", "allow", "remotefm1"),      # RemoteFM1's USB product name is "FM-1", as 0.1's
    ("ota-FM-1_015", None, "ota-FM-1", "loader", "loader"),     # update mode (the official loader Bubba starts)
]


def guard():
    bad = [(i, v, I.classify_firmware(i, v)[:2]) for i, v, verdict, kind in GUARD_CASES
           if I.classify_firmware(i, v)[:2] != (verdict, kind)]
    ok(not bad, f"classify_firmware: {len(GUARD_CASES)} identity / version cases{'; wrong: ' + repr(bad) if bad else ''}")
    bad = [(i, v, n, I.classify_firmware(i, v, n)[:2]) for i, v, n, verdict, kind in PORT_CASES
           if I.classify_firmware(i, v, n)[:2] != (verdict, kind)]
    ok(not bad, f"classify_firmware with the port's name: {len(PORT_CASES)} cases (Bubba Machine){'; wrong: ' + repr(bad) if bad else ''}")
    ok(I.family("ChoralRoot 0.22 1-core") == "choralroot" and I.backup_name("ChoralRoot 0.22 1-core").startswith("choralroot-backup-"),
       "the single-core package's version (ChoralRoot 0.22 1-core): family choralroot, its backup file named as ChoralRoot's")
    raw = package("FM-1_920")
    image = I.logical_image(raw)
    p = pkgfile("cr_guard.fwsc", raw)

    def over(identity, version, *extra, answer=True, name="FM-1"):
        side = BackupSide(version, I.FELUCCA_IDS) if version else None
        dev = FakeFM1(image, identity=identity, name=name, after_write="FM-1_920", bk=lambda i: side if i == identity else None)
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
    for ident, ver in (("FM-1_910", "FELUCCA v1.0.1"), ("FM-1_90111", "MELODEE v0.11.1"), ("FM-1_920", "ChoralRoot 0.12"), ("FM-1_920", "ChoralRoot 0.22 1-core"),
                       ("FM-1_015", None), ("FM-1_52000201", None), ("FM-1_5200", None)):
        rc, out, err, dev = over(ident, ver)
        ok(rc == 0 and dev.identity == "FM-1_920" and dev.bad == 0 and (not ver or ver in out),
           f"over {ver or ('RemoteFM1 ' if ident.startswith('FM-1_52') else 'the stock ') + ident}: installed")
    names = [I.classify_firmware(i, None)[2] for i in ("FM-1_52000201", "FM-1_52011203", "FM-1_5200")]
    ok(names == ["RemoteFM1 0.2.1", "RemoteFM1 1.12.3", "RemoteFM1 (development build)"],
       f"RemoteFM1's version read from its identity: {names}")
    rc, out, err = cli(["--info"], FakeFM1(image, identity="FM-1_52000201"))
    ok(rc == 0 and "ChoralRoot can be installed over RemoteFM1 0.2.1" in out, "--info on RemoteFM1 0.2.1: named, installed over")
    for extra in ((), ("--force",)):              # Bubba Machine: its updater refuses our loader, so never
        rc, out, err, dev = over("FM-1_003", None, *extra, name="FM-1 Bubba Machine")
        ok(rc == 8 and dev.upgrades == 0 and "Bubba Machine (FM-1_003)" in err and "official V15" in err
           and "FM-1.fwsc" in err and "--force does not" in err,
           f"over Bubba Machine{' with --force' if extra else ''}: refused (exit 8), nothing sent, the way back to V15")
    rc, out, err, dev = over("FM-1_003", None)
    ok(rc == 0 and dev.identity == "FM-1_920" and dev.bad == 0, "over FM-1_003 on the FM-1's own port (the old stock): installed")
    rc, out, err = cli(["--info"], FakeFM1(image, identity="FM-1_003", name="FM-1 Bubba Machine"))
    ok(rc == 0 and "not installed over Bubba Machine (FM-1_003)" in out and "FM-1.fwsc" in out,
       "--info on Bubba Machine: named, not installed over, the way back to V15")
    side = BackupSide("FELUCCA SLOOP 2.2", I.FELUCCA_IDS)
    rc, out, err = cli(["--info"], FakeFM1(image, identity="FM-1_900", bk=lambda _i: side))
    ok(rc == 0 and "FELUCCA SLOOP 2.2" in out and "not installed over Sloop" in out, "--info: version and the verdict")


def smk():
    """the M-Vave SMK-37 (SMK37.md): one keyboard under several model names, never an FM-1; the rule on every path"""
    raw = package("SMK-37 Pro_920")
    image = I.logical_image(raw)
    p = pkgfile("cr_smk.fwsc", raw)
    side = BackupSide("FELUCCA v1.6", I.FELUCCA_IDS)
    dev = FakeFM1(image, identity="SMK-37 Pro_900", name="Felucca", loader="ota-SMK-37 Pro_900",
                  after_write="SMK-37 Pro_920", bk=lambda i: side if i == "SMK-37 Pro_900" else None)
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 0 and dev.bad == 0 and "done: the FM-1 runs SMK-37 Pro_920" in out, "SMK: over another SMK port: installed")
    dev = FakeFM1(image, identity="SMK-37 Elite_016", name="SMK-37 Elite", loader="ota-SMK-37 Pro_900",
                  after_write="SMK-37 Pro_920")
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 0 and dev.bad == 0, "SMK: a Pro package on an SMK-37 Elite (the same keyboard): installed")
    dev = FakeFM1(image, identity="FM-1_015")
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 6 and dev.upgrades == 0, "SMK: an SMK package on an FM-1 -> exit 6, nothing sent")
    fm1 = pkgfile("cr_fm1.fwsc", package("FM-1_920"))
    dev = FakeFM1(image, identity="SMK-37 Pro_016", name="SMK-37 Pro")
    rc, out, err = cli([fm1, "--yes"], dev)
    ok(rc == 6 and dev.upgrades == 0, "SMK: an FM-1 package on an SMK -> exit 6, nothing sent")
    dev = FakeFM1(image, identity="ota-SMK-37 Pro_900", name="Felucca Update")
    rc, out, err = cli([fm1, "--yes"], dev)
    ok(rc == 6 and dev.upgrades == 0, "SMK: an SMK in update mode never gets an FM-1 package (the resume path)")
    dev = FakeFM1(image, identity="FM-1_015", loader="ota-SMK-37 Pro_900", after_write="FM-1_920")
    rc, out, err = cli([fm1, "--yes"], dev)
    ok(rc == 6 and dev.upgrades == 1 and "nothing was written" in err,
       "SMK: a loader of another keyboard after step 1 -> exit 6, nothing written")
    dev = FakeFM1(image, identity="SMK-37_931", name="SMK-37")
    rc, out, err = cli([p, "--yes", "--force"], dev)
    ok(rc == 8 and dev.upgrades == 0 and "its own updater" in err, "SMK: over SMK-37_9NN (its own updater): refused, not with --force")
    dev = FakeFM1(image, identity="ota-SMK-37 Pro_016", name="SMK-37 Pro")
    rc, out, err = cli([p, "--yes"], dev)
    ok(rc == 6 and dev.upgrades == 0 and "another firmware" in err,
       "SMK: in the stock updater's update mode: never written (exit 6)")
    ok(I.our_loader("ota-SMK-37 Elite_900") and not I.our_loader("ota-SMK-37 Pro_016") and not I.our_loader("ota-SMK-37_931"),
       "our_loader: ours only")
    ok(I.same_keyboard("SMK-37 Pro_920", "ota-MKE-P37_900") and not I.same_keyboard("FM-1_920", "SMK-37 Pro_920"),
       "same_keyboard: the SMK models one keyboard, never the FM-1")


def starrykey():
    """a 21-character identity ("STARRYKEY-37 PLAY_016"): the device's and the package's (as web/fm1pkg.js)"""
    def pkg(product, blocks, size=0x3000):
        img = bytearray((i * 7) & 0xFF for i in range(size))
        data = bytes(img[0x400:])
        e = bytearray(0x50)
        e[4:6] = I._crc16(data).to_bytes(2, "little")
        e[8:16] = (0x400).to_bytes(4, "little") + len(data).to_bytes(4, "little")
        e[0x40:0x49] = b"flash.bin"
        lst = I._dec(bytes(e))
        hdr = bytearray(0x40)
        hdr[4:10] = size.to_bytes(4, "little") + (1).to_bytes(2, "little")
        hdr[2:4] = I._crc16(lst).to_bytes(2, "little")
        hdr[0:2] = I._crc16(bytes(hdr[2:0x40])).to_bytes(2, "little")
        img[0:0x90] = I._dec(bytes(hdr)) + lst
        raw = bytearray()
        for i in range(blocks):
            m = (ord(product[i]) + i + 1) & 0xFF if i < len(product) else 0x7D
            raw += img[i * I.KEEP:(i + 1) * I.KEEP] + bytes([m])
        return bytes(raw + img[blocks * I.KEEP:])

    ident = I.parse_identity(b"\xF0" + I.pack7(bytes([0, 0x59, 0x11, 0, 0, 0]) + b"STARRYKEY-37 PLAY_016" + bytes(7)) + b"\xF7")
    ok(ident and ident.text == "STARRYKEY-37 PLAY_016" and ident.model == "STARRYKEY-37 PLAY" and ident.version == 16,
       "device identity STARRYKEY-37 PLAY_016: all 21 characters")
    ident = I.parse_identity(b"\xF0" + I.pack7(bytes([0, 0x59, 0x11, 0, 0, 0]) + b"FM-1_015\0xyz" + bytes(16)) + b"\xF7")
    ok(ident and ident.text == "FM-1_015", "device identity: the text ends at the first NUL")
    raw = pkg("STARRYKEY-37 PLAY_016", 36)
    im = I.deinterleave(raw)
    ok(I.identity_blocks(raw) == 21 and I.block_count(raw) == 36 and len(im) == len(raw) - 36 and I.files_ok(im)
       and I.product_of(raw) == "STARRYKEY-37 PLAY_016", "36-block package with a 21-character identity: read in full")
    raw = pkg("STARRYKEY-37 PLAY_015", 20)
    ok(I.identity_blocks(raw) == 20 and I.product_of(raw) == "STARRYKEY-37 PLAY_01" and I.files_ok(I.deinterleave(raw)),
       "20-block package: the identity stops at 20 characters (the stock V015 layout)")
    raw = pkg("SMK-37 Pro_016", 36)
    ok(I.identity_blocks(raw) == 20 and I.block_count(raw) == 36 and I.product_of(raw) == "SMK-37 Pro_016",
       "36-block package with a short identity: unchanged")
    raw = package("FM-1_920")
    ok(I.deinterleave(raw) == I.logical_image(raw) and I.product_of(raw) == "FM-1_920", "an FM-1 package: as before")
    if shutil.which("node"):                      # the page reads them as this tool does (web/fm1pkg.js)
        js = ("import { logicalImage, productOf } from %r; import { readFileSync } from 'node:fs';"
              "const p = readFileSync(process.argv[1]); process.stderr.write(productOf(p));"
              "process.stdout.write(logicalImage(p));") % str(ROOT / "web/fm1pkg.js")
        same = True
        for name, raw in (("long", pkg("STARRYKEY-37 PLAY_016", 36)), ("short20", pkg("STARRYKEY-37 PLAY_015", 20)),
                          ("smk36", pkg("SMK-37 Pro_016", 36))):
            f = pkgfile(f"id-{name}.fwsc", raw)
            r = subprocess.run(["node", "--input-type=module", "-e", js, f], capture_output=True, check=True)
            same &= I.deinterleave(raw) == r.stdout and I.product_of(raw) == r.stderr.decode()
        ok(same, "web/fm1pkg.js reads the 21-character identity and the 36-block layouts as this tool does")


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
    js_port = js.replace("c.map(([i, v]) => { const r = classifyFirmware(i, v);", "c.map(([i, v, n]) => { const r = classifyFirmware(i, v, n);")
    r = subprocess.run(["node", "--input-type=module", "-e", js_port, json.dumps([[i, v, n] for i, v, n, _, _ in PORT_CASES])],
                       capture_output=True, check=True)
    got = [tuple(x) for x in json.loads(r.stdout)]
    want = [I.classify_firmware(i, v, n) for i, v, n, _, _ in PORT_CASES]
    ok(got == want, "classify_firmware == fm1ota.js classifyFirmware with the port's name (Bubba Machine)")
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
    for f in sorted((ROOT / "build").glob("choralroot-smk37*.fwsc")):   # (the SMK packages, when built: SMK37.md)
        r = subprocess.run(["node", "--input-type=module", "-e", js, str(f)], capture_output=True, check=True)
        raw = f.read_bytes()
        ok(I.deinterleave(raw) == r.stdout and I.product_of(raw) == r.stderr.decode(),
           f"logical image / identity == fm1pkg.js ({f.name}, {I.product_of(raw)})")


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


def select():
    cr = BackupSide("ChoralRoot 0.16", I.CR_IDS)
    dev = FakeFM1(b"", identity="FM-1_920", bk=lambda _i: cr)
    rc, out, err = cli(["--select", "1", "12", "3"], dev)
    ok(rc == 0 and "part 1: engine 12 position 3" in out and cr.log == ["select 1 12 3"] and
       dev.sent[-1] == bytes([0xF0, 0x7D, 0x46, 0x4C, 76, 1, 12, 3, 0, 0xF7]), "--select 1 12 3: 76 SELECT, rc 0")
    rc, out, err = cli(["--select", "0", "12", "99"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: cr))
    ok(rc != 0 and "bad pool position" in err, "--select a bad position: the rc named")
    rc, out, err = cli(["--select", "2", "12", "1"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: cr))
    ok(rc != 0, "--select part 2: refused before sending")


def debug():
    """--debug: the counters by name, and the boot stub's mailbox decoded (firmware/stub/stub_core.h)"""
    n = len(I.DEBUG_NAMES)
    at = I.DEBUG_NAMES.index("stub_ms")
    ok(I.DEBUG_NAMES[at:at + 5] == ["stub_ms", "stub_us", "stub_ver", "stub_decision", "stub_detail"] and
       I.DEBUG_NAMES[at - 1] == "st_wait_us", "--debug: the stub's five values follow the stage times (cr_backup.c)")
    cr = BackupSide("ChoralRoot 0.1", I.CR_IDS)
    cr.debug = [0] * n
    cr.debug[at:at + 5] = [47, 47310, 1, 1, 0 | 5 << 8 | 1 << 16 | 0 << 24]
    rc, out, err = cli(["--debug"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: cr))
    ok(rc == 0 and "stub_ms 47" in out and "boot stub v1: booted the app after 47.3 ms; marker ok, staged loader "
       "not checked, rescue attempts before this boot 1, rescue key not held" in out, "--debug: the boot stub's line")
    ok("event log" not in out, "--debug: a reply that ends with the names (an older firmware): no log, no tail")
    cr.debug[at:at + 5] = [0, 0, 0, 0, 0]
    rc, out, err = cli(["--debug"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: cr))
    ok(rc == 0 and "boot stub: none ran" in out, "--debug: a firmware without the stub says so")
    # 0.18: the worst cases behind the tail (DEBUG_PEAK): named, the late half's stage in words, the three lines
    cr.debug = [0] * (n + I.DEBUG_LOG + len(I.DEBUG_TAIL))
    cr.peak = [2580, 9, 700, 150, 1400, 120, 60, 12, 4, 2950, 34500, 30000, 40, 110, 90, 200, 300, 520, 35, 90, 14, 0, 5, 9]
    rc, out, err = cli(["--debug"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: cr))
    ok(rc == 0 and "pk_block_us 2580" in out and "saves_session 9" in out and "stale_halves" not in out and
       "the deadline:" not in out, "--debug: a 0.18 of 24 PEAK values: those, and no word about the six it does not send")
    cr.peak += [640, 3350, 410, 2, 1130, 0x0200B2C4]
    rc, out, err = cli(["--debug"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: cr))
    ok(rc == 0 and "pk_lag_us 640" in out and "irq_gap_pc 33600196 (0x0200B2C4)" in out and
       "the deadline: 2 halves in which the DAC played something old" in out and
       "started at most 640 us behind its boundary, its longest half took 3350 us with TIMER5 nested in it (410 us" in out and
       "once came 1130 us after the one before: masked for about 1030 us, until 0x0200B2C4" in out,
       "--debug: when the ISR got to start (PEAK's values 25..30): the stale halves, the lag, TIMER5, the masked stretch")
    ok(rc == 0 and len(I.DEBUG_PEAK) == 79 and "key_ons" not in out and "pk_block_us 2580" in out and "pk_late_stage 4 (fx)" in out and
       "worst half since the last --debug: 2580 us of 2902 (88 %) over 34500 halves" in out and
       "late 12 (the latest: 2950 us, its largest stage fx)" in out and
       "down 40 / 110, delay 90 / 200, reverb 300 / 520, up 35 / 90" in out and
       "14 erases (45 ms of silence each), 0 of them into a sounding output (holes), settings saves 5, session saves 9" in out,
       "--debug: the worst cases since the last read (78 PEAK, DEBUG_PEAK), the fx stage's parts, the flash's holes")
    cr.peak += [212, 3100, 2650, 5400]
    rc, out, err = cli(["--debug"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: cr))
    ok(rc == 0 and "key_ons 212" in out and "key_pick_us 3100" in out and "key_eng_avg_us 2650" in out and
       "key_eng_us 5400" in out and "keys: 212 note-ons since the last --debug" in out and
       "posted one after 3.1 ms at most, the engine had it after 2.6 ms on average, 5.4 ms at most" in out,
       "--debug: the keys' way to the engine (PEAK's values 31..34): the note-ons, the main loop's and the engine's times")
    ok("shed_voices" not in out and "overload:" not in out, "--debug: a firmware of 34 PEAK values (to 0.21): no word of the shed")
    # 3y44: what the overload shed did (PEAK's values 35..41)
    cr.peak += [3, 0x10000 | 0 << 12 | 2 << 8 | 69, 2, 17, 2611, 9, 4]
    rc, out, err = cli(["--debug"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: cr))
    ok(rc == 0 and "shed_voices 3" in out and "shed_last_voice 66117 (part 0 (chord) voice 2, note 69 (A4))" in out and
       "shed_last_why 2 (a held voice, the oldest that is not its part's lowest note)" in out and
       "shed_over_halves 17" in out and "shed_last_us 2611" in out and "voices_faded 9" in out and "voices_stolen 4" in out and
       "overload: 17 halves above 85 % of the budget since the last --debug, 3 voices shed (two such halves in a row fade "
       "one); the latest: part 0 (chord) voice 2, note 69 (A4), a held voice, the oldest that is not its part's lowest "
       "note, asked by a half of 2611 us; voices faded out for another note, part or the shed 9, own voices taken for a "
       "new note 4" in out,
       "--debug: what the overload shed did (PEAK's values 35..41): the count, the latest voice and why, the halves above 85 %")
    ok("p5_core0_voices" not in out and "P5: at most" not in out, "--debug: a firmware of 41 PEAK values (3y44, 3y45): no word of P5's cores")
    cr.peak[34:] = [0, 0, 0, 0, 0, 2, 1]
    rc, out, err = cli(["--debug"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: cr))
    ok(rc == 0 and "shed_last_voice 0 (none)" in out and "overload: 0 halves above 85 % of the budget since the last "
       "--debug, 0 voices shed (two such halves in a row fade one); voices faded out" in out,
       "--debug: nothing shed: said so, with the voices faded and taken")
    # 3y46: P5's voices on two cores (PEAK's values 42..45)
    cr.peak[34:] = [1, 0x10000 | 1 << 12 | 3 << 8 | 38, 4, 5, 2500, 6, 2, 4, 4, 4, 3]
    rc, out, err = cli(["--debug"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: cr))
    ok(rc == 0 and "p5_core0_voices 4" in out and "p5_core1_voices 4" in out and "p5_bass_voices 4" in out and
       "p5_bass_given_up 3" in out and "shed_last_why 4 (a voice of the bass part's P5 stack, before any chord voice)" in out and
       "part 1 (bass) voice 3, note 38 (D2)" in out and
       "P5: at most 4 voices rendered by core 0 and 4 by core 1 in one block (four each is the rule), the bass part 4 at "
       "most, 3 held bass voices given up to the chord; core 1's longest job of a half 150 us (pk_part1_us), core 0's "
       "longest wait for it 60 us (pk_wait_us)" in out,
       "--debug: P5 on two cores (PEAK's values 42..45): the voices a core rendered, the bass part's, given up; a shed bass voice")
    cr.peak[41:] = [0, 0, 0, 0]
    rc, out, err = cli(["--debug"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: cr))
    ok(rc == 0 and "p5_core0_voices 0" in out and "P5: at most" not in out, "--debug: no P5 voice played: the values, no line")
    ok("erases_near" not in out and "quiet writes" not in out, "--debug: a firmware of 60 PEAK values (to 3y50): no word of the quiet writes")
    cr.peak[45:] = [0] * 15 + [7, 3, 0, 612]
    rc, out, err = cli(["--debug"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: cr))
    ok(rc == 0 and len(I.DEBUG_PEAK) == 79 and "saves_auto 7" in out and "erases_ahead 3" in out and "erases_near 0" in out and
       "flash_prog_us 612" in out and "quiet writes since power-on: 7 saves by the unit itself (Options, favourites), 3 of "
       "the 14 erases done ahead" in out and "0 erases started by the unit while a voice was active or within 2 s of an "
       "input (the rule: 0), the longest page program 612 us" in out and "NEAR PLAYING" not in out,
       "--debug: the quiet writes (PEAK's values 61..64 (3y52; p5-g's 35..38)): the unit's own saves, the erases ahead, those near playing, a page's time")
    cr.peak[62] = 2
    rc, out, err = cli(["--debug"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: cr))
    ok(rc == 0 and "erases_near 2" in out and "AN ERASE FELL NEAR PLAYING" in out, "--debug: an erase near playing is said loudly")
    ok("ui_frame_max_us" not in out and "the UI since" not in out, "--debug: a firmware of 64 PEAK values (to 3y52): no word of the UI's frame")
    cr.peak[64:] = [9100, 2400, 3, 580]
    rc, out, err = cli(["--debug"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: cr))
    ok(rc == 0 and "ui_frame_max_us 9100" in out and "flash_read_bytes_ps 580" in out and "a frame took 2400 us on average, "
       "9100 us at most; flash read 3 times a second (580 bytes a second)" in out and "OVER 50 MS" not in out,
       "--debug: the UI's frame and the flash reads (PEAK's values 65..68, 3y60)")
    # the dual-core fallback's branch (its values 35..45 there, appended here by name: 69..79)
    ok("c1_bar" not in out and "last crash" not in out, "--debug: a firmware of 68 PEAK values (to 3y60): no word of the bar")
    cr.peak += [1]
    rc, out, err = cli(["--debug"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: cr))
    ok(rc == 0 and len(I.DEBUG_PEAK) == 79 and I.DEBUG_PEAK[68] == "c1_bar" and
       "c1_bar 1 (after a crash beside core 1)" in out and "last crash" not in out and "shed_voices 1" in out,
       "--debug: why this boot kept core 1 off (PEAK's value 69, c1_bar), the values before it as they were")
    cr.peak += [3, 1, 0x0200046E, 0x02007D80, 0, 0x00001000, 184200, 0x40, 1]
    rc, out, err = cli(["--debug"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: cr))
    ok(rc == 0 and "crash_pc 33555566 (0x0200046E)" in out and "boot_reset 64 (0x00000040)" in out and
       "boot_stage 1 (loop: input)" in out and
       "last crash (kept until a power-off): #3, vector 1, pc 0x0200046E, rets 0x02007D80, EMU_MSG 0x00000000, "
       "DBG_MSG 0x00001000, 184.2 s after its boot" in out,
       "--debug: the last crash's record and the boot's reset reason (PEAK's values 70..78)")
    cr.peak[-9] = 0
    rc, out, err = cli(["--debug"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: cr))
    ok(rc == 0 and "last crash: none since the power-on" in out, "--debug: no crash since the power-on")
    cr.peak += [2]
    rc, out, err = cli(["--debug"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: cr))
    ok(rc == 0 and "voices_shed 2" in out and I.DEBUG_PEAK[-1] == "voices_shed",
       "--debug: the voices the overload shed dropped since power-on (PEAK's value 79)")
    cr.peak = None
    rc, out, err = cli(["--debug"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: cr))
    ok(rc == 0 and "pk_block_us" not in out and "stub_ms" in out, "--debug on a firmware without 78 PEAK: 73's values alone")
    ok("ua_rate" not in out and "ChoralRoot In:" not in out, "--debug on a firmware without 79 USB (before 0.19): no word of a rate")
    # 0.19: 79 USB (DEBUG_USB): the rate, the 48 kHz packets, the resampler's time on the unit
    ok(I.BK_USB == 79 and I.DEBUG_USB[:4] == ["ua_rate", "ua_rate_last_open", "ua_rate_sets", "ua_rate_changes"] and
       len(I.DEBUG_USB) == 14 and I.DEBUG_USB[10:] == ["ua_packet_max", "ua_48k_offered", "ua_48k_refused", "ua_replugs"],
       "--debug: 79 USB's names (cr_backup.c crb_usb's order; the option's state appended behind 0.19's first eleven)")
    cr.usb = [48000, 48000, 3, 1, 59000, 4, 1200, 81000, 46, 131, 588]
    cr.debug[I.DEBUG_NAMES.index("ua_open_close_ms")] = 60400
    cr.debug[I.DEBUG_NAMES.index("ua_frames_last_open")] = 2900000
    rc, out, err = cli(["--debug"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: cr))
    ok(rc == 0 and "ua_rate 48000" in out and "ua_rs_avg_us 46" in out and "ua_packet_max 588" in out and
       "ChoralRoot In: 48000 Hz now, the last whole open at 48000 Hz; the host set the rate 3 times (1 changes)" in out and
       "48 kHz: packets of 48 / 47 / 49 frames: 59000 / 4 / 1200; the resampler takes 46 us a block on average = 184 us "
       "of a half's 2902 (6.3 %), 131 us at most for one block" in out and
       "last whole open: 2900000 frames in 60400 ms = 48.0132 frames/ms (at 48000 Hz: 48 is the rate's)" in out,
       "--debug: 79 USB by name, the rate's line, the resampler's cost a half, the last open against its rate")
    cr.usb = [44100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 588]
    cr.debug[I.DEBUG_NAMES.index("ua_open_close_ms")] = 0
    rc, out, err = cli(["--debug"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: cr))
    ok(rc == 0 and "ChoralRoot In: 44100 Hz now, the last whole open at no rate (none yet); the host set the rate 0 "
       "times (0 changes)" in out and "48 kHz: packets" not in out, "--debug: 44.1 kHz, never at 48: no 48 kHz line")
    ok("Options > USB Rate" not in out, "--debug: a 79 USB answer of before the option (11 values): no word of it")
    # 2026-10-12: Options > USB Rate (48 kHz is opt-in): its state, the refused requests, the replugs
    cr.usb = [44100, 44100, 2, 0, 0, 0, 0, 0, 0, 0, 588, 0, 2, 1]
    rc, out, err = cli(["--debug"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: cr))
    ok(rc == 0 and "ua_48k_offered 0" in out and "ua_48k_refused 2" in out and "ua_replugs 1" in out and
       "Options > USB Rate: 44.1 kHz (48 kHz is not offered: 0.18's device); the host asked for 48 kHz 2 times and "
       "got 44.1" in out, "--debug: USB Rate 44.1 kHz: said, with the host's refused requests for 48 kHz")
    cr.usb = [48000, 48000, 1, 1, 59000, 4, 1200, 81000, 46, 131, 588, 1, 0, 1]
    rc, out, err = cli(["--debug"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: cr))
    ok(rc == 0 and "Options > USB Rate: 44.1 + 48 kHz (the computer chooses)" in out and "got 44.1" not in out,
       "--debug: USB Rate 44.1 + 48 kHz: said")
    cr.usb = [48000, 44100, 1]                      # (a shorter answer: what it has)
    rc, out, err = cli(["--debug"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: cr))
    ok(rc == 0 and "ua_rate_sets 1" in out and "ua_rate_changes" not in out and "ChoralRoot In:" not in out,
       "--debug: a 79 USB answer with fewer values: those named, no line made of the missing ones")
    cr.usb = None
    cr.debug = [0] * n
    # stub version 2: the key samples behind the event log (appended: DEBUG_TAIL), the request bits in the detail
    cr.debug = [0] * (n + I.DEBUG_LOG) + [0xFFF00000, 0x00000001, 0, 0x000000FF]
    cr.debug[at:at + 5] = [62, 62400, 2, 1, 0 | 5 << 8 | 0 << 16 | 1 << 24]
    rc, out, err = cli(["--debug"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: cr))
    ok(rc == 0 and "boot stub v2: booted the app after 62.4 ms; marker ok, staged loader not checked, rescue attempts "
       "before this boot 0, rescue key held" in out and "stub_oct_dn 4293918720 (0xFFF00000)" in out and
       "rescue key: OCT- closed in 12 of 32 samples (tail 12 of 12), OCT+ 1 (tail 0), idle 0 (tail 0)" in out and
       "PA in 0x000000FF at the last (PA8 low)" in out, "--debug: stub v2's key samples, in hex and in words")
    cr.debug[at + 4] = 0 | 0 << 8 | 1 << 26
    rc, out, err = cli(["--debug"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: cr))
    ok(rc == 0 and "staged loader ok, rescue attempts before this boot 0, rescue key not held (the app asked for the "
       "test)" in out, "--debug: the test request's answer (the loader's code, the request named)")
    cr.debug[at + 4] = 0 | 3 << 8 | 1 << 26
    rc, out, err = cli(["--debug"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: cr))
    ok(rc == 0 and "staged loader not Felucca's" in out, "--debug: a loader the stub would refuse is named")


def uac():
    """tools/uac_check.py: what the unit did during a capture, from two reads of its counters (73 DEBUG). The
    arithmetic and the words only: nothing is recorded, no port but the simulated FM-1's is opened"""
    import uac_check as U
    base = {"ms": 100000, "ua_cap_alt": 0, "ua_opens": 3, "ua_open_ms": 50000, "ua_open_close_ms": 14100,
            "ua_frames_last_open": 620000, "ua_tx_packets": 49900, "ua_sofs": 50000, "ua_overruns": 3,
            "ua_underruns": 0, "ua_missed_frames": 0, "ua_nudges_up": 20, "ua_nudges_down": 0}
    # a clean minute: one open of 60.4 s, a packet a frame but the 28 of the open's start, the close's one overrun
    frames = round(60372 * 44.1) + 1162
    after = dict(base, ms=165000, ua_opens=4, ua_open_ms=110400, ua_open_close_ms=60400, ua_frames_last_open=frames,
                 ua_tx_packets=49900 + 60372, ua_sofs=50000 + 60400, ua_overruns=4, ua_nudges_up=20 + 1162)
    u = U.unit_delta(base, after)
    ok(u["opens"] == 1 and u["closes"] == 1 and u["whole"] and u["last_ms"] == 60400 and u["open_ms"] == 60400 and
       u["frames"] == frames and abs(u["frames_ratio"] - frames / (44100 * 60.4)) < 1e-12 and
       abs(u["short_ms"] - (60400 - frames / 44.1)) < 1e-6 and u["packets"] == 60372 and u["sofs"] == 60400 and
       u["overruns"] == 1 and u["overruns_open"] == 0 and u["underruns"] == 0 and u["missed"] == 0 and
       round(u["ppm"]) == 436, "uac_check: a clean minute: the differences of the counters, the open, the clock in ppm")
    text = "\n".join(U.unit_report(base, after, 60.0))
    ok("opens of the stream: 1, closes: 1" in text and "the last whole open: 60.400 s = 1.007 x the 60 s asked" in text
       and "frames sent in it: 2 663 567 = 60.398 s at 44100 Hz = 0.99997 x its length" in text and
       "packets: 60 372 for 60 400 USB frames (SOFs) while open: 28 frames without a packet" in text and
       "overruns: 1   (one is counted at each close of the stream" in text and
       "1 close here: none more than the closes account for" in text
       and "underruns: 0, missed frames: 0" in text and "+436 ppm" in text and
       text.endswith("verdict: the FM-1 sent the stream whole"), "uac_check: a clean minute: its lines, the close's overrun said, the verdict")
    # the hour of 2026-10-10 (docs/USB-AUDIO.md): 3 597 711 packets for 3 597 791 SOFs, 12 overruns (three at stream
    # closes), 69 036 nudges up; the open's own figures here are made up to fit
    hour = {"ms": 3700000, "ua_cap_alt": 0, "ua_opens": 3, "ua_open_ms": 3597800, "ua_open_close_ms": 1500000,
            "ua_frames_last_open": round(1500000 * 44.1 * 1.000435), "ua_tx_packets": 3597711, "ua_sofs": 3597791,
            "ua_overruns": 12, "ua_underruns": 0, "ua_missed_frames": 0, "ua_nudges_up": 69036, "ua_nudges_down": 0}
    u = U.unit_delta({"ms": 1000}, hour)
    text = "\n".join(U.unit_report({"ms": 1000}, hour, 3600.0))
    ok(round(u["ppm"]) == 435 and u["sofs"] - u["packets"] == 80 and u["opens"] == 3 and u["closes"] == 3 and
       u["overruns_open"] == 9 and "3 597 711 for 3 597 791 USB frames" in text and "69 036 up, 0 down" in text and
       "+435 ppm" in text and "3 closes here: 9 more than the closes account for" in text and
       "652 ms more than 44100 Hz x its length" in text and
       "verdict: the host opened the stream 3 times; 9 overruns beyond the closes' (the host did not read for a moment "
       "while the stream ran)" in text,
       "uac_check: the reference hour's figures: +435 ppm, 80 frames without a packet, the overruns beyond the closes")
    # trouble named: an underrun, missed frames, an open that sent too little, an open shorter than the capture
    bad = dict(after, ua_underruns=1, ua_missed_frames=7, ua_frames_last_open=frames - 44100, ua_open_close_ms=60400)
    text = "\n".join(U.unit_report(base, bad, 60.0))
    ok("1 underrun (the ring ran dry)" in text and "7 USB frames missed" in text and "ms of the open without audio sent" in text
       and "sent the stream whole" not in text, "uac_check: an underrun, missed frames, a second of audio not sent: named in the verdict")
    text = "\n".join(U.unit_report(base, dict(after, ua_open_close_ms=20000, ua_frames_last_open=882000), 60.0))
    ok("verdict: the open lasted 20.0 s of the 60 s" in text, "uac_check: an open shorter than the capture is said")
    # the stream still open after the capture (another program), never opened, the FM-1 restarted meanwhile
    still = dict(base, ms=165000, ua_cap_alt=1, ua_opens=4, ua_open_ms=110400, ua_frames_this_open=2000000)
    u = U.unit_delta(base, still)
    text = "\n".join(U.unit_report(base, still, 60.0))
    ok(u["opens"] == 1 and u["closes"] == 0 and not u["whole"] and u["frames_ratio"] is None and
       "still open now" in text and "no whole open to measure" in text and "frames in this open so far: 2 000 000" in text,
       "uac_check: the stream still open afterwards: no whole open, said (the last one's figures are not taken for it)")
    text = "\n".join(U.unit_report(base, dict(base, ms=165000), 60.0))
    ok("opens of the stream: 0, closes: 0" in text and "verdict: the stream was never opened (did ffmpeg find the device?)" in text,
       "uac_check: no open during the capture: said")
    u = U.unit_delta(base, dict(after, ms=30000, ua_opens=1, ua_overruns=1, ua_tx_packets=60372, ua_sofs=60400, ua_nudges_up=1162, ua_open_ms=60400))
    ok(u["restarted"] and u["opens"] == 1 and u["packets"] == 60372 and u["overruns"] == 1,
       "uac_check: the FM-1 restarted between the reads (its uptime went back): the second read alone counts")
    # 0.19, 48 kHz: the open's rate as the unit reports it (79 USB, merged into the read): a clean minute at 48 kHz,
    # the FM-1's clock +435 ppm: 48.0209 frames a ms, 1262 packets of 49
    b48 = dict(base, ua_rate=44100, ua_rate_last_open=44100, ua_rate_sets=0, ua_rate_changes=0, ua_pk576=0, ua_pk564=0,
               ua_pk588=0, ua_rs_blocks=0, ua_rs_avg_us=0, ua_rs_max_us=0)
    f48 = 60372 * 48 + 1262
    a48 = dict(b48, ms=165000, ua_opens=4, ua_open_ms=110400, ua_open_close_ms=60400, ua_frames_last_open=f48,
               ua_tx_packets=49900 + 60372, ua_sofs=50000 + 60400, ua_overruns=4, ua_nudges_up=20 + 1262,
               ua_rate=48000, ua_rate_last_open=48000, ua_rate_sets=1, ua_rate_changes=1, ua_pk576=60372 - 1262,
               ua_pk588=1262, ua_rs_blocks=65000, ua_rs_avg_us=46, ua_rs_max_us=131)
    u = U.unit_delta(b48, a48)
    ok(u["rate"] == 48000 and u["rate_known"] and u["rate_now"] == 48000 and u["rate_sets"] == 1 and
       u["rate_changes"] == 1 and u["pk48"] == (59110, 0, 1262) and abs(u["frames_ratio"] - f48 / (48000 * 60.4)) < 1e-12
       and abs(u["short_ms"] - (60400 - f48 / 48.0)) < 1e-6 and round(u["ppm"]) == 435,
       "uac_check at 48 kHz: the open's frames against 48000 Hz, the nudges against 48 frames a packet (+435 ppm)")
    text = "\n".join(U.unit_report(b48, a48, 60.0, 48000))
    ok(f"frames sent in it: {U.num(f48)} = {f48 / 48000:.3f} s at 48000 Hz = {f48 / (48000 * 60.4):.5f} x its length" in text and
       "the rate of that open, as the FM-1 reports it: 48000 Hz (resampled from its 44.1 kHz by the FM-1)" in text and
       "rate requests from the host (SET_CUR): 1, of which changes of the rate: 1; the rate now: 48000 Hz" in text and
       "48 kHz packets: 59 110 of 48 frames, 0 of 47, 1 262 of 49; the resampler: 46 us a block on average = 184 us of a "
       "half's 2902 (6.3 %), 131 us at most" in text and "+435 ppm" in text and
       text.endswith("verdict: the FM-1 sent the stream whole"), "uac_check at 48 kHz: its lines, the resampler's cost, the verdict")
    text = "\n".join(U.unit_report(b48, a48, 60.0, 44100))
    ok("verdict: the FM-1 streamed at 48000 Hz, not the 44100 Hz asked for" in text, "uac_check --rate 44100, the FM-1 at 48000: said")
    text = "\n".join(U.unit_report(b48, a48, 60.0))
    ok(text.endswith("verdict: the FM-1 sent the stream whole"), "uac_check without --rate: the rate the FM-1 reports is taken")
    # the same counters judged as 44.1 kHz would be 8.8 % too many frames: the rate must come from the unit
    old = {k: v for k, v in a48.items() if not k.startswith(("ua_rate", "ua_pk5", "ua_rs"))}
    u = U.unit_delta(base, old)
    text = "\n".join(U.unit_report(base, old, 60.0, 48000))
    ok(u["rate"] == 44100 and not u["rate_known"] and "this firmware does not report a rate (before 0.19: 44100 Hz only), "
       "48000 Hz was asked for" in text, "uac_check: a firmware without 79 USB is 44100 Hz; --rate 48000 against it: said")
    text = "\n".join(U.unit_report(base, after, 60.0, 44100))
    ok(text.endswith("verdict: the FM-1 sent the stream whole") and "as the FM-1 reports it" not in text,
       "uac_check: a firmware without 79 USB, --rate 44100: as before")
    text = "\n".join(U.unit_report(b48, dict(a48, ua_rate_changes=3, ua_rate_sets=5), 60.0, 48000))
    ok("the host changed the rate 3 times during the capture" in text, "uac_check: the rate changed more than once in a capture: said")
    # 2026-10-12: 48 kHz is an option of the unit, off by default: --rate 48000 against a unit that does not offer it
    off = dict(base, ua_rate=44100, ua_48k_offered=0)
    why48 = U.rate_offered(off, 48000)
    ok(why48 is not None and why48.startswith("the unit does not offer 48 kHz: Options > USB Rate is 44.1 kHz") and
       "44.1 + 48 kHz" in why48 and "Audio MIDI Setup" in why48,
       "uac_check --rate 48000, the unit at USB Rate 44.1 kHz: 'the unit does not offer 48 kHz: Options > USB Rate ...'")
    ok(U.rate_offered(off, 44100) is None and U.rate_offered(off, None) is None and
       U.rate_offered(dict(off, ua_48k_offered=1), 48000) is None and U.rate_offered(base, 48000) is None and
       U.rate_offered(None, 48000) is None,
       "uac_check: 44100 or no --rate, the option on, a firmware that does not say, no counters: not refused")
    text = "\n".join(U.unit_report(base, dict(after, ua_rate=44100, ua_rate_last_open=44100, ua_48k_offered=0,
                                              ua_48k_refused=1), 60.0, 48000))
    ok("the FM-1 streamed at 44100 Hz, not the 48000 Hz asked for (it does not offer 48 kHz: Options > USB Rate)" in text,
       "uac_check: the option switched off between the check and the capture: the verdict names it")
    # the read itself, through fm1_install's MIDI side (the simulated FM-1): by name; a firmware without the counters
    n = len(I.DEBUG_NAMES)
    cr = BackupSide("ChoralRoot 0.17", I.CR_IDS)
    cr.debug = list(range(n))
    dev = FakeFM1(b"", identity="FM-1_920", bk=lambda _i: cr)
    d, why = U.counters(I.Updater(dev))
    ok(why is None and d["ua_opens"] == I.DEBUG_NAMES.index("ua_opens") and d["ms"] == 0 and all(x.closed for x in dev.links),
       "uac_check: the counters read by name through the tool's MIDI side, the link closed")
    ok("ua_rate" not in d, "uac_check: a firmware without 79 USB: 73's counters alone")
    cr.usb = [44100, 0, 0, 0, 0, 0, 0, 0, 0, 0, 588, 0, 0, 0]
    dev = FakeFM1(b"", identity="FM-1_920", bk=lambda _i: cr)
    d, why = U.counters(I.Updater(dev))
    ok(why is None and d["ua_48k_offered"] == 0 and (U.rate_offered(d, 48000) or "").startswith(
        "the unit does not offer 48 kHz: Options > USB Rate") and U.rate_offered(d, 44100) is None,
       "uac_check against the simulated FM-1 at USB Rate 44.1 kHz: --rate 48000 is refused with the option's name")
    cr.usb[11] = 1
    dev = FakeFM1(b"", identity="FM-1_920", bk=lambda _i: cr)
    d, why = U.counters(I.Updater(dev))
    ok(why is None and d["ua_48k_offered"] == 1 and U.rate_offered(d, 48000) is None,
       "uac_check against the simulated FM-1 at USB Rate 44.1 + 48 kHz: --rate 48000 goes on")
    cr.usb = [48000, 44100, 2, 1, 7, 8, 9, 10, 11, 12, 588]
    dev = FakeFM1(b"", identity="FM-1_920", bk=lambda _i: cr)
    d, why = U.counters(I.Updater(dev))
    ok(why is None and d["ua_rate"] == 48000 and d["ua_rate_last_open"] == 44100 and d["ua_pk588"] == 9 and
       d["ua_rs_max_us"] == 12 and d["ua_opens"] == I.DEBUG_NAMES.index("ua_opens") and all(x.closed for x in dev.links),
       "uac_check: 79 USB's values merged into the read by name (0.19), the link closed")
    cr.usb = None
    cr.debug = list(range(I.DEBUG_NAMES.index("ua_cap_alt")))
    d, why = U.counters(I.Updater(FakeFM1(b"", identity="FM-1_920", bk=lambda _i: cr)))
    ok(d is None and "no USB recording counters" in why, "uac_check: a firmware whose DEBUG ends before the recording's counters: said")
    d, why = U.counters(I.Updater(FakeFM1(b"", identity="ota-FM-1_900", name="Felucca Update")))
    ok(d is None and "update mode" in why, "uac_check: an FM-1 in update mode: no counters, said")
    dev = FakeFM1(b"", identity="FM-1_920")
    dev.connected = False
    d, why = U.counters(I.Updater(dev))
    ok(d is None and "no FM-1 answers" in why, "uac_check: no FM-1: no counters, said")
    w = TMP / "uac.wav"
    import struct
    import wave
    with wave.open(str(w), "wb") as f:
        f.setnchannels(2)
        f.setsampwidth(2)
        f.setframerate(44100)
        tone = struct.pack("<2h", 8000, -8000) * 44100          # 3 s loud, 1 s silent, 2 s loud
        f.writeframes(tone * 3 + bytes(4 * 44100) + tone * 2)
    text = "\n".join(U.wav_report(str(w), 9.0, False, 6))
    ok("what ffmpeg wrote: 6.00 s (2 ch, 44100 Hz) of the 9 s asked = 0.667 (ffmpeg's avfoundation capture is itself short "
       "by 5 to 12 %: not a measure of the FM-1)" in text and "(start s, length s): [(3.0, 1.0)]" in text and
       "chords sent: 6" in text, "uac_check: the WAV: its length as what ffmpeg wrote, the silent gap found")
    ok("no WAV that can be read" in U.wav_report(str(TMP / "none.wav"), 9.0, False, 0)[0], "uac_check: no WAV: said, no traceback")


def stub():
    """--stub test | rescue: 77 STUB, the restart, what comes back (cr_backup.c crb_stub, firmware/stub)"""
    n = len(I.DEBUG_NAMES)
    at = I.DEBUG_NAMES.index("stub_ms")
    keep = I.DELAY["reboot"]
    I.DELAY["reboot"] = 0.3                        # (the fake restarts 50 ms after its reply)
    try:
        cr = BackupSide("ChoralRoot 0.1", I.CR_IDS)
        cr.debug = [0] * (n + I.DEBUG_LOG) + [0, 0, 0, 0x100]
        cr.debug[at:at + 5] = [61, 61000, 2, 1, 0 | 0 << 8 | 1 << 26]
        dev = FakeFM1(b"", identity="FM-1_920", bk=lambda i: None if i.startswith("ota-") else cr)

        def restart(op, back=None):
            cr.log.append(f"stub {op}")
            threading.Timer(0.05, dev.boot, back or (("FM-1_920", "FM-1") if op == 1 else
                                                     ("ota-FM-1_900", "Felucca Update"))).start()
            return 0
        cr.stub = restart
        rc, out, err = cli(["--stub", "test"], dev)
        ok(rc == 0 and cr.log == ["stub 1"] and bytes([0xF0, 0x7D, 0x46, 0x4C, 77, 1, 0xF7]) in dev.sent and
           "stub test: accepted" in out and "boot stub v2: booted the app after 61.0 ms; marker ok, staged loader ok" in out
           and "(the app asked for the test)" in out and "rescue key: OCT- closed in 0 of 32 samples" in out and
           "stub_ms 61" in out and "halves" not in out, "--stub test: 77 STUB 1, the restart, the stub's lines only")
        cr.log.clear()
        rc, out, err = cli(["--stub", "rescue"], dev)
        ok(rc == 0 and cr.log == ["stub 2"] and bytes([0xF0, 0x7D, 0x46, 0x4C, 77, 2, 0xF7]) in dev.sent and
           "update mode: ota-FM-1_900" in out and "switch the FM-1 off and on" in out and "install a package" in out,
           "--stub rescue: 77 STUB 2, update mode reported, how to leave it")
        dev.boot("FM-1_920", "FM-1")
        cr.stub = lambda op: restart(op, ("FM-1_920", "FM-1"))
        rc, out, err = cli(["--stub", "rescue"], dev)
        ok(rc != 0 and "not in update mode" in err, "--stub rescue, the firmware comes back instead: an error")
        dev.boot("FM-1_920", "FM-1")
        cr.stub = lambda op: 3
        rc, out, err = cli(["--stub", "test"], dev)
        ok(rc != 0 and "busy" in err, "--stub while EDIT is open: busy, named")
        cr.stub = lambda op: 127
        rc, out, err = cli(["--stub", "rescue"], dev)
        ok(rc != 0 and "no boot stub of version 2" in err, "--stub on a firmware whose stub is older: refused (127)")
        rc, out, err = cli(["--stub", "test"], FakeFM1(b"", identity="FM-1_910",
                                                       bk=lambda _i: BackupSide("FELUCCA 1.0", I.FELUCCA_IDS)))
        ok(rc != 0 and "no answer" in err, "--stub on Felucca: an error")
        try:
            cli(["--stub", "test", "--info"], dev)
            ok(False, "--stub with --info refused")
        except SystemExit:
            ok(True, "--stub with --info refused")

        # an answer that is not seen does not decide (the unit had taken requests whose answer never came): what
        # comes back does, by its uptime (DEBUG's first value, ms) and the stub's own words
        def mute(op, leaves, ms):
            cr.log.append(f"stub {op}")
            if leaves:                            # off the bus without a word, back 0.3 s later
                dev.connected = False
                threading.Timer(0.3, dev.boot, ("FM-1_920", "FM-1")).start()
            cr.debug[0] = ms
        dev.boot("FM-1_920", "FM-1")
        cr.log.clear()
        cr.stub = lambda op: mute(op, True, 400)
        t = time.monotonic()
        rc, out, err = cli(["--stub", "test"], dev)
        ok(rc == 0 and cr.log == ["stub 1"] and "no answer was seen (the FM-1 has left the bus)" in out and
           "boot stub v2: booted the app after 61.0 ms" in out and "(the app asked for the test)" in out and
           time.monotonic() - t < 1.5, "--stub test, no answer but the FM-1 leaves and comes back just started: the stub's lines, exit 0")
        cr.stub = lambda op: mute(op, True, 600000)
        rc, out, err = cli(["--stub", "test"], dev)
        ok(rc == 4 and "the FM-1 was disconnected" in err, "--stub test, no answer, the FM-1 leaves and is back without a restart (its uptime): exit 4")
        cr.stub = lambda op: mute(op, False, 600000)
        rc, out, err = cli(["--stub", "test"], dev)
        ok(rc == 7 and "no answer to backup command 77" in err and "the FM-1 is still there" in out,
           "--stub test, no answer and no restart: exit 7 as before")
        cr.debug[at + 4] = 0
        cr.stub = lambda op: mute(op, True, 400)
        rc, out, err = cli(["--stub", "test"], dev)
        ok(rc == 7 and "its boot stub did not see the test request" in err, "--stub test, no answer, a restart the stub knows nothing of: exit 7")
        cr.debug[0], cr.debug[at + 4] = 0, 1 << 26
        vals, asked = cr.debug, []

        def leaves_when_asked():                  # no answer to 77, still there; it leaves as DEBUG is asked, and
            asked.append(1)                       # is back 0.3 s later, just started
            if len(asked) == 1:
                dev.connected = False
                threading.Timer(0.3, dev.boot, ("FM-1_920", "FM-1")).start()
                return None
            return [200] + vals[1:]
        cr.stub, cr.debug = (lambda op: None), leaves_when_asked
        rc, out, err = cli(["--stub", "test"], dev)
        ok(rc == 0 and len(asked) == 2 and "no answer was seen (the FM-1 is still there)" in out and
           "boot stub v2: booted the app after 61.0 ms" in out,
           "--stub test, no answer, the FM-1 restarts only when it is asked what it did: looked at again, exit 0")
        cr.debug = vals
    finally:
        I.DELAY["reboot"] = keep


def screen():
    cr = BackupSide("ChoralRoot 0.1", I.CR_IDS)
    dev = FakeFM1(b"", identity="FM-1_920", bk=lambda _i: cr)
    rc, out, err = cli(["--screen", "off"], dev)
    rc2, out2, err2 = cli(["--screen", "on"], dev)
    ok(rc == 0 and rc2 == 0 and "screen off" in out and "screen on" in out2 and cr.log == ["screen 0", "screen 1"] and
       dev.sent[-1] == bytes([0xF0, 0x7D, 0x46, 0x4C, 74, 1, 0xF7]),
       "--screen off / on: 74 SCREEN F0 7D 46 4C 4A 00|01 F7, rc 0")
    rc, out, err = cli(["--screen", "off"], FakeFM1(b"", identity="FM-1_910", bk=lambda _i: BackupSide("FELUCCA 1.0", I.FELUCCA_IDS)))
    ok(rc != 0 and "no answer" in err, "--screen on Felucca (no SCREEN): an error")
    try:
        cli(["--screen", "off", "--info"], dev)
        ok(False, "--screen with --info refused")
    except SystemExit as e:
        ok(e.code == 2, "--screen goes alone")


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
       lines["U05"].split() == ["U05", "WARM", "ANALOG", "retired", "+"] and lines["U06"] == "U06  (empty)" and
       lines["U17"].split() == ["U17", "BELL", "PHASE", "retired", "+"] and lines["U20"] == "U20  (empty)" and
       len({ln.index(w) for ln, w in ((lines["U01"], "VA"), (lines["U02"], "FM6"), (lines["U03"], "CZ-1"))}) == 1 and
       not any(isinstance(x, int) for x in side.log), "--sounds: 32 slots, names, engines, patch / no patch / retired (ANALOG; PHASE since 0.18), invalid -> (empty)")

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
       base64.b64decode(e["patch"]["data"]) == qb + bytes(16),
       "--export-sound of a QUAD sound (a store of before the pool: version 3): patch kind quad, 96 bytes (zero-padded)")
    rc, out, err = cli(["--delete-sound", "10", "--yes"], dev)
    ok(rc == 0 and side.log[2:] == [22, 6] and not int.from_bytes(side.objs[22][8:12], "little"), "--delete-sound of a QUAD sound: its bit cleared, the bank last")
    badq = []
    q4 = b"Q\x04" + bytes(94)
    for data, word in ((b"R" + qb[1:], "bytes, not 96"), (qb[:1] + b"\x04" + qb[2:], "bytes, not 96"), (qb[:79], "bytes, not 96"),
                       (b"R" + q4[1:], "FM TONE"), (q4[:1] + b"\x06" + q4[2:], "FM TONE"), (qb + b"\x01" + bytes(15), "FM TONE")):
        try:
            I.read_sound_file({**json.loads(fq.read_text()), "patch": {"kind": "quad", "data": base64.b64encode(data).decode()}})
            badq.append((word, "accepted"))
        except I.InstallError as ex:
            if word not in str(ex):
                badq.append((word, str(ex)))
    ok(not badq, f"read_sound_file: a QUAD blob's magic / version / length refused{'; wrong: ' + repr(badq) if badq else ''}")
    qv1 = base64.b64decode("UQEBAwMjGgoAP2RkZGQAUBhIAC4ALAABAQEAAQFkACgAQAAofwAAQAAAAH8AYCg+SFADAAAAAABAUAMAAAAAAEBQAwAAAAAAQAAAAAAAAAA=")   # a version 1 blob (a 0.14 dev build's EP)
    ok(qb[1] == 3 and all(I.read_sound_file({**json.loads(fq.read_text()), "patch": {"kind": "quad", "data": base64.b64encode(d).decode()}})[3][1] ==
                          (d + bytes(96 - len(d)))[:96] for d in (qv1, qb[:1] + b"\x02" + qb[2:], qb, q4)),
       "read_sound_file: QUAD blob versions 1..3 (80 bytes: zero-padded to 96) and 4 (96) accepted")

    fel = BackupSide("FELUCCA 1.0", I.FELUCCA_IDS, {6: sound_bank({0: pad})}.items())
    dev = FakeFM1(b"", identity="FM-1_910", bk=lambda _i: fel)
    rc, out, err = cli(["--sounds"], dev)
    ok(rc == 7 and "ChoralRoot" in err and "FELUCCA 1.0" in err, "--sounds on Felucca: exit 7, needs ChoralRoot")

    ok(usage(["--export-sound", "0", "x.json"]) == 2 and
       usage(["--rename-sound", "a", "X"]) == 2 and usage(["--delete-sound", "129"]) == 2, "N outside 1..128: a usage error (exit 2)")
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
       ff["14"] == 1 and ff["12"] == 0 and not any(e in fp for e in ("0", "1", "4", "10")) and I.pool_of(1) == 12 and
       I.pool_of(0) == 13 and I.pool_of(8) == 8 and len(fp["8"]) == 8 and I.pool_of(14) == 14,
       "binding: the factory table (the firmware's, --check), retired engines' pools (ANALOG's: the VA's)")
    ok(sorted(fp, key=int) == ["7", "8", "12", "13", "14", "15", "16"] and all(I.pool_of(e) == 13 and I.engine_retired(e) for e in (2, 3, 5, 6, 9, 11)) and
       not any(I.engine_retired(e) for e in (7, 8, 12, 13, 14, 15, 16)) and I.ENGINE_NAMES[2] == "PHASE" and I.ENGINE_NAMES[11] == "NOISE" and
       I.ENGINE_NAMES[9] == "PHYS",
       "0.18: five engines with a pool, 0.22 seven (P5, GRAIN); PHASE, LOFI, VOICE, TRIO, NOISE, PHYS retired (the VA's pool), their names kept")
    p5n, p5i = fp["16"], I.FACTORY_IDS["16"]
    ok(ff["16"] == 1 and p5n[0] == "INIT P5" and len(p5i) == len(p5n) and p5i[0] == 0 and len(set(p5i[1:])) == len(p5i) - 1 and
       I.pool_of(16) == 16 and all(1 <= x <= 200 for x in p5i[1:]),
       f"binding: P5's pool (3y80): INIT P5, then the {len(p5n) - 1} shipped programs, each with its Sequential number (the favourites' key)")
    pobjs = {6: sound_bank({0: bind(sound_rec(9, "MY PLUCK", seed=21), 3)}), 7: b"", 9: b"", 10: b"", 11: b"", 12: b"", 13: b""}
    pt = I.parse_sound_objects(pobjs)
    ok(pt[0]["used"] and pt[0]["bound"] is None and pt[0]["added"] and pt[0]["engineName"] == "PHYS" and
       I.sound_line(pt[0]).split() == ["U01", "MY", "PLUCK", "PHYS", "retired", "+"] and
       I.read_sound_file(json.dumps(I.export_sound(pobjs, 1, "ChoralRoot 0.18")))[2] == 9,
       "0.18: a PHYS record: kept, listed 'retired', an added preset (the VA's pool), exports and reads as PHYS")
    js = (ROOT / "web" / "fm1sounds.js").read_text()
    jf = re.search(r"^export const FACTORY_PRESETS = (.*);$", js, re.M)
    js1 = re.search(r"^export const FACTORY_FIRST = (.*);$", js, re.M)
    jsi = re.search(r"^export const FACTORY_IDS = (.*);$", js, re.M)
    ok(jf and js1 and jsi and json.loads(jf.group(1)) == fp and json.loads(js1.group(1)) == ff and json.loads(jsi.group(1)) == I.FACTORY_IDS,
       "binding: fm1_install.py and fm1sounds.js embed the same factory table (and P5's Sequential numbers)")
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
            8: bind(sound_rec(0, "GRID", ver=5, seed=10), 1),    # U09 a drum grid record: never bound
            9: bind(sound_rec(2, "PD BRASS", seed=11), 1),       # U10 PHASE (retired in 0.18) over its BRASS: the VA's
            10: sound_rec(11, "WIND", seed=12)}                  #     pool, added (not the VA's first); U11 NOISE: added
    objs = {6: sound_bank(recs), 7: b"", 9: b"", 10: b"", 11: b"", 12: b"", 13: b""}
    t = I.parse_sound_objects(objs)
    ok(t[9]["used"] and t[9]["bound"] is None and t[9]["added"] and t[9]["engineName"] == "PHASE" and t[10]["added"] and
       I.sound_line(t[9]).split() == ["U10", "PD", "BRASS", "PHASE", "retired", "+"] and
       I.sound_line(t[10]).split() == ["U11", "WIND", "NOISE", "retired", "+"] and
       I.export_sound(objs, 10, "ChoralRoot 0.18")["engineName"] == "PHASE" and
       I.read_sound_file(json.dumps(I.export_sound(objs, 10, "ChoralRoot 0.18")))[2] == 2,
       "0.18: a PHASE / NOISE record: kept, listed 'retired', an added preset (the VA's pool), exports and reads as PHASE")
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
       I.sound_line(t[1]).split()[-1] == "+" and I.sound_line(t[8]).split() == ["U09", "GRID", "ANALOG", "retired", "+"],
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
    va4, why = bytes([0x56, 4]) + bytes(108), ""     # (0.22's VA version 4: a firmware before the pool would play INIT)
    try:
        I._set_blob({}, "va", 1, va4)
    except I.InstallError as e:
        why = str(e)
    ok("older format" in why and "device manager" in why and I._set_blob({}, "va", 1, bytes([0x56, 3]) + bytes(108)) is None,
       "a VA version 4 into a firmware before the pool: refused with the reason (version 3: taken)")
    rec, _n, _e, _p = I.sound_from_syx("cz", bytes(144))
    ok(rec[175] == 0xA6 and rec[191] == 0 and I.sound_record_valid(rec) and
       I.sound_bindings([rec]) == [None], "binding: a .syx import's record: the mark, bound to nothing (added)")
    ok("added" in I.__doc__ and "over FM6 02 FM BELL" in I.__doc__, "binding: --help says what --sounds shows")
    # 3y78 (0.22, the pool): U011..U020 FM TONE sounds migrated from 0.21 (an 80-byte version-3 patch, header 16 / 80).
    # --delete-sound U015 writes that bank alone, U015 384 zeros, the other nine as read; a sound imported into a slot
    # starts from an empty slot (a patch header of no kind left there is not kept)
    bk = bytearray(I.POOL_BANK)
    for j in range(10):
        o = j * I.POOL_SLOT
        bk[o:o + I.SOUND_REC] = sound_rec(15, f"TONE {j + 11}", seed=j)
        bk[o + I.POOL_HDR], bk[o + I.POOL_HDR + 1] = 16, 80
        bk[o + I.POOL_DATA:o + I.POOL_DATA + 80] = bytes([0x51, 3]) + bytes(78)
    at51 = lambda b: {i: (bytes(b) if i == 51 else b"") for i in I.POOL_IDS}
    ch = I.delete_sound(at51(bk), 15)
    db = ch[0][1] if [i for i, _ in ch] == [51] else b""
    ok(db[4 * 384:5 * 384] == bytes(384) and db[:4 * 384] == bk[:4 * 384] and db[5 * 384:] == bk[5 * 384:],
       "pool (3y78): delete U015 among 80-byte FM TONE slots: bank 51 alone, U015 384 zeros, the other nine as read")
    bk[5 * 384 + I.POOL_HDR], bk[5 * 384 + I.POOL_HDR + 1] = 9, 33   # U016: a patch header of no kind (GRAIN's)
    wheel = sound_rec(7, "WHEEL", seed=3)
    ch = I.import_sound(at51(bk), 16, (wheel, "WHEEL", 7, None))
    pb = ch[0][1] if [i for i, _ in ch] == [51] else b""
    ok(pb[5 * 384:5 * 384 + 192] == wheel and pb[5 * 384 + 192:6 * 384] == bytes(192),
       "pool (3y78): a sound imported into a slot starts from an empty slot (a stale header of no kind gone)")


# ------------------------------------------------ MIDI calls that never return ---
# fm1_install.py "MIDI": a close, an open or a port list that does not return must not hold the tool. Two levels: a
# simulated backend whose calls block (bounded(), Link: any backend), and the tool's own backend, its worker
# processes run with tests/fake_rtmidi's rtmidi, whose calls block with the GIL held as python-rtmidi's do.

class StuckLink(FakeLink):
    def close(self):                              # never returns
        threading.Event().wait()


class StuckFM1(FakeFM1):
    """a FakeFM1 with a call that never returns: stuck = "close" (every link's), "open" (after the first `after`
    opens) or "names" (the port list)"""

    def __init__(self, *a, stuck="close", after=0, **k):
        super().__init__(*a, **k)
        self.stuck, self.after, self.opens = stuck, after, 0

    def open(self, in_name, out_name):
        self.opens += 1
        if self.stuck == "open" and self.opens > self.after:
            threading.Event().wait()
        link = super().open(in_name, out_name)
        if self.stuck == "close":
            link.__class__ = StuckLink
        return link

    def input_names(self):
        if self.stuck == "names":
            threading.Event().wait()
        return super().input_names()

    output_names = input_names


def timed(args, dev, **k):
    t = time.monotonic()
    rc, out, err = cli(args, dev, **k)
    return rc, out, err, time.monotonic() - t


def stub_debug():
    """--debug's numbers of a unit whose stub (version 2) has just answered a test request"""
    at = I.DEBUG_NAMES.index("stub_ms")
    v = [0] * (len(I.DEBUG_NAMES) + I.DEBUG_LOG) + [0, 0, 0, 0x100]
    v[at:at + 5] = [61, 61000, 2, 1, 1 << 26]
    return v


def stuck():
    keep = {k: I.DELAY[k] for k in ("port", "close", "reboot", "wait_reboot")}
    I.DELAY.update(port=0.4, close=0.2, reboot=0.3, wait_reboot=1)     # (the fake restarts 50 ms after its reply)
    try:
        def unit(**k):
            cr = BackupSide("ChoralRoot 0.17", I.CR_IDS)
            cr.debug = stub_debug()
            dev = StuckFM1(b"", identity="FM-1_920", bk=lambda i: None if i.startswith("ota-") else cr, **k)
            cr.stub = lambda op: threading.Timer(0.05, dev.boot, ("FM-1_920", "FM-1")).start() or 0
            return dev

        # a close that never returns: waited for at most 2 x DELAY close, then left; the result is the same
        rc, out, err, dt = timed(["--info"], unit())
        ok(rc == 0 and "FM-1_920" in out and "ChoralRoot 0.17" in out and dt < 3, f"--info, a close that never returns: the result, exit 0 ({dt:.1f} s)")
        rc, out, err, dt = timed(["--debug"], unit())
        ok(rc == 0 and "boot stub v2" in out and dt < 3, f"--debug, a close that never returns: the result, exit 0 ({dt:.1f} s)")
        dev = unit()
        rc, out, err, dt = timed(["--stub", "test"], dev)
        ok(rc == 0 and "stub test: accepted" in out and "boot stub v2: booted the app after 61.0 ms" in out and
           "(the app asked for the test)" in out and len(dev.links) == 2 and dt < 4,
           f"--stub test, the closes after the restart and at the end never return: the stub's lines, exit 0 ({dt:.1f} s)")
        raw = package()
        p = pkgfile("stuck.fwsc", raw)
        dev = StuckFM1(I.logical_image(raw))
        rc, out, err, dt = timed([p, "--yes"], dev)
        ok(rc == 0 and dev.bad == 0 and "done: the FM-1 runs FM-1_900" in out and dt < 8,
           f"install, every close never returns (two restarts): done, exit 0 ({dt:.1f} s)")
        t = time.monotonic()
        link = I.Link(StuckLink(dev, dev.gen))
        link.close()
        d1 = time.monotonic() - t
        link.close()
        d2 = time.monotonic() - t - d1
        ok(0.3 < d1 < 1.5 and d2 < 0.1, f"Link.close: waited for 2 x DELAY close ({d1:.2f} s), once ({d2:.2f} s)")

        # an open, a port list that never returns: the FM-1 is "not found" (3), with what did not return
        rc, out, err, dt = timed(["--info"], unit(stuck="open"))
        ok(rc == 3 and "FM-1 not found" in err and "the opening of FM-1 did not return" in err and dt < 4,
           f"--info, an open that never returns: exit 3, 'FM-1 not found', what did not return ({dt:.1f} s)")
        rc, out, err, dt = timed(["--info"], unit(stuck="names"))
        ok(rc == 3 and "FM-1 not found" in err and "the list of the MIDI ports did not return" in err and dt < 5,
           f"--info, a port list that never returns: exit 3, what did not return ({dt:.1f} s)")
        rc, out, err, dt = timed([p, "--yes"], StuckFM1(I.logical_image(raw), stuck="open"))
        ok(rc == 3 and "not found" in err and dt < 4, f"install, an open that never returns: exit 3, nothing sent ({dt:.1f} s)")

        # after a restart: the wait has one bound and says what it waited for
        rc, out, err, dt = timed(["--stub", "test"], unit(stuck="open", after=1))
        ok(rc == 5 and "did not come back" in err and "waited 1 s for a MIDI port that answers the identity query" in err
           and "the opening of FM-1 did not return in time" in err and dt < 6,
           f"--stub test, every open after the restart never returns: exit 5 after the wait's bound ({dt:.1f} s)")
        dev = unit()
        dev.stuck = None
        dev.bk("FM-1_920").stub = lambda op: threading.Timer(0.05, setattr, (dev, "connected", False)).start() or 0
        for what, word in (("test", "the FM-1 did not come back"), ("rescue", "nothing came back on USB-MIDI")):
            dev.boot("FM-1_920", "FM-1")
            rc, out, err, dt = timed(["--stub", what], dev)
            ok(rc == 5 and word in err and "waited 1 s for a MIDI port that answers the identity query" in err and
               "did not return" not in err and 1 < dt < 4,
               f"--stub {what}, the FM-1 stays away: exit 5 after 1 s, what was waited for ({dt:.1f} s)")
        dev = StuckFM1(I.logical_image(raw), stuck=None, after_write="FM-1_900")
        boot = dev.boot
        dev.boot = lambda identity, name: boot(identity, name) or (setattr(dev, "connected", False) if name == "Felucca" else None)
        rc, out, err, dt = timed([p, "--yes"], dev)
        ok(rc == 5 and "the FM-1 did not come back (waited 1 s for a MIDI port that answers the identity query as the "
           "firmware)" in err and dt < 8, f"install, the FM-1 stays away after the write: exit 5, what was waited for ({dt:.1f} s)")
        t = time.monotonic()
        back = I.Updater(unit(stuck="names")).wait_for(lambda i: True, 1.0)
        dt = time.monotonic() - t
        ok(back is None and 1.0 <= dt < 3.5, f"wait_for with a port list that never returns: back after its bound ({dt:.1f} s)")
    finally:
        I.DELAY.update(keep)


def midi_side():
    """the tool's own backend: workers (fm1_install.py --midi-worker) with tests/fake_rtmidi as their rtmidi"""
    env = {k: os.environ.get(k) for k in ("PYTHONPATH", "FAKE_RTMIDI", "FAKE_RTMIDI_PIDS", "FAKE_RTMIDI_STATE")}
    keep = {k: I.DELAY[k] for k in ("port", "close", "stuck", "open", "reboot")}
    os.environ["PYTHONPATH"] = os.pathsep.join(filter(None, [str(ROOT / "tests" / "fake_rtmidi"), env["PYTHONPATH"]]))
    I.DELAY.update(close=0.3, stuck=5, open=0.01, reboot=0.05)

    def fresh(knobs=""):                          # a new port-list worker too: it read its knobs at its start
        os.environ["FAKE_RTMIDI"] = knobs
        if I.MidiBackend.lister:
            I.MidiBackend.lister.close()
        I.MidiBackend.lister = None

    def tool(args, knobs="", port=10.0):
        fresh(knobs)
        I.DELAY["port"] = 10.0
        if "names" not in knobs and "missing" not in knobs:
            I.MidiBackend().input_names()         # (the port-list worker is up before a short bound counts)
        I.DELAY["port"] = port
        return timed(args, None)

    try:
        fresh()
        try:
            names = I.MidiBackend().input_names()
        except (I.InstallError, OSError) as e:
            names = str(e)
        ok(names == ["FM-1 fake"], "MIDI worker: it runs with tests/fake_rtmidi (the port list is the fake's)")
        if names != ["FM-1 fake"]:                # never a real port: nothing is opened without the fake
            return
        rc, out, err, dt = tool(["--info"])
        ok(rc == 0 and "FM-1_920  [running]  port: FM-1 fake  version: ChoralRoot 0.17" in out and
           len(I.Worker.live) == 1, f"MIDI worker: --info through a worker (a note behind every reply), its link's worker gone ({dt:.1f} s)")
        rc, out, err, dt = tool(["--stub", "test"])
        ok(rc == 0 and "boot stub v2: booted the app after 61.0 ms" in out and "(the app asked for the test)" in out,
           f"MIDI worker: --stub test through two workers ({dt:.1f} s)")

        o = I.Updater(I.MidiBackend()).open(None, "FM-1 fake")
        ok(o and o.send(bytes([0x90, 60, 100])) and not o.lost and o.close() is None and len(I.Worker.live) == 1,
           "MIDI worker: an output alone, a note sent (uac_check.py's use)")
        os.environ["FAKE_RTMIDI_NAME"] = "ChoralRoot fake"      # fm1_sample_upload.py looks for Felucca / ChoralRoot
        fresh()
        try:
            import fm1_sample_upload as S
            link = S.Link()
            r = link.req(1, [])
            ok(bytes(r[:15]) == b"ChoralRoot 0.17", "MIDI worker: fm1_sample_upload.py's Link: a request and its reply")
            link.link.close()
        finally:
            del os.environ["FAKE_RTMIDI_NAME"]
            fresh()

        # rtmidi's close never returns and keeps the GIL: the worker cannot answer, it is killed after DELAY close
        rc, out, err, dt = tool(["--info"], "close")
        ok(rc == 0 and "FM-1_920  [running]" in out and len(I.Worker.live) == 1 and dt < 8,
           f"MIDI worker: --info, close_port never returns (GIL held): the result, exit 0, the worker killed ({dt:.1f} s)")
        rc, out, err, dt = tool(["--stub", "test"], "close")
        ok(rc == 0 and "boot stub v2: booted the app after 61.0 ms" in out and len(I.Worker.live) == 1 and dt < 10,
           f"MIDI worker: --stub test, close_port never returns: the stub's lines, exit 0 ({dt:.1f} s)")
        rc, out, err, dt = tool(["--debug"], "close")
        ok(rc == 0 and "boot stub v2" in out and "stub_ms 61" in out and dt < 8, f"MIDI worker: --debug, close_port never returns: the result, exit 0 ({dt:.1f} s)")

        # an install through the workers: three links (the firmware, the loader, the new firmware), one worker each
        raw = package()
        state = TMP / "fake-fm1"
        (TMP / "fake-fm1.image").write_bytes(I.logical_image(raw))
        os.environ["FAKE_RTMIDI_STATE"] = str(state)
        try:
            for knobs, what in (("", "install through the workers"), ("close", "install, close_port never returns"),
                                ("finishgone", "install, the loader's last question comes as it leaves the bus")):
                state.write_bytes(b"FM-1_920")
                rc, out, err, dt = tool([pkgfile("worker.fwsc", raw), "--yes"], knobs)
                ok(rc == 0 and "device:  FM-1_920  (FM-1 fake)  ChoralRoot 0.17" in out and "100%" in out and
                   "done: the FM-1 runs FM-1_900" in out and state.read_bytes() == b"FM-1_900" and
                   not (TMP / "fake-fm1.bad").exists() and len(I.Worker.live) == 1 and dt < 15,
                   f"MIDI worker: {what}: every read answered with the image's bytes, done, exit 0 ({dt:.1f} s)")
        finally:
            del os.environ["FAKE_RTMIDI_STATE"]

        # A restart request (FAKE_RTMIDI vanish): the FM-1 answers and leaves the bus 150 ms later, as the firmware
        # does. The answer is read and the ports are closed by their worker while the FM-1 is still there; an answer
        # that comes with the leaving is not dropped; one that never comes does not decide; a port whose FM-1 is
        # gone is closed by its worker all the same (no worker is killed with its ports open).
        state, log = TMP / "fake-stub", TMP / "fake-stub.log"
        os.environ["FAKE_RTMIDI_STATE"] = str(state)
        keep2 = {k: I.DELAY[k] for k in ("reboot", "wait_reboot")}
        I.DELAY.update(reboot=0.3, wait_reboot=3)   # (the first look after the request: while the FM-1 is away)

        def stubbed(knobs, args=("--stub", "test"), clean=True):
            for f in TMP.glob("fake-stub*") if clean else ():
                f.unlink()
            rc, out, err, dt = tool(list(args), knobs)
            lg = [ln.split() for ln in (log.read_text() if log.exists() else "").splitlines()]
            return rc, out, err, dt, [x for x in lg if x[0] == "open"], [x for x in lg if x[0] == "close"], lg

        try:
            rc, out, err, dt, opens, closes, lg = stubbed("vanish")
            ok(rc == 0 and "stub test: accepted" in out and "boot stub v2: booted the app" in out and
               "(the app asked for the test)" in out and len(opens) == 4 and len(closes) == 4 and
               all(x[3] == "1" for x in closes) and lg[:4] == opens[:2] + closes[:2] and len(I.Worker.live) == 1,
               f"MIDI worker: --stub test, the FM-1 leaves 150 ms after its answer: read, the ports closed before it is gone ({dt:.1f} s)")
            rc, out, err, dt, opens, closes, lg = stubbed("vanish,late")
            ok(rc == 0 and "stub test: accepted" in out and "boot stub v2: booted the app" in out and len(closes) == 4 and
               [x[3] for x in closes[:2]] == ["0", "0"], "MIDI worker: the answer in rtmidi's queue when the port is found "
               "gone: handed over before \"lost\", accepted; the ports of the gone FM-1 closed by their worker")
            rc, out, err, dt, opens, closes, lg = stubbed("vanish,mute")
            ok(rc == 0 and "no answer was seen (the FM-1 has left the bus)" in out and "boot stub v2: booted the app" in out
               and "(the app asked for the test)" in out and len(opens) == 4 and len(closes) == 4 and
               [x[3] for x in closes[:2]] == ["0", "0"],
               f"MIDI worker: --stub test, the answer never comes, the FM-1 leaves and is back just started: the stub's lines, exit 0 ({dt:.1f} s)")
            rc, out, err, dt, opens, closes, lg = stubbed("no77")
            ok(rc == 7 and "no answer to backup command 77" in err and "the FM-1 is still there" in out and
               len(opens) == len(closes) == 4, "MIDI worker: --stub test on a firmware that does not answer 77 (and "
               "does not restart): exit 7 as before")

            # the FM-1 does not come back for CoreMIDI although it is on the USB bus: said, with what to do
            I.DELAY["wait_reboot"] = 1
            rc, out, err, dt, opens, closes, lg = stubbed("vanish,offline")
            hint = "FM-1 fake is on the USB bus, but its MIDI port is known to CoreMIDI as offline"
            ok(rc == 5 and "the FM-1 did not come back" in err and hint in err and "Rescan MIDI" in err and
               "--rescan-midi" in err and "Felucca Update" not in err and "Some Interface" not in err,
               f"MIDI worker: after the restart the port stays offline in CoreMIDI, the FM-1 on USB: exit 5, said with the way out ({dt:.1f} s)")
            rc, out, err, dt, opens, closes, lg = stubbed("vanish,offline", ("--info",), clean=False)
            ok(rc == 3 and "FM-1 not found" in err and hint in err, "MIDI worker: --info then: exit 3, the same words")
            rc, out, err, dt, opens, closes, lg = stubbed("vanish,offline", ("--rescan-midi",), clean=False)
            ok(rc == 0 and "MIDI rescanned. MIDI inputs: FM-1 fake" in out and ["rescan"] in lg,
               "MIDI worker: --rescan-midi: the worker's rescan, the port is listed again")
            rc, out, err, dt, opens, closes, lg = stubbed("vanish,offline", ("--info",), clean=False)
            ok(rc == 0 and "FM-1_920  [running]" in out, "MIDI worker: --info after the rescan: found")
            try:
                cli(["--rescan-midi", "--info"], None)
                ok(False, "--rescan-midi goes alone")
            except SystemExit as e:
                ok(e.code == 2, "--rescan-midi goes alone")

            # a worker whose tool goes away, or that is asked to end (SIGTERM), closes its ports first
            for how in ("stdin", "SIGTERM"):
                for f in TMP.glob("fake-stub*"):
                    f.unlink()
                fresh()
                w = subprocess.Popen([sys.executable, str(ROOT / "tools" / "fm1_install.py"), "--midi-worker", "5"],
                                     stdin=subprocess.PIPE, stdout=subprocess.PIPE)
                w.stdin.write(I.frame(b"o", b'["FM-1 fake", "FM-1 fake"]'))
                w.stdin.flush()
                got = w.stdout.read(5)
                if how == "stdin":
                    w.stdin.close()
                else:
                    w.terminate()
                try:
                    w.wait(8)
                except subprocess.TimeoutExpired:
                    w.kill()
                lg = [ln.split()[::2] for ln in (log.read_text() if log.exists() else "").splitlines()]
                ok(w.returncode == 0 and got == I.frame(b"a") and w.stdout.read() == b"" and
                   lg == [["open", "in"], ["open", "out"], ["close", "in"], ["close", "out"]],
                   f"MIDI worker: its tool gone ({how}): it closes its ports and leaves")
        finally:
            I.DELAY.update(keep2)
            del os.environ["FAKE_RTMIDI_STATE"]

        # a worker is started without a fork of the tool (no copy of it, with its command line, before an exec)
        if getattr(subprocess, "_USE_POSIX_SPAWN", False) and hasattr(subprocess, "_fork_exec"):
            fork, subprocess._fork_exec = subprocess._fork_exec, None
            try:
                fresh()
                ok(I.MidiBackend().input_names() == ["FM-1 fake"], "MIDI worker: started by posix_spawn, not by fork")
            except TypeError:
                ok(False, "MIDI worker: started by posix_spawn, not by fork")
            finally:
                subprocess._fork_exec = fork

        # rtmidi's open, its port list never return: exit 3 with what did not return, no worker left in the call
        rc, out, err, dt = tool(["--info"], "open", port=1.0)
        ok(rc == 3 and "FM-1 not found" in err and "the opening of FM-1 fake did not return within its bound" in err and
           len(I.Worker.live) == 1 and dt < 8, f"MIDI worker: --info, open_port never returns: exit 3 'FM-1 not found' ({dt:.1f} s)")
        rc, out, err, dt = tool(["--info"], "names", port=1.0)
        ok(rc == 3 and "FM-1 not found" in err and "the list of the MIDI ports did not return" in err and
           len(I.Worker.live) <= 1 and dt < 8, f"MIDI worker: --info, get_ports never returns: exit 3, what did not return ({dt:.1f} s)")
        rc, out, err, dt = tool(["--info"], "missing")
        ok(rc == 2 and "needs python-rtmidi" in err, "MIDI worker: no rtmidi to import: exit 2, what to install")

        # a worker whose tool is gone while it is in a call that never returns ends itself (no GIL needed)
        fresh("names")
        w = subprocess.Popen([sys.executable, str(ROOT / "tools" / "fm1_install.py"), "--midi-worker", "0.5"],
                             stdin=subprocess.PIPE, stdout=subprocess.PIPE)
        w.stdin.write(I.frame(b"n"))
        w.stdin.close()
        try:
            w.wait(8)
        except subprocess.TimeoutExpired:
            w.kill()
        ok(w.returncode == 1, "MIDI worker: in a call that never returns, its tool gone: it ends itself after `stuck`")

        # the tool as a process: the exit is not held, and no worker outlives it
        pids = TMP / "worker-pids"
        os.environ["FAKE_RTMIDI"], os.environ["FAKE_RTMIDI_PIDS"] = "close", str(pids)
        t = time.monotonic()
        r = subprocess.run([sys.executable, str(ROOT / "tools" / "fm1_install.py"), "--info"], capture_output=True,
                           text=True, timeout=60)
        dt = time.monotonic() - t

        def alive(pid):
            try:
                os.kill(pid, 0)
                return True
            except ProcessLookupError:
                return False
            except PermissionError:
                return True
        started = [int(x) for x in pids.read_text().split()] if pids.exists() else []
        end = time.monotonic() + 3
        while any(map(alive, started)) and time.monotonic() < end:
            time.sleep(0.05)
        ok(r.returncode == 0 and "FM-1_920  [running]" in r.stdout and "ChoralRoot can be installed over" in r.stdout and
           r.stderr == "" and dt < 20, f"the tool as a process, close_port never returns: its output, exit 0 ({dt:.1f} s)")
        ok(len(started) == 2 and not any(map(alive, started)), f"... and none of its {len(started)} workers outlives it")
    finally:
        fresh()
        I.DELAY.update(keep)
        for k, v in env.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v


def exits():
    """the tool's own process cannot stay behind: leave()'s timer, Watch (fm1_install.py)"""
    head = "import sys, time\nsys.path.insert(0, %r)\nimport fm1_install as I\n" % str(ROOT / "tools")

    def py(body):
        t = time.monotonic()
        r = subprocess.run([sys.executable, "-c", head + body], capture_output=True, text=True, timeout=60)
        return r, time.monotonic() - t
    r, dt = py("I.DELAY['exit'] = 0.5\nclass Held:\n    def flush(self):\n        time.sleep(1000)\n"
               "sys.stdout = Held()\nI.leave(7)\n")
    ok(r.returncode == 7 and dt < 10, f"leave: an output that is never flushed does not hold the exit, the code is kept ({dt:.1f} s)")
    r, dt = py("I.DELAY.update(dead=0.5, exit=0.5)\nI.Watch.start()\ntime.sleep(1000)\n")
    ok(r.returncode == 1 and "no step for 0.5 s: the tool ends itself" in r.stderr and 'File "<string>", line 6' in r.stderr
       and dt < 10, f"Watch: no step for DELAY dead: the tool ends itself (exit 1) and says where it was ({dt:.1f} s)")
    r, dt = py("I.DELAY.update(dead=0.4, exit=0.5)\nI.Watch.start()\nfor k in range(10):\n    time.sleep(0.1)\n"
               "    I.Watch.step()\nimport builtins\nbuiltins.input = lambda p: time.sleep(1.2) or 'y'\n"
               "print(I.ask_tty('?'), I.Watch.paused)\ntime.sleep(0.2)\nI.leave(3)\n")
    ok(r.returncode == 3 and r.stdout.split() == ["True", "False"] and r.stderr == "",
       "Watch: steps keep the tool, a question to the user stops the clock")
    env = dict(os.environ, PYTHONPATH=str(ROOT / "tests" / "fake_rtmidi"), FAKE_RTMIDI="")
    w = subprocess.Popen([sys.executable, str(ROOT / "tools" / "fm1_install.py"), "--info", "--port", "no such port"],
                         stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, env=env)   # (--port: no port is opened)
    w.stderr.close()
    try:
        w.wait(30)
    except subprocess.TimeoutExpired:
        w.kill()
    ok(w.returncode == 3, "an error line that cannot be written (stderr's reader is gone): the exit code is still 3")


def favorites():
    """--favorites (docs/PRESETS.md "Favourites"): the object as the FIRMWARE's own writer makes it (tests/cr_favs_test.c
    --emit: cr_favs.c crf_pack through storage.c, compiled here), decoded by the tool; the text; the list made of it"""
    import subprocess
    exe = TMP / "cr_favs_emit"
    blob = TMP / "favs.bin"
    c = subprocess.run(["cc", "-std=c11", "-O1", "-w", "-o", str(exe), str(ROOT / "tests/cr_favs_test.c")], capture_output=True, text=True)
    e = subprocess.run([str(exe), "--emit", str(blob)], capture_output=True, text=True) if c.returncode == 0 else c
    ok(c.returncode == 0 and e.returncode == 0 and blob.exists(), "favorites: the firmware's writer on the host wrote the object"
       + ("" if c.returncode == 0 else ": " + c.stderr[-300:]))
    data = blob.read_bytes() if blob.exists() else b""
    want = {12: [0, 24], 13: [3], 14: [64], 15: [15], 7: [4], 16: [1, 15, 44, 199, 200], I.FAV_USER: [0, 31]}
    ok(I.parse_favorites(data) == want and data[:1] == bytes([I.FAV_VERSION]) and I.FAV_VERSION == 2,
       "favorites: the tool's decoder reads the firmware's bytes (version 2): every mark of the fixed set, nothing else")
    marks, types, filt = I.parse_favorites_full(data)
    ok(marks == want and filt is True and
       types == {(12, 24): "chord", (13, 3): "bass", (16, 15): "chord", (16, 44): "bass", (I.FAV_USER, 31): "bass",
                 (12, 5): "bass", (16, 7): "chord"},
       "favorites: .. and the types (Chord / Bass; the others Both), those of two sounds that are not marked, the filter on")
    v1 = bytes([1, 3, 0x80, 1, 0x04, 12, 1, 0x09, 16, 2, 0x00, 0x10])
    ok(I.parse_favorites_full(v1) == ({0x80: [2], 12: [0, 3], 16: [12]}, {}, False),
       "favorites: a version-1 object (0.22's first builds): its marks, every one Both, no filter")
    ok(I.parse_favorites_full(bytes([2, 3, 12, 1, 3, 12 | 0x40, 1, 3, 12 | 0x20, 1, 2])) == ({12: [0, 1]}, {(12, 0): "chord"}, False),
       "favorites: Chord and Bass at once (a foreign object): Both, as the firmware reads it")
    ok(I.parse_favorites(b"") == {} and I.parse_favorites(bytes([1, 0])) == {} and I.parse_favorites_full(bytes([2, 0])) == ({}, {}, False),
       "favorites: an empty object, no section: no marks")
    for bad, what in ((bytes([3]) + data[1:], "another version"), (data[:-2], "cut short"), (b"\x01", "one byte")):
        try:
            I.parse_favorites(bad)
            ok(False, f"favorites: {what} must be refused")
        except I.BackupError:
            ok(True, f"favorites: {what}: refused")
    newer = bytes([1, 3, 99, 2, 0xFF, 0xFF, 16, 28]) + b"\xFF" * 28 + bytes([12, 1, 2])
    ok(I.parse_favorites(newer) == {99: list(range(16)), 16: list(range(1, 201)), 12: [1]},
       "favorites: a newer object: P5's ids 1..200 only, an unknown section listed by its number")

    objs = {I.FAV_ID: data, 6: sound_bank({0: sound_rec(13, "MY PAD", seed=1)}), 7: sound_bank({15: sound_rec(12, "LAST", seed=2)})}
    side = BackupSide("ChoralRoot 0.22", I.CR_IDS, objs.items())
    rc, out, err = cli(["--favorites"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: side))
    lines = out.splitlines()
    ok(rc == 0 and lines[0] == "# favorites on ChoralRoot 0.22: 13 marked, the filter on" and
       lines[1:8] == ["# FM6 01 TINE EP", "# FM6 25 PIANO  (chord)", "# FM TONE 16 NOISE-ISH", "# VA 04 SLOW STRINGS  (bass)", "# CZ-1 64 TYPHOON",
                      "# WHEEL 05 ROCK DRIVE", "# user U01 MY PAD"] and lines[8] == "# user U32 LAST  (bass)",
       "--favorites: the engines' marked presets by their pool place and name, the marked user sounds by slot and name; "
       "a mark typed Chord or Bass says so after the name")
    ok(lines[9].startswith("# P5: 5 programs") and [ln.split("#")[0].split() for ln in lines[10:]] == [["1"], ["15"], ["44"], ["199"], ["200"]] and
       lines[10] == "1      # ITS PROPHET5" and lines[11] == "15     # REEDY STRPAD  (chord)" and lines[12].endswith("  (bass)") and len(lines) == 15,
       "--favorites: P5's programs by Sequential's number, one a line with the name (and the type) as a comment")
    ok("FM6 06" not in out and not any(ln.split("#")[0].split() == ["7"] for ln in lines),
       "--favorites: a sound that is typed and not marked (FM6 06, P5 7) is not listed")

    # --restore-favorites (3y54): text -> object, packed as the firmware's crf_pack packs it
    ok(I.pack_favorites(marks, types, filt) == data, "--restore-favorites: the tool's packer = the firmware's writer, byte for byte")
    tm, tt, tf, sk = I.parse_favorites_text(out)
    marked = {k: v for k, v in types.items() if k[1] in marks.get(k[0], [])}
    want_obj = I.pack_favorites(marks, marked, True)
    ok((tm, tt, tf, sk) == (marks, marked, True, []) and I.pack_favorites(tm, tt, tf) == want_obj,
       "--restore-favorites: the --favorites text decodes to the marks, the types of the marked sounds and the filter (engines, user, P5)")
    fav = TMP / "favs-restore.txt"
    fav.write_text(out)
    dst = BackupSide("ChoralRoot 0.22", I.CR_IDS, [(I.FAV_ID, b"")])
    rc, o2, err = cli(["--restore-favorites", str(fav)], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: dst), answer=False)
    ok(rc == 1 and "cancelled" in o2 and "13 marks" in o2 and dst.log == [], "--restore-favorites: says what it will write, asks; no: nothing written")
    rc, o2, err = cli(["--restore-favorites", str(fav), "--yes"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: dst))
    ok(rc == 0 and "13 marks restored" in o2 and dst.log == [I.FAV_ID] and dst.objs[I.FAV_ID] == want_obj and dst.restarts == 0,
       "--restore-favorites --yes: a PUT of object 23 alone, read back, no restart" + ("" if rc == 0 else ": " + err))
    dst.objs[6] = objs[6]
    rc, o3, err = cli(["--favorites"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: dst))
    ok(rc == 0 and [ln for ln in o3.splitlines() if "U32" not in ln] == [ln for ln in out.splitlines() if "U32" not in ln],
       "--restore-favorites: text -> object -> --favorites text = the input (U32's name lives in a bank not given here)")
    for ver in ("ChoralRoot 0.21", "Felucca 1.0"):
        bad = BackupSide(ver, I.CR_IDS, [(I.FAV_ID, b"")])
        rc, _o, err = cli(["--restore-favorites", str(fav), "--yes"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: bad))
        ok(rc != 0 and bad.log == [], f"--restore-favorites on {ver}: refused, nothing written")
    ok(I.favorites_text({16: [3]}, "X", types={(16, 3): "bass"}).splitlines()[-1] == "3      #  (bass)" and
       I.favorites_text({16: [3]}, "X").splitlines()[-1] == "3", "--favorites: a P5 line without the bank's names: the number, the type in the comment")
    ok(not any(isinstance(x, int) for x in side.log) and side.restarts == 0, "--favorites: nothing written to the FM-1")
    sys.path.insert(0, str(ROOT / "tools"))
    import gen_prophet_factory as g
    import p5_shipped_from_favorites as pf
    fav = TMP / "favorites.txt"
    fav.write_text(out)
    ok(g.shipped(str(fav)) == [1, 15, 44, 199, 200], "--favorites: its output is a shipped list as it is (gen_prophet_factory.py reads it)")
    # a grouped list as the present one (the tree's is by number since 3y51: the groups are tested here, not the tree)
    cur = ("# a list grouped by hand\n\n# ---- pads, strings, voices\n1      # ITS PROPHET5: the first\n15     # REEDY STRPAD\n"
           "44     # CARS STRINGS\n\n# ---- brass\n6      # STABBY BRASS\n")
    text = pf.build(pf.marked(out), cur)
    new = TMP / "shipped.txt"
    new.write_text(text)
    body = [ln for ln in text.splitlines() if ln and not ln.startswith("# ") and ln != "#"]
    ok(g.shipped(str(new)) == [1, 15, 44, 199, 200] and "# ---- pads, strings, voices" in text.splitlines() and
       "# ---- brass" not in text and body[:3] == [ln for ln in cur.splitlines() if ln.split("#")[0].split() in (["1"], ["15"], ["44"])] and
       "# ---- marked on the unit, not in the list before (by Sequential number)" in text and len(body) == 5 and
       body[3].startswith("199 ") and body[4].startswith("200 "),
       "p5_shipped_from_favorites: the marked programs of the present list in its order under its headings, the new ones after, by number")
    by = pf.build([200, 3, 1], "", by_number=True)
    new.write_text(by)
    ok(g.shipped(str(new)) == [1, 3, 200], "p5_shipped_from_favorites --by-number: by Sequential number")
    try:
        pf.build([], "")
        ok(False, "an empty list must be refused")
    except ValueError:
        ok(True, "p5_shipped_from_favorites: no P5 mark: refused (not an empty list)")
    old = BackupSide("ChoralRoot 0.21", [i for i in I.CR_IDS if i != I.FAV_ID])
    rc, out, err = cli(["--favorites"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: old))
    ok(rc == 7 and "keeps no favourites" in err, "--favorites on ChoralRoot 0.21 (no object 23): exit 7, says so")
    # a backup made on 0.22 restored onto 0.21: the favourites are skipped (0.21 does not list 23), the rest goes in
    import json
    full = BackupSide("ChoralRoot 0.22", I.CR_IDS, {1: per(764, I.PER5, 1), 6: objs[6], I.FAV_ID: data}.items())
    path = TMP / "fav-backup.json"
    rc, out, err = cli(["--backup", str(path)], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: full))
    f = json.loads(path.read_text()) if path.exists() else {"objects": []}
    ok(rc == 0 and "favorites" in out and any(o["id"] == I.FAV_ID and o["size"] == len(data) for o in f["objects"]),
       "--backup on 0.22: the favourites are in the file (object 23, \"favorites\")")
    rc, out, err = cli(["--restore", str(path), "--yes"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: old))
    ok(rc == 0 and I.FAV_ID not in old.objs and old.objs.get(6) == objs[6] and not any(x == I.FAV_ID for x in old.log) and "favorites" in out,
       "--restore of it onto 0.21: object 23 is never sent (not listed there): reported as kept in the file, the sounds restored")
    fresh = BackupSide("ChoralRoot 0.22", I.CR_IDS)
    rc, out, err = cli(["--restore", str(path), "--yes"], FakeFM1(b"", identity="FM-1_920", bk=lambda _i: fresh))
    ok(rc == 0 and fresh.objs.get(I.FAV_ID) == data, "--restore of it onto 0.22: the favourites are back")


# ------------------------------------------------ several FM-1s, the unit's ID ---
# 3y66 (docs/INSTALL-COMPAT.md "Several FM-1s"): INFO's tag 49 08 and the unit's ID; --info lists every FM-1;
# --unit ID picks one by it; an install with --unit takes update mode only from the one new port; never a MIDI
# rescan with --unit; without --unit, several FM-1s refuse a command that writes or restarts.

class FakeBus:
    """a MIDI backend with several FakeFM1s on it (several FM-1s on one computer): their ports in the system's
    order, two of one name both listed (the tool's port_labels: the second "NAME [2]"); a unit that restarts comes
    back at the end of the list, as a new device. known(): CoreMIDI's view for offline(); rescan() counted"""

    def __init__(self, *units, known=None):
        self.units, self.rescans, self.seen = [], 0, known or {}
        for u in units:
            self.plug(u)

    def plug(self, u):
        u.bus = self
        self.units.append(u)

    def again(self, u):
        if u in self.units:
            self.units.remove(u)
            self.units.append(u)

    def input_names(self):
        return I.port_labels([u.name for u in list(self.units) if u.connected])

    output_names = input_names

    def open(self, in_name, out_name):
        live = [u for u in list(self.units) if u.connected]
        k = I.port_index([u.name for u in live], in_name)
        if k is None or in_name != out_name:
            raise IOError("no such port")
        return live[k].open(live[k].name, live[k].name)

    def known(self):
        return self.seen

    def rescan(self):
        self.rescans += 1
        return None


def cr_unit(uid, image=b"", name="ChoralRoot FM-1", version="ChoralRoot 0.22", objs=(), uid_after=None):
    """a ChoralRoot FM-1 with its unit ID (None: a ChoralRoot from before it); after an install it runs FM-1_920
    again under the same name, with uid_after's ID ("": none) or its own"""
    side = BackupSide(version, I.CR_IDS, objs, uid=uid)
    dev = FakeFM1(image, identity="FM-1_920", name=name, after_write="FM-1_920", name_after=name,
                  bk=lambda i: None if i.startswith("ota-") else side)
    dev.side = side
    if uid_after is not None:
        dev.boot, boot = (lambda identity, n: (setattr(side, "uid", uid_after or None)
                                                if not identity.startswith("ota-") and dev.upgrades == 2 else None)
                          or boot(identity, n)), dev.boot
    return dev


def units():
    import json

    def ucli(args, dev):                          # (a usage error: argparse's exit 2 and its line)
        err = io.StringIO()
        old, sys.stderr = sys.stderr, err
        try:
            return I.main(args, backend=dev, out=io.StringIO(), ask=lambda _p: True), "", err.getvalue()
        except SystemExit as e:
            return e.code, "", err.getvalue()
        finally:
            sys.stderr = old
    # the INFO reply: the ID after the tags, old parsers' version to the NUL
    a = list(b"ChoralRoot 0.22") + [0] * 8 + [0x42, 1, 3, 0x43, 1, 3, 0x49, 8, *b"A1B2C3D4"]
    old = a[:-10]
    fel = list(b"FELUCCA v1.0") + [0, 2, 90, 4, 16, 82, *b"FM6", 0, *b"VA", 0, 4, 16, 0x55, 1, 3, 0x4D, 1, 64, 1, 0x42, 1, 3]
    ok(I.unit_id_of(a) == "A1B2C3D4" and I.unit_id_of(old) is None and I.unit_id_of(fel) is None and
       I.unit_id_of(a[:-1]) is None and I.unit_id_of(a[:-10] + [0x49, 8, *b"a1b2c3d4"]) is None,
       "unit_id_of: 49 08 and 8 hex digits after 42 / 43; none without the tag, cut short, in lower case, Felucca's")
    ok(bytes(a[:a.index(0)]).decode() == "ChoralRoot 0.22" and I.device_info(type("B", (), {"request": lambda s, *x: a})())
       == ("ChoralRoot 0.22", "choralroot") and I.device_info(type("B", (), {"request": lambda s, *x: a})(), with_id=True)
       == ("ChoralRoot 0.22", "choralroot", "A1B2C3D4"), "device_info: the version to the NUL as before; with_id the ID too")
    ok(I.port_labels(["ChoralRoot FM-1", "IAC", "ChoralRoot FM-1"]) == ["ChoralRoot FM-1", "IAC", "ChoralRoot FM-1 [2]"]
       and I.port_index(["ChoralRoot FM-1", "IAC", "ChoralRoot FM-1"], "ChoralRoot FM-1 [2]") == 2 and
       I.port_index(["ChoralRoot FM-1"], "ChoralRoot FM-1 [2]") is None and I.port_base("ChoralRoot FM-1 [2]") ==
       "ChoralRoot FM-1" and I.port_labels(["A", "B"]) == ["A", "B"],
       "port_labels: a name two ports share, the second 'NAME [2]'; one port's name as it is; port_index, port_base")

    # --info: every FM-1, each with its ID; a stock one without
    A, B = cr_unit("A1B2C3D4"), cr_unit("0F0E0D0C")
    S = FakeFM1(b"", identity="FM-1_015", name="FM-1")
    rc, out, err = cli(["--info"], FakeBus(A, B, S))
    ok(rc == 0 and out.count("FM-1_920  [running]") == 2 and "port: ChoralRoot FM-1  version: ChoralRoot 0.22\nunit ID: A1B2C3D4"
       in out and "port: ChoralRoot FM-1 [2]  version: ChoralRoot 0.22\nunit ID: 0F0E0D0C" in out and
       "FM-1_015  [running]  port: FM-1\nunit ID: none (no INFO answer" in out and
       "3 FM-1s connected (unit IDs A1B2C3D4, 0F0E0D0C, none)" in out and A.upgrades == B.upgrades == 0,
       "--info: three FM-1s, two of one port name: each with its port, version and unit ID (the stock one: none)")
    rc, out, err = cli(["--info"], FakeBus(cr_unit("A1B2C3D4")))
    ok(rc == 0 and out == "FM-1_920  [running]  port: ChoralRoot FM-1  version: ChoralRoot 0.22\nunit ID: A1B2C3D4\n"
       "ChoralRoot can be installed over ChoralRoot 0.22\n", "--info, one FM-1: the lines of before and its unit ID")
    rc, out, err = cli(["--info", "--unit", "0f0e0d0c"], FakeBus(A, B, S))
    ok(rc == 0 and "unit ID: 0F0E0D0C" in out and "A1B2C3D4" not in out.split("\n", 1)[1] and "FM-1_015" not in out,
       "--info --unit 0f0e0d0c (any case): that unit alone")

    # --unit picks the right one, for a read and for a write; none, or two answering with it: refused
    A = cr_unit("A1B2C3D4", objs={6: fill(3080, 1)}.items())
    B = cr_unit("0F0E0D0C", objs={6: fill(3080, 2)}.items())
    path = TMP / "b.json"
    rc, out, err = cli(["--backup", str(path), "--unit", "0f0e0d0c"], FakeBus(A, B))
    f = json.loads(path.read_text()) if path.exists() else {"objects": []}
    got = {o["id"]: o["data"] for o in f["objects"] if o["size"]}
    import base64
    ok(rc == 0 and base64.b64decode(got.get(6, "")) == fill(3080, 2) and "unit 0F0E0D0C: ChoralRoot 0.22 (FM-1_920) on port "
       "ChoralRoot FM-1 [2]" in out, "--backup --unit (the second of two of one port name): its objects, said which")
    rc, out, err = cli(["--restore", str(path), "--yes", "--unit", "A1B2C3D4"], FakeBus(A, B))
    ok(rc == 0 and A.side.objs[6] == fill(3080, 2) and A.side.restarts == 1 and not B.side.log and B.side.restarts == 0,
       "--restore --unit: written to that FM-1 alone (the other: nothing written, no restart)")
    puts = lambda: sum(d.startswith(I.BK_HDR + bytes([I.BK_PUT])) for d in A.sent + B.sent)   # noqa: E731
    n0 = puts()
    rc, out, err = cli(["--delete-sound", "1", "--yes", "--unit", "CCCC0003"], FakeBus(A, B))
    ok(rc == 3 and "no FM-1 answers with unit ID CCCC0003" in err and "A1B2C3D4" in err and "0F0E0D0C" in err and
       puts() == n0,
       "--unit an ID no FM-1 has: exit 3, the IDs that answered listed, nothing written")
    twin = cr_unit("A1B2C3D4")
    rc, out, err = cli(["--sounds", "--unit", "A1B2C3D4"], FakeBus(A, twin))
    ok(rc == 9 and "2 ports answer with unit ID A1B2C3D4" in err, "--unit an ID two ports answer with: exit 9, nothing done")
    n0 = len(A.sent)
    rc, out, err = ucli(["--info", "--unit", "A1B2C3"], FakeBus(A))
    ok(rc == 2 and "8 hex digits" in err and len(A.sent) == n0, "--unit not 8 hex digits: exit 2 before any MIDI")

    # without --unit, two FM-1s: a write or a restart refused with their IDs, a read reads the first and says so
    raw = package("FM-1_920")
    image = I.logical_image(raw)
    p = pkgfile("units.fwsc", raw)
    A, B = cr_unit("A1B2C3D4", image), cr_unit("0F0E0D0C", image)
    for args in ([p, "--yes"], ["--delete-sound", "1", "--yes"], ["--restore", str(path), "--yes"], ["--stub", "test"]):
        rc, out, err = cli(args, FakeBus(A, B))
        ok(rc == 9 and "2 FM-1s are connected" in err and "--unit" in err and "A1B2C3D4" in err and "0F0E0D0C" in err and
           A.upgrades == B.upgrades == 0 and not A.side.log and not B.side.log,
           f"two FM-1s, no --unit: {args[0] if args[0] != p else 'an install'} refused (exit 9), both IDs listed, nothing sent but questions")
    rc, out, err = cli(["--sounds"], FakeBus(A, B))
    ok(rc == 0 and "note: 2 FM-1s are connected" in out and "user sounds on ChoralRoot 0.22" in out,
       "two FM-1s, no --unit: --sounds (a read) reads the first, as with one, and says that two are there")
    rc, out, err = cli([p, "--yes"], FakeBus(cr_unit("A1B2C3D4", image), FakeFM1(image, identity="FM-1_015")))
    ok(rc == 9 and "unit ID none" in err, "a ChoralRoot and a stock FM-1, no --unit: an install refused (the stock one: no ID)")

    # an install with --unit: update mode only from the one new port, the ID checked when it is back
    for target in ("second", "first"):
        A, B = cr_unit("A1B2C3D4", image), cr_unit("0F0E0D0C", image)
        T, O = (B, A) if target == "second" else (A, B)
        rc, out, err = cli([p, "--yes", "--unit", T.side.uid.lower()], FakeBus(A, B))
        ok(rc == 0 and T.upgrades == 2 and T.bad == 0 and T.identity == "FM-1_920" and O.upgrades == 0 and
           not any(d == I.UPGRADE for d in O.sent) and f"unit ID {T.side.uid}: the same FM-1 after the restart" in out and
           "done: the FM-1 runs FM-1_920" in out,
           f"install --unit, the {target} of two ports named alike: that FM-1 through its update mode and back, its ID "
           "checked; the other got no upgrade")
    A, B = cr_unit("A1B2C3D4", image), cr_unit("0F0E0D0C", image)
    bus = FakeBus(A, B)
    C = FakeFM1(image, identity="FM-1_015", name="FM-1")
    boot = B.boot
    B.boot = lambda identity, n: boot(identity, n) or (bus.plug(C) if identity.startswith("ota-") else None)
    rc, out, err = cli([p, "--yes", "--unit", "0F0E0D0C"], bus)
    ok(rc == 9 and "more than one new MIDI port appeared" in err and "nothing was written" in err and B.upgrades == 1 and
       B.identity.startswith("ota-") and A.upgrades == 0 and C.upgrades == 0,
       "install --unit, two new ports at the restart (another FM-1 plugged in): exit 9, the loader never asked to write")
    A, B = cr_unit("A1B2C3D4", image), cr_unit("0F0E0D0C", image)
    bus = FakeBus(A, B)
    L = FakeFM1(image, identity="ota-FM-1_900", name="Felucca Update")
    L.connected = False
    bus.plug(L)
    B.boot = lambda identity, n: setattr(L, "connected", True) if identity.startswith("ota-") else None
    rc, out, err = cli([p, "--yes", "--unit", "0F0E0D0C"], bus)
    ok(rc == 9 and "is still there" in err and "nothing was written" in err and L.upgrades == 0 and B.upgrades == 1,
       "install --unit, a new port while the FM-1's own stays: exit 9, nothing written")

    # the firmware installed has no ID: the one new port of another name is taken (said so); one of the other
    # FM-1's name cannot be told apart (exit 5)
    A, B = cr_unit("A1B2C3D4", image), cr_unit("0F0E0D0C", image, uid_after="")
    B.name_after = "FM-1"
    rc, out, err = cli([p, "--yes", "--unit", "0F0E0D0C"], FakeBus(A, B))
    ok(rc == 0 and B.upgrades == 2 and "unit ID: none from FM-1_920 ChoralRoot 0.22" in out and "taken as this FM-1" in out,
       "install --unit, a firmware without the ID after it under a new port name: taken as the unit, said so")
    A, B = cr_unit("A1B2C3D4", image), cr_unit("0F0E0D0C", image, uid_after="")
    rc, out, err = cli([p, "--yes", "--unit", "0F0E0D0C"], FakeBus(A, B))
    ok(rc == 5 and "with unit ID 0F0E0D0C did not come back" in err and B.upgrades == 2,
       "install --unit, a firmware without the ID under the other FM-1's port name: not taken (exit 5, --info to check)")

    # never a MIDI rescan with --unit: refused as an option, and the advice for an offline port is the replug
    seen = {"midi": [["ChoralRoot FM-1", True]], "usb": ["ChoralRoot FM-1"]}
    A, B = cr_unit("A1B2C3D4", image), cr_unit("0F0E0D0C", image)
    bus = FakeBus(A, B, known=seen)
    rc, out, err = ucli(["--rescan-midi", "--unit", "A1B2C3D4"], bus)
    ok(rc == 2 and "every MIDI port" in err and bus.rescans == 0, "--rescan-midi --unit: refused (exit 2), nothing rescanned")
    boot = B.boot
    B.boot = lambda identity, n: boot(identity, n) or (setattr(B, "connected", False) if not identity.startswith("ota-") else None)
    rc, out, err = cli([p, "--yes", "--unit", "0F0E0D0C"], bus)
    ok(rc == 5 and "unplug this FM-1's USB cable" in err and "Rescan MIDI" not in err and "--rescan-midi" not in err and
       bus.rescans == 0, "install --unit, the FM-1 offline after the write: exit 5, unplug and replug said, no rescan")
    rc, out, err = cli([p, "--yes"], FakeBus(cr_unit("A1B2C3D4", image), known=seen))
    ok(rc == 0, "one FM-1, no --unit: the install as before")


PHASES = [wire, exits, backups, screen, debug, uac, stub, stuck, midi_side, select, sounds, syx, bindings, favorites, installs,
          errors, against_js, guard, smk, starrykey, against_js_guard, official, units]


def run_phases():
    """The phases, in PHASES' order on the output. Most of this file's time is waiting (the tool's bounds and the
    fake's restarts, in real time), so the phases run side by side, each in a fork of this process taken before
    any phase has run (one thread, the module's state as imported, a TMP of its own), its output to a file; the
    files are printed in PHASES' order as the phases end, the FAIL counts added. A phase that raises: its output
    and traceback are the last thing printed and the exit status is 1, as when the phases ran one after another.
    INSTALL_TEST_JOBS: how many at once (default 6, at most one per core; 1: one after another in this process,
    as before)."""
    global failed, TMP
    try:
        jobs = int(os.environ.get("INSTALL_TEST_JOBS", "") or 0)
    except ValueError:
        jobs = 0
    if jobs <= 0:
        jobs = min(6, os.cpu_count() or 1)
    if jobs == 1 or not hasattr(os, "fork") or threading.active_count() != 1:
        for phase in PHASES:
            phase()
        return
    sys.stdout.flush()
    sys.stderr.flush()
    top, running, ended, shown, k = TMP, {}, {}, 0, 0
    try:
        while shown < len(PHASES):
            while k < len(PHASES) and len(running) < jobs:
                pid = os.fork()
                if pid == 0:
                    code = 1
                    try:
                        fd = os.open(top / f"phase-{k}.out", os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
                        os.dup2(fd, 1)
                        os.dup2(fd, 2)
                        TMP = top / f"phase-{k}"
                        TMP.mkdir()
                        failed = 0
                        PHASES[k]()
                        sys.stdout.flush()
                        (top / f"phase-{k}.failed").write_text(str(failed))
                        code = 0
                    except BaseException:
                        import traceback
                        sys.stdout.flush()
                        traceback.print_exc()
                        sys.stderr.flush()
                    finally:
                        os._exit(code)
                running[pid] = k
                k += 1
            pid, status = os.wait()
            if pid not in running:
                continue
            ended[running.pop(pid)] = status
            while shown in ended:
                sys.stdout.write((top / f"phase-{shown}.out").read_text(errors="replace"))
                sys.stdout.flush()
                if ended[shown] != 0:             # the phase raised (or was killed): nothing after it is reported
                    sys.exit(1)
                failed += int((top / f"phase-{shown}.failed").read_text())
                shown += 1
    finally:
        for pid in running:                       # (an early end: the phases still running)
            try:
                os.kill(pid, 15)
            except OSError:
                pass


run_phases()
shutil.rmtree(TMP, ignore_errors=True)
print(f"INSTALL TESTS FAILED ({failed})" if failed else "install tests passed")
sys.exit(1 if failed else 0)
