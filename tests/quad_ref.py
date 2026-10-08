#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca)
"""QUAD (FM TONE: firmware/src/eng_quad.c, docs/QUAD.md): the table generator and a reference model of the voice.

  python3 tests/quad_ref.py          # writes firmware/src/quad_tables.h and tests/quad_goldens/*.json

The tables: the 27 HARM waves (sine + the 26-wave series of the Digitone manual's Appendix A.6, additive, 512 points +
the wrap point, int16, flash), the ratio tables (C / B steps, A steps, the version-1 pair's BR), the value-name lists
of the ratio and offset columns, the base-width filter's one-pole coefficients, the DTUN curve, the LFO rate constant.

The model: pure Python (no numpy), the same equations as eng_quad.c. The control path (phase increments, the four
envelopes, the LFOs, the levels (B LEV law, key scaling, velocity), the filter coefficients) is integer, as the
firmware's; the signal path (the operators, feedback, the mix, the DC blocker, the base-width filter, the SVF, the
amplitude) is float. The goldens: 0.3 s of two notes (C4 and G4, velocity 100, released at 0.2 s) on eight test patches
(one per algorithm; FDBK 14: the feedback of a HARM-shaped operator (the odd algorithms) is chaotic enough above
~25 that the model's float and the firmware's integer part ways) and four presets: the first 2048 output samples and the RMS of each 10 ms block.
tests/cr_quad_test.c renders the same and compares. Needs build/gen/felucca_tables.h (Felucca's tables).
"""
import json
import math
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TABLES = os.path.join(ROOT, "build", "gen", "felucca_tables.h")
OUT_H = os.path.join(ROOT, "firmware", "src", "quad_tables.h")
GOLD = os.path.join(ROOT, "tests", "quad_goldens")
FS = 44100
CTL = 32
M32 = 0xFFFFFFFF


# ------------------------------------------------------------------ tables
def felucca_table(name):
    src = open(TABLES).read()
    m = re.search(r"\b%s\[\d*\]\s*=\s*\{(.*?)\};" % name, src, re.S)
    if not m:
        sys.exit("quad_ref: %s not in %s (build the generated tables first)" % (name, TABLES))
    return [int(x) for x in re.findall(r"-?\d+", m.group(1))]


SINE = felucca_table("SINE")
PITCH_OCT = felucca_table("PITCH_OCT")
ENV_LIN = felucca_table("ENV_LIN")
ENV_EXP = felucca_table("ENV_EXP")
SVF_G = felucca_table("SVF_G")
TANH_Q15 = felucca_table("TANH_Q15")

# the ratio steps: C, B1 and B2 0.25 0.5 0.75 then 1..16 (19); A 0.25..16 in 0.25 (64); RATIO B (the pair) = B1 index
# x 19 + B2 index (B2 the fast hand, the Digitone's watch: B2 runs through its steps, then B1 steps on: 361 pairs).
# BR: the version-1 blob's B2 / B1 (its pair was BR index x 19 + B1 index), for its loader
RCB = [0.25, 0.5, 0.75] + [float(n) for n in range(1, 17)]
RA = [0.25 * (i + 1) for i in range(64)]
BR = [0.5, 1.0, 1.5, 2.0, 3.0, 4.0]
NRB = len(RCB) * len(RCB)
NRB_V1 = len(RCB) * len(BR)
RB_DEF = 3 * len(RCB) + 3               # 1.00 / 1.00
# LFO: inc a control tick = SPEED x MULT x BPM x LFO_K >> 16: f = SPEED x MULT / 128 x BPM / 240 Hz (128 = a bar)
LFO_K = int(round(2 ** 32 * CTL / (128 * 240 * FS) * 65536))
HN = 512                                # HARM table points (+ the wrap point)


def q16(r):
    return int(round(r * 65536))


# HARM: the 26 waves of the Digitone manual's Appendix A.6 figure (its rows: saw build-up 7, saw reduction 6, odd/even
# mix 1, square build-up 5, square reduction 4, bell 3), each a list of (harmonic, amplitude); sine phases, the
# fundamental at 1, scaled to a peak of 32767 per wave. Our own recipes, drawn to match the figure's shapes.
def harm_recipes():
    R = []
    for n in (2, 3, 4, 6, 8, 11, 16):                       # 1..7 saw build-up: harmonics 1..n at 1 / h
        R.append([(h, 1.0 / h) for h in range(1, n + 1)])
    for k in range(1, 7):                                   # 8..13 saw reduction: the 16-partial saw losing its low
        g = 1.0 - k / 7.0                                   # partials 2..k+1, the rest fading toward a sine
        R.append([(1, 1.0)] + [(h, g / h) for h in range(k + 2, 17)])
    R.append([(h, (1.0 if h % 2 else 0.35) / h) for h in range(1, 16)])   # 14 odd / even mix: evens at 0.35
    for n in (3, 5, 7, 11, 17):                             # 15..19 square build-up: odd harmonics up to n
        R.append([(h, 1.0 / h) for h in range(1, n + 1, 2)])
    for k in range(1, 5):                                   # 20..23 square reduction: the 17-partial square losing
        g = 1.0 - k / 5.0                                   # its low odd partials 3..2k+1, the rest fading
        R.append([(1, 1.0)] + [(h, g / h) for h in range(2 * k + 3, 18, 2)])
    R.append([(1, 1.0), (3, 0.5), (4, 0.35), (7, 0.25), (10, 0.15)])                  # 24..26 bells
    R.append([(1, 1.0), (2, 0.3), (5, 0.5), (9, 0.35), (13, 0.2)])
    R.append([(1, 1.0), (4, 0.6), (6, 0.45), (11, 0.35), (14, 0.25), (19, 0.15)])
    assert len(R) == 26
    return R


