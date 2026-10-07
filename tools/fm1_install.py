#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Install a Felucca package (.fwsc) on an FM-1 over USB-MIDI.

The same update as the web installer (web/fm1ota.js): step 1, the
running firmware reads parts of the package and starts the update loader;
step 2, the loader reads the whole package and writes it. In both steps the
device asks (SysEx read requests on the logical image) and we answer. Then
the FM-1 restarts and the installed identity is checked.

  fm1_install.py PACKAGE.fwsc [--port NAME] [--yes] [--force]
  fm1_install.py FM-1.fwsc            (the official V15 file: back to the stock firmware)
  fm1_install.py --info [--port NAME]
  fm1_install.py --backup FILE        (save what is stored on the FM-1: settings, user sounds, loops, samples ...)
  fm1_install.py --restore FILE       (write a backup back; ChoralRoot restarts afterwards)
  fm1_install.py PACKAGE.fwsc --backup FILE [--restore FILE]   (back up, install, restore onto the new firmware)

Backups are web/fm1backup.js's files (JSON, "felucca-backup" version 1), over the same SysEx (web/EDITOR_PROTOCOL.md);
FILE may be a directory (a dated name: choralroot-backup-YYYYMMDD.json). A restore writes the objects the connected
firmware lists: a Felucca backup restores its settings, banks, FM6 patches and samples 1-2 on ChoralRoot, and back.

If the FM-1 is still in update mode (an earlier install was cut off), the
install finishes the write. Needs mido with python-rtmidi.

