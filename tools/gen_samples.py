#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Build the SAMPLER engine's sample sets from WAV files into a C header.

Samples are stored as IMA ADPCM (4 bit) at their native rate; loop points
and roots come from the WAV 'smpl' chunk and the file name (C3 = MIDI 60
naming), single-cycle waves get their root from rate / loop length.
The ADPCM state at the loop start is stored so loops restart exactly.

Libraries:
  cc0        assets/samples-cc0/ (tools/fetch_cc0.py, Versilian Studios, CC0):
             PIANO, FLUTE, SAX (set 1, once TRANH, is an alias of PIANO)
  generated  Felucca's own drum kit from tools/gen_waves.py (always built), with
             CC0 hand percussion: one GM-mapped kit, PERC
SAMPLE's factory presets end before PERC. Its initializer remains in the table
for old projects; every sample set and SET / USR index stays in its original place.
A retired set keeps its index as an alias: the original's name and zones (no data), and a
preset equal to the original's (SMP_SET_ORIG), so old projects and presets play it and
browsing skips it (params.c enum_orig, ui.c preset_orig).

The SLICE engine's built-in BREAK (eng_slice.c) is rendered here too: one bar of 16ths
arranged from the generated drums (no third-party loop), stored after every set (their
offsets do not move) and NOT one of the SAMPLE sets (the SET list, its presets and the
USR1-3 numbers stay as they were). Its slice table (decoder states on a 128-point grid,
the hits as AUTO slices) is written with it: SLC_BREAK_INIT.

