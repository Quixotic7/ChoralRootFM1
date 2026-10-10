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
  fm1_install.py --sounds             (ChoralRoot: list the 32 user sounds U01..U32)
  fm1_install.py --export-sound N FILE   (user sound N to a sound file; FILE may be a directory)
  fm1_install.py --import-sound N FILE   (a sound file into slot N) [--yes]
  fm1_install.py --rename-sound N NAME   /   --delete-sound N [--yes]
  fm1_install.py --export-syx N FILE     (an FM6 voice / CZ-1 tone as .syx: DX7 single voice / Casio tone dump)
  fm1_install.py --import-syx N FILE [--voice V] [--yes]   (a DX7 voice or bank, or a CZ-1 tone, into slot N)

Backups are web/fm1backup.js's files (JSON, "felucca-backup" version 1), over the same SysEx (web/EDITOR_PROTOCOL.md);
FILE may be a directory (a dated name: choralroot-backup-YYYYMMDD.json). A restore writes the objects the connected
firmware lists: a Felucca backup restores its settings, banks, FM6 patches and samples 1-2 on ChoralRoot, and back.

Sound files are web/fm1sounds.js's ("choralroot-sound" version 1, docs/SOUNDS.md): one user sound, its record and
its VA / FM6 / CZ-1 patch; only the objects that hold that slot are written. A .syx carries an FM6 voice or a CZ-1
tone only; an import makes the rest of the sound from the engine's defaults (docs/SOUNDS.md ".syx export and import")
and adds it to its engine's preset pool (it overwrites no factory preset).

The binding (docs/SOUNDS.md "The binding"): a sound saved on the FM-1 over a factory preset replaces it in the
engine's pool; --sounds shows "over FM6 02 FM BELL" for it and "+" for a preset added after the factory ones. A sound
file keeps the record's binding (its "binding" field says what it overwrites; the record's bytes are what counts).

If the FM-1 is still in update mode (an earlier install was cut off), the
install finishes the write. Needs mido with python-rtmidi.

Exit codes: 0 done, 1 cancelled or other error, 2 bad arguments or package,
3 FM-1 not found, 4 connection lost or the device stopped, 5 timeout (no
loader / no restart), 6 wrong model, or another identity after the install,
7 the backup, restore or sound write failed (or the firmware has no backup protocol, or is not ChoralRoot for the
sounds), 8 the running firmware is one ChoralRoot
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
        "nobackup": 7, "backup": 7, "nosounds": 7, "unsupported": 8, "badsound": 2,
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
CR_IDS = [1, 6, 7, 8, 9, 10, 11, 12, 13] + list(range(14, 22)) + [22] + list(range(40, 50))   # fm1backup.js CR_BACKUP_IDS
# (9 the VA patches, 10 11 the FM6 patches, 12 13 the CZ-1 tones, 14..21 the CZ-1 banks, 22 the FM TONE patches, 40..49
# loops; no samples)
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
    if i in (10, 11):
        return "FM6 patches " + ("1-16" if i == 10 else "17-32")
    if i in (12, 13):
        return "CZ-1 tones " + ("1-16" if i == 12 else "17-32")
    if 14 <= i <= 21:
        return "CZ-1 bank " + chr(ord("A") + i - 14)
    if i == 22:
        return "FM TONE patches"
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


def bk_get_object(bl, i, size, chunk=lambda n: None):
    """GET object i (size bytes, as LIST reported) in 256-byte windows -> its bytes (the caller compares the CRC)"""
    data = bytearray()
    for off in range(0, size, BK_CHUNK):
        n = min(BK_CHUNK, size - off)
        for attempt in range(2):
            try:
                a = bl.request(BK_GET, [i, *bk_u32(off), n & 127, n >> 7], 1.0)
                break
            except BackupError:
                if attempt:
                    raise
        bk_check(a[1])
        if a[0] != i or bk_r32(a, 2) != off or (a[7] | a[8] << 7) != n:
            raise BackupError("unexpected backup reply")
        data += bk_unpack(a[9:], n)
        chunk(n)
    return bytes(data)


def bk_retry(fn, busy=lambda: None):
    """fn(), sent again while the FM-1 answers rc 3 (busy: the loop plays), a second apart"""
    for k in range(int(DELAY["bk_busy_tries"]) + 1):
        try:
            return fn()
        except BackupError as e:
            if e.rc != 3 or k >= DELAY["bk_busy_tries"]:
                raise
            busy()
            time.sleep(DELAY["bk_busy"])


def bk_put_object(bl, i, b, chunk=lambda n: None, busy=lambda: None):
    """PUT object i whole: begin (busy retried), data, commit (busy retried); after a begin, a failure aborts the
    session. The BackupError raised carries .begun (False: the begin itself was refused)."""
    def put(args):
        a = bl.request(BK_PUT, args, 4.0)
        bk_check(a[2])
    try:
        bk_retry(lambda: put([0, i, *bk_u32(len(b)), *bk_u32(bk_crc(b))]), busy)
    except BackupError as e:
        e.begun = False
        raise
    try:
        for off in range(0, len(b), BK_CHUNK):
            c = b[off:off + BK_CHUNK]
            put([1, i, *bk_u32(off), *bk_pack(c)])
            chunk(len(c))
        bk_retry(lambda: put([2, i]), busy)
    except BackupError as e:
        try:
            put([3, i])
        except InstallError:
            pass
        e.begun = True
        raise


def capture(bl, firmware, progress=lambda done, total: None):
    import base64
    man = bk_manifest(bl.request(BK_LIST, [], 3.0))
    total, done, objs = sum(o["size"] for o in man), 0, []
    for o in man:
        def chunk(n):
            nonlocal done
            done += n
            progress(done, total)
        data = bk_get_object(bl, o["id"], o["size"], chunk)
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
        return bk_retry(fn, busy)

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
            def chunk(n):
                nonlocal done
                done += n
                progress(done, total)
            try:
                bk_put_object(bl, i, b, chunk, busy)
            except BackupError as e:
                if o.get("converted") and e.rc == (2 if e.begun else 1):
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


# ------------------------------------------------------------ user sounds ---
# docs/SOUNDS.md (the page's web/fm1sounds.js in Python): one user sound (slot U01..U32: its record and, for a VA /
# FM6 / CZ-1 / FM TONE sound, its patch) to and from a small JSON file ("choralroot-sound" version 1), renamed or deleted, on
# the backup protocol's whole-object reads and writes. Only the objects whose bytes change are written, the stores
# first and the bank last (a refused patch leaves the slot's record as it was).

