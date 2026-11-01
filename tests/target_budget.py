#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
"""Target-side cost estimate of the hot DSP code, from the pi32v2 objdump listing tools/build.py writes
(build/felucca.dis). For each engine's render function and the audio ISR: the instructions
inside loops (every address range closed by a backward branch: the per-sample loops and what they
contain), and the hardware divides and calls in them. cost = the loop instructions, each weighted 4 per
level of nesting (an inner loop runs several times per sample) and 1 + 8 for a divide (many cycles). A static count, not a cycle count: it changes only when the compiled code changes,
so it is exact run to run, and it catches a render loop that grew (more work per sample, a new divide,
something not inlined any more). Compared with tests/target_budget.txt at +10 %. The host
CPU check (regress.c) is the primary one; this one sees the target compiler's code.
  tests/target_budget.py [DIS [BUDGET]]      BUDGET_UPDATE=1 rewrites BUDGET"""
import os
import re
import sys

FUNCS = ["analog_render", "digital_render", "digital_render_legacy", "digital_render_custom", "phase_render", "lofi_render", "sample_render", "formant_render",
         "trio_render", "trio_pass", "wheel_render", "wheel_block",
         "grain_render", "grain_block", "grain_tick",     # GRAIN (eng_grain.c, ChoralRoot 0.22): the voice, the part, the ISR's tick
         "phys_render", "drum_render", "noise_render", "fm6_render", "px_modal_block", "px_modal_run", "px_memb_block",
         "px_string_excite", "px_string_run", "px_symp_run",
         "dv_metal_run", "dv_kick_run", "dv_snare_run", "dv_clap_run", "dv_hat_run", "dv_tom_run",   # drum_voice.c
         "dv_rim_run", "dv_bell_run", "dv_cym_run", "dv_out",
         "slicer_track",
         "slice_render", "slc_rev",                          # SLICE (eng_slice.c): the render, the reverse windows
         "va_render", "va_block",                            # VA (eng_va.c, ChoralRoot): the voice, the part's LFOs
         "cz_native_render", "cz_native_line", "cz_native_line_mv", "cz_native_noise", "cz_env_tick",   # CZ-1 (cz_native.c, Melodee's): the voice, a line's loops (one per wave and window; _mv: the same with the DCW depth ramped a sample at a time, for the blocks in which it changes), line 2 with NOISE, envelopes
         "quad_render", "quad_block",                        # FM TONE (eng_quad.c, code name QUAD, ChoralRoot): the voice, the part's macros
         "quad_drv_run", "quad_comb_lend",                   # .. DRIVE PRE's loop (3y213), the combs' lines (3y214: a comb's blocks only)
         # PROPHET (eng_prophet_test.c, Melodee's Prophet-5): the sample loop (one loop with every switch of a program
         # in it: tools/p5_cost.py walks it per program for the unit's figure; this weighs it whole, to catch growth),
         # a voice's block (its set-up, the fade of a budget victim), the part's wheel source and its DC blocker, the
         # part's block (p5_env_tick has no loop: nothing to weigh)
         "p5_samples", "p5_render", "p5_mod_samples", "p5_post", "p5_block",
         "fm1_alnk0_irq", "fm1_timer5_irq",               # the audio ISR; TIMER5: the key / LED scan (hal/fm1_input.h)
         # TIMER5's USB work, each a function of its own since 0.19 (usb.c USB_OUTLINE / USB_COLD), so that
         # fm1_timer5_irq is what every 100 us tick runs: the scan, the clock, the UART's bytes. Inlined there, the
         # control transfers (ep0_service) and the bus reset were two thirds of its instructions, and each branch
         # back from one of their tails was a "loop" around the rest, x4 a level: the figure went 45994 -> 58635 ->
         # 68460 over two merges that added no instruction to any path a tick takes. Watched instead: usb_poll (2 kHz,
         # never nested in the render; its two spans are the returns from the cold calls, not loops), ep1_rx (a MIDI
         # packet from the host), ua_service_paced (the recording's endpoint, every tick; its loop is the frame number
         # read twice) and ua_transmit (a packet's frames out of the ring, once a ms while a stream is open: the one
         # real per-sample loop of the four). Not watched: ua_tx_fill (no loop: nothing to count), ep0_service and
         # usb_bus_reset (they run on a cable or a host's request, and a budget on code laid out as one span guards
         # nothing)
         "usb_poll", "ep1_rx", "ua_service_paced", "ua_transmit",
         "mod_begin", "mod_voice", "mod_end",                 # the modulation matrix (mod.c), called when active
         "master_block",                                    # the master (fx.c): a loop per LOWCUT / limiter setting
         "mix_loop", "mix_loop_f", "mix_loop_s", "mix_loop_sf",   # a part into the mix (fx.c mix_part_out): plain, with the
         "mix_loop_u", "mix_loop_fu", "mix_loop_su", "mix_loop_sfu",   # sends (f), the stereo side (s), USB's tap (u)
         "perf_begin", "perf_mute", "perf_pre", "perf_block", "perf_master",   # the FX layer (perform.c), when busy
         "rev_room", "rev_spring",                # the reverb bus (fx.c): REVERB TYPE ROOM / SPRING (Felucca's: CR_FX 0)
         "xf_block", "xf_down", "xf_up", "xd_run", "xr_run",   # ChoralRoot's delay and reverb (cr_fx.c, CR_FX 1): the
         "xf_prep", "xf_delay", "xf_reverb", "mix_fx_split", "c1_fx_job"]   # (3y50: xf_block in three; the reverb on core 1)
                                                  # block, the half-band filters (a send down, the wet up), the delay's
                                                  # loop, the reverb's (xd_run, xr_run: a pass is one half-rate sample)
