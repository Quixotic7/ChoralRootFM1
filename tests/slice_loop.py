#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""A user-style drum loop for the SLICE AUTO test (tests/slice_test.c): two bars at 96 BPM with a
swing, played by Felucca's generated drums (build/genwav, tools/gen_waves.py) at
44.1 kHz with a little noise, not on the BREAK's grid or tempo. Writes OUT.wav and OUT.hits (one
line per onset: seconds).
  tests/slice_loop.py OUT"""
import random
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import sampleio as sio  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
GEN = ROOT / "build" / "genwav"
SR, BPM, SWING = 44100, 96, 0.16          # odd 16ths late by 16 % of a 16th
# (16th of the two bars, sound, gain)
HITS = [(0, "KICK", 1.0), (0, "CHAT", 0.4), (2, "CHAT", 0.3), (4, "SNARE", 0.9), (4, "CHAT", 0.35),
        (6, "KICK", 0.8), (8, "CHAT", 0.4), (9, "RIM", 0.5), (10, "KICK", 0.85), (12, "SNARE", 0.95),
        (14, "OHAT", 0.35), (16, "KICK", 1.0), (16, "CHAT", 0.4), (19, "KICK", 0.7), (20, "SNARE", 0.9),
        (22, "CHAT", 0.3), (24, "CHAT", 0.4), (26, "KICK", 0.85), (27, "KICK", 0.6), (28, "SNARE", 1.0),
        (28, "CLAP", 0.6), (29, "TOM HI", 0.6), (30, "TOM LO", 0.7), (31, "SNARE", 0.3)]


def main(out):
    six = 60 / BPM / 4
    n = int(32 * six * SR)
    x = [0.0] * n
    rnd = random.Random(7)
    times = {}
    for step, name, gain in HITS:
        t = step * six + (SWING * six if step % 2 else 0)
        sr, s = sio.read_any_wav(GEN / f"D {name}.wav")
        s = sio.resample(s, sr, SR)
        i0 = int(round(t * SR))
        for i, v in enumerate(s):
            if i0 + i < n:
                x[i0 + i] += v * gain
        times[i0] = t
    pk = max(abs(v) for v in x)
    pcm = [max(-32768, min(32767, int(v / pk * 28000 + rnd.uniform(-60, 60)))) for v in x]
    w = Path(out + ".wav")
    data = struct.pack(f"<{n}h", *pcm)
    w.write_bytes(b"RIFF" + struct.pack("<I", 36 + len(data)) + b"WAVEfmt " +
                  struct.pack("<IHHIIHH", 16, 1, 1, SR, SR * 2, 2, 16) + b"data" + struct.pack("<I", len(data)) + data)
    Path(out + ".hits").write_text("".join(f"{times[k]:.6f}\n" for k in sorted(times)))
    print(f"slice_loop: {out}.wav, {n / SR:.2f} s, {len(times)} onsets")


if __name__ == "__main__":
    main(sys.argv[1])