SOUND_IDS = (6, 7, 9, 10, 11, 12, 13, 22)        # the banks, the VA store, the FM6 halves, the CZ-1 halves, the FM TONE
                                                 # store (22: firmware 0.14 on; read when the FM-1 lists it)
SOUND_SLOTS, SOUND_PER_BANK, SOUND_REC = 32, 16, 192
ENGINE_NAMES = ("ANALOG", "DIGITAL", "PHASE", "LOFI", "SAMPLE", "VOICE", "TRIO", "WHEEL", "GRAIN", "PHYS", "DRUM",
                "NOISE", "FM6", "VA", "CZ-1", "FM TONE")   # ENGINES[] (append-only; 0, 1, 4, 8, 10 retired: ANALOG
                                                          # since 0.14). 15: FM TONE (eng_quad.c, code name QUAD)
ENGINE_PATCH = {12: "fm6", 13: "va", 14: "cz", 15: "quad"}  # engine -> its patch kind
PATCH_KINDS = ("va", "fm6", "cz", "quad")
PATCH_SIZE = {"va": 110, "fm6": 128, "cz": 144, "quad": 80}
PATCH_ID = {"va": 9, "fm6": 10, "cz": 12, "quad": 22}   # the store (the first half for FM6 / CZ-1)
UPB_MAGIC, VAS_MAGIC, FM6U_MAGIC, CZU_MAGIC, QUDS_MAGIC = 0x31425055, 0x31534156, 0x55364D46, 0x55315A43, 0x53445551
BANK_SIZE = 8 + SOUND_PER_BANK * SOUND_REC     # 3080
STORE_SIZE = {"va": 16 + SOUND_SLOTS * 110, "fm6": 16 + 16 * 128, "cz": 16 + 16 * 144,
              "quad": 16 + SOUND_SLOTS * 80}   # 3536, 2064, 2320, 2576
WHOLE_STORES = ("va", "quad")                  # one store of 32 (the others: two halves of 16), its used mask at 8
SOUND_FORMAT = "choralroot-sound"
# the factory presets of each engine (ENGINES[e]->presets[k].name) and the first of them in the engine's pool (the
# CZ-1's preset 0, INIT TONE, is the pool's INIT): tests/sound_templates.c prints both from the firmware, and its
# --check fails until the two lines below hold its strings verbatim (docs/SOUNDS.md "The binding")
FACTORY_PRESETS = {"2":["BRASS","ORGAN","STRING","RESO","BELL","WIRE"],"3":["PULSE LD","WAVE BASS","ARP 8BIT","WAVE LEAD","STEP LEAD"],"5":["CHOIR AAH","VOX LEAD","WOW BASS","WHISPER"],"6":["FAT BASS","ARP LEAD","SYNC LEAD","RING BELL","CHIP CHOIR"],"7":["FULL ORGAN","JAZZ PERC","GOSPEL","SOFT FLUTE","ROCK DRIVE"],"9":["BELL TREE","MARIMBA","PLUCK","BOWED METAL","KALIMBA","HAND DRUM","TOMS","DRONE STRING","HARP"],"11":["WIND","RAIN","ARCADE","METAL"],"12":["TINE EP","FM BELL","FM BASS","BRASS","FM PAD","MARIMBA","FM ORGAN","FM PLUCK","DX TINE","BRASS SECT","SOLID BASS","BELLS","DX MARIMBA","CLAVINET","DRAWBARS","STRINGS","GLASS PAD","SYNC LEAD","HARP","KALIMBA","FLUTE","STEEL DRUM","SAW BASS","TUBULAR","PIANO"],"13":["LUSH PAD","WARM PAD","GLASS PAD","SLOW STRINGS","ENSEMBLE STR","SYNTH BRASS","SOFT BRASS","POLY KEYS","PWM KEYS","CLAV","SOFT LEAD","HOLLOW","BELLS","SWEEP PAD","SOFT AAH","ORGANISH","DEEP SUB","PUNCH BASS","RUBBER BASS","SYNC BASS","MORPH PAD","VINYL KEYS","WIDE STRINGS","CLOUD PAD","SHIMMER"],"14":["INIT TONE","BRASS 1","BRASS 2","BRASS 3","STRINGS 1","STRINGS 2","STRINGS 3","STRINGS 4","ORCHESTRA","ACO.GUITAR","JAZZ GUITAR","ELEC.GUITAR","SLAP BASS","SYNTH.BASS","ELEC.BASS 1","ELEC.BASS 2","HARP","BRASS 4","SAXOPHONE","CELLO","FLUTE","WHISTLE","HARMONICA","RECORDER","KOTO","PIANO 1","PIANO 2","PIANO 3","ELEC.PIANO","HONKY-TONK","FUNKY CLAV 1","FUNKY CLAV 2","HARPSICHORD","JAZZ ORGAN 1","JAZZ ORGAN 2","PIPE ORGAN 1","PIPE ORGAN 2","ACCORDION","VOICE 1","VOICE 2","VOICE 3","MUSIC BOX","VIBRAPHONE","XYLOPHONE","MARIMBA","MALLET LOG","AFRO PERC","BELLS","METALLIC","SYN STRINGS","FAT ENSEMBLE","SITAR","SYNTH.LEAD 1","SYNTH.LEAD 2","SYNTH.LEAD 3","SYNTH.LEAD 4","SWEEP 1","SYN DRUMS 1","SYN DRUMS 2","CONGA","STEEL DRUM","SWEEP 2","JET ROAR","MOTORCYCLE","TYPHOON"],"15":["EP","BELL","BASS","PLUCK","BRASS","GLASS PAD","HOLLOW","SQUARE LEAD","METAL","WOBBLE","CLAV","STRINGS","MARIMBA","DRONE","FEEDBACK","NOISE-ISH"]}
FACTORY_FIRST = {"2":0,"3":0,"5":0,"6":0,"7":0,"9":0,"11":0,"12":0,"13":0,"14":1,"15":0}
BIND_MARK, BIND_NOTE, BIND_FLAGS = 0xA6, 160 + 15, 176 + 15   # note[15] = the mark, flags[15] = factory index + 1


def _u16(b, off):
    return int.from_bytes(b[off:off + 2], "little")


def _u32(b, off):
    return int.from_bytes(b[off:off + 4], "little")


def sound_record_valid(r):
    """up_valid: used 0xA5, ver 1..5, engine < 16, 8 <= np <= (144 | 72), name[0] != 0, packed values <= 191"""
    if len(r) != SOUND_REC or r[0] != 0xA5 or not 1 <= r[1] <= 5 or r[2] >= len(ENGINE_NAMES) or not r[4]:
        return False
    if not 8 <= r[3] <= (144 if r[1] >= 4 else 72):
        return False
    return r[1] < 4 or all(v <= 191 for v in r[16:16 + r[3]])


