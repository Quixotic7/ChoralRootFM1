/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* DIGITAL -> FM6 without a divide by zero (#61; the fix after PR #111 by spinkham): src/fm4_convert.c.
 * fm4_convert's two loop-invariant divides (ENV -> FLT's attack: t_att x FLT / (INDEX + FLT), and the sustain's
 * index: isus x 32767 / ipk) were guarded by tests the JieLi compiler moved them ahead of; INDEX + FLT ENV == 0
 * (0.9's ORGAN: INDEX 0) divided by 0 and trapped in persist_boot. They are now computed once, unguarded, with
 * divisors that are never 0. Built with -fsanitize=integer-divide-by-zero (run_tests.sh): a divide by 0 anywhere
 * on the way aborts the test. (The host compiler does not move a divide ahead of its test, so the old code passes
 * that too: the target's code is checked in its disassembly, not here.)
 * 1. the same values: fm4_convert against the guarded form as it was (fm4_convert_ref below, as 1.0.3 had it):
 *    the patch, the returned FM6 preset and the values it leaves in p, over every INDEX x FLT ENV x a SUS / ATK
 *    grid (every ALG, op envelopes varied), then random values over every parameter's whole range.
 * 2. a 0.9 project (FUN3, as 0.9-beta stored it) with DIGITAL tracks (ORGAN, then each other DIGITAL preset)
 *    imports as FM6 with the patches the guarded form gives.
 * Run by tests/run_tests.sh (needs build/gen from one firmware build). */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1
#include "../firmware/src/project.c"
#if FELUCCA_FM4
#error "fm4_div0_test converts DIGITAL sounds: build it without FELUCCA_FM4"
#endif

/* fm4_convert as it was before #61's fix (1.0.3): the reference for the values */
static uint32_t fm4_convert_ref(int16_t *p, uint8_t *v)
{
    uint32_t alg = (uint32_t)p[P_E0] & 7u, car, k, near = fm4_nearest(&p[P_E0]);
    int32_t idx = clamp(p[P_E4], 0, 127), fenv = clamp(p[P_ED_FLT], -64, 63), sus = clamp(p[P_SUS], 0, 127);
    int32_t ipk = clamp(idx + fenv, 0, 127), isus = clamp(idx / 4 + fenv * sus / 127, 0, 127), fb = FM4_MUTE;
    int32_t t_att = fm4_samples(p[P_ATK]), t_mod = fm4_samples(p[P_E5]);
    const char *name = 0;
    memset(v, 0, FP_SIZE + 1u);
    for (k = 0; k < 6u; k++) {                         /* every operator silent, as a start */
        uint8_t *o = v + k * FP_OP;
        o[FP_R1] = o[FP_R1 + 1] = o[FP_R1 + 2] = o[FP_R1 + 3] = 99;
        o[FP_BP] = 39;
        o[FP_FC] = 1;
        o[FP_DET] = 7;
    }
    v[FP_ALG] = FM4_ALG[alg].alg;
    car = fm6_carriers(v[FP_ALG]);
    for (k = 0; k < 4u; k++) {
        const int16_t *e = &p[P_FM1_ATK + k * 5u];
        uint32_t slot = 6u - FM4_ALG[alg].op[k], share = FM4_ALG[alg].share[k], r;
        uint8_t *o = v + slot * FP_OP;
        int flat = !e[0] && !e[1] && e[2] == 127 && !e[3];
        uint32_t lvl = (uint32_t)clamp(e[4], 0, 127) * 258u;             /* Q15 */
        uint32_t osus = flat ? 32767u : (uint32_t)clamp(e[2], 0, 127) * 258u;
        int32_t t_odec = flat || e[2] == 127 ? 0 : e[1] ? fm4_samples(e[1]) : 1;
        int32_t t_orel = flat ? 0x7FFFFFFF : e[3] ? fm4_samples(e[3]) : 0;
        int32_t ta, td, tr, ms_s;
        r = k ? (uint32_t)p[P_E1 + k - 1u] % 15u : 1u;                  /* .5 1 2 .. 12 14 16 */
        o[FP_FC] = (uint8_t)(k ? r == 0u ? 0u : r <= 12u ? r : r == 13u ? 14u : 16u : 1u);
        if ((car >> slot) & 1u) {                      /* a carrier: the master envelope x the op's */
            uint32_t s = (uint32_t)sus * 258u;
            o[FP_OL] = (uint8_t)fm4_outlevel(fm4_ms(lvl / share) + 256 - 96);
            o[FP_KVS] = 3;
            ta = t_att;
            if (!flat && e[0] && fm4_samples(e[0]) > ta)
                ta = fm4_samples(e[0]);
            s = (s * osus) >> 15;
            td = sus < 127 ? fm4_samples(p[P_DEC]) : 0;
            td = td > t_odec ? td : t_odec;
            tr = fm4_samples(p[P_REL]);
            if (!flat)
                tr = t_orel ? ((tr >> 5) * (t_orel >> 5) / ((tr >> 5) + (t_orel >> 5) + 1)) << 5 : 0;
            ms_s = fm4_ms(s);
            o[FP_R1] = (uint8_t)fm4_rate(ta * 100 / 146);
            o[FP_L1] = 99;
            o[FP_L1 + 1] = o[FP_L1 + 2] = (uint8_t)fm4_level(ms_s);
            o[FP_R1 + 1] = (uint8_t)fm4_fall(td, ms_s == FM4_MUTE ? FM4_MUTE : -ms_s);
            o[FP_R1 + 2] = 99;
            o[FP_R1 + 3] = (uint8_t)fm4_rate(tr * 100 / 664);
            o[FP_L1 + 3] = 0;
            if (k == 3u && p[P_E6] && o[FP_OL]) {      /* op 4 a carrier (ALG 8): feedback on its output (sustain) */
                int32_t need = fm4_ms((uint32_t)p[P_E6] * 258u * lvl >> 15) + 256;
                int32_t out = (fm6_scaleout(o[FP_OL]) << 5) - 4064 + (ms_s == FM4_MUTE ? -2048 : ms_s);
                if (p[P_E6] >= FM4_FB_NOISE && ms_s != FM4_MUTE && out < need) {   /* noisy: held up (louder) */
                    int32_t lift = need - out;
                    out += lift > FM4_FB_LIFT ? FM4_FB_LIFT : lift;
                    o[FP_OL] = (uint8_t)fm4_outlevel(out - ms_s);
                    out = (fm6_scaleout(o[FP_OL]) << 5) - 4064 + ms_s;
                }
                fb = need - 256 - out;
            }
        } else {                                       /* a modulator: INDEX through MODDEC x the op's envelope */
            uint32_t a = (uint32_t)ipk * lvl / 127u / share;
            uint32_t m = ipk ? ((uint32_t)isus * 32767u / (uint32_t)ipk * osus) >> 15 : 0u;
            int32_t ms_a = fm4_ms(a);
            ms_s = fm4_ms(m);
            if (k == 3u && p[P_E6] && a) {             /* op 4's feedback (FM6: its deviation is the op's output x
                                                        * 2^(FB - 8); DIGITAL's does not follow the index) */
                int32_t need = fm4_ms((uint32_t)p[P_E6] * 258u * lvl >> 15) + 256;   /* the output FB 7 wants */
                if (p[P_E6] >= FM4_FB_NOISE && ms_s != FM4_MUTE && ms_a + ms_s < need) {
                    int32_t lift = need - (ms_a + ms_s), t;      /* DIGITAL's noisy feedback: FM6 gets there only
                                                                  * with the op held up (more index: the price) */
                    t = ms_a + ms_s + (lift > FM4_FB_LIFT ? FM4_FB_LIFT : lift);
                    if (t <= ms_a) {
                        ms_s = t - ms_a;
                    } else {
                        ms_s = 0;
                        ms_a = t > 0 ? 0 : t;
                    }
                }
                fb = need - 256 - (ms_a + (ms_s == FM4_MUTE ? -2048 : ms_s));        /* at the sustain */
            }
            o[FP_OL] = (uint8_t)fm4_outlevel(ms_a);
            o[FP_KVS] = a ? 1 : 0;
            ta = fenv > 0 ? t_att * fenv / (idx + fenv) : 0;
            if (!flat && e[0] && fm4_samples(e[0]) > ta)
                ta = fm4_samples(e[0]);
            td = ms_s < 0 ? (isus < ipk ? t_mod : 0) : 0;
            td = td > t_odec ? td : t_odec;
            o[FP_R1] = (uint8_t)fm4_rate(ta * 100 / 146);
            o[FP_L1] = 99;
            o[FP_L1 + 1] = o[FP_L1 + 2] = (uint8_t)fm4_level(ms_s);
            o[FP_R1 + 1] = (uint8_t)fm4_fall(td, ms_s == FM4_MUTE ? FM4_MUTE : -ms_s);
            o[FP_R1 + 2] = 99;
            if (flat) {                                /* the index keeps falling after the key */
                o[FP_R1 + 3] = o[FP_R1 + 1];
                o[FP_L1 + 3] = o[FP_L1 + 2];
            } else {
                o[FP_R1 + 3] = (uint8_t)(t_orel ? fm4_rate(t_orel * 100 / 664) : 99u);
                o[FP_L1 + 3] = 0;
            }
        }
    }
    fb += 8 * 256 + 128;                               /* FM6 feedback: deviation = output x 2^(FB - 8) */
    if (p[P_E6] && fb > 0)
        v[FP_FB] = (uint8_t)(fb / 256 > 7 ? 7 : fb / 256);
    for (k = 0; k < 4u; k++) {
        v[FP_PR1 + k] = 99;
        v[FP_PL1 + k] = 50;
    }
    v[FP_OKS] = 1;
    v[FP_LFS] = 35;
    v[FP_TRNSP] = 24;
    for (k = 0; k < FM4_NPRESETS && !name; k++) {      /* a DIGITAL preset as it is: its name */
        const preset_t *pr = &DIGITAL_PRESETS[k];
        uint32_t i, same = pr->env[0] == p[P_ATK] && pr->env[1] == p[P_DEC] && pr->env[2] == p[P_SUS] &&
                           pr->env[3] == p[P_REL];
        for (i = 0; i < 7u; i++)
            same &= pr->e[i] == p[P_E0 + i];
        if (same)
            name = pr->name;
    }
    for (k = 0; k < 10u; k++)
        v[FP_NAME + k] = ' ';
    if (name) {
        for (k = 0; k < 10u && name[k]; k++)
            v[FP_NAME + k] = (uint8_t)name[k];
    } else {
        memcpy(v + FP_NAME, "4OP ALG", 7);
        v[FP_NAME + 8] = (uint8_t)('1' + alg);
    }
    fm6_sanitize(v);
    for (k = 0; k < 7u; k++)                           /* FM6's macros: the patch as it is */
        p[P_E0 + k] = 0;
    p[P_E7] = FM4_TO_FM6[near];
    for (k = P_FM1_ATK; k <= P_FM4_LEVEL; k++)
        p[k] = (k - P_FM1_ATK) % 5u == 2u || (k - P_FM1_ATK) % 5u == 4u ? 127 : 0;
    return FM4_TO_FM6[near];
}

static int bad;
static void check(const char *what, int ok)
{
    printf("fm4 div0: %-100s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static uint32_t seed = 0x2468ACE1u;
static uint32_t rnd(void) { seed ^= seed << 13; seed ^= seed >> 17; seed ^= seed << 5; return seed; }
static int16_t rnd_in(uint32_t i)                  /* a value in parameter i's range */
{
    const param_desc_t *d = param_desc_of(ENGI_DIGITAL, i);
    return (int16_t)(d->min + (int32_t)(rnd() % (uint32_t)(d->max - d->min + 1)));
}

/* one input: both forms on copies of p -> 1 if the patch, the result and p afterwards are the same */
static uint32_t n_cmp, n_zero;
static int same(const int16_t *p)
{
    int16_t a[P_COUNT], b[P_COUNT];
    uint8_t va[FP_SIZE + 1u], vb[FP_SIZE + 1u];
    int32_t idx = clamp(p[P_E4], 0, 127), fenv = clamp(p[P_ED_FLT], -64, 63);
    uint32_t ra, rb;
    memcpy(a, p, sizeof a);
    memcpy(b, p, sizeof b);
    ra = fm4_convert(a, va);
    rb = fm4_convert_ref(b, vb);
    n_cmp++;
    n_zero += idx + fenv == 0;                     /* the old code's moved divides divided by 0 here */
    return ra == rb && !memcmp(va, vb, sizeof va) && !memcmp(a, b, sizeof a);
}

static void defaults(int16_t *p)
{
    uint32_t i;
    for (i = 0; i < P_COUNT; i++)
        p[i] = param_desc_of(ENGI_DIGITAL, i)->def;
}

int main(void)
{
    static const int16_t SUS[] = {0, 1, 2, 31, 63, 64, 100, 126, 127};
    static const int16_t ATK[] = {0, 1, 2, 40, 64, 100, 127};
    int16_t p[P_COUNT];
    uint32_t i, k, s, a, diff = 0;
    int32_t idx, fenv;

    /* 1. the same values */
    for (idx = 0; idx <= 127; idx++)
        for (fenv = -64; fenv <= 63; fenv++)
            for (s = 0; s < NELEM(SUS); s++)
                for (a = 0; a < NELEM(ATK); a++) {
                    defaults(p);
                    p[P_E0] = (int16_t)((idx + fenv + 64 + (int32_t)a) & 7);
                    p[P_E4] = (int16_t)idx;
                    p[P_ED_FLT] = (int16_t)fenv;
                    p[P_SUS] = SUS[s];
                    p[P_ATK] = ATK[a];
                    if ((idx + (int32_t)a) & 1)            /* half of them with the op envelopes varied */
                        for (i = P_FM1_ATK; i <= P_FM4_LEVEL; i++)
                            p[i] = rnd_in(i);
                    p[P_E5] = rnd_in(P_E5);
                    p[P_E6] = (int16_t)(s & 1u ? rnd_in(P_E6) : 0);
                    diff += !same(p);
                }
    check("every INDEX x FLT ENV x a SUS / ATK grid (every ALG): the same patch, preset and values as before", !diff);
    printf("    %u inputs, %u with INDEX + FLT ENV == 0 (the moved divides' divisor 0)\n", n_cmp, n_zero);
    diff = n_cmp = n_zero = 0;
    for (k = 0; k < 400000u; k++) {
        for (i = 0; i < P_COUNT; i++)
            p[i] = rnd_in(i);
        if (k & 1u)                                    /* the flat default op envelope (0 0 127 0) half the time */
            for (i = 0; i < 4u; i++) {
                p[P_FM1_ATK + i * 5u] = p[P_FM1_ATK + i * 5u + 1u] = p[P_FM1_ATK + i * 5u + 3u] = 0;
                p[P_FM1_ATK + i * 5u + 2u] = 127;
            }
        if ((k & 6u) == 2u)                            /* and INDEX + FLT ENV == 0 often */
            p[P_ED_FLT] = (int16_t)-clamp(p[P_E4], 0, 64);
        diff += !same(p);
    }
    check("random values over every parameter's range: the same as before", !diff);
    printf("    %u inputs, %u with INDEX + FLT ENV == 0\n", n_cmp, n_zero);

    /* 2. 0.9 projects with DIGITAL tracks (FUN3: P_E0 at 49, no OP ENV values or matrix yet) */
    for (k = 0; k < FM4_NPRESETS; k += NTRK) {
        static project_v3_t v3;
        static project_t q;
        int16_t full[NTRK][P_COUNT], ref[P_COUNT];
        uint8_t v[FP_SIZE + 1u], pk[FM6_PACKED];
        uint32_t t, pr, n;
        int ok;
        memset(&v3, 0, sizeof v3);
        v3.magic = PROJ_MAGIC_V3;
        v3.size = sizeof v3;
        for (i = 0; i < G_COUNT; i++)
            v3.g[i] = GP[i].def;
        v3.parts = NPART;
        for (t = 0; t < NTRK; t++) {                   /* ORGAN PAD MARIMBA FUNK KEY, then E.PIANO BELL BASS BRASS */
            n = (k + t + 4u) % FM4_NPRESETS;
            defaults(full[t]);
            fm4_preset_values(full[t], n);
            for (i = 0; i <= P_SLDEPTH; i++)
                v3.t[t].p[i] = full[t][i];
            for (i = 0; i < 8u; i++)
                v3.t[t].p[49u + i] = full[t][P_E0 + i];
            v3.t[t].engine = ENGI_DIGITAL;
            v3.t[t].preset = (uint8_t)n;
        }
        v3.sum = proj_hash(&v3, sizeof v3 - 4u);
        ok = sizeof v3 == 2584u && proj_import(&q, &v3, (int)sizeof v3) && proj_ok(&q);
        for (t = 0; t < NTRK && ok; t++) {
            memcpy(ref, full[t], sizeof ref);
            pr = fm4_convert_ref(ref, v);
            fm6_pack(v, pk);
            ok = q.t[t].engine == ENGI_FM6 && q.t[t].preset == pr && pr == FM4_TO_FM6[(k + t + 4u) % FM4_NPRESETS] &&
                 !memcmp(q.fm6[t], pk, FM6_PACKED) && !memcmp(q.t[t].p, ref, sizeof ref);
        }
        if (!k)
            check("ORGAN: INDEX + FLT ENV is 0 (#61's divisor)", str_eq(DIGITAL_PRESETS[4].name, "ORGAN") &&
                  full[0][P_E4] == 0 && full[0][P_ED_FLT] == 0);
        check(k ? "a 0.9 project (FUN3), DIGITAL E.PIANO BELL BASS BRASS: FM6, the same patches as before"
                : "a 0.9 project (FUN3), DIGITAL ORGAN PAD MARIMBA FUNK KEY: FM6, the same patches as before", ok);
    }
    return bad ? 1 : 0;
}
