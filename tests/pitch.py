#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""YIN pitch of hostsim renders (4000 samples from 0.25 s, the held note).

  tests/pitch.py build/host/render.wav ...
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from sampleio import read_any_wav, yin  # noqa: E402

for f in sys.argv[1:]:
    sr, x = read_any_wav(Path(f))
    a = int(0.25 * sr)
    print(f"{f}: {yin(x[a:a + 4000], sr, interpolate=False):.1f} Hz")