# built only with FELUCCA_FM4=1 (DIGITAL, src/eng_digital.c; not in the default build, so not in BUDGET): absent,
# they are skipped; present, checked against these (their budget lines until the engine was retired in 1.0)
OPTIONAL = {"digital_render": 12, "digital_render_legacy": 333, "digital_render_custom": 558,
            # ChoralRoot builds without the SLICE engine and the SLICER insert (choralroot.c FELUCCA_SLICE 0,
            # FELUCCA_SLICER 0): absent from build/choralroot.dis, checked when built (felucca.c)
            "slicer_track": 632, "slice_render": 453, "slc_rev": 120,
            # .. and without SAMPLE, GRAIN, DRUM (choralroot.c FELUCCA_SAMPLE / GRAIN / DRUM 0: all-synth)
            # .. and without ANALOG (choralroot.c FELUCCA_ANALOG 0, 0.14: the VA replaces it)
            "analog_render": 581,
            # .. and since 0.18 without Felucca's ROOM / SPRING reverb (choralroot.c CR_FX 1: cr_fx.c's instead)
            "rev_room": 218, "rev_spring": 126,
            # .. and since 0.18 without PHASE, LOFI, VOICE, TRIO, NOISE (choralroot.c FELUCCA_PHASE / LOFI / VOICE / TRIO /
            # NOISE 0): their budget lines until then
            "phase_render": 77, "lofi_render": 123, "formant_render": 312, "trio_render": 341, "trio_pass": 175,
            "noise_render": 259,
            # .. and without PHYS (choralroot.c FELUCCA_PHYS 0, 0.18): its budget lines until then
            "phys_render": 61, "px_modal_block": 111, "px_modal_run": 192, "px_memb_block": 106, "px_string_excite": 171,
            "px_string_run": 264, "px_symp_run": 252,
            # .. and since 0.18 without the FX hold layer (choralroot.c FELUCCA_PERFORM 0): their budget lines until then
            "perf_begin": 34, "perf_mute": 19, "perf_pre": 27, "perf_block": 813, "perf_master": 31,
            "sample_render": 126, "grain_render": 1394, "grain_block": 139, "grain_tick": 35, "drum_render": 86, "dv_metal_run": 26,
            "dv_kick_run": 153, "dv_snare_run": 158, "dv_clap_run": 74, "dv_hat_run": 61, "dv_tom_run": 75,
            "dv_rim_run": 61, "dv_bell_run": 52, "dv_cym_run": 67, "dv_out": 35}
TOL = 0.10                      # exact (no noise): small edits pass, a grown render loop does not
DIV_W = 8                       # a divide weighs 1 + 8 instructions
NEST = 4                        # an instruction in a loop inside a loop weighs 4, two deep 16, ...
MAXD = 4