def _record_name(r):
    return bytes(r[4:16]).split(b"\0")[0].decode("latin-1")


def _bank_ok(b):
    return len(b) == BANK_SIZE and _u32(b, 0) == UPB_MAGIC and _u16(b, 4) == SOUND_REC and _u16(b, 6) == SOUND_PER_BANK


def _new_bank():
    return UPB_MAGIC.to_bytes(4, "little") + SOUND_REC.to_bytes(2, "little") + SOUND_PER_BANK.to_bytes(2, "little") \
        + bytes(SOUND_PER_BANK * SOUND_REC)


def _store_at(kind, slot):
    """-> (backup id, index in that store, byte offset of the blob)"""
    k = slot - 1
    if kind in WHOLE_STORES:
        return PATCH_ID[kind], k, 16 + k * PATCH_SIZE[kind]
    return PATCH_ID[kind] + k // 16, k % 16, 16 + (k % 16) * PATCH_SIZE[kind]


def _store_ok(kind, i, b):
    if len(b) != STORE_SIZE[kind]:
        return False
    if kind == "va":
        return _u32(b, 0) == VAS_MAGIC and _u16(b, 4) in (2, 3) and _u16(b, 6) == 32 and _u16(b, 12) == 110
    if kind == "quad":
        return _u32(b, 0) == QUDS_MAGIC and _u16(b, 4) == 1 and _u16(b, 6) == 32 and _u16(b, 12) == 80
    return (_u32(b, 0) == (FM6U_MAGIC if kind == "fm6" else CZU_MAGIC) and _u16(b, 4) == 1 and _u16(b, 6) == 16 and
            _u16(b, 8) == 16 * (i - PATCH_ID[kind]) and _u16(b, 10) == PATCH_SIZE[kind] and not _u32(b, 12) >> 16)


def _new_store(kind, i):
    if kind in WHOLE_STORES:
        head = (VAS_MAGIC if kind == "va" else QUDS_MAGIC).to_bytes(4, "little") \
            + (3 if kind == "va" else 1).to_bytes(2, "little") + (32).to_bytes(2, "little") + bytes(4) \
            + PATCH_SIZE[kind].to_bytes(2, "little") + bytes(2)
    else:
        head = (FM6U_MAGIC if kind == "fm6" else CZU_MAGIC).to_bytes(4, "little") + (1).to_bytes(2, "little") \
            + (16).to_bytes(2, "little") + (16 * (i - PATCH_ID[kind])).to_bytes(2, "little") \
            + PATCH_SIZE[kind].to_bytes(2, "little") + bytes(4)
    return head + bytes(STORE_SIZE[kind] - 16)


def _store_blob(objs, kind, slot):
    """the blob of `kind` stored for slot (its used bit set in a well-formed store), or None"""
    i, k, off = _store_at(kind, slot)
    b = objs.get(i, b"")
    if not _store_ok(kind, i, b) or not _u32(b, 8 if kind in WHOLE_STORES else 12) >> k & 1:
        return None
    return bytes(b[off:off + PATCH_SIZE[kind]])


def _set_blob(objs, kind, slot, blob):
    """objs[store] with slot's blob set (bit set) or, blob None, cleared (bit cleared, bytes zeroed). An empty or
    malformed store (the FM-1 reads it as empty) is created from the header when a blob goes in, else left alone."""
    i, k, off = _store_at(kind, slot)
    b = objs.get(i, b"")
    if not _store_ok(kind, i, b):
        if blob is None:
            return
        b = _new_store(kind, i)
    b = bytearray(b)
    uo = 8 if kind in WHOLE_STORES else 12
    used = _u32(b, uo)
    used = used | 1 << k if blob is not None else used & ~(1 << k)
    b[uo:uo + 4] = used.to_bytes(4, "little")
    b[off:off + PATCH_SIZE[kind]] = blob if blob is not None else bytes(PATCH_SIZE[kind])
    objs[i] = bytes(b)


def _set_record(objs, slot, rec):
    """objs[bank] with slot's record replaced; an empty or malformed bank is created when a sound goes in"""
    i, off = 6 + (slot - 1) // 16, 8 + ((slot - 1) % 16) * SOUND_REC
    b = objs.get(i, b"")
    if not _bank_ok(b):
        if not any(rec):
            return
        b = _new_bank()
    objs[i] = bytes(b[:off]) + bytes(rec) + bytes(b[off + SOUND_REC:])


