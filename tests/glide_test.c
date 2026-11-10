/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the glide (firmware/src/voice.c glide_step / glide_set / glide_voice; GitHub issue 46), the firmware's
 * sources through hostsim.c:   build/host/glide_test      (run_tests.sh builds and runs it)
 * 1. the screen: GLIDE shows 3 x F_TIME's time, 3 ms .. 30 s (F_GLIDE).
 * 2. the time is the one shown: TIME (any interval in it) and RATE (an octave in it), up to 30 s, within 2 %; the
 *    slide is smooth (Q16 steps: no 1/16-semitone stairs) and lands exactly.
 * 3. chord glide: POLY with GLIDE, a chord let go and the next one played: each new note takes the released voice
 *    nearest it and slides there (C E G -> D F A: the same three voices); with GLIDE 0 the old allocation.
 * 4. a held voice is never taken: a note added over a held chord gets its own voice. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int bad;
static void check(const char *what, int ok)
{
    printf("glide: %-74s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static int32_t out_buf[2 * CTL];
static void blocks(uint32_t n)
{
    while (n--)
        mix_block(out_buf, CTL);
}

static void fresh(uint32_t voice, uint32_t g, uint32_t mode)
{
    uint32_t k;
    memset(trk, 0, sizeof trk);
    memset(vsq, 0, sizeof vsq);
    host_tracks_init();
    for (k = 0; k < NPART; k++)
        host_preset(&trk[k], ENGI_VA, 0);
    trk[0].p[P_VOICE] = (int16_t)voice;
    trk[0].p[P_GLIDE] = (int16_t)g;
    trk[0].p[P_GLMODE] = (int16_t)mode;
    trk[0].p[P_AMODE] = 0;
    trk[0].p[P_SUS] = 127;
    trk[0].p[P_REL] = 90;                      /* (a release long enough to still sound at the chord change) */
}

/* control ticks from note a to the end of a legato slide to b (MONO), and whether it moved smoothly */
static uint32_t slide_ticks(uint32_t g, uint32_t mode, uint32_t a, uint32_t b, int *smooth)
{
    track_t *t = &trk[0];
    voice_t *v = &t->v[0];
    uint32_t n = 0;
    int32_t last;
    fresh(V_LEGATO, g, mode);
    trk_note_on(t, a, 100);
    blocks(4);
    trk_note_on(t, b, 100);
    last = v->gpos;
    *smooth = 1;
    while (v->pitch_cur != v->pitch16 && n < 200000u) {
        int32_t d;
        blocks(1);
        n++;
        d = v->gpos - last;
        last = v->gpos;
        if ((d < 0 ? -d : d) > v->gstep || (n > 1u && v->pitch_cur != v->pitch16 && (d < 0 ? -d : d) != v->gstep))
            *smooth = 0;                                /* the same step each tick (a slow one well under 1/16 st) */
        if (v->gpos >> 16 != v->pitch_cur && (v->gpos + 0xFFFF) >> 16 != v->pitch_cur)
            *smooth = 0;
    }
    if (v->gpos != (int32_t)(b << 20))
        *smooth = 0;
    trk_note_off(t, a);
    trk_note_off(t, b);
    return n;
}

static int near_pct(double got, double want, double pct)   /* (or 2 ticks: a short glide's rounding) */
{
    return (got >= want * (1.0 - pct / 100.0) && got <= want * (1.0 + pct / 100.0)) || (got - want < 2.0 && want - got < 2.0);
}

static int voice_of(const track_t *t, uint32_t note)   /* the voice index sounding note (held), -1: none */
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].note == note)
            return (int)i;
    return -1;
}