The header is cached (build/gen_samples.cache) under a hash of every
input file, this script, sampleio.py and the Python version, so unchanged
inputs skip the slow pitch detection.
"""
import hashlib
import math
import os
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import sampleio as sio  # noqa: E402
from sampleio import detect_hz, hz_to_midi, ima_encode, key_split, onset, peak, read_any_wav, resample  # noqa: E402

SRC = Path(__file__).resolve().parents[1]
GENDIR = SRC / "build" / "genwav"
CACHE = SRC / "build" / "gen_samples.cache"
CC0 = SRC / "assets" / "samples-cc0"
TR = 22050                                   # stored sample rate

# CC0 library: set -> kind ("oneshot" decaying, "sus" looped sustain, "kit" one sample per key)
# Built-in sets (piano, flute, sax, GM percussion); the other slots of the 8 are for the user
# (USR1-3, loaded from the web editor). TRANH was removed (66 KB of flash); its index 1 stays, an alias of PIANO ("alias": the set named)
CC0_SETS = [("PIANO", "oneshot"), ("PIANO", "alias"), ("FLUTE", "sus"), ("SAX", "sus"), ("KIT", "kit")]
MEASURED_TUNING = ()                         # sets whose recordings are not at A440 (was TRANH, ~+35 ct)

KIT_BASE = 53                     # F3, the lowest FM-1 key

NOTE = {"C": 0, "C#": 1, "D": 2, "D#": 3, "E": 4, "F": 5, "F#": 6, "G": 7, "G#": 8, "A": 9, "A#": 10, "B": 11}

# GM drum map (General MIDI percussion key map): role -> [(lo, hi, root)].
# One sample can serve several GM notes; a range is pitched around its root.
GM_KIT = {
    "kick": [(35, 36, 36)], "rim": [(37, 37, 37)], "snare": [(38, 38, 38), (40, 40, 40)],
    "clap": [(39, 39, 39)], "chh": [(42, 42, 42), (44, 44, 44)], "ohh": [(46, 46, 46)],
    "tomlo": [(41, 45, 43)], "tomhi": [(47, 50, 48)], "tom": [(41, 50, 47)],
    "crash": [(49, 49, 49), (52, 52, 52), (55, 55, 55), (57, 57, 57)],
    "ride": [(51, 51, 51), (53, 53, 53), (59, 59, 59)],
    "tamb": [(54, 54, 54)], "cowbell": [(56, 56, 56)], "conga": [(62, 64, 63)],
    "shaker": [(69, 70, 70), (82, 82, 82)], "claves": [(75, 75, 75)], "wood": [(76, 77, 76)],
}
GM_ROLE_WORDS = [("bassdrum", "kick"), ("kick", "kick"), ("snare", "snare"), ("hihat", "chh"), ("chat", "chh"),
                 ("ohat", "ohh"), ("clap", "clap"), ("tom lo", "tomlo"), ("tom hi", "tomhi"), ("tom", "tom"),
                 ("rim", "rim"), ("cowbell", "cowbell"), ("tamb", "tamb"), ("shaker", "shaker"),
                 ("conga", "conga"), ("claves", "claves"), ("wood", "wood"), ("crash", "crash"), ("ride", "ride")]
GM_KEEP = {"crash": 0.5, "ride": 0.5, "ohh": 0.5}  # the cymbals cut to 0.5 s (saves flash) ...
GM_FADE = {"crash": 0.2, "ride": 0.2}            # ... with a long raised-cosine fade (s); the others 30 ms linear
GM_CC0_ROLES = ("tamb", "shaker", "conga", "claves", "wood")   # the CC0 set is orchestral: hand percussion only

# SLICE's BREAK: (step, GM role, gain) on a bar of 16ths; the open hat is choked by the next hat
BREAK_BPM, BREAK_STEPS = 120, 16
BREAK_HITS = [(0, "kick", 1.0), (0, "chh", 0.55), (2, "kick", 0.7), (2, "chh", 0.4), (4, "snare", 1.0),
              (4, "chh", 0.45), (6, "chh", 0.4), (7, "kick", 0.8), (8, "chh", 0.55), (9, "snare", 0.3),
              (10, "kick", 0.9), (10, "ohh", 0.4), (12, "snare", 1.0), (12, "chh", 0.45), (14, "chh", 0.4),
              (14, "snare", 0.35)]
SLC_GRID, SLC_AUTO = 128, 32                 # eng_slice.c slc_src_t

ENV = {"wave":(5, 80, 100, 50), "kit": (0, 127, 127, 60), "multi": (0, 85, 0, 75),
       "oneshot": (0, 127, 127, 70), "sus": (12, 80, 120, 60)}

# PIANO's notes cut to 0.75 s (saves flash), faded out over the last SET_FADE s
SET_KEEP = {"PIANO": 0.75}
SET_FADE = {"PIANO": 0.15}

_wavs = {}


def cos_fade(x, fade):
    """fade the last `fade` samples of x out on a raised cosine (no corner at either end)"""
    n = len(x)
    for i in range(fade):
        x[n - fade + i] *= 0.5 + 0.5 * math.cos(math.pi * (i + 1) / fade)


def wav(path):
    """read_any_wav, once per file and run (the CC0 pass reads every file twice)"""
    if path not in _wavs:
        _wavs[path] = read_any_wav(path)
    return _wavs[path]


def name_note(name):
    """'_C#3' in a CC0 file name -> (octave, pitch class)"""
    m = re.search(r"_([A-G]#?)(-?\d)", name)
    return (int(m.group(2)), NOTE[m.group(1)]) if m else None


def cc0_entries(setname, kind):
    """-> [(file name, int16 samples at TR, loop or None, root)]"""
    files = sorted((CC0 / setname).glob("*.wav"))
    out = []
    if kind != "kit":
        # octave convention of this set (C4 = 60 or C3 = 60): YIN votes, the names give the notes
        votes = {1: 0, 2: 0}
        for p in files:
            nn = name_note(p.name)
            if not nn:
                continue
            sr, x = wav(p)
            det = hz_to_midi(detect_hz(x[onset(x):], sr))
            for o in (1, 2):
                if abs(det - ((nn[0] + o) * 12 + nn[1])) < 1.0:
                    votes[o] += 1
        conv = 2 if votes[2] >= votes[1] else 1
    for k, p in enumerate(files):
        sr, x = wav(p)
        x = x[max(0, onset(x) - 16):]
        nn = name_note(p.name)
        if kind == "kit":
            root = KIT_BASE + k
        elif nn:
            root = (nn[0] + conv) * 12 + nn[1]
            if setname in MEASURED_TUNING:              # tuned off A440: the measured pitch near the named note
                det = hz_to_midi(detect_hz(x, sr))
                if abs(det - root) < 1.0:
                    root = det
        else:
            root = hz_to_midi(detect_hz(x, sr))
        x = resample(x, sr, TR)
        keep = SET_KEEP.get(setname) or {"oneshot": 1.0, "sus": 0.95, "kit": 0.6}[kind]
        x = x[:int(keep * TR)]
        n = len(x)
        if kind == "sus":                            # crossfaded sustain loop in the steady part
            ls, le, xf = int(0.40 * TR), n - 1, int(0.06 * TR)
            for i in range(xf):
                a = i / xf
                x[le - xf + i] = x[le - xf + i] * (1 - a) + x[ls - xf + i] * a
            loop = (ls, le)
        else:
            if setname in SET_FADE:                  # a shortened set: a long raised-cosine fade-out
                cos_fade(x, int(SET_FADE[setname] * TR))
            else:
                fade = int(0.08 * TR)
                for i in range(fade):
                    x[n - fade + i] *= 1 - i / fade
            loop = None
        pk = peak(x)
        out.append((p.name, [int(v * 30000 / pk) for v in x], loop, root))
    return out


def gm_role(name):
    n = name.lower()
    return next((r for w, r in GM_ROLE_WORDS if w in n), None)


def gm_kit_sources(have_cc0):
    """role -> wav path: Felucca's own synthesized drum kit (generated by
    gen_waves.py), plus CC0 hand percussion for the roles it does not make"""
    src = {}
    for p in sorted(GENDIR.glob("D *.wav")):
        r = gm_role(p.stem)
        if r and r not in src:
            src[r] = p
    if have_cc0 and (CC0 / "KIT").exists():
        for p in sorted((CC0 / "KIT").glob("*.wav")):
            r = gm_role(p.name)
            if r in GM_CC0_ROLES and r not in src:
                src[r] = p
    if "tomlo" in src or "tomhi" in src:
        src.pop("tom", None)
    return src


def gm_kit_entry(role, path):
    sr, x = wav(path)
    x = resample(x[max(0, onset(x) - 16):], sr, TR)
    x = x[:int(GM_KEEP.get(role, 0.45) * TR)]
    pk = peak(x)
    end = len(x)                                    # drop the silent tail
    while end > 64 and abs(x[end - 1]) < 0.004 * pk:
        end -= 1
    x = x[:end]
    if role in GM_FADE:
        cos_fade(x, min(len(x) // 2, int(GM_FADE[role] * TR)))
    else:
        fade = min(len(x) // 4, int(0.03 * TR))
        for i in range(fade):
            x[len(x) - fade + i] *= 1 - i / fade
    return [int(v * 30000 / pk) for v in x]


def ima_states(data, positions):
    """IMA ADPCM decoder state before sample p for each p (ascending), packed as eng_slice.c reads it:
    (predictor & 0xFFFF) | index << 16 (segment 0)"""
    pred, idx, out, want = 0, 0, [], list(positions)
    for n in range(max(want) + 1 if want else 0):
        while want and want[0] == n:
            out.append((pred & 0xFFFF) | idx << 16)
            want.pop(0)
        code = (data[n >> 1] >> (4 * (n & 1))) & 15
        step = sio.IMA_STEP[idx]
        vd = step >> 3
        if code & 4:
            vd += step
        if code & 2:
            vd += step >> 1
        if code & 1:
            vd += step >> 2
        pred = max(-32768, min(32767, pred - vd if code & 8 else pred + vd))
        idx = max(0, min(88, idx + sio.IMA_IDX[code & 7]))
    return out


def break_loop():
    """SLICE's BREAK: BREAK_HITS from Felucca's generated drums at TR -> (int16 samples, hit positions)"""
    src = gm_kit_sources(False)                     # generated sounds only: the same on every build
    n = int(round(TR * 60 / BREAK_BPM * 4))
    x = [0.0] * n
    pos = [s * n // BREAK_STEPS for s in range(BREAK_STEPS + 1)]
    for step, role, gain in BREAK_HITS:
        smp = gm_kit_entry(role, src[role])
        if role == "ohh":                           # choked by the next hat
            nxt = min([s for s, r, _ in BREAK_HITS if r in ("chh", "ohh") and s > step] or [BREAK_STEPS])
            keep, fade = pos[nxt] - pos[step], int(0.004 * TR)
            smp = [v * min(1.0, (keep - i) / fade) for i, v in enumerate(smp[:keep])]
        for i, v in enumerate(smp):                 # the tails wrap round: a seamless loop
            x[(pos[step] + i) % n] += v * gain
    pk = peak(x)
    return [int(v * 30000 / pk) for v in x], sorted({pos[s] for s, _, _ in BREAK_HITS})


class Builder:
    def __init__(self):
        self.zones, self.sets, self.blob, self.kinds = [], [], bytearray(), {}
        self.alias = {}                         # set index -> the set index it aliases
        self.brk = None

    def slice_break(self):
        """SLICE's BREAK, after every set; its slice table: decoder states at k * len / SLC_GRID, the hits"""
        x, hits = break_loop()
        off, _ = self.add(x, 0)
        n = len(x)
        data = self.blob[off:off + (n + 1) // 2]
        grid = ima_states(data, [k * n // SLC_GRID for k in range(SLC_GRID)])
        hits = hits[:SLC_AUTO]
        self.brk = dict(off=off, n=n, grid=grid, apos=hits, ast=ima_states(data, hits))

    def add(self, s, loop_start):
        """ADPCM-encode s into the blob (each sample starts on an even offset) -> (offset, state at loop_start)"""
        enc, st = ima_encode(s, loop_start)
        off = len(self.blob)
        self.blob += enc
        if len(self.blob) & 1:
            self.blob.append(0)
        return off, st

    def add_set(self, name, kind, entries):
        """entries: zone dicts with root16 (and key for a kit); assigns the key ranges"""
        if not entries:
            return
        if kind == "kit":
            for e in entries:
                e["lo"] = e["hi"] = e["key"]
        else:
            entries.sort(key=lambda e: e["root16"])
            for e, (lo, hi) in zip(entries, key_split([e["root16"] // 16 for e in entries])):
                e["lo"], e["hi"] = lo, hi
        self.sets.append((name, len(self.zones), len(entries)))
        self.zones += entries
        self.kinds[name] = kind

    def gm_kit(self, have_cc0):
        """one GM-mapped kit: generated drums + CC0 hand percussion"""
        z0 = len(self.zones)
        for role, path in gm_kit_sources(have_cc0).items():
            smp = gm_kit_entry(role, path)
            off, st = self.add(smp, len(smp))
            for lo, hi, root in GM_KIT[role]:
                self.zones.append(dict(off=off, n=len(smp), ls=len(smp), le=len(smp), looped=False, sr=TR,
                                       root16=root * 16, pred=st[0], idx=st[1], lo=lo, hi=hi))
        self.sets.append(("PERC", z0, len(self.zones) - z0))
        self.kinds["PERC"] = "kit"

    def alias_set(self, name):
        """a retired set's index: the zones of the earlier set `name` (no data of its own)"""
        orig = next(i for i, (n, _, _) in enumerate(self.sets) if n == name)
        self.alias[len(self.sets)] = orig
        self.sets.append(self.sets[orig])

    def cc0_set(self, name, kind):
        entries = []
        for k, (_, s, loop, root) in enumerate(cc0_entries(name, kind)):
            ls, le = loop if loop else (len(s), len(s))
            off, st = self.add(s, ls)
            entries.append(dict(off=off, n=len(s), ls=ls, le=le, looped=bool(loop), sr=TR,
                                root16=int(round(root * 16)), pred=st[0], idx=st[1],
                                key=KIT_BASE + k if kind == "kit" else None))
        self.add_set(name, kind, entries)

    def header(self):
        zones, sets, blob = self.zones, self.sets, self.blob
        L = ["/* generated by tools/gen_samples.py: IMA ADPCM sample sets */", "#pragma once",
             "#include <stdint.h>", ""]
        L.append(f"static const uint8_t SMP_DATA[{max(1, len(blob))}] = {{")
        for i in range(0, len(blob), 32):
            L.append("    " + ",".join(map(str, blob[i:i + 32])) + ",")
        if not blob:
            L.append("    0,")
        L.append("};")
        L.append("static const smp_zone_t SMP_ZONES[] = {")
        for e in zones:
            rate = int(round(e["sr"] / 44100 * 65536))
            L.append(f"    {{{e['off']}, {e['n']}, {e['ls']}, {e['le']}, {rate}, {e['root16']}, {e['pred']}, "
                     f"{e['idx']}, {e['lo']}, {e['hi']}, {1 if e['looped'] else 0}}},")
        if not zones:
            L.append("    {0, 0, 0, 0, 65536, 960, 0, 0, 0, 127, 0},")
        L.append("};")
        L.append("static const smp_set_t SMP_SETS[] = {")
        for name, z0, nz in sets:
            L.append(f'    {{"{name}", {z0}, {nz}}},')
        if not sets:
            L.append('    {"NONE", 0, 1},')
        L.append("};")
        L.append(f"#define SMP_NSETS {max(1, len(sets))}")
        named = sets or [("NONE", 0, 0)]
        perc = next((i for i, (name, _, _) in enumerate(named) if name == "PERC"), len(named))
        L.append(f"#define SMP_NPRESETS {perc}")
        L.append(f"#define SMP_PERC_PRESET {perc}")
        L.append("/* PERC and later private SET initializers are retained, outside factory browsing. */")
        L.append("static const preset_t SMP_PRESET_TABLE[] = {")
        for i, (name, _, _) in enumerate(named):
            k = self.kinds.get(name, "wave")
            a, d, s_, r = ENV[k]
            loop = 0 if k == "kit" else 1
            pat = ", PAT(12)" if k == "kit" else ""     # a kit: engines.c PATTERNS[11] BEAT
            si = self.alias.get(i, i)                   # an alias: the original's preset (hidden by its name)
            L.append(f'    {{"{name}", {{{si}, 0, 0, {loop}, 127, 0, 0, 0}}, {{{a}, {d}, {s_}, {r}}}, 0, 0{pat}}},')
        L.append("};")
        orig = [self.alias.get(i, i) for i in range(len(named))]
        L.append("/* SET i plays SMP_SET_ORIG[i]: != i for a retired set kept as an alias (its preset i too) */")
        L.append("static const uint8_t SMP_SET_ORIG[] = {" + ", ".join(map(str, orig)) + "};")
        L.append(f"#define SMP_NALIAS {sum(1 for i, o in enumerate(orig) if o != i and i < perc)}   /* among the presets */")
        names = ", ".join(f'"{n}"' for n, _, _ in named)
        L.append("#define SMP_SET_NAMES_INIT " + names)
        L.append("static const char *const SMP_SET_NAMES[] = {" + names + "};")
        L += self.break_header()
        return "\n".join(L) + "\n"

    def break_header(self):
        """SLC_BREAK_INIT: an eng_slice.c slc_src_t (len, rate, nseg, nauto, seg[], grid[], apos[], ast[])"""
        b = self.brk
        if not b:
            return ["#define SLC_BREAK_INIT {0}"]
        rate = int(round(TR / 44100 * 65536))

        def lst(v, k):
            v = list(v) + [0] * (k - len(v))
            return ", \\\n    ".join(", ".join(map(str, v[i:i + 12])) for i in range(0, len(v), 12))
        return ["", f"/* SLICE's BREAK: one bar of {BREAK_STEPS} steps at {BREAK_BPM} BPM, {len(b['apos'])} hits */",
                f"#define SLC_BREAK_BPM {BREAK_BPM}", f"#define SLC_BREAK_STEPS {BREAK_STEPS}",
                f"#define SLC_BREAK_INIT {{{b['n']}, {rate}, 1, {len(b['apos'])}, {{{{{b['off']}, 0, {b['n']}}}}}, {{ \\",
                "    " + lst(b["grid"], SLC_GRID) + "}, { \\", "    " + lst(b["apos"], SLC_AUTO) + "}, { \\",
                "    " + lst(b["ast"], SLC_AUTO) + "}}"]

    def summary(self):
        brk = f", SLICE BREAK {self.brk['n']} samples" if self.brk else ""
        return f"samples: {len(self.sets)} sets, {len(self.zones)} zones, {len(self.blob)} B ADPCM{brk}"


def slice_on():
    """the SLICE engine is built (src/core.h: on unless FELUCCA_SLICE=0)"""
    return os.environ.get("FELUCCA_SLICE", "1") != "0"


def input_key(have_cc0):
    """hash of everything the header depends on"""
    h = hashlib.sha256()
    here = Path(__file__).resolve().parent
    for p in (here / "gen_samples.py", here / "sampleio.py"):
        h.update(p.read_bytes())
    h.update(repr((sys.version_info[:2], have_cc0, slice_on())).encode())   # sum() differs across versions
    files = sorted(GENDIR.glob("*.wav"))
    if have_cc0:
        files += sorted(CC0.glob("*/*.wav"))
    for p in files:
        h.update(str(p.relative_to(SRC)).encode() + b"\0")
        h.update(hashlib.sha256(p.read_bytes()).digest())
    return h.hexdigest()


def main(out):
    subprocess.run([sys.executable, str(Path(__file__).with_name("gen_waves.py")), str(GENDIR)], check=True)
    have_cc0 = CC0.exists() and any(CC0.glob("*/*.wav"))
    if not have_cc0:
        print("samples: no CC0 samples in assets/samples-cc0 - generated material only")
    key = input_key(have_cc0)
    try:
        ck, summary, text = CACHE.read_text().split("\n", 2)
        if ck == key:
            Path(out).write_text(text)
            print(summary + " (cached)")
            return
    except (OSError, ValueError):
        pass
    b = Builder()
    if have_cc0:
        for name, kind in CC0_SETS:
            if kind == "alias":
                b.alias_set(name)
            elif kind != "kit":                     # the CC0 KIT feeds the GM kit
                b.cc0_set(name, kind)
    b.gm_kit(have_cc0)
    if slice_on():                                  # SLICE's BREAK: only when that engine is built
        b.slice_break()                             # last: the sets' offsets stay as they were
    text = b.header()
    Path(out).write_text(text)
    CACHE.parent.mkdir(parents=True, exist_ok=True)
    CACHE.write_text(f"{key}\n{b.summary()}\n{text}")
    print(b.summary())


if __name__ == "__main__":
    main(sys.argv[1])