HARM_R = harm_recipes()


def harm_tables():
    """27 tables of HN + 1 points (the last = the first): 0 the sine, 1..26 the recipes"""
    out = []
    for rec in [[(1, 1.0)]] + HARM_R:
        fine = [sum(a * math.sin(2 * math.pi * h * i / (HN * 16)) for h, a in rec) for i in range(HN * 16)]
        pk = max(abs(x) for x in fine)
        tab = [int(round(32767 * sum(a * math.sin(2 * math.pi * h * i / HN) for h, a in rec) / pk)) for i in range(HN)]
        tab = [max(-32767, min(32767, x)) for x in tab]
        out.append(tab + [tab[0]])
    return out


HARM = harm_tables()


# DTUN: A up, B2 down by c(d) cents: 0..64 linear to 6 cents, 64..127 quadratic to 50 cents (the Digitone: "very
# slight" up to ~64, much more above); Q16 factor - 1
def dtun_cents(d):
    return 6.0 * d / 64.0 if d <= 64 else 6.0 + 44.0 * ((d - 64) / 63.0) ** 2


DT_UP = [int(round(65536 * (2 ** (dtun_cents(d) / 1200.0) - 1))) for d in range(128)]
DT_DN = [int(round(65536 * (2 ** (-dtun_cents(d) / 1200.0) - 1))) for d in range(128)]


def cutoff_hz(v):
    return 30.0 * ((16000 / 30) ** (v / 127))


DC_K = 75 / 65536.0     # eng_quad.c QUAD_DC_K: the DC blocker after the operators, a one-pole high-pass at ~8 Hz
BW_K = [min(65535, int(round(65536 * (1 - math.exp(-2 * math.pi * cutoff_hz(v) / FS))))) for v in range(128)]

MULT_N = ["1", "2", "4", "8", "16", "32", "64", "128", "256", "512", "1k", "2k"]


def rtxt(r):
    return "%.2f" % r