def _record(objs, slot):
    b = objs.get(6 + (slot - 1) // 16, b"")
    if not _bank_ok(b):
        return bytes(SOUND_REC)
    off = 8 + ((slot - 1) % 16) * SOUND_REC
    return bytes(b[off:off + SOUND_REC])


def pool_of(engine):
    """the preset pool a record is in (cr_bank.c cb_rec_engine): its engine if selectable, DIGITAL's FM6, another
    retired one (ANALOG since 0.14, SAMPLE, GRAIN, DRUM) the VA's"""
    return engine if str(engine) in FACTORY_PRESETS else 12 if engine == 1 else 13


def _bind_raw(r):
    """the factory preset index a valid record's binding names (cr_bank.c cb_bind_raw), None: an added preset"""
    if r[1] in (3, 5) or r[BIND_NOTE] != BIND_MARK or not r[BIND_FLAGS]:
        return None
    e, f = pool_of(r[2]), r[BIND_FLAGS] - 1
    if e != r[2] and r[2] != 1:
        return None                                # (ANALOG's, a retired engine's record: an added preset where it
                                                   # plays; a DIGITAL one binds in FM6's)
    return f if FACTORY_FIRST[str(e)] <= f < len(FACTORY_PRESETS[str(e)]) else None


def sound_bindings(records):
    """the 32 records (None: empty) -> per slot None, or {index, pos, label, name} of the factory preset it overwrites
    (cr_bank.c cb_bound: the first slot bound to a factory preset wins; a later one is an added preset)"""
    seen, out = set(), []
    for r in records:
        f = None if r is None else _bind_raw(r)
        e = None if f is None else pool_of(r[2])
        if f is None or (e, f) in seen:
            out.append(None)
            continue
        seen.add((e, f))
        pos = f - FACTORY_FIRST[str(e)] + 1
        out.append({"index": f, "pos": pos, "label": f"{ENGINE_NAMES[e]} {pos:02d}", "name": FACTORY_PRESETS[str(e)][f]})
    return out


def binding_of(bound):
    """a slot's binding as a sound file carries it: {"overwrites": the pool position, "name"} or None (added)"""
    return {"overwrites": bound["pos"], "name": bound["name"]} if bound else None


def parse_sound_objects(objs):
    """objs: {backup id: bytes} (b"" or missing: never written) -> the 32 slots, a dict each:
    slot, used, name, engine, engineName, patch (the kind when the slot's blob is stored, else None), record, blob,
    bound (None, or the factory preset it overwrites: sound_bindings), added (used and not bound)"""
    table = []
    for slot in range(1, SOUND_SLOTS + 1):
        r = _record(objs, slot)
        row = {"slot": slot, "used": False, "name": "", "engine": None, "engineName": "", "patch": None,
               "record": r, "blob": None, "bound": None, "added": False}
        if sound_record_valid(r):
            kind = ENGINE_PATCH.get(r[2])
            blob = kind and _store_blob(objs, kind, slot)
            row.update(used=True, name=_record_name(r), engine=r[2], engineName=ENGINE_NAMES[r[2]],
                       patch=kind if blob else None, blob=blob or None)
        table.append(row)
    for row, bound in zip(table, sound_bindings([row["record"] if row["used"] else None for row in table])):
        row.update(bound=bound, added=row["used"] and not bound)
    return table


def sound_line(row):
    """U05  MY PAD        FM6     patch     over FM6 02 FM BELL / U07  PAD 2  VA  patch  + / U06  (empty)"""
    if not row["used"]:
        return f"U{row['slot']:02d}  (empty)"
    kind = ENGINE_PATCH.get(row["engine"])
    tail = "" if not kind else "patch" if row["patch"] else "no patch"
    pool = f"over {row['bound']['label']} {row['bound']['name']}" if row.get("bound") else "+"
    return f"U{row['slot']:02d}  {row['name']:<12}  {row['engineName']:<7} {tail:<8}  {pool}"


def sound_file_name(slot, name):
    return f"choralroot-sound-U{slot:02d}-{re.sub(r'[^A-Za-z0-9-]', '_', name)}.json"


def export_sound(objs, slot, firmware):
    """slot 1..32 -> the sound file (a dict, "choralroot-sound" version 1)"""
    import base64
    row = parse_sound_objects(objs)[slot - 1]
    if not row["used"]:
        raise InstallError("emptyslot", f"U{slot:02d} is empty: nothing to export")
    patch = None
    if row["patch"]:
        patch = {"kind": row["patch"], "data": base64.b64encode(row["blob"]).decode("ascii")}
    return {"format": SOUND_FORMAT, "version": 1, "firmware": firmware,
            "created": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()), "slot": slot, "name": row["name"],
            "engine": row["engine"], "engineName": row["engineName"], "binding": binding_of(row["bound"]),
            "record": base64.b64encode(row["record"]).decode("ascii"), "patch": patch}


def check_sound_name(name):
    """None when name is a valid sound name (1..12 ASCII 32..126), else what is wrong"""
    if not isinstance(name, str) or not 1 <= len(name) <= 12 or any(not 32 <= ord(c) <= 126 for c in name):
        return "a name is 1 to 12 characters, ASCII 32..126 (letters, digits, space, punctuation)"
    return None


def read_sound_file(f):
    """validate a sound file (dict or JSON text) -> (record bytes, name, engine, patch (kind, bytes) or None);
    the JSON's name, when present, is written into the record; its "binding" (present or not) is not read: the
    record's binding bytes are kept as they are"""
    import base64
    import binascii
    import json

    def bad(msg):
        return InstallError("badsound", msg)

    def b64(v, what):
        if not isinstance(v, str):
            raise bad(f"{what} is missing or not base64 text")
        try:
            return base64.b64decode(v, validate=True)
        except (binascii.Error, ValueError):
            raise bad(f"{what} is not valid base64 (the file is damaged)")
    if isinstance(f, (str, bytes)):
        try:
            f = json.loads(f)
        except ValueError as e:
            raise bad(f"not a sound file: {e}")
    if isinstance(f, dict) and f.get("format") == "felucca-backup":
        raise bad("this is a whole backup, not a sound file (--restore writes a backup)")
    if not isinstance(f, dict) or f.get("format") != SOUND_FORMAT or f.get("version") != 1:
        raise bad("not a ChoralRoot sound file (format choralroot-sound, version 1)")
    rec = bytearray(b64(f.get("record"), "the record"))
    if len(rec) != SOUND_REC:
        raise bad(f"the record is {len(rec)} bytes, not {SOUND_REC}")
    if not sound_record_valid(rec):
        raise bad("the record is not a valid user sound (used / version / engine / parameter count / name / values)")
    engine = rec[2]
    if f.get("engine") is not None and f["engine"] != engine:
        raise bad(f"the file's engine ({f['engine']!r}) is not the record's ({engine} {ENGINE_NAMES[engine]})")
    name = f.get("name")
    if name is not None:
        why = check_sound_name(name)
        if why:
            raise bad(f"name {name!r}: {why}")
        rec[4:16] = name.encode("ascii").ljust(12, b"\0")
    else:
        name = _record_name(rec)
    patch, want = f.get("patch"), ENGINE_PATCH.get(engine)
    if patch is not None:
        if not isinstance(patch, dict) or patch.get("kind") not in PATCH_KINDS:
            raise bad("the patch is not {kind: va | fm6 | cz | quad, data: base64}")
        kind = patch["kind"]
        if kind != want:
            raise bad(f"a {kind} patch does not go with the engine {ENGINE_NAMES[engine]} "
                      + (f"(its patch kind is {want})" if want else "(it has no patch)"))
        data = b64(patch.get("data"), "the patch data")
        if len(data) != PATCH_SIZE[kind]:
            raise bad(f"the {kind} patch is {len(data)} bytes, not {PATCH_SIZE[kind]}")
        if kind == "va" and (data[0] != 0x56 or not 1 <= data[1] <= 3):
            raise bad("not a VA patch (its first bytes are not 'V' and a version 1..3)")
        if kind == "fm6" and (data[112] != 0x46 or data[113] != 1):
            raise bad("not an FM6 patch (bytes 112, 113 are not 'F', 1)")
        if kind == "quad" and (data[0] != 0x51 or not 1 <= data[1] <= 3):
            raise bad("not an FM TONE patch (its first bytes are not 'Q' and a version 1..3)")
        patch = (kind, bytes(data))
    return bytes(rec), name, engine, patch


