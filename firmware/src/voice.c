/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Voice allocation, envelopes, LFO and per-track rendering.
 * Runs in the audio ISR.
 *
 * Each synth part has its own NVOICE voices (so MONO / LEGATO / UNISON keep using
 * v[0..]; POLY uses the engine's cap, NPOLY or FM6's 16), but the parts' sounding voices
 * share one budget of VBUDGET units (a voice of an engine capped above NPOLY takes one, any
 * other two: eight of them, as before): a part that starts
 * a voice while the budget is full takes one from any part (voice_victim): the oldest
 * released voice, else the oldest extra UNISON voice, else the oldest held voice of a
 * POLY part that is not its lowest note. A voice taken from another part fades out
 * over one block (stage 4: the envelope goes to 0, the block's amplitude ramp
 * declicks it); one of the part's own is restarted in place, as before. Extra UNISON
 * voices only start when there is room. */
static uint32_t vage;                                   /* voice ages: one clock for every part */
static int32_t lfo_wave(track_t *t, uint32_t ph)
{
    switch (t->p[P_LWAVE]) {
    case 1:
        return osc_tri(ph);
    case 2:
        return (int32_t)(ph >> 16) - 32768;
    case 3:
        return ph < 0x80000000u ? 32767 : -32767;
    case 4:
        return (int32_t)(t->lfo_rnd >> 16) - 32768;
    default:
        return osc_sine(ph);
    }
}

/* the S&H value of a new LFO cycle. Parts 1..3 draw it from the shared generator (rng); part 4 has its
 * own xorshift, so the shared sequence, which also seeds the engines' noise (PHYS, VOICE, ..), does not
 * depend on part 4's LFO (the golden renders rely on it) */
#define LFO_SHARED_RNG 3u
static uint32_t lfo_rand(track_t *t)
{
    uint32_t s = t->lfo_rnd ? t->lfo_rnd : 0x9E3779B9u;
    if ((uint32_t)(t - trk) < LFO_SHARED_RNG)
        return rng();
    s ^= s << 13;
    s ^= s >> 17;
    s ^= s << 5;
    return s;
}

CR_HOT_CORE static void track_lfo_tick(track_t *t)
{
    uint32_t old = t->lfo_ph;
    t->lfo_ph += LFO_INC[t->p[P_LRATE] & 127];
    if (t->lfo_ph < old)
        t->lfo_rnd = lfo_rand(t);
    t->lfo_val = lfo_wave(t, t->lfo_ph);
    if (t->lfo_fade < 32767) {
        int32_t step = (int32_t)(ENV_LIN[t->p[P_LFADE] & 127] >> 9);
        t->lfo_fade = t->p[P_LFADE] ? clamp(t->lfo_fade + (step ? step : 1), 0, 32767) : 32767;
    }
}

/* voices the engine may use (POLY and UNISON): its cap, else NPOLY */
static uint32_t trk_nvoice(const track_t *t)
{
    const engine_t *e = ENGINES[t->engine];
    uint32_t c = e->cap ? e->cap(t) : e->poly;
    return !c ? NPOLY : c < NVOICE ? c : NVOICE;
}

/* the part's voice mode: P_VOICE, POLY for an engine of hits (engine_t.oneshot: DRUM) */
static uint32_t trk_vmode(const track_t *t)
{
    return ENGINES[t->engine]->oneshot ? V_POLY : (uint32_t)t->p[P_VOICE];
}

/* ------------------------------------------------- the shared voice budget --- */
static uint32_t voice_units(const track_t *t)
{
    const engine_t *e = ENGINES[t->engine];
    return e->units ? e->units(t) : trk_nvoice(t) > NPOLY ? 1u : 2u;
}

/* the budget a part shares: all parts' one (Felucca, one core), or with the second core alive (cr_cpu1.h) part 1's own
 * and part 0's (with parts 2..3, which ChoralRoot leaves silent): each VBUDGET units */
#if CR_CPU1
#define VB_SAME(a, b) (!cr_dualcore_active() || ((a) == 1u) == ((b) == 1u))
#else
#define VB_SAME(a, b) 1
#endif
static uint32_t voices_busy_of(const track_t *self)        /* budget units of the sounding voices (not fading) of the
                                                         * parts that share self's budget */
{
    uint32_t p, i, n, u = 0, sp = (uint32_t)(self - trk);
    for (p = 0; p < NPART; p++) {
        if (!VB_SAME(p, sp))
            continue;
        for (i = n = 0; i < NVOICE; i++)
            n += trk[p].v[i].active && trk[p].v[i].stage != 4u;
        u += n * voice_units(&trk[p]);
    }
    return u;
}
static uint32_t voices_busy(void)                       /* budget units of all parts' sounding voices (not fading) */
{
    uint32_t p, i, n, u = 0;
    for (p = 0; p < NPART; p++) {
        for (i = n = 0; i < NVOICE; i++)
            n += trk[p].v[i].active && trk[p].v[i].stage != 4u;
        u += n * voice_units(&trk[p]);
    }
    return u;
}

static uint32_t lowest_held(const track_t *t)           /* index of the lowest held note (the bass), NVOICE = none */
{
    uint32_t i, low = NVOICE;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].gate && (low == NVOICE || t->v[i].note < t->v[low].note))
            low = i;
    return low;
}

/* the voice to give up for a new one (see the top); soft: only released voices and
 * extra voices of other non-POLY parts (also ones left after a mode change).
 * Returns its index, *pp its part; NVOICE = none */
static uint32_t voice_victim(const track_t *self, int soft, track_t **pp)
{
    uint32_t p, i, best = NVOICE, cat = 4;
    for (p = 0; p < NPART; p++) {
        track_t *t = &trk[p];
        uint32_t mode;
        if (!VB_SAME(p, (uint32_t)(self - trk)))
            continue;                                   /* another budget's (the second core: parts 0 / 1 apart) */
        mode = trk_vmode(t);
        uint32_t low = mode == V_POLY ? lowest_held(t) : NVOICE;
        if (soft && t == self)
            continue;
        for (i = 0; i < NVOICE; i++) {
            const voice_t *v = &t->v[i];
            uint32_t c;
            if (!v->active || v->stage == 4u)
                continue;
            if (!v->gate)
                c = 1;                                  /* released */
            else if (mode != V_POLY && i > 0)
                c = 2;                                  /* extra UNISON, or a voice left by a mode / cap change */
            else if (!soft && mode == V_POLY && i != low)
                c = 3;                                  /* held, not the bass */
            else
                continue;
            if (c < cat || (c == cat && v->age < (*pp)->v[best].age)) {
                cat = c;
                best = i;
                *pp = t;
            }
        }
    }
    return best;
}

