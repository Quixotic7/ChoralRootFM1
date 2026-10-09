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

static void track_lfo_tick(track_t *t)
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
        uint32_t mode = trk_vmode(t);
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

/* the budget is full: free room for a voice of part t. 0 = nothing to take (soft) */
static int voice_room(track_t *t, int soft)
{
    while (voices_busy() + voice_units(t) > VBUDGET) {
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
    while (nfree && voices_busy() + voice_units(t) > VBUDGET) {
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
    if (t->p[P_ALLOC] || t->p[P_GLIDE]) {
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
        if (!t->v[i].gate && t->v[i].stage != 4u && (best == np || t->v[i].age < t->v[best].age))
            best = i;
    if (best == np)
        for (i = 0; i < np; i++)                         /* oldest held, not the bass */
            if (i != low && (!voice_fade_steal || t->v[i].stage != 4u) && (best == np || t->v[i].age < t->v[best].age))
                best = i;
    voice_steals++;
    if (voice_fade_steal && !(best < np && !t->v[best].env_out) && vsq_push(t, note, vel)) {   /* fade it, the note
                                                         * next block (vsq); one not rendered yet (silent): in place */
        if (best < np)
            voice_kill(&t->v[best]);
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
            for (i = 0; i < vsq[p].n; i++) {
                if (vsq[p].armed[i]) {
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
static uint32_t track_render(track_t *t, int32_t *out, uint32_t n)
{
    const engine_t *e = ENGINES[t->engine];
    const int16_t *p = t->p;
    uint32_t i;
    int32_t lfo = mulq15(t->lfo_val, t->lfo_fade);
    /* TUNE in cents: whole 1/16 semitones in the pitch, the rest as a fine factor (no dead zone) */
    int32_t tune = song.g[G_TUNE] >= 0 ? song.g[G_TUNE] * 16 / 100 : -((-song.g[G_TUNE] * 16 + 99) / 100);
    int32_t tune_fine = (song.g[G_TUNE] * 16 - tune * 100) * 2367 / 16000;   /* rest, in 1/4096 (1 ct = 2.367) */
    int32_t bend, bend16, bend_fine;
    uint32_t ti = (uint32_t)(t - trk);
    if (ENGINES[eng_idx(t->eng_req)] == &ENG_DRUM) {
        midi_bend_q8[ti] = midi_bend_target[ti] = 0;
    } else {
        int32_t d = midi_bend_target[ti] - midi_bend_q8[ti];
        /* ~6 ms smoothing, with an exact landing (no permanent small offset). */
        midi_bend_q8[ti] += d > 0 ? (d < 4 ? d : (d + 3) / 4) : (d > -4 ? d : (d - 3) / 4);
    }
    bend = midi_bend_q8[ti];
    bend16 = bend >= 0 ? bend / 16 : -((-bend + 15) / 16);
    bend_fine = (bend - bend16 * 16) * 2367 / 2560;
    uint32_t nr = 0, fade = t->xf_on && t->xf;
    int16_t pe_new[8];
    for (i = 0; i < n; i++)
        out[i] = 0;
    part_side_on = 0;
    if (e->render2)
        for (i = 0; i < n; i++)
            part_side[i] = 0;
    if (fade)                                           /* engine switch: the old engine, its own values */
        for (i = 0; i < 8u; i++) {
            pe_new[i] = t->p[P_E0 + i];
            t->p[P_E0 + i] = t->pe_old[i];
        }
    track_lfo_tick(t);
    if (e->block)                                       /* the engine's per-part work (WHEEL: bars, rotor) */
        e->block(t);
    for (i = 0; i < NVOICE; i++) {
        voice_t *v = &t->v[i];
        vmod_t m;
        int32_t env, pitch;
        if (!v->active)
            continue;
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
                } else {
                    v->env = 1 << 24;                   /* (the ADSR's release never ends it) */
                    env = 32767;
                }
            }
            if (e->amp)                                 /* the engine's own amplitude curve */
                env = e->amp(t, v, env);
            m.envq15 = e->ownenv ? 0 : env;
            m.amp1 = e->ownenv ? env : mulq15(env, v->vel * 258);
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
        if (mod.on)                                     /* the modulation matrix (mod.c) */
            mod_voice(t, v, &m, v->fine + tune_fine + bend_fine);
        /* 1/16 st and 1/4096 -> Q24 octaves: the voice's own offset (the matrix's pitch too), without TUNE and bend */
        m.plog = (m.pitch16 - tune - bend16 - v->pitch16) * 87381 + (m.fine - tune_fine - bend_fine) * 5909;
        if (e->render2)                                 /* (VA: SPREAD / USPREAD, a stereo side) */
            part_side_on |= (uint8_t)e->render2(t, v, out, part_side, n, &m);
        else
            e->render(t, v, out, n, &m);
        nr++;
    }
    if (e->post)
        e->post(t, out, n, nr);
    if (fade) {
        for (i = 0; i < 8u; i++)
            t->p[P_E0 + i] = pe_new[i];
        t->xf--;
    }
    return nr;
}