def _changes(old, new):
    """the objects whose bytes changed, the stores first, the banks last"""
    return [(i, new[i]) for i in (9, 10, 11, 12, 13, 22, 6, 7) if i in new and new[i] != old.get(i, b"")]


def import_sound(objs, slot, sound):
    """sound: read_sound_file's result -> [(id, bytes)] to write. The patch goes into its store at slot (bit set);
    the other kinds' blobs of that slot are cleared (as the FM-1 does when a sound is saved)."""
    rec, _name, _engine, patch = sound
    new = dict(objs)
    for kind in PATCH_KINDS:
        _set_blob(new, kind, slot, patch[1] if patch and patch[0] == kind else None)
    _set_record(new, slot, rec)
    return _changes(objs, new)


def rename_sound(objs, slot, name):
    why = check_sound_name(name)
    if why:
        raise InstallError("usage", f"name {name!r}: {why}")
    r = _record(objs, slot)
    if not sound_record_valid(r):
        raise InstallError("emptyslot", f"U{slot:02d} is empty: nothing to rename")
    new = dict(objs)
    _set_record(new, slot, r[:4] + name.encode("ascii").ljust(12, b"\0") + r[16:])
    return _changes(objs, new)


def delete_sound(objs, slot):
    new = dict(objs)
    for kind in PATCH_KINDS:
        _set_blob(new, kind, slot, None)
    _set_record(new, slot, bytes(SOUND_REC))
    return _changes(objs, new)


# ------------------------------------------------------- .syx export / import ---
# docs/SOUNDS.md ".syx export and import": an FM6 slot's voice as a DX7 single voice (F0 43 0n 00 01 1B <155> sum F7)
# and a CZ-1 slot's tone as Casio's tone dump (F0 44 00 00 7n 30 <288 nibbles> F7). An import builds the record from
# the engine's template (generated from the firmware: tests/sound_templates.c; --check keeps these the firmware's).

SOUND_TEMPLATES = {"fm6": "pQQMW1NZWCBJTVBPUlQAAKhKhpp8QEBAQHxAQEBAQEBAQEJBgEC/QEBAQEBAUEJAgEBAQEBAQEBAQEBAaEBBQb9AQEBAQEBAQEBAQEBAQL9Av0BAv0C/QEC/QL9AQL9Av0BAQEBAQEBAQEAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
                   "cz": "pQQOW1NZWCBJTVBPUlQAAKhKhpp8QEBAQHxAQEBAQEBAQEJBgEC/QEBAQEBAUEJAgEBAQEBAQEBAQEBAaEBBQb9AQEBAQEBAQEBAQEBAQL9Av0BAv0C/QEC/QL9AQL9Av0BAQEBAQEBAQEIAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"}
SYX_ENGINE = {"fm6": 12, "cz": 14}
SYX_EMPTY_NAME = {"fm6": "FM6 VOICE", "cz": "CZ TONE"}
FM6_VCED, FM6_VMEM, FM6_BANK, CZ_TONE = 155, 128, 4096, 144
# eng_fm6.c fm6_max: the highest value of each VCED byte (per operator 21, then the voice's 19; the name 32..126)
FM6_OPMAX = (99,) * 11 + (3, 3, 7, 3, 7, 99, 1, 31, 99, 14)
FM6_VMAX = (99,) * 8 + (31, 7, 1, 99, 99, 99, 99, 1, 5, 7, 48)
# fm6_core.c FM6_FNDEF (Dexed's function settings, ENGINE = MARK1) in eng_fm6.c FM6_FNBITS bits each, LSB first
FM6_FNBITS = (4, 4, 4, 1, 7, 1, 7, 3, 7, 3, 7, 3, 7, 3, 1, 2)
FM6_FNDEF = (3, 3, 0, 0, 0, 0, 99, 1, 0, 0, 0, 0, 0, 0, 0, 1)


def _fm6_fn_pack(fn):
    w = sh = 0
    for v, n in zip(fn, FM6_FNBITS):
        w |= (v & (1 << n) - 1) << sh
        sh += n
    return w.to_bytes(8, "little")


FM6_FN_DEFAULT = _fm6_fn_pack(FM6_FNDEF)       # blob bytes 114..121 of an imported voice


def _fm6_sanitize(v):
    """fm6_sanitize: every VCED value clamped into its range, a name byte outside 32..126 a space"""
    out = bytearray(FM6_VCED)
    for i in range(FM6_VCED):
        x = v[i]
        if i >= 145:
            out[i] = x if 32 <= x <= 126 else 32
        else:
            out[i] = min(x, FM6_OPMAX[i % 21] if i < 126 else FM6_VMAX[i - 126])
    return bytes(out)


def _fm6_vmem_to_vced(b):
    """fm6_unpack: a 128-byte VMEM record -> the 155-byte VCED (sanitized)"""
    v = bytearray(FM6_VCED)
    for k in range(6):
        o, d = b[k * 17:k * 17 + 17], k * 21
        for i in range(11):
            v[d + i] = o[i] & 0x7F
        v[d + 11], v[d + 12] = o[11] & 3, o[11] >> 2 & 3
        v[d + 13], v[d + 20] = o[12] & 7, o[12] >> 3 & 15
        v[d + 14], v[d + 15] = o[13] & 3, o[13] >> 2 & 7
        v[d + 16] = o[14] & 0x7F
        v[d + 17], v[d + 18] = o[15] & 1, o[15] >> 1 & 31
        v[d + 19] = o[16] & 0x7F
    for i in range(9):
        v[126 + i] = b[102 + i] & 0x7F
    v[134] &= 31
    v[135], v[136] = b[111] & 7, b[111] >> 3 & 1
    for i in range(4):
        v[137 + i] = b[112 + i] & 0x7F
    v[141], v[142], v[143] = b[116] & 1, b[116] >> 1 & 7, b[116] >> 4 & 7
    v[144] = b[117] & 0x7F
    for i in range(10):
        v[145 + i] = b[118 + i] & 0x7F
    return _fm6_sanitize(v)