static uint32_t voice_kills;                            /* voices given up (budget, overload): console, hostsim */
static uint32_t voice_steals;                           /* a part's own sounding voice restarted for a new note */
static void voice_kill(voice_t *v)                      /* fade out over the next block (env_tick stage 4) */
{
    v->gate = 0;
    v->stage = 4;
    voice_kills++;
}

#if FELUCCA_PROPHET
/* ---------------------------------- P5's voices over the two parts and the two cores (3y46) ---
 * eng_prophet_test.c "THE VOICES", docs/PROPHET.md "Voices". The budget's units cannot say it (two cores: each part
 * its own sixteen), so P5 has a POOL beside them: the chord part's and the bass part's P5 voices together.
 *   TWO CORES   2 x P5_CORE = 8. THE CHORD FIRST: a chord note that finds the pool full takes (1) a released voice
 *               of either part, the oldest; (2) a voice of the bass part's stack, from the top (the stack thins
 *               towards one, never its last voice: 3y53); only then one of its own (voice_alloc's steal). A bass
 *               note takes a released voice, else the chord's oldest held voice that is not its lowest note (3y53:
 *               the chord has at most seven while a P5 bass note sounds); a stack's extra voices start only into
 *               free room. A bass of ANOTHER engine is core 1's work beside the chord's four voices there: every
 *               P5_BASS_UNITS budget units of its sounding voices count as one voice of the pool (a MONO bass: none);
 *   ONE CORE    P5_ONE_POOL = 4; the chord part four, the bass part one (p5_cap), and the bass's one voice before the
 *               chord's fourth: a bass note takes a released voice, else the chord's oldest held voice that is not
 *               its lowest note.
 * All of it on core 0, before the hand-over (events_block): core 1 renders what it is given. p5_pool_enforce, once
 * a block: a part above its cap or a pool above its size gives voices up at once (core 1 given up under an eight-
 * voice chord: the next block has four; a cap that changed under held notes) */
#define P5_IS(t) (ENGINES[(t)->engine] == &ENG_P5_TEST)
#define P5_POOLED(t) ((uint32_t)((t) - trk) < 2u && P5_IS(t))
#define P5_BASS_UNITS 6u                /* 16 units of an 8-voice engine are 1070 .. 1230 us on the unit (CZ-1 measured,
                                         * the VA estimated), a P5 voice 385 .. 440: about 6 units */
static uint32_t p5_bass_gave;           /* held bass voices given up to the chord part (78 PEAK) */
/* the part's voices that are a whole voice's work: sounding, or stolen for a waiting note and fading as a whole voice
 * (eng_prophet_test.c P5_STEAL_BLOCKS: the pool has no room for another voice until the fade has ended). Not a voice
 * given up for the pool, the budget or the shed: that one is its last sample's line */
static uint32_t p5_sounding(const track_t *t)
{
    const p5_part_t *s = p5_part((uint32_t)(t - trk));
    uint32_t i, n = 0;
    for (i = 0; i < NVOICE; i++)
        n += t->v[i].active && (t->v[i].stage != 4u || (s && s->voice[i % P5_POLY].steal));
    return n;
}
static uint32_t voice_units(const track_t *t);
static uint32_t p5_pool_used(void)
{
    const track_t *bt = &trk[P5_PART_BASS];
    uint32_t u = 0;
    if (P5_IS(&trk[0]))
        u += p5_sounding(&trk[0]);
    if (P5_IS(bt))
        u += p5_sounding(bt);
    else if (cr_dualcore_active())
        u += p5_sounding(bt) * voice_units(bt) / P5_BASS_UNITS;
    return u;
}
static uint32_t p5_pool_size(void) { return cr_dualcore_active() ? P5_POLY : P5_ONE_POOL; }
#define P5_HELD(v) ((v)->active && (v)->gate && (v)->stage != 4u)
/* a held voice of part t to give up: a stack's (MONO, LEGATO, UNISON: voice 0 is the lead) from the top, a POLY
 * part's oldest that is not its lowest note; last: the lead / the lowest note too. NVOICE: none */
static uint32_t p5_part_victim(const track_t *t, int last)
{
    uint32_t i, best = NVOICE, low;
    if (trk_vmode(t) != V_POLY) {
        for (i = NVOICE; i-- > 1u;)
            if (P5_HELD(&t->v[i]))
                return i;
        return last && P5_HELD(&t->v[0]) ? 0u : NVOICE;
    }
    low = lowest_held(t);
    for (i = 0; i < NVOICE; i++)
        if (P5_HELD(&t->v[i]) && i != low && (best == NVOICE || t->v[i].age < t->v[best].age))
            best = i;
    return best != NVOICE || !last ? best : low;
}
/* the voice the pool gives up for a new voice of part self (soft: an extra voice of a stack asks); its index, *pp its
 * part. NVOICE: none (the caller takes one of its own, or does without) */