int main(void)
{
    char val[16], what[96];
    const char *unit = "";
    static const uint32_t G[] = {40, 64, 100, 127};
    uint32_t k;
    int smooth;

    /* 1 */
    param_format(&TP[P_GLIDE], 0, val, &unit);
    check("GLIDE 0 shows 3.0 ms", str_eq(val, "3.0") && str_eq(unit, "ms"));
    param_format(&TP[P_GLIDE], 127, val, &unit);
    check("GLIDE 127 shows 30.0 s", str_eq(val, "30.0") && str_eq(unit, "s"));
    check("GLIDE is F_GLIDE (the editor protocol's fmt 20)", TP[P_GLIDE].fmt == F_GLIDE && F_GLIDE == 20);

    /* 2 */
    for (k = 0; k < sizeof G / sizeof G[0]; k++) {
        double want = 3.0 * TIME_MS_X10[G[k]] / 10.0 / 1000.0 * FS / CTL;   /* ticks */
        uint32_t n = slide_ticks(G[k], 1, 60, 67, &smooth);
        snprintf(what, sizeof what, "TIME, GLIDE %u: a fifth in %u ticks (shown %.1f)", G[k], n, want);
        check(what, near_pct(n, want, 2.0) && smooth);
        n = slide_ticks(G[k], 1, 72, 48, &smooth);
        snprintf(what, sizeof what, "TIME, GLIDE %u: two octaves down in the same time, smooth", G[k]);
        check(what, near_pct(n, want, 2.0) && smooth);
        n = slide_ticks(G[k], 0, 60, 72, &smooth);
        snprintf(what, sizeof what, "RATE, GLIDE %u: an octave in %.2f s", G[k], n * (double)CTL / FS);
        check(what, near_pct(n, want, 2.0) && smooth);
        n = slide_ticks(G[k], 0, 60, 66, &smooth);
        snprintf(what, sizeof what, "RATE, GLIDE %u: half an octave in half the time", G[k]);
        check(what, near_pct(n, want / 2.0, 2.0) && smooth);
    }

    /* 3 */
    {
        uint32_t fs;
        for (fs = 0; fs < 2u; fs++) {
            track_t *t = &trk[0];
            int c, e, g, d, f, a, ok;
            voice_fade_steal = (uint8_t)fs;
            fresh(V_POLY, 120, 1);
            trk_note_on(t, 60, 100);
            trk_note_on(t, 64, 100);
            trk_note_on(t, 67, 100);
            blocks(200);
            c = voice_of(t, 60), e = voice_of(t, 64), g = voice_of(t, 67);
            trk_note_off(t, 60);
            trk_note_off(t, 64);
            trk_note_off(t, 67);
            trk_note_on(t, 62, 100);
            trk_note_on(t, 65, 100);
            trk_note_on(t, 69, 100);
            d = voice_of(t, 62), f = voice_of(t, 65), a = voice_of(t, 69);
            ok = d == c && f == e && a == g && t->v[d].pitch_cur == 60 * 16 && t->v[a].pitch_cur == 67 * 16;
            blocks((uint32_t)(3.0 * TIME_MS_X10[120] / 10.0 / 1000.0 * FS / CTL / 2.0));
            ok = ok && t->v[d].pitch_cur > 60 * 16 + 4 && t->v[d].pitch_cur < 62 * 16 - 4 && t->v[d].gate &&
                 t->v[a].pitch_cur > 67 * 16 + 4 && t->v[a].pitch_cur < 69 * 16 - 4;
            snprintf(what, sizeof what, "POLY: C E G -> D F A, the same voices slide (half way at half the time)%s",
                     fs ? ", fade" : "");
            check(what, ok);
            blocks(30000);
            check("POLY: there at the end", t->v[d].pitch_cur == 62 * 16 && t->v[f].pitch_cur == 65 * 16 &&
                                               t->v[a].pitch_cur == 69 * 16);
            /* nearest: C E G -> E G C' moves C up an octave, E and G stay */
            trk_note_off(t, 62);
            trk_note_off(t, 65);
            trk_note_off(t, 69);
            trk_note_on(t, 60, 100);
            trk_note_on(t, 64, 100);
            trk_note_on(t, 67, 100);
            blocks(30000);
            c = voice_of(t, 60), e = voice_of(t, 64), g = voice_of(t, 67);
            trk_note_off(t, 60);
            trk_note_off(t, 64);
            trk_note_off(t, 67);
            trk_note_on(t, 64, 100);
            trk_note_on(t, 67, 100);
            trk_note_on(t, 72, 100);
            check("POLY: C E G -> E G C', E and G keep their voices, C slides up",
                  voice_of(t, 64) == e && voice_of(t, 67) == g && voice_of(t, 72) == c && t->v[c].pitch_cur == 60 * 16);
        }
        voice_fade_steal = 0;
    }
    {
        track_t *t = &trk[0];
        int c;
        fresh(V_POLY, 0, 1);
        trk_note_on(t, 60, 100);
        blocks(200);
        c = voice_of(t, 60);
        trk_note_off(t, 60);
        trk_note_on(t, 62, 100);
        check("POLY, GLIDE 0: the next note takes another voice, no slide",
              voice_of(t, 62) != c && t->v[voice_of(t, 62)].pitch_cur == 62 * 16 && t->v[c].note == 60);
    }

    /* 4 */
    {
        track_t *t = &trk[0];
        int c, e, g, d;
        fresh(V_POLY, 120, 1);
        trk_note_on(t, 60, 100);
        trk_note_on(t, 64, 100);
        trk_note_on(t, 67, 100);
        blocks(200);
        c = voice_of(t, 60), e = voice_of(t, 64), g = voice_of(t, 67);
        trk_note_on(t, 62, 100);
        d = voice_of(t, 62);
        check("POLY: a note over a held chord: its own voice, the chord held",
              d >= 0 && d != c && d != e && d != g && t->v[c].gate && t->v[c].note == 60 && t->v[e].note == 64 &&
                  t->v[g].note == 67);
    }

    printf(bad ? "glide: %d FAILED\n" : "glide: all ok\n", bad);
    return bad != 0;
}