def _fm6_vced_to_vmem(v):
    """fm6_pack: a 155-byte VCED -> the 128-byte VMEM record"""
    b = bytearray(FM6_VMEM)
    for k in range(6):
        o, d = v[k * 21:k * 21 + 21], k * 17
        for i in range(11):
            b[d + i] = o[i] & 0x7F
        b[d + 11] = o[11] & 3 | (o[12] & 3) << 2
        b[d + 12] = o[13] & 7 | (o[20] & 15) << 3
        b[d + 13] = o[14] & 3 | (o[15] & 7) << 2
        b[d + 14] = o[16] & 0x7F
        b[d + 15] = o[17] & 1 | (o[18] & 31) << 1
        b[d + 16] = o[19] & 0x7F
    for i in range(9):
        b[102 + i] = v[126 + i] & 0x7F
    b[110] &= 31
    b[111] = v[135] & 7 | (v[136] & 1) << 3
    for i in range(4):
        b[112 + i] = v[137 + i] & 0x7F
    b[116] = v[141] & 1 | (v[142] & 7) << 1 | (v[143] & 7) << 4
    b[117] = v[144] & 0x7F
    for i in range(10):
        b[118 + i] = v[145 + i] & 0x7F
    return bytes(b)


def _fm6_pack7(s):
    """fm6_pack7: eight 7-bit bytes -> seven (the eighth in the top bits) (128 -> 112)"""
    d = bytearray()
    for i in range(0, len(s), 8):
        d += bytes(s[i + j] & 0x7F | (s[i + 7] >> j & 1) << 7 for j in range(7))
    return bytes(d)


def _fm6_unpack7(s):
    """fm6_unpack7: 112 -> 128"""
    d = bytearray()
    for i in range(0, len(s), 7):
        g = s[i:i + 7]
        d += bytes(x & 0x7F for x in g) + bytes([sum((x >> 7) << j for j, x in enumerate(g))])
    return bytes(d)


def fm6_blob_to_vced(blob):
    """a 128-byte FM6 blob -> the 155-byte VCED (fm6_blob_read's voice; the function settings are left out)"""
    if len(blob) != PATCH_SIZE["fm6"] or blob[112] != 0x46 or blob[113] != 1:
        raise InstallError("badsound", "not an FM6 patch (bytes 112, 113 are not 'F', 1)")
    return _fm6_vmem_to_vced(_fm6_unpack7(blob[:112]))


def vced_to_fm6_blob(vced):
    """a 155-byte VCED -> a 128-byte FM6 blob (fm6_blob_make: every value clamped, the function defaults)"""
    if len(vced) != FM6_VCED:
        raise InstallError("badsound", f"a DX7 voice is {FM6_VCED} bytes, not {len(vced)}")
    return _fm6_pack7(_fm6_vced_to_vmem(_fm6_sanitize(vced))) + b"F\x01" + FM6_FN_DEFAULT + bytes(6)


def _dx7_sum(data):
    return -sum(data) & 0x7F


def fm6_vced_syx(vced):
    """the DX7 single voice: F0 43 00 00 01 1B <155> checksum F7 (163 bytes)"""
    return bytes([0xF0, 0x43, 0x00, 0x00, 0x01, 0x1B]) + bytes(vced) + bytes([_dx7_sum(vced), 0xF7])


def cz_tone_syx(tone):
    """Casio's tone dump: F0 44 00 00 70 30 <288 nibbles, the low first> F7 (295 bytes)"""
    if len(tone) != CZ_TONE:
        raise InstallError("badsound", f"a CZ-1 tone is {CZ_TONE} bytes, not {len(tone)}")
    return bytes([0xF0, 0x44, 0x00, 0x00, 0x70, 0x30]) + bytes(n for x in tone for n in (x & 15, x >> 4)) + b"\xF7"


def parse_syx(data):
    """a .syx (or a raw voice / bank / tone) -> ("fm6", [VCED 155 ...]) or ("cz", [tone 144 ...]); InstallError
    "badsound" for anything else (a CZ-101 / 1000 128-byte tone, a bad checksum, an unknown file)"""
    def bad(msg):
        return InstallError("badsound", msg)
    data = bytes(data)
    if not data:
        raise bad("the file is empty")
    if data[0] != 0xF0:                           # a raw voice / bank / tone, by its length
        if len(data) == FM6_VCED:
            return "fm6", [_fm6_sanitize(data)]
        if len(data) == FM6_BANK:
            return "fm6", [_fm6_vmem_to_vced(data[k * 128:k * 128 + 128]) for k in range(32)]
        if len(data) == CZ_TONE:
            return "cz", [data]
        raise bad(f"not a .syx file (no SysEx F0) and not a raw DX7 voice (155), bank (4096) or CZ-1 tone (144): "
                  f"{len(data)} bytes")
    frames, i = [], 0
    while i < len(data):
        if data[i] != 0xF0:
            if data[i] in (0x0A, 0x0D, 0x20):    # stray line ends between frames
                i += 1
                continue
            raise bad(f"not a SysEx file (byte {i} is {data[i]:02X} between frames)")
        j = data.find(b"\xF7", i)
        if j < 0:
            raise bad("a SysEx frame is not finished (no F7)")
        frames.append(data[i:j + 1])
        i = j + 1
    kind, out = None, []
    for f in frames:
        if f[1:2] == b"\x43" and len(f) >= 4 and f[2] & 0xF0 == 0 and f[3] == 0x00:      # DX7 single voice
            if len(f) != 163 or f[4:6] != b"\x01\x1B":
                raise bad(f"a DX7 single voice is 163 bytes (F0 43 0n 00 01 1B .. F7), not {len(f)}")
            body, what, voices = f[6:161], "voice", None
        elif f[1:2] == b"\x43" and len(f) >= 4 and f[2] & 0xF0 == 0 and f[3] == 0x09:    # DX7 32-voice bank
            if len(f) != 4104 or f[4:6] != b"\x20\x00":
                raise bad(f"a DX7 32-voice bank is 4104 bytes (F0 43 0n 09 20 00 .. F7), not {len(f)}")
            body, what = f[6:4102], "bank"
            voices = [_fm6_vmem_to_vced(body[k * 128:k * 128 + 128]) for k in range(32)]
        elif f[1:4] == b"\x44\x00\x00" and len(f) > 6 and f[4] & 0xF0 == 0x70 and f[5] == 0x30:   # Casio tone
            n = len(f) - 7
            if n == 256:
                raise bad("a CZ-101 / CZ-1000 tone (128 bytes): the FM-1 converts those only live, over MIDI "
                          "(send it to the CZ-1 part), not into a slot")
            if n != 288 or any(x > 15 for x in f[6:-1]):
                raise bad(f"not a CZ-1 tone dump (288 nibbles), {n} bytes of data")
            if kind not in (None, "cz"):
                raise bad("the file mixes DX7 voices and CZ tones")
            kind = "cz"
            out.append(bytes(f[6 + 2 * k] | f[7 + 2 * k] << 4 for k in range(CZ_TONE)))
            continue
        else:
            continue                              # another device's message: not ours
        if f[-2] != _dx7_sum(body):
            raise bad(f"the DX7 {what}'s checksum is wrong (the file is damaged)")
        if kind not in (None, "fm6"):
            raise bad("the file mixes DX7 voices and CZ tones")
        kind = "fm6"
        out += voices if voices is not None else [_fm6_sanitize(body)]
    if not out:
        raise bad("no DX7 voice or CZ-1 tone in this .syx file")
    return kind, out