static uint32_t p5_pool_victim(const track_t *self, int soft, track_t **pp)
{
    track_t *ct = &trk[0], *bt = &trk[P5_PART_BASS];
    uint32_t p, i, best = NVOICE, two = (uint32_t)cr_dualcore_active(), chord = self == ct;
    if (soft && !chord)
        return NVOICE;                                  /* a bass stack's extra voice: only into free room */
    for (p = 0; p < 2u; p++) {                          /* a released voice, the oldest */
        track_t *t = &trk[p];
        if (!P5_IS(t) || (soft && t == self))
            continue;
        for (i = 0; i < NVOICE; i++) {
            const voice_t *v = &t->v[i];
            if (v->active && !v->gate && v->stage != 4u && (best == NVOICE || v->age < (*pp)->v[best].age)) {
                best = i;
                *pp = t;
            }
        }
    }
    if (best != NVOICE)
        return best;
    if (chord && two && P5_IS(bt)) {                    /* two cores: the chord first */
        *pp = bt;
        return p5_part_victim(bt, 0);                   /* never its last voice (3y53: the bass keeps one) */
    }
    if (!chord && P5_IS(ct)) {                          /* the bass's one voice before the chord's last (one core: its
                                                         * fourth; two: its eighth, 3y53) */
        *pp = ct;
        return p5_part_victim(ct, 0);
    }
    return NVOICE;
}
static void voice_kill(voice_t *v);
static void p5_pool_take(const track_t *self, track_t *vp, uint32_t k)
{
    if (self == &trk[0] && vp == &trk[P5_PART_BASS] && vp->v[k].gate)
        p5_bass_gave++;
    voice_kill(&vp->v[k]);
}
static __attribute__((noinline)) int p5_pool_room(track_t *t, int soft)   /* room in the pool for a voice of part t; 0: none to be had */
{
    while (p5_pool_used() >= p5_pool_size()) {
        track_t *vp = 0;
        uint32_t k = p5_pool_victim(t, soft, &vp);
        if (k == NVOICE)
            return 0;
        p5_pool_take(t, vp, k);
    }
    return 1;
}
static __attribute__((noinline)) void p5_pool_enforce(void)
{
    uint32_t p, k, i;
    if (!P5_IS(&trk[0]) && !P5_IS(&trk[P5_PART_BASS]))
        return;
    for (p = 0; p < 2u; p++) {                          /* a part above its cap */
        track_t *t = &trk[p];
        uint32_t cap = trk_nvoice(t), n;
        if (!P5_IS(t) || (n = p5_sounding(t)) <= cap)
            continue;
        for (; n > cap; n--) {
            for (i = 0, k = NVOICE; i < NVOICE; i++)    /* a released voice, the oldest; a held one beyond the cap's
                                                         * slots (voice_alloc does not see it), the oldest; else as
                                                         * the pool takes them */
                if (t->v[i].active && !t->v[i].gate && t->v[i].stage != 4u && (k == NVOICE || t->v[i].age < t->v[k].age))
                    k = i;
            for (i = cap; k == NVOICE && i < NVOICE; i++)
                if (P5_HELD(&t->v[i]))
                    k = i;
            if (k == NVOICE)
                k = p5_part_victim(t, 1);
            if (k == NVOICE)
                break;
            voice_kill(&t->v[k]);
        }
    }
    while (p5_pool_used() > p5_pool_size()) {           /* the pool above its size */
        track_t *vp = 0;
        k = p5_pool_victim(&trk[0], 0, &vp);
        if (k == NVOICE)
            k = p5_pool_victim(&trk[P5_PART_BASS], 0, &vp);
        if (k == NVOICE && P5_IS(&trk[0])) {
            vp = &trk[0];
            k = p5_part_victim(vp, 0);
        }
        if (k == NVOICE)
            break;
        p5_pool_take(&trk[0], vp, k);
    }
}
#endif

/* the budget is full: free room for a voice of part t. 0 = nothing to take (soft) */
static int voice_room(track_t *t, int soft)
{
#if FELUCCA_PROPHET
    if (P5_POOLED(t) && !p5_pool_room(t, soft))
        return 0;                                       /* (P5's pool: a bass voice the chord part's voices leave no
                                                         * room for does not start) */
#endif
    while (voices_busy_of(t) + voice_units(t) > VBUDGET) {
        track_t *vp = 0;
        uint32_t k = voice_victim(t, soft, &vp);
        if (k == NVOICE)
            return !soft;                               /* hard: over the budget (cannot happen: each of the
                                                         * NPART parts protects one voice at most) */
        voice_kill(&vp->v[k]);
    }
    return 1;
}

static voice_t *voice_reuse(track_t *t, voice_t *v)
{
    if (v->stage == 4u)
        voice_room(t, 0);                               /* its budget was taken before this block */
    return v;
}

/* POLY allocation (within the engine's voice cap). DRUM: reuse its lane; other engines: the same note.
 * Else a free voice: ROTATE takes
 * the next one round-robin (release tails ring out), REUSE (or any GLIDE) takes
 * the free voice whose last pitch is closest, so poly portamento moves each
 * voice the shortest way. Steal: the oldest released voice, else the oldest
 * held one, never the lowest held note (the bass). A free voice needs room in the
 * shared budget: when the voice to give up is one of this part's, it is restarted
 * in place instead. */
/* ChoralRoot (cr_out.c sets it; 0 in Felucca and its golden renders): a part's own sounding voice is never
 * restarted in place for another note (the old note's tail cut at once and the new note starting at its level:
 * "a new note cuts the previous one's envelope", on every change of a 6-note chord). It fades out over the next
 * block (stage 4, as a voice given up for another part) and the note waits in vsq for that block: engine_block
 * starts it on the voice the fade freed, 0.7 to 1.5 ms later. A note-off meanwhile drops it. */
static uint8_t voice_fade_steal;
/* 3y45: an engine may fade such a voice over more than the one block (engine_t.steal / fade: P5, whose four voices
 * are stolen at every chord change; docs/PROPHET.md "A stolen voice"). The waiting note starts when the fade is over
 * (engine_block). 0: no engine does (the code as before) */
#ifndef VOICE_STEAL_FADE
#define VOICE_STEAL_FADE FELUCCA_PROPHET
#endif
#if VOICE_STEAL_FADE
/* a voice of the part is still fading (out of line: asked only while a note waits, and the audio ISR's loops stay
 * as they were) */
static __attribute__((noinline)) uint32_t voice_fading(const track_t *t)
{
    uint32_t i, f = 0;
    for (i = 0; i < NVOICE; i++)
        f |= t->v[i].active && t->v[i].stage == 4u;
    return f;
}
/* a killed voice's level at this block's end (env_tick, stage 4), 0: its last block (out of line, as above) */
static __attribute__((noinline)) int32_t voice_fade_level(track_t *t, voice_t *v)
{
    const engine_t *e = ENGINES[t->engine];
    return e->fade ? e->fade(t, v) : 0;
}
#endif
#define VSQ 8u
static struct { uint8_t note[VSQ], vel[VSQ], armed[VSQ], n; } vsq[NPART];   /* armed: its block rendered */
static int vsq_push(track_t *t, uint32_t note, uint32_t vel)   /* 0: full (the caller restarts in place) */
{
    uint32_t p = (uint32_t)(t - trk), i;
    if (p >= NPART)
        return 0;
    for (i = 0; i < vsq[p].n; i++)
        if (vsq[p].note[i] == note) {
            vsq[p].vel[i] = (uint8_t)vel;
            return 1;
        }
    if (vsq[p].n >= VSQ)
        return 0;
    vsq[p].note[vsq[p].n] = (uint8_t)note;
    vsq[p].armed[vsq[p].n] = 0;
    vsq[p].vel[vsq[p].n++] = (uint8_t)vel;
    return 1;
}
static void vsq_drop(track_t *t, uint32_t note)            /* note 128: all */
{
    uint32_t p = (uint32_t)(t - trk), i, k = 0;
    if (p >= NPART)
        return;
    for (i = 0; i < vsq[p].n; i++)
        if (note < 128u && vsq[p].note[i] != note) {
            vsq[p].note[k] = vsq[p].note[i];
            vsq[p].armed[k] = vsq[p].armed[i];
            vsq[p].vel[k++] = vsq[p].vel[i];
        }
    vsq[p].n = (uint8_t)k;
}

