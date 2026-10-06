/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
#define main hostsim_main
#include "hostsim.c"
#undef main
static int fails;
static void check(const char *label, int ok) { printf("FM/sample: %s %s\n", label, ok ? "ok" : "FAIL"); fails += !ok; }
static uint64_t energy(const int32_t *b) { uint64_t e = 0; for (uint32_t i = 0; i < CTL; i++) e += (int64_t)b[i] * b[i]; return e; }
int main(void)
{
    int32_t a[CTL], b[CTL], g[4];
    track_t *t = &trk[0];
    voice_t *v = &t->v[0], *v2 = &t->v[1];
    vmod_t m = {0};
    memset(trk, 0, sizeof trk); host_tracks_init();
    for (uint32_t i = 0; i < NENGINES; i++) if (ENGINES[i] == &ENG_DIGITAL) host_preset(t, i, 0);
    m.inc = pitch_inc(60 * 16); m.amp0 = m.amp1 = m.envq15 = 32767; m.shape = 64 << 8;
    v->vel = 96; v->note = 60; v->gate = 1;
    int same = 1;
    for (uint32_t alg = 0; alg < 8; alg++) {
        t->p[P_E0] = alg; digital_note_on(t, v); *v2 = *v;
        memset(a, 0, sizeof a); memset(b, 0, sizeof b);
        digital_render(t, v, a, CTL, &m); digital_render_legacy(t, v2, b, CTL, &m);
        same &= !memcmp(a, b, sizeof a);
    }
    check("all eight default algorithms are bit-identical", same);
    t->p[P_E0] = 7;
    for (uint32_t op = 0; op < 4; op++) t->p[P_FM1_LEVEL + op * 5] = 0;
    digital_note_on(t, v); memset(a, 0, sizeof a); digital_render(t, v, a, CTL, &m);
    check("muting all operators silences every carrier", energy(a) == 0);
    t->p[P_FM1_LEVEL] = 127; digital_note_on(t, v); memset(a, 0, sizeof a); digital_render(t, v, a, CTL, &m);
    check("one carrier alone produces sound", energy(a) > 0);
    v->gate = 0; memset(a, 0, sizeof a); digital_render(t, v, a, CTL, &m);
    check("level-only edits preserve master release", energy(a) > 0);
    v->gate = 1; t->p[P_FM1_ATK] = 80; t->p[P_FM1_DEC] = 30; t->p[P_FM1_SUS] = 24; t->p[P_FM1_REL] = 40;
    digital_note_on(t, v); digital_op_tick(t, v, g); int32_t e0 = digital_env[0][0][0];
    digital_op_tick(t, v, g); check("attack rises from silence", e0 > 0 && digital_env[0][0][0] > e0);
    for (uint32_t i = 0; i < FS * 12 / CTL; i++) digital_op_tick(t, v, g);
    check("decay reaches operator sustain", abs(digital_env[0][0][0] - (24 << 17)) < 4000);
    v->gate = 0; e0 = digital_env[0][0][0]; digital_op_tick(t, v, g);
    check("operator release falls after note-off", digital_env[0][0][0] < e0);
    int isolated = 1; for (uint32_t op = 1; op < 4; op++) isolated &= digital_env[0][0][op] == 1 << 24;
    check("editing one envelope leaves other operators flat", isolated);
    memset(trk, 0, sizeof trk); host_tracks_init();
    t->p[P_E0] = smp_perc_set();
    int zones = 1;
    const uint8_t notes[] = {42,44,49};
    for (uint32_t i = 0; i < sizeof notes; i++) {
        v->note = notes[i]; sample_note_on(t, v); const smp_zone_t *z = smp_zone(v->s[4]);
        zones &= !v->s[6] && z->lo == notes[i] && z->hi == notes[i];
    }
    check("GM hats and crash outrank overlapping tom ranges", zones);
    return fails != 0;
}