def _syx_name(raw, kind):
    s = "".join(c if 32 <= ord(c) <= 126 else " " for c in raw.decode("latin-1")).strip()[:12].rstrip()
    return s or SYX_EMPTY_NAME[kind]


def sound_from_syx(kind, patch):
    """a voice (VCED 155) or a tone (144) -> (record, name, engine, (kind, blob)) as read_sound_file returns: the
    engine's template record named after the voice / tone"""
    import base64
    if kind == "fm6":
        blob = vced_to_fm6_blob(patch)
        name = _syx_name(bytes(patch[145:155]), kind)
    elif kind == "cz":
        if len(patch) != CZ_TONE:
            raise InstallError("badsound", f"a CZ-1 tone is {CZ_TONE} bytes, not {len(patch)}")
        blob, name = bytes(patch), _syx_name(bytes(patch[128:144]), kind)
    else:
        raise InstallError("badsound", f"no .syx kind {kind!r}")
    rec = bytearray(base64.b64decode(SOUND_TEMPLATES[kind]))
    rec[4:16] = name.encode("ascii").ljust(12, b"\0")
    rec[BIND_NOTE], rec[BIND_FLAGS] = BIND_MARK, 0   # bound to nothing: an added preset of its engine's pool
    return bytes(rec), name, SYX_ENGINE[kind], (kind, blob)


def export_syx(objs, slot):
    """slot 1..32 (an FM6 / CZ-1 sound with its patch) -> (the .syx bytes, the sound's name)"""
    row = parse_sound_objects(objs)[slot - 1]
    if not row["used"]:
        raise InstallError("emptyslot", f"U{slot:02d} is empty: nothing to export")
    if row["engine"] not in SYX_ENGINE.values():
        raise InstallError("nosyx", f"U{slot:02d} is a {row['engineName']} sound: only FM6 and CZ-1 sounds export "
                                    "as .syx (--export-sound saves any sound)")
    if not row["patch"]:
        raise InstallError("nosyx", f"U{slot:02d} ({row['engineName']}) has no patch stored: nothing to export as .syx")
    if row["patch"] == "fm6":
        return fm6_vced_syx(fm6_blob_to_vced(row["blob"])), row["name"]
    return cz_tone_syx(row["blob"]), row["name"]


def syx_file_name(slot, name):
    return sound_file_name(slot, name)[:-len(".json")] + ".syx"


def read_sounds(bl, progress=lambda done, total: None):
    """LIST, then GET of the sound objects the FM-1 lists -> {id: bytes} (an object that changed while it was read
    is read again once)"""
    for attempt in range(2):
        man = [o for o in bk_manifest(bl.request(BK_LIST, [], 3.0)) if o["id"] in SOUND_IDS]
        total, done, objs, changed = sum(o["size"] for o in man), 0, {}, False
        for o in man:
            def chunk(n):
                nonlocal done
                done += n
                progress(done, total)
            objs[o["id"]] = bk_get_object(bl, o["id"], o["size"], chunk)
            if bk_crc(objs[o["id"]]) != o["crc"]:
                changed = True
                break
        if not changed:
            return objs
    raise BackupError("the FM-1 changed its sounds while they were read: try again with it stopped")


def write_sounds(bl, changed, progress=lambda done, total: None, busy=lambda: None):
    """PUT each changed object in order; the first failure stops it (BackupError naming the object)"""
    total, done = sum(len(b) for _i, b in changed), 0
    for k, (i, b) in enumerate(changed):
        def chunk(n):
            nonlocal done
            done += n
            progress(done, total)
        try:
            bk_put_object(bl, i, b, chunk, busy)
        except BackupError as e:
            rest = [object_name(j) for j, _b in changed[k + 1:]]
            more = f"; not written: {', '.join(rest)}" if rest else ""
            raise BackupError(f"writing {object_name(i)}: {e}" + (" (the FM-1 refused the content)" if e.rc == 2 else "")
                              + more, e.rc)