static voice_t *voice_alloc(track_t *t, uint32_t note, uint32_t vel)
{
    const engine_t *e = ENGINES[t->engine];
    uint32_t i, np = trk_nvoice(t), best = np, low = np, nfree = 0;
    int32_t bd = 0x7FFFFFFF;
#if FELUCCA_PROPHET
    uint32_t nopool = 0;                                /* P5's pool is full and gives nothing: no free voice */
#define VA_NOPOOL nopool
#else
#define VA_NOPOOL 0
#endif
    if (voice_fade_steal) {                             /* already waiting for a fade: its velocity, once */
        uint32_t p = (uint32_t)(t - trk);
        for (i = 0; p < NPART && i < vsq[p].n; i++)
            if (vsq[p].note[i] == note) {
                vsq[p].vel[i] = (uint8_t)vel;
                return 0;
            }
    }
    if (e->alloc) {                                     /* the engine's own choice (FM6: Dexed's; Melodee's voice
                                                         * model): a voice it takes while sounding is its own steal,
                                                         * restarted in place as Dexed does (FM6 keeps no KEY SYNC
                                                         * on it: no click), not ChoralRoot's fade-and-defer */
        voice_t *v = &t->v[e->alloc(t, note) % np];
        if (!v->active || v->stage == 4u)
            voice_room(t, 0);
        return v;
    }
    if (t->engine == ENGI_DRUM) {
        voice_t *v = drum_reuse(t, note);
        if (v)
            return voice_reuse(t, v);
    }
    for (i = 0; i < np; i++) {
        if (t->v[i].active && t->v[i].note == note) {
            /* ChoralRoot: a sampled engine restarts its sample on a retrigger (a step at the voice's level): the
             * sounding voice fades over a block, the note starts fresh after it (vsq) */
            if (voice_fade_steal && ENGINES[t->engine]->sampled && t->v[i].env_out && t->v[i].stage != 4u &&
                vsq_push(t, note, vel)) {
                voice_kill(&t->v[i]);
                return 0;
            }
            return voice_reuse(t, &t->v[i]);
        }
        nfree += !t->v[i].active;
    }
    while (nfree && voices_busy_of(t) + voice_units(t) > VBUDGET) {
        track_t *vp = 0;
        uint32_t k = voice_victim(t, 0, &vp);
        if (k < np && vp == t && (!voice_fade_steal || !t->v[k].env_out)) {
            voice_steals++;
            return &t->v[k];                            /* our own: restart it in place (no click) */
        }
        if (k >= NVOICE)
            break;
        voice_kill(&vp->v[k]);
    }
#if FELUCCA_PROPHET
    if (P5_POOLED(t))                                   /* P5's pool (above): the chord first */
        while (nfree && p5_pool_used() >= p5_pool_size()) {
            track_t *vp = 0;
            uint32_t k = p5_pool_victim(t, 0, &vp);
            if (k == NVOICE) {
                nopool = 1;
                break;
            }
            if (vp == t && k < np && (!voice_fade_steal || !t->v[k].env_out)) {
                voice_steals++;
                return &t->v[k];                        /* our own released voice, not heard yet: in place */
            }
            p5_pool_take(t, vp, k);
        }
#endif
    if (VA_NOPOOL) {
    } else if (t->p[P_ALLOC] || t->p[P_GLIDE]) {
        for (i = 0; i < np; i++) {
            int32_t d = t->v[i].pitch_cur - (int32_t)note * 16;
            if (t->v[i].active)
                continue;
            d = d < 0 ? -d : d;
            if (!t->v[i].pitch_cur)
                d = 0x7FFFFFF0;                          /* never played: last choice */
            if (d < bd) {
                bd = d;
                best = i;
            }
        }
    } else {
        for (i = 0; i < np && best == np; i++) {
            uint32_t k = (t->rr + i) % np;
            if (!t->v[k].active)
                best = k;
        }
        t->rr = (uint8_t)((best == np ? t->rr : best) + 1u) % np;
    }
    if (best < np)
        return &t->v[best];
    for (i = 0; i < np; i++)                             /* the lowest held note is protected */
        if (t->v[i].gate && (low == np || t->v[i].note < t->v[low].note))
            low = i;
    for (i = 0; i < np; i++)                             /* oldest released (not one fading out for another part) */
        if (!t->v[i].gate && t->v[i].stage != 4u && (!VA_NOPOOL || t->v[i].active) &&   /* (P5's pool full: a free
                                                         * slot is not a voice to take) */
            (best == np || t->v[i].age < t->v[best].age))
            best = i;
    if (best == np)
        for (i = 0; i < np; i++)                         /* oldest held, not the bass */
            if (i != low && (!voice_fade_steal || t->v[i].stage != 4u) && (!VA_NOPOOL || t->v[i].active) &&   /* (3y53:
                                                         * the pool full, the bass's one voice kept: a free slot is
                                                         * not a voice to take) */
                (best == np || t->v[i].age < t->v[best].age))
                best = i;
#if FELUCCA_PROPHET
    /* P5 (3y46): before a HELD voice of the part is taken: a slot of the part that is fading out and that no waiting
     * note has claimed is free in a block or two (a voice the pool gave up for the bass's key, or for a note struck
     * again on a voice that was fading). The note waits for that slot (vsq, nothing taken), and when it comes back
     * the pool gives it its room: the chord first. Without this a chord struck over its own release with a P5 bass
     * under it took one of its own new notes while the bass kept its voice */
#ifndef P5_WAIT_SLOT
#define P5_WAIT_SLOT 1                                  /* (0: as before it, for the test's proof) */
#endif
    if (P5_WAIT_SLOT && voice_fade_steal && P5_POOLED(t) && best < np && t->v[best].gate) {
        uint32_t f = 0, pt = (uint32_t)(t - trk);
        for (i = 0; i < np; i++)
            f += t->v[i].active && t->v[i].stage == 4u;
        if (f > vsq[pt].n) {
            best = np;
            nopool = 0;
        }
    }
#endif
    if (VA_NOPOOL && best == np)
        return 0;                                       /* (P5's pool: none of our own to take either: no note) */
    voice_steals++;
    if (voice_fade_steal && !(best < np && !t->v[best].env_out) && vsq_push(t, note, vel)) {   /* fade it, the note
                                                         * next block (vsq); one not rendered yet (silent): in place */
        if (best < np) {
            voice_kill(&t->v[best]);
#if VOICE_STEAL_FADE
            if (e->steal)
                e->steal(t, &t->v[best]);
#endif
        }
        return 0;
    }
    return voice_reuse(t, &t->v[best == np ? 0 : best]);
}

