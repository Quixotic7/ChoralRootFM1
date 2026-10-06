# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Sample helpers shared by gen_samples.py (built-in sets), fm1_sample_upload.py
(user slots) and the host test pitch.py. The web editor (web/editor.html) has a JS port of
read_any_wav / resample / ima_encode / user_slot that must give the same bytes
(web/test_web.mjs checks it).

The float arithmetic here is part of the generated output: keep the order of
operations (and Python's sum() where it is used) when touching these functions.
"""
import math
import re
import struct
import sys
import zlib
from functools import reduce
from operator import add, sub

IMA_STEP = [7, 8, 9, 10, 11, 12, 13, 14, 16, 17, 19, 21, 23, 25, 28, 31, 34, 37, 41, 45, 50, 55, 60, 66, 73, 80, 88,
            97, 107, 118, 130, 143, 157, 173, 190, 209, 230, 253, 279, 307, 337, 371, 408, 449, 494, 544, 598, 658,
            724, 796, 876, 963, 1060, 1166, 1282, 1411, 1552, 1707, 1878, 2066, 2272, 2499, 2749, 3024, 3327, 3660,
            4026, 4428, 4871, 5358, 5894, 6484, 7132, 7845, 8630, 9493, 10442, 11487, 12635, 13899, 15289, 16818,
            18500, 20350, 22385, 24623, 27086, 29794, 32767]
IMA_IDX = [-1, -1, -1, -1, 2, 4, 6, 8]


def wav_chunks(d):
    """RIFF/WAVE chunks -> {id: (offset of the body, declared length)}; the first of each id wins"""
    if len(d) < 12 or d[:4] != b"RIFF" or d[8:12] != b"WAVE":
        raise ValueError("not a WAV file")
    out, i = {}, 12
    while i + 8 <= len(d):
        cid, n = d[i:i + 4], struct.unpack_from("<I", d, i + 4)[0]
        out.setdefault(cid, (i + 8, n))
        i += 8 + n + (n & 1)
    return out


def wav_format(d, ch):
    """-> (tag, channels, rate, bits); WAVE_FORMAT_EXTENSIBLE resolved to its subformat"""
    if b"fmt " not in ch:
        raise ValueError("WAV without a fmt chunk")
    o = ch[b"fmt "][0]
    tag, nch, sr = struct.unpack_from("<HHI", d, o)
    bits = struct.unpack_from("<H", d, o + 14)[0]
    if tag == 0xFFFE:
        tag = struct.unpack_from("<H", d, o + 24)[0]
    return tag, nch, sr, bits


def read_wav(path):
    """16-bit PCM, first channel, as ints -> (rate, samples, (loop start, loop end) from 'smpl' or None)"""
    d = path.read_bytes()
    ch = wav_chunks(d)
    tag, nch, sr, bits = wav_format(d, ch)
    if bits != 16:
        raise ValueError(f"{path}: {bits}-bit, expected 16")
    o, n = ch.get(b"data", (0, 0))
    s = list(struct.unpack_from("<%dh" % (min(n, len(d) - o) // 2), d, o))[::nch]
    loop = None
    if b"smpl" in ch:
        o = ch[b"smpl"][0]
        if struct.unpack_from("<I", d, o + 28)[0]:          # number of loops
            loop = struct.unpack_from("<II", d, o + 36 + 8)
    return sr, s, loop


def _channel_values(tag, bps, raw):
    """interleaved sample values as floats in [-1, 1)"""
    n = len(raw) // bps
    if tag == 3:
        return list(struct.unpack("<%d%s" % (n, "f" if bps == 4 else "d"), raw))
    if bps == 2:
        return [v / 32768 for v in struct.unpack("<%dh" % n, raw)]
    if bps == 3:                                     # 24 bit: pad to 32 (zero low byte), exact power-of-two scale
        w = bytearray(4 * n)
        w[1::4], w[2::4], w[3::4] = raw[0::3], raw[1::3], raw[2::3]
        return [v / 2147483648 for v in struct.unpack("<%di" % n, w)]
    if bps == 4:
        return [v / 2147483648 for v in struct.unpack("<%di" % n, raw)]
    return [(b - 128) / 128 for b in raw]


def read_any_wav(path):
    """PCM 8/16/24/32 or float, any channel count, mixed to mono -> (rate, [float]).
    A truncated data chunk is fine."""
    d = path.read_bytes()
    ch = wav_chunks(d)
    tag, nch, sr, bits = wav_format(d, ch)
    bps = bits // 8
    if tag not in (1, 3) or not bps or not nch:
        raise ValueError(f"{path}: unsupported WAV format (tag {tag}, {bits} bit)")
    o, n = ch.get(b"data", (0, 0))
    frame = bps * nch
    nfr = min(n, len(d) - o) // frame
    v = _channel_values(tag, bps, d[o:o + nfr * frame])
    if nch == 1:
        return sr, v
    if nch == 2:                                     # (0.0 + l + r) / 2, as the per-channel sum below
        return sr, [(a + b) / 2 for a, b in zip(v[0::2], v[1::2])]
    out = []
    for f in range(0, len(v), nch):
        acc = 0.0
        for c in range(nch):
            acc += v[f + c]
        out.append(acc / nch)
    return sr, out


def resample(x, sr, to):
    """crude anti-alias (moving average over the ratio), then linear interpolation"""
    if sr == to:
        return x
    if sr > to:
        k = max(1, int(round(sr / to)))
        if k == 2:
            x = [(a + b) / 2 for a, b in zip(x, x[1:])] + [x[-1] / 2] if x else []
        elif k > 1:                                  # left to right like the editor (not sum(): compensated
            x = [reduce(add, x[i:i + k]) / k for i in range(len(x))]   # since Python 3.12)
    step, out, p, last = sr / to, [], 0.0, len(x) - 1
    append = out.append
    while p < last:
        i = int(p)
        f = p - i
        append(x[i] * (1 - f) + x[i + 1] * f)
        p += step
    return out


def _diff_py(w, W, hi):
    """d[tau] = sum((w[i] - w[i + tau]) ** 2 for i in range(W)), tau = 1..hi+1"""
    head, two = w[:W], [2] * W
    return [0.0] + [sum(map(pow, map(sub, head, w[tau:tau + W]), two)) for tau in range(1, hi + 2)]


def _diff_np(w, W, hi):
    """_diff_py with numpy, all lags at once. Python >= 3.12 sums floats with Neumaier
    compensation (Objects/bltinmodule.c builtin_sum); this runs the same steps in the same
    order per lag, so the results are the same doubles."""
    a = np.asarray(w, dtype=np.float64)
    taus = np.arange(1, hi + 2)
    f = (a[0] - a[taus]) ** 2                      # sum() starts at int 0: 0 + x == x for x >= 0
    c = np.zeros_like(f)
    for i in range(1, W):
        x = (a[i] - a[i + taus]) ** 2
        t = f + x
        c += np.where(np.abs(f) >= np.abs(x), (f - t) + x, (x - t) + f)
        f = t
    f = np.where(c != 0, f + c, f)
    return [0.0] + f.tolist()


try:
    import numpy as np
    if sys.version_info < (3, 12):               # plain left-to-right sum() there
        raise ImportError
    _diff = _diff_np
except ImportError:
    _diff = _diff_py


def yin(w, sr, fmax=2000, fmin=30, threshold=0.15, interpolate=True):
    """YIN (cumulative-mean-normalised difference) over the window w: the first dip under
    the threshold between the periods of fmax and fmin, else the deepest -> Hz"""
    W = len(w) // 2
    lo, hi = max(2, int(sr / fmax)), min(W - 1, int(sr / fmin))
    d = _diff(w, W, hi)
    cm, run = [1.0] * (hi + 2), 0.0
    for tau in range(1, hi + 2):
        run += d[tau]
        cm[tau] = d[tau] * tau / run if run else 1.0
    tau = next((t for t in range(lo, hi) if cm[t] < threshold and cm[t] <= cm[t + 1]), None)
    if tau is None:
        tau = min(range(lo, hi), key=lambda t: cm[t])
    if interpolate and 1 <= tau < hi:
        y0, y1, y2 = cm[tau - 1], cm[tau], cm[tau + 1]
        den = y0 - 2 * y1 + y2
        tau = tau + (0.5 * (y0 - y2) / den if den else 0)
    return sr / tau


def detect_hz(x, sr):
    """pitch of a recording: YIN on 3000 samples from 0.15 s in (or the last 3000)"""
    a = int(0.15 * sr)
    return yin(x[a:a + 3000] if len(x) > a + 3000 else x[-3000:], sr)


def hz_to_midi(hz):
    return 12 * math.log2(hz / 440.0) + 69


def peak(x, floor=1e-9):
    return max(floor, max(map(abs, x)))


def onset(x, rel=0.02):
    """index of the first sample above rel x peak"""
    thr = rel * peak(x)
    return next((i for i, v in enumerate(x) if abs(v) > thr), 0)


def ima_encode(samples, loop_start):
    """IMA ADPCM, 4 bit, low nibble first, from predictor 0 / index 0
    -> (bytes, (predictor, index) at loop_start)"""
    pred, idx, nib, state_at_loop = 0, 0, bytearray(), (0, 0)
    steps, adj = IMA_STEP, IMA_IDX
    for n, x in enumerate(samples):
        if n == loop_start:
            state_at_loop = (pred, idx)
        step = steps[idx]
        diff = x - pred
        code = 0
        if diff < 0:
            code = 8
            diff = -diff
        vd = step >> 3
        if diff >= step:
            code |= 4
            diff -= step
            vd += step
        if diff >= step >> 1:
            code |= 2
            diff -= step >> 1
            vd += step >> 1
        if diff >= step >> 2:
            code |= 1
            vd += step >> 2
        pred = pred - vd if code & 8 else pred + vd
        if pred > 32767:
            pred = 32767
        elif pred < -32768:
            pred = -32768
        idx += adj[code & 7]
        if idx < 0:
            idx = 0
        elif idx > 88:
            idx = 88
        nib.append(code)
    if len(nib) & 1:
        nib.append(0)
    return bytes(a | (b << 4) for a, b in zip(nib[0::2], nib[1::2])), state_at_loop


def key_split(roots):
    """sorted root notes -> [(lo, hi)]: each zone reaches halfway to its neighbours, the
    outer zones to the ends of the keyboard"""
    n = len(roots)
    return [(0 if j == 0 else (roots[j - 1] + r) // 2 + 1, 127 if j == n - 1 else (r + roots[j + 1]) // 2)
            for j, r in enumerate(roots)]


# ---- user sample slots (firmware/src/eng_sample.c, web/EDITOR_PROTOCOL.md)
SLOT_SIZE, SLOT_DATA_OFF, SLOT_RATE, SLOT_ZONES = 0x14000, 512, 22050, 16
SLOT_HDR_LEN = 32 + SLOT_ZONES * 28
SLOT_MAX_DATA = SLOT_SIZE - SLOT_DATA_OFF


def note_from_name(name, default=60):
    """"C4" / "F#3" / "Bb2" in a file name, C4 = 60 (as the web editor), clamped to 0..127"""
    m = re.search(r"(?<![A-Za-z])([A-G])([#b]?)(-?\d)(?!\d)", name)
    if not m:
        return default
    pc = "C D EF G A B".index(m.group(1)) + {"#": 1, "b": -1, "": 0}[m.group(2)]
    return max(0, min(127, (int(m.group(3)) + 1) * 12 + pc))


def to_int16(x):
    """mono float -> int16 list, peak normalised to 30000"""
    pk = peak(x)
    return [max(-32768, min(32767, int(v / pk * 30000))) for v in x]


def user_slot(name, zones):
    """zones: [(int16 samples at SLOT_RATE, root, lo or None, hi or None)] -> (header bytes, ADPCM data).
    Zones without lo/hi split the keyboard between their roots."""
    zs, data = [], bytearray()
    for s, root, lo, hi in zones:
        adp, st = ima_encode(s, 0)
        zs.append(dict(off=len(data), n=len(s), ls=0, le=len(s) - 1, root=root, lo=lo, hi=hi, pred=st[0], idx=st[1]))
        data += adp
    if len(data) > SLOT_MAX_DATA:
        raise ValueError(f"too long: {len(data)} B of ADPCM, a slot holds {SLOT_MAX_DATA} B "
                         f"({SLOT_MAX_DATA * 2 / SLOT_RATE:.1f} s in all)")
    if not 1 <= len(zs) <= SLOT_ZONES:
        raise ValueError(f"1..{SLOT_ZONES} files per slot")
    zs.sort(key=lambda z: z["root"])
    for z, (lo, hi) in zip(zs, key_split([z["root"] for z in zs])):
        if z["lo"] is None:
            z["lo"], z["hi"] = lo, hi
    rate = int(round(SLOT_RATE / 44100 * 65536))
    zb = b"".join(struct.pack("<5I2h4B", z["off"], z["n"], z["ls"], z["le"], rate, z["root"] * 16, z["pred"],
                              z["idx"], z["lo"], z["hi"], 0) for z in zs)
    nm = re.sub(r"[^\x20-\x7E]", "", name.upper())[:8].encode().ljust(8, b"\0")
    hdr = struct.pack("<IHBB8sII8x", 0x504D5346, 1, len(zs), 0, nm, len(data), zlib.crc32(data)) + zb
    return hdr.ljust(SLOT_HDR_LEN, b"\0"), bytes(data)
