#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Host-visible pops in an emulator WAV (build/host/emu --headless --script S --wav W).

  python3 tools/emu/wavclicks.py W.wav [--from S] [--to S] [--ratio R] [--floor F] [-v]

Three checks on the 16-bit stereo output:
  jumps   sample-to-sample steps above 0.5 full scale (a hard discontinuity)
  holes   a 128-frame block of exact zeros between sounding blocks (a render that did not happen)
  clicks  short high-frequency bursts: the third difference of the signal (+18 dB / octave: a step, a corner of a
          fast envelope ramp, a phase reset) in 32-frame windows, against the median of the windows around it
          (+-23 ms). A window R times (default 12, ~22 dB) above its neighbourhood and above the floor F (default
          0.002 of full scale, RMS of the third difference) is a click. Musical attacks of bright sounds also rise
          above the median; read the list with the script's note times.
Prints one line per finding (time in s, the 128-frame block, the ratio) and a summary; exit 0 always.
numpy if it is there, else pure python (slower)."""
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
    if np is not None:
        x = np.frombuffer(raw, dtype="<i2").astype(np.float64) / 32768.0
        return x.reshape(-1, ch), fs
    v = struct.unpack("<%dh" % (n * ch), raw)
    return [[v[i * ch + c] / 32768.0 for c in range(ch)] for i in range(n)], fs


def main():
    a = sys.argv[1:]
    if not a:
        print(__doc__)
        return 0
    path, t0, t1, ratio, floor, verbose = a[0], 0.0, 1e9, 12.0, 0.002, False
    i = 1
    while i < len(a):
        if a[i] == "--from": t0 = float(a[i + 1]); i += 2
        elif a[i] == "--to": t1 = float(a[i + 1]); i += 2
        elif a[i] == "--ratio": ratio = float(a[i + 1]); i += 2
        elif a[i] == "--floor": floor = float(a[i + 1]); i += 2
        elif a[i] == "-v": verbose = True; i += 1
        else: i += 1
    x, fs = load(path)
    if np is None:
        print("wavclicks: numpy missing: pure python, jumps only")
        jumps = [k for k in range(1, len(x)) if max(abs(x[k][c] - x[k - 1][c]) for c in range(len(x[0]))) > 0.5]
        for k in jumps:
            print("jump  %8.4f s" % (k / fs))
        print("wavclicks: %d jumps" % len(jumps))
        return 0
    n = x.shape[0]
    lo, hi = int(t0 * fs), min(n, int(t1 * fs))
    m = x.mean(axis=1) if x.shape[1] > 1 else x[:, 0]
    # jumps
    d1 = np.abs(np.diff(x, axis=0)).max(axis=1)
    jumps = [k + 1 for k in np.nonzero(d1 > 0.5)[0] if lo <= k < hi]
    # holes: all-zero 128-frame blocks with sound on both sides (within 4 blocks)
    nb = n // 128
    blk = np.abs(x[: nb * 128]).reshape(nb, 128 * x.shape[1]).max(axis=1)
    zero = blk == 0
    holes = []                                     # (first block, length) of each run of zero blocks
    b = 1
    while b < nb:
        if zero[b] and blk[b - 1] > 30 / 32768:
            e_ = b
            while e_ < nb and zero[e_]:
                e_ += 1
            if e_ < nb and blk[e_:e_ + 4].max() > 30 / 32768 and lo <= b * 128 < hi:
                holes.append((b, e_ - b))
            b = e_
        b += 1
    # clicks
    d3 = np.zeros_like(m)
    d3[3:] = m[3:] - 3 * m[2:-1] + 3 * m[1:-2] - m[:-3]
    W = 32
    nw = n // W
    e = np.sqrt((d3[: nw * W].reshape(nw, W) ** 2).mean(axis=1))
    level = np.sqrt((m[: nw * W].reshape(nw, W) ** 2).mean(axis=1))
    K = 32
    clicks = []
    last = -10
    for w in range(nw):
        if not (lo <= w * W < hi) or e[w] < floor:
            continue
        nb_ = np.concatenate([e[max(0, w - K):max(0, w - 2)], e[w + 3:w + K]])
        med = np.median(nb_) if nb_.size else 0.0
        r = e[w] / max(med, 1e-7)
        if r >= ratio:
            if w - last > 4:
                clicks.append((w * W / fs, (w * W) // 128, r, e[w], level[w]))
            elif clicks and r > clicks[-1][2]:
                clicks[-1] = (clicks[-1][0], clicks[-1][1], r, e[w], level[w])
            last = w
    for k in jumps:
        print("jump  %8.4f s  block %6d  step %.3f" % (k / fs, k // 128, d1[k - 1]))
    for b, k in holes:
        print("hole  %8.4f s  block %6d  %d zero blocks (%.1f ms) between sounding blocks" % (b * 128 / fs, b, k,
                                                                                            k * 128000.0 / fs))
    for t, b, r, ev, lv in clicks:
        print("click %8.4f s  block %6d  x%-6.0f d3 rms %.4f  level %.3f" % (t, b, r, ev, lv))
    peak = np.abs(x[lo:hi]).max() if hi > lo else 0
    print("wavclicks: %s: %.2f s, peak %.3f: %d jumps > 0.5 FS, %d silent holes mid-sound, %d clicks (x%.0f over "
          "the neighbourhood, floor %.4f)" % (path.split("/")[-1], n / fs, peak, len(jumps), len(holes), len(clicks),
                                               ratio, floor))
    return 0


if __name__ == "__main__":
    sys.exit(main())
