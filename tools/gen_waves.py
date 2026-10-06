#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Synthesize Felucca's own sample library (no third-party material).

Writes 16-bit mono WAVs with 'smpl' loop chunks to the output directory:
  single-cycle waves  additive synthesis, one 256-sample cycle, looped
  drum one-shots      kick / snare / hats / clap / toms / rim / cowbell
  pluck               Karplus-Strong strings at three pitches (one-shots)
Everything here is generated from code in this file and is part of Felucca.
"""
import math
import random
import struct
import sys
from pathlib import Path

SR = 32000


def write_wav(path, samples, sr, loop=None, root=60):
    data = b"".join(struct.pack("<h", max(-32768, min(32767, int(v)))) for v in samples)
    fmt = struct.pack("<HHIIHH", 1, 1, sr, sr * 2, 2, 16)
    chunks = b"fmt " + struct.pack("<I", len(fmt)) + fmt
    if loop:
        smpl = struct.pack("<9I", 0, 0, int(1e9 / sr), root, 0, 0, 0, 1, 0)
        smpl += struct.pack("<6I", 0, 0, loop[0], loop[1], 0, 0)
        chunks += b"smpl" + struct.pack("<I", len(smpl)) + smpl
    chunks += b"data" + struct.pack("<I", len(data)) + data
    path.write_bytes(b"RIFF" + struct.pack("<I", 4 + len(chunks)) + b"WAVE" + chunks)


def norm(s, peak=30000):
    m = max(1e-9, max(abs(v) for v in s))
    return [v * peak / m for v in s]


def cycle(partials, n=256):
    return norm([sum(a * math.sin(2 * math.pi * k * i / n + ph) for k, a, ph in partials) for i in range(n)])


WAVES = {
    "ORGAN": [(1, 1.0, 0), (2, 0.8, 0), (3, 0.6, 0), (4, 0.35, 0), (6, 0.2, 0), (8, 0.15, 0)],
    "REED": [(k, 1.0 / k, 0) for k in range(1, 24, 2)],
    "BRASS": [(k, (1.0 / k) * (1.4 if 3 <= k <= 6 else 1.0), 0) for k in range(1, 28)],
    "GLASS": [(1, 0.6, 0), (5, 0.5, 0.3), (9, 0.35, 0.7), (13, 0.25, 1.1), (17, 0.18, 0.2)],
    "VOX": [(k, math.exp(-((k - 3) ** 2) / 4) + 0.7 * math.exp(-((k - 9) ** 2) / 6) + 0.05, 0) for k in range(1, 20)],
}


def digi(seed):
    r = random.Random(seed)
    return [(k, r.random() / math.sqrt(k), r.random() * 6.28) for k in range(1, 32)]


def drum_kick():
    out, ph = [], 0.0
    for i in range(int(0.45 * SR)):
        t = i / SR
        f = 45 + 110 * math.exp(-t * 30)
        ph += 2 * math.pi * f / SR
        out.append(math.sin(ph) * math.exp(-t * 7) + (random.uniform(-1, 1) * math.exp(-t * 400) * 0.3))
    return out


def drum_snare():
    out, ph1, ph2, lp = [], 0.0, 0.0, 0.0
    for i in range(int(0.3 * SR)):
        t = i / SR
        ph1 += 2 * math.pi * 185 / SR
        ph2 += 2 * math.pi * 330 / SR
        n = random.uniform(-1, 1)
        lp += 0.5 * (n - lp)
        out.append(0.5 * (math.sin(ph1) + 0.6 * math.sin(ph2)) * math.exp(-t * 25) + (n - lp) * math.exp(-t * 14))
    return out


def drum_hat(decay):
    out, hp = [], 0.0
    for i in range(int(decay * 6 * SR)):
        t = i / SR
        n = random.uniform(-1, 1)
        hp += 0.2 * (n - hp)
        out.append((n - hp) * math.exp(-t / decay))
    return out


def drum_clap():
    out, bp1, bp2 = [], 0.0, 0.0
    for i in range(int(0.35 * SR)):
        t = i / SR
        burst = 1.0 if (t < 0.03 and (t * 1000) % 10 < 2.5) else math.exp(-(t - 0.03) * 18) if t >= 0.03 else 0.0
        n = random.uniform(-1, 1)
        bp1 += 0.3 * (n - bp1)
        bp2 += 0.3 * (bp1 - bp2)
        out.append((bp1 - bp2) * burst)
    return out


def drum_tom(f0):
    out, ph = [], 0.0
    for i in range(int(0.5 * SR)):
        t = i / SR
        ph += 2 * math.pi * (f0 + f0 * 0.6 * math.exp(-t * 20)) / SR
        out.append(math.sin(ph) * math.exp(-t * 7))
    return out


def drum_rim():
    out = []
    for i in range(int(0.08 * SR)):
        t = i / SR
        out.append((math.sin(2 * math.pi * 1700 * t) + 0.6 * math.sin(2 * math.pi * 520 * t)) * math.exp(-t * 70))
    return out


def drum_cowbell():
    out = []
    for i in range(int(0.4 * SR)):
        t = i / SR
        sq = lambda f: 1.0 if math.sin(2 * math.pi * f * t) > 0 else -1.0
        out.append((sq(562) + sq(845)) * 0.5 * math.exp(-t * 9))
    return out


def drum_metal_hat(decay):
    """metallic hat: six detuned squares + a little noise, band-passed, short"""
    fr = [205.3, 304.4, 369.6, 522.7, 540.0, 800.0]
    out, ph, lp, hp = [], [0.0] * 6, 0.0, 0.0
    for i in range(int(decay * 5 * SR)):
        t = i / SR
        m = 0.0
        for k in range(6):
            ph[k] += fr[k] * 3.0 / SR
            m += 1.0 if (ph[k] % 1.0) < 0.5 else -1.0
        m /= 6
        x = 0.7 * m + 0.3 * random.uniform(-1, 1)
        hp += 0.35 * (x - hp)
        y = x - hp                                   # high-pass
        lp += 0.6 * (y - lp)                         # tame the top
        out.append(lp * math.exp(-t / decay) * (1 if t > 0.001 else t / 0.001))
    return out


def drum_cymbal(decay, bell):
    """noise + six inharmonic square partials (those of drum_metal_hat), high-passed"""
    fr = [205.3, 304.4, 369.6, 522.7, 540.0, 800.0]
    out, hp, hp2, ph = [], 0.0, 0.0, [0.0] * 6
    for i in range(int(decay * 4 * SR)):
        t = i / SR
        m = 0.0
        for k in range(6):
            ph[k] += fr[k] * (2.2 if bell else 3.3) / SR
            m += 1.0 if (ph[k] % 1.0) < 0.5 else -1.0
        x = 0.55 * m / 6 + 0.45 * random.uniform(-1, 1)
        hp += 0.25 * (x - hp)
        hp2 += 0.25 * ((x - hp) - hp2)
        y = (x - hp) - hp2
        env = math.exp(-t / decay) * (1 if t > 0.002 else t / 0.002)
        if bell:
            env += 0.6 * math.exp(-t / 0.08)
        out.append(y * env)
    return out


def pluck(freq, secs=1.6):
    n = int(SR / freq)
    r = random.Random(int(freq))
    buf = [r.uniform(-1, 1) for _ in range(n)]
    out, i = [], 0
    for _ in range(int(secs * SR)):
        a = buf[i % n]
        b = buf[(i + 1) % n]
        buf[i % n] = 0.497 * (a + b)
        out.append(a)
        i += 1
    return out


def main(outdir):
    out = Path(outdir)
    out.mkdir(parents=True, exist_ok=True)
    random.seed(1)
    for name, parts in WAVES.items():
        write_wav(out / f"W {name}.wav", cycle(parts), SR, loop=(0, 255))
    for k in range(3):
        write_wav(out / f"W DIGI{k + 1}.wav", cycle(digi(k + 7)), SR, loop=(0, 255))
    kit = [("KICK", drum_kick()), ("SNARE", drum_snare()), ("CHAT", drum_metal_hat(0.045)),
           ("OHAT", drum_metal_hat(0.28)),
           ("CLAP", drum_clap()), ("TOM LO", drum_tom(95)), ("TOM HI", drum_tom(150)), ("RIM", drum_rim()),
           ("COWBELL", drum_cowbell()), ("CRASH", drum_cymbal(0.25, False)), ("RIDE", drum_cymbal(0.3, True))]
    for name, s in kit:
        write_wav(out / f"D {name}.wav", norm(s), SR)
    for note, f in (("C2", 65.41), ("C3", 130.81), ("C4", 261.63)):    # note names: C3 = MIDI 60
        write_wav(out / f"P PLUCK {note}.wav", norm(pluck(f * 2)), SR)
    print(f"generated waves into {out}")


if __name__ == "__main__":
    main(sys.argv[1])