/* glide: RATE = a fixed speed, TIME = the same time for any interval */
static void glide_set(track_t *t, voice_t *v, int glide)
{
    if (!glide || !v->pitch_cur) {
        v->pitch_cur = v->pitch16;
        v->gstep = 0;
        return;
    }
    if (t->p[P_GLMODE]) {
        int32_t d = v->pitch16 - v->pitch_cur, ticks = 1 + t->p[P_GLIDE] * t->p[P_GLIDE] / 8;
        d = d < 0 ? -d : d;
        v->gstep = d / ticks > 0 ? d / ticks : 1;
    } else {
        v->gstep = 0;
    }
}

static void voice_start(track_t *t, voice_t *v, uint32_t note, uint32_t vel, int glide)
{
    const engine_t *e = ENGINES[t->engine];
    int sounding = v->active && v->stage != 0;
    uint32_t ph0 = v->ph[0], ph1 = v->ph[1], ph2 = v->ph[2];
    int32_t s0 = v->s[0], s1 = v->s[1], s4 = v->s[4], s5 = v->s[5], s6 = v->s[6], s7 = v->s[7];
    uint32_t keep = e->keep;
    voice_was = (uint8_t)(!v->active ? 0u : v->gate ? 2u : 1u);
    v->note = (uint8_t)note;
    v->vel = (uint8_t)vel;
    v->gate = 1;
    v->active = 1;
    v->stage = 1;
    v->age = ++vage;
    v->pitch16 = (int32_t)note * 16;
    v->mvel = t->m_vel;                                 /* mod.c: the note's VEL and RAND */
    v->mrnd = t->m_rnd;
    t->m_vi = (uint8_t)(v - t->v);
    glide_set(t, v, glide);
    if (!sounding) {
        v->env = 0;
        v->env_out = 0;
    }                                                   /* sounding: the attack starts from the current level */
    e->note_on(t, v);
    if (sounding && !e->sampled) {                     /* retrigger / steal: keep phases and filter */
        v->ph[0] = ph0;                                 /* states (resetting them clicks) */
        v->ph[1] = ph1;
        v->ph[2] = ph2;
        if (keep & 0x01u)                               /* engine_t.keep: the slots the engines use */
            v->s[0] = s0;
        if (keep & 0x02u)
            v->s[1] = s1;
        if (keep & 0x10u)
            v->s[4] = s4;
        if (keep & 0x20u)
            v->s[5] = s5;
        if (keep & 0x40u)
            v->s[6] = s6;
        if (keep & 0x80u)
            v->s[7] = s7;
    }
}

/* MONO / LEGATO / UNISON: one note on one voice (NPOLY for UNISON, or the
 * engine's voice cap, spread by DETUNE over the same width) */
static void mono_play(track_t *t, uint32_t note, uint32_t vel, int retrig, int glide)
{
    uint32_t mode = (uint32_t)t->p[P_VOICE], nv = mode == V_UNISON ? trk_nvoice(t) : 1u, i;
    for (i = 0; i < nv; i++) {
        voice_t *v = &t->v[i];
        int32_t k = 2 * (int32_t)i - (int32_t)(nv - 1u);   /* -7 .. 7 */
        if (nv > 1u && nv != NPOLY)
            k = k * (NPOLY - 1) / (int32_t)(nv - 1u);       /* other counts: the outer ones as wide */
        v->fine = nv > 1u ? k * t->p[P_DETUNE] * 56 / 889 : 0;   /* up to ~±40 cents */
        if (retrig || !v->active || !v->gate) {
            if ((!v->active || v->stage == 4u) && !voice_room(t, i > 0))
                continue;                                   /* no room for this extra UNISON voice */
            /* level: about the same sum for 8 or 4 voices at random phases */
            voice_start(t, v, note, nv >= NPOLY ? vel * 36u / 100u : nv > 1u ? vel / 2u : vel, glide);
            if (nv > 1u && i && !ENGINES[t->engine]->sampled) {   /* random start phases: */
                static uint32_t seed = 0x1234567u;          /* in phase they stack, evenly spread they cancel */
                seed = seed * 1664525u + 1013904223u;
                v->ph[0] += seed;
            }
        } else {                                            /* legato: new pitch, same envelope */
            v->note = (uint8_t)note;
            v->pitch16 = (int32_t)note * 16;
            v->mvel = t->m_vel;
            v->mrnd = t->m_rnd;
            if (vel > v->vel && nv == 1u)
                v->vel = (uint8_t)vel;
            glide_set(t, v, glide);
            if (ENGINES[t->engine]->legato)
                ENGINES[t->engine]->legato(t, v);
        }
    }
    t->mono_note = (uint8_t)note;
}

/* the note the mono stack asks for (PRIO: LAST / LOW / HIGH), 0 = none */
static uint32_t mono_pick(const track_t *t)
{
    uint32_t i, n;
    if (!t->nmono)
        return 0;
    n = t->mono_stack[t->nmono - 1u];
    for (i = 0; i < t->nmono; i++) {
        if (t->p[P_PRIO] == 1 && t->mono_stack[i] < n)
            n = t->mono_stack[i];
        if (t->p[P_PRIO] == 2 && t->mono_stack[i] > n)
            n = t->mono_stack[i];
    }
    return n;
}

static void mono_remove(track_t *t, uint32_t note)
{
    uint32_t j, k = 0;
    for (j = 0; j < t->nmono; j++)
        if (t->mono_stack[j] != note)
            t->mono_stack[k++] = t->mono_stack[j];
    t->nmono = (uint8_t)k;
}