def write_header():
    L = ["/* generated by tests/quad_ref.py: do not edit (python3 tests/quad_ref.py) */",
         "/* QUAD (eng_quad.c, docs/QUAD.md): the HARM waves, the ratio tables, the value names, DTUN, the LFO rate */",
         "#pragma once",
         "#include <stdint.h>",
         "#define QUAD_NRCB %du                /* RATIO C (and B1, B2): 0.25 0.5 0.75 1 2 .. 16 */" % len(RCB),
         "#define QUAD_NRA %du                 /* RATIO A: 0.25 .. 16 in 0.25 */" % len(RA),
         "#define QUAD_NBR %du                  /* the version-1 blob's B2 / B1 (its loader) */" % len(BR),
         "#define QUAD_NRB_V1 %du             /* the version-1 blob's RATIO B: BR index x QUAD_NRCB + B1 index */" % NRB_V1,
         "#define QUAD_NRB %du                /* RATIO B: B1 index x QUAD_NRCB + B2 index (B2 the fast hand) */" % NRB,
         "#define QUAD_RB_DEF %d               /* 1.00 / 1.00 */" % RB_DEF,
         "#define QUAD_LFO_K %d         /* LFO: SPEED x MULT x BPM x QUAD_LFO_K >> 16 a control tick (128 = a bar) */" % LFO_K,
         "#define QUAD_NHARM %du               /* HARM: the sine + 26 waves */" % len(HARM),
         "#define QUAD_HN %du                 /* HARM table points (+ the wrap point) */" % HN]

    def arr(name, ctype, vals, per=12):
        L.append("static const %s %s[%d] = {" % (ctype, name, len(vals)))
        for i in range(0, len(vals), per):
            L.append("    " + ", ".join(str(v) for v in vals[i:i + per]) + ",")
        L.append("};")

    def names(name, vals):
        L.append("static const char *const %s[%d] = {" % (name, len(vals) + 1))
        for i in range(0, len(vals), 8):
            L.append("    " + ", ".join('"%s"' % v for v in vals[i:i + 8]) + ",")
        L.append("    0,")
        L.append("};")

    arr("QUAD_RCB_Q16", "int32_t", [q16(r) for r in RCB], 10)
    arr("QUAD_RA_Q16", "int32_t", [q16(r) for r in RA], 10)
    arr("QUAD_BR_Q16", "int32_t", [q16(r) for r in BR], 10)
    L.append("/* the value names (F_INT columns with a 0-terminated name list: value v - min names it) */")
    names("N_QUAD_RCB", [rtxt(r) for r in RCB])
    names("N_QUAD_RA", [rtxt(r) for r in RA])
    names("N_QUAD_OFS", [("+" if o >= 0 else "-") + "%d.%02d" % (abs(o) // 100, abs(o) % 100) for o in range(-100, 101)])
    L.append("/* the base-width filter: a one-pole's coefficient (Q16) at 30 Hz x (16000 / 30)^(v / 127) (CUTOFF_HZ's scale) */")
    arr("QUAD_BW_K", "uint16_t", BW_K)
    L.append("/* DTUN d: A x (1 + QUAD_DT_UP[d] / 65536), B2 x (1 + QUAD_DT_DN[d] / 65536): 0..64 -> 0..6 cents, 64..127")
    L.append(" * -> 6..50 cents (quadratic) */")
    arr("QUAD_DT_UP", "int16_t", DT_UP, 16)
    arr("QUAD_DT_DN", "int16_t", DT_DN, 16)
    L.append("/* HARM: 0 the sine, 1..26 the Digitone's series (docs/QUAD.md \"HARM\": saw build-up 1..7, saw reduction")
    L.append(" * 8..13, odd/even 14, square build-up 15..19, square reduction 20..23, bell 24..26), additive, peak 32767;")
    L.append(" * %d points and the first again (no wrap in the interpolation). Flash (XIP): %.1f KB */" %
             (HN, len(HARM) * (HN + 1) * 2 / 1024.0))
    L.append("static const int16_t QUAD_HARM[%d][%d] = {" % (len(HARM), HN + 1))
    for t in HARM:
        L.append("    {")
        for i in range(0, HN + 1, 16):
            L.append("        " + ", ".join(str(v) for v in t[i:i + 16]) + ",")
        L.append("    },")
    L.append("};")
    with open(OUT_H, "w") as f:
        f.write("\n".join(L) + "\n")


# ------------------------------------------------------------- the patch
LFO_F = ["SPEED", "MULT", "FADE", "DEST", "WAVE", "PHASE", "TRIG", "DEPTH"]
NAMES = ["ALGO", "RC", "RA", "RB1", "HARM", "DTUN", "FDBK", "MIX", "OFSC", "OFSA", "OFSB1", "OFSB2",
         "AATK", "ADEC", "AEND", "ALEV", "BATK", "BDEC", "BEND", "BLEV", "ADLY", "ATRIG", "ARST", "PHRT",
         "BDLY", "BTRIG", "BRST", "VEL", "AKEY", "B1KEY", "FATK", "FDEC", "FSUS", "FREL", "FREQ", "RESO", "FTYPE",
         "FDEPTH", "FDLY", "FKTRK", "BASE", "WIDTH", "EATK", "EDEC", "ESUS", "EREL", "LEVEL"]
NAMES += ["L%d%s" % (k, f) for k in range(3) for f in LFO_F]
NAMES += ["RB2", "B2KEY"]               # (version 2: B2's step after the LFOs; version 3: B2 KEY)
Q = {n: i for i, n in enumerate(NAMES)}
NP = len(NAMES)
assert NP == 73
# (min, max, def), as eng_quad.c's page columns
RNG = {"ALGO": (1, 8, 1), "RC": (0, 18, 3), "RA": (0, 63, 3), "RB1": (0, 18, 3), "RB2": (0, 18, 3), "HARM": (-26, 26, 0),
       "DTUN": (0, 127, 0), "FDBK": (0, 120, 0), "MIX": (-64, 63, 0), "OFSC": (-100, 100, 0), "OFSA": (-100, 100, 0),
       "OFSB1": (-100, 100, 0), "OFSB2": (-100, 100, 0), "AATK": (0, 127, 0), "ADEC": (0, 127, 60),
       "AEND": (0, 127, 64), "ALEV": (0, 127, 48), "BATK": (0, 127, 0), "BDEC": (0, 127, 60), "BEND": (0, 127, 0),
       "BLEV": (0, 127, 0), "ADLY": (0, 127, 0), "ATRIG": (0, 1, 1), "ARST": (0, 1, 1), "PHRT": (0, 4, 1),
       "BDLY": (0, 127, 0), "BTRIG": (0, 1, 1), "BRST": (0, 1, 1), "VEL": (0, 127, 64), "AKEY": (0, 127, 0),
       "B1KEY": (0, 127, 0), "B2KEY": (0, 127, 0), "FATK": (0, 127, 0), "FDEC": (0, 127, 64), "FSUS": (0, 127, 0),
       "FREL": (0, 127, 40), "FREQ": (0, 127, 127), "RESO": (0, 127, 0), "FTYPE": (0, 3, 1), "FDEPTH": (-64, 63, 0),
       "FDLY": (0, 127, 0), "FKTRK": (0, 127, 0), "BASE": (0, 127, 0), "WIDTH": (0, 127, 127), "EATK": (0, 127, 0),
       "EDEC": (0, 127, 64), "ESUS": (0, 127, 127), "EREL": (0, 127, 40), "LEVEL": (0, 127, 100)}
for _k in range(3):
    RNG.update({"L%dSPEED" % _k: (-64, 63, 16), "L%dMULT" % _k: (0, 23, 3), "L%dFADE" % _k: (-64, 63, 0),
                "L%dDEST" % _k: (0, 39, 0), "L%dWAVE" % _k: (0, 6, 0), "L%dPHASE" % _k: (0, 127, 0),
                "L%dTRIG" % _k: (0, 4, 0), "L%dDEPTH" % _k: (-64, 63, 0)})
FT = {"OFF": 0, "LP12": 1, "HP12": 2, "LP24": 3}
WAVE = {"TRI": 0, "SINE": 1, "SQR": 2, "SAW": 3, "RAMP": 4, "EXP": 5, "RAND": 6}
TRIG = {"FREE": 0, "TRIG": 1, "HOLD": 2, "ONE": 3, "HALF": 4}
DEST_N = ["NONE", "PITCH", "PAB2", "ALGO", "RC", "RA", "RB", "OFSC", "OFSA", "OFSB1", "OFSB2", "HARM", "DTUN", "FDBK",
          "MIX", "ALEV", "BLEV", "AATK", "ADEC", "AEND", "BATK", "BDEC", "BEND", "ADLY", "BDLY", "FREQ", "RESO", "FENV",
          "BASE", "WIDTH", "FATK", "FDEC", "FSUS", "FREL", "EATK", "EDEC", "ESUS", "EREL", "LEVEL", "PAN"]
DEST = {n: i for i, n in enumerate(DEST_N)}
ND = len(DEST_N)
assert ND == 40
# the algorithms' feedback operator (0 C, 1 A, 2 B1, 3 B2)
FBOP = [1, 3, 1, 3, 2, 1, 1, 2]


def init_patch():
    return [RNG[n][2] for n in NAMES]


def QRC(q):
    return q - 1 if q <= 4 else q // 4 + 2


def patch_of(**kw):
    p = init_patch()
    for k, v in kw.items():
        lo, hi, _ = RNG[k]
        p[Q[k]] = max(lo, min(hi, v))
    return p


def syn(al, rc, ra, b1, b2, h, dt, fb, mx):
    return dict(ALGO=al, RC=rc, RA=ra, RB1=b1, RB2=b2, HARM=h, DTUN=dt, FDBK=fb, MIX=mx)


def env(x, a, d, e, l):
    return {x + "ATK": a, x + "DEC": d, x + "END": e, x + "LEV": l}


def amp(a, d, s, r, l):
    return dict(EATK=a, EDEC=d, ESUS=s, EREL=r, LEVEL=l)


def flt(ty, f, r, dp):
    return dict(FTYPE=FT[ty], FREQ=f, RESO=r, FDEPTH=dp)


def fenv(a, d, s, r):
    return dict(FATK=a, FDEC=d, FSUS=s, FREL=r)


def lfo(k, sp, mu, de, w, dp, tr="FREE"):
    return {"L%dSPEED" % k: sp, "L%dMULT" % k: mu, "L%dDEST" % k: DEST[de], "L%dWAVE" % k: WAVE[w],
            "L%dDEPTH" % k: dp, "L%dTRIG" % k: TRIG[tr]}


def merge(*ds):
    r = {}
    for d in ds:
        r.update(d)
    return r


# four of eng_quad.c's presets (the C test checks these patches against its own: keep them in step)
PRESETS = {
    "EP": patch_of(**merge(syn(2, QRC(4), 3, QRC(4), QRC(56), 0, 12, 0, -30), env("A", 0, 78, 20, 66),
                           env("B", 0, 52, 0, 62), dict(VEL=100, AKEY=40, B1KEY=30, B2KEY=30, PHRT=1),
                           amp(0, 96, 40, 62, 72))),
    "BASS": patch_of(**merge(syn(1, QRC(4), 3, QRC(4), QRC(4), 0, 0, 24, -64), env("A", 0, 62, 34, 88),
                             env("B", 0, 50, 0, 0), flt("LP12", 92, 10, 20), fenv(0, 60, 0, 40),
                             amp(0, 80, 100, 30, 110))),
    "GLASS PAD": patch_of(**merge(syn(7, QRC(4), 7, QRC(16), QRC(16), 8, 40, 0, 0), env("A", 70, 90, 100, 36),
                                  env("B", 80, 96, 60, 50), dict(PHRT=0), amp(70, 90, 120, 100, 88),
                                  lfo(0, 8, 3, "HARM", "SINE", 30))),
    "WOBBLE": patch_of(**merge(syn(1, QRC(4), 3, QRC(4), QRC(4), 0, 0, 10, -64), env("A", 0, 64, 127, 60),
                               env("B", 0, 60, 40, 0), flt("LP24", 70, 60, 0), amp(0, 80, 110, 40, 115),
                               lfo(0, 32, 4, "ALEV", "TRI", 50, "TRIG"), lfo(1, 32, 4, "FREQ", "TRI", 40, "TRIG"))),
}


def algo_patch(al):
    """a test patch for algorithm al: every part of the voice moving (both envelopes, HARM (on C for the even, on A /
    B1 for the odd algorithms), detune, feedback, B LEV between its points, key scaling, the filter and its envelope
    (a type per algorithm), the base-width filter, an LFO on MIX)"""
    return patch_of(**merge(syn(al, QRC(4), 7, QRC(4), QRC(8), 9 if al % 2 else -10, 70, 14, 20),
                            env("A", 0, 60, 40, 80), env("B", 5, 70, 30, 70), flt(["LP12", "HP12", "LP24", "OFF"][al % 4],
                                                                                  100 if al % 4 != 2 else 75, 30, 20),
                            fenv(0, 60, 40, 40), dict(BASE=10, WIDTH=100, OFSB2=3, AKEY=40, B1KEY=70, B2KEY=20),
                            amp(2, 70, 100, 40, 100), lfo(0, 20, 4, "MIX", "TRI", 30, "TRIG")))


# --------------------------------------------------------------- the model
def mulq15(a, b):
    return (a * b) >> 15


def mulq16(a, k):
    return ((a >> 16) * k) + (((a & 0xFFFF) * k) >> 16)


def clamp(v, lo, hi):
    return lo if v < lo else hi if v > hi else v


def cdiv(a, b):
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


def pitch_inc(p):
    i = p + 64
    o = (i * 21846) >> 22
    return PITCH_OCT[i - o * 192] >> (10 - o)


def sine_int(ph):
    i = ph >> 22
    a, b = SINE[i], SINE[(i + 1) & 1023]
    return a + (((b - a) * ((ph >> 7) & 0x7FFF)) >> 15)


def osc_tri(ph):
    s = (ph & M32) >> 15
    return s - 32768 if s < 65536 else 98303 - s


def xs(s):
    s &= M32
    s = s if s else 0x6C8E9CF5
    s ^= (s << 13) & M32
    s ^= s >> 17
    s ^= (s << 5) & M32
    return s


def exp2q(x):
    """eng_quad.c quad_exp2: 2^(x / 4096) in Q15 (x Q12 octaves), the fraction by a quadratic (within 0.3 %)"""
    ip, f = x >> 12, x & 4095
    m = (4096 + ((f * (2689 + ((f * 1407) >> 12))) >> 12)) << 3
    if ip >= 0:
        return m << ip if ip < 15 else 1 << 30
    return m >> -ip if -ip < 31 else 0


def fbq_of(f):
    """FDBK f (0..120) -> the feedback multiplier: beta = 2 pi fbq / 16384 rad on the average of the last two outputs,
    1.9 rad at 35 (the saw: h2 / h1 0.47, h3 / h1 0.29), 7.5 at 120"""
    return (f * 4237 + ((f * f * 537) >> 6)) >> 5


def blev(v):
    """the B LEV law (the Digitone manual's graph): v 0..127 -> B1, B2 in Q12 (0..4096)"""
    if v <= 43:
        return v * 4096 // 43, 0
    if v < 85:
        return 4096 - (v - 43) * 3686 // 42, (v - 43) * 4096 // 42
    return 410 + (v - 85) * 3686 // 42, 4096


def lev_q15(u):
    return min(32767, (u * u) >> 9)


def tab_f(t, ph, n):
    """table t of n points at the (float) phase ph, linearly interpolated (the float side of sine_i / quad_hw)"""
    x = (ph % 4294967296.0) / (4294967296.0 / n)
    i = int(x)
    if i > n - 1:
        i = n - 1
    f = x - i
    a = t[i]
    return a + (t[i + 1 if i + 1 < len(t) else 0] - a) * f


def softclip_f(x):
    a = abs(x)
    i = int(a) >> 8
    if i >= 256:
        y = TANH_Q15[256]
    else:
        y = TANH_Q15[i] + (TANH_Q15[i + 1] - TANH_Q15[i]) * (a - 256 * i) / 256.0
    return -y if x < 0 else y


def soft_knee_f(y, k):
    a = abs(y)
    if a <= k:
        return y
    a = k + softclip_f((a - k) * 2) / 2.0
    return -a if y < 0 else a


def lfo_inc(p, k, bpm=120):
    mu = p[Q["L%dMULT" % k]]
    b = bpm if mu < 12 else 120
    v = (p[Q["L%dSPEED" % k]] * (1 << (mu % 12)) * b * LFO_K) >> 16
    return clamp(v, -0x7FFFFFFF, 0x7FFFFFFF)


def lfo_wave(w, ph, r):
    ph &= M32
    if w == 1:
        return sine_int(ph)
    if w == 2:
        return 32767 if ph < 0x80000000 else -32767
    if w == 3:
        return (((ph + 0x80000000) & M32) >> 16) - 32768
    if w == 4:
        return 32767 - (ph >> 17)
    if w == 5:
        u = 32767 - (ph >> 17)
        u = (u * u) >> 15
        return (u * u) >> 15
    if w == 6:
        return r
    return osc_tri((ph + 0x40000000) & M32)


def tsvf_coef_k(cut, k):
    cut = clamp(cut, 0, 127 << 8)
    i = cut >> 8
    g = SVF_G[i]
    if i < 127:
        g += ((SVF_G[i + 1] - g) * (cut & 255)) >> 8
    den = 4096 + ((g * (g + k)) >> 12)
    a1 = (4096 << 13) // den
    a2 = (a1 * g) >> 12
    a3 = (a2 * g) >> 12
    return a1, a2, a3


PHRT_MASK = [0, 0xF, 0x1, 0xE, 0xA]


class Part:
    def __init__(self, p):
        self.p = p
        self.lph = [0, 0, 0]
        self.lrnd = [0, 0, 0]

    def block(self):
        for k in range(3):
            old = self.lph[k]
            inc = lfo_inc(self.p, k)
            self.lph[k] = (old + inc) & M32
            if (inc > 0 and self.lph[k] < old) or (inc < 0 and self.lph[k] > old):
                self.lrnd[k] = xs(self.lrnd[k] + k)


class Voice:
    def __init__(self, part, idx):
        self.part = part
        self.idx = idx
        self.ph = [0.0] * 4
        self.fb1 = self.fb2 = 0.0
        self.env = [0] * 4
        self.dly = [0] * 4
        self.stage = [0] * 4
        self.lv = [0, 0, 0]
        self.amp = 0
        self.f = [0.0] * 4
        self.bh = self.bl = 0.0
        self.dc = 0.0
        self.lph = [0] * 3
        self.lrnd = [0] * 3
        self.ltr = [0] * 3
        self.lsl = [0] * 3
        self.lstop = 0
        self.ticks = 0
        self.live = 0
        self.gate = 0
        self.active = 0
        self.note = 60
        self.vel = 100
        self.env_out = 0
        self.venv = 0

    def env_start(self, k, d, reset):
        if reset:
            self.env[k] = 0
        self.dly[k] = 0
        self.stage[k] = 1 if d else 2

    def note_on(self, note, vel):
        p = self.part.p
        sounding = self.active
        self.note, self.vel = note, vel
        self.gate = self.active = 1
        if not sounding:
            self.venv = self.env_out = 0
        fresh = (not self.venv and not self.env_out) or not self.live
        mask = PHRT_MASK[p[Q["PHRT"]]]
        for k in range(4):
            if (mask >> k) & 1:
                self.ph[k] = 0.0
        if fresh:
            self.env = [0] * 4
            self.fb1 = self.fb2 = 0.0
            self.lv = [0, 0, 0]
            self.amp = 0
            self.f = [0.0] * 4
            self.bh = self.bl = 0.0
            self.dc = 0.0
            self.env_start(0, p[Q["ADLY"]], 1)
            self.env_start(1, p[Q["BDLY"]], 1)
        else:
            if mask:
                self.fb1 = self.fb2 = 0.0
            if p[Q["ATRIG"]]:
                self.env_start(0, p[Q["ADLY"]], p[Q["ARST"]])
            if p[Q["BTRIG"]]:
                self.env_start(1, p[Q["BDLY"]], p[Q["BRST"]])
        self.env_start(2, p[Q["FDLY"]], fresh)
        self.env_start(3, 0, fresh)
        for k in range(3):
            tm = p[Q["L%dTRIG" % k]]
            sp = 0 if p[Q["L%dWAVE" % k]] == 6 else p[Q["L%dPHASE" % k]] << 25
            self.ltr[k] = 0
            self.lstop &= ~(1 << k)
            self.lrnd[k] = xs(self.lrnd[k] + 0x9E3779B9 * (k + 1) + self.idx)
            if tm == 2:
                self.lph[k] = (self.part.lph[k] + sp) & M32
                self.lrnd[k] = self.part.lrnd[k]
            else:
                self.lph[k] = sp
        self.ticks = 0
        self.live = 1

    def env_tick(self, k, atk, dec, sus, rel, dly, gate, adsr):
        x = self.env[k]
        if adsr and not gate and self.stage[k] and self.stage[k] < 4:
            self.stage[k] = 4
        st = self.stage[k]
        if st == 1:
            self.dly[k] += ENV_LIN[dly & 127]
            if self.dly[k] >= (1 << 24):
                self.stage[k] = 2
        elif st == 2:
            x += ENV_LIN[atk & 127]
            if x >= (1 << 24):
                x = 1 << 24
                self.stage[k] = 3
        elif st == 3:
            x += mulq16(sus - x, ENV_EXP[dec & 127])
        elif st == 4:
            x -= mulq16(x, ENV_EXP[rel & 127])
            if x < (1 << 12):
                x = 0
                self.stage[k] = 0
        else:
            x = 0
        self.env[k] = x
        return x >> 9

    def done(self):
        if not self.live or not self.stage[3]:
            self.live = 0
            return True
        return False

    def lfos(self):
        """the three LFOs read, then advanced: the destination sums (Q15 each)"""
        p = self.part.p
        g = lambda n: p[Q[n]]
        dst = [0] * ND
        for k in range(3):
            d, tm, wv = g("L%dDEST" % k), g("L%dTRIG" % k), g("L%dWAVE" % k)
            inc0 = lfo_inc(p, k)
            if tm == 0:
                ph = (self.part.lph[k] + (0 if wv == 6 else g("L%dPHASE" % k) << 25)) & M32
                rv = self.part.lrnd[k]
            else:
                ph = self.lph[k]
                rv = self.lrnd[k]
                if tm != 2 and not (self.lstop >> k) & 1:
                    old = ph
                    st = abs(inc0) >> 1
                    if tm != 1:
                        lim = 0x80000000 if tm == 3 else 0x40000000
                        if self.ltr[k] + st >= lim:
                            st = lim - self.ltr[k]
                            self.lstop |= 1 << k
                        self.ltr[k] += st
                    self.lph[k] = (old + (-(st << 1) if inc0 < 0 else st << 1)) & M32
                    if (inc0 > 0 and self.lph[k] < old) or (inc0 < 0 and self.lph[k] > old):
                        self.lrnd[k] = xs(self.lrnd[k] + k)
            if wv == 6:                                    # RAND: SPH is the slew (a one-pole on the steps)
                r = (rv >> 16) - 32768
                sl = g("L%dPHASE" % k)
                if sl:
                    kk = clamp(((abs(inc0) >> 17) * 254) // sl, 1, 32767)
                    self.lsl[k] += ((r - self.lsl[k]) * kk) >> 15
                else:
                    self.lsl[k] = r
            if not d or d >= ND or not g("L%dDEPTH" % k):
                continue
            w = lfo_wave(wv, ph, self.lsl[k])
            w = (w * g("L%dDEPTH" % k)) >> 6
            fd = g("L%dFADE" % k)
            if fd:
                f = min(32767, self.ticks * (ENV_LIN[2 * abs(fd) - 1] >> 9))
                w = (w * (f if fd < 0 else 32767 - f)) >> 15
            dst[d] += w
        if self.ticks < 0xFFFF:
            self.ticks += 1
        return dst

    def render(self, out, pitch16, inc_base, latest):
        p = self.part.p
        g = lambda n: p[Q[n]]
        dst = self.lfos()
        D = lambda n, s: (dst[DEST[n]] * s) >> 15
        mv = lambda n, de, s, lo, hi: clamp(g(n) + D(de, s), lo, hi)
        eA = self.env_tick(0, mv("AATK", "AATK", 127, 0, 127), mv("ADEC", "ADEC", 127, 0, 127),
                           mv("AEND", "AEND", 127, 0, 127) << 17, 0, mv("ADLY", "ADLY", 127, 0, 127), 1, 0)
        eB = self.env_tick(1, mv("BATK", "BATK", 127, 0, 127), mv("BDEC", "BDEC", 127, 0, 127),
                           mv("BEND", "BEND", 127, 0, 127) << 17, 0, mv("BDLY", "BDLY", 127, 0, 127), 1, 0)
        eF = self.env_tick(2, mv("FATK", "FATK", 127, 0, 127), mv("FDEC", "FDEC", 127, 0, 127),
                           mv("FSUS", "FSUS", 127, 0, 127) << 17, mv("FREL", "FREL", 127, 0, 127), g("FDLY"),
                           self.gate, 1)
        eE = self.env_tick(3, mv("EATK", "EATK", 127, 0, 127), mv("EDEC", "EDEC", 127, 0, 127),
                           mv("ESUS", "ESUS", 127, 0, 127) << 17, mv("EREL", "EREL", 127, 0, 127), 0, self.gate, 1)
        if latest:
            self.part.pan = clamp((dst[DEST["PAN"]] * 64) >> 15, -64, 63)
        alg = mv("ALGO", "ALGO", 7, 1, 8)
        dt = mv("DTUN", "DTUN", 127, 0, 127)
        ra = mv("RA", "RA", 63, 0, 63)
        rb = clamp(g("RB1") * 19 + g("RB2") + D("RB", 180), 0, NRB - 1)
        rb1i, rb2i = rb // 19, rb % 19

        def ratio(r, o):
            r += cdiv(o * 65536, 100)
            return 0 if r < 0 else r

        def opinc(base, r, det):
            qq = (r * (65536 + det)) >> 16
            return (base * qq >> 16) & M32

        bAll = inc_base
        if dst[DEST["PITCH"]]:
            bAll = (inc_base * exp2q((dst[DEST["PITCH"]] * 4096) >> 15)) >> 15
        bAB2 = bAll
        if dst[DEST["PAB2"]]:
            bAB2 = (bAll * exp2q((dst[DEST["PAB2"]] * 4096) >> 15)) >> 15
        rc = ratio(q16(RCB[mv("RC", "RC", 18, 0, 18)]), mv("OFSC", "OFSC", 100, -100, 100))
        rA = ratio(q16(RA[ra]), mv("OFSA", "OFSA", 100, -100, 100))
        rB1 = ratio(q16(RCB[rb1i]), mv("OFSB1", "OFSB1", 100, -100, 100))
        rB2 = ratio(q16(RCB[rb2i]), mv("OFSB2", "OFSB2", 100, -100, 100))
        inc = [opinc(bAll, rc, 0), opinc(bAB2, rA, DT_UP[dt]), opinc(bAll, rB1, 0), opinc(bAB2, rB2, DT_DN[dt])]
        hq = clamp((g("HARM") << 8) + ((dst[DEST["HARM"]] * 26 * 256) >> 15), -26 * 256, 26 * 256)
        ha = hb = HARM[0]
        hff = 0.0
        if hq:
            h = abs(hq)
            t0 = h >> 8
            t1 = t0 + 1 if t0 < 26 else 26
            hff = ((h & 255) << 7) / 32768.0 if t1 > t0 else 0.0
            ha, hb = HARM[t0], HARM[t1]
        fbq = fbq_of(mv("FDBK", "FDBK", 120, 0, 120))
        mix = mv("MIX", "MIX", 127, -64, 63)
        gy = (64 + mix) * 258
        gx = 32766 - gy
        vel = self.vel
        velf = 32767 - ((g("VEL") * (127 - vel) * 2080) >> 10)
        la_v = mv("ALEV", "ALEV", 127, 0, 127)
        u1, u2 = blev(mv("BLEV", "BLEV", 127, 0, 127))
        lv = [lev_q15(la_v * 4096 // 127), lev_q15(u1), lev_q15(u2)]
        tl = [0, 0, 0]
        for j, kn in enumerate(("AKEY", "B1KEY", "B2KEY")):
            l = mulq15(lv[j], velf)
            kt = g(kn)
            if kt:
                gg = min(65535, exp2q(-(((pitch16 - 960) * kt * 172) >> 10)))
                l = min(32767, (l * gg) >> 15)
            tl[j] = mulq15(eA if j == 0 else eB, l)
        l0 = [x << 16 for x in self.lv]
        dl = [((tl[j] << 16) - l0[j]) >> 5 for j in range(3)]
        fdep = mv("FDEPTH", "FENV", 127, -64, 63)
        cut = (mv("FREQ", "FREQ", 127, 0, 127) << 8) + ((eF * fdep) >> 6) + \
            (((pitch16 - 960) * g("FKTRK") * 150) >> 10)
        reso = mv("RESO", "RESO", 127, 0, 127)
        fty = g("FTYPE")
        svf = fty != 0 and not (fty != 2 and cut >= (127 << 8) and not reso)
        kd = 8192 - reso * 60
        c1 = c2 = (0, 0, 0)
        if svf:
            c1 = tsvf_coef_k(cut, kd)
            if fty == 3:
                c2 = tsvf_coef_k(cut, 8192)
        base = mv("BASE", "BASE", 127, 0, 127)
        wid = mv("WIDTH", "WIDTH", 127, 0, 127)
        hpk = BW_K[base] / 65536.0 if base else 0.0
        lpk = BW_K[base + wid] / 65536.0 if base + wid < 127 else 0.0
        lvl = mv("LEVEL", "LEVEL", 127, 0, 127)
        A1 = mulq15(mulq15(eE, 32767 - (127 - vel) * 129), lvl * lvl * 2)
        A1 = mulq15(A1, 32767)
        A0 = self.amp
        dA = (A1 - A0) >> 5
        a = A0
        p0, p1, p2, p3 = self.ph
        fb1, fb2 = self.fb1, self.fb2
        fs = self.f[:]
        bh, bl, dc = self.bh, self.bl, self.dc
        S = lambda ph: tab_f(SINE, ph, 1024)
        if hq:
            HW = lambda ph: tab_f(ha, ph, HN) + ((tab_f(hb, ph, HN) - tab_f(ha, ph, HN)) * hff if hff else 0.0)
        else:
            HW = S
        WC = HW if hq < 0 else S
        WA = WB1 = HW if hq > 0 else S
        WS = S
        K = 262144.0     # a modulator's output (Q15) -> phase: << 18
        la, lb1, lb2 = l0
        for i in range(CTL):
            la += dl[0]
            lb1 += dl[1]
            lb2 += dl[2]
            LA, LB1, LB2 = (la >> 16) / 32768.0, (lb1 >> 16) / 32768.0, (lb2 >> 16) / 32768.0
            FB = (fb1 + fb2) * fbq * 4.0
            if alg == 1:
                yf = WA(p1 + FB)
                y1 = WB1(p2 + WS(p3) * LB2 * K)
                X = WC(p0 + (yf * LA + y1 * LB1) * K)
                Y = y1
            elif alg == 2:
                X = WC(p0 + WA(p1) * LA * K)
                yf = WS(p3 + FB)
                Y = WB1(p2 + yf * LB2 * K)
            elif alg == 3:
                yf = WA(p1 + FB)
                oa = yf * LA * K
                X = WC(p0 + oa) + WS(p3 + oa)
                Y = WB1(p2 + oa)
            elif alg == 4:
                yf = WS(p3 + FB)
                y1 = WB1(p2 + yf * LB2 * K)
                yA = WA(p1 + y1 * LB1 * K)
                X = WC(p0 + yA * LA * K)
                Y = y1
            elif alg == 5:
                yf = WB1(p2 + FB)
                yA = WA(p1 + (yf * LB1 + WS(p3) * LB2) * K)
                X = WC(p0 + yA * LA * K)
                Y = yA
            elif alg == 6:
                yf = WA(p1 + FB)
                o = (yf * LA + WS(p3) * LB2) * K
                X = WC(p0 + o)
                Y = WB1(p2 + o)
            elif alg == 7:
                yf = WA(p1 + FB)
                oa = yf * LA
                X = WC(p0 + oa * K) + oa
                ob2 = WS(p3) * LB2
                Y = WB1(p2 + ob2 * K) * LB1 + ob2
            else:
                X = WC(p0 + WA(p1) * LA * K) + WS(p3) * LB2
                yf = WB1(p2 + FB)
                Y = yf * LB1
            fb2, fb1 = fb1, yf
            p0 += inc[0]
            p1 += inc[1]
            p2 += inc[2]
            p3 += inc[3]
            x = (X * gx + Y * gy) / 32768.0 / 2.0
            dc += (x - dc) * DC_K
            y = x - dc
            if hpk:
                bh += (y - bh) * hpk
                y -= bh
            if lpk:
                bl += (y - bl) * lpk
                y = bl
            if svf:
                if fty == 3:                                  # LP24: a plain LP12 stage, then the resonant one
                    a1, a2, a3 = c2
                    v3 = y - fs[3]
                    v1 = (a1 * fs[2] + a2 * v3) / 8192.0
                    v2 = fs[3] + (a2 * fs[2] + a3 * v3) / 8192.0
                    fs[2] = clamp(2 * v1 - fs[2], -150000, 150000)
                    fs[3] = clamp(2 * v2 - fs[3], -150000, 150000)
                    y = v2
                a1, a2, a3 = c1
                v3 = y - fs[1]
                v1 = (a1 * fs[0] + a2 * v3) / 8192.0
                v2 = fs[1] + (a2 * fs[0] + a3 * v3) / 8192.0
                fs[0] = clamp(2 * v1 - fs[0], -150000, 150000)
                fs[1] = clamp(2 * v2 - fs[1], -150000, 150000)
                y = y - kd * v1 / 4096.0 - v2 if fty == 2 else v2
                y = soft_knee_f(clamp(y, -200000, 200000), 16000)
            y = clamp(y, -65535, 65535)
            a += dA
            out[i] += y * a / 32768.0 * 6000 / 2048.0
        self.ph = [p0 % 4294967296.0, p1 % 4294967296.0, p2 % 4294967296.0, p3 % 4294967296.0]
        self.fb1, self.fb2 = fb1, fb2
        self.f, self.bh, self.bl, self.dc = fs, bh, bl, dc
        self.lv = tl[:]
        self.amp = A1


def render(patch, notes=(60, 67), vel=100, secs=0.3, rel=0.2):
    part = Part(patch)
    part.pan = 0
    vs = [Voice(part, i) for i in range(len(notes))]
    for v, n in zip(vs, notes):
        v.note_on(n, vel)
    nt = int(secs * FS / CTL)
    out_all = []
    for t in range(nt):
        if t == int(rel * FS / CTL):
            for v in vs:
                v.gate = 0
        part.block()
        out = [0.0] * CTL
        for i, v in enumerate(vs):
            if not v.active:
                continue
            if v.done():
                v.active = v.gate = 0
                v.venv = v.env_out = 0
                continue
            v.venv = 1 << 24
            v.env_out = 32767
            v.render(out, v.note * 16, pitch_inc(v.note * 16), i == len(vs) - 1)
        out_all += out
    return out_all


def golden(name, patch):
    out = render(patch)
    n = len(out)
    rms = []
    for b in range(0, n - 440, 441):
        blk = out[b:b + 441]
        rms.append(round(math.sqrt(sum(x * x for x in blk) / len(blk)), 3))
    g = {"name": name, "patch": patch, "notes": [60, 67], "vel": 100, "secs": 0.3, "release": 0.2, "n": n,
         "first": [int(round(x)) for x in out[:2048]], "rms": rms}
    fn = os.path.join(GOLD, re.sub(r"[^a-z0-9]+", "_", name.lower()) + ".json")
    with open(fn, "w") as f:
        json.dump(g, f, separators=(",", ":"))
    return fn, max(abs(x) for x in out)


def main():
    write_header()
    print("quad_ref: wrote", os.path.relpath(OUT_H, ROOT))
    os.makedirs(GOLD, exist_ok=True)
    jobs = [("algo%d" % a, algo_patch(a)) for a in range(1, 9)] + list(PRESETS.items())
    for name, p in jobs:
        fn, pk = golden(name, p)
        print("quad_ref: %-28s peak %7.0f" % (os.path.relpath(fn, ROOT), pk))


if __name__ == "__main__":
    main()