dis = sys.argv[1] if len(sys.argv) > 1 else "build/felucca.dis"
budget = sys.argv[2] if len(sys.argv) > 2 else "tests/target_budget.txt"
LABEL = re.compile(r"^([A-Za-z_][A-Za-z_0-9.]*):$")
INSN = re.compile(r"^\s*([0-9a-f]+):\s+((?:[0-9a-f]{2} )+)\s*(.*)$")
TARGET = re.compile(r"goto -?\d+ <[^>]*: ([0-9a-f]+) >")


def functions(path):
    """{name: [(addr, text)]}: a function runs to the next label not starting with '.' (the compiler's
    jump-table labels are inside functions); jump-table data ('< n : 0x.. >') is left out"""
    out, cur = {}, None
    with open(path) as f:
        for line in f:
            m = LABEL.match(line.strip())
            if m:
                name = m.group(1)
                if not name.startswith("."):
                    cur = out.setdefault(name, []) if name in FUNCS else None
                continue
            m = INSN.match(line)
            if cur is not None and m and not m.group(3).lstrip().startswith("<"):
                cur.append((int(m.group(1), 16), m.group(3).strip()))
    return out


def cost(insns):
    """loops: one span per loop head (the farthest backward branch to it); an instruction inside d
    spans weighs NEST ** (d - 1) (an inner loop runs several times per pass of the outer one)"""
    lo = insns[0][0]
    heads = {}
    for a, t in insns:
        m = TARGET.search(t)
        if m and lo <= int(m.group(1), 16) < a:
            h = int(m.group(1), 16)
            heads[h] = max(heads.get(h, a), a)
    n = divs = calls = 0
    w = 0
    for a, t in insns:
        d = min(sum(1 for h, e in heads.items() if h <= a <= e), MAXD)
        if not d:
            continue
        k = NEST ** (d - 1)
        n += 1
        if re.search(r"= r\d+ / r\d+", t):
            divs += 1
            k *= 1 + DIV_W
        calls += t.startswith("call")
        w += k
    return {"insns": len(insns), "loop": n, "div": divs, "call": calls, "cost": w}


def main():
    if not os.path.exists(dis):
        print(f"target: skip ({dis} missing: run ./build.sh)")
        return 0
    fns = functions(dis)
    res = {n: cost(fns[n]) for n in FUNCS if fns.get(n)}
    missing = [n for n in FUNCS if n not in res and n not in OPTIONAL]
    base = {}
    if os.path.exists(budget):
        for line in open(budget):
            p = line.split()
            if len(p) >= 2 and not line.startswith("#"):
                base[p[0]] = int(p[1])
    if os.environ.get("BUDGET_UPDATE"):
        with open(budget, "w") as f:
            f.write("# FELUCCA target cost budget (tests/target_budget.py): instructions in the loops of each\n"
                    f"# function in build/felucca.dis, x{NEST} per nesting level, divides x{1 + DIV_W}. The check allows "
                    f"+{TOL * 100:.0f} %.\n# Rewritten by BUDGET_UPDATE=1.\n")
            for n, r in res.items():
                if n not in OPTIONAL:
                    f.write(f"{n} {r['cost']}\n")
        print(f"target: budget {budget} rewritten ({len(res)} functions)")
    fail = 0
    for n in missing:
        print(f"target: FAIL {n} not found in {dis} (renamed? inlined? update FUNCS)")
        fail += 1
    for n, r in res.items():
        b = base.get(n, OPTIONAL.get(n))
        state = "no budget (BUDGET_UPDATE=1 adds it)" if b is None else "ok"
        if b is not None and not os.environ.get("BUDGET_UPDATE"):
            if r["cost"] > b * (1 + TOL):
                state = (f"OVER BUDGET (+{(r['cost'] / b - 1) * 100:.0f} %, limit +{TOL * 100:.0f} %)" if b else
                         "OVER BUDGET (the budget is 0: no loop)")
                fail += 1
            elif r["cost"] < b * (1 - TOL):
                state = f"note: {(r['cost'] / b - 1) * 100:.0f} % (BUDGET_UPDATE=1 to keep it)"
        print(f"target: {n:15s} {r['insns']:5d} instructions, {r['loop']:4d} in loops, {r['div']} divides, "
              f"{r['call']:2d} calls there: cost {r['cost']:5d} (budget {b if b is not None else '-'}) {state}")
    return 1 if fail else 0


if __name__ == "__main__":
    sys.exit(main())