static void trk_note_on(track_t *t, uint32_t note, uint32_t vel)
{
    uint32_t any = 0, i, mode = trk_vmode(t);
    if (t->p[P_MUTE])
        return;
    if (t->xf_on || t->eng_req != t->engine) {          /* engine switch under way: after the fade */
        for (i = 0; i < t->xp_n && t->xp_note[i] != note; i++)
            ;
        if (i == t->xp_n && t->xp_n < 4u)
            t->xp_n++;
        i = i < t->xp_n ? i : t->xp_n - 1u;             /* (full: the last one is replaced) */
        t->xp_note[i] = (uint8_t)note;
        t->xp_vel[i] = (uint8_t)vel;
        return;
    }
    mod_note(t, note, vel);                             /* the matrix's VEL / KEY / RAND */
    for (i = 0; i < NVOICE; i++)
        any |= t->v[i].gate;
    if (!any) {                                        /* fresh phrase: LFO retrigger and fade */
        t->lfo_ph = (uint32_t)t->p[P_LPHASE] << 25;
        t->lfo_fade = 0;
    }
    if (mode == V_POLY) {
        voice_t *v = voice_alloc(t, note, vel);
        t->nmono = 0;                                   /* no stale mono stack after a mode change */
        t->mono_note = 0;
        if (!v)
            return;                                     /* (voice_fade_steal: it starts next block) */
        v->fine = 0;
        voice_start(t, v, note, vel, t->p[P_GLIDE] != 0 && !ENGINES[t->engine]->oneshot);
        return;
    }
    {
        uint32_t sounding = t->mono_note && t->v[0].active && t->v[0].gate, want;
        mono_remove(t, note);
        if (t->nmono >= NVOICE)
            mono_remove(t, t->mono_stack[0]);
        t->mono_stack[t->nmono++] = (uint8_t)note;
        if (ENGINES[t->engine]->mono_key)
            ENGINES[t->engine]->mono_key(t, note);
        want = mono_pick(t);
        if (sounding && want == t->mono_note)
            return;                                     /* LOW / HIGH priority: this key does not win */
        if (mode == V_MONO)                             /* MONO: always retrigger, glide when GLIDE is set */
            mono_play(t, want, vel, 1, t->p[P_GLIDE] != 0 || t->slide_glide);
        else                                            /* LEGATO / UNISON: glide only between held notes */
            mono_play(t, want, vel, !sounding, sounding && (t->p[P_GLIDE] != 0 || t->slide_glide));
    }
}

static void trk_note_off(track_t *t, uint32_t note)
{
    uint32_t i, k = 0, mode = trk_vmode(t);
    for (i = 0; i < t->xp_n; i++)                       /* not sounding yet (engine switch): forget it */
        if (t->xp_note[i] != note) {
            t->xp_note[k] = t->xp_note[i];
            t->xp_vel[k++] = t->xp_vel[i];
        }
    t->xp_n = (uint8_t)k;
    vsq_drop(t, note);                                 /* waiting for a stolen voice's fade: forget it */
    mono_remove(t, note);                              /* a mode change must not retain a released key */
    if (mode != V_POLY) {
        uint32_t nv = mode == V_UNISON ? trk_nvoice(t) : 1u;
        if (t->mono_note == note) {
            uint32_t next = mono_pick(t);
            if (next) {                                 /* fall back to a held note, legato */
                mono_play(t, next, t->v[0].vel, 0, t->p[P_GLIDE] != 0);
            } else {
                for (i = 0; i < nv; i++)
                    if (t->v[i].gate) {
                        t->v[i].gate = 0;
                        t->v[i].stage = 3;
                    }
                t->mono_note = 0;
            }
        }
    }
    for (i = 0; i < NVOICE; i++)                        /* a fallback above already has its new pitch */
        if (t->v[i].note == note && t->v[i].gate) {
            t->v[i].gate = 0;
            t->v[i].stage = 3;
        }
}

static void trk_all_off(track_t *t)
{
    const engine_t *e = ENGINES[t->engine];
    uint32_t i;
    for (i = 0; i < NVOICE; i++) {
        t->v[i].gate = 0;
#if VOICE_STEAL_FADE
        if (e->fade && t->v[i].active && t->v[i].stage == 4u)
            continue;                                   /* (a stolen voice's fade under way goes on to its end) */
#endif
        t->v[i].stage = t->v[i].active ? 3 : 0;
        if (t->v[i].active && e->endless && e->endless(t, &t->v[i]))
            t->v[i].stage = 4;                          /* a release that never ends (QUAD's REL INF): this
                                                         * block fades it (env_tick stage 4, as voice_kill) */
    }
    t->nmono = 0;
    t->mono_note = 0;
    t->xp_n = 0;
    vsq_drop(t, 128u);
}

/* engine switch, at each block start (events_block), before any note of the block. The UI writes
 * eng_req and the new engine's P_E0..P_E7 together (IRQ off). The part's sounding voices (released by
 * the preset change) then fade out over XF_BLOCKS blocks on the old engine, rendered with its own
 * parameters (pe_old: an engine never reads another engine's values, which index its tables); only
 * then the engine switches and the notes that came during the fade start on it. A part with nothing
 * sounding switches at once. */
#define XF_BLOCKS 4u                                    /* 4 x 32 samples: 2.9 ms */
static void engine_block(track_t *t)
{
    uint32_t i, any = 0;
#if FELUCCA_PROPHET
    if (t == &trk[0])
        p5_pool_enforce();                              /* (P5's voices over the two parts: once a block) */
#endif
    if (t->xf_on || t->eng_req != t->engine) {
        if (!t->xf_on) {
            for (i = 0; i < NVOICE; i++)
                any |= t->v[i].active;
            if (any) {
                t->xf_on = 1;
                t->xf = XF_BLOCKS;
                return;
            }
        } else if (t->xf) {
            return;                                     /* still fading (track_render counts down) */
        }
        for (i = 0; i < NVOICE; i++) {                  /* faded to 0: gone */
            voice_t *v = &t->v[i];
            v->active = v->gate = 0;
            v->stage = 0;
            v->env = v->env_out = 0;
        }
        if (t->engine != eng_idx(t->eng_req))           /* another engine: its state as at power-on */
            eng_state_clear((uint32_t)(t - trk));
        t->engine = eng_idx(t->eng_req);
        t->xf_on = 0;
        t->nmono = 0;
        t->mono_note = 0;
        {
            uint32_t n = t->xp_n;
            t->xp_n = 0;
            for (i = 0; i < n; i++)
                trk_note_on(t, t->xp_note[i], t->xp_vel[i]);
        }
    }
    for (i = 0; i < 8u; i++)                            /* the engine's own values, for a later fade */
        t->pe_old[i] = t->p[P_E0 + i];
    {   /* notes that waited one rendered block for a stolen voice's fade (voice_alloc, vsq): the armed ones start
         * (a voice still missing: it fades another and waits again), the others are armed for the next block */
        uint32_t p = (uint32_t)(t - trk);
        if (p < NPART && vsq[p].n) {
            uint8_t nn[VSQ], vv[VSQ];
            uint32_t n = 0, k = 0;
#if VOICE_STEAL_FADE                                    /* a longer fade still running: the notes wait for its end */
            uint32_t hold = ENGINES[t->engine]->fade ? voice_fading(t) : 0u;
#define VSQ_HOLD hold
#else
#define VSQ_HOLD 0
#endif
            for (i = 0; i < vsq[p].n; i++) {
                if (vsq[p].armed[i] && !VSQ_HOLD) {
                    nn[n] = vsq[p].note[i];
                    vv[n++] = vsq[p].vel[i];
                } else {
                    vsq[p].note[k] = vsq[p].note[i];
                    vsq[p].vel[k] = vsq[p].vel[i];
                    vsq[p].armed[k++] = 1;
                }
            }
            vsq[p].n = (uint8_t)k;
            for (i = 0; i < n; i++)
                trk_note_on(t, nn[i], vv[i]);
        }
    }
}