Exit codes: 0 done, 1 cancelled or other error, 2 bad arguments or package,
3 FM-1 not found, 4 connection lost or the device stopped, 5 timeout (no
loader / no restart), 6 wrong model, or another identity after the install,
7 the backup or restore failed (or the firmware has no backup protocol), 8 the running firmware is one ChoralRoot
is not installed over (Sloop, the Felucca 0.x betas, unknown ones: docs/INSTALL-COMPAT.md; --force overrides).
"""
import argparse
import hashlib
import queue
import re
import sys
import time

HS_QUERY = bytes([0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7])
UPGRADE = bytes([0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7])
FINISH_CHECK, FINISH_WRITE = 0xE0000000, 0xF0000000
MAXDATA = 512
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F
LOADER_MARK = b"FELUCCA-LOADER-1"
# the unmodified official FM-1 V15 (FM-1.fwsc from M-VAVE): returning to it is allowed without --force,
# as the web installer's "Return to official V15" (web/fm1pkg.js validateStockPackage)
STOCK_V15_SHA256 = "db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a"
PORT_RE = re.compile(r"fm-1|felucca|ota|composite|sinco|usb-midi", re.I)   # never probe other gear

# seconds; the tests shorten them
DELAY = {"open": 0.3, "start": 2.0, "reply": 0.01, "loader": 3.0, "reboot": 3.0, "retry": 1.0,
         "poll": 1.0, "hs": 1.0, "info": 1.5, "idle_check": 8.0, "idle_write": 180.0,
         "wait_loader": 30.0, "wait_reboot": 40.0}

EXIT = {"usage": 2, "badpkg": 2, "badbackup": 2, "notfound": 3, "model": 6, "lost": 4, "stopped": 4, "badreq": 4,
        "nobackup": 7, "backup": 7, "unsupported": 8,
        "noloader": 5, "noreturn": 5, "mismatch": 6}


class InstallError(Exception):
    def __init__(self, code, msg):
        super().__init__(msg)
        self.code = code


# ------------------------------------------------------------ wire format ---

def pack7(data):
    out, acc, nb = [], 0, 0
    for b in data:
        acc |= b << nb
        nb += 8
        while nb >= 7:
            out.append(acc & 0x7F)
            acc >>= 7
            nb -= 7
    if nb:
        out.append(acc & 0x7F)
    return bytes(out)


def unpack7(s):
    out, acc, nb = [], 0, 0
    for b in s:
        acc |= (b & 0x7F) << nb
        nb += 7
        while nb >= 8:
            out.append(acc & 0xFF)
            acc >>= 8
            nb -= 8
    return bytes(out)


def response(addr, data, fl=0):
    n = len(data)
    body = bytearray([0x00, 0x59, 0x30, (n + 8) & 0xFF, ((n + 8) >> 8) & 0xFF, 0, fl])
    body += addr.to_bytes(4, "little") + bytes([n & 0xFF, (n >> 8) & 0xFF, 0]) + bytes(data)
    body.append(~sum(body[6:]) & 0xFF)
    return b"\xF0" + pack7(body) + b"\xF7"


def parse_request(pkt):
    """-> (flags, addr, len) or None"""
    if len(pkt) < 2 or pkt[0] != 0xF0 or pkt[-1] != 0xF7:
        return None
    u = unpack7(pkt[1:-1])
    if len(u) != 15 or u[:3] != b"\x00\x59\x30" or ~sum(u[6:14]) & 0xFF != u[14]:
        return None
    return u[6], int.from_bytes(u[7:11], "little"), int.from_bytes(u[11:14], "little")


class Identity:
    def __init__(self, text, model, version):
        self.text, self.model, self.version = text, model, version

    @property
    def loader(self):
        return self.model.lower().startswith("ota-")


def parse_identity(pkt):
    if len(pkt) < 2 or pkt[0] != 0xF0 or pkt[-1] != 0xF7:
        return None
    d = unpack7(pkt[1:-1])
    if len(d) != 34 or d[:3] != b"\x00\x59\x11":
        return None
    txt = d[6:33].rstrip(b"\0").decode("latin-1")
    m = re.fullmatch(r"([^_]+)_(\d+)", txt)
    return Identity(txt, m[1], int(m[2])) if m else None


# --------------------------------------------------------------- package ---

def product_of(raw):
    """the package identity ("FM-1_906"): one marker byte after each of the first 20 blocks"""
    if len(raw) < BLOCKS * BLK:
        raise InstallError("badpkg", "not an FM-1 package (too short)")
    return "".join(chr((m - i - 1) & 0xFF) for i in range(BLOCKS) if (m := raw[i * BLK + KEEP]) != 0x7D)


def logical_image(raw):
    """the image the device reads during an update: the package without the 20 marker bytes"""
    out = bytearray()
    for i in range(BLOCKS):
        out += raw[i * BLK:i * BLK + KEEP]
    return bytes(out + raw[BLOCKS * BLK:])


def model_of(text):
    return text.split("_")[0].removeprefix("ota-")


# which firmware runs: may ChoralRoot be installed over it? The same table as web/fm1ota.js classifyFirmware
# (docs/INSTALL-COMPAT.md). identity: the handshake text; info: the backup protocol's INFO version, None when the
# firmware does not answer. The 9xx identities are shared by every Felucca-based firmware (Felucca 0.9-beta FM-1_909,
# Felucca 1.0 FM-1_910, Sloop 2.0 FM-1_920 = ChoralRoot's), so they are told apart by the INFO text only.
RECOVERY_URL = "https://github.com/Quixotic7/MvaveFM1Unbricker"
REFUSE_REASON = "An install over it has left an FM-1 that no longer starts, and the data it leaves in the flash is not known to be safe for ChoralRoot."


def classify_firmware(identity, info):
    """-> (verdict "allow" | "refuse" | "loader", kind, name)"""
    m = re.fullmatch(r"(ota-)?([^_]+)_(\d+)", identity or "", re.I)
    v = (info or "").strip()
    if not m:
        return "refuse", "unknown", f"an unknown firmware ({identity or 'no identity'})"
    if m[1]:
        return "loader", "loader", identity
    num = int(m[3])
    if m[2] != "FM-1":
        return "refuse", "unknown", identity
    if 1 <= num < 100:
        return "allow", "stock", f"the official M-VAVE firmware ({identity})"
    if num == 0:
        return "refuse", "sloop", "Sloop's rescue mode (FM-1_000)"
    if num < 900:
        return "refuse", "unknown", f"an unknown firmware ({identity})"
    if re.match(r"choralroot\b", v, re.I):
        return "allow", "choralroot", v
    if re.match(r"melodee\b", v, re.I):
        return "allow", "melodee", f"Melodee ({v[8:]})"
    if re.search(r"sloop", v, re.I):
        return "refuse", "sloop", "Sloop"
    f = re.fullmatch(r"felucca\s+(v)?(\d+)\.(\d+)(\S*)", v, re.I)
    # Felucca 1.0 and later: "v1.0", "v1.0.1", "v1.1-rc1" (build.py "v" + release); the 0.x betas: "0.9-BETA", "0.5 BETA"
    if f and int(f[2]) >= 1 and not re.search(r"beta", v, re.I) and (f[1] or not f[4]):
        return "allow", "felucca", f"Felucca {f[1] or 'v'}{f[2]}.{f[3]}{f[4]}"
    if re.match(r"felucca\b", v, re.I):
        return "refuse", "felucca-beta", f"a Felucca beta or a firmware based on one ({v})"
    if v:
        return "refuse", "unknown", f"an unknown firmware ({identity}, {v})"
    return "refuse", "unknown", f"an unknown Felucca-based firmware ({identity})"


def refusal_text(name):
    return (f"Installing over {name} is not supported: {REFUSE_REASON} "
            f"Return to the official V15 firmware with the installer you used for {name} first, then install ChoralRoot. "
            f'If an FM-1 is already dark (black screen, a "WL82 UBOOT1.00" USB disk): {RECOVERY_URL}')


# ------------------------------------------------------------------ MIDI ---

class MidoLink:
    """one MIDI in/out pair with a SysEx queue"""

    def __init__(self, backend, in_name, out_name):
        import mido
        self.backend, self.name, self.mido = backend, in_name, mido
        self.q = queue.Queue()
        self.inp = mido.open_input(in_name, callback=self._rx)
        try:
            self.out = mido.open_output(out_name)
        except Exception:
            self.inp.close()
            raise
        time.sleep(DELAY["open"])   # CoreMIDI may drop a message sent just after opening

    def _rx(self, msg):
        if msg.type == "sysex":
            self.q.put(bytes([0xF0, *msg.data, 0xF7]))

    @property
    def lost(self):
        return self.name not in self.backend.input_names()

    def send(self, pkt):
        try:
            self.out.send(self.mido.Message("sysex", data=pkt[1:-1]))
            return True
        except Exception:
            return False

    def read(self, timeout):
        try:
            return self.q.get(timeout=max(timeout, 0))
        except queue.Empty:
            return None

    def drain(self):
        while not self.q.empty():
            self.q.get_nowait()

    def close(self):
        for p in (self.inp, self.out):
            try:
                p.close()
            except Exception:
                pass


class MidoBackend:
    def __init__(self):
        import mido
        self.mido = mido

    def input_names(self):
        return list(dict.fromkeys(self.mido.get_input_names()))

    def output_names(self):
        return list(dict.fromkeys(self.mido.get_output_names()))

    def open(self, in_name, out_name):
        return MidoLink(self, in_name, out_name)


def pair_output(name, outs):
    if name in outs:
        return name
    base = re.sub(r"\s+\d+$", "", name)     # Windows numbers each port
    same = [o for o in outs if re.sub(r"\s+\d+$", "", o) == base]
    if len(same) == 1:
        return same[0]
    return outs[0] if len(outs) == 1 else None


# --------------------------------------------------------------- updater ---

class Device:
    def __init__(self, link, ident, name):
        self.link, self.id, self.name = link, ident, name


def handshake(link, tries=3):
    link.drain()
    for _ in range(tries):
        if not link.send(HS_QUERY):
            return None
        end = time.monotonic() + DELAY["hs"]
        while (left := end - time.monotonic()) > 0:
            p = link.read(left)
            if p is None:
                break
            ident = parse_identity(p)
            if ident:
                return ident
    return None


class Updater:
    def __init__(self, backend, port=None):
        self.backend = backend
        self.port = port

    def candidates(self, only_port):
        names = self.backend.input_names()
        if self.port:
            mine = [n for n in names if self.port.lower() in n.lower()]
            if only_port:
                return mine
            return mine + [n for n in names if n not in mine and PORT_RE.search(n)]
        return [n for n in names if PORT_RE.search(n)]

    def find(self, want=None, only_port=True):
        """the first matching port that answers the handshake (and want(identity))"""
        outs = self.backend.output_names()
        for name in self.candidates(only_port):
            out = pair_output(name, outs)
            if not out:
                continue
            try:
                link = self.backend.open(name, out)
            except Exception:
                continue
            ident = handshake(link, 2)
            if ident and (want is None or want(ident)):
                return Device(link, ident, name)
            link.close()
        return None

    def wait_for(self, want, secs):
        end = time.monotonic() + secs
        while time.monotonic() < end:
            dev = self.find(want, only_port=False)
            if dev:
                return dev
            time.sleep(DELAY["retry"])
        return None

    def serve(self, link, image, finish, idle, progress):
        """answer read requests until the device asks for `finish`; -> (served, finished, lost)"""
        served, last = 0, time.monotonic()
        while True:
            pkt = link.read(DELAY["poll"])
            if pkt is None:
                if link.lost:
                    return served, False, True
                if time.monotonic() - last > idle:
                    return served, False, False
                continue
            r = parse_request(pkt)
            if not r:
                continue
            fl, addr, n = r
            last = time.monotonic()
            if addr in (FINISH_CHECK, FINISH_WRITE):
                link.send(response(addr, b"success\0"))
                if addr == finish:
                    return served, True, False
                continue
            if n > MAXDATA or addr + n > len(image):
                raise InstallError("badreq", f"the device asked for {addr:#x}+{n}, outside the package")
            time.sleep(DELAY["reply"])
            if not link.send(response(addr, image[addr:addr + n], fl)):
                return served, False, True
            served += 1
            progress(served, addr + n)

    def check(self, dev, image, step):
        """step 1: the running firmware checks the package and starts the loader"""
        step("start", dev.id.text)
        dev.link.send(UPGRADE)
        time.sleep(DELAY["start"])
        served, finished, lost = self.serve(dev.link, image, FINISH_CHECK, DELAY["idle_check"],
                                            lambda k, end: step("check", k))
        dev.link.close()
        if not finished:
            raise InstallError("lost" if lost else "stopped",
                               f"the FM-1 {'was disconnected' if lost else 'stopped answering'} after "
                               f"{served} requests; nothing was written")
        step("loader")
        time.sleep(DELAY["loader"])
        ota = self.wait_for(lambda i: i.loader, DELAY["wait_loader"])
        if not ota:
            raise InstallError("noloader", "the update loader did not appear. Replug the USB cable and "
                                           "run the install again: the FM-1 stays in update mode until it is done")
        return ota

    def write(self, ota, image, step):
        """step 2: the loader reads and writes the whole package, then restarts"""
        step("write", 0)
        ota.link.send(UPGRADE)
        time.sleep(DELAY["start"])
        served, finished, lost = self.serve(ota.link, image, FINISH_WRITE, DELAY["idle_write"],
                                            lambda k, end: step("write", min(99, end * 100 // len(image))))
        ota.link.close()
        if not finished:
            raise InstallError("lost" if lost else "stopped",
                               f"the loader {'was disconnected' if lost else 'stopped answering'} after "
                               f"{served} requests. Replug the USB cable and run the install again to finish")
        step("write", 100)

    def verify(self, product, step):
        step("reboot")
        time.sleep(DELAY["reboot"])
        back = self.wait_for(lambda i: not i.loader, DELAY["wait_reboot"])
        if not back:
            raise InstallError("noreturn", "the FM-1 did not come back: power-cycle it")
        back.link.close()
        if back.id.text != product:
            raise InstallError("mismatch", f"written, but the FM-1 reports {back.id.text} (expected {product})")
        step("done", back.id.text)
        return back.id.text

    def install(self, dev, image, product, step=lambda *a: None):
        """running firmware -> loader -> new firmware; dev from find(). A device already in
        update mode only needs the write."""
        if model_of(dev.id.text) != model_of(product):
            dev.link.close()
            raise InstallError("model", f"the device is {dev.id.text}, the package is for {product}")
        ota = dev if dev.id.loader else self.check(dev, image, step)
        self.write(ota, image, step)
        return self.verify(product, step)


# ------------------------------------------------------------ backup / restore ---
# The page's web/fm1backup.js in Python (web/EDITOR_PROTOCOL.md: Felucca's backup commands; ChoralRoot answers them
# with its own objects). One file format, "felucca-backup" version 1 (JSON); a restore writes the objects of the file
# that the connected firmware lists in BACKUP_LIST.

BK_HDR = bytes([0xF0, 0x7D, 0x46, 0x4C])
BK_INFO, BK_LIST, BK_GET, BK_PUT, BK_RESTART = 1, 65, 66, 67, 72
BK_CHUNK = 256
FELUCCA_IDS = [0, 1, 2, 3, 4, 5, 6, 7, 8, 32, 33, 34]
CR_IDS = [1, 6, 7, 8, 9] + list(range(40, 50))   # (no samples: ChoralRoot has no SAMPLE engine)
KNOWN_IDS = set(FELUCCA_IDS) | set(CR_IDS)
BK_RC = {1: "invalid object, size or request", 2: "the data failed validation", 3: "busy: stop the loop on the FM-1",
         4: "flash write failed", 5: "stale session: start again"}
PER4, PER5, CR_BLOCK = 0x50455234, 0x50455235, 192
DELAY.update({"bk_busy": 1.0, "bk_busy_tries": 120})


class BackupError(InstallError):
    def __init__(self, msg, rc=0):
        super().__init__("backup", msg)
        self.rc = rc


def bk_u32(n):
    return [(n >> (7 * i)) & (15 if i == 4 else 127) for i in range(5)]


def bk_r32(a, off=0):
    if len(a) < off + 5 or a[off + 4] > 15:
        raise BackupError("invalid number in a reply")
    return a[off] | a[off + 1] << 7 | a[off + 2] << 14 | a[off + 3] << 21 | a[off + 4] << 28


def bk_pack(data):
    out = []
    for off in range(0, len(data), 7):
        c = data[off:off + 7]
        out.append(sum((b >> 7) << i for i, b in enumerate(c)))
        out += [b & 127 for b in c]
    return out


def bk_unpack(a, size):
    out, i = bytearray(), 0
    while len(out) < size:
        n = min(7, size - len(out))
        if i >= len(a) or a[i] >> n or i + 1 + n > len(a):
            raise BackupError("malformed data in a reply")
        m = a[i]
        out += bytes(a[i + 1 + k] | ((m >> k) & 1) << 7 for k in range(n))
        i += 1 + n
    if i != len(a):
        raise BackupError("trailing bytes in a reply")
    return bytes(out)


def bk_crc(b):
    import zlib
    return zlib.crc32(bytes(b)) & 0xFFFFFFFF


def bk_check(rc):
    if rc:
        raise BackupError(f"the FM-1 answered {rc}: {BK_RC.get(rc, 'error')}", rc)


def is_sample(i):
    return 32 <= i <= 34


def max_size(i):
    return 81920 if is_sample(i) or i not in KNOWN_IDS else 3840


def object_name(i):
    if i == 0:
        return "current music"
    if i == 1:
        return "settings"
    if 2 <= i <= 5:
        return f"project {i - 1}"
    if i in (6, 7):
        return "user sounds " + ("1-16" if i == 6 else "17-32")
    if i == 8:
        return "FM6 patch bank"
    if i == 9:
        return "VA patches"
    if is_sample(i):
        return f"sample slot {i - 31}"
    if 40 <= i <= 49:
        return f"loop slot {i - 39}"
    return f"object {i}"


def family(firmware):
    f = (firmware or "").lower()
    return "choralroot" if f.startswith("choralroot") else "felucca" if f.startswith("felucca") else "other"


class BackupLink:
    """request / reply over a MidoLink (one request at a time, the reply carries the same command)"""

    def __init__(self, link):
        self.link = link

    def request(self, cmd, args, timeout=1.5):
        self.link.drain()
        if not self.link.send(BK_HDR + bytes([cmd, *args, 0xF7])):
            raise InstallError("lost", "the FM-1 was disconnected")
        end = time.monotonic() + timeout
        while (left := end - time.monotonic()) > 0:
            p = self.link.read(left)
            if p is None:
                break
            if len(p) >= 6 and p[:4] == BK_HDR and p[4] == cmd and p[-1] == 0xF7:
                return list(p[5:-1])
        if self.link.lost:
            raise InstallError("lost", "the FM-1 was disconnected")
        raise BackupError(f"no answer to backup command {cmd} (the stock firmware, or an older Felucca?)")


def bk_manifest(a):
    if not a or a[0] != 1:
        raise BackupError("unsupported backup protocol")
    bk_check(a[1])
    n = a[2]
    if not n or len(a) != 3 + n * 11:
        raise BackupError("incomplete object list")
    out, seen = [], set()
    for k in range(n):
        p = 3 + k * 11
        i, size, crc = a[p], bk_r32(a, p + 1), bk_r32(a, p + 6)
        if i in seen or size > max_size(i) or (not size and crc):
            raise BackupError("unexpected object list")
        seen.add(i)
        out.append({"id": i, "size": size, "crc": crc})
    return out


def device_info(bl):
    """INFO -> (version, family), or None when the firmware does not answer"""
    try:
        a = bl.request(BK_INFO, [], DELAY["info"])
    except BackupError:
        return None
    v = bytes(a[:a.index(0)] if 0 in a else a).decode("latin-1")
    return v, family(v)


def capture(bl, firmware, progress=lambda done, total: None):
    import base64
    man = bk_manifest(bl.request(BK_LIST, [], 3.0))
    total, done, objs = sum(o["size"] for o in man), 0, []
    for o in man:
        data = bytearray()
        for off in range(0, o["size"], BK_CHUNK):
            n = min(BK_CHUNK, o["size"] - off)
            for attempt in range(2):
                try:
                    a = bl.request(BK_GET, [o["id"], *bk_u32(off), n & 127, n >> 7], 1.0)
                    break
                except BackupError:
                    if attempt:
                        raise
            bk_check(a[1])
            if a[0] != o["id"] or bk_r32(a, 2) != off or (a[7] | a[8] << 7) != n:
                raise BackupError("unexpected backup reply")
            data += bk_unpack(a[9:], n)
            done += n
            progress(done, total)
        if bk_crc(data) != o["crc"]:
            raise BackupError("the FM-1 changed during the backup: try again with it stopped")
        objs.append({**o, "data": base64.b64encode(bytes(data)).decode("ascii")})
    f = {"format": "felucca-backup", "version": 1, "firmware": firmware,
         "created": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "objects": objs}
    read_backup(f)
    return f


def read_backup(f):
    """validate a backup (dict or JSON text) -> list of objects with their bytes"""
    import base64
    import binascii
    import json
    if isinstance(f, (str, bytes)):
        try:
            f = json.loads(f)
        except ValueError as e:
            raise InstallError("badbackup", f"not a backup file: {e}")
    if not isinstance(f, dict) or f.get("format") != "felucca-backup" or f.get("version") != 1 or not f.get("objects"):
        raise InstallError("badbackup", "not an FM-1 backup (format felucca-backup, version 1)")
    objs, seen = [], set()
    for o in f["objects"]:
        try:
            i, size, crc = o["id"], o["size"], o["crc"]
            data = base64.b64decode(o["data"], validate=True)
        except (KeyError, TypeError, binascii.Error):
            raise InstallError("badbackup", "invalid object in the backup")
        if not isinstance(i, int) or not 0 <= i <= 127 or i in seen or not isinstance(size, int) or not 0 <= size <= max_size(i) \
                or len(data) != size or bk_crc(data) != crc:
            raise InstallError("badbackup", f"object {i}: damaged (size or checksum)")
        if is_sample(i) and size and (size < 512 or int.from_bytes(data[:4], "little") != 0x504D5346 or
                                      int.from_bytes(data[16:20], "little") != size - 512 or
                                      bk_crc(data[512:]) != int.from_bytes(data[20:24], "little")):
            raise InstallError("badbackup", f"{object_name(i)}: not a valid sample set")
        seen.add(i)
        objs.append({"id": i, "size": size, "crc": crc, "bytes": data})
    if family(f.get("firmware")) == "felucca" and [o["id"] for o in objs] not in (FELUCCA_IDS, [i for i in FELUCCA_IDS if i != 8]):
        raise InstallError("badbackup", "not a complete Felucca backup")
    if any(o["id"] == 0 and not o["size"] for o in objs) or not any(o["size"] for o in objs):
        raise InstallError("badbackup", "the backup holds nothing")
    return objs


def restore(bl, f, progress=lambda done, total: None, busy=lambda: None):
    """write the objects of f that the FM-1 lists (settings, then the music, last) -> (restored ids, skipped ids)"""
    objs = read_backup(f)                         # every byte checked before the first write
    dev = {o["id"]: o for o in bk_manifest(bl.request(BK_LIST, [], 3.0))}
    plan = []
    for o in objs:
        d = dev.get(o["id"])
        if d is None:
            continue
        if (o["id"] == 1 and d["size"] and o["size"] == d["size"] + CR_BLOCK and
                int.from_bytes(o["bytes"][:4], "little") == PER5):   # ChoralRoot's settings on Felucca: its fields only
            b = PER4.to_bytes(4, "little") + o["bytes"][4:d["size"]]
            o = {**o, "bytes": b, "size": len(b), "crc": bk_crc(b), "converted": True}
        plan.append(o)
    skipped = [o["id"] for o in objs if o["id"] not in dev]
    restored, total, done = [], sum(o["size"] for o in plan), 0

    def retry(fn):
        for k in range(int(DELAY["bk_busy_tries"]) + 1):
            try:
                return fn()
            except BackupError as e:
                if e.rc != 3 or k >= DELAY["bk_busy_tries"]:
                    raise
                busy()
                time.sleep(DELAY["bk_busy"])

    def put(args):
        a = bl.request(BK_PUT, args, 4.0)
        bk_check(a[2])

    def smp(cmd, args):
        a = bl.request(cmd, args, 4.0)
        if a[0] != args[0]:
            raise BackupError("unexpected sample reply")
        bk_check(a[-1])

    order = [o for o in plan if o["id"] > 1] + [o for o in plan if o["id"] == 1] + [o for o in plan if o["id"] == 0]
    for o in order:
        i, b = o["id"], o["bytes"]
        if is_sample(i):
            slot = i - 32
            if not o["size"]:
                retry(lambda: smp(14, [slot]))
            else:
                retry(lambda: smp(11, [slot]))
                for off in range(512, o["size"], BK_CHUNK):
                    c = b[off:off + BK_CHUNK]
                    retry(lambda: smp(12, [slot, off & 127, off >> 7 & 127, off >> 14 & 127, *bk_pack(c)]))
                    done += len(c)
                    progress(done, total)
                retry(lambda: smp(13, [slot, *bk_pack(b[:480])]))
                done += 512
                progress(done, total)
        else:
            try:
                retry(lambda: put([0, i, *bk_u32(o["size"]), *bk_u32(o["crc"])]))
            except BackupError as e:
                if o.get("converted") and e.rc == 1:
                    skipped.append(i)
                    continue
                raise
            try:
                for off in range(0, o["size"], BK_CHUNK):
                    c = b[off:off + BK_CHUNK]
                    put([1, i, *bk_u32(off), *bk_pack(c)])
                    done += len(c)
                    progress(done, total)
                retry(lambda: put([2, i]))
            except BackupError as e:
                try:
                    put([3, i])
                except InstallError:
                    pass
                if o.get("converted") and e.rc == 2:
                    skipped.append(i)
                    continue
                raise
        restored.append(i)
    return restored, skipped


def backup_name(firmware):
    fam = family(firmware)
    return f"{'fm1' if fam == 'other' else fam}-backup-{time.strftime('%Y%m%d')}.json"


def open_backup(up):
    """the running FM-1 -> (BackupLink, (version, family)); the link must be closed by the caller"""
    dev = up.find(lambda i: not i.loader)
    if not dev:
        raise not_found(up)
    bl = BackupLink(dev.link)
    info = device_info(bl)
    if not info:
        dev.link.close()
        raise InstallError("nobackup", f"{dev.id.text} does not answer the backup protocol "
                                       "(the stock firmware, or an older Felucca)")
    return bl, info


def run_backup(up, path, out):
    import json
    import os
    bl, (version, _) = open_backup(up)
    prog = Progress(out)
    try:
        f = capture(bl, version, lambda d, t: prog.put(f"backing up {d * 100 // max(1, t):3d}%"))
    finally:
        prog.end()
        bl.link.close()
    if os.path.isdir(path):
        path = os.path.join(path, backup_name(version))
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(f, fh)
    held = [object_name(o["id"]) for o in f["objects"] if o["size"]]
    print(f"backup of {version} saved: {path}\n  holds: {', '.join(held) or 'nothing'}", file=out)
    return path


def run_restore(up, path, out, ask, yes):
    try:
        text = open(path, encoding="utf-8").read()
    except OSError as e:
        raise InstallError("badbackup", f"cannot read {path}: {e.strerror}")
    objs = read_backup(text)
    bl, (version, fam) = open_backup(up)
    try:
        held = [object_name(o["id"]) for o in objs if o["size"]]
        print(f"restore onto {version}: {', '.join(held)}", file=out)
        if not yes and not ask("Restore? What the backup holds replaces what is stored on the FM-1. [y/N] "):
            print("cancelled", file=out)
            return 1
        prog = Progress(out)
        try:
            restored, skipped = restore(bl, text, lambda d, t: prog.put(f"restoring {d * 100 // max(1, t):3d}%"),
                                        lambda: prog.put("busy: stop the loop on the FM-1"))
        finally:
            prog.end()
        if fam == "choralroot":
            try:
                bk_check(bl.request(BK_RESTART, [], 2.0)[0])
            except InstallError as e:
                print(f"restart: {e} (power-cycle the FM-1)", file=out)
    finally:
        bl.link.close()
    print(f"restored: {', '.join(map(object_name, restored)) or 'nothing'}", file=out)
    if skipped:
        print(f"not used by {version} (kept in the file): {', '.join(map(object_name, skipped))}", file=out)
    return 0


# ------------------------------------------------------------------- CLI ---

def load_package(path, force):
    try:
        raw = open(path, "rb").read()
    except OSError as e:
        raise InstallError("badpkg", f"cannot read {path}: {e.strerror}")
    product = product_of(raw)
    if not re.fullmatch(r"[^_]+_\d+", product):
        raise InstallError("badpkg", f"{path}: not an FM-1 package (identity {product!r})")
    official = hashlib.sha256(raw).hexdigest() == STOCK_V15_SHA256
    if LOADER_MARK not in raw and not official and not force:
        raise InstallError("badpkg", f"{path}: no Felucca update loader in this package; only Felucca's own "
                                     "packages and the unmodified official V15 (FM-1.fwsc) are installed "
                                     "(--force overrides)")
    return product, logical_image(raw), official


def not_found(up):
    names = up.backend.input_names()
    where = f"no MIDI port matching {up.port!r} answered" if up.port else "FM-1 not found"
    return InstallError("notfound", f"{where} (USB data cable? another app using it?)"
                        f"\n  MIDI inputs: {', '.join(names) if names else '(none)'}")


class Progress:
    def __init__(self, out):
        self.out, self.line = out, False

    def __call__(self, k, a=None):
        if k == "check":
            self.put(f"checking the package ({a} reads)")
        elif k == "write":
            self.put(f"writing {a:3d}%")
        else:
            self.end()
            msg = {"start": f"starting the update on {a}", "loader": "switching to update mode",
                   "reboot": "restarting", "done": f"done: the FM-1 runs {a}"}[k]
            print(msg, file=self.out, flush=True)

    def put(self, s):
        print(f"\r{s}\033[K", end="", file=self.out, flush=True)
        self.line = True

    def end(self):
        if self.line:
            print(file=self.out, flush=True)
            self.line = False


def run(a, backend, out, ask):
    up = Updater(backend, a.port)
    if a.info:
        dev = up.find()
        if not dev:
            raise not_found(up)
        info = None if dev.id.loader else device_info(BackupLink(dev.link))
        dev.link.close()
        mode = "update loader (update not finished)" if dev.id.loader else "running"
        print(f"{dev.id.text}  [{mode}]  port: {dev.name}" + (f"  version: {info[0]}" if info else ""), file=out)
        if not dev.id.loader:
            verdict, _kind, name = classify_firmware(dev.id.text, info and info[0])
            print(f"ChoralRoot can be installed over {name}" if verdict == "allow" else
                  f"ChoralRoot is not installed over {name} (docs/INSTALL-COMPAT.md)", file=out)
        return 0
    if not a.package:                             # backup and / or restore only
        if a.backup:
            run_backup(up, a.backup, out)
        return run_restore(up, a.restore, out, ask, a.yes) if a.restore else 0
    product, image, official = load_package(a.package, a.force)
    print(f"package: {product}  ({a.package})", file=out)
    dev = up.find()
    if not dev:
        raise not_found(up)
    version = None
    if not dev.id.loader and not official and model_of(dev.id.text) == model_of(product):
        info = device_info(BackupLink(dev.link))     # the version text tells the Felucca-based firmwares apart
        version = info and info[0]
    print(f"device:  {dev.id.text}  ({dev.name})" + (f"  {version}" if version else "") +
          ("  in update mode: the write will be finished" if dev.id.loader else ""), file=out)
    if not dev.id.loader and not official and model_of(dev.id.text) == model_of(product):
        verdict, _kind, name = classify_firmware(dev.id.text, version)   # (the return to V15 is always allowed)
        if verdict == "refuse" and not a.force:
            dev.link.close()
            raise InstallError("unsupported", refusal_text(name) + "\n  (--force installs anyway, at your own risk)")
        if verdict == "refuse":
            print(f"--force: installing over {name} anyway, although it is not supported", file=out)
    if not a.yes and not ask("Install? Do not unplug the FM-1 while writing. [y/N] "):
        dev.link.close()
        print("cancelled", file=out)
        return 1
    if a.backup:                                  # the FM-1's data to a file first (as the web installer)
        if dev.id.loader:
            dev.link.close()
            raise InstallError("nobackup", "the FM-1 is in update mode and cannot be backed up: finish the install "
                                           "without --backup (a backup saved earlier is still the one to restore)")
        dev.link.close()
        run_backup(up, a.backup, out)
        dev = up.find()
        if not dev:
            raise not_found(up)
    elif not dev.id.loader:
        print("(no --backup FILE: what is stored on the FM-1 is not saved first)", file=out)
    prog = Progress(out)
    try:
        up.install(dev, image, product, prog)
    finally:
        prog.end()
    if a.restore:                                 # the backup onto the firmware just installed
        time.sleep(DELAY["start"])
        return run_restore(up, a.restore, out, ask, True)
    return 0


def ask_tty(prompt):
    try:
        return input(prompt).strip().lower() in ("y", "yes")
    except EOFError:
        return False


def main(argv=None, backend=None, out=sys.stdout, ask=ask_tty):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("package", nargs="?", help="Felucca package (.fwsc)")
    ap.add_argument("--info", action="store_true", help="print the identity of the connected FM-1")
    ap.add_argument("--port", metavar="NAME", help="MIDI port to use (part of its name)")
    ap.add_argument("--yes", action="store_true", help="do not ask for confirmation")
    ap.add_argument("--force", action="store_true", help="install a package without the Felucca loader marker, or over a "
                                                         "firmware ChoralRoot does not support installing over (at your own risk)")
    ap.add_argument("--backup", metavar="FILE", help="save a backup of the FM-1 to FILE (a directory: a dated name) "
                                                     "before the install, or alone")
    ap.add_argument("--restore", metavar="FILE", help="restore a backup FILE onto the FM-1 (after the install, or alone)")
    a = ap.parse_args(argv)
    if a.info and (a.package or a.backup or a.restore):
        ap.error("--info goes alone")
    if not (a.info or a.package or a.backup or a.restore):
        ap.error("give a PACKAGE.fwsc, --backup FILE, --restore FILE or --info")
    try:
        if backend is None:
            try:
                backend = MidoBackend()
            except ImportError:
                raise InstallError("usage", "needs mido and python-rtmidi (pip install mido python-rtmidi)")
        return run(a, backend, out, ask)
    except InstallError as e:
        print(f"error: {e}", file=sys.stderr)
        return EXIT.get(e.code, 1)
    except KeyboardInterrupt:
        print("\ninterrupted: if the FM-1 is in update mode, run the install again to finish", file=sys.stderr)
        return 130


if __name__ == "__main__":
    sys.exit(main())
