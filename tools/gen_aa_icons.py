#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Rasterise the Fukiai icon font (web/fukiai.ttf, MIT, (c) Hügelton Instruments) into
4-bit alpha icon cells for the UI (run by tools/build.py generate()).

  gen_aa_icons.py OUT.h [--font web/fukiai.ttf] [--sizes 12,16] [--big NAME,NAME,...]
                        [--gamma G] [--prefix ICON_] [--list] [--report]

Every icon the firmware already names (assets/icons.json, ICON_<NAME>; icons.c keeps
choosing which one a parameter gets) is mapped to a Fukiai glyph by LEGACY below, and the icons the
UI adds (transport, tracks 1-4, USB, favourites, ...) by EXTRA (ICON_X_<NAME>).

Output: per size S in --sizes
  AI<S>_DATA    cells of S x S px, 4-bit alpha, 2 px per byte, rows back to back
  AI<S>_IDX[ICON_COUNT]  cell number of every icon at that size, 0xFF = not rasterised at that size
  (12 px: all legacy icons + SMALL_EXTRA; 16 px: only the --big set; 24 px: HUGE_DEFAULT)
--list prints every Fukiai glyph name and its codepoint (what is available).
--report prints the mapping, the unmapped legacy names and the byte cost.
"""
import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import aa_raster as ar  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]

# legacy icon name (assets/icons.json) -> Fukiai glyph name. Chosen from the glyph names only
# (the semantics: ADSR stage icons for ATK/DEC/SUS/REL, filter response for CUT/RES, ...): review the
# result in the icon specimen of the UI renders and adjust the table, nothing else changes.
LEGACY = {
    "generic": "ui_knob", "attack": "function_env_adsr_attack", "decay": "function_env_adsr_decay",
    "sustain": "function_env_adsr_sustain", "release": "function_env_adsr_release", "env": "function_env_adsr_lin",
    "cutoff": "function_filter_lpf", "reso": "function_filter_lpf_peak", "rate": "symbol_speed",
    "lfo_wave": "waveform_sine", "wave": "waveform_variant", "pulse": "waveform_pulse_quarter",
    "shape": "waveform_variant_four", "pitch": "waveform_pitch", "tune": "symbol_tuning",
    "detune": "function_phaseout", "transpose": "control_arrow_up_down", "octave": "control_arrow_up",
    "level": "symbol_volume", "pan": "symbol_pan", "mute": "control_speaker_mute",
    "dist": "function_signal_clip", "drive": "symbol_diode", "fold": "waveform_fold", "bits": "function_mask",
    "chorus": "symbol_waves", "delay": "symbol_echo", "reverb": "symbol_spring", "feedback": "symbol_feedback",
    "arp": "control_arrow_loop", "gate": "function_gate_unipolar", "swing": "symbol_swing",
    "tempo": "symbol_tempo", "scale": "control_arow_scale", "quantize": "symbol_grid",
    "glide": "symbol_transition", "slide": "symbol_rise", "voice": "symbol_primitives_o", "mod": "symbol_modular",
    "noise": "waveform_noise_white", "sub": "symbol_bass", "ratio": "function_multiply",
    "algorithm": "symbol_node", "sample": "symbol_audio", "steps": "symbol_grid_nine", "accent": "symbol_thunder_f",
    "time": "symbol_clock", "phase": "function_phase", "fade": "waveform_fade", "mix": "symbol_combine",
    "keytrack": "symbol_keyboard", "size": "control_arrow_both", "damp": "symbol_weight",
    "tone": "function_filter_band", "sweep": "symbol_warp", "vibrato": "waveform_cos", "loop": "waveform_loop",
    "hold": "symbol_lock_close_f", "order": "symbol_sort", "prob": "symbol_dice",
    "length": "control_arrow_end", "division": "note_quarter", "chip": "symbol_primitives_f",
    "midi": "port_midi", "save": "symbol_download_as", "load": "symbol_folder_open", "clear": "symbol_trash",
    "w_sin": "waveform_sine", "w_tri": "waveform_triangle_ac", "w_saw": "waveform_sawtooth_ac",
    "w_sqr": "waveform_square", "w_pls": "waveform_pulse_quarter", "w_pwm": "waveform_pulse_half",
    "w_sh": "waveform_noise_step", "w_dsin": "function_signal_double", "w_spls": "waveform_pulse_eighth",
    "w_rsaw": "waveform_sawtooth_ac_inv", "w_rtri": "waveform_triangle_dc", "w_rtrp": "waveform_triangle_dc_inv",
    "tape": "symbol_tape", "mouth": "symbol_mouth", "trio": "symbol_primitives_o",
    "drawbar": ("symbol_drawbar", "ui_slider_vertical"), "slice": "symbol_comb", "grain": "symbol_cloud_o", "phys": "symbol_pick",
    "drum": "symbol_drum",
}

# icons the redesign adds (header, dialogs, lists, menu); name -> Fukiai glyph
EXTRA = {
    "x_play": "control_play_f", "x_stop": "control_stop_f", "x_pause": "control_pause_f", "x_rec": "control_rec_f",
    "x_rec_o": "control_rec_o", "x_play_o": "control_play_o",
    "x_trk1": "numbers_circle_1_roman_f", "x_trk2": "numbers_circle_2_roman_f",
    "x_trk3": "numbers_circle_3_roman_f", "x_trk4": "numbers_circle_4_roman_f",
    "x_trk1_o": "numbers_circle_1_roman_o", "x_trk2_o": "numbers_circle_2_roman_o",
    "x_trk3_o": "numbers_circle_3_roman_o", "x_trk4_o": "numbers_circle_4_roman_o",
    "x_usb": "port_usb_c", "x_star": "symbol_star", "x_star_o": "symbol_star_o", "x_check": "symbol_check",
    "x_cog": "symbol_cog", "x_lock": "symbol_lock_close_f", "x_power": "control_power",
    "x_folder": "symbol_folder", "x_doc": "symbol_document", "x_plus": "symbol_plus", "x_minus": "symbol_minus",
    "x_undo": "symbol_arrow_undo", "x_redo": "symbol_arrow_redo", "x_warn": "symbol_error",
    "x_palette": "symbol_primitives_o", "x_speaker": "control_speaker_2", "x_info": "symbol_info_f",
    "x_back": "control_arrow_back", "x_down": "symbol_carret_down", "x_up": "symbol_carret_up",
    "x_left": "symbol_carret_left", "x_right": "symbol_carret_right", "x_hugelton": "symbol_hugelton",
    # page and state icons (Fukiai 0.8.0): MIXER / PHRASES / SONG pages, motion, calibration, the battery
    # (battery_2 is not used: the stock level 0..3 maps to battery_0 / 1 / 3 / 4, see ui_draw.c batt_icon)
    "x_mixer": "ui_slider_vertical", "x_pattern": "symbol_grid_nine", "x_song": "symbol_disc",
    "x_motion": "symbol_motion", "x_motion_rec": "symbol_motion_rec", "x_motion_del": "symbol_motion_delete",
    "x_doctor": "symbol_doctor", "x_bat0": "system_battery_0", "x_bat1": "system_battery_1",
    "x_bat3": "system_battery_3", "x_bat4": "system_battery_4", "x_bat_chg": "system_battery_charging",
    # the FX hold layer (ui_draw.c draw_layer, Fukiai 0.8.3) and the HOLD menu row
    "x_fx": "control_fx", "x_timer": "symbol_stopwatch", "x_hpf": "function_filter_hpf",
    "x_repeat": "control_arrow_loop", "x_reverse": "control_reverse_f", "x_tstop": "symbol_stop",
    "x_freeze": "symbol_freeze",
    # OCT UP / OCT DN (the harmonizer): stand-ins until Fukiai has a pitch-shift glyph;
    # the first of each tuple the font has
    "x_oct_up": ("symbol_pitch_up", "control_arrow_up"), "x_oct_dn": ("symbol_pitch_down", "control_arrow_down"),
}

# a glyph given as a tuple: the first one the font has (a glyph still to be drawn falls back to a stand-in;
# a missing preferred glyph never breaks the build). WHEEL: symbol_drawbar once Fukiai has it.

# glyphs broken in the font, mended before rasterising (a mended font needs no change here: a glyph already
# on the em, advance == unitsPerEm, is left alone). Fukiai 0.8.0 system_battery_0: drawn at the wrong scale
# (advance 4653, outline 4654 x 2048 units on a 2048 em) and with a filled copy of its counter (an extra
# contour of the inner box wound like the outside: the battery reads solid). The copy is dropped (a contour
# with the box of a counter but the outside's winding) and the rest fitted into the outline box of a sibling.
FIT_TO = {"system_battery_0": "system_battery_4"}

# new icons that are also needed at 12 px (list rows, menu values)
SMALL_EXTRA = ["x_trk1", "x_trk2", "x_trk3", "x_trk4", "x_trk1_o", "x_trk2_o", "x_trk3_o", "x_trk4_o",
               "x_star", "x_star_o", "x_check", "x_lock", "x_cog", "x_usb", "x_plus", "x_minus", "x_undo", "x_redo",
               "x_warn", "x_folder", "x_doc", "x_back", "x_down", "x_up", "x_left", "x_right",
               "x_mixer", "x_pattern", "x_song", "x_motion", "x_motion_rec", "x_motion_del",
               "x_fx", "x_hpf", "x_repeat", "x_reverse", "x_tstop", "x_freeze"]

# the 24 px set: the header battery (Fukiai's battery is a wide, short glyph: at 16 px its body was 12 x 6)
# and the FX map's effect cells (ui_layer.c layer_fx: the icon alone, no name)
HUGE_DEFAULT = ["x_bat0", "x_bat1", "x_bat3", "x_bat4", "x_bat_chg", "x_usb",   # (+ the USB-C plug: as flat)
                "x_repeat", "x_reverse", "cutoff", "x_hpf", "x_tstop", "x_freeze", "x_oct_up", "x_oct_dn"]

# the 16 px set: header, footer, dialogs and menu rows (everything else only exists at 12 px)
BIG_DEFAULT = ["x_play", "x_stop", "x_pause", "x_rec", "x_rec_o", "x_trk1", "x_trk2", "x_trk3", "x_trk4",
               "x_trk1_o", "x_trk2_o", "x_trk3_o", "x_trk4_o", "x_usb", "x_star", "x_star_o", "x_check",
               "x_cog", "x_power", "x_warn", "x_palette", "x_speaker", "x_info", "x_hugelton", "x_doc",
               "wave", "algorithm", "phase", "bits", "sample", "mouth", "trio", "drawbar", "slice", "grain",
               "phys", "drum", "noise", "mod", "tempo", "tape",
               "x_song", "x_motion_del", "x_doctor", "x_fx", "x_timer", "x_bat0", "x_bat1", "x_bat3", "x_bat4", "x_bat_chg"]


def glyph_table(font_path):
    from fontTools.ttLib import TTFont
    f = TTFont(str(font_path))
    return {name: cp for cp, name in f.getBestCmap().items()}


def resolve(glyph, cps):
    """a glyph name, or a tuple of them: the first one the font has (the last one if none, reported missing)"""
    if isinstance(glyph, tuple):
        return next((g for g in glyph if g in cps), glyph[-1])
    return glyph


def fitted_font(font_path, out_dir, names):
    """font_path, or a copy with the FIT_TO glyphs among names mended (see FIT_TO)"""
    from fontTools.pens.recordingPen import RecordingPen
    from fontTools.pens.ttGlyphPen import TTGlyphPen
    from fontTools.ttLib import TTFont
    f = TTFont(str(font_path), recalcBBoxes=False)     # (the copy keeps every other glyph's stored header
    flags = f["head"].flags                             #  and the head flags: they rasterise unchanged)
    upm, glyf, hmtx, gs = f["head"].unitsPerEm, f["glyf"], f["hmtx"], f.getGlyphSet()
    fixed = []

    def area(pts):
        return sum(pts[i][0] * pts[i - 1][1] - pts[i - 1][0] * pts[i][1] for i in range(len(pts))) / 2

    for g, ref in FIT_TO.items():
        if g not in names or g not in glyf.glyphs or hmtx[g][0] == upm:
            continue
        rec = RecordingPen()
        gs[g].draw(rec)
        contours, cur = [], []
        for op, args in rec.value:
            cur.append((op, args))
            if op in ("closePath", "endPath"):
                contours.append(cur)
                cur = []
        info = []
        for c in contours:
            pts = [p for _, args in c for p in args]
            xs, ys = [p[0] for p in pts], [p[1] for p in pts]
            info.append((area(pts), (min(xs), min(ys), max(xs), max(ys))))
        outer = max(range(len(info)), key=lambda i: abs(info[i][0]))
        sign = info[outer][0] > 0

        def near(p, q):
            return all(abs(u - v) <= 2 for u, v in zip(p, q))
        keep = [c for i, c in enumerate(contours)
                if not ((info[i][0] > 0) == sign and i != outer and
                        any(j != i and (info[j][0] > 0) != sign and near(info[i][1], info[j][1]) for j in range(len(info))))]
        x0 = min(info[i][1][0] for i in range(len(info)))
        y0 = min(info[i][1][1] for i in range(len(info)))
        x1 = max(info[i][1][2] for i in range(len(info)))
        y1 = max(info[i][1][3] for i in range(len(info)))
        r = glyf[ref]
        stored = (r.xMin, r.yMin, r.xMax, r.yMax)
        r.recalcBounds(glyf)
        sx, sy = (r.xMax - r.xMin) / (x1 - x0), (r.yMax - r.yMin) / (y1 - y0)
        pen = TTGlyphPen(None)
        for c in keep:
            for op, args in c:
                getattr(pen, op)(*[(r.xMin + (x - x0) * sx, r.yMin + (y - y0) * sy) for x, y in args])
        r.xMin, r.yMin, r.xMax, r.yMax = stored
        new = pen.glyph()
        new.xMin, new.yMin, new.xMax, new.yMax = stored  # (as stored for its sibling)
        glyf[g] = new
        hmtx[g] = hmtx[ref]
        fixed.append((g, len(contours) - len(keep)))
    if not fixed:
        return str(font_path), fixed
    p = Path(out_dir) / (Path(font_path).stem + "_fitted.ttf")
    f["head"].flags = flags
    f.save(str(p))
    return str(p), fixed


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("out", nargs="?")
    ap.add_argument("--font", default=str(ROOT / "web" / "fukiai.ttf"))
    ap.add_argument("--json", default=str(ROOT / "assets" / "icons.json"), help="legacy icon names (order = enum)")
    ap.add_argument("--sizes", default="12,16,24")
    ap.add_argument("--big", default=",".join(BIG_DEFAULT))
    ap.add_argument("--gamma", type=float, default=1.0)
    ap.add_argument("--prefix", default="ICON_", help="enum prefix (the preview uses XI_ next to the firmware's ICON_)")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--report", action="store_true")
    a = ap.parse_args()
    cps = glyph_table(a.font)
    if a.list:
        for name, cp in sorted(cps.items(), key=lambda kv: kv[1]):
            print(f"U+{cp:04X} {name}")
        return
    if not a.out:
        ap.error("OUT.h is required")
    legacy = json.loads(Path(a.json).read_text())["names"]
    names = list(legacy) + list(EXTRA)
    glyph_of = {n: resolve(g, cps) for n, g in {**LEGACY, **EXTRA}.items()}
    missing = [n for n in legacy if n not in LEGACY]
    bad = [f"{n} -> {g}" for n, g in glyph_of.items() if g not in cps]
    if missing or bad:
        print("unmapped legacy names:", missing, "\nglyphs not in the font:", bad)
        raise SystemExit(1)
    font, fixed = fitted_font(a.font, Path(a.out).parent, set(glyph_of.values()))
    for g, dropped in fixed:
        print(f"icons: {g} is off the em in {Path(a.font).name}: fitted to {FIT_TO[g]}'s box"
              + (f", {dropped} filled counter copy dropped" if dropped else ""))
    for n, g in {**LEGACY, **EXTRA}.items():
        if isinstance(g, tuple) and glyph_of[n] != g[0]:
            print(f"icons: {n}: {g[0]} not in the font yet, using {glyph_of[n]}")
    mended = {g for g, _ in fixed}                     # (only those come from the mended copy)
    sizes = [int(s) for s in a.sizes.split(",")]
    big = [b for b in a.big.split(",") if b]
    out = [f"/* generated by tools/gen_aa_icons.py from {Path(a.font).name} (Fukiai, MIT, (c) 2026 Hügelton Instruments) */",
           "#pragma once", "#include <stdint.h>", "enum {"]
    out += [f"    {a.prefix}{n.upper()}," for n in names]
    out += [f"    {a.prefix}COUNT", "};", ""]
    cost = {}
    for s in sizes:
        subset = ([n for n in names if n in legacy or n in SMALL_EXTRA] if s == min(sizes) else
                  [n for n in names if n in HUGE_DEFAULT] if s == 24 else [n for n in names if n in big])
        data, idx = [], [0xFF] * len(names)
        for n in subset:
            b, ink = ar.raster_icon(font if glyph_of[n] in mended else a.font, s, cps[glyph_of[n]], a.gamma)
            assert ink, f"{n} is empty at {s}px"
            idx[names.index(n)] = len(data)
            data.append(b)
        bpi = (s * s + 1) // 2
        flat = [v for b in data for v in b]
        out.append(f"#define AI{s}_N {len(data)}")
        out.append(ar.c_array(f"AI{s}_DATA", "uint8_t", flat, 24, "0x{:02x}"))
        out.append(ar.c_array(f"AI{s}_IDX", "uint8_t", idx, 24))
        out.append("")
        cost[s] = (len(data), len(flat) + len(idx))
    Path(a.out).write_text("\n".join(out))
    tot = sum(c[1] for c in cost.values())
    for s, (n, b) in cost.items():
        print(f"icons {s}px: {n} cells, {b} B")
    print(f"icons total {tot} B -> {a.out}   ({len(legacy)} legacy names + {len(EXTRA)} new)")
    if a.report:
        for n in names:
            print(f"  {a.prefix}{n.upper():12s} {glyph_of[n]}")


if __name__ == "__main__":
    main()