/* one control tick (CTL samples) of the amplitude envelope; returns Q15 */
static int32_t env_tick(track_t *t, voice_t *v)
{
    const int16_t *p = t->p;
    int32_t sus = (int32_t)p[P_SUS] << 17;              /* Q24 */
    switch (v->stage) {
    case 1:
        v->env += (int32_t)ENV_LIN[p[P_ATK] & 127];
        if (v->env >= (1 << 24)) {
            v->env = 1 << 24;
            v->stage = 2;
        }
        break;
    case 2:
        v->env += mulq16(sus - v->env, ENV_EXP[p[P_DEC] & 127]);
        break;
    case 3:
        v->env -= mulq16(v->env, ENV_EXP[p[P_REL] & 127]);
        if (v->env < (1 << 12)) {
            v->env = 0;
            v->stage = 0;
            v->active = 0;
        }
        break;
    case 4:                                             /* given up for another part: this block fades it */
#if VOICE_STEAL_FADE
        {   /* (.. or, a voice the engine fades over its own blocks: the level this block ends at, 0: the last one) */
            int32_t g = voice_fade_level(t, v);
            if (g) {
                v->env = g << 9;
                break;
            }
        }
#endif
        v->env = 0;
        v->stage = 0;
        v->active = 0;
        break;
    default:
        v->env = 0;
        break;
    }
    return v->env >> 9;
}

/* render one block of a part into out (cleared here); returns the voices rendered. An engine with render2 (VA)
 * also renders a stereo side into part_side: part_side_on = 1 when a voice added to it (fx.c mix_part plays the
 * part as mid - side / mid + side), else the part is mono as before */
/* Channel bend is live performance state, outside projects/presets. Q8 semitones. */
static int32_t midi_bend_q8[NTRK], midi_bend_target[NTRK];
static int32_t part_side[CTL];
static uint8_t part_side_on;
/* side / side_on: the part's stereo side (part_side, or core 1's own: fx.c); lfo_pre: 0 = tick the LFO here, else the
 * caller ticked it (before the hand-off to core 1, so the shared generator's draws keep one core's order: cr_cpu1.h)
 * and this is the value it had before, the one the block renders with */
/* THE CHORD PART'S VOICES ON TWO CORES (P5 3y46, the VA 3y177; fx.c p5x_run, docs/DUALCORE.md "What each core runs").
 * While p5x.on (fx.c mix_parts_split: two cores, an engine with engine_t.split on the chord part) this function, for
 * part 0, does everything of a voice but its samples (the envelope's tick, the pitch, the matrix: the voice's vmod_t)
 * and puts the voice on a list; p5x_run then gives a part of the list to core 1 (in the bass part's job), renders the
 * rest here, waits and adds the two sums (the mid and, for render2, the side). A voice's render writes its own state
 * and the buffers it is given, and reads the part's block (engine_t.block: before the voices, on core 0) and its
 * parameters: no word is both cores' (P5: eng_prophet_test.c p5_render, docs/PROPHET.md "What a voice's render
 * touches"; the VA: eng_va.c va_render, docs/VA.md "Two cores"). (The names are 3y46's, when P5 alone was split.) */
