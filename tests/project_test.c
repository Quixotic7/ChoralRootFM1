/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Host test of the project formats (firmware/src/project.c, -DPROJ_HOST part): a format 4 ("FUN4", 8-byte
 * steps: before the drum grid), a format 3 ("FUN3", 57 parameters per track: before the modulation matrix), a
 * format 2 ("FUN2", 53: before the SLICER) and a format 1 ("FUN1") project, built byte for byte as the
 * firmware stored them, convert to format 6 ("FUN6"): every old value at its parameter, the parameters
 * added since at their defaults (SLICER OFF,
 * every matrix slot OFF), steps, globals, selection, the engine bytes (0..7 kept: the engines added since
 * were appended); damaged ones are refused. Track 4 of a project written before 1.0 (formats 2 and 3,
 * `parts` 0) was the GM drum part: it becomes the legacy SAMPLE part 4 (PROJ_DEF_KEEP: project_load
 * gives it the PERC preset), its steps and parameters kept, the drum level / reverb send (G_DRLVL /
 * G_DRREV) as its LEVEL / REV; global id 24 (the drum part's MIDI channel then, REVERB TYPE now) loads as ROOM
 * from every format before FUN7. A FUN4 written while the drums were PHYS's MODEL DRUM (`phys` 1) loads such
 * a track as the DRUM engine with the same sound (its E values moved), other PHYS tracks as they were; one
 * of before 1.0 (`phys` 0) its DUST as MODAL bowed. The steps of a DRUM track of before FUN5 (DRUM, or
 * PHYS DRUM become DRUM) get their lanes' notes as hits, the other notes (a low tom, a crash) kept, the
 * step accent as the hits' accents; other tracks' steps stay as they were (no hits).
 * Run by tests/run_tests.sh (needs build/gen from one firmware build). */
#define main hostsim_main
#include "hostsim.c"
#undef main
#define PROJ_HOST 1                             /* (trk_def_engine: engines.c, through hostsim.c) */
#include "../firmware/src/project.c"

static int check(const char *what, int ok)
{
    printf("%-60s %s\n", what, ok ? "ok" : "FAIL");
    return ok ? 0 : 1;
}

/* the value parameter k (old id) of track t had in the old project */
static int16_t oldv(uint32_t t, uint32_t k) { return (int16_t)(t * 100u + k * 3u + 1u); }

static const uint8_t OLD_ENG[NTRK] = {7, 0, 6, 0};   /* WHEEL, ANALOG, TRIO; the drum track: 0 (no engine) */
static void fill_v2_track(proj_trk_v2_t *d, uint32_t t)
{
    uint32_t k;
    for (k = 0; k < PROJ_NP_V2; k++)
        d->p[k] = oldv(t, k);
    d->engine = OLD_ENG[t];
    d->preset = (uint8_t)(t + 5u);
    for (k = 0; k < NSTEP; k++) {
        step8_t *s = &d->step[k];
        s->note[0] = (uint8_t)(36u + (k + t) % 40u);
        s->n = (uint8_t)(k % 3u);
        s->time = (uint8_t)(k % 3u);
        s->flags = (uint8_t)(k & 3u);
        s->vel = (uint8_t)(64u + t);
    }
}

/* today's steps n are the old steps o (8 bytes each), with no hits */
static int steps_same(const step_t *n, const step8_t *o)
{
    uint32_t k;
    for (k = 0; k < NSTEP; k++)
        if (memcmp(n[k].note, o[k].note, 4) || n[k].n != o[k].n || n[k].time != o[k].time ||
            n[k].flags != o[k].flags || n[k].vel != o[k].vel || n[k].hit || n[k].acc)
            return 0;
    return 1;
}

/* today's project q as format 4 stored it (8-byte steps; the hits dropped) */
static void to_v4(project_v4_t *v, const project_t *q)
{
    uint32_t t, k;
    memset(v, 0, sizeof *v);
    v->magic = PROJ_MAGIC_V4;
    v->size = sizeof *v;
    memcpy(v->g, q->g, sizeof v->g);
    v->sel = q->sel;
    v->parts = q->parts;
    v->phys = q->phys;
    for (t = 0; t < NTRK; t++) {
        for (k = 0; k < 61u; k++) v->t[t].p[k] = q->t[t].p[k];
        for (k = 0; k < 8u; k++) v->t[t].p[61u + k] = q->t[t].p[P_E0 + k];
        v->t[t].engine = q->t[t].engine;
        v->t[t].preset = q->t[t].preset;
        for (k = 0; k < NSTEP; k++) {
            memcpy(v->t[t].step[k].note, q->t[t].step[k].note, 4);
            v->t[t].step[k].n = q->t[t].step[k].n;
            v->t[t].step[k].time = q->t[t].step[k].time;
            v->t[t].step[k].flags = q->t[t].step[k].flags;
            v->t[t].step[k].vel = q->t[t].step[k].vel;
        }
    }
    v->sum = proj_hash(v, sizeof *v - 4u);
}

/* track t of the converted project has the old values where they belong; drum: track 4 was the drum part,
 * lvl / rev: its level and reverb send (the old globals) */
static int track_ok(const proj_trk_t *n, const proj_trk_v2_t *o, uint32_t t, int drum, int16_t lvl, int16_t rev)
{
    uint32_t k;
    int ok = (drum ? n->engine == 4u && n->preset == PROJ_DEF_KEEP
                   : n->engine == o->engine && n->preset == o->preset) &&
             steps_same(n->step, o->step);
    for (k = 0; k <= P_DETUNE; k++)
        ok &= n->p[k] == (drum && k == P_LEVEL ? lvl : drum && k == P_REV ? rev : oldv(t, k));
    ok &= n->p[P_SLCR] == 0 && n->p[P_SLPAT] == TP[P_SLPAT].def && n->p[P_SLRATE] == TP[P_SLRATE].def &&
          n->p[P_SLDEPTH] == TP[P_SLDEPTH].def;
    for (k = P_M1SRC; k <= P_M4AMT; k++)
        ok &= n->p[k] == 0;                     /* every matrix slot OFF */
    for (k = 0; k < 8u; k++)
        ok &= n->p[P_E0 + k] == oldv(t, 45u + k);
    return ok;
}

/* a format 3 track (57 parameters: the SLICER at 45..48, P_E0 49) */
static int16_t oldv3(uint32_t t, uint32_t k) { return (int16_t)(t * 200u + k * 5u + 3u); }
static void fill_v3_track(proj_trk_v3_t *d, uint32_t t)
{
    uint32_t k;
    for (k = 0; k < PROJ_NP_V3; k++)
        d->p[k] = oldv3(t, k);
    d->engine = (uint8_t)(t * 3u);
    d->preset = (uint8_t)(t + 1u);
    for (k = 0; k < NSTEP; k++) {
        step8_t *s = &d->step[k];
        s->note[0] = (uint8_t)(40u + (k * 7u + t) % 40u);
        s->n = (uint8_t)((k + t) % 3u);
        s->time = (uint8_t)(k % 3u);
        s->flags = (uint8_t)((k + 1u) & 3u);
        s->vel = (uint8_t)(70u + t);
    }
}
static int track_v3_ok(const proj_trk_t *n, const proj_trk_v3_t *o, uint32_t t)
{
    uint32_t k;
    int ok = n->engine == o->engine && n->preset == o->preset && steps_same(n->step, o->step);
    for (k = 0; k <= P_SLDEPTH; k++)
        ok &= n->p[k] == oldv3(t, k);           /* up to the SLICER: the same ids */
    for (k = P_M1SRC; k <= P_M4AMT; k++)
        ok &= n->p[k] == TP[k].def && TP[k].def == 0;
    for (k = 0; k < 8u; k++)
        ok &= n->p[P_E0 + k] == oldv3(t, 49u + k);
    return ok;
}

int main(void)
{
    static project_v4_t v4;
    static project_v3_t v3;
    static project_v2_t v2;
    static project_v1_t v1;
    static project_t q, q2;
    static union {
        project_t v5;
        project_v4_t v4;
        project_v3_t v3;
        project_v2_t v2;
        project_v1_t v1;
    } buf;
    uint32_t i, t;
    int bad = 0, ok;

    bad += check("layout: SLICER after DETUNE, then the matrix just before P_E0",
                 P_SLCR == P_DETUNE + 1 && P_SLDEPTH + 1 == P_M1SRC && P_M4AMT + 1 == P_FM1_ATK && P_FM4_LEVEL + 1 == P_CHRD && P_VOIC + 1 == P_E0 && P_E0 == 83 &&
                 P_COUNT == PROJ_NP_V3 + 34u && PROJ_NP_V3 == PROJ_NP_V2 + 4u);
    bad += check("FUN8 fits one flash object, the retained cache in NOINIT", sizeof(project_store_t) <= 4096u - 256u &&
                 sizeof(project_store_t) == 3584u && 0xC8u + 4u * sizeof(project_store_t) <= 0x3D50u);

    /* format 2, as written before the SLICER */
    memset(&v2, 0, sizeof v2);
    v2.magic = PROJ_MAGIC_V2;
    v2.size = sizeof v2;
    for (i = 0; i < PROJ_NG_V2; i++)
        v2.g[i] = (int16_t)(500 + i);
    v2.sel = 2;
    for (t = 0; t < NTRK; t++)
        fill_v2_track(&v2.t[t], t);
    v2.sum = proj_hash(&v2, sizeof v2 - 4u);
    bad += check("FUN2 image is 2552 bytes (as stored)", sizeof v2 == 2552u);
    memcpy(&buf, &v2, sizeof v2);
    ok = proj_import(&q, &buf, (int)sizeof v2);
    bad += check("FUN2 -> FUN6: converted, valid format 5 slot", ok && proj_ok(&q) && q.magic == PROJ_MAGIC);
    ok = q.sel == 2;
    for (i = 0; i < G_COUNT; i++)
        ok &= q.g[i] == (i == G_RTYPE ? 0 : (int16_t)(500 + i));
    bad += check("FUN2 -> FUN6: globals (id 24, the old drum channel: ROOM) and selected track", ok);
    ok = 1;
    for (t = 0; t < NTRK; t++)
        ok &= track_ok(&q.t[t], &v2.t[t], t, t == 3u, 127, 127);   /* (G_DRLVL / G_DRREV 525 / 526: 127) */
    bad += check("FUN2 -> FUN6: every parameter mapped, SLICER and matrix OFF (4 tracks)", ok);

    bad += check("FUN2 -> FUN6: engine bytes kept (WHEEL 7, ANALOG 0, TRIO 6)",
                 q.t[0].engine == 7 && q.t[1].engine == 0 && q.t[2].engine == 6 &&
                 str_eq(ENGINES[7]->name, "WHEEL") && str_eq(ENGINES[6]->name, "TRIO") && NENGINES > 8);
    bad += check("FUN2 -> FUN6: the drum track -> part 4, SAMPLE (PERC on load), steps kept",
                 q.parts == NPART && q.t[3].engine == 4 && str_eq(ENGINES[4]->name, "SAMPLE") &&
                 TRK_DEF[3][0] == ENGI_DRUM && str_eq(SMP_PRESET_TABLE[SMP_PERC_PRESET].name, "PERC") &&
                 q.t[3].preset == PROJ_DEF_KEEP && steps_same(q.t[3].step, v2.t[3].step));

    /* format 3 (1.0, four parts), as written before the modulation matrix */
    memset(&v3, 0, sizeof v3);
    v3.magic = PROJ_MAGIC_V3;
    v3.size = sizeof v3;
    for (i = 0; i < G_COUNT; i++)
        v3.g[i] = (int16_t)(600 + i);
    v3.sel = 3;
    v3.parts = NPART;
    for (t = 0; t < NTRK; t++)
        fill_v3_track(&v3.t[t], t);
    v3.sum = proj_hash(&v3, sizeof v3 - 4u);
    bad += check("FUN3 image is 2584 bytes (as stored)", sizeof v3 == 2584u);
    memcpy(&buf, &v3, sizeof v3);
    ok = proj_import(&q, &buf, (int)sizeof v3) && proj_ok(&q) && q.magic == PROJ_MAGIC && q.sel == 3 &&
         q.parts == NPART;
    for (i = 0; i < G_COUNT; i++)
        ok &= q.g[i] == (i == G_RTYPE ? 0 : (int16_t)(600 + i));
    for (t = 0; t < NTRK; t++)
        ok &= track_v3_ok(&q.t[t], &v3.t[t], t);
    bad += check("FUN3 -> FUN6: every parameter kept, the matrix OFF, steps, globals (24: ROOM)", ok);

    /* a FUN3 written before 1.0 (parts 0: track 4 the drum part, engine byte 0) */
    v3.parts = 0;
    v3.t[3] = v3.t[0];
    v3.t[3].engine = 0;
    v3.t[3].preset = 0;
    v3.g[G_DRLVL] = 90;
    v3.g[G_DRREV] = 20;
    v3.sum = proj_hash(&v3, sizeof v3 - 4u);
    memcpy(&buf, &v3, sizeof v3);
    ok = proj_import(&q2, &buf, (int)sizeof v3) && proj_ok(&q2) && q2.parts == NPART && q2.t[3].engine == 4 &&
         q2.t[3].preset == PROJ_DEF_KEEP && q2.t[3].p[P_LEVEL] == 90 && q2.t[3].p[P_REV] == 20 &&
         q2.t[3].p[P_PAN] == oldv3(0, P_PAN) && q2.t[3].p[P_SLEN] == oldv3(0, P_SLEN) &&
         q2.t[3].p[P_SLCR] == oldv3(0, P_SLCR) && q2.t[3].p[P_M1SRC] == 0 &&
         steps_same(q2.t[3].step, v3.t[0].step) && !memcmp(&q2.t[0], &q.t[0], sizeof q.t[0]);
    bad += check("FUN3 before 1.0: the drum track -> part 4 (LEVEL / REV from G_DRLVL / G_DRREV)", ok);

    /* a FUN6 round trip: stored as is (a matrix slot set; an engine added since: SLICE, 8; a DRUM track with
     * notes on its lanes stays so: only an older format is converted) */
    q.t[1].engine = 8;
    q.t[2].p[P_M2SRC] = 6;
    q.t[2].p[P_M2DST] = 11;
    q.t[2].p[P_M2AMT] = -17;
    q.t[0].engine = ENGI_DRUM;
    q.t[0].step[0] = (step_t){{36, 42, 0, 0}, 2, ST_NOTE, 0, 96, 1u << DV_SNARE, 1u << DV_SNARE};
    q.sum = proj_sum(&q);
    memcpy(&buf, &q, sizeof q);
    bad += check("FUN6 -> FUN6: as stored (a matrix slot, engine 8, a DRUM step with notes and hits)",
                 proj_import(&q2, &buf, (int)sizeof q) && !memcmp(&q, &q2, sizeof q) && q2.t[1].engine == 8 &&
                 q2.t[2].p[P_M2AMT] == -17 && q2.t[0].step[0].n == 2u && q2.t[0].step[0].hit == 1u << DV_SNARE);
    q.t[0].engine = 3;
    q.t[0].step[0] = q.t[0].step[1];
    q.sum = proj_sum(&q);

    {
        project_v5_t old;
        memset(&old, 0, sizeof old);
        memcpy(old.g, q.g, sizeof old.g); old.sel = q.sel; old.parts = q.parts; old.phys = q.phys;
        for (uint32_t k = 0; k < NTRK; k++) {
            for (uint32_t j = 0; j < 61u; j++) old.t[k].p[j] = q.t[k].p[j];
            for (uint32_t j = 0; j < 8u; j++) old.t[k].p[61u + j] = q.t[k].p[P_E0 + j];
            old.t[k].engine = q.t[k].engine; old.t[k].preset = q.t[k].preset;
            for (uint32_t j = 0; j < NSTEP; j++) memcpy(&old.t[k].step[j], &q.t[k].step[j], sizeof(step10_t));
        }
        old.magic = PROJ_MAGIC_V5;
        old.size = sizeof old;
        old.sum = proj_hash(&old, sizeof old - 4u);
        bad += check("FUN5 -> FUN6: sounds, grid and reserved lane bytes kept, no chain",
            proj_import(&q2, &old, sizeof old) && !memcmp(q2.t, q.t, sizeof q.t) &&
            !q2.chain.count && chain_valid(&q2.chain));
        old.sum ^= 1u;
        bad += check("FUN5: damaged checksum refused", !proj_import(&q2, &old, sizeof old));
        q.chain.count = 2;
        q.chain.row[0] = (chain_row_t){0, 4};
        q.chain.row[1] = (chain_row_t){3, 16};
        q.sum = proj_sum(&q);
        bad += check("FUN6: chain round trip", proj_import(&q2, &q, sizeof q) &&
            !memcmp(&q.chain, &q2.chain, sizeof q.chain));
        q.chain.row[0].repeat = 0;
        q.sum = proj_sum(&q);
        bad += check("FUN6: invalid row refused even with a correct checksum", !proj_import(&q2, &q, sizeof q));
        chain_defaults(&q.chain);
        q.sum = proj_sum(&q);
    }

    /* format 4 (1.0 development builds): 8-byte steps; a DRUM track's lane notes become its grid */
    to_v4(&v4, &q);
    bad += check("FUN4 image is 2680 bytes (as stored)", sizeof v4 == 2680u);
    memcpy(&buf, &v4, sizeof v4);
    ok = proj_import(&q2, &buf, (int)sizeof v4) && proj_ok(&q2) && q2.magic == PROJ_MAGIC && q2.sel == q.sel &&
         !memcmp(q2.g, q.g, sizeof q.g);
    for (t = 0; t < NTRK; t++)
        ok &= !memcmp(q2.t[t].p, q.t[t].p, sizeof q.t[t].p) && q2.t[t].engine == q.t[t].engine &&
              steps_same(q2.t[t].step, v4.t[t].step);
    bad += check("FUN4 -> FUN6: parameters, engines, globals; steps as they were, no hits", ok);
    {   /* track 2 a DRUM track: BEAT-like steps, a chord, a low tom 41, a crash 49, a step accent, a TIE */
        static const step8_t D[6] = {
            {{36, 0, 0, 0}, 1, ST_NOTE, SF_ACCENT, 96}, {{42, 38, 0, 0}, 2, ST_NOTE, 0, 100},
            {{41, 36, 0, 0}, 2, ST_NOTE, SF_ACCENT, 96}, {{49, 0, 0, 0}, 1, ST_NOTE, 0, 96},
            {{0, 0, 0, 0}, 0, ST_TIE, 0, 0}, {{38, 46, 0, 0}, 2, ST_REST, 0, 96},
        };
        const step_t *s = q2.t[1].step;
        v4.t[1].engine = ENGI_DRUM;
        for (i = 0; i < 6u; i++)
            v4.t[1].step[i] = D[i];
        v4.sum = proj_hash(&v4, sizeof v4 - 4u);
        memcpy(&buf, &v4, sizeof v4);
        ok = proj_import(&q2, &buf, (int)sizeof v4) && q2.t[1].engine == ENGI_DRUM;
        ok &= s[0].n == 0 && s[0].hit == 1u << DV_KICK && s[0].acc == 1u << DV_KICK && !(s[0].flags & SF_ACCENT) &&
              s[0].vel == 96 && s[0].time == ST_NOTE;
        ok &= s[1].n == 0 && s[1].hit == ((1u << DV_HATC) | (1u << DV_SNARE)) && !s[1].acc && s[1].vel == 100;
        ok &= s[2].n == 1 && s[2].note[0] == 41 && s[2].hit == 1u << DV_KICK && (s[2].flags & SF_ACCENT) &&
              step_lanes(&s[2]) == ((1u << DV_KICK) | (1u << DV_TOM)) && step_accents(&s[2]) == step_lanes(&s[2]);
        ok &= s[3].n == 1 && s[3].note[0] == 49 && !s[3].hit && step_lanes(&s[3]) == 1u << DV_BELL;
        ok &= s[4].time == ST_TIE && !s[4].hit && s[5].time == ST_REST && s[5].n == 2 && !s[5].hit;
        ok &= steps_same(q2.t[0].step, v4.t[0].step);   /* (not DRUM: as it was) */
        bad += check("FUN4 -> FUN6: a DRUM track's lane notes -> hits (accents too), a low tom / crash kept", ok);
    }
    v3.t[0].p[7]++;
    memcpy(&buf, &v3, sizeof v3);
    bad += check("FUN3 with a bad checksum: refused", !proj_import(&q2, &buf, (int)sizeof v3));
    memcpy(&buf, &q, sizeof q);
    buf.v5.magic = PROJ_MAGIC_V3;
    bad += check("FUN6 size with a FUN3 magic: refused", !proj_import(&q2, &buf, (int)sizeof q));

    /* damaged / wrong size */
    v2.t[1].p[3]++;
    memcpy(&buf, &v2, sizeof v2);
    bad += check("FUN2 with a bad checksum: refused", !proj_import(&q2, &buf, (int)sizeof v2));
    v2.t[1].p[3]--;
    memcpy(&buf, &v2, sizeof v2);
    bad += check("FUN2 with a wrong length: refused", !proj_import(&q2, &buf, (int)sizeof v2 - 2));
    memcpy(&buf, &q, sizeof q);
    buf.v5.magic = PROJ_MAGIC_V2;
    bad += check("FUN6 size with a FUN2 magic: refused", !proj_import(&q2, &buf, (int)sizeof q));

    /* format 1: one instrument -> track 1, the others their defaults */
    memset(&v1, 0, sizeof v1);
    v1.magic = PROJ_MAGIC_V1;
    v1.size = sizeof v1;
    for (i = 0; i < PROJ_NG_V2; i++)
        v1.g[i] = (int16_t)(700 + i);
    fill_v2_track(&v1.t, 0);
    v1.sum = proj_hash(&v1, sizeof v1 - 4u);
    memcpy(&buf, &v1, sizeof v1);
    ok = proj_import(&q, &buf, (int)sizeof v1) && proj_ok(&q) && track_ok(&q.t[0], &v1.t, 0, 0, 0, 0) && q.g[5] == 705 &&
         q.parts == NPART;
    for (t = 1; t < NTRK; t++)
        ok &= q.t[t].preset == PROJ_DEF_SOUND && q.t[t].engine == trk_def_engine(t) && q.t[t].p[P_SLCR] == 0 && q.t[t].p[P_LEVEL] == TP[P_LEVEL].def &&
              q.t[t].p[P_E0] == ENGINES[trk_def_engine(t)]->edit[0].def && q.t[t].step[0].time == ST_REST;
    bad += check("FUN1 -> FUN6: track 1 mapped, tracks 2..4 defaults", ok);

    /* PHYS MODEL DRUM (a FUN4 with phys 1) -> the DRUM engine; the old DRUM KIT preset becomes DRUM's */
    {
        static const int16_t OLD_KIT[8] = {4, 64, 70, 64, 64, 100, 0, 0};   /* PHYS DRUM KIT before 1.0 */
        static const int16_t E0[8] = {4, 70, 80, 60, 50, 110, 100, 70}, E1[8] = {2, 10, 20, 30, 40, 50, 60, 70};
        const preset_t *dk = &ENGINES[ENGI_DRUM]->presets[0];
        q.t[0].engine = ENGI_PHYS;                      /* MODEL DRUM, PERC 70 (CYM), KICK 100 (ROUND) */
        q.t[0].preset = 9;
        q.t[1].engine = ENGI_PHYS;                      /* MEMB (2: DUST only before phys 1) */
        q.t[2].engine = ENGI_PHYS;
        q.t[3].engine = ENGI_PHYS;
        for (i = 0; i < 8u; i++) {
            q.t[0].p[P_E0 + i] = E0[i];
            q.t[1].p[P_E0 + i] = E1[i];
            q.t[2].p[P_E0 + i] = OLD_KIT[i];
            q.t[3].p[P_E0 + i] = (int16_t)(i ? 30 : PM_SYMP);
        }
        q.phys = 1;
        for (i = 1; i < NSTEP; i++)                     /* (the other steps rests) */
            q.t[0].step[i] = (step_t){{50, 0, 0, 0}, 1, ST_REST, 0, 0, 0, 0};
        q.t[0].step[0] = (step_t){{36, 42, 0, 0}, 2, ST_NOTE, 0, 96, 0, 0};
        q.sum = proj_sum(&q);
        to_v4(&v4, &q);
        memcpy(&buf, &v4, sizeof v4);
        ok = proj_import(&q2, &buf, (int)sizeof v4) && proj_ok(&q2) && q2.phys == PROJ_PHYS &&
             q2.t[0].engine == ENGI_DRUM && q2.t[0].preset == 0 && str_eq(ENGINES[ENGI_DRUM]->name, "DRUM") &&
             q2.t[0].p[P_E0] == 2 && q2.t[0].p[P_E6] == 1 && q2.t[0].p[P_E7] == 0;
        for (i = 1; i < 6u; i++)
            ok &= q2.t[0].p[P_E0 + i] == E0[i];         /* TUNE TONE DECY SNAP ACC in place */
        ok &= q2.t[1].engine == ENGI_PHYS && !memcmp(q2.t[1].p, q.t[1].p, sizeof q.t[1].p);
        ok &= q2.t[3].engine == ENGI_PHYS && !memcmp(q2.t[3].p, q.t[3].p, sizeof q.t[3].p);
        ok &= q2.t[2].engine == ENGI_DRUM;              /* the old DRUM KIT: DRUM's DRUM KIT */
        for (i = 0; i < 8u; i++)
            ok &= q2.t[2].p[P_E0 + i] == dk->e[i];
        ok &= !memcmp(&q2.t[0].step[1], &q.t[0].step[1], sizeof q.t[0].step - sizeof(step_t)) &&
              q2.t[0].p[P_LEVEL] == q.t[0].p[P_LEVEL];
        ok &= q2.t[0].step[0].n == 0 && q2.t[0].step[0].hit == ((1u << DV_KICK) | (1u << DV_HATC));   /* the grid */
        ok &= !memcmp(q2.t[1].step, q.t[1].step, sizeof q.t[1].step);   /* (PHYS: as they were) */
        bad += check("FUN4 phys 1: PHYS MODEL DRUM -> DRUM engine (its lane notes as hits), MEMB kept", ok);
        memcpy(&buf, &q2, sizeof q2);
        bad += check("FUN6 after it: imported again, as it is", proj_import(&q, &buf, (int)sizeof q2) &&
                                                                !memcmp(&q, &q2, sizeof q));
        q.phys = 0;                                     /* before 1.0: MODEL 2 was DUST */
        q.t[1].engine = ENGI_PHYS;
        q.sum = proj_sum(&q);
        to_v4(&v4, &q);
        memcpy(&buf, &v4, sizeof v4);
        ok = proj_import(&q2, &buf, (int)sizeof v4) && q2.phys == PROJ_PHYS && q2.t[1].engine == ENGI_PHYS &&
             q2.t[1].p[P_E0] == PM_MODAL && q2.t[1].p[P_E6] == 64;
        bad += check("FUN4 phys 0: PHYS DUST -> MODAL bowed", ok);
    }

    {   /* the FUN7 name: the reserved tail's last 12 bytes, covered by the hash; zero = no name */
        project_t a, c;
        project_store_t st, st2;
        uint32_t i, zero = 1;
        bad += check("FUN8 name at the end of the reserved tail, the FM6 patches before it, after the data",
                     PROJ_NAME_OFF == 3568u && PROJ_FM6_OFF == 3056u && 68u + NTRK * (P_COUNT + 2u + NSTEP * 9u) +
                     sizeof(chain_config_t) + sizeof(motion_store_t) + 16u == PROJ_FM6_OFF);
        memset(&a, 0, sizeof a);
        a.magic = PROJ_MAGIC; a.size = sizeof a; a.parts = NPART; a.phys = PROJ_PHYS;
        chain_defaults(&a.chain);
        a.t[1].p[P_LEVEL] = 99;
        a.sum = proj_sum(&a);
        ok = proj_pack(&st, &a);
        for (i = 0; i < PROJ_NAME_LEN; i++) zero &= st.raw[PROJ_NAME_OFF + i] == 0u;
        bad += check("FUN7 no name: the tail all zero (as firmware before wrote it)", ok && zero);
        bad += check("FUN7 of before names (zero tail): loads, no name", proj_import(&c, &st, sizeof st) && !c.name[0] &&
                     c.t[1].p[P_LEVEL] == 99);
        memcpy(a.name, "lofi jam", 8);
        a.sum = proj_sum(&a);
        ok = proj_pack(&st, &a) && proj_import(&c, &st, sizeof st);
        bad += check("FUN7 name round trip (upper case, 0-padded)", ok && !memcmp(c.name, "LOFI JAM\0\0\0\0", 12) &&
                     !memcmp(st.raw + PROJ_NAME_OFF, "LOFI JAM\0\0\0\0", 12));
        memcpy(a.name, "ABCDEFGHIJKL", 12);         /* 12: no 0 */
        a.sum = proj_sum(&a);
        ok = proj_pack(&st, &a) && proj_import(&c, &st, sizeof st);
        bad += check("FUN7 name of 12 characters (no terminator)", ok && !memcmp(c.name, "ABCDEFGHIJKL", 12));
        st2 = st;
        st2.raw[PROJ_NAME_OFF + 3] ^= 1u;
        bad += check("FUN7 name byte changed: the hash refuses the project", !proj_import(&c, &st2, sizeof st2));
        st2 = st;
        st2.raw[PROJ_NAME_OFF + 3] = 7u;            /* not printable, the hash right */
        {
            uint32_t sum = proj_hash(st2.raw, PROJ_STORE_SIZE - 4u);
            memcpy(st2.raw + PROJ_STORE_SIZE - 4u, &sum, 4);
        }
        bad += check("FUN7 name with a byte outside 32..126: the project loads, no name",
                     proj_import(&c, &st2, sizeof st2) && !c.name[0] && c.t[1].p[P_LEVEL] == 99);
        bad += check("FUN6..FUN1 imports: no name", !q2.name[0] && !q.name[0]);
    }

    {   /* FUN8: each track's FM6 patch travels with the project; FUN7 and older get the init patch */
        project_t a, c;
        static project_store_t st, slot;
        static uint8_t v7[PROJ_STORE_V7];
        uint32_t i, k, init = 1, sum;
        memset(&a, 0, sizeof a);
        a.magic = PROJ_MAGIC; a.size = sizeof a; a.parts = NPART; a.phys = PROJ_PHYS;
        chain_defaults(&a.chain);
        for (k = 0; k < NTRK; k++) {
            memcpy(a.fm6[k], FM6_FACTORY[k * 2u], FM6_PACKED);
            a.t[k].engine = ENGI_FM6;
            a.t[k].p[P_E7] = (int16_t)(k * 2u);
        }
        memcpy(a.name, "FM SONG", 7);
        a.sum = proj_sum(&a);
        ok = proj_pack(&st, &a) && ((uint32_t *)st.raw)[0] == 0x46554E38u && proj_import(&c, &st, sizeof st);
        for (k = 0; k < NTRK; k++)
            ok &= !memcmp(c.fm6[k], FM6_FACTORY[k * 2u], FM6_PACKED) && c.t[k].engine == ENGI_FM6 &&
                  !memcmp(st.raw + PROJ_FM6_OFF + k * FM6_PACKED, FM6_FACTORY[k * 2u], FM6_PACKED);
        bad += check("FUN8: the four FM6 patches round trip (bytes at PROJ_FM6_OFF)", ok && !memcmp(c.name, "FM SONG", 7));
        st.raw[PROJ_FM6_OFF + 5] ^= 1u;
        bad += check("FUN8: a patch byte changed: the hash refuses it", !proj_import(&c, &st, sizeof st));
        st.raw[PROJ_FM6_OFF + 5] ^= 1u;
        /* the same music as FUN7 (3388 bytes, no patches): the data where it was, the name at 3372 */
        memcpy(v7, st.raw, PROJ_STORE_V7 - 16u);
        memset(v7 + PROJ_FM6_OFF, 0, PROJ_STORE_V7 - 16u - PROJ_FM6_OFF);
        memcpy(v7 + PROJ_STORE_V7 - 16u, st.raw + PROJ_NAME_OFF, 12);
        ((uint32_t *)v7)[0] = 0x46554E37u;
        ((uint32_t *)v7)[1] = PROJ_STORE_V7;
        sum = proj_hash(v7, PROJ_STORE_V7 - 4u);
        memcpy(v7 + PROJ_STORE_V7 - 4u, &sum, 4);
        ok = proj_import(&c, v7, sizeof v7);
        for (k = 0; k < NTRK; k++)
            init &= !memcmp(c.fm6[k], FM6_INIT, FM6_PACKED);
        bad += check("FUN7 -> FUN8: the music and name kept, every track the init patch",
                     ok && init && c.t[2].engine == ENGI_FM6 && c.t[2].p[P_E7] == 4 && !memcmp(c.name, "FM SONG", 7));
        {   /* REVERB TYPE (id 24) of a FUN7 holding the old drum channel (10): ROOM; SPRING (1) kept */
            int16_t g24 = 10;
            memcpy(v7 + 8u + 2u * G_RTYPE, &g24, 2);
            sum = proj_hash(v7, PROJ_STORE_V7 - 4u);
            memcpy(v7 + PROJ_STORE_V7 - 4u, &sum, 4);
            ok = proj_import(&c, v7, sizeof v7) && c.g[G_RTYPE] == 0 && proj_ok(&c);
            g24 = 1;
            memcpy(v7 + 8u + 2u * G_RTYPE, &g24, 2);
            sum = proj_hash(v7, PROJ_STORE_V7 - 4u);
            memcpy(v7 + PROJ_STORE_V7 - 4u, &sum, 4);
            ok &= proj_import(&c, v7, sizeof v7) && c.g[G_RTYPE] == 1;
            bad += check("FUN7 REVERB TYPE above SPRING (an old drum channel): ROOM", ok);
        }
        ok = proj_import(&c, v7, sizeof v7 - 1u);
        bad += check("FUN7 one byte short: refused", !ok);
        /* the retained cache after an update: slot 1's FUN7 record, longer slot, garbage after it */
        memset(&slot, 0xA5, sizeof slot);
        memcpy(slot.raw, v7, sizeof v7);
        bad += check("a retained 3584-byte slot holding a FUN7 record loads it", proj_import(&c, &slot, sizeof slot) &&
                     c.t[2].p[P_E7] == 4 && !memcmp(c.fm6[0], FM6_INIT, FM6_PACKED));
        memset(&slot, 0xA5, sizeof slot);
        bad += check("a retained slot of garbage is empty", !proj_import(&c, &slot, sizeof slot));
        for (i = 0; i < FM6_PACKED; i++) a.fm6[1][i] = (uint8_t)(i | 0x80u);
        a.sum = proj_sum(&a);
        bad += check("FUN8: a patch byte above 127 cannot be packed", !proj_pack(&st, &a));
        memcpy(a.fm6[1], FM6_INIT, FM6_PACKED);
        a.t[0].step[3].n = 1; a.t[0].step[3].note[0] = 200; a.t[0].step[3].note[2] = 128;
        a.t[0].step[3].vel = 255; a.t[0].step[3].hit = 0x05; a.t[0].step[3].acc = 0xF7;
        a.sum = proj_sum(&a);
        ok = proj_pack(&st, &a) && proj_import(&c, &st, sizeof st);
        bad += check("FUN8: a step out of range packs inside it (note, velocity 127; accents on hits): it loads",
                     ok && c.t[0].step[3].note[0] == 127u && c.t[0].step[3].note[2] == 127u && c.t[0].step[3].vel == 127u &&
                     c.t[0].step[3].hit == 0x05u && c.t[0].step[3].acc == 0x05u);
    }

#if !FELUCCA_FM4
    {   /* DIGITAL tracks (engine 1, retired: src/fm4_convert.c) in FUN8 / FUN7 / FUN4 / FUN1 projects load as FM6 with
         * the converted patch as the track's own; their motion on the EDIT or OP ENV values goes, the rest stays */
        project_t a, c, d;
        static project_store_t st;
        static uint8_t v7[PROJ_STORE_V7];
        static project_v4_t w4;
        static project_v1_t w1;
        uint8_t v[FP_SIZE + 1u], pk[FM6_PACKED];
        int16_t p[P_COUNT];
        uint32_t i, sum;
        memset(&a, 0, sizeof a);
        a.magic = PROJ_MAGIC; a.size = sizeof a; a.parts = NPART; a.phys = PROJ_PHYS;
        chain_defaults(&a.chain);
        for (t = 0; t < NTRK; t++) {
            for (i = 0; i < P_COUNT; i++)
                a.t[t].p[i] = param_desc_of(t == 1u ? ENGI_DIGITAL : 0u, i)->def;
            a.t[t].engine = t == 1u ? ENGI_DIGITAL : 0u;
            a.t[t].preset = t == 1u ? 5u : 0u;
            memcpy(a.fm6[t], FM6_INIT, FM6_PACKED);
        }
        fm4_preset_values(a.t[1].p, 5);                 /* DIGITAL PAD, an OP ENV edit on op 2 */
        a.t[1].p[P_FM2_ATK] = 30;
        a.t[1].p[P_FM2_LEVEL] = 100;
        a.t[1].p[P_LEVEL] = 90;
        memcpy(p, a.t[1].p, sizeof p);
        fm4_convert(p, v);
        fm6_pack(v, pk);
        a.motion.count = 5;
        a.motion.event[0] = (motion_event_t){1u << 6 | 3u, P_E4, 20};          /* track 2: INDEX */
        a.motion.event[1] = (motion_event_t){1u << 6 | 4u, P_FM1_LEVEL, 50};   /* track 2: OP LEVEL */
        a.motion.event[2] = (motion_event_t){1u << 6 | 5u, P_CHOR, 60};        /* track 2: a send: stays */
        a.motion.event[3] = (motion_event_t){0u << 6 | 3u, P_E4, 70};          /* track 1 (ANALOG): stays */
        a.motion.event[4] = (motion_event_t){1u << 6 | 9u, P_E7, 0};           /* track 2: E7 */
        a.sum = proj_sum(&a);
#define FM4_OK(c) ((c).t[1].engine == ENGI_FM6 && (c).t[1].preset == 4u && !memcmp((c).fm6[1], pk, FM6_PACKED) && \
                   !memcmp((c).t[1].p, p, sizeof p) && (c).t[0].engine == 0u && !memcmp((c).fm6[0], FM6_INIT, FM6_PACKED))
        ok = proj_pack(&st, &a) && proj_import(&c, &st, sizeof st);
        bad += check("FUN8 with a DIGITAL track: FM6, the converted patch its own, PTCH / preset FM6 PAD", ok && FM4_OK(c));
        bad += check("  its motion on INDEX / OP LEVEL / E7 goes; a send's and the other track's stay",
                     c.motion.count == 2u && c.motion.event[0].param == P_CHOR && c.motion.event[1].place == 3u &&
                     c.motion.event[1].param == P_E4 && motion_valid(&c.motion) && proj_ok(&c));
        bad += check("  imported again: as it is (FM6 now)", proj_import(&d, &c, sizeof c) && !memcmp(&d, &c, sizeof d));
        memcpy(v7, st.raw, PROJ_STORE_V7 - 16u);       /* the same as FUN7 (as the FUN8 block above) */
        memset(v7 + PROJ_FM6_OFF, 0, PROJ_STORE_V7 - 16u - PROJ_FM6_OFF);
        memset(v7 + PROJ_STORE_V7 - 16u, 0, 12);
        ((uint32_t *)v7)[0] = 0x46554E37u;
        ((uint32_t *)v7)[1] = PROJ_STORE_V7;
        sum = proj_hash(v7, PROJ_STORE_V7 - 4u);
        memcpy(v7 + PROJ_STORE_V7 - 4u, &sum, 4);
        bad += check("FUN7 with a DIGITAL track: the same", proj_import(&c, v7, sizeof v7) && FM4_OK(c) &&
                     c.motion.count == 2u);
        memset(&a.motion, 0, sizeof a.motion);
        a.sum = proj_sum(&a);
        to_v4(&w4, &a);
        memcpy(&buf, &w4, sizeof w4);
        memcpy(p, a.t[1].p, sizeof p);                  /* (FUN4 had no OP ENV values: they load as defaults) */
        for (i = P_FM1_ATK; i <= P_FM4_LEVEL; i++)
            p[i] = TP[i].def;
        fm4_convert(p, v);
        fm6_pack(v, pk);
        ok = proj_import(&c, &buf, (int)sizeof w4) && FM4_OK(c);
        bad += check("FUN4 with a DIGITAL track (no OP ENV then: their defaults): FM6, converted", ok);
        memset(&w1, 0, sizeof w1);
        w1.magic = PROJ_MAGIC_V1;
        w1.size = sizeof w1;
        for (i = 0; i < PROJ_NG_V2; i++)
            w1.g[i] = GP[i].def;
        for (i = 0; i < PROJ_NP_V2 - 8u; i++)
            w1.t.p[i] = TP[i].def;
        for (i = 0; i < 8u; i++)
            w1.t.p[PROJ_NP_V2 - 8u + i] = DIGITAL_PRESETS[1].e[i];   /* BELL */
        w1.t.engine = ENGI_DIGITAL;
        w1.t.preset = 1;
        w1.sum = proj_hash(&w1, sizeof w1 - 4u);
        memcpy(&buf, &w1, sizeof w1);
        ok = proj_import(&c, &buf, (int)sizeof w1) && c.t[0].engine == ENGI_FM6 && c.t[0].preset == 1u &&
             c.t[1].engine == ENGI_FM6 && c.t[1].preset == PROJ_DEF_SOUND;   /* (track 2: the power-on FM6 PAD) */
        bad += check("FUN1 with DIGITAL: track 1 FM6 (BELL), track 2 the power-on sound (FM6 now)", ok);
#undef FM4_OK
    }
#endif
    printf("%s\n", bad ? "PROJECT FORMAT TEST FAILED" : "project format test passed");
    return bad != 0;
}
