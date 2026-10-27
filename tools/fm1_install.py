#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Install a Felucca package (.fwsc) on an FM-1 (or an M-Vave SMK-37: SMK37.md) over USB-MIDI.

The same update as the web installer (web/fm1ota.js): step 1, the
running firmware reads parts of the package and starts the update loader;
step 2, the loader reads the whole package and writes it. In both steps the
device asks (SysEx read requests on the logical image) and we answer. Then
the FM-1 restarts and the installed identity is checked.

  fm1_install.py PACKAGE.fwsc [--port NAME] [--yes] [--force]
  fm1_install.py FM-1.fwsc            (the official V15 file: back to the stock firmware)
  fm1_install.py --info [--port NAME]
  fm1_install.py --debug              (ChoralRoot: the device's diagnostic counters, read-only; docs/USB-AUDIO.md)
  fm1_install.py --rescan-midi        (macOS: CoreMIDI rescans its devices, as "Rescan MIDI" in Audio MIDI Setup's
                                       MIDI Studio: for an FM-1 that is on USB while its MIDI port stays offline
                                       after a restart. Every MIDI program's ports restart with it)
  fm1_install.py --stub test|rescue   (ChoralRoot with the boot stub, docs/BOOT-SAFETY.md. test: restart once with
                                       the stub's loader check on, then print the stub's --debug lines. rescue: restart
                                       into update mode as OCT- at power-on does; leave it by installing a package,
                                       or by switching the FM-1 off and on)
  fm1_install.py --screen off|on      (ChoralRoot: the screen off until --screen on or a key, button or knob on the
                                       unit; MIDI in does not wake it: tests over MIDI keep it dark)
  fm1_install.py --backup FILE        (save what is stored on the FM-1: settings, user sounds, loops, samples ...)
  fm1_install.py --restore FILE       (write a backup back; ChoralRoot restarts afterwards)
  fm1_install.py PACKAGE.fwsc --backup FILE [--restore FILE]   (back up, install, restore onto the new firmware)
  fm1_install.py --sounds             (ChoralRoot: list the 32 user sounds U01..U32)
  fm1_install.py --restore-favorites FILE [--yes]   (0.22 on: a --favorites text's marks back into object 23)
  fm1_install.py --favorites          (ChoralRoot 0.22 on: the sounds marked as favorites, read-only; P5's programs as
                                       lines for assets/prophet5-factory/shipped.txt: tools/p5_shipped_from_favorites.py)
  fm1_install.py --export-sound N FILE   (user sound N to a sound file; FILE may be a directory)
  fm1_install.py --import-sound N FILE   (a sound file into slot N) [--yes]
  fm1_install.py --rename-sound N NAME   /   --delete-sound N [--yes]
  fm1_install.py --export-syx N FILE     (an FM6 voice / CZ-1 tone as .syx: DX7 single voice / Casio tone dump)
  fm1_install.py --import-syx N FILE [--voice V] [--yes]   (a DX7 voice or bank, or a CZ-1 tone, into slot N)

Several FM-1s on one computer (docs/INSTALL-COMPAT.md "Several FM-1s"): --info lists every FM-1 it finds, each with its
unit ID (ChoralRoot from 0.22: 8 hex digits, also under Options > Version on the unit). --unit ID, with any command,
uses only the FM-1 whose INFO answers with that ID (none or several: exit 3 / 9, nothing done); an install with --unit
takes the update mode only from the one MIDI port that appears when that FM-1 restarts, and checks the ID again when it
is back; it never rescans MIDI. Without --unit, while several FM-1s answer, a command that writes to an FM-1 or
restarts it is refused (exit 9) with their IDs; a read-only one reads the first, as with one FM-1, and says so.
  fm1_install.py --info                       (every FM-1: its port, identity, version, unit ID)
  fm1_install.py PACKAGE.fwsc --unit A1B2C3D4 (the same with every command)

An install that was interrupted (the cable, the battery): on a ChoralRoot with the boot stub (docs/BOOT-SAFETY.md;
from 0.16 on, 0.15 and older do not have it) the FM-1 comes back in update mode at the next power-on (three LED flashes, a black screen): run
the install again to finish it. The same on purpose, on a working unit too: hold OCT- alone while switching on,
three flashes, run the install (or switch off and on: nothing was written). One long flash instead: switch off for
10 s and try once more. --debug prints a "boot stub" line on a firmware that has it.

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
install finishes the write. Needs python-rtmidi (pip install python-rtmidi; mido is no longer used).

The SMK-37 Pro, SMK-37 Elite and MKE-P37 (and the Starrykey 37 Play) are one keyboard under several names: a package
for one of them installs on any of them, never an FM-1 package on an SMK or back, and an update loader is written
only when it is of the package's keyboard (the same rule on every path: the install and the resume of an update).

Exit codes: 0 done, 1 cancelled or other error, 2 bad arguments or package,
3 FM-1 not found, 4 connection lost or the device stopped, 5 timeout (no
loader / no restart), 6 wrong model (or a loader of another keyboard), or another identity after the install,
7 the backup, restore or sound write failed (or the firmware has no backup protocol, or is not ChoralRoot for the
sounds), 8 the running firmware is one ChoralRoot
is not installed over (Sloop, the Felucca 0.x betas, unknown ones: docs/INSTALL-COMPAT.md; --force overrides), 9 several
FM-1s where one is needed: connected without --unit for a command that writes or restarts, several ports answering with
the --unit ID, or more than one new port (or a new port that cannot be told apart) at an install's restart with --unit.

No MIDI call can hold the tool: the ports are opened, listed and closed in worker processes, each wait has a bound,
and a port that does not open or answer in time is given up with the codes above (3 not found, 4 lost, 5 no restart)
and what was waited for. A port is always closed by its worker; a worker is killed only when it has missed its bound.
The tool itself ends (exit 1) when it has taken no step for a minute ("MIDI" in the source says why).
"""
import argparse
import hashlib
import json
import os
import queue
import re
import subprocess
import sys
import threading
import time
from collections import Counter

HS_QUERY = bytes([0xF0, 0x00, 0x32, 0x45, 0x00, 0x00, 0x00, 0x40, 0x7F, 0xF7])
UPGRADE = bytes([0xF0, 0x22, 0x24, 0x35, 0x7F, 0xF7])
FINISH_CHECK, FINISH_WRITE = 0xE0000000, 0xF0000000
MAXDATA = 512
BLOCKS, BLK, KEEP = 20, 0x30, 0x2F         # BLOCKS: the identity blocks (more may follow, see block_count)
MAX_BLOCKS = 64
LOADER_MARK = b"FELUCCA-LOADER-1"
# the unmodified official FM-1 V15 (FM-1.fwsc from M-VAVE): returning to it is allowed without --force,
# as the web installer's "Return to official V15" (web/fm1pkg.js validateStockPackage)
STOCK_V15_SHA256 = "db1642b2b6fa5c2cccb11ffd13878068bb28601678d3644049f99dc40e7edb8a"
# the MIDI ports probed: the FM-1 / SMK-37 models' and the firmwares' names; other gear is never sent anything
PORT_RE = re.compile(r"fm-1|smk|elite|mke-p37|starry|felucca|sloop|clipper|melodee|fomni|x0x|makkuro|maku|rainbow|amb-1|"
                     r"choral|orbit|snoop|vector|vox|ota|composite|sinco|usb-midi", re.I)

# seconds; the tests shorten them
DELAY = {"open": 0.3, "start": 2.0, "reply": 0.01, "loader": 3.0, "reboot": 3.0, "retry": 1.0,
         "poll": 1.0, "hs": 1.0, "info": 1.5, "idle_check": 8.0, "idle_write": 180.0,
         "wait_loader": 30.0, "wait_reboot": 40.0,
         "port": 10.0,      # a MIDI worker's answer: the port list, an open (its start included), a send, "is it gone?"
         "close": 1.0,      # a MIDI worker's close of its ports, and its death once killed
         "stuck": 30.0,     # a MIDI worker ends itself when a call of its own has not returned in this (above the rest)
         "gone": 0.2,       # while an answer is waited for: how often "has the FM-1 left?" is asked
         "dead": 60.0,      # the tool ends itself when it has taken no step for this long (Watch)
         "exit": 5.0}       # the tool's exit, whatever holds it (leave)

EXIT = {"usage": 2, "badpkg": 2, "badbackup": 2, "notfound": 3, "model": 6, "lost": 4, "stopped": 4, "badreq": 4,
        "nobackup": 7, "backup": 7, "nosounds": 7, "unsupported": 8, "badsound": 2,
        "noloader": 5, "noreturn": 5, "mismatch": 6, "several": 9}


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
    txt = d[6:33].split(b"\0")[0].decode("latin-1")    # (some firmwares end the text early)
    m = re.fullmatch(r"([^_]+)_(\d+)", txt)
    return Identity(txt, m[1], int(m[2])) if m else None


# --------------------------------------------------------------- package ---

def _id_char(m, i):
    c = (m - i - 1) & 0xFF
    return 0x20 <= c < 0x7F


def identity_blocks(raw):
    """the identity blocks: 20, or more when the identity is longer (the Starrykey 37 Play V016's
    "STARRYKEY-37 PLAY_016" takes 21): further identity markers count only when unused 0x7D markers follow
    them (the 36-block layout of the stock SMK-37 packages)"""
    def marker(k):
        return raw[k * BLK + KEEP] if (k + 1) * BLK <= len(raw) else None
    n = BLOCKS
    while n < MAX_BLOCKS and (m := marker(n)) is not None and m != 0x7D and _id_char(m, n):
        n += 1
    return n if n > BLOCKS and marker(n) == 0x7D else BLOCKS


def product_of(raw):
    """the package identity ("FM-1_906", "SMK-37 Pro_016"): one marker byte after each identity block (0x7D: none);
    past the first 20 only when the package really has those blocks (the layout's count)"""
    if len(raw) < BLOCKS * BLK:
        raise InstallError("badpkg", "not an FM-1 package (too short)")
    ib = identity_blocks(raw)
    n = BLOCKS if ib == BLOCKS else min(ib, layout(raw)[1])
    return "".join(chr((m - i - 1) & 0xFF) for i in range(n) if (m := raw[i * BLK + KEEP]) != 0x7D)


def block_count(raw):
    """interleave blocks: the identity blocks, then more while the marker is the unused 0x7D (the stock SMK-37
    packages: 36). A 20-block package can have 0x7D there by chance: layout() checks the CRCs and falls back."""
    n = identity_blocks(raw)
    while n < MAX_BLOCKS and (n + 1) * BLK <= len(raw) and raw[n * BLK + KEEP] == 0x7D:
        n += 1
    return n


def logical_image(raw, blocks=BLOCKS):
    """the image the device reads during an update: the package without the marker bytes"""
    out = bytearray()
    for i in range(blocks):
        out += raw[i * BLK:i * BLK + KEEP]
    return bytes(out + raw[blocks * BLK:])


def _crc16(data, crc=0):
    """CRC-16/XMODEM, as the package and the device use"""
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


def _dec(b):
    """JieLi ENC stream cipher (its own inverse)"""
    out, key = bytearray(b), 0xFFFF
    for i in range(len(out)):
        out[i] ^= key & 0xFF
        key = ((key << 1) ^ (0x1021 if key & 0x8000 else 0)) & 0xFFFF
    return bytes(out)


def files_ok(image):
    """the logical image's header and file list are intact and flash.bin (0) / ota.bin (100) match their CRCs.
    The stock packer's other entries store a CRC of something else: checked for their bounds only."""
    if len(image) < 0x40:
        return False
    hdr = _dec(image[0:0x40])
    if _crc16(hdr[2:0x40]) != int.from_bytes(hdr[0:2], "little"):
        return False
    count = int.from_bytes(hdr[8:10], "little")
    if not 1 <= count <= 8 or len(image) < 0x40 + 0x50 * count:
        return False
    for i in range(count):
        e = _dec(image[0x40 + i * 0x50:0x90 + i * 0x50])
        typ = int.from_bytes(e[0:2], "little")
        off, size = int.from_bytes(e[8:12], "little"), int.from_bytes(e[12:16], "little")
        if off + size > len(image):
            return False
        if typ in (0, 100) and _crc16(image[off:off + size]) != int.from_bytes(e[4:6], "little"):
            return False
    return True


def layout(raw):
    """-> (logical image, block count): 20 blocks, or the detected count when the files' CRCs agree"""
    n = block_count(raw)
    if n != BLOCKS:
        image = logical_image(raw, n)
        if files_ok(image) or not files_ok(logical_image(raw, BLOCKS)):
            return image, n
    return logical_image(raw, BLOCKS), BLOCKS


def deinterleave(raw):
    """the logical image: 20 blocks, or the detected count when the files' CRCs agree"""
    return layout(raw)[0]


def model_of(text):
    return text.split("_")[0].removeprefix("ota-")


# the SMK-37 Pro, SMK-37 Elite, MKE-P37, Starrykey 37 Play and the SMK-37+VA mod: one keyboard under several model
# names, so a package for one runs on all of them (also "SMK-37", the name other firmwares report); an FM-1 is another
# device
SMK_FAMILY = re.compile(r"SMK-37([ +].*)?|MKE-P37|STARRYKEY-37.*", re.I)


def our_loader(text):
    """the update loader of SLOOP / Felucca packages (ours: "ota-FM-1_9XX", "ota-SMK-37 Pro_9XX" and the other SMK
    models', Makkuro37 _7XX). The stock updater's loader ("ota-SMK-37 Pro_016") and ChanceTheMaker's ("ota-SMK-37_931")
    may lay the image out differently: an SMK left in their update mode is never written (but for the official
    package, the return to stock)"""
    return bool(re.fullmatch(r"ota-(FM-1|SMK-37 Pro|SMK-37 Elite|MKE-P37)_[789]\d\d", text, re.I))


def same_keyboard(a, b):
    """the models of identities a and b are the same hardware (never an FM-1 and an SMK)"""
    ma, mb = model_of(a), model_of(b)
    return ma == mb or bool(SMK_FAMILY.fullmatch(ma) and SMK_FAMILY.fullmatch(mb))


# which firmware runs: may ChoralRoot be installed over it? The same table as web/fm1ota.js classifyFirmware
# (docs/INSTALL-COMPAT.md). identity: the handshake text; info: the backup protocol's INFO version, None when the
# firmware does not answer. The 9xx identities are shared by every Felucca-based firmware (Felucca 0.9-beta FM-1_909,
# Felucca 1.0 FM-1_910, Sloop 2.0 FM-1_920 = ChoralRoot's), so they are told apart by the INFO text only.
RECOVERY_URL = "https://github.com/Quixotic7/MvaveFM1Unbricker"
REFUSE_REASON = "An install over it has left an FM-1 that no longer starts, and the data it leaves in the flash is not known to be safe for ChoralRoot."


def classify_firmware(identity, info, port=None):
    """-> (verdict "allow" | "refuse" | "loader", kind, name). port: the MIDI port's name, when known"""
    m = re.fullmatch(r"(ota-)?([^_]+)_(\d+)", identity or "", re.I)
    v = (info or "").strip()
    if not m:
        return "refuse", "unknown", f"an unknown firmware ({identity or 'no identity'})"
    if m[1]:
        return "loader", "loader", identity
    num = int(m[3])
    if SMK_FAMILY.fullmatch(m[2]):                # the M-Vave SMK-37 (SMK37.md): its own table
        return classify_smk(identity, m[2], num, v)
    if m[2] != "FM-1":
        return "refuse", "unknown", identity
    if BUBBA_PORT.search(port or ""):             # Bubba Machine answers FM-1_003, as an old official firmware would
        return "refuse", "bubba", f"Bubba Machine ({identity})"
    r = REMOTEFM1_ID.fullmatch(m[3])
    if r:                                         # RemoteFM1 0.2 on (fm1-base's identity, see REMOTEFM1_ID)
        return "allow", "remotefm1", (f"RemoteFM1 {int(r[1])}.{int(r[2])}.{int(r[3])}" if r[1]
                                      else "RemoteFM1 (development build)")
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


def classify_smk(identity, model, num, v):
    """an SMK-37 (any of its model names): the official firmware and the Felucca-family ports (SLOOP, Felucca and the
    ports on their SMK board layer: its owner tag keeps each firmware's data apart) are installed over. The bare name
    "SMK-37_9NN" is ChanceTheMaker's SMK firmwares': another package format and their own updater, so not ours"""
    if model.upper() == "SMK-37" and 900 <= num < 1000:
        return "refuse", "other", f"a firmware with its own updater ({identity})"
    if 1 <= num < 100:
        return "allow", "stock", f"the official M-VAVE firmware ({identity})"
    if re.match(r"choralroot\b", v, re.I):
        return "allow", "choralroot", v
    if 700 <= num < 1000:
        return "allow", "smk", f"an SMK-37 port ({identity}{', ' + v if v else ''})"
    return "refuse", "unknown", f"an unknown firmware ({identity})"


# Bubba Machine (github.com/Erbubar23/bubba-machine): its USB product name is "FM-1 Bubba Machine", its handshake
# FM-1_003. Its updater speaks M-VAVE's protocol but takes only a package with M-VAVE's own update loader and the
# unit's boot head (hal/fm1/fm1_update.c), so it refuses every Felucca-family package, ChoralRoot's included, before
# it writes anything; the official V15 goes through (docs/INSTALL-COMPAT.md "Bubba Machine")
BUBBA_PORT = re.compile(r"bubba", re.I)
# RemoteFM1 0.2 on (github.com/Quixotic7/RemoteFM1; on fm1-base + felucca-bsp, felucca-bsp's updater): fm1-base's
# identity, its fm1.toml id_prefix "FM-1_52" + "00" (a development build) or + XXYYZZ (release X.Y.Z, two digits
# each: 0.2.1 = FM-1_52000201). No INFO answer. RemoteFM1 0.1 answered as the official V15 (FM-1_015). Only
# RemoteFM1's prefix: other fm1-base firmwares' update paths are not known here (docs/INSTALL-COMPAT.md "RemoteFM1")
REMOTEFM1_ID = re.compile(r"52(?:00|(\d\d)(\d\d)(\d\d))")
BUBBA_TEXT = ("Bubba Machine's updater takes only M-VAVE's official package, so ChoralRoot cannot be installed over it "
              "directly. Return to the official V15 first: this tool "
              "with M-VAVE's FM-1.fwsc (python3 tools/fm1_install.py FM-1.fwsc), the web installer's \"Return to "
              "official V15\" or Bubba Machine's own installer; then install ChoralRoot.")


def refusal_text(name):
    return (f"Installing over {name} is not supported: {REFUSE_REASON} "
            f"Return to the official V15 firmware with the installer you used for {name} first, then install ChoralRoot. "
            f'If an FM-1 is already dark (black screen, a "WL82 UBOOT1.00" USB disk): {RECOVERY_URL}')


# ------------------------------------------------------------------ MIDI ---
# The ports are opened by python-rtmidi in worker processes (this file run as --midi-worker): one per link, one that
# lists the ports; never in the tool's own process. Why (reproduced without an FM-1: a virtual port that sends notes
# while a mido input on it is closed stops the interpreter within a few closes, with the stack seen on the unit:
# mido/backends/rtmidi.py _close <- ports.py close):
#  - python-rtmidi 1.5.8 keeps the GIL through every one of its calls and hands each incoming message to Python on
#    CoreMIDI's receive thread, which needs the GIL (mido's rtmidi backend always sets that callback). Closing an
#    input (close_port: MIDIPortDispose) waits until the delivery in progress has returned. A message that arrives
#    while the port is being closed therefore stops the interpreter for good: the close holds the GIL and waits for
#    the delivery, the delivery waits for the GIL. No thread of the process runs again, so a close moved to a thread
#    cannot be given up either (its join never returns).
#  - the FM-1 sends such a message whenever it plays or sends its clock (Options > MIDI Clock: Out): the notes and
#    pulses that wait while a SysEx goes out leave in the reply's last USB packet (usb.c ep1_tx), so a long reply
#    (DEBUG: ~650 bytes, 14 USB packets) very often has one right behind it, and the tool closed right after a reply
#    (--stub test, --debug, --info). The FM-1's answers to another program on the same port (a browser page) are
#    such messages too. The update's closes follow one-packet messages, and its loader plays nothing: the install
#    met this far less often.
#  - so the worker polls rtmidi's queue (no callback, the clock filtered: nothing of Python runs on CoreMIDI's
#    thread and the deadlock cannot form), and whatever a CoreMIDI call does, the tool itself only waits on a pipe,
#    with a bound.
#  - a port is always closed by its worker (close_port, the client deleted, then the worker leaves), as mido closed
#    it; with no callback the close cannot meet the deadlock. A worker is killed only when it has not answered
#    within its bound. Not as a way to drop a link: on the unit, workers killed with their ports open while the FM-1
#    left the bus (a restart) were followed by the returning FM-1 staying offline in CoreMIDI until a MIDI rescan,
#    and by answers of the FM-1 that were never seen. So the link of an FM-1 that restarts is closed as soon as its
#    answer is read (the firmware leaves the bus 150 ms after it: cr_backup.c ed_service), and what a worker has
#    received is handed to the tool before it answers "gone" or closes.

NO_RTMIDI = b"!python-rtmidi"


def frame(tag, data=b""):
    return tag + len(data).to_bytes(4, "little") + bytes(data)


def read_frame(rd):
    """-> (tag, data), or None at the end of the pipe"""
    h = rd.read(5)
    d = rd.read(int.from_bytes(h[1:], "little")) if len(h) == 5 else None
    return None if d is None else (h[:1], d)


def midi_devices():
    """(in a worker, macOS) what CoreMIDI and the USB bus know, to tell an FM-1 that is on the bus while its MIDI
    port is offline: {"midi": [[name, offline], ...] CoreMIDI's devices, the absent ones too (it keeps every device
    it has seen), "usb": the names on the bus (ioreg)}. Nothing is sent to a device. Elsewhere: both empty."""
    import ctypes
    out = {"midi": [], "usb": []}
    try:
        cm = ctypes.CDLL("/System/Library/Frameworks/CoreMIDI.framework/CoreMIDI")
        cf = ctypes.CDLL("/System/Library/Frameworks/CoreFoundation.framework/CoreFoundation")
        out["usb"] = re.findall(r"\+-o (.+?)@", subprocess.run(["ioreg", "-p", "IOUSB", "-w0"], capture_output=True,
                                                                text=True, timeout=5).stdout)
    except (OSError, subprocess.SubprocessError):
        return out
    cm.MIDIGetNumberOfDevices.restype = ctypes.c_ulong
    cm.MIDIGetDevice.restype, cm.MIDIGetDevice.argtypes = ctypes.c_uint32, [ctypes.c_ulong]
    cm.MIDIObjectGetStringProperty.argtypes = [ctypes.c_uint32, ctypes.c_void_p, ctypes.POINTER(ctypes.c_void_p)]
    cm.MIDIObjectGetIntegerProperty.argtypes = [ctypes.c_uint32, ctypes.c_void_p, ctypes.POINTER(ctypes.c_int32)]
    cf.CFStringGetCString.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_long, ctypes.c_uint32]
    cf.CFRelease.argtypes = [ctypes.c_void_p]
    name_id, off_id = (ctypes.c_void_p.in_dll(cm, k) for k in ("kMIDIPropertyName", "kMIDIPropertyOffline"))
    for i in range(cm.MIDIGetNumberOfDevices()):
        dev, s, off, buf = cm.MIDIGetDevice(i), ctypes.c_void_p(), ctypes.c_int32(), ctypes.create_string_buffer(256)
        if cm.MIDIObjectGetStringProperty(dev, name_id, ctypes.byref(s)) == 0 and s:
            cf.CFStringGetCString(s, buf, 256, 0x08000100)     # (UTF-8)
            cf.CFRelease(s)
        cm.MIDIObjectGetIntegerProperty(dev, off_id, ctypes.byref(off))
        out["midi"].append([buf.value.decode("utf-8", "replace"), bool(off.value)])
    return out


def midi_rescan():
    """(in a worker, macOS) MIDIRestart(), Audio MIDI Setup's "Rescan MIDI" -> its status (0: done). Every MIDI
    program's ports are restarted with it: only on request (--rescan-midi), never by itself."""
    import ctypes
    return ctypes.CDLL("/System/Library/Frameworks/CoreMIDI.framework/CoreMIDI").MIDIRestart()


def midi_worker(stuck):
    """--midi-worker: the MIDI side of one link, or of the port list (above). Frames (a tag, a 4-byte length, the
    bytes) on stdin and stdout. In: n the port names, d midi_devices(), r midi_rescan(), o [input, output] open
    them, s a message to send, l is the port gone?, c close the ports and leave. Out: a the answer to each command
    in turn ("!" and a text: it failed), m a SysEx that came in; what has come in goes out before every answer.
    The end of stdin (the tool is gone), SIGTERM and SIGHUP are a c: the ports are closed all the same. Without
    the GIL (faulthandler's own thread), a command that has not returned after `stuck` seconds ends the worker:
    the tool gave up on it long before, and no worker stays behind in a call that does not return."""
    import faulthandler
    import signal
    try:
        import rtmidi
    except ImportError:
        rtmidi = None
    rd, wr, null = sys.stdin.buffer, sys.stdout.buffer, open(os.devnull, "w")
    cmds = queue.SimpleQueue()                            # (its put is safe in a signal handler)
    inp = out = names = None
    alone = False                                         # the tool's end of the pipes is closed
    signal.signal(signal.SIGINT, signal.SIG_IGN)          # ^C is the tool's: it closes its workers
    for s in ("SIGTERM", "SIGHUP"):
        if hasattr(signal, s):
            signal.signal(getattr(signal, s), lambda *a: cmds.put(None))

    def pump():
        while True:
            c = read_frame(rd)
            cmds.put(c)
            if c is None:
                return

    def put(tag, data):
        nonlocal alone
        try:
            wr.write(frame(tag, data))
            wr.flush()
        except OSError:
            alone = True

    def taken():
        """what has come in goes to the tool (the SysEx; the notes are dropped): nothing that was received stays
        in rtmidi's queue behind an answer"""
        while inp and (m := inp.get_message()):           # (message, delta)
            if m[0][0] == 0xF0:
                put(b"m", m[0])

    def ports(cls):
        """the port names as a client made for the question sees them (then deleted), as mido listed them: what
        an FM-1 that left and came back has always been found with"""
        p = cls()
        try:
            return p.get_ports()
        finally:
            p.delete()

    def do(tag, data):
        nonlocal inp, out, names
        if tag == b"n":
            return json.dumps([ports(rtmidi.MidiIn), ports(rtmidi.MidiOut)]).encode()
        if tag == b"d":
            return json.dumps(getattr(rtmidi, "devices", midi_devices)()).encode()   # (the tests' rtmidi has its own)
        if tag == b"r":
            return str(getattr(rtmidi, "rescan", midi_rescan)()).encode()
        if tag == b"o":
            names = json.loads(data)                      # (port_labels' names: "NAME [2]", the second NAME)
            if names[0] is not None:
                inp = rtmidi.MidiIn()
                inp.ignore_types(sysex=False, timing=True, active_sense=True)   # SysEx in; the clock stays out
                k = port_index(inp.get_ports(), names[0])
                inp.open_port(inp.get_ports().index(names[0]) if k is None else k)   # (not there: ValueError)
            if names[1] is not None:
                out = rtmidi.MidiOut()
                k = port_index(out.get_ports(), names[1])
                out.open_port(out.get_ports().index(names[1]) if k is None else k)
        elif tag == b"s":
            out.send_message(data)
        elif tag == b"l":
            # asked of the open port: the list is the process's own, a client made for the question would see the
            # same; and no client comes and goes while an answer is on its way (the tool asks every DELAY gone)
            if (port_index(inp.get_ports(), names[0]) if inp else port_index(out.get_ports(), names[1])) is not None:
                return b"\0"
            taken()                                       # gone: what it sent before it left, first
            return b"\1"
        elif tag == b"c":
            if inp:
                inp.close_port()
                taken()                                   # (up to the close)
                inp.delete()
                inp = None
            if out:
                out.close_port()
                out.delete()
                out = None
        return b""

    threading.Thread(target=pump, daemon=True).start()
    while True:
        taken()
        try:
            c = None if alone else cmds.get(timeout=0.001 if inp else None)   # a command at once; else the input
        except queue.Empty:                                                   # again in 1 ms
            continue
        asked, c = c is not None, c or (b"c", b"")
        faulthandler.dump_traceback_later(stuck, exit=True, file=null)
        taken()
        try:
            r = do(*c) if rtmidi else NO_RTMIDI
        except Exception as e:
            r = b"!" + str(e).encode()
        faulthandler.cancel_dump_traceback_later()
        if asked:
            put(b"a", r)
        if c[0] == b"c":
            os._exit(0)                                   # (no destructor of rtmidi is left to run)


class Watch:
    """The tool's own process cannot stay behind. Started for a run from the command line, a daemon thread ends the
    process (exit 1, saying where its threads are) when no step was taken for DELAY["dead"] seconds: every MIDI
    operation is a step (Worker.ask) and every wait between two is far shorter; a question to the user stops the
    clock. The thread always runs: nothing in this process keeps the GIL in a call (the MIDI calls are the
    workers')."""
    t, paused = 0.0, False                        # the time of the last step; a question is being asked

    @classmethod
    def step(cls):
        cls.t = time.monotonic()

    @classmethod
    def start(cls):
        cls.step()
        threading.Thread(target=cls.run, daemon=True).start()

    @classmethod
    def run(cls):
        while cls.paused or time.monotonic() - cls.t < DELAY["dead"]:
            time.sleep(min(1.0, DELAY["dead"] / 4))
        t = threading.Timer(DELAY["exit"], os._exit, (1,))    # (should stderr itself be what holds)
        t.daemon = True
        t.start()
        try:
            import faulthandler
            say(f"error: no step for {DELAY['dead']:g} s: the tool ends itself. Its threads:")
            sys.stderr.flush()
            faulthandler.dump_traceback(file=sys.stderr, all_threads=True)
        except (OSError, ValueError, AttributeError):
            pass
        leave(1)


class Worker:
    """a --midi-worker process. ask(): one command -> its answer, or None when it has not come within the bound:
    the worker is then killed (it is in a call that does not return) and answers None from then on. close(): the
    worker closes its ports and leaves by itself."""
    live = []                                     # leave() closes what is left: no worker outlives the tool

    def __init__(self):
        # close_fds=False: Python then starts the worker by posix_spawn (macOS, Linux), not by fork: no copy of
        # this process, with its threads' locks and its command line, exists before an exec. The pipes of the other
        # workers are not inherited (Python opens every descriptor close-on-exec).
        self.p = subprocess.Popen([sys.executable, os.path.abspath(__file__), "--midi-worker", str(DELAY["stuck"])],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE, close_fds=False)
        self.q, self.ans, self.lock, self.dead = queue.Queue(), queue.Queue(), threading.Lock(), False
        Worker.live.append(self)
        threading.Thread(target=self.pump, daemon=True).start()

    def pump(self):
        while (f := read_frame(self.p.stdout)):
            (self.q if f[0] == b"m" else self.ans).put(f[1])
        self.ans.put(None)                        # the worker is gone

    def ask(self, tag, data=b"", secs=None):
        Watch.step()
        with self.lock:
            r = None
            if not self.dead:
                try:
                    self.p.stdin.write(frame(tag, data))
                    self.p.stdin.flush()
                    r = self.ans.get(timeout=DELAY["port"] if secs is None else secs)
                except (OSError, ValueError, queue.Empty):
                    pass
            if r is None:
                self.kill()
            return r

    def close(self):
        if self.ask(b"c", secs=DELAY["close"]) is not None:   # closed: it leaves by itself
            try:
                self.p.wait(DELAY["close"])
            except subprocess.TimeoutExpired:
                pass
        self.kill()

    def kill(self):
        """the end of a worker that has left, or the last resort for one that missed its bound"""
        self.dead = True
        try:
            Worker.live.remove(self)
        except ValueError:
            pass
        try:
            if self.p.poll() is None:
                self.p.kill()
            self.p.wait(DELAY["close"])
            self.p.stdin.close()
        except (OSError, ValueError, subprocess.TimeoutExpired):
            pass


def no_rtmidi():
    return InstallError("usage", "needs python-rtmidi (pip install python-rtmidi)")


class MidiLink:
    """one MIDI in / out pair (either may be None) in its worker; the SysEx that came in as a queue"""

    def __init__(self, in_name, out_name):
        self.w = Worker()
        self.q = self.w.q
        r = self.w.ask(b"o", json.dumps([in_name, out_name]).encode())
        if r != b"":
            self.w.close()                        # (what it did open is closed)
            if r == NO_RTMIDI:
                raise no_rtmidi()
            if r is None:
                raise TimeoutError(f"opening {in_name or out_name}")
            raise OSError(r[1:].decode(errors="replace"))
        time.sleep(DELAY["open"])   # CoreMIDI may drop a message sent just after opening

    @property
    def lost(self):
        """the port is no longer listed (or the worker does not say). What the FM-1 sent before it left is in the
        queue by now: the worker hands it over before it answers."""
        return self.w.ask(b"l") != b"\0"

    def send(self, pkt):
        """any MIDI message (the tool's own are SysEx)"""
        return self.w.ask(b"s", pkt) == b""

    def read(self, timeout):
        try:
            return self.q.get(timeout=max(timeout, 0))
        except queue.Empty:
            return None

    def drain(self):
        while not self.q.empty():
            self.q.get_nowait()

    def close(self):
        """the worker closes its ports and leaves (CoreMIDI's own way, whatever became of the FM-1; and before the
        next open: Windows opens a port once); killed only when it has not within the bound"""
        self.w.close()


class MidiBackend:
    """this computer's MIDI ports (python-rtmidi, in worker processes)"""
    lister = None                                 # the worker that lists the ports (one for the process)

    def ask(self, tag, what):
        if not MidiBackend.lister or MidiBackend.lister.dead:
            MidiBackend.lister = Worker()
        r = MidiBackend.lister.ask(tag)
        if r == NO_RTMIDI:
            raise no_rtmidi()
        if r is None:
            raise TimeoutError(what)
        return r

    def names(self, k):
        r = self.ask(b"n", "the list of the MIDI ports")
        return [] if r[:1] == b"!" else port_labels(json.loads(r)[k])    # (two ports of one name: both, the 2nd "[2]")

    def input_names(self):
        return self.names(0)

    def output_names(self):
        return self.names(1)

    def open(self, in_name, out_name):
        return MidiLink(in_name, out_name)

    def known(self):
        """midi_devices(): CoreMIDI's devices with their offline mark, the names on the USB bus"""
        r = self.ask(b"d", "CoreMIDI's device list")
        return {} if r[:1] == b"!" else json.loads(r)

    def rescan(self):
        """midi_rescan() -> None, or why not"""
        r = self.ask(b"r", "the MIDI rescan")
        return None if r == b"0" else r.decode(errors="replace").lstrip("!")


def bounded(fn, secs):
    """fn() in a daemon thread, waited for at most secs -> (True, its result), or (False, None) when it has not
    returned: it is left behind (a daemon thread does not hold the exit, and leave() does not wait for anything).
    What fn raised is raised here. The tool's own backend keeps its bounds by itself (above); this is for any other
    (the tests' simulated FM-1, a backend whose calls can be left: python-rtmidi's in this process could not)."""
    box = []

    def run():
        try:
            box.append((fn(), None))
        except BaseException as e:
            box.append((None, e))
    t = threading.Thread(target=run, daemon=True)
    t.start()
    t.join(secs)
    if not box:
        return False, None
    if box[0][1] is not None:
        raise box[0][1]
    return True, box[0][0]


class Link:
    """a backend's link as the tool uses it: its lost and its close cannot hold the tool, whatever the backend does.
    close(): the backend's own close, always (the port is closed, never dropped), waited for at most twice DELAY
    close, then left behind."""

    def __init__(self, link):
        self.link, self.send, self.read, self.drain, self.shut = link, link.send, link.read, link.drain, False

    @property
    def lost(self):
        try:
            done, v = bounded(lambda: self.link.lost, DELAY["port"] + DELAY["close"])
        except Exception:
            done, v = False, None
        return bool(v) or not done                # (no answer: gone too)

    def close(self):
        if not self.shut:
            self.shut = True
            try:
                bounded(self.link.close, 2 * DELAY["close"])
            except Exception:
                pass


def port_labels(raw):
    """the ports as the tool names and opens them: a name that several ports share (two FM-1s: "ChoralRoot FM-1"
    twice, as CoreMIDI may list them) is the first one's, " [2]", " [3]" ... are added to the next ones', in the
    system's order; a name of one port stays as it is (one FM-1: the names are the system's)"""
    seen, out = {}, []
    for n in raw:
        k = seen[n] = seen.get(n, 0) + 1
        out.append(n if k == 1 else f"{n} [{k}]")
    return out


def port_index(raw, label):
    """the index in the system's list `raw` of the port `label` names (port_labels), or None"""
    labels = port_labels(raw)
    return labels.index(label) if label in labels else None


def port_base(label):
    """the system's name of the port a label names (port_labels: " [k]" off, k >= 2)"""
    m = re.fullmatch(r"(.*) \[(\d+)\]", label)
    return m[1] if m and int(m[2]) >= 2 else label


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


class Probe:
    """what one port answered (Updater.survey): the identity, and from a running firmware its INFO (version, family,
    unit ID or None; None: no backup protocol)"""

    def __init__(self, name, ident, info):
        self.name, self.ident, self.info = name, ident, info

    @property
    def uid(self):
        return self.info[2] if self.info else None

    @property
    def version(self):
        return self.info[0] if self.info else None


def id_text(p):
    """a probe's unit ID in words (--info)"""
    if p.uid:
        return p.uid
    if p.ident.loader:
        return "none in update mode"
    if not p.info:
        return "none (no INFO answer: the stock firmware, or another without the backup protocol)"
    return "none (not ChoralRoot: ChoralRoot from 0.22 has one)" if p.info[1] != "choralroot" else \
        "none (a ChoralRoot from before the unit ID: an update brings it)"


def units_text(probes):
    """one line per FM-1: port, identity, version, unit ID"""
    w = max(len(p.name) for p in probes)
    return "\n".join(f"  {p.name:<{w}}  {p.ident.text:<12}  {p.version or ('update mode' if p.ident.loader else '-'):<20}  "
                     f"unit ID {p.uid or 'none'}" for p in probes)


def several_text(probes, what):
    return (f"{len(probes)} FM-1s are connected: name the FM-1 {what} with --unit ID (fm1_install.py --info lists "
            f"them):\n{units_text(probes)}\n  An FM-1 without a unit ID (update mode, the stock firmware, another firmware, "
            "a ChoralRoot from before the unit ID) cannot be named: unplug the other FM-1s and run this again.")


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
    """unit (--unit ID): only the FM-1 whose INFO answers with that ID is used (select(): the port it is on, found
    before the first command; mine(): every port a command or a wait takes is checked again). acting: the command
    writes to the FM-1 or restarts it; without --unit it is refused while several FM-1s answer (ready())"""

    def __init__(self, backend, port=None, unit=None, acting=False, out=None):
        self.backend = backend
        self.port = port
        self.gave_up = None                       # the last MIDI call that did not return within its bound
        self.unit = unit.upper() if unit else None
        self.acting, self.out, self.checked = acting, out, False
        self.only = None                          # --unit: the port the unit is on (select; after a restart, verify)
        self.loader_port = None                   # --unit: the update-mode port its restart brought (wait_new)
        self.fresh = None                         # --unit: the ports before the last restart (a Counter of the
        #                                           system's names), for a firmware that comes back without an ID
        self.unverified = None                    # --unit: what came back without an ID and was taken (fresh)

    def ready(self):
        """once, before the first port a command takes: --unit finds its unit (select); without it, several FM-1s
        refuse a command that writes or restarts, and a read-only one says which it reads. With at most one
        candidate port nothing is asked: one FM-1 runs as it always has"""
        if self.checked:
            return
        self.checked = True
        if self.unit:
            p = self.select()
            if self.out:
                print(f"unit {self.unit}: {p.version} ({p.ident.text}) on port {p.name}", file=self.out)
            return
        if len(self.candidates(True)) < 2:
            return
        fm1 = [p for p in self.survey(True)[0] if model_of(p.ident.text) == "FM-1"]
        if len(fm1) < 2:
            return
        if self.acting:
            raise InstallError("several", several_text(fm1, "this command is for"))
        if self.out:
            print(f"note: {len(fm1)} FM-1s are connected; this reads the first that answers, as with one (--unit ID "
                  f"names one):\n{units_text(fm1)}", file=self.out)

    def survey(self, only_port=True):
        """every candidate port asked the identity and (a running firmware) INFO, each link closed again ->
        ([Probe of each port that answered], [the ports that did not])"""
        outs = self.names(outs=True)
        ins = self.names() if outs else []
        got, silent = [], []
        for name in self.candidates(only_port, ins, every=True) if outs else []:
            out = pair_output(name, outs)
            link = self.open(name, out) if out else None
            ident = info = None
            if link:
                try:
                    ident = handshake(link, 2)
                    info = device_info(BackupLink(link), with_id=True) if ident and not ident.loader else None
                finally:
                    link.close()
            if ident:
                got.append(Probe(name, ident, info))
            else:
                silent.append(name)
        return got, silent

    def select(self):
        """--unit: the one port whose FM-1 answers INFO with the ID -> its Probe (self.only). None, or several:
        InstallError (exit 3, 9), nothing sent to any FM-1 but the identity and INFO questions"""
        probes, _silent = self.survey(True)
        hits = [p for p in probes if p.uid == self.unit]
        if len(hits) == 1:
            self.only = hits[0].name
            return hits[0]
        if hits:
            raise InstallError("several", f"{len(hits)} ports answer with unit ID {self.unit}: "
                                          f"{', '.join(p.name for p in hits)}; nothing was done")
        seen = f"\n  what answered:\n{units_text(probes)}" if probes else ""
        raise InstallError("notfound", f"no FM-1 answers with unit ID {self.unit}{seen}\n  An FM-1 in update mode or "
                                       "without ChoralRoot's unit ID cannot be named: unplug the other FM-1s and run "
                                       "this without --unit")

    def mine(self, link, ident, name, ins):
        """--unit: is the FM-1 on this port the unit? A running firmware: its INFO's ID. Update mode (no ID): only
        the one port the switch brought (wait_new). A firmware that answers without an ID (one installed that has
        none): only as the one new port since the restart (self.fresh), whose name no other port has"""
        if not self.unit:
            return True
        if ident.loader:
            return name == self.loader_port
        info = device_info(BackupLink(link), with_id=True)
        if info and info[2]:
            return info[2] == self.unit
        if self.unverified and name == self.only:     # (taken so after the install's restart: verify)
            return True
        if self.fresh is None:
            return False
        now, b = Counter(port_base(n) for n in ins), port_base(name)
        if now[b] > self.fresh[b] and now[b] == 1:
            self.unverified = f"{ident.text}" + (f" {info[0]}" if info else "")
            return True
        return False

    def ports(self):
        """the system's port names now, as a Counter (two FM-1s of one name: that name twice)"""
        return Counter(port_base(n) for n in self.names())

    def wait_new(self, before, target, secs):
        """--unit, after the unit was asked to restart into update mode: the one MIDI port that appeared since
        `before` (self.ports() while it was there), once the unit's own port (`target`) has gone -> the Device of
        its update loader, or None in secs. Before anything is written, InstallError (exit 9) when more than one
        new port appears, when the new port's name is another port's too, when it answers as something else than
        an update loader, or when the unit's port is still there beside it (twice in a row)"""
        end, tb, stay = time.monotonic() + secs, port_base(target), 0
        while time.monotonic() < end:
            now = self.ports()
            new, gone = now - before, before - now
            if sum(new.values()) > 1:
                raise InstallError("several", f"more than one new MIDI port appeared while this FM-1 restarted into "
                                              f"update mode ({', '.join(sorted(new.elements()))}): which one is its "
                                              "update mode is not known, and nothing was written. Unplug the other "
                                              "FM-1s, then run the install again without --unit: the FM-1 stays in "
                                              "update mode until it is done")
            if new:
                name = next(iter(new))
                if not gone[tb]:
                    stay += 1
                    if stay >= 2:
                        raise InstallError("several", f"a new MIDI port appeared ({name}) while this FM-1's port "
                                                      f"({target}) is still there: nothing was written. Unplug the "
                                                      "other FM-1s and run the install again without --unit")
                elif now[name] > 1:
                    raise InstallError("several", f"the new MIDI port has the name of another port ({name}): which "
                                                  "is this FM-1's update mode is not known, and nothing was written. "
                                                  "Unplug the other FM-1s and run the install again without --unit")
                else:
                    outs = self.names(outs=True)
                    out = pair_output(name, outs) if outs else None
                    link = self.open(name, out) if out else None
                    ident = handshake(link, 2) if link else None
                    if ident and ident.loader:
                        self.loader_port = name
                        return Device(link, ident, name)
                    if link:
                        link.close()
                    if ident:
                        raise InstallError("several", f"the new MIDI port {name} answers as {ident.text}, not as an "
                                                      "update loader: nothing was written. Unplug the other FM-1s "
                                                      "and run the install again without --unit")
            time.sleep(min(DELAY["retry"], max(0, end - time.monotonic())))
        return None

    def names(self, outs=False):
        """the MIDI inputs (or outputs); none when the backend does not answer within the bound"""
        try:
            done, v = bounded(self.backend.output_names if outs else self.backend.input_names,
                              DELAY["port"] + DELAY["close"])
        except TimeoutError:
            done = False
        if not done:
            self.gave_up = "the list of the MIDI ports"
        return v if done else []

    def open(self, name, out):
        """-> the Link of an input / output pair (either may be None), or None: no such port, or the open did not
        return within the bound"""
        try:
            done, link = bounded(lambda: self.backend.open(name, out),
                                 DELAY["port"] + DELAY["open"] + 3 * DELAY["close"])
        except TimeoutError:
            done = False
        except InstallError:
            raise
        except Exception:
            return None
        if not done:
            self.gave_up = f"the opening of {name or out}"
        return Link(link) if done else None

    def offline(self):
        """an FM-1 that is on the USB bus while CoreMIDI has its MIDI port offline (seen on the unit after restarts:
        it stays so until a MIDI rescan) -> the words for it, '' when there is none (or no way to know: the
        backend's known(), macOS)"""
        try:
            done, k = bounded(getattr(self.backend, "known", dict), DELAY["port"] + DELAY["close"])
        except Exception:
            done, k = False, None
        k = k if done and k else {}
        stuck = [n for n, off in k.get("midi", []) if off and n in k.get("usb", []) and
                 (PORT_RE.search(n) or (self.port and self.port.lower() in n.lower()))]
        if stuck and self.unit:                   # (a rescan restarts every MIDI port of the computer, the other
            #                                       FM-1s' too: with --unit the advice is the replug alone)
            return (f"{stuck[0]} is on the USB bus, but its MIDI port is known to CoreMIDI as offline: unplug this "
                    "FM-1's USB cable and plug it in again (with --unit nothing rescans MIDI: that would restart "
                    "every MIDI port on this computer)")
        return (f"{stuck[0]} is on the USB bus, but its MIDI port is known to CoreMIDI as offline: Audio MIDI Setup > "
                "MIDI Studio > Rescan MIDI (or fm1_install.py --rescan-midi), or replug the cable") if stuck else ""

    def rescan(self):
        """--rescan-midi: CoreMIDI rescans its devices (the backend's rescan(), macOS) -> None, or why not"""
        fn = getattr(self.backend, "rescan", None)
        try:
            done, why = bounded(fn, DELAY["port"] + DELAY["close"]) if fn else (True, "this MIDI backend has no rescan")
        except TimeoutError:
            done = False
        return why if done else "the MIDI rescan did not return in time"

    def waited(self, secs, what):
        """the words of a wait given up: how long, for what, the MIDI call that did not return, if one, and an
        FM-1 that CoreMIDI keeps offline"""
        off = self.offline()
        return (f"waited {secs:g} s for {what}" +
                (f"; {self.gave_up} did not return in time: replug the FM-1, or restart the computer"
                 if self.gave_up else "") + (f". {off}" if off else ""))

    def candidates(self, only_port, names=None, every=False):
        """the ports to try, in order (names: the inputs, when already listed). With --unit, once its port is known
        (self.only), only_port means that port alone (every: all the same)"""
        names = self.names() if names is None else names
        if self.only and only_port and not every:
            return [n for n in names if n == self.only]
        if self.port:
            mine = [n for n in names if self.port.lower() in n.lower()]
            if only_port:
                return mine
            return mine + [n for n in names if n not in mine and PORT_RE.search(n)]
        return [n for n in names if PORT_RE.search(n)]

    def find(self, want=None, only_port=True, end=None):
        """the first matching port that answers the handshake (and want(identity)); no port is tried after `end`.
        With --unit only the unit's (mine)"""
        self.ready()
        outs = self.names(outs=True)
        ins = self.names() if outs else []
        for name in self.candidates(only_port, ins) if outs else []:   # (no output, or no list in time: nothing to pair)
            out = pair_output(name, outs)
            if not out or (end is not None and time.monotonic() >= end):
                continue
            link = self.open(name, out)
            if not link:
                continue
            ident = handshake(link, 2)
            if ident and (want is None or want(ident)) and self.mine(link, ident, name, ins):
                return Device(link, ident, name)
            link.close()
        return None

    def wait_for(self, want, secs):
        """the FM-1 after a restart, for at most secs (and the try of one port that had begun: every step of it
        has its own bound) -> Device or None"""
        end = time.monotonic() + secs
        while time.monotonic() < end:
            dev = self.find(want, only_port=False, end=end)
            if dev:
                return dev
            time.sleep(min(DELAY["retry"], max(0, end - time.monotonic())))
        return None

    def serve(self, link, image, finish, idle, progress):
        """answer read requests until the device asks for `finish`; -> (served, finished, lost).
        progress(served, sent): the requests answered and the bytes sent so far (not the address: the loader reads
        the marker's sector out of order, firmware/loader/ldr_core.c)"""
        served, sent, last = 0, 0, time.monotonic()
        while True:
            pkt = link.read(DELAY["poll"])
            if pkt is None and link.lost:
                pkt = link.read(0)                # (what it asked before it left is still answered for: the link
                if pkt is None:                   # hands over all it has received before it says lost)
                    return served, False, True
            if pkt is None:
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
            sent += n
            progress(served, sent)

    def check(self, dev, image, step):
        """step 1: the running firmware checks the package and starts the loader"""
        step("start", dev.id.text)
        before = self.ports() if self.unit else None    # (--unit: the ports while the unit is still there)
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
        if self.unit:                             # only the one port its restart brings (wait_new)
            ota = self.wait_new(before, dev.name, DELAY["wait_loader"])
        else:
            ota = self.wait_for(lambda i: i.loader, DELAY["wait_loader"])
        if not ota and self.unit:
            what = self.waited(DELAY["wait_loader"], "the one new MIDI port of this FM-1's update mode")
            raise InstallError("noloader", f"the update loader did not appear ({what}). Unplug the other FM-1s, replug "
                                           "this one and run the install again without --unit (in update mode it "
                                           "has no ID): it stays in update mode until it is done")
        if not ota:
            what = self.waited(DELAY["wait_loader"], "a MIDI port that answers the identity query as ota-...")
            raise InstallError("noloader", f"the update loader did not appear ({what}). Replug the USB cable and "
                                           "run the install again: the FM-1 stays in update mode until it is done")
        return ota

    def write(self, ota, image, step):
        """step 2: the loader reads and writes the whole package, then restarts"""
        step("write", 0)
        if self.unit:
            self.fresh = self.ports()             # (the ports before the restart into the new firmware: mine)
        ota.link.send(UPGRADE)
        time.sleep(DELAY["start"])
        served, finished, lost = self.serve(ota.link, image, FINISH_WRITE, DELAY["idle_write"],
                                            lambda k, sent: step("write", min(99, sent * 100 // len(image))))
        ota.link.close()
        if not finished:
            raise InstallError("lost" if lost else "stopped",
                               f"the loader {'was disconnected' if lost else 'stopped answering'} after "
                               f"{served} requests. Replug the USB cable and run the install again to finish")
        step("write", 100)

    def back(self):
        """the firmware after a restart -> Device (wait_for's bound), or the error that says what was waited for"""
        back = self.wait_for(lambda i: not i.loader, DELAY["wait_reboot"])
        if not back and self.unit:
            what = self.waited(DELAY["wait_reboot"], f"a MIDI port that answers INFO with unit ID {self.unit}")
            raise InstallError("noreturn", f"the FM-1 with unit ID {self.unit} did not come back ({what}): unplug "
                                           "and replug it (or power-cycle it), then fm1_install.py --info. A firmware "
                                           "without the unit ID that came back under another FM-1's port name cannot "
                                           "be told apart")
        if not back:
            what = self.waited(DELAY["wait_reboot"], "a MIDI port that answers the identity query as the firmware")
            raise InstallError("noreturn", f"the FM-1 did not come back ({what}): power-cycle it")
        return back

    def verify(self, product, step):
        step("reboot")
        time.sleep(DELAY["reboot"])
        back = self.back()
        back.link.close()
        if self.unit:                             # (the unit's port from now on: a restore after the install)
            self.only, self.loader_port, self.fresh = back.name, None, None
        if back.id.text != product:
            raise InstallError("mismatch", f"written, but the FM-1 reports {back.id.text} (expected {product})")
        step("done", back.id.text)
        if self.unit:
            step("unit", f"unit ID {self.unit}: the same FM-1 after the restart (port {back.name})" if not self.unverified
                 else f"unit ID: none from {self.unverified}, the firmware now on it; it is the one MIDI port that "
                      f"appeared at the restart ({back.name}), taken as this FM-1")
        return back.id.text

    def install(self, dev, image, product, step=lambda *a: None):
        """running firmware -> loader -> new firmware; dev from find(). A device already in
        update mode only needs the write. The keyboard rule (same_keyboard) on both: the device, and the loader
        that answers after step 1"""
        if not same_keyboard(dev.id.text, product):
            dev.link.close()
            raise InstallError("model", f"the device is {dev.id.text}, the package is for {product}")
        ota = dev if dev.id.loader else self.check(dev, image, step)
        if not same_keyboard(ota.id.text, product):
            ota.link.close()
            raise InstallError("model", f"the device is in the update mode of {ota.id.text}, the package is for "
                                        f"{product}: nothing was written")
        self.write(ota, image, step)
        return self.verify(product, step)


# ------------------------------------------------------------ backup / restore ---
# The page's web/fm1backup.js in Python (web/EDITOR_PROTOCOL.md: Felucca's backup commands; ChoralRoot answers them
# with its own objects). One file format, "felucca-backup" version 1 (JSON); a restore writes the objects of the file
# that the connected firmware lists in BACKUP_LIST.

BK_HDR = bytes([0xF0, 0x7D, 0x46, 0x4C])
BK_INFO, BK_LIST, BK_GET, BK_PUT, BK_RESTART = 1, 65, 66, 67, 72
BK_DEBUG = 73                                     # read-only counters (cr_backup.c crb_debug), --debug
BK_SCREEN = 74                                    # the screen off (0) / on (1) (cr_backup.c, cr_ui.c), --screen
BK_SELECT = 76                                    # a part to an engine and pool position, live (cr_backup.c), --select
BK_STUB = 77                                      # a request to the boot stub and a restart (cr_backup.c crb_stub), --stub
BK_PEAK = 78                                      # read-only: the audio ISR's worst cases since the last read (crb_peak), --debug
BK_USB = 79                                       # read-only: ChoralRoot In's rate and its 48 kHz counters (crb_usb), --debug
STUB_RC = {1: "bad request", 3: "busy: close EDIT / SAVE on the FM-1, stop recording the loop",
           127: "no boot stub of version 2 or later on this firmware"}
SELECT_RC = {1: "bad part or engine", 2: "bad pool position", 3: "busy: close EDIT / SAVE on the FM-1"}
DEBUG_NAMES = ["ms", "halves", "max_us", "late", "nested", "timer_irqs", "ui_frames", "last_us",
               "cpu_avg_us", "cpu_max_us", "cpu_max_all_us", "cpu_halves", "c1_state", "c1_jobs",
               "c1_rejected", "c1_faults", "c1_fallback", "rail_sysvdd_boot", "rail_sysvdd_now", "rail_vdc14_boot",
               "rail_vdc14_now",
               "st_tick_us", "st_part0_us", "st_part1_us", "st_fx_us", "st_master_us", "st_wait_us",
               "stub_ms", "stub_us", "stub_ver", "stub_decision", "stub_detail",
               "ua_cap_alt", "ua_stage_on", "ua_cap_ready", "ua_tx_packets", "ua_underruns", "ua_overruns", "ua_fill",
               "ua_fill_q8", "ua_missed_frames", "ua_poll_max_us", "ua_service_max_us", "ua_svc_t5", "ua_svc_isr",
               "ua_isr_calls", "ua_isr_gated", "ua_fill_free", "ua_fill_busy", "ua_csr_or", "ua_csr_last", "ua_sofs",
               "ua_frame_last", "ua_armed_frame_dup", "usb_resets", "usb_suspended", "usb_timeouts", "usb_suspends",
               "usb_sof_seen", "ua_arm_gap_max_us", "ua_arm_gap_lt1.2ms", "ua_arm_gap_lt2.5ms", "ua_arm_gap_more",
               "ua_open_ms", "ua_opens", "ua_arm_calls", "ua_pk528", "ua_pk540", "ua_pk_other", "ua_pk_other_last", "ua_take_0_50ms", "ua_take_50_100ms", "ua_take_100_150ms", "ua_take_150_200ms",
               "ua_take_200_250ms", "ua_take_250_300ms", "ua_first_take_ms", "ua_first_take_max_ms", "ua_open_close_ms",
               "ua_holds", "ua_cap_hold", "ua_ep_maxp", "ua_nudges_up", "ua_nudges_down",
               "ua_frames_this_open", "ua_frames_last_open", "ua_isr_sie_busy", "ua_t5_sie_busy", "usb_setups", "usb_stalls", "ua_log_n"]
SETUP_NAMES = {(0x00, 0x05): "SET_ADDRESS", (0x80, 0x06): "GET_DESCRIPTOR", (0x00, 0x09): "SET_CONFIGURATION",
               (0x80, 0x08): "GET_CONFIGURATION", (0x01, 0x0B): "SET_INTERFACE", (0x81, 0x0A): "GET_INTERFACE",
               (0x02, 0x01): "CLEAR_FEATURE(ep)", (0x80, 0x00): "GET_STATUS(dev)", (0x81, 0x00): "GET_STATUS(if)",
               (0x82, 0x00): "GET_STATUS(ep)", (0x22, 0x01): "SET_CUR(ep rate)", (0xA2, 0x81): "GET_CUR(ep)",
               (0xA2, 0x82): "GET_MIN(ep)", (0xA2, 0x83): "GET_MAX(ep)", (0xA2, 0x84): "GET_RES(ep)",
               (0x21, 0x01): "SET_CUR(if)", (0xA1, 0x81): "GET_CUR(if)", (0xA1, 0x82): "GET_MIN(if)",
               (0xA1, 0x83): "GET_MAX(if)", (0xA1, 0x84): "GET_RES(if)"}


def debug_event(ms, what):
    """one entry of the DEBUG event log (cr_backup.c crb_debug, usb_audio.c ua_log)"""
    if what >> 24 == 0xFF and not ms >> 31:
        return f"{ms:9d} ms  overrun #{what & 0xFFFF}"
    bm, req, wv, wi = what & 0xFF, (what >> 8) & 0xFF, (what >> 16) & 0xFF, what >> 24
    name = SETUP_NAMES.get((bm, req), f"req {bm:02X} {req:02X}")
    return f"{ms & 0x7FFFFFFF:9d} ms  {'STALLED ' if ms >> 31 else ''}{name} wValue {wv} wIndex {wi}"
BK_CHUNK = 256
FELUCCA_IDS = [0, 1, 2, 3, 4, 5, 6, 7, 8, 32, 33, 34]
CR_IDS = [1, 6, 7, 8, 9, 10, 11, 12, 13] + list(range(14, 22)) + [22, 23] + list(range(40, 50))   # fm1backup.js CR_BACKUP_IDS
# (9 the VA patches, 10 11 the FM6 patches, 12 13 the CZ-1 tones, 14..21 the CZ-1 banks, 22 the FM TONE patches, 23 the
# favourites (0.22 on; a firmware before it does not list 23, and a restore skips what the FM-1 does not list), 40..49
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
    if i == 23:
        return "favorites"
    if is_sample(i):
        return f"sample slot {i - 31}"
    if 40 <= i <= 49:
        return f"loop slot {i - 39}"
    if 50 <= i <= 62:                              # 0.22: the sound pool's banks (10 slots each, the last 121..128)
        a = (i - 50) * 10 + 1
        return f"user sounds {a:03d}-{min(a + 9, 128):03d}"
    return f"object {i}"


def family(firmware):
    f = (firmware or "").lower()
    return "choralroot" if f.startswith("choralroot") else "felucca" if f.startswith("felucca") else "other"


class BackupLink:
    """request / reply over a link (one request at a time, the reply carries the same command)"""

    def __init__(self, link):
        self.link = link

    def request(self, cmd, args, timeout=1.5):
        """-> the reply's bytes. While it waits it asks, every DELAY gone, whether the FM-1 has left (one that
        restarts is back before a longer wait ends: "lost" is then never seen); what the FM-1 sent before it left
        is still taken (the link hands over all it has received before it says lost)."""
        self.link.drain()
        if not self.link.send(BK_HDR + bytes([cmd, *args, 0xF7])):
            raise InstallError("lost", "the FM-1 was disconnected")
        end, lost = time.monotonic() + timeout, False
        while True:
            left = end - time.monotonic()
            p = self.link.read(0 if lost else max(0, min(left, DELAY["gone"])))
            if p is not None:
                if len(p) >= 6 and p[:4] == BK_HDR and p[4] == cmd and p[-1] == 0xF7:
                    return list(p[5:-1])
            elif lost:
                raise InstallError("lost", "the FM-1 was disconnected")
            elif self.link.lost:
                lost = True                       # (once more through the queue: its last words)
            elif left <= 0:
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


UNIT_TAG = 0x49                                   # INFO's tag of the unit's ID (cr_backup.c CRB_INFO, 3y66)


def unit_id_of(a):
    """the unit's ID in ChoralRoot's INFO reply -> "A1B2C3D4", or None. The reply: the version, its NUL, Felucca's 7
    bytes (no engines), then tagged fields, tag len value: 42 01 03, 43 01 03, and from 3y66 49 08 and the ID's 8 hex
    digits (none known: no tag). Hosts before it read the version to its NUL and nothing after it"""
    if 0 not in a:
        return None
    p = a.index(0) + 8
    while p + 2 <= len(a) and p + 2 + a[p + 1] <= len(a):
        v = bytes(a[p + 2:p + 2 + a[p + 1]])
        if a[p] == UNIT_TAG and re.fullmatch(rb"[0-9A-F]{8}", v):
            return v.decode()
        p += 2 + a[p + 1]
    return None


def device_info(bl, with_id=False):
    """INFO -> (version, family), with_id: (version, family, the unit's ID or None); None when the firmware does not
    answer"""
    try:
        a = bl.request(BK_INFO, [], DELAY["info"])
    except BackupError:
        return None
    v = bytes(a[:a.index(0)] if 0 in a else a).decode("latin-1")
    if with_id:
        return v, family(v), unit_id_of(a) if family(v) == "choralroot" else None
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
        if fam == "choralroot":                   # (the restart is asked for, its answer waited for 2 s; not the
            try:                                  # FM-1's return: the next command finds it, or says it is not there)
                bk_check(bl.request(BK_RESTART, [], 2.0)[0])
            except InstallError as e:
                print("restart: no answer was seen, but the FM-1 has left the bus: it restarts" if e.code == "lost"
                      else f"restart: {e} (power-cycle the FM-1)", file=out)
    finally:
        bl.link.close()                           # (at once: the FM-1 leaves the bus 150 ms after its answer)
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
SOUND_SLOTS, SOUND_PER_BANK, SOUND_REC = 128, 16, 192   # 0.22: the sound pool's 128 (LEGACY_SLOTS: 0.21's 32)
LEGACY_SLOTS = 32
# 0.22, the sound pool (docs/PRESETS.md "The pool"): ids 50..62, 13 banks of 10 slots of 384 bytes: the record, at 192
# the patch's engine + 1 (0: none), at 193 its length, the patch from 196
POOL_IDS = tuple(range(50, 63))
POOL_PER, POOL_SLOT, POOL_HDR, POOL_DATA = 10, 384, 192, 196
POOL_BANK = POOL_PER * POOL_SLOT                 # 3840
ENGINE_NAMES = ("ANALOG", "DIGITAL", "PHASE", "LOFI", "SAMPLE", "VOICE", "TRIO", "WHEEL", "GRAIN", "PHYS", "DRUM",
                "NOISE", "FM6", "VA", "CZ-1", "FM TONE", "P5")   # ENGINES[] (append-only; 0, 1, 4, 8, 10 retired: ANALOG
                                                          # since 0.14; 2, 3, 5, 6, 11, 9 retired in 0.18: PHASE, LOFI,
                                                          # VOICE, TRIO, NOISE, PHYS). 15: FM TONE (eng_quad.c, code name QUAD)
ENGINE_PATCH = {12: "fm6", 13: "va", 14: "cz", 15: "quad", 16: "p5", 8: "grain"}  # engine -> its patch kind (P5,
                                                                                  # GRAIN: the pool only)
PATCH_KINDS = ("va", "fm6", "cz", "quad", "p5", "grain")
PATCH_SIZE = {"va": 110, "fm6": 128, "cz": 144, "quad": 96, "p5": 138, "grain": 64}
# a kind's blob in the legacy stores (before the pool): FM TONE's 80 (versions 1..3; versions 4 and 5, 2026-10-14: 96
# bytes, the hi-res values in hundredths; the FM-1 converts a version 4 on load). A version 1..3 blob is zero-padded to 96 (the firmware converts it on load)
STORE_PATCH = dict(PATCH_SIZE, quad=80)
QUAD_V3 = 80
GRAIN_V1 = 20                                       # GRAIN's first build's record (version 1; the FM-1 converts it on load)


def _quad96(b):
    """an FM TONE blob of 80 bytes (versions 1..3) zero-padded to 96; 96 as it is"""
    return bytes(b) + bytes(PATCH_SIZE["quad"] - len(b)) if len(b) == QUAD_V3 else bytes(b)
KIND_ENGINE = {k: e for e, k in ENGINE_PATCH.items()}


def is_pool(objs):
    """the objects are the sound pool's (0.22: any of ids 50..62), else 0.21's"""
    return any(i in objs for i in POOL_IDS)


def _pool_at(slot):
    return POOL_IDS[0] + (slot - 1) // POOL_PER, ((slot - 1) % POOL_PER) * POOL_SLOT
PATCH_ID = {"va": 9, "fm6": 10, "cz": 12, "quad": 22}   # the store (the first half for FM6 / CZ-1)
UPB_MAGIC, VAS_MAGIC, FM6U_MAGIC, CZU_MAGIC, QUDS_MAGIC = 0x31425055, 0x31534156, 0x55364D46, 0x55315A43, 0x53445551
BANK_SIZE = 8 + SOUND_PER_BANK * SOUND_REC     # 3080
STORE_SIZE = {"va": 16 + LEGACY_SLOTS * 110, "fm6": 16 + 16 * 128, "cz": 16 + 16 * 144,
              "quad": 16 + LEGACY_SLOTS * 80}   # 3536, 2064, 2320, 2576
WHOLE_STORES = ("va", "quad")                  # one store of 32 (the others: two halves of 16), its used mask at 8
SOUND_FORMAT = "choralroot-sound"
# the factory presets of each engine (ENGINES[e]->presets[k].name) and the first of them in the engine's pool (the
# CZ-1's preset 0, INIT TONE, and P5's, INIT P5, are the pools' INIT; P5's presets are the shipped list,
# assets/prophet5-factory/shipped.txt); FACTORY_IDS: a preset's favourites id where it is not its index (P5: its
# Sequential program number, the device manager's marks): tests/sound_templates.c prints the three from the firmware
# built as the unit is, and its --check fails until the three lines below hold its strings verbatim (docs/SOUNDS.md
# "The binding")
FACTORY_PRESETS = {"7":["FULL ORGAN","JAZZ PERC","GOSPEL","SOFT FLUTE","ROCK DRIVE"],"8":["CLOUD PAD","SHIMMER","BREATH PAD","FROZEN","STRUM","SWARM","ACID DUST","GLITCH"],"12":["TINE EP","FM BELL","FM BASS","BRASS","FM PAD","MARIMBA","FM ORGAN","FM PLUCK","DX TINE","BRASS SECT","SOLID BASS","BELLS","DX MARIMBA","CLAVINET","DRAWBARS","STRINGS","GLASS PAD","SYNC LEAD","HARP","KALIMBA","FLUTE","STEEL DRUM","SAW BASS","TUBULAR","PIANO"],"13":["LUSH PAD","WARM PAD","GLASS PAD","SLOW STRINGS","ENSEMBLE STR","SYNTH BRASS","SOFT BRASS","POLY KEYS","PWM KEYS","CLAV","SOFT LEAD","HOLLOW","BELLS","SWEEP PAD","SOFT AAH","ORGANISH","DEEP SUB","PUNCH BASS","RUBBER BASS","SYNC BASS","MORPH PAD","VINYL KEYS","WIDE STRINGS","CLOUD PAD","SHIMMER"],"14":["INIT TONE","BRASS 1","BRASS 2","BRASS 3","STRINGS 1","STRINGS 2","STRINGS 3","STRINGS 4","ORCHESTRA","ACO.GUITAR","JAZZ GUITAR","ELEC.GUITAR","SLAP BASS","SYNTH.BASS","ELEC.BASS 1","ELEC.BASS 2","HARP","BRASS 4","SAXOPHONE","CELLO","FLUTE","WHISTLE","HARMONICA","RECORDER","KOTO","PIANO 1","PIANO 2","PIANO 3","ELEC.PIANO","HONKY-TONK","FUNKY CLAV 1","FUNKY CLAV 2","HARPSICHORD","JAZZ ORGAN 1","JAZZ ORGAN 2","PIPE ORGAN 1","PIPE ORGAN 2","ACCORDION","VOICE 1","VOICE 2","VOICE 3","MUSIC BOX","VIBRAPHONE","XYLOPHONE","MARIMBA","MALLET LOG","AFRO PERC","BELLS","METALLIC","SYN STRINGS","FAT ENSEMBLE","SITAR","SYNTH.LEAD 1","SYNTH.LEAD 2","SYNTH.LEAD 3","SYNTH.LEAD 4","SWEEP 1","SYN DRUMS 1","SYN DRUMS 2","CONGA","STEEL DRUM","SWEEP 2","JET ROAR","MOTORCYCLE","TYPHOON"],"15":["EP","BELL","BASS","PLUCK","BRASS","GLASS PAD","HOLLOW","SQUARE LEAD","METAL","WOBBLE","CLAV","STRINGS","MARIMBA","DRONE","FEEDBACK","NOISE-ISH"],"16":["INIT P5","ITS PROPHET5","AFTER RINGER","FOREVER KEYS","STABBY BRASS","NO WHINING","FANTASTIC 5","PDG ORGAN","CLAVICLE","BLOW BY BLOW","TAARICHOIR","REEDY STRPAD","CANNED HAPPY","SYNCHROTRILL","BORN IN 80S","STICKY PLUCK","WATERY PLUCK","BASS FACE","WHINY OPENER","SYNCTINK","TREMBL ORGAN","SCI-FI ECHO","UNICORN DRMS","COCKTAIL HR","NICE SMOOTH","TUMESC BASS","INTERNALIZED","BLINKR BELLS","UWATER TRILL","POLY MCFAT","REED EP","PIPE ORGANIS","FAT POLYBASS","CHORAL VOICE","HUM-ALONG","AFTRBLW HORN","MIDNIGHT PWM","RIPPLES","RADIO TOWERS","HARPSI-VIBE","INSTABIL PAD","VINTAGE WRLY","PRESSURE PTS","80S HORROR","NURSERYCOMP","HOUSE MOVER","VOCAL PAD","SQUARECLIFF","UNDER ICE","MARIMBA","UNISAWYER","RECORDER","GHOSTLY GLOW","UPTIGHT BASS","RAVECAVE","CELTIC MOON","MOUSE BLASTR","KYOTO","ORGAN PERC","DRM ORGONON","PLUCK HOLLOW","VELO RES LD2","REGAL MOVMNT","LOOSE SQUARE","RUBBR BUMPER","GALV BUCKET","SOFT SWELL","REVERB STRGS","LAGOON BASS","PLUCKTY DUCK","BELL TREE","RISER CHORD","TIMPANALLY","TUNED PERC","FUNKY BELLS","FEDBACK","NYLON FINGRS","FAINT MEMORY","FULL HEXAGON","ICONIC BRASS","SYNC SWELLPD","PRETTY PLUCK","FEATURE BASS","CHORUSLAYER","BETTER WAYS","BASSMENT","DENOUEMENT","SYNCWAVE","ORGANIZER","BULL BY HORN","NAIVE MELODY","POLY MORGUE","SWITCHBACK","PLUCKYNESS","CYBERIAN WND","METAL DRONE","LOW STRINGS","PERC E PIANO","FLUTES","PERC ORGAN","UNISON GLIDE","ORGAN W RES","TOY PIANO","TRUMPT FLUTE","ELEC PIANO I","OCTAVE SAWS","DELAYED HARM","ECHO REPEAT","PULSE W MOD","4THS W RES","SWEEP HARMS","SLOW SYNC","NOISE SWEEP","RES BELLS","HOLLOW SOUND","CAT"]}
FACTORY_FIRST = {"7":0,"8":0,"12":0,"13":0,"14":1,"15":0,"16":1}
FACTORY_IDS = {"16":[0,1,2,3,6,7,8,10,11,12,13,15,16,17,18,19,20,21,23,24,25,26,27,28,29,30,31,32,34,41,43,45,46,47,51,53,54,56,59,60,61,62,65,66,67,68,70,72,77,79,82,83,85,86,87,89,92,93,95,97,100,101,103,104,107,108,109,110,111,112,113,114,115,117,120,122,123,127,128,131,132,133,135,136,137,139,140,142,143,146,147,148,150,152,156,159,162,164,165,168,169,171,172,173,179,181,183,184,185,187,188,189,194,198,199,200]}
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
        return PATCH_ID[kind], k, 16 + k * STORE_PATCH[kind]
    return PATCH_ID[kind] + k // 16, k % 16, 16 + (k % 16) * STORE_PATCH[kind]


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
            + STORE_PATCH[kind].to_bytes(2, "little") + bytes(2)
    else:
        head = (FM6U_MAGIC if kind == "fm6" else CZU_MAGIC).to_bytes(4, "little") + (1).to_bytes(2, "little") \
            + (16).to_bytes(2, "little") + (16 * (i - PATCH_ID[kind])).to_bytes(2, "little") \
            + PATCH_SIZE[kind].to_bytes(2, "little") + bytes(4)
    return head + bytes(STORE_SIZE[kind] - 16)


def _store_blob(objs, kind, slot):
    """the blob of `kind` stored for slot (its used bit set in a well-formed store), or None"""
    if is_pool(objs):                              # (the pool: the slot's patch when it is that kind's)
        i, off = _pool_at(slot)
        b = objs.get(i, b"")
        n = b[off + POOL_HDR + 1] if len(b) == POOL_BANK else 0
        if len(b) != POOL_BANK or b[off + POOL_HDR] != KIND_ENGINE[kind] + 1 or \
                (n != PATCH_SIZE[kind] and not (kind == "quad" and n == QUAD_V3)
                 and not (kind == "grain" and n == GRAIN_V1)):   # (an older FM TONE slot: 80; a GRAIN slot of the first build: 20)
            return None
        d = bytes(b[off + POOL_DATA:off + POOL_DATA + n])
        return _quad96(d) if kind == "quad" else d
    if kind in ("p5", "grain"):                    # (the pool only: no store before it)
        return None
    i, k, off = _store_at(kind, slot)
    b = objs.get(i, b"")
    if not _store_ok(kind, i, b) or not _u32(b, 8 if kind in WHOLE_STORES else 12) >> k & 1:
        return None
    d = bytes(b[off:off + STORE_PATCH[kind]])
    return _quad96(d) if kind == "quad" else d


def _set_blob(objs, kind, slot, blob):
    """objs[store] with slot's blob set (bit set) or, blob None, cleared (bit cleared, bytes zeroed). An empty or
    malformed store (the FM-1 reads it as empty) is created from the header when a blob goes in, else left alone."""
    if is_pool(objs):
        i, off = _pool_at(slot)
        b = objs.get(i, b"")
        if blob is None and (len(b) != POOL_BANK or b[off + POOL_HDR] != KIND_ENGINE[kind] + 1):
            return
        b = bytearray(b if len(b) == POOL_BANK else bytes(POOL_BANK))
        b[off + POOL_HDR:off + POOL_SLOT] = bytes(POOL_SLOT - POOL_HDR)
        if blob is not None:
            b[off + POOL_HDR], b[off + POOL_HDR + 1] = KIND_ENGINE[kind] + 1, PATCH_SIZE[kind]
            b[off + POOL_DATA:off + POOL_DATA + len(blob)] = blob
        objs[i] = bytes(b)
        return
    if kind == "p5":
        if blob is not None:
            raise InstallError("usage", "this FM-1 keeps no P5 programs (ChoralRoot 0.22 or later needed)")
        return
    if kind == "grain":
        if blob is not None:
            raise InstallError("usage", "this FM-1 keeps no GRAIN sounds (a ChoralRoot with the GRAIN engine needed)")
        return
    if kind == "quad" and blob is not None:        # (a firmware before the pool keeps versions 1..3, 80 bytes)
        if blob[1] > 3:
            raise InstallError("usage", "this FM-1 keeps FM TONE sounds in an older format: update ChoralRoot (0.22 or "
                               "later) to add this one")
        blob = bytes(blob[:QUAD_V3])
    if kind == "va" and blob is not None and blob[1] > 3:   # (its va_blob_ok takes 1..3: a version 4 / 5 would play INIT)
        raise InstallError("usage", "this FM-1 keeps VA sounds in an older format: update ChoralRoot (0.22 or later) "
                           "to add this one, or place it with the web device manager, which converts it")
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
    b[off:off + STORE_PATCH[kind]] = blob if blob is not None else bytes(STORE_PATCH[kind])
    objs[i] = bytes(b)


def _set_record(objs, slot, rec):
    """objs[bank] with slot's record replaced; an empty or malformed bank is created when a sound goes in"""
    if is_pool(objs):
        i, off = _pool_at(slot)
        b = objs.get(i, b"")
        if len(b) != POOL_BANK:
            if not any(rec):
                return
            b = bytes(POOL_BANK)
        tail = bytes(b[off + SOUND_REC:off + POOL_SLOT]) if any(rec) else bytes(POOL_SLOT - SOUND_REC)
        objs[i] = bytes(b[:off]) + bytes(rec) + tail + bytes(b[off + POOL_SLOT:])
        return
    i, off = 6 + (slot - 1) // 16, 8 + ((slot - 1) % 16) * SOUND_REC
    b = objs.get(i, b"")
    if not _bank_ok(b):
        if not any(rec):
            return
        b = _new_bank()
    objs[i] = bytes(b[:off]) + bytes(rec) + bytes(b[off + SOUND_REC:])


def _record(objs, slot):
    if is_pool(objs):
        i, off = _pool_at(slot)
        b = objs.get(i, b"")
        return bytes(b[off:off + SOUND_REC]) if len(b) == POOL_BANK else bytes(SOUND_REC)
    b = objs.get(6 + (slot - 1) // 16, b"")
    if not _bank_ok(b):
        return bytes(SOUND_REC)
    off = 8 + ((slot - 1) % 16) * SOUND_REC
    return bytes(b[off:off + SOUND_REC])


def pool_of(engine):
    """the preset pool a record is in (cr_bank.c cb_rec_engine): its engine if selectable, DIGITAL's FM6, another
    retired one (ANALOG since 0.14; PHASE, LOFI, VOICE, TRIO, NOISE, PHYS since 0.18; SAMPLE, GRAIN, DRUM) the VA's"""
    return engine if str(engine) in FACTORY_PRESETS else 12 if engine == 1 else 13


def engine_retired(engine):
    """a record's engine has no pool of its own on this firmware: the slot keeps the record, the FM-1 plays a stand-in"""
    return engine is not None and str(engine) not in FACTORY_PRESETS


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


def _slot_row(objs, slot):
    """the slot's row of parse_sound_objects; a slot past the FM-1's (0.21: 32) is a usage error"""
    table = parse_sound_objects(objs)
    if not 1 <= slot <= len(table):
        raise InstallError("usage", f"slot {slot}: this FM-1 has the user slots 1..{len(table)}")
    return table[slot - 1]


def parse_sound_objects(objs):
    """objs: {backup id: bytes} (b"" or missing: never written) -> the 32 slots, a dict each:
    slot, used, name, engine, engineName, patch (the kind when the slot's blob is stored, else None), record, blob,
    bound (None, or the factory preset it overwrites: sound_bindings), added (used and not bound)"""
    table = []
    for slot in range(1, (SOUND_SLOTS if is_pool(objs) else LEGACY_SLOTS) + 1):
        r = _record(objs, slot)
        row = {"slot": slot, "used": False, "name": "", "engine": None, "engineName": "", "patch": None,
               "record": r, "blob": None, "bound": None, "added": False, "pool": is_pool(objs)}
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
    """U05  MY PAD        FM6     patch     over FM6 02 FM BELL / U07  PAD 2  VA  patch  + / U06  (empty) /
    U17  BELL          PHASE   retired   + (a record of a retired engine: kept, an added preset of the VA's pool)"""
    lab = slot_label(row["slot"], row.get("pool", row["slot"] > 99))
    if not row["used"]:
        return f"{lab}  (empty)"
    kind = ENGINE_PATCH.get(row["engine"])
    tail = "retired" if engine_retired(row["engine"]) else "" if not kind else "patch" if row["patch"] else "no patch"
    pool = f"over {row['bound']['label']} {row['bound']['name']}" if row.get("bound") else "+"
    return f"{lab}  {row['name']:<12}  {row['engineName']:<7} {tail:<8}  {pool}"


def slot_label(slot, pool=True):
    """U005 (the pool's 128, 0.22), U05 (0.21's 32)"""
    return f"U{slot:03d}" if pool else f"U{slot:02d}"


def sound_file_name(slot, name):
    return f"choralroot-sound-U{slot:02d}-{re.sub(r'[^A-Za-z0-9-]', '_', name)}.json"


def export_sound(objs, slot, firmware):
    """slot 1..32 -> the sound file (a dict, "choralroot-sound" version 1)"""
    import base64
    row = _slot_row(objs, slot)
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
        if kind == "quad" and len(data) == QUAD_V3 and data[:1] == b"Q" and 1 <= data[1] <= 3:
            data = _quad96(data)                   # (an FM TONE sound file of before 2026-10-14: the FM-1 converts it)
        if kind == "grain" and len(data) == GRAIN_V1 and data[:1] == b"G" and data[1] == 1:
            data = bytes(data) + bytes(PATCH_SIZE["grain"] - GRAIN_V1)   # (a GRAIN sound file of the first build: zero-padded, the FM-1 converts it)
        if len(data) != PATCH_SIZE[kind]:
            raise bad(f"the {kind} patch is {len(data)} bytes, not {PATCH_SIZE[kind]}")
        if kind == "va" and (data[0] != 0x56 or not 1 <= data[1] <= 5):
            raise bad("not a VA patch (its first bytes are not 'V' and a version 1..5)")
        if kind == "fm6" and (data[112] != 0x46 or data[113] != 1):
            raise bad("not an FM6 patch (bytes 112, 113 are not 'F', 1)")
        if kind == "quad" and (data[0] != 0x51 or not 1 <= data[1] <= 5 or (data[1] < 4 and any(data[80:]))):
            raise bad("not an FM TONE patch (its first bytes are not 'Q' and a version 1..5)")
        patch = (kind, bytes(data))
    return bytes(rec), name, engine, patch


def _changes(old, new):
    """the objects whose bytes changed, the stores first, the banks last (the pool: its banks in order)"""
    order = POOL_IDS if is_pool(new) else (9, 10, 11, 12, 13, 22, 6, 7)
    return [(i, new[i]) for i in order if i in new and new[i] != old.get(i, b"")]


def import_sound(objs, slot, sound):
    """sound: read_sound_file's result -> [(id, bytes)] to write. The patch goes into its store at slot (bit set);
    the other kinds' blobs of that slot are cleared (as the FM-1 does when a sound is saved)."""
    rec, _name, _engine, patch = sound
    new = dict(objs)
    if is_pool(new):                               # (the pool: from an empty slot, no byte of the last sound kept)
        _set_record(new, slot, bytes(SOUND_REC))
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
    """the pool: the whole slot sent empty (384 zeros, as the FM-1 writes one), the bank's other slots as read"""
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
                   "cz": "pQQOW1NZWCBJTVBPUlQAAKhKhpp8QEBAQHxAQEBAQEBAQEJBgEC/QEBAQEBAUEJAgEBAQEBAQEBAQEBAaEBBQb9AQEBAQEBAQEBAQEBAQL9Av0BAv0C/QEC/QL9AQL9Av0BAQEBAQEBAQEIAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA",
                   "quad": "pQQPW1NZWCBJTVBPUlQAAKhKhpp8QEBAQHxAQEBAQEBAQEJBgEC/QEBAQEBAUEJAgEBAQEBAQEBAQEBAaEBBQb9AQEBAQEBAQEBAQEBAQL9Av0BAv0C/QEC/QL9AQL9Av0BAQUNDQ0BAQAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"}
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
    row = _slot_row(objs, slot)
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
        listed = bk_manifest(bl.request(BK_LIST, [], 3.0))
        pool = any(o["id"] in POOL_IDS for o in listed)  # (0.22: the sound pool; 0.21's objects are not listed then)
        man = [o for o in listed if o["id"] in (POOL_IDS if pool else SOUND_IDS)]
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
            for i in (POOL_IDS if pool else ()):
                objs.setdefault(i, b"")             # (a bank never written: listed with size 0)
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
        if not is_pool(objs) and (6 not in objs or 7 not in objs):
            raise InstallError("nosounds", f"{version} does not list the user sound banks")
        table = parse_sound_objects(objs)
        for op in (a.export_sound, a.import_sound, a.rename_sound, a.delete_sound, getattr(a, "export_syx", None),
                   getattr(a, "import_syx", None)):
            n = op and str(op[0] if isinstance(op, (list, tuple)) else op)
            if n and n.isdigit() and int(n) > len(table):
                raise InstallError("usage", f"slot {n}: {version} has the user slots 1..{len(table)}")
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
    return product, deinterleave(raw), official


def not_found(up):
    names = up.names()
    where = f"no MIDI port matching {up.port!r} answered" if up.port else "FM-1 not found"
    if up.unit:
        where = f"the FM-1 with unit ID {up.unit} was not found"
    stuck = (f"\n  {up.gave_up} did not return within its bound ({DELAY['port']:g} s): replug the FM-1, or restart "
             "the computer") if up.gave_up else ""
    off = up.offline()
    return InstallError("notfound", f"{where} (USB data cable? another app using it?)"
                        f"\n  MIDI inputs: {', '.join(names) if names else '(none)'}{stuck}" + (f"\n  {off}" if off else ""))


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
                   "reboot": "restarting", "done": f"done: the FM-1 runs {a}", "unit": a}[k]
            print(msg, file=self.out, flush=True)

    def put(self, s):
        print(f"\r{s}\033[K", end="", file=self.out, flush=True)
        self.line = True

    def end(self):
        if self.line:
            print(file=self.out, flush=True)
            self.line = False


DEBUG_LOG = 32                                    # after DEBUG_NAMES: the USB recording's event log, 16 pairs
DEBUG_TAIL = ["stub_oct_dn", "stub_oct_up", "stub_idle", "stub_pa_in"]   # after the log (appended: an older tool
#                                                   reads what it knows and shows these as two more log lines)
# 78 PEAK's values (0.18, cr_backup.c crb_peak, docs/FX.md "What the unit says"; a command of its own: 73 DEBUG's
# count is one 7-bit byte and it is full at 127 values): the audio ISR's WORST CASES since the
# last --debug (reading clears them: play, read, and the numbers are that stretch's). us of a half buffer (budget
# 2902): the whole half and each stage; the late halves, the latest one's largest stage and its time; the halves
# counted; inside the fx stage (over the halves in which the effects ran) the average and the largest of the sends'
# down-sampler, the delay, the reverb and the wet's up-sampler; then since power-on: flash erases, erases that cut a
# sounding output (holes: the rule is 0), the settings' and the session's saves
DEBUG_PEAK = ["pk_block_us", "pk_tick_us", "pk_part0_us", "pk_part1_us", "pk_fx_us", "pk_master_us", "pk_wait_us",
              "pk_late", "pk_late_stage", "pk_late_us", "pk_halves", "fx_busy_halves",
              "fx_down_avg_us", "fx_down_max_us", "fx_delay_avg_us", "fx_delay_max_us", "fx_reverb_avg_us",
              "fx_reverb_max_us", "fx_up_avg_us", "fx_up_max_us",
              "flash_erases", "flash_holes", "saves_settings", "saves_session",
              # 0.18's third build: when the ISR got to start (firmware/src/cr_cpu1.h cr_prof_entry, cr_prof_t5)
              "pk_lag_us", "pk_wall_us", "pk_t5_us", "stale_halves", "irq_gap_us", "irq_gap_pc",
              # the keys' way to the engine (firmware/src/cr_out.c cr_klat, docs/INTEGRATION.md "Keys while a frame
              # draws"): the note-ons timed; us from the first scan that saw the key closed (its debounce, 1.1 .. 2.2
              # ms, in it) to the main loop posting the note-on (the longest), and to the audio ISR applying it (the
              # average, the longest); the sound leaves the DAC one half (2.9 ms) after that
              "key_ons", "key_pick_us", "key_eng_avg_us", "key_eng_us",
              # 3y44, what the overload shed did since the last --debug (firmware/src/audio.c shed_check / shed_voice,
              # docs/PROPHET.md "The crackle"; voices_shed was on the console alone): voices shed; the latest one
              # (0 none, else 0x10000 | part << 12 | voice << 8 | note) and why (SHED_WHY); the halves above 85 % of
              # the budget (one alone sheds nothing, two in a row do); the time of the half that asked for the latest
              # shed, its lag in it; then voice.c's: voices faded out for another note, another part or the shed, and
              # own sounding voices taken for a new note (a five-key chord on P5's four voices: one each)
              "shed_voices", "shed_last_voice", "shed_last_why", "shed_over_halves", "shed_last_us",
              "voices_faded", "voices_stolen",
              # 3y46 (0.22), P5's voices on two cores since the last --debug (firmware/src/fx.c p5x_run, voice.c
              # p5_pool_*; docs/PROPHET.md "Voices"): the most P5 voices core 0 and core 1 rendered in one block
              # (four each is the rule; core 1's: the bass part's P5 voices and its share of the chord part's), the
              # bass part's most P5 voices, and held bass voices given up to the chord part. Core 1's longest job of
              # a half is pk_part1_us (the bass part and core 1's share of the chord's voices); pk_wait_us is how
              # long core 0 waited for it at most
              "p5_core0_voices", "p5_core1_voices", "p5_bass_voices", "p5_bass_given_up",
              # 3y48 (the owner's stutter with an arp: "So not sure why it wrote to flash"), since power-on, never
              # cleared: automatic erases started while something sounded in the second before (the rule: 0), the
              # latest erase's writer (ERASE_WHY), object (OBJ_*; 0x100 + k a loop slot) and time (ms since power-on,
              # 0 none); the time irq_gap_us happened (a peak, cleared with it), of ua_arm_gap_max_us, of the latest
              # USB capture under- or overrun (the stream restarted); the unit's clock now. (p5-g e31c096 puts other
              # values at 35..38 on its own list: merge by name, not by index)
              "erases_auto_sounding", "last_erase_why", "last_erase_obj", "last_erase_ms", "irq_gap_ms",
              "ua_arm_gap_max_ms", "ua_cap_reset_ms", "uptime_ms",
              # 3y50, each core's half since the last --debug (firmware/src/cr_cpu1.h cr_dbg, fx.c mix_fx_split;
              # docs/FX.md "Each core's half"): the largest busy time of core 0 (its stages but its waits) and of core
              # 1 (its voices' job and the reverb's), core 0's waits in one half (both joins), the effects' join
              # alone, core 1 idle between its two jobs; which core ran which effect in the largest whole half (1 the
              # chorus, 2 the delay, 4 the reverb ran, 8 the reverb on core 1); the reverb jobs core 1 ran
              "pk_c0_busy_us", "pk_c1_busy_us", "pk_c0_wait_us", "pk_fx_join_us", "pk_c1_idle_us", "pk_fx_map",
              "c1_fx_jobs",
              # 3y52: the quiet writes (p5-g e31c096's values 35..38 there, appended here by name; 0.22,
              # firmware/src/cr_settings.c, storage.c "erase ahead"; docs/SETTINGS.md "When flash is written"), all
              # since power-on: the saves the unit did by itself (an Option's, a favourite's), the erases done ahead
              # (the session's and the favourites', not the settings': at power-on before the audio starts, after
              # 10 s without a sound or an input), the erases the unit started by itself while a voice was active or within 2 s of an input (the rule:
              # 0), the longest page program in us (the wait for the chip, the interrupts masked)
              "saves_auto", "erases_ahead", "erases_near", "flash_prog_us",
              # 3y60 (a full sound pool made the UI laggy; docs/PRESETS.md "What browsing reads"), since the last
              # --debug: a UI frame's own time (input, LEDs, draw) the longest and the average in us; the flash reads
              # per second (storage_hw.c st_read), calls and bytes
              "ui_frame_max_us", "ui_frame_avg_us", "flash_reads_ps", "flash_read_bytes_ps",
              # (the values below, 69..79, were 35..45 on the dual-core fallback's branch, worktree-dualcore-1:
              # appended here by name, not by index)
              # why this boot kept core 1 off although Options > Dual Core said On (firmware/src/cr_cpu1.h C1_BAR_*,
              # docs/DUALCORE.md): 0 none, 1 a crash beside it, 2 a hang, 3 it gave up (73 DEBUG is full)
              "c1_bar",
              # 0.22: the last crash's record (hal/fm1_irq.h fm1_crash, what the crash screen shows for 4 s; kept in RAM
              # until a power-off; crash_count 0: none), this boot's reset reason (P3_RST_SRC bits 0..7: 0 power-on, 2
              # watchdog, 6 soft; the core reset as bit 8) and the breadcrumb the last reset left (BOOT_STAGE)
              "crash_count", "crash_vec", "crash_pc", "crash_rets", "crash_emu", "crash_dbg", "crash_uptime_ms",
              "boot_reset", "boot_stage",
              # the voices the overload shed dropped since power-on (audio.c shed_voice: two halves in a row over
              # 85 % of the 2902 us; the console's voices_shed; shed_voices above is since the last --debug): the
              # rule is 0
              "voices_shed"]
ERASE_WHY = ["other (a sound saved, a store)", "automatic: the Options", "automatic: the favourites",
             "the global save (SAVE held) or Reset Settings", "the calibration / the update gesture",
             "a backup restore", "a loop slot"]
C1_BAR = ["none", "after a crash beside core 1", "after a hang (watchdog) while core 1 ran", "core 1 gave up"]
PEAK_HEX = ("irq_gap_pc", "crash_vec", "crash_pc", "crash_rets", "crash_emu", "crash_dbg", "boot_reset")
BOOT_STAGE = {0: "none", 1: "loop: input", 2: "loop: draw", 9: "loop: idle", 10: "guard", 11: "flash probe",
              12: "user sounds", 13: "fm6 bank", 14: "va store", 15: "fm6 store", 16: "settings", 17: "update cleanup",
              18: "lcd", 19: "splash", 20: "input", 21: "sounds", 22: "audio", 23: "usb", 24: "uart", 25: "irqs",
              26: "calibration", 27: "ui init", 28: "ui settings", 29: "loops", 30: "core 1"}   # cr_bootguard.h BS_*
LATE_STAGE = ["none", "tick", "part0", "part1", "fx", "master", "wait"]
SHED_WHY = ["none", "a released voice, the quietest", "a held voice, the oldest that is not its part's lowest note",
            "asked, and no voice to give", "a voice of the bass part's P5 stack, before any chord voice"]
NOTE_NAMES = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"]


def shed_voice_text(v):
    """78 PEAK's shed_last_voice in words"""
    if not v >> 16 & 1:
        return "none"
    note = v & 127
    return (f"part {v >> 12 & 15} ({['chord', 'bass'][v >> 12 & 15] if (v >> 12 & 15) < 2 else '?'}) voice {v >> 8 & 15}, "
            f"note {note} ({NOTE_NAMES[note % 12]}{note // 12 - 1})")
# 79 USB's values (0.19, cr_backup.c crb_usb, docs/USB-AUDIO.md "48 kHz"; a command of its own because 73 DEBUG is
# full; reading clears nothing; values are only appended): ChoralRoot In's rate now and in the last whole open (Hz; 0:
# no open yet), the host's SET_CUR sampling-frequency requests and those that changed the rate, the 48 kHz packets by
# size (48 frames; 47 and 49: the drift's), the resampler's blocks, its recent average and its longest time for one
# block of 32 frames in us (four a half; the longest has TIMER5 nested in it), the largest packet in bytes; then
# Options > USB Rate (2026-10-12): 1 when ChoralRoot In offers 48 kHz (0, the default: 44.1 kHz only, 0.18's device),
# the host's requests for 48 kHz while it was not offered, the times the FM-1 left the bus for a USB option
DEBUG_USB = ["ua_rate", "ua_rate_last_open", "ua_rate_sets", "ua_rate_changes", "ua_pk576", "ua_pk564", "ua_pk588",
             "ua_rs_blocks", "ua_rs_avg_us", "ua_rs_max_us", "ua_packet_max", "ua_48k_offered", "ua_48k_refused",
             "ua_replugs"]
USB_RATE_OPTION = "Options > USB Rate"             # the entry's name on the unit (cr_ui.c O_NAME)


def stub_lines(d, out):
    """the boot stub's mailbox in words (firmware/stub/stub_core.h, docs/BOOT-SAFETY.md)"""
    if not d.get("stub_ver"):
        if "stub_ver" in d:
            print("boot stub: none ran before this boot (a firmware without it, or the emulator)", file=out)
        return
    det = d["stub_detail"]
    mk = ["ok", "bad magic", "bad format", "bad marker CRC", "bad length", "bad body CRC", "not checked"]
    ld = ["ok", "bad head", "bad inner head", "not Felucca's", "bad blocks", "not checked"]

    def name(t, i):
        return t[i] if i < len(t) else f"code {i}"
    req = " (the app asked for the rescue)" if det >> 25 & 1 else " (the app asked for the test)" if det >> 26 & 1 else ""
    print(f"boot stub v{d['stub_ver']}: {name(['?', 'booted the app', 'loader', 'UBOOT'], d['stub_decision'])} "
          f"after {d['stub_us'] / 1000:.1f} ms; marker {name(mk, det & 0xFF)}, staged loader "
          f"{name(ld, det >> 8 & 0xFF)}, rescue attempts before this boot {det >> 16 & 0xFF}, "
          f"rescue key {'held' if det >> 24 & 1 else 'not held'}{req}", file=out)
    if "stub_oct_dn" in d and d["stub_ver"] >= 2:
        def n(v, tail=False):
            return bin(v >> 20 if tail else v).count("1")
        dn, up, idle = d["stub_oct_dn"], d["stub_oct_up"], d["stub_idle"]
        print(f"rescue key: OCT- closed in {n(dn)} of 32 samples (tail {n(dn, True)} of 12), OCT+ {n(up)} "
              f"(tail {n(up, True)}), idle {n(idle)} (tail {n(idle, True)}); samples 0x{dn:08X} 0x{up:08X} "
              f"0x{idle:08X} (bit 0 the first, 1.5 ms apart; held = OCT- tail >= 10, the others <= 1); "
              f"PA in 0x{d['stub_pa_in']:08X} at the last (PA8 {'low' if not d['stub_pa_in'] >> 8 & 1 else 'high'})",
              file=out)


def print_debug(vals, out, stub_only=False, peak=(), usb=()):
    d = dict(zip(DEBUG_NAMES, vals))
    log = vals[len(DEBUG_NAMES):len(DEBUG_NAMES) + DEBUG_LOG]
    d.update(zip(DEBUG_TAIL, vals[len(DEBUG_NAMES) + DEBUG_LOG:]))
    d.update(zip(DEBUG_PEAK, peak))
    d.update(zip(DEBUG_USB, usb))
    if stub_only:
        for k in [k for k in d if k.startswith("stub_")]:
            print(f"{k} {d[k]}" + (f" (0x{d[k]:08X})" if k in DEBUG_TAIL or k == "stub_detail" else ""), file=out)
        stub_lines(d, out)
        return
    for k, v in enumerate(vals[:len(DEBUG_NAMES)]):
        name = DEBUG_NAMES[k]
        print(f"{name} {v}" + (f" (0x{v:02X})" if name.startswith("ua_csr") else ""), file=out)
    for k in DEBUG_TAIL:
        if k in d:
            print(f"{k} {d[k]} (0x{d[k]:08X})", file=out)
    for k in DEBUG_PEAK:
        if k in d:
            print(f"{k} {d[k]}" + (f" ({LATE_STAGE[d[k]] if d[k] < len(LATE_STAGE) else '?'})" if k == "pk_late_stage" else
                                   f" (0x{d[k]:08X})" if k in PEAK_HEX else
                                   f" ({shed_voice_text(d[k])})" if k == "shed_last_voice" else
                                   f" ({SHED_WHY[d[k]] if d[k] < len(SHED_WHY) else '?'})" if k == "shed_last_why" else
                                   f" ({ERASE_WHY[d[k]] if d[k] < len(ERASE_WHY) else '?'})" if k == "last_erase_why" else
                                   f" ({C1_BAR[d[k]] if d[k] < len(C1_BAR) else '?'})" if k == "c1_bar" else
                                   f" ({BOOT_STAGE.get(d[k], '?')})" if k == "boot_stage" else ""),
                  file=out)
    if "crash_count" in d:
        if d["crash_count"]:
            print(f"last crash (kept until a power-off): #{d['crash_count']}, vector {d['crash_vec']}, pc 0x{d['crash_pc']:08X}, "
                  f"rets 0x{d['crash_rets']:08X}, EMU_MSG 0x{d['crash_emu']:08X}, DBG_MSG 0x{d['crash_dbg']:08X}, "
                  f"{d['crash_uptime_ms'] / 1000:.1f} s after its boot (build/choralroot.dis names the section)", file=out)
        else:
            print("last crash: none since the power-on", file=out)
    for k in DEBUG_USB:
        if k in d:
            print(f"{k} {d[k]}", file=out)
    if "ua_rate_changes" in d:
        print(f"ChoralRoot In: {d['ua_rate']} Hz now, the last whole open at "
              f"{d['ua_rate_last_open'] or 'no rate (none yet)'}" + (" Hz" if d["ua_rate_last_open"] else "") +
              f"; the host set the rate {d['ua_rate_sets']} times ({d['ua_rate_changes']} changes)", file=out)
        if "ua_48k_offered" in d:
            print(f"{USB_RATE_OPTION}: " + ("44.1 + 48 kHz (the computer chooses)" if d["ua_48k_offered"] else
                                             "44.1 kHz (48 kHz is not offered: 0.18's device)") +
                  (f"; the host asked for 48 kHz {d['ua_48k_refused']} times and got 44.1" if d.get("ua_48k_refused") else ""),
                  file=out)
        if d.get("ua_rs_blocks"):
            print(f"48 kHz: packets of 48 / 47 / 49 frames: {d.get('ua_pk576', 0)} / {d.get('ua_pk564', 0)} / "
                  f"{d.get('ua_pk588', 0)}; the resampler takes {d['ua_rs_avg_us']} us a block on average = "
                  f"{4 * d['ua_rs_avg_us']} us of a half's 2902 ({4 * d['ua_rs_avg_us'] * 100 / 2902:.1f} %), "
                  f"{d['ua_rs_max_us']} us at most for one block (TIMER5 nested in it)", file=out)
    if "pk_block_us" in d and d.get("pk_halves"):
        print(f"worst half since the last --debug: {d['pk_block_us']} us of 2902 ({d['pk_block_us'] * 100 // 2902} %) over "
              f"{d['pk_halves']} halves ({d['pk_halves'] * 128 / 44100:.1f} s); stages tick {d['pk_tick_us']}, part0 "
              f"{d['pk_part0_us']}, part1 {d['pk_part1_us']}, fx {d['pk_fx_us']}, master {d['pk_master_us']}, wait "
              f"{d['pk_wait_us']}; late {d['pk_late']}"
              + (f" (the latest: {d['pk_late_us']} us, its largest stage {LATE_STAGE[d['pk_late_stage']] if d['pk_late_stage'] < len(LATE_STAGE) else '?'})"
                 if d["pk_late"] else ""), file=out)
        print(f"fx stage, avg / max us a half over {d['fx_busy_halves']} halves with the effects running: down "
              f"{d['fx_down_avg_us']} / {d['fx_down_max_us']}, delay {d['fx_delay_avg_us']} / {d['fx_delay_max_us']}, reverb "
              f"{d['fx_reverb_avg_us']} / {d['fx_reverb_max_us']}, up {d['fx_up_avg_us']} / {d['fx_up_max_us']}", file=out)
        print(f"flash since power-on: {d['flash_erases']} erases (45 ms of silence each), {d['flash_holes']} of them into a "
              f"sounding output (holes), settings saves {d['saves_settings']}, session saves {d['saves_session']}", file=out)
    if "erases_near" in d:
        print(f"quiet writes since power-on: {d['saves_auto']} saves by the unit itself (Options, favourites), "
              f"{d['erases_ahead']} of the {d['flash_erases']} erases done ahead (at power-on or after 10 s idle; the "
              f"rest: saves that had to erase), {d['erases_near']} erases started by the unit while a voice was active or "
              f"within 2 s of an input (the rule: 0), the longest page program {d['flash_prog_us']} us"
              + ("" if not d["erases_near"] else ": AN ERASE FELL NEAR PLAYING"), file=out)
    if "flash_read_bytes_ps" in d:
        print(f"the UI since the last --debug: a frame took {d['ui_frame_avg_us']} us on average, {d['ui_frame_max_us']} us "
              f"at most; flash read {d['flash_reads_ps']} times a second ({d['flash_read_bytes_ps']} bytes a second)"
              + ("" if d["ui_frame_max_us"] < 50000 else ": A FRAME TOOK OVER 50 MS"), file=out)
    if "stale_halves" in d and d.get("pk_halves"):
        print(f"the deadline: {d['stale_halves']} halves in which the DAC played something old (the audible ones; a late "
              f"half alone is not); the ISR started at most {d['pk_lag_us']} us behind its boundary, its longest half "
              f"took {d['pk_wall_us']} us with TIMER5 nested in it ({d['pk_t5_us']} us of TIMER5 in one half at most)", file=out)
        print(f"interrupts: TIMER5 (100 us apart) once came {d['irq_gap_us']} us after the one before"
              + (f": masked for about {d['irq_gap_us'] - 100} us, until 0x{d['irq_gap_pc']:08X} (build/choralroot.dis)"
                 if d["irq_gap_us"] > 150 else ": nothing masked them for long"), file=out)
    if "shed_voices" in d and d.get("pk_halves"):
        print(f"overload: {d['shed_over_halves']} halves above 85 % of the budget since the last --debug, "
              f"{d['shed_voices']} voices shed (two such halves in a row fade one)"
              + (f"; the latest: {shed_voice_text(d['shed_last_voice'])}, "
                 f"{SHED_WHY[d['shed_last_why']] if d['shed_last_why'] < len(SHED_WHY) else '?'}, asked by a half of "
                 f"{d['shed_last_us']} us" if d["shed_last_why"] else "")
              + f"; voices faded out for another note, part or the shed {d['voices_faded']}, own voices taken for a new "
              f"note {d['voices_stolen']}", file=out)
    if "p5_core0_voices" in d and (d["p5_core0_voices"] or d["p5_core1_voices"]):
        print(f"P5: at most {d['p5_core0_voices']} voices rendered by core 0 and {d['p5_core1_voices']} by core 1 in one "
              f"block (four each is the rule), the bass part {d['p5_bass_voices']} at most, {d['p5_bass_given_up']} held "
              f"bass voices given up to the chord; core 1's longest job of a half {d.get('pk_part1_us', 0)} us "
              f"(pk_part1_us), core 0's longest wait for it {d.get('pk_wait_us', 0)} us (pk_wait_us)", file=out)
    if d.get("key_ons"):
        print(f"keys: {d['key_ons']} note-ons since the last --debug; from the first closed scan (the debounce, 1.1 to 2.2 "
              f"ms, in it) the main loop posted one after {d['key_pick_us'] / 1000:.1f} ms at most, the engine had it "
              f"after {d['key_eng_avg_us'] / 1000:.1f} ms on average, {d['key_eng_us'] / 1000:.1f} ms at most (key_eng_us: "
              f"a frame's draw no longer adds to it; the sound leaves the DAC 2.9 ms later)", file=out)
    stub_lines(d, out)
    if d.get("ua_open_close_ms"):
        print(f"last whole open: {d['ua_frames_last_open']} frames in {d['ua_open_close_ms']} ms = "
              f"{d['ua_frames_last_open'] / d['ua_open_close_ms']:.4f} frames/ms" +
              (f" (at {d['ua_rate_last_open']} Hz: {d['ua_rate_last_open'] / 1000:g} is the rate's)"
               if d.get("ua_rate_last_open") else ""), file=out)
    if log:
        n = vals[DEBUG_NAMES.index("ua_log_n")]
        print("event log (oldest first):", file=out)
        for j in range(0, len(log) - 1, 2):
            if j // 2 >= 16 - min(n, 16):
                print("  " + debug_event(log[j], log[j + 1]), file=out)


def usb_values(link, timeout=1.0):
    """79 USB -> {name: value} (DEBUG_USB), or {} from a firmware before 0.19 (no answer, or "unsupported")"""
    try:
        r = BackupLink(link).request(BK_USB, [], timeout)
    except BackupError:
        return {}
    if not r or r[0] != 1 or len(r) != 2 + 5 * r[1]:
        return {}
    return dict(zip(DEBUG_USB, [bk_r32(r, 2 + 5 * k) for k in range(r[1])]))


def debug_values(link, timeout):
    """73 DEBUG -> its values"""
    r = BackupLink(link).request(BK_DEBUG, [], timeout)
    if not r or r[0] != 1 or len(r) != 2 + 5 * r[1]:
        raise BackupError("unexpected debug reply")
    return [bk_r32(r, 2 + 5 * k) for k in range(r[1])]


def run_stub(up, what, out):
    """--stub test | rescue: 77 STUB, then what the restart shows. The FM-1 answers and leaves the bus 150 ms later
    (cr_backup.c ed_service): the link is closed as soon as the answer is read, before it is gone. An answer that
    was not seen does not decide (on the unit the FM-1 had taken requests whose answer never came): what comes
    back does. Its uptime (DEBUG's ms) says whether it restarted since the request, the stub's own words why."""
    dev = up.find()
    if not dev or dev.id.loader:
        raise not_found(up)
    before = up.ports() if up.unit else None     # (--unit: its update mode is the one port its restart brings)
    t0, r, unseen = time.monotonic(), None, None
    try:
        r = BackupLink(dev.link).request(BK_STUB, [1 if what == "test" else 2], 1.5)
    except InstallError as e:
        if e.code not in ("lost", "backup"):
            raise
        unseen = e
    finally:
        dev.link.close()
    if unseen:
        where = "the FM-1 has left the bus" if unseen.code == "lost" else "the FM-1 is still there"
        print(f"stub {what}: no answer was seen ({where}); what it shows next tells whether it took the request",
              file=out)
    elif len(r) != 1:
        raise BackupError("unexpected stub reply")
    elif r[0]:
        raise BackupError(f"stub request refused: {STUB_RC.get(r[0], r[0])}", r[0])
    else:
        print(f"stub {what}: accepted, the FM-1 restarts", file=out)
    if not unseen or unseen.code == "lost":
        time.sleep(DELAY["reboot"])

    def restarted(link):
        """has the FM-1 started since the request? -> (yes, DEBUG's values); its first value is the uptime in ms
        (0: a build that does not count it, nothing is known)"""
        vals = debug_values(link, 3.0)
        return 0 < vals[0] < (time.monotonic() - t0) * 1000 + 1000, vals

    if what == "rescue":
        if up.unit:                               # update mode has no ID: the one new port; else the firmware by its ID
            back = up.wait_new(before, dev.name, DELAY["wait_reboot"]) or up.find(lambda i: not i.loader, False)
        else:
            back = up.wait_for(lambda i: True, DELAY["wait_reboot"])
        if not back:
            w = up.waited(DELAY["wait_reboot"], "a MIDI port that answers the identity query")
            raise InstallError("noreturn", f"nothing came back on USB-MIDI ({w}): no ota-FM-1 and no FM-1. One "
                                           "long LED flash and a \"WL82 UBOOT1.00\" disk: the staged loader was "
                                           "refused (switch off and on to start the firmware). Nothing at all: "
                                           "docs/BOOT-SAFETY.md")
        try:
            if unseen and not back.id.loader:     # the firmware, as before the request: did it restart at all?
                try:
                    fresh = restarted(back.link)[0]
                except InstallError:
                    fresh = False
                if not fresh:
                    raise unseen
        finally:
            back.link.close()
        if not back.id.loader:
            raise InstallError("mismatch", f"the FM-1 came back as {back.id.text}, not in update mode: the stub did not "
                                           "take the request (--debug shows what it saw)")
        print(f"update mode: {back.id.text}  ({back.name}). To leave it: install a package (fm1_install.py "
              "PACKAGE.fwsc; the same build writes nothing), or switch the FM-1 off and on", file=out)
        return 0

    def look():
        """the firmware that is there (again) -> (has it started since the request?, DEBUG's values)"""
        back = up.back()
        try:
            if not unseen or unseen.code == "lost":
                time.sleep(DELAY["start"])        # (the firmware's first second: USB up before its main loop answers)
            return restarted(back.link)
        finally:
            back.link.close()

    try:
        fresh, vals = look()
    except InstallError as e:
        if not unseen or e.code == "noreturn":
            raise
        if unseen.code == "lost" or e.code != "lost":   # (a firmware without 77 has no DEBUG either: the first
            raise unseen from None                      # error is the one to say)
        unseen = e                                # still there after the request, gone when asked: it restarts now
        time.sleep(DELAY["reboot"])
        try:
            fresh, vals = look()
        except InstallError as e2:
            if e2.code == "noreturn":
                raise
            raise unseen from None
    if unseen and not fresh:
        raise unseen
    if unseen and not dict(zip(DEBUG_NAMES, vals)).get("stub_detail", 0) >> 26 & 1:
        raise BackupError("the FM-1 restarted, but its boot stub did not see the test request (and no answer to 77 "
                          "was seen)")
    print_debug(vals, out, stub_only=True)
    return 0


def acting_op(a):
    """the commands that write to an FM-1, restart it or change what it plays: with several FM-1s they need --unit"""
    return bool(a.package or a.restore or a.import_sound or a.import_syx or a.rename_sound or a.delete_sound or
                getattr(a, "restore_favorites", None) or getattr(a, "stub", None) or getattr(a, "select", None) or
                getattr(a, "screen", None))


def info_lines(p, out):
    """--info, one FM-1"""
    mode = "update loader (update not finished)" if p.ident.loader else "running"
    print(f"{p.ident.text}  [{mode}]  port: {p.name}" + (f"  version: {p.version}" if p.info else ""), file=out)
    print(f"unit ID: {id_text(p)}", file=out)
    if not p.ident.loader:
        verdict, kind, name = classify_firmware(p.ident.text, p.version, p.name)
        print(f"ChoralRoot can be installed over {name}" if verdict == "allow" else
              f"ChoralRoot is not installed over {name} (docs/INSTALL-COMPAT.md)", file=out)
        if kind == "bubba":
            print(BUBBA_TEXT, file=out)


def run_info(up, out):
    """--info: every FM-1 that answers (with --unit: that one), its identity, version and unit ID"""
    if up.unit:
        probes, silent = [up.select()], []
    else:
        probes, silent = up.survey(True)
    if not probes:
        raise not_found(up)
    for k, p in enumerate(probes):
        if k:
            print(file=out)
        info_lines(p, out)
    if len(probes) > 1:
        ids = [p.uid or "none" for p in probes]
        print(f"\n{len(probes)} FM-1s connected (unit IDs {', '.join(ids)}): --unit ID names the one a command is for",
              file=out)
        if silent:
            print(f"no answer on: {', '.join(silent)}", file=out)
    return 0


def run(a, backend, out, ask):
    up = Updater(backend, a.port, getattr(a, "unit", None), acting_op(a), out)
    if getattr(a, "rescan_midi", False):
        why = up.rescan()
        if why:
            raise InstallError("other", f"no MIDI rescan: {why}")
        for _ in range(5):                        # (the drivers start again: its port within a few seconds)
            time.sleep(DELAY["retry"])
            names = up.names()
            if any(PORT_RE.search(n) or (a.port and a.port.lower() in n.lower()) for n in names):
                break
        print(f"MIDI rescanned. MIDI inputs: {', '.join(names) if names else '(none)'}", file=out)
        return 0
    if getattr(a, "stub", None):
        return run_stub(up, a.stub, out)
    if sound_op(a):
        return run_sounds(up, a, out, ask)
    if getattr(a, "favorites", False):
        return run_favorites(up, out)
    if getattr(a, "restore_favorites", None):
        return run_restore_favorites(up, a.restore_favorites, out, ask, a.yes)
    if getattr(a, "select", None):
        part, eng, pos = a.select
        if part not in (0, 1) or not 0 <= eng < 128 or not 0 <= pos < 16384:
            raise InstallError("usage", "--select PART ENGINE POS: part 0 (chord) or 1 (bass), engine 0..127, pos 0..16383")
        dev = up.find()
        if not dev or dev.id.loader:
            raise not_found(up)
        try:
            r = BackupLink(dev.link).request(BK_SELECT, [part, eng, pos & 127, pos >> 7], 1.5)
        finally:
            dev.link.close()
        if len(r) != 1:
            raise BackupError("unexpected select reply")
        if r[0]:
            raise BackupError(f"select refused: {SELECT_RC.get(r[0], r[0])}", r[0])
        print(f"part {part}: engine {eng} position {pos}", file=out)
        return 0
    if getattr(a, "screen", None):
        dev = up.find()
        if not dev or dev.id.loader:
            raise not_found(up)
        try:
            r = BackupLink(dev.link).request(BK_SCREEN, [1 if a.screen == "on" else 0], 1.5)
        finally:
            dev.link.close()
        if r != [0]:
            raise BackupError("unexpected screen reply")
        print(f"screen {a.screen}", file=out)
        return 0
    if getattr(a, "debug", False):
        dev = up.find()
        if not dev or dev.id.loader:
            raise not_found(up)
        pk = None
        try:
            r = BackupLink(dev.link).request(BK_DEBUG, [], 1.5)
            try:                                  # (a firmware before 0.18's PEAK: no answer, or "unsupported")
                pk = BackupLink(dev.link).request(BK_PEAK, [], 1.0)
            except BackupError:
                pk = None
            usb = usb_values(dev.link)            # (0.19: the rate and the 48 kHz counters; {} before)
        finally:
            dev.link.close()
        if not r or r[0] != 1 or len(r) != 2 + 5 * r[1]:
            raise BackupError("unexpected debug reply")
        peak = [bk_r32(pk, 2 + 5 * k) for k in range(pk[1])] if pk and pk[0] == 1 and len(pk) == 2 + 5 * pk[1] else []
        print_debug([bk_r32(r, 2 + 5 * k) for k in range(r[1])], out, peak=peak,
                    usb=[usb[k] for k in DEBUG_USB if k in usb])
        return 0
    if a.info:
        return run_info(up, out)
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
    if not dev.id.loader and not official and same_keyboard(dev.id.text, product):
        info = device_info(BackupLink(dev.link))     # the version text tells the Felucca-based firmwares apart
        version = info and info[0]
    if dev.id.loader and SMK_FAMILY.fullmatch(model_of(dev.id.text)) and not our_loader(dev.id.text) and not official:
        dev.link.close()
        raise InstallError("model", f"the keyboard is in the update mode of another firmware ({dev.id.text}): nothing "
                                    "was written. Finish that update with its own updater")
    print(f"device:  {dev.id.text}  ({dev.name})" + (f"  {version}" if version else "") +
          ("  in update mode: the write will be finished" if dev.id.loader else ""), file=out)
    if not dev.id.loader and not official and same_keyboard(dev.id.text, product):
        verdict, kind, name = classify_firmware(dev.id.text, version, dev.name)   # (the return to V15 is always allowed)
        if kind == "bubba":                       # (its updater refuses our loader: never, not with --force)
            dev.link.close()
            raise InstallError("unsupported", f"the keyboard runs {name}: nothing was written, and --force does not "
                                              "change that. " + BUBBA_TEXT)
        if kind == "other":                       # (its own updater and package format: never, not with --force)
            dev.link.close()
            raise InstallError("unsupported", f"the keyboard runs {name}: ChoralRoot is not installed over it from "
                                              "here, nothing was written. Return to the official firmware with that "
                                              "firmware's own updater first")
        if verdict == "refuse" and not a.force:
            dev.link.close()
            raise InstallError("unsupported", refusal_text(name) + "\n  (--force installs anyway, at your own risk)")
        if verdict == "refuse":
            print(f"--force: installing over {name} anyway, although it is not supported", file=out)
    print("before writing: the FM-1 must be fully charged (it updates on its battery, and this tool cannot read it), "
          "plugged straight into the computer, and the computer kept awake; an interrupted write can leave it dark "
          "with no USB device at all (docs/INSTALL-COMPAT.md). With the boot stub on the FM-1 (ChoralRoot from 0.16 on) "
          "an interrupted write is finished by switching the FM-1 on again (or holding OCT- alone while switching on) "
          "and running this install again; the first install of a version with the stub is not yet covered while its "
          "first sector is written", file=out)
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


# ------------------------------------------------------------- favourites ---
# docs/PRESETS.md "Favourites" (firmware/src/cr_favs.c): the marks on sounds are one small object, backup id 23, read
# with the backup protocol's LIST and GET (nothing is written, no command of its own).

FAV_ID, FAV_VERSION, FAV_USER, FAV_P5 = 23, 2, 0x80, 16
FAV_USER2 = 0x81                                 # 0.22: the pool's slots 33..128 (FAV_USER's ids 32..127 in the text)
FAV_CHORD, FAV_BASS, FAV_OPT = 0x40, 0x20, 0xFF   # version 2: tag | FAV_CHORD / FAV_BASS: the sounds typed so; the options
FAV_ORDER = (12, 15, 13, 14, 7)                  # the engines as the FM-1 shows them (P5 comes last: its own block)


def parse_favorites_full(b):
    """the favourites object -> (marks, types, filter). marks: {tag: [ids]} (tag: an engine's index, FAV_USER the
    user slots; an id: the preset's index in its engine's table, a user slot 0..31, for P5 the Sequential program
    number 1..200). types: {(tag, id): "chord" | "bass"} for the sounds typed so, marked or not (none: Both).
    filter: the favourites filter is on. The object: the version (2; 1, 0.22's first builds, had the marks alone),
    the number of sections, then per section tag, len, len bytes: a tag's bits are its marks (bit i of byte i // 8
    is id i), tag | 0x40 the sounds typed Chord, tag | 0x20 those typed Bass, 0xFF one byte of options (bit 0: the
    filter). Empty bytes: no marks."""
    b = bytes(b)
    if not b:
        return {}, {}, False
    if len(b) < 2 or b[0] not in (1, FAV_VERSION):
        raise BackupError(f"favourites of version {b[0]}: this tool reads versions 1 and {FAV_VERSION}")
    marks, only, filt, p = {}, {"chord": {}, "bass": {}}, False, 2
    for _ in range(b[1]):
        if p + 2 > len(b) or p + 2 + b[p + 1] > len(b):
            raise BackupError("the favourites object is cut short")
        tag, n = b[p], b[p + 1]
        ids = [i for i in range(n * 8) if b[p + 2 + i // 8] >> (i % 8) & 1]
        p += 2 + n
        dst = marks
        if b[0] >= 2:
            if tag == FAV_OPT:
                filt = bool(n and b[p - n] & 1)
                continue
            if tag & FAV_CHORD:
                dst, tag = only["chord"], tag & ~FAV_CHORD
            elif tag & FAV_BASS:
                dst, tag = only["bass"], tag & ~FAV_BASS
        if tag == FAV_P5:
            ids = [i for i in ids if 1 <= i <= 200]
        if tag == FAV_USER2:                       # (0.22: one list of user slots 0..127)
            tag, ids = FAV_USER, sorted(set(dst.get(FAV_USER, [])) | {32 + i for i in ids})
        elif tag == FAV_USER and dst.get(FAV_USER):
            ids = sorted(set(dst[FAV_USER]) | set(ids))
        if ids:
            dst[tag] = ids
    types = {(t, i): "chord" for t, v in only["chord"].items() for i in v}
    for t, v in only["bass"].items():
        for i in v:
            if (t, i) in types:                   # (both at once, a foreign object: Both, as the firmware reads it)
                del types[(t, i)]
            else:
                types[(t, i)] = "bass"
    return marks, types, filt


def parse_favorites(b):
    """the favourites object -> {tag: [ids]}: the marks alone (parse_favorites_full has the types and the filter)"""
    return parse_favorites_full(b)[0]


def p5_program_names():
    """Sequential's 200 programs' PRESETS names by number (tools/gen_prophet_factory.py and the bank in assets/);
    {} where the bank is not beside this tool"""
    try:
        here = os.path.dirname(os.path.abspath(__file__))
        if here not in sys.path:
            sys.path.insert(0, here)
        import gen_prophet_factory as g
        return dict(enumerate(g.names(g.records()), 1))
    except Exception:                             # (no bank, no generator: the numbers alone)
        return {}


def favorites_text(marks, version, user_names=None, p5_names=None, types=None, filt=False):
    """the marks as text. Every line but P5's begins with '#', and P5's are 'NUMBER  # NAME': the whole text is a
    valid assets/prophet5-factory/shipped.txt (tools/p5_shipped_from_favorites.py turns it into the list). A mark
    typed Chord or Bass says so after the name, "  (chord)" / "  (bass)" (in the comment: the line is still the
    list's); a sound that is typed and not marked is not listed."""
    user_names, p5_names, types = user_names or {}, p5_names or {}, types or {}
    total = sum(len(v) for v in marks.values())
    lines = [f"# favorites on {version}: {total} marked" + (", the filter on" if filt else "")]

    def ty(tag, i):
        return f"  ({types[(tag, i)]})" if (tag, i) in types else ""

    for e in list(FAV_ORDER) + sorted(t for t in marks if t not in FAV_ORDER and t not in (FAV_USER, FAV_P5)):
        name = ENGINE_NAMES[e] if e < len(ENGINE_NAMES) else f"engine {e}"
        presets, first = FACTORY_PRESETS.get(str(e), []), FACTORY_FIRST.get(str(e), 0)
        for i in marks.get(e, []):
            lines.append(f"# {name} {i - first + 1:02d} {presets[i] if i < len(presets) else '?'}" + ty(e, i))
    for k in marks.get(FAV_USER, []):
        lines.append(f"# user U{k + 1:02d} {user_names.get(k + 1, '')}".rstrip() + ty(FAV_USER, k))
    p5 = sorted(marks.get(FAV_P5, []))
    lines.append(f"# P5: {len(p5)} program{'' if len(p5) == 1 else 's'}, by Sequential's number"
                 + (" (the lines below as they are: assets/prophet5-factory/shipped.txt)" if p5 else ""))
    lines += [(f"{i:<6d} # {p5_names.get(i, '')}".rstrip() + ty(FAV_P5, i)) if p5_names.get(i) or ty(FAV_P5, i) else str(i)
              for i in p5]
    return "\n".join(lines) + "\n"


def run_favorites(up, out):
    """--favorites: LIST, GET of object 23 (and of the user banks for the names of marked user sounds) -> the text"""
    bl, (version, fam) = open_backup(up)
    try:
        if fam != "choralroot":
            raise InstallError("nosounds", f"{version}: favourites are ChoralRoot's (0.22 on)")
        man = {o["id"]: o for o in bk_manifest(bl.request(BK_LIST, [], 3.0))}
        if FAV_ID not in man:
            raise InstallError("nosounds", f"{version} keeps no favourites (ChoralRoot 0.22 on)")
        data = bk_get_object(bl, FAV_ID, man[FAV_ID]["size"])
        if bk_crc(data) != man[FAV_ID]["crc"] and data:
            raise BackupError("the FM-1 changed its favourites while they were read: try again")
        marks, types, filt = parse_favorites_full(data)
        names = {}
        if marks.get(FAV_USER):
            objs = {i: bk_get_object(bl, i, man[i]["size"]) for i in (6, 7) if i in man}
            for k in marks[FAV_USER]:
                r = _record(objs, k + 1)
                if sound_record_valid(r):
                    names[k + 1] = _record_name(r)
    finally:
        bl.link.close()
    out.write(favorites_text(marks, version, names, p5_program_names() if marks.get(FAV_P5) else {}, types, filt))
    return 0


# --restore-favorites FILE (3y54): a --favorites text back into object 23, packed as cr_favs.c crf_pack does
FAV_SEC = ((FAV_USER, 4), (FAV_USER2, 12), (7, 2), (12, 8), (13, 8), (14, 16), (15, 8), (FAV_P5, 26))   # cr_favs.c
                                                 # CRF_SEC, in order (0.22: FAV_USER2 the pool's slots 33..128)
FAV_PACK_MAX = 308                               # cr_favs.c CRF_PACK_MAX (cr_backup.c refuses more; 377264f: 264)


def pack_favorites(marks, types=None, filt=False):
    """{tag: [ids]}, {(tag, id): "chord"|"bass"}, filter -> the object's bytes, byte for byte as the firmware's
    crf_pack writes them (version 2; per section of FAV_SEC: the marks, then the Chord, then the Bass bitmap, the zero
    bytes at the end dropped, a zero bitmap not stored; the options last, only when the filter is on)"""
    types = types or {}
    sizes = dict(FAV_SEC)
    if any(i >= 32 for i in marks.get(FAV_USER, [])) or any(t == FAV_USER and i >= 32 for t, i in types):
        marks = dict(marks)                        # (slots 33..128: section 0x81, as the firmware packs them)
        marks[FAV_USER2] = [i - 32 for i in marks[FAV_USER] if i >= 32] if FAV_USER in marks else []
        marks[FAV_USER] = [i for i in marks.get(FAV_USER, []) if i < 32]
        types = {((FAV_USER2, i - 32) if t == FAV_USER and i >= 32 else (t, i)): v for (t, i), v in types.items()}
    for tag, ids in marks.items():
        for i in ids:
            if tag not in sizes or not 0 <= i < sizes[tag] * 8 or (tag == FAV_P5 and not 1 <= i <= 200):
                raise InstallError("badbackup", f"favourites: no such sound to mark (section {tag}, id {i})")
    out = bytearray([FAV_VERSION, 0])
    for tag, n in FAV_SEC:
        for w, ortag in ((None, 0), ("chord", FAV_CHORD), ("bass", FAV_BASS)):
            bits = bytearray(n)
            ids = marks.get(tag, []) if w is None else [i for (t, i), v in types.items() if t == tag and v == w]
            for i in ids:
                if 0 <= i < n * 8 and not (tag == FAV_P5 and not 1 <= i <= 200):
                    bits[i // 8] |= 1 << (i % 8)
            bits = bytes(bits).rstrip(b"\0")
            if bits:
                out += bytes([tag | ortag, len(bits)]) + bits
                out[1] += 1
    if filt:
        out += bytes([FAV_OPT, 1, 1])
        out[1] += 1
    return bytes(out)


def parse_favorites_text(text):
    """a --favorites text -> (marks, types, filter, skipped lines). P5: 'NUMBER  # NAME  (type)'; the engines' and the
    user sounds' comment lines '# FM6 01 NAME  (type)', '# user U03 NAME  (type)' are decoded too (the engine by its
    name, the preset by its pool place); the filter from the header ', the filter on'. Other '#' lines are notes."""
    marks, types, filt, skipped = {}, {}, False, []
    names = sorted(((ENGINE_NAMES[e], e) for e in FAV_ORDER), key=lambda x: -len(x[0]))

    def add(tag, i, rest):
        if i not in marks.setdefault(tag, []):
            marks[tag].append(i)
        m = re.search(r"\((chord|bass)\)\s*$", rest)
        if m:
            types[(tag, i)] = m.group(1)

    for ln in text.splitlines():
        s = ln.strip()
        if not s:
            continue
        if not s.startswith("#"):
            num, _, rest = s.partition("#")
            if not re.fullmatch(r"\d+", num.strip()) or not 1 <= int(num) <= 200:
                raise InstallError("badbackup", f"not a P5 program number 1..200: {ln!r}")
            add(FAV_P5, int(num), rest)
            continue
        body = s[1:].strip()
        if body.startswith("favorites on "):
            filt = body.endswith(", the filter on")
            continue
        if body.startswith("P5:"):
            continue
        m = re.match(r"user U(\d\d\d?)\b(.*)$", body)
        if m and 1 <= int(m.group(1)) <= SOUND_SLOTS:
            add(FAV_USER, int(m.group(1)) - 1, m.group(2))
            continue
        for name, e in names:
            m = re.match(re.escape(name) + r" (\d\d)\b(.*)$", body)
            if m:
                add(e, int(m.group(1)) - 1 + FACTORY_FIRST.get(str(e), 0), m.group(2))
                break
        else:
            skipped.append(ln)
    for t in marks:
        marks[t].sort()
    return marks, types, filt, skipped


def cr_version_at_least(version, want=(0, 22)):
    m = re.search(r"(\d+)\.(\d+)", version or "")
    return bool(m) and (int(m.group(1)), int(m.group(2))) >= want


def run_restore_favorites(up, path, out, ask, yes):
    """--restore-favorites FILE: the marks of a --favorites text -> object 23 (a PUT of it alone), read back and
    compared. cr_backup.c applies a PUT of 23 at once (crf_boot: the RAM copy from flash), no restart"""
    try:
        text = open(path, encoding="utf-8").read()
    except (OSError, UnicodeDecodeError) as e:
        raise InstallError("badbackup", f"cannot read {path}: {getattr(e, 'strerror', None) or e}")
    marks, types, filt, skipped = parse_favorites_text(text)
    types = {k: v for k, v in types.items() if k[1] in marks.get(k[0], [])}
    data = pack_favorites(marks, types, filt)
    if len(data) > FAV_PACK_MAX:
        raise InstallError("badbackup", f"favourites object of {len(data)} bytes: over {FAV_PACK_MAX}")
    total = sum(len(v) for v in marks.values())
    bl, (version, fam) = open_backup(up)
    try:
        if fam != "choralroot" or not cr_version_at_least(version):
            raise InstallError("nosounds", f"{version}: favourites (object 23, version 2) are ChoralRoot's, 0.22 on")
        man = {o["id"]: o for o in bk_manifest(bl.request(BK_LIST, [], 3.0))}
        if FAV_ID not in man:
            raise InstallError("nosounds", f"{version} keeps no favourites (ChoralRoot 0.22 on)")
        for ln in skipped:
            print(f"skipped (not a mark): {ln.strip()}", file=out)
        per = ", ".join(f"{'user' if t == FAV_USER else 'P5' if t == FAV_P5 else ENGINE_NAMES[t]} {len(marks[t])}"
                        for t, _n in FAV_SEC if marks.get(t))
        nt = {w: sum(1 for v in types.values() if v == w) for w in ("chord", "bass")}
        print(f"write onto {version}: favourites (object 23, {len(data)} bytes, version {FAV_VERSION}): {total} marks"
              f" ({per or 'none'}); {nt['chord']} Chord, {nt['bass']} Bass, the rest Both; the filter "
              f"{'on' if filt else 'off'}. It replaces the marks on the FM-1; nothing else is written", file=out)
        if not yes and not ask("Restore the favourites? [y/N] "):
            print("cancelled", file=out)
            return 1
        bk_put_object(bl, FAV_ID, data, busy=lambda: print("busy: stop the loop on the FM-1", file=out))
        man = {o["id"]: o for o in bk_manifest(bl.request(BK_LIST, [], 3.0))}
        back = bk_get_object(bl, FAV_ID, man[FAV_ID]["size"])
    finally:
        bl.link.close()
    if back != data:
        raise BackupError("the favourites read back differ from what was written")
    print(f"{total} marks restored", file=out)
    return 0


def sound_op(a):
    return [o for o, v in (("--sounds", a.sounds), ("--export-sound", a.export_sound), ("--import-sound", a.import_sound),
                           ("--export-syx", a.export_syx), ("--import-syx", a.import_syx),
                           ("--rename-sound", a.rename_sound), ("--delete-sound", a.delete_sound)) if v]


def ask_tty(prompt):
    Watch.paused = True                           # (the user's time is not the tool's: the clock stops)
    try:
        return input(prompt).strip().lower() in ("y", "yes")
    except EOFError:
        return False
    finally:
        Watch.step()
        Watch.paused = False


def main(argv=None, backend=None, out=sys.stdout, ask=ask_tty):
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("package", nargs="?", help="Felucca package (.fwsc)")
    ap.add_argument("--info", action="store_true", help="print the identity of the connected FM-1")
    ap.add_argument("--debug", action="store_true", help="print ChoralRoot's diagnostic counters (read-only)")
    ap.add_argument("--select", nargs=3, type=int, metavar=("PART", "ENGINE", "POS"),
                    help="ChoralRoot: part 0 (chord) / 1 (bass) to ENGINE's pool position POS (0 INIT, 1.. the factory "
                         "presets, then the added ones, as PRESETS turns), live (testing the device manager)")
    ap.add_argument("--screen", choices=("off", "on"), help="ChoralRoot: switch the screen off (until --screen on or a "
                                                            "key, button or knob on the unit; MIDI in does not wake it) "
                                                            "or on")
    ap.add_argument("--rescan-midi", action="store_true",
                    help="macOS: have CoreMIDI rescan its devices (Audio MIDI Setup's Rescan MIDI), for an FM-1 on USB "
                         "whose MIDI port stays offline; every MIDI program's ports restart with it")
    ap.add_argument("--stub", choices=("test", "rescue"),
                    help="ChoralRoot with the boot stub (v2): test = restart once with the stub's loader check on and "
                         "print its --debug lines; rescue = restart into update mode, as OCT- held at power-on (to "
                         "leave update mode: install a package, or switch the FM-1 off and on)")
    ap.add_argument("--port", metavar="NAME", help="MIDI port to use (part of its name)")
    ap.add_argument("--unit", metavar="ID", help="only the FM-1 with this unit ID (8 hex digits, any case: --info, or "
                                                 "Options > Version on the unit; ChoralRoot from 0.22), with any "
                                                 "command; for several FM-1s on one computer")
    ap.add_argument("--yes", action="store_true", help="do not ask for confirmation")
    ap.add_argument("--force", action="store_true", help="install a package without the Felucca loader marker, or over a "
                                                         "firmware ChoralRoot does not support installing over (at your own risk)")
    ap.add_argument("--backup", metavar="FILE", help="save a backup of the FM-1 to FILE (a directory: a dated name) "
                                                     "before the install, or alone")
    ap.add_argument("--restore", metavar="FILE", help="restore a backup FILE onto the FM-1 (after the install, or alone)")
    ap.add_argument("--sounds", action="store_true", help="list the 32 user sounds (ChoralRoot): 'over FM6 02 FM BELL' "
                                                          "for one saved over a factory preset, '+' for one added")
    ap.add_argument("--favorites", action="store_true",
                    help="ChoralRoot 0.22 on: print the sounds marked as favorites (read-only: the backup protocol's "
                         "read of object 23). Per engine a '# FM6 01 TINE EP' line, a marked user sound '# user U03 "
                         "NAME', and P5's programs by their Sequential number, one a line, 'NUMBER  # NAME': the output "
                         "is a valid assets/prophet5-factory/shipped.txt (tools/p5_shipped_from_favorites.py)")
    ap.add_argument("--restore-favorites", metavar="FILE",
                    help="ChoralRoot 0.22 on: write the marks of a --favorites text FILE back to the FM-1 (object 23 "
                         "alone: P5's numbers, the engines' and user sounds' lines, the types, the filter; asks "
                         "first unless --yes), read them back and compare")
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
        ap.error(f"{ops[0]} goes alone (with --port, --unit, --yes): not with a package, --info, --backup or --restore")

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
    if a.favorites and (a.info or a.debug or a.screen or a.select or a.stub or a.rescan_midi or a.package or a.backup
                        or a.restore or ops):
        ap.error("--favorites goes alone")
    if a.restore_favorites and (a.favorites or a.info or a.debug or a.screen or a.select or a.stub or a.rescan_midi
                                or a.package or a.backup or a.restore or ops):
        ap.error("--restore-favorites goes alone")
    if a.debug and (a.info or a.package or a.backup or a.restore or ops):
        ap.error("--debug goes alone")
    if a.screen and (a.debug or a.info or a.package or a.backup or a.restore or ops):
        ap.error("--screen goes alone")
    if a.select and (a.screen or a.debug or a.info or a.package or a.backup or a.restore or ops):
        ap.error("--select goes alone")
    if a.stub and (a.select or a.screen or a.debug or a.info or a.package or a.backup or a.restore or ops):
        ap.error("--stub goes alone")
    if a.rescan_midi and (a.stub or a.select or a.screen or a.debug or a.info or a.package or a.backup or a.restore
                          or ops):
        ap.error("--rescan-midi goes alone")
    if a.unit is not None and not re.fullmatch(r"[0-9A-Fa-f]{8}", a.unit):
        ap.error(f"--unit: a unit ID is 8 hex digits (fm1_install.py --info shows them), not {a.unit!r}")
    if a.unit and a.rescan_midi:
        ap.error("--rescan-midi restarts every MIDI port on this computer, the other FM-1s' too: not with --unit "
                 "(unplug and replug the FM-1 instead)")
    if not (a.rescan_midi or a.stub or a.select or a.screen or a.debug or a.info or a.package or a.backup or a.restore
            or ops or a.favorites or a.restore_favorites):
        ap.error("give a PACKAGE.fwsc, --backup FILE, --restore FILE, --info, --sounds, --screen off|on or --select")
    try:
        return run(a, MidiBackend() if backend is None else backend, out, ask)   # (without python-rtmidi: the first
        #                                                                          listing says so, exit 2)
    except InstallError as e:
        say(f"error: {e}")
        return EXIT.get(e.code, 1)
    except KeyboardInterrupt:
        say("\ninterrupted: if the FM-1 is in update mode, run the install again to finish (not in update mode: "
            "switch it off, hold OCT- alone and switch it on; docs/INSTALL-COMPAT.md)")
        return 130


def say(text):
    """a line on stderr. A stderr that cannot be written (its reader is gone) changes nothing else: not the exit
    code, not the way out through leave()"""
    try:
        print(text, file=sys.stderr)
    except (OSError, ValueError):
        pass


def exit_code(fn):
    """fn() -> the exit code: its own, or the one of what it raised (argparse's and sys.exit's SystemExit, a text
    printed as Python prints it; an InstallError; anything else with its traceback, 1), for leave()"""
    try:
        return fn() or 0
    except SystemExit as e:
        if e.code is None or isinstance(e.code, int):
            return e.code or 0
        say(e.code)
    except InstallError as e:
        say(f"error: {e}")
        return EXIT.get(e.code, 1)
    except KeyboardInterrupt:
        say("\ninterrupted")
        return 130
    except BaseException:
        import traceback
        say(traceback.format_exc().rstrip())
    return 1


def leave(rc):
    """the end of a tool that used the MIDI side: the workers still there close their ports and leave (none outlives
    the tool), what was printed is flushed, and the process ends with rc. Not sys.exit: the interpreter's own
    shutdown waits for the threads that are not daemons and runs the destructors of what is still open, and neither
    a thread left behind in a backend's call (bounded()) nor a destructor may hold the exit; the exit code is the
    same. Whatever holds this function itself (a worker that does not die, an output nobody reads), a timer ends
    the process with rc after DELAY["exit"] seconds."""
    t = threading.Timer(DELAY["exit"], os._exit, (rc,))
    t.daemon = True
    t.start()
    for w in list(Worker.live):
        w.close()
    for f in (sys.stdout, sys.stderr):
        try:
            f.flush()
        except (OSError, ValueError):
            pass
    os._exit(rc)


if __name__ == "__main__":
    if sys.argv[1:2] == ["--midi-worker"]:
        midi_worker(float(sys.argv[2]))
    Watch.start()
    leave(exit_code(main))