def run_sounds(up, a, out, ask):
    """--sounds, --export-sound N FILE, --import-sound N FILE, --export-syx N FILE, --import-syx N FILE [--voice V],
    --rename-sound N NAME, --delete-sound N"""
    import json
    import os
    sound = None
    if a.import_sound:                            # the file is checked before anything is sent
        path = a.import_sound[1]
        try:
            text = open(path, encoding="utf-8").read()
        except (OSError, UnicodeDecodeError) as e:
            raise InstallError("badsound", f"cannot read {path}: {getattr(e, 'strerror', None) or e}")
        sound = read_sound_file(text)
    if a.import_syx:                              # the .syx too: checked before anything is sent
        path = a.import_syx[1]
        try:
            data = open(path, "rb").read()
        except OSError as e:
            raise InstallError("badsound", f"cannot read {path}: {e.strerror}")
        kind, patches = parse_syx(data)
        v = a.voice or 1
        if v > len(patches):
            raise InstallError("usage", f"--voice {v}: {path} holds {len(patches)} "
                                        f"{'voice' if kind == 'fm6' else 'tone'}{'s' if len(patches) > 1 else ''}")
        sound = sound_from_syx(kind, patches[v - 1])
    bl, (version, fam) = open_backup(up)
    try:
        if fam != "choralroot":
            raise InstallError("nosounds", f"the Sounds need a ChoralRoot firmware (the FM-1 runs {version}: it has "
                                           "the banks but not the patch stores; --backup saves everything)")
        prog = Progress(out)
        try:
            objs = read_sounds(bl, lambda d, t: prog.put(f"reading the sounds {d * 100 // max(1, t):3d}%"))
        finally:
            prog.end()
        if 6 not in objs or 7 not in objs:
            raise InstallError("nosounds", f"{version} does not list the user sound banks")
        table = parse_sound_objects(objs)
        if a.sounds:
            print(f"user sounds on {version}:", file=out)
            for row in table:
                print(sound_line(row), file=out)
            return 0
        if a.export_sound:
            slot, path = a.export_sound
            f = export_sound(objs, slot, version)
            if os.path.isdir(path):
                path = os.path.join(path, sound_file_name(slot, f["name"]))
            try:
                with open(path, "w", encoding="utf-8") as fh:
                    json.dump(f, fh, indent=2)
                    fh.write("\n")
            except OSError as e:
                raise InstallError("other", f"cannot write {path}: {e.strerror}")
            print(f"{sound_line(table[slot - 1])}\nsaved: {path}", file=out)
            return 0
        if a.export_syx:
            slot, path = a.export_syx
            data, name = export_syx(objs, slot)
            if os.path.isdir(path):
                path = os.path.join(path, syx_file_name(slot, name))
            try:
                with open(path, "wb") as fh:
                    fh.write(data)
            except OSError as e:
                raise InstallError("other", f"cannot write {path}: {e.strerror}")
            print(f"{sound_line(table[slot - 1])}\nsaved: {path} ({len(data)} bytes)", file=out)
            return 0
        if a.import_sound or a.import_syx:
            slot = (a.import_sound or a.import_syx)[0]
            row = table[slot - 1]
            print(f"import {sound[1]} ({ENGINE_NAMES[sound[2]]}, {'patch' if sound[3] else 'no patch'}) into U{slot:02d}",
                  file=out)
            if row["used"] and not a.yes and not ask(f"U{slot:02d} holds {row['name']} ({row['engineName']}). "
                                                     "Replace it? [y/N] "):
                print("cancelled", file=out)
                return 1
            changed = import_sound(objs, slot, sound)
        elif a.rename_sound:
            slot, name = a.rename_sound
            changed = rename_sound(objs, slot, name)
        else:
            slot = a.delete_sound
            row = table[slot - 1]
            what = f"{row['name']} ({row['engineName']})" if row["used"] else "(empty)"
            if not a.yes and not ask(f"Delete U{slot:02d} {what}? [y/N] "):
                print("cancelled", file=out)
                return 1
            changed = delete_sound(objs, slot)
        if not changed:
            print(f"{sound_line(table[slot - 1])}\nnothing to write: the FM-1 holds this already", file=out)
            return 0
        missing = [object_name(i) for i, _b in changed if i not in objs]
        if missing:
            raise InstallError("nosounds", f"{version} does not list {', '.join(missing)}: this sound cannot be written")
        prog = Progress(out)
        try:
            write_sounds(bl, changed, lambda d, t: prog.put(f"writing U{slot:02d} {d * 100 // max(1, t):3d}%"),
                         lambda: prog.put("busy: stop the loop on the FM-1"))
        finally:
            prog.end()
        print(f"written: {', '.join(object_name(i) for i, _b in changed)}", file=out)
        print(sound_line(parse_sound_objects(read_sounds(bl))[slot - 1]), file=out)
        return 0
    finally:
        bl.link.close()


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
    if sound_op(a):
        return run_sounds(up, a, out, ask)
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
    print("before writing: the FM-1 must be fully charged (it updates on its battery, and this tool cannot read it), "
          "plugged straight into the computer, and the computer kept awake; an interrupted write can leave it dark "
          "with no USB device at all (docs/INSTALL-COMPAT.md)", file=out)
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


def sound_op(a):
    return [o for o, v in (("--sounds", a.sounds), ("--export-sound", a.export_sound), ("--import-sound", a.import_sound),
                           ("--export-syx", a.export_syx), ("--import-syx", a.import_syx),
                           ("--rename-sound", a.rename_sound), ("--delete-sound", a.delete_sound)) if v]


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
    ap.add_argument("--sounds", action="store_true", help="list the 32 user sounds (ChoralRoot): 'over FM6 02 FM BELL' "
                                                          "for one saved over a factory preset, '+' for one added")
    ap.add_argument("--export-sound", nargs=2, metavar=("N", "FILE"),
                    help="save user sound N (1..32) to a sound file FILE (a directory: choralroot-sound-UNN-NAME.json)")
    ap.add_argument("--import-sound", nargs=2, metavar=("N", "FILE"),
                    help="write the sound file FILE into slot N (asks before replacing a sound; --yes does not)")
    ap.add_argument("--export-syx", nargs=2, metavar=("N", "FILE"),
                    help="save the FM6 voice / CZ-1 tone of user sound N as a .syx FILE (DX7 single voice / Casio tone "
                         "dump; a directory: choralroot-sound-UNN-NAME.syx)")
    ap.add_argument("--import-syx", nargs=2, metavar=("N", "FILE"),
                    help="a .syx FILE (a DX7 voice or bank, a CZ-1 tone) into slot N as an FM6 / CZ-1 sound, added "
                         "to its engine's presets after the factory ones (asks before replacing a sound; --yes does "
                         "not)")
    ap.add_argument("--voice", metavar="V", help="with --import-syx: voice V (1..32) of a bank or multi-voice file "
                                                 "(default 1)")
    ap.add_argument("--rename-sound", nargs=2, metavar=("N", "NAME"), help="rename user sound N (1 to 12 characters)")
    ap.add_argument("--delete-sound", metavar="N", help="empty slot N (asks; --yes does not)")
    a = ap.parse_args(argv)
    ops = sound_op(a)
    if len(ops) > 1:
        ap.error(f"{' and '.join(ops)}: one sound operation at a time")
    if ops and (a.package or a.info or a.backup or a.restore):
        ap.error(f"{ops[0]} goes alone (with --port, --yes): not with a package, --info, --backup or --restore")

    def slot_of(v):
        if not re.fullmatch(r"\d+", v) or not 1 <= int(v) <= SOUND_SLOTS:
            ap.error(f"{ops[0]}: N is a slot 1..{SOUND_SLOTS}, not {v!r}")
        return int(v)
    for o in ("export_sound", "import_sound", "export_syx", "import_syx", "rename_sound"):
        if getattr(a, o):
            setattr(a, o, (slot_of(getattr(a, o)[0]), getattr(a, o)[1]))
    if a.delete_sound:
        a.delete_sound = slot_of(a.delete_sound)
    if a.voice is not None:
        if not a.import_syx:
            ap.error("--voice goes with --import-syx")
        if not re.fullmatch(r"\d+", a.voice) or not 1 <= int(a.voice) <= 32:
            ap.error(f"--voice: V is a voice 1..32, not {a.voice!r}")
        a.voice = int(a.voice)
    if a.rename_sound and check_sound_name(a.rename_sound[1]):
        ap.error(f"--rename-sound: {check_sound_name(a.rename_sound[1])}")
    if a.info and (a.package or a.backup or a.restore):
        ap.error("--info goes alone")
    if not (a.info or a.package or a.backup or a.restore or ops):
        ap.error("give a PACKAGE.fwsc, --backup FILE, --restore FILE, --info or --sounds")
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
