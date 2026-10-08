#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The notes of an emulator WAV (build/host/emu --headless --script S --wav W) and their pitch.

  python3 tools/emu/wavpitch.py W.wav [--gap MS] [--floor F]

The 16-bit output (the channels averaged) is cut into notes at silences: 10 ms blocks whose RMS is under F (default
0.001 of full scale) for at least MS (default 150 ms) end a note. Per note one line:
  note K: START..END s, RMS R, zc HZ, cent HZ, peak HZ
zc: the zero-crossing rate / 2 over the note's middle (from 15 % to 85 % of it: past the attack, before the tail),
the frequency of a sine-like carrier whose phase runs forward (an FM carrier with a small index); cent: the spectral
centroid (the power-weighted mean frequency below 8 kHz: the timbre's brightness, it moves when a modulator's ratio does and
the pitch does not); peak: the largest bin of a Hann-windowed DFT over the same span. Exit 0 always. numpy if it is there, else pure python (slower)."""
import cmath
import math
import struct
import sys
import wave

try:
    import numpy as np
except ImportError:   # pragma: no cover
    np = None


def load(path):
    w = wave.open(path, "rb")
    n, ch, fs = w.getnframes(), w.getnchannels(), w.getframerate()
    raw = w.readframes(n)
    w.close()
    v = struct.unpack("<%dh" % (n * ch), raw)
    return [sum(v[i * ch:(i + 1) * ch]) / (ch * 32768.0) for i in range(n)], fs


def notes(x, fs, gap_ms, floor):
    blk = fs // 100
    out, start, quiet = [], None, 0
    for b in range(0, len(x) - blk, blk):
        r = math.sqrt(sum(s * s for s in x[b:b + blk]) / blk)
        if r >= floor:
            if start is None:
                start = b
            quiet = 0
        elif start is not None:
            quiet += 10
            if quiet >= gap_ms:
                out.append((start, b - quiet * fs // 1000 + blk))
                start, quiet = None, 0
    if start is not None:
        out.append((start, len(x)))
    return out


def pitch(x, fs):
    n = len(x)
    zc = sum(1 for i in range(n - 1) if (x[i] < 0) != (x[i + 1] < 0)) * fs / 2.0 / max(1, n - 1)
    if np is not None:
        a = np.asarray(x) * np.hanning(n)
        sp = np.abs(np.fft.rfft(a))
        sp[0] = 0
        pk = float(np.argmax(sp)) * fs / n
        fr = np.arange(len(sp)) * fs / n
        pw = (sp * sp)[fr < 8000]                                  # (power below 8 kHz)
        ce = float(np.sum(pw * fr[fr < 8000]) / max(1e-30, float(np.sum(pw))))
    else:   # (a coarse search: 20 Hz steps to 5 kHz)
        best, pk, sw, sm = -1.0, 0.0, 0.0, 0.0
        for f in range(20, 5000, 20):
            s = abs(sum(x[i] * cmath.exp(-2j * math.pi * f * i / fs) for i in range(0, n, 2)))
            sw += s * s * f
            sm += s * s
            if s > best:
                best, pk = s, float(f)
        ce = sw / max(1e-12, sm)
    return zc, ce, pk


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return
    gap, floor = 150, 0.001
    if "--gap" in a:
        gap = int(a[a.index("--gap") + 1])
    if "--floor" in a:
        floor = float(a[a.index("--floor") + 1])
    x, fs = load(a[0])
    for k, (s, e) in enumerate(notes(x, fs, gap, floor)):
        m0, m1 = s + (e - s) * 15 // 100, s + (e - s) * 85 // 100
        seg = x[m0:m1]
        rms = math.sqrt(sum(v * v for v in seg) / max(1, len(seg)))
        zc, ce, pk = pitch(seg, fs)
        print("note %d: %.2f..%.2f s, RMS %.4f, zc %.1f Hz, cent %.1f Hz, peak %.1f Hz" % (k + 1, s / fs, e / fs, rms, zc, ce,
                                                                                         pk))


if __name__ == "__main__":
    main()