#if FELUCCA_PROPHET && CR_CPU1
#define P5_SPLIT 1
typedef struct { voice_t *v; vmod_t m; uint8_t c1; } p5x_item_t;   /* c1: core 1 renders it */
static struct {
    uint8_t on;                         /* this block: part 0's voices go on the list (core 0 sets and clears it) */
    uint8_t n, n1;                      /* voices on the list; how many of them are core 1's */
    p5x_item_t it[NVOICE];
} p5x __attribute__((section(".pool")));              /* (cleared at power-on with the pool; RAM is the tight region) */
static void p5x_run(track_t *t, int32_t *out, int32_t *side, uint8_t *side_on, uint32_t n);
#else
#define P5_SPLIT 0
#endif
CR_HOT_CORE static uint32_t track_render_x(track_t *t, int32_t *out, int32_t *side, uint8_t *side_on, uint32_t n,
                               const int32_t *lfo_pre)
{
    const engine_t *e = ENGINES[t->engine];
    const int16_t *p = t->p;
    uint32_t i;
    int32_t lfo = lfo_pre ? *lfo_pre : mulq15(t->lfo_val, t->lfo_fade);
    /* TUNE and the bend are worked out for the first voice that sounds (pitch_set: a silent part has no use for
     * them), and only when they are not 0, where their divides give 0 */
    int32_t tune = 0, tune_fine = 0, bend, bend16 = 0, bend_fine = 0, pitch_set = 0;
    uint32_t ti = (uint32_t)(t - trk);
    if (ENGINES[eng_idx(t->eng_req)] == &ENG_DRUM) {
        midi_bend_q8[ti] = midi_bend_target[ti] = 0;
    } else {
        int32_t d = midi_bend_target[ti] - midi_bend_q8[ti];
        /* ~6 ms smoothing, with an exact landing (no permanent small offset). */
        midi_bend_q8[ti] += d > 0 ? (d < 4 ? d : (d + 3) / 4) : (d > -4 ? d : (d - 3) / 4);
    }
    bend = midi_bend_q8[ti];
    uint32_t nr = 0, fade = t->xf_on && t->xf;
    int16_t pe_new[8];
    for (i = 0; i < n; i++)
        out[i] = 0;
    *side_on = 0;
    if (e->render2)
        for (i = 0; i < n; i++)
            side[i] = 0;
    if (fade)                                           /* engine switch: the old engine, its own values */
        for (i = 0; i < 8u; i++) {
            pe_new[i] = t->p[P_E0 + i];
            t->p[P_E0 + i] = t->pe_old[i];
        }
    if (!lfo_pre)
        track_lfo_tick(t);
    if (e->block)                                       /* the engine's per-part work (WHEEL: bars, rotor) */
        e->block(t);
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &t->v[i];
        vmod_t m;
        int32_t env, pitch;
        if (!v->active)
            continue;
        if (!pitch_set) {
            int32_t g = song.g[G_TUNE];
            pitch_set = 1;
            if (g) {    /* TUNE in cents: whole 1/16 semitones in the pitch, the rest as a fine factor (no dead zone) */
                tune = g >= 0 ? g * 16 / 100 : -((-g * 16 + 99) / 100);
                tune_fine = (g * 16 - tune * 100) * 2367 / 16000;   /* rest, in 1/4096 (1 ct = 2.367) */
            }
            if (bend) {
                bend16 = bend >= 0 ? bend / 16 : -((-bend + 15) / 16);
                bend_fine = (bend - bend16 * 16) * 2367 / 2560;
            }
        }
        {
            env = env_tick(t, v);
            if (e->ownenv && v->active) {               /* the engine's envelopes (FM6): they end the voice */
                int dn = e->done(t, v);
                if (dn && !e->alloc) {                  /* (Felucca's, bit for bit: the other engines) */
                    v->active = v->gate = 0;
                    v->stage = 0;
                    v->env = v->env_out = 0;
                    continue;
                } else if (dn) {                        /* FM6 (Melodee's): its last block, faded out (the engine's
                                                         * state goes on one more block, as Dexed's) */
                    v->active = v->gate = 0;
                    v->stage = 0;
                    v->env = 0;
                    env = 0;
#if VOICE_STEAL_FADE
                } else if (v->stage == 4u) {            /* (a longer fade, engine_t.fade: env is its level) */
#endif
                } else {
                    v->env = 1 << 24;                   /* (the ADSR's release never ends it) */
                    env = 32767;
                }
            }
            if (e->amp)                                 /* the engine's own amplitude curve */
                env = e->amp(t, v, env);
            m.envq15 = e->ownenv ? 0 : env;
            m.amp1 = e->ownenv || cr_touch_mode ? env : mulq15(env, v->vel * 258);   /* Key Touch on: the level follows
                                                                                      * the EXPR routing (mod.c) */
            if (p[P_LD_AMP])
                m.amp1 = mulq15(m.amp1, 32767 - mulq15((lfo + 32768) >> 1, p[P_LD_AMP] * 258));
            if (fade)                                   /* linear to 0 over the fade */
                m.amp1 = m.amp1 * (int32_t)(t->xf - 1u) / (int32_t)XF_BLOCKS;
            m.amp0 = v->env_out;
            v->env_out = m.amp1;
        }
        if (v->pitch_cur != v->pitch16) {               /* glide */
            int32_t d = v->pitch16 - v->pitch_cur;
            int32_t st = v->gstep ? v->gstep : (int32_t)(ENV_LIN[(p[P_GLIDE] ? p[P_GLIDE] : 40) & 127] >> 14);   /* slide: ~45 ms */
            if (st < 1)
                st = 1;
            v->pitch_cur += d > 0 ? (d < st ? d : st) : (-d < st ? d : -st);
        }
        if (!env && !m.amp0 && v->stage == 2 && !e->sampled)
            continue;                                   /* held at a silent sustain (SUS 0): nothing to render */
        pitch = v->pitch_cur + tune + bend16 + ((lfo * p[P_LD_PIT] * 3) >> 15) + ((m.envq15 * p[P_ED_PIT] * 3) >> 15);
        m.pitch16 = clamp(pitch, 0, 2047);
        m.inc = pitch_inc(m.pitch16);
        m.fine = v->fine + tune_fine + bend_fine;
        if (v->fine + tune_fine + bend_fine)             /* residual below 1/16 semitone */
            m.inc += (uint32_t)((int32_t)(m.inc >> 12) * (v->fine + tune_fine + bend_fine));
        m.cutoff = ((lfo * p[P_LD_FLT]) >> 7) + ((m.envq15 * p[P_ED_FLT]) >> 7);
        if (v->vel > 110)                               /* accent opens the filter with the env */
            m.cutoff += (m.envq15 * 24) >> 7;
        m.shape = (64 << 8) + ((lfo * p[P_LD_SHP]) >> 7) + ((m.envq15 * p[P_ED_SHP]) >> 7);
        if (MODP(t).on)                                     /* the modulation matrix (mod.c) */
            mod_voice(t, v, &m, v->fine + tune_fine + bend_fine);
        /* 1/16 st and 1/4096 -> Q24 octaves: the voice's own offset (the matrix's pitch too), without TUNE and bend */
        m.plog = (m.pitch16 - tune - bend16 - v->pitch16) * 87381 + (m.fine - tune_fine - bend_fine) * 5909;
#if P5_SPLIT
        if (t == &trk[0] && p5x.on) {                   /* (on two cores: its samples later, p5x_run) */
            p5x.it[p5x.n].v = v;
            p5x.it[p5x.n++].m = m;
        } else
#endif
        if (e->render2)                                 /* (VA: SPREAD / USPREAD, a stereo side) */
            *side_on |= (uint8_t)e->render2(t, v, out, side, n, &m);
        else
            e->render(t, v, out, n, &m);
        nr++;
    }
#if P5_SPLIT
    if (t == &trk[0] && p5x.on)
        p5x_run(t, out, side, side_on, n);
#endif
    if (e->post)
        e->post(t, out, n, nr);
    if (!nr && e->tail)                                 /* (P5: the part's own tail after its last voice) */
        nr = e->tail(t);
    if (fade) {
        for (i = 0; i < 8u; i++)
            t->p[P_E0 + i] = pe_new[i];
        t->xf--;
    }
    return nr;
}
static uint32_t track_render(track_t *t, int32_t *out, uint32_t n)
{
    return track_render_x(t, out, part_side, &part_side_on, n, 0);
}
