/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: the semantic looper (cr_loop.h, docs/LOOPER.md). The grid version's d_cr_loop.lua and
 * design.md section 14, with bar sync, the count-in, quantize on commit and beat-relative time added.
 *
 * Time: lp->t counts ticks (CRL_PPQN per quarter) of the engine's tempo, derived from the ms clock with the
 * remainder kept (no drift). Recording stores, per gesture, its on tick relative to the loop start and its length
 * in ticks; playback compares positions in the cycle, so a BPM change moves the loop's speed at once.
 * Capture: the engine's gesture hook (cr_out_t.gesture) says when a chord gesture begins and ends, with its final
 * resolved chord at the end; an event is written when the gesture ends (or when the capture ends with it held). */
#ifndef CR_LOOP_H
#include "cr_loop.h"
#endif

static const uint8_t CRL_BARS[CRL_NSYNC] = {0, 1, 2, 4, 8, 16};
static const uint8_t CRL_QGRID[CRL_NQUANT] = {0, 96, 48, 32, 24, 16, 12};
/* the Rate in 48ths of a loop tick a tempo tick: x1/16 x1/12 x1/8 x1/6 x1/4 x1/2 x3/4 x1 x1.5 x2 x4 (48: the smallest
 * unit that holds a 16th, a 12th and a 6th; until 2026-10-11 quarters, x1/4 the slowest). Each divides CRL_Q48 or is a
 * multiple of 12, so the loop's ticks fall on the same tempo ticks every cycle */
#define CRL_Q48 48u
static const uint8_t CRL_RATE_Q48[CRL_NRATE] = {3, 4, 6, 8, 12, 24, 36, 48, 72, 96, 192};

static void crl_zero(void *p, uint32_t n)
{
    uint8_t *b = (uint8_t *)p;
    while (n--) *b++ = 0;
}
static void crl_copy(void *d, const void *s, uint32_t n)
{
    uint8_t *a = (uint8_t *)d;
    const uint8_t *b = (const uint8_t *)s;
    while (n--) *a++ = *b++;
}
static uint32_t crl_data_size(uint32_t nev) { return (uint32_t)(sizeof(crl_data_t) - sizeof(crl_ev_t) * (CRL_MAX_EV - nev)); }

uint32_t cr_loop_bar(const cr_loop_t *lp) { return lp->sig == CRL_SIG_44 ? 4u * CRL_PPQN : 3u * CRL_PPQN; }
uint32_t cr_loop_beat(const cr_loop_t *lp) { return lp->sig == CRL_SIG_68 ? CRL_PPQN / 2u : CRL_PPQN; }
int cr_loop_layers(const cr_loop_t *lp) { return lp->d.nlayers; }
uint32_t cr_loop_rate_q48(const cr_loop_t *lp) { return CRL_RATE_Q48[lp->rate < CRL_NRATE ? lp->rate : CRL_RATE_1]; }

void cr_loop_init(cr_loop_t *lp)
{
    crl_zero(lp, sizeof *lp);
    lp->count_in = 1;
    lp->level = 100;
    lp->metro_vol = 70;
    lp->rate = lp->rate_want = CRL_RATE_1;
    lp->state = CRL_EMPTY;
    lp->rnd = 0x2545F491u;
}

void cr_loop_seed(cr_loop_t *lp, uint32_t seed) { lp->rnd = seed ? seed : 0x2545F491u; }

static uint32_t crl_xs(uint32_t x)                  /* xorshift32 (cr_engine.c crx_rand's) */
{
    if (!x) x = 0x9E3779B9u;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    return x;
}

/* ------------------------------------------------------------ playback --- */
static void crl_voices_off(cr_loop_t *lp, cr_t *c)
{
    int lv;
    for (lv = 0; lv < CR_MAX_LOOPV; lv++)
        if (lp->pv[lv].used) {
            cr_loop_event(c, lv, 0, 0, 0, 0, 0);
            lp->pv[lv].used = 0;
        }
    lp->pre = lp->pre_end = 0;
    lp->hit_on = 0;                                  /* (stopped: nothing is "playing now") */
}

static void crl_pre_off(cr_loop_t *lp, cr_t *c)     /* the step preview's voices end (cr_loop_ed_preview) */
{
    int lv;
    for (lv = 0; lp->pre && lv < CR_MAX_LOOPV; lv++)
        if ((lp->pre >> lv) & 1u) {
            cr_loop_event(c, lv, 0, 0, 0, 0, 0);
            lp->pv[lv].used = 0;
        }
    lp->pre = lp->pre_end = 0;
}

/* a step's condition on this pass of the loop (CRL_C_*; the audio ISR: a table and a shift, no division). A:B: pass A
 * of every B, counted from when the loop started playing (pass12: the pass mod 12, which 2, 3 and 4 divide) */
static int crl_cond(const cr_loop_t *lp, uint32_t cd)
{
    static const uint16_t M[10] = {0xFFFu, 0x555u, 0xAAAu, 0x249u, 0x492u, 0x924u, 0x111u, 0x222u, 0x444u, 0x888u};
    if (cd < 10u) return (M[cd] >> lp->pass12) & 1u;
    return cd == CRL_C_FIRST ? !lp->pass_n : lp->pass_n != 0;
}
/* a step's chance roll (ch 1 .. 7): decided per pass and per STEP, not per event (a step's chord and its bass note,
 * and every hit of its ratchet, come or stay away together): the pass's random word (lp->rnd, new at every wrap)
 * hashed with the step (two multiplies), its top byte against the level */
static int crl_chance(const cr_loop_t *lp, uint32_t ch, uint32_t s)
{
    static const uint8_t T[8] = {255, 230, 192, 154, 128, 102, 64, 26};   /* CRL_CHANCE_PCT of 256 */
    uint32_t x = lp->rnd + (s + 1u) * 0x9E3779B1u;
    x ^= x >> 15; x *= 0x2C1B3C6Du; x ^= x >> 12; x *= 0x297A2D39u;
    return (x >> 24) < T[ch & 7u];
}

static void crl_hit(cr_loop_t *lp, cr_t *c, int lv)   /* voice lv sounds its chord (a ratchet: again) */
{
    uint32_t qx = lp->pv[lv].qx;
    cr_loop_event_v(c, lv, lp->pv[lv].root, (uint8_t)(qx >> 4),
                    (uint8_t)(!(qx & 0xF0u) && (qx & CRL_X_BASS) ? CR_GX_BASS : qx & 15u),   /* (a bass-side note: the
                                                     * bass part, as it was played) */
                    lp->pv[lv].vel, 1, lp->pv[lv].voic);
    lp->pv[lv].used = 1;
    lp->pv[lv].off = lp->t + (lp->pv[lv].hits ? lp->pv[lv].gate : lp->pv[lv].last);   /* (the last hit: to the note's end) */
    if ((lp->pre >> lv) & 1u) return;                /* the step preview: heard, not a hit of the loop */
    lp->played++;
    lp->hit_on = 1;                                  /* the editor's "now playing" (three bytes a hit) */
    lp->hit_root = lp->pv[lv].root;
    lp->hit_qx = lp->pv[lv].qx;
}

/* an event is due. Its step's properties (the first CRL_STEPS steps have them): the condition may keep it silent this
 * pass; a ratchet makes it 2 .. 4 hits evenly inside ONE step (the slice: 12, 8, 6 ticks), each as long as the slice,
 * or the note's part of it when the note is shorter than a step. A note LONGER than a step (the owner, 2026-10-11:
 * "the last trig of ratchet then holds for the length"): the hits before the last are a slice long and the last one
 * holds to the note's end: dur - (N - 1) slices (x2 on 2 steps: 12 then 36 ticks; x4 on 4: 6, 6, 6 then 78). A ratchet
 * stays on its one loop voice (crl_offs strikes it again), so it can never take more voices than a plain note does.
 * pre: the step preview (cr_loop_ed_preview): the condition and the chance are not asked, the voice is marked */
static void crl_play_ev(cr_loop_t *lp, cr_t *c, const crl_ev_t *e, uint32_t idx, int pre)
{
    static const uint8_t SLICE[4] = {CRL_STEP, CRL_STEP / 2u, CRL_STEP / 3u, CRL_STEP / 4u};
    int lv, best = -1;
    uint32_t vel = (uint32_t)e->vel * lp->level / 100u, s = e->t / CRL_STEP, a = 0, b = 0, dur = e->dur ? e->dur : 1u;
    if (!vel) return;
    if (s < CRL_STEPS) {
        a = lp->d.st[s][0];
        b = lp->d.st[s][1];
    }
    if (!pre && (a >> 2) && !crl_cond(lp, a >> 2)) return;
    if (!pre && (b >> 5) && !crl_chance(lp, b >> 5, s)) return;
    b &= 31u;
    for (lv = 0; lv < CR_MAX_LOOPV; lv++)
        if (!lp->pv[lv].used) { best = lv; break; }
    if (best < 0)                                   /* all busy: the one that would end first */
        for (lv = 0; lv < CR_MAX_LOOPV; lv++)
            if (best < 0 || (int32_t)(lp->pv[lv].off - lp->pv[best].off) < 0) best = lv;
    if (pre) lp->pre |= (uint8_t)(1u << best);
    else lp->pre &= (uint8_t)~(1u << best);         /* (a preview's voice taken by the loop: the loop's now) */
    lp->pv[best].ev = (uint16_t)idx;
    lp->pv[best].root = e->root;
    lp->pv[best].qx = e->qx;
    lp->pv[best].vel = (uint8_t)vel;
    lp->pv[best].voic = (int8_t)(b && b < CRL_NVOIC ? (int)b - 13 : CR_VOIC_FOLLOW);
    lp->pv[best].hits = (uint8_t)(a & 3u);
    lp->pv[best].slice = SLICE[a & 3u];
    if (a & 3u) {
        uint32_t sl = SLICE[a & 3u], g = dur >= CRL_STEP ? sl : sl * dur / CRL_STEP;
        if (!g) g = 1;
        lp->pv[best].gate = (uint8_t)g;              /* (the hits before the last: <= a slice) */
        dur = dur > CRL_STEP ? dur - (a & 3u) * sl : g;   /* the last: the rest of a long note (> a slice) */
    }
    lp->pv[best].last = (uint16_t)dur;               /* (a plain note: its one hit) */
    crl_hit(lp, c, best);
}

static void crl_hide(cr_loop_t *lp, uint32_t i)    /* Replace: event i hidden by the layer being recorded */
{
    lp->d.ev[i].layer = (uint8_t)(CRL_HID | lp->d.nlayers);
    lp->gen++;
}

static void crl_fire(cr_loop_t *lp, cr_t *c, uint32_t lo, uint32_t hi)   /* events at positions lo..hi */
{
    uint32_t i;
    int hiding = lp->cap == CRL_CAP_OD && lp->rep && lp->rep_n;   /* Replace, a gesture held: what starts is erased */
    for (i = 0; i < lp->d.nev; i++) {
        const crl_ev_t *e = &lp->d.ev[i];
        if ((e->layer & CRL_HID) || e->t < lo || e->t > hi) continue;
        if (hiding && i < lp->layer_ev0) crl_hide(lp, i);
        else crl_play_ev(lp, c, e, i, 0);
    }
}

/* each layer's first event, from the layers of a loop with no hidden events (a load) */
static void crl_lev0_scan(cr_loop_t *lp)
{
    uint32_t l, i = 0;
    for (l = 0; l < CRL_MAX_LAYERS; l++) {
        while (i < lp->d.nev && (lp->d.ev[i].layer & 0x1Fu) < l) i++;
        lp->lev0[l] = (uint16_t)i;
    }
}

/* undo of layer L: the events it hid come back, in their own layers (by their place: the array is in layer order) */
static void crl_restore(cr_loop_t *lp, uint32_t L)
{
    uint32_t i, l;
    for (i = 0; i < lp->d.nev; i++)
        if (lp->d.ev[i].layer == (uint8_t)(CRL_HID | L)) {
            for (l = L ? L - 1u : 0u; l && lp->lev0[l] > i; l--) {}
            lp->d.ev[i].layer = (uint8_t)l;
            lp->gen++;
        }
}

static void crl_offs(cr_loop_t *lp, cr_t *c)
{
    int lv;
    for (lv = 0; lv < CR_MAX_LOOPV; lv++) {
        if (!lp->pv[lv].used) continue;
        if (lp->pv[lv].used == 2u) {                              /* a ratchet between two hits: the next one */
            if ((int32_t)(lp->t - lp->pv[lv].off) >= 0) crl_hit(lp, c, lv);
            continue;
        }
        if (!cr_loop_busy(c, lv)) lp->pv[lv].used = 0;            /* (a panic took it) */
        else if ((int32_t)(lp->t - lp->pv[lv].off) >= 0) {
            cr_loop_event(c, lv, 0, 0, 0, 0, 0);
            lp->pv[lv].used = 0;
            if (lp->pv[lv].hits) {                                /* a ratchet: the voice is kept for its next hit */
                lp->pv[lv].hits--;
                if (lp->pv[lv].gate >= lp->pv[lv].slice) crl_hit(lp, c, lv);   /* (back to back: now) */
                else {
                    lp->pv[lv].used = 2;
                    lp->pv[lv].off += (uint32_t)lp->pv[lv].slice - lp->pv[lv].gate;
                }
            }
        }
        if (!lp->pv[lv].used) lp->pre &= (uint8_t)~(1u << lv);   /* (a preview that ended on its own) */
    }
}

static void crl_take_next(cr_loop_t *lp)            /* a queued slot replaces the loop at the cycle's end */
{
    const crl_data_t *n = lp->next;
    lp->next = 0;
    if (n && n->len && n->nev) crl_copy(&lp->d, n, crl_data_size(n->nev));
    else crl_zero(&lp->d, crl_data_size(0));
    crl_lev0_scan(lp);
    lp->gen++;
    lp->dirty = 0;
    lp->loads++;
    if (!lp->d.len) lp->state = CRL_EMPTY;
}

/* the loop starts again (the end of a cycle): the click's grid restarts here when the loop's start is on one of its
 * beats (at x1 always, as before the Rate: a Free loop's click follows the loop); the Rate asked for is taken */
static void crl_wrap(cr_loop_t *lp)
{
    if (lp->rate == CRL_RATE_1 || (lp->rt - lp->rcyc0) % cr_loop_beat(lp) == 0u) lp->rcyc0 = lp->rt;
    lp->pass12 = (uint8_t)(lp->pass12 >= 11u ? 0u : lp->pass12 + 1u);   /* the next pass (the steps' conditions) */
    lp->pass_n = 1;
    lp->rnd = crl_xs(lp->rnd);
    if (lp->rate != lp->rate_want) {
        lp->rate = lp->rate_want;
        lp->rfrac = 0;
    }
}

static void crl_play_upto(cr_loop_t *lp, cr_t *c)
{
    uint32_t pos;
    if (lp->state != CRL_PLAYING || !lp->d.len) return;
    pos = lp->t - lp->cyc0;
    while (pos >= lp->d.len) {
        if (lp->scan < lp->d.len) crl_fire(lp, c, lp->scan, lp->d.len - 1u);
        pos -= lp->d.len;
        lp->cyc0 += lp->d.len;
        lp->scan = 0;
        crl_wrap(lp);
        if (lp->next_on) {
            lp->next_on = 0;
            crl_take_next(lp);
            lp->pass12 = lp->pass_n = 0;             /* (another loop: its first pass) */
            if (lp->state != CRL_PLAYING || !lp->d.len) return;
            if (pos >= lp->d.len) pos %= lp->d.len;
        }
    }
    if (lp->scan <= pos) {
        crl_fire(lp, c, lp->scan, pos);
        lp->scan = pos + 1u;
    }
}

static void crl_start(cr_loop_t *lp, cr_t *c)
{
    lp->state = CRL_PLAYING;
    lp->cyc0 = lp->t;
    lp->rcyc0 = lp->rt;
    lp->rate = lp->rate_want;                        /* (a start: the Rate asked for) */
    lp->rfrac = 0;
    lp->scan = 0;
    lp->pass12 = lp->pass_n = 0;                     /* the first pass (the steps' conditions count from here) */
    lp->rnd = crl_xs(lp->rnd ^ lp->t * 0x9E3779B1u); /* (the random ones: another run each start) */
    lp->hit_on = 0;
    crl_play_upto(lp, c);
    if (lp->metro) lp->click = 2;                    /* the grid restarts here: its first beat now */
}

/* ------------------------------------------------------------- capture --- */
/* step entry: the pattern's steps (a set Length: its steps, the cursor wraps there; Free: CRL_STEPS at most) */
static uint32_t crl_step_n(const cr_loop_t *lp) { return lp->step_len ? lp->step_len / CRL_STEP : CRL_STEPS; }

uint32_t cr_loop_step_len(const cr_loop_t *lp)
{
    uint32_t bar = cr_loop_bar(lp), bars = CRL_BARS[lp->sync < CRL_NSYNC ? lp->sync : 1], most = CRL_STEPS * CRL_STEP / bar;
    return (bars > most ? most : bars) * bar;
}

/* the cursor moves on a step (a chord entered, a rest): the step it leaves is used. A set Length: it wraps; Free: past
 * the last of the CRL_STEPS it stops (step == CRL_STEPS: nothing can be entered there) */
static void crl_step_adv(cr_loop_t *lp)
{
    uint32_t nst = crl_step_n(lp), s = lp->step;
    if (s < nst && s + 1u > lp->step_end) lp->step_end = (uint16_t)(s + 1u);
    lp->step = (uint16_t)(s + 1u < nst ? s + 1u : lp->step_len ? 0u : nst);
    lp->step_w = 0;
    lp->gen++;
}

/* the steps' properties: all gone (a new loop: a fresh take, a pattern entered, a clear) */
static void crl_st_zero(cr_loop_t *lp) { crl_zero(lp->d.st, sizeof lp->d.st); }
/* .. gone from the steps no event starts in any more (an undo took the layer, the editor cleared the step or closed
 * with steps hidden beyond the end). An event a Replace layer hid counts: undo brings it back with its step's */
static void crl_st_prune(cr_loop_t *lp)
{
    uint64_t has = 0;
    uint32_t i, s;
    for (i = 0; i < lp->d.nev; i++) {
        s = lp->d.ev[i].t / CRL_STEP;
        if (s < CRL_STEPS) has |= (uint64_t)1 << s;
    }
    for (s = 0; s < CRL_STEPS; s++)
        if (!((has >> s) & 1u)) lp->d.st[s][0] = lp->d.st[s][1] = 0;
}

static void crl_write(cr_loop_t *lp, uint32_t t0, int16_t root, uint8_t q, uint8_t ext, uint8_t vel)
{
    crl_ev_t *e;
    uint32_t start = t0, rel, dur;
    if (lp->cap == CRL_CAP_REC) {
        if ((int32_t)(lp->t - lp->rec0) < 0) return;                   /* ended in the count-in */
        if ((int32_t)(start - lp->rec0) < 0) start = lp->rec0;        /* held into the take: from its start */
        rel = start - lp->rec0;
        if ((lp->rec_len && rel >= lp->rec_len) || rel >= CRL_MAX_LEN) return;
    } else if (lp->cap == CRL_CAP_OD && lp->d.len) {
        int32_t r;
        if ((int32_t)(start - lp->od0) < 0) start = lp->od0;
        r = (int32_t)(start - lp->cyc0) % (int32_t)lp->d.len;
        rel = (uint32_t)(r < 0 ? r + (int32_t)lp->d.len : r);
    } else if (lp->cap == CRL_CAP_STEP) {                              /* at the step it was pressed on (t0: the
                                                                        * step, cr_loop_gesture), one step long */
        if (t0 >= crl_step_n(lp)) return;                              /* (Free: past the last step: refused there) */
        rel = t0 * lp->step_g;
        start = lp->t - lp->step_g;
    } else {
        return;
    }
    if (lp->d.nev >= CRL_MAX_EV) { lp->full = 1; return; }
    dur = lp->t - start;
    dur = dur < 1u ? 1u : dur > CRL_MAX_LEN ? CRL_MAX_LEN : dur;
    if (lp->cap == CRL_CAP_STEP) {
        if (t0 == lp->step) lp->step_w = 1;
        if (t0 + 1u > lp->step_end) lp->step_end = (uint16_t)(t0 + 1u);
    }
    lp->gen++;
    e = &lp->d.ev[lp->d.nev++];
    e->t = (uint16_t)rel;
    e->dur = (uint16_t)dur;
    e->root = (uint8_t)(root < 0 ? 0 : root > 127 ? 127 : root);
    e->vel = (uint8_t)(vel ? vel : 1u);
    if (q >= CR_Q_COUNT) q = 0;
    /* a single note has no extensions: its nibble is its part (the gesture's CR_GX_BASS: a Split Bass bass-side key) */
    e->qx = (uint8_t)(q << 4 | (q ? ext & 15u : ext & CR_GX_BASS ? CRL_X_BASS : 0u));
    e->layer = lp->d.nlayers;
    if (lp->d.nev >= CRL_MAX_EV) lp->full = 1;
}

/* a fresh take starts: Overwrite's loop goes now (not on arming: REC again before this keeps it) */
static void crl_take_start(cr_loop_t *lp)
{
    if (!lp->ow) return;
    lp->ow = 0;
    lp->d.nev = 0;
    lp->d.len = 0;
    lp->d.nlayers = 0;
    crl_st_zero(lp);
    lp->state = CRL_EMPTY;
    lp->next_on = 0;
    lp->dirty = 1;
    lp->gen++;
}

/* Replace: a gesture begins at cycle position p: an older event sounding there is cut at p (it is hidden, and a copy
 * ending at p goes into the new layer: undo brings the whole one back) and its voice ends */
static void crl_cut(cr_loop_t *lp)
{
    uint32_t p = (lp->t - lp->cyc0) % lp->d.len, i, lv, n = lp->layer_ev0;
    for (i = 0; i < n; i++) {
        crl_ev_t *e = &lp->d.ev[i];
        uint32_t rel;
        if (e->layer & CRL_HID) continue;
        rel = (p + lp->d.len - e->t) % lp->d.len;
        if (!rel || rel >= e->dur) continue;
        if (lp->d.nev >= CRL_MAX_EV) { lp->full = 1; return; }
        lp->d.ev[lp->d.nev] = *e;
        lp->d.ev[lp->d.nev].dur = (uint16_t)rel;
        lp->d.ev[lp->d.nev].layer = lp->d.nlayers;
        lp->d.nev++;
        crl_hide(lp, i);
        for (lv = 0; lv < CR_MAX_LOOPV; lv++)
            if (lp->pv[lv].used && lp->pv[lv].ev == i) {
                lp->pv[lv].hits = 0;                 /* (a ratchet: no more hits) */
                if (lp->pv[lv].used == 2u) lp->pv[lv].used = 0;
                else lp->pv[lv].off = lp->t;         /* (crl_offs ends it) */
            }
    }
}

static void crl_ed_write(cr_loop_t *lp, int16_t root, uint8_t q, uint8_t ext, uint8_t vel);
void cr_loop_gesture(cr_loop_t *lp, uint32_t gid, int16_t root, uint8_t q, uint8_t ext, uint8_t vel, int on)
{
    uint32_t i, k = CRL_OPEN;
    if (on == CR_G_KEYUP) {
        /* a latched gesture's key is up (Sticky, a Perform mode's Hold): it sounds on, and its end comes only with the
         * next press. The editor writes the step NOW, as at an unlatched key's release: until 2026-10-11 it wrote at
         * that end, so the chord landed a press late, in whichever step was selected by then: a step got the chord
         * that was still sounding, not the keys being played (the owner: "it should only enter the keys I'm playing").
         * A capture: nothing (a take records the latched chord's real length, as before) */
        if (!lp->ed || lp->cap != CRL_CAP_NONE) return;
        for (i = 0; i < CRL_OPEN; i++)
            if (lp->open[i].used == 1u && lp->open[i].gid == gid) {
                lp->open[i].used = 2;
                crl_ed_write(lp, root, q, ext, vel);
                return;
            }
        return;
    }
    if (on) {
        if (lp->pre) lp->pre_end = 1;                                  /* a key played by hand: the step preview ends
                                                                        * (at the next tick: not inside the hook) */
        if (lp->ed) {                                                  /* the editor: the first key of a new chord:
                                                                        * what it plays replaces the step's notes (a
                                                                        * latched chord already written: not held) */
            for (i = 0; i < CRL_OPEN && lp->open[i].used != 1u; i++) {}
            if (i == CRL_OPEN) lp->ed_w = 0;
        }
        for (i = 0; i < CRL_OPEN; i++)
            if (!lp->open[i].used) { k = i; break; }
        if (k == CRL_OPEN)                                             /* full: the oldest goes */
            for (i = k = 0; i < CRL_OPEN; i++)
                if ((int32_t)(lp->open[i].t0 - lp->open[k].t0) < 0) k = i;
        lp->open[k].used = 1;
        lp->open[k].gid = gid;
        lp->open[k].t0 = lp->t;
        lp->open[k].root = root;                                       /* (for a capture ending while held) */
        lp->open[k].q = q;
        lp->open[k].ext = ext;
        lp->open[k].vel = vel;
        lp->open[k].rep = 0;
        if (lp->cap == CRL_CAP_STEP) {
            /* a root pressed after a chord was written here and let go: the next step, whatever is still held or
             * latched (the chord key kept down between roots, LOCK's latch: the owner's "only records to the first
             * step", 2026-10-12). Roots held TOGETHER share the step (a chord over a Split Bass bass note). The step
             * goes with the gesture (t0): one let go late is written where it was pressed */
            if (lp->step_w) crl_step_adv(lp);
            lp->open[k].t0 = lp->step;
            if (lp->step >= crl_step_n(lp)) lp->step_no++;             /* (Free, a 65th: refused, the UI says so) */
        }
        if (lp->cap == CRL_CAP_ARMED) {                                /* Free: the first chord starts the take */
            crl_take_start(lp);
            lp->cap = CRL_CAP_REC;
            lp->rec0 = lp->t;
            lp->rrec0 = lp->rt;
            lp->rec_len = 0;
            lp->layer_ev0 = 0;
            lp->open[k].t0 = lp->t;
        } else if (lp->cap == CRL_CAP_OD_ARMED) {                      /* the first chord opens the layer */
            lp->cap = CRL_CAP_OD;
            lp->od0 = lp->t;
            lp->layer_ev0 = lp->d.nev;
        }
        if (lp->cap == CRL_CAP_OD && lp->rep && lp->state == CRL_PLAYING && lp->d.len) {   /* Replace: a held span */
            lp->open[k].rep = 1;
            if (!lp->rep_n++) lp->rep0 = (lp->t - lp->cyc0) % lp->d.len;
            crl_cut(lp);
        }
        if (lp->cap != CRL_CAP_NONE) lp->gen++;
        return;
    }
    for (i = 0; i < CRL_OPEN; i++)
        if (lp->open[i].used && lp->open[i].gid == gid) {
            uint32_t was = lp->open[i].used;
            lp->open[i].used = 0;
            if (lp->open[i].rep && lp->rep_n) lp->rep_n--;
            if (lp->ed && lp->cap == CRL_CAP_NONE) {
                if (was == 1u) crl_ed_write(lp, root, q, ext, vel);    /* (a latched one: written at its key's up) */
            } else crl_write(lp, lp->open[i].t0, root, q, ext, vel);
            return;
        }
}

static void crl_close_open(cr_loop_t *lp)          /* gestures held at the capture's end: closed now */
{
    uint32_t i;
    for (i = 0; i < CRL_OPEN; i++)
        if (lp->open[i].used) {
            crl_write(lp, lp->open[i].t0, lp->open[i].root, lp->open[i].q, lp->open[i].ext, lp->open[i].vel);
            lp->open[i].t0 = lp->t;                                    /* (a later capture starts it here) */
        }
}

static void crl_quantize(cr_loop_t *lp, uint32_t i0)   /* note-ons to the grid; offs kept (>= 1 tick) */
{
    uint32_t g = CRL_QGRID[lp->quant < CRL_NQUANT ? lp->quant : 0], i;
    if (!g || !lp->d.len) return;
    for (i = i0; i < lp->d.nev; i++) {
        crl_ev_t *e = &lp->d.ev[i];
        uint32_t nt = (e->t + g / 2u) / g * g, off = (uint32_t)e->t + e->dur, dur;
        if (nt >= lp->d.len) { e->t = 0; continue; }   /* past the end: the loop's start, its length kept */
        dur = off > nt ? off - nt : 1u;
        e->t = (uint16_t)nt;
        e->dur = (uint16_t)(dur > CRL_MAX_LEN ? CRL_MAX_LEN : dur);
    }
}

static void crl_drop_take(cr_loop_t *lp)
{
    crl_st_zero(lp);
    lp->d.nev = 0;
    lp->d.len = 0;
    lp->d.nlayers = 0;
    lp->state = CRL_EMPTY;
}

/* step entry ends (REC; LOOP: play = 1): the steps entered become the loop (the one there before goes), its length
 * synced, or (Free) the steps used exactly: three chords are a loop three steps long (the owner, 2026-10-11; until
 * then rounded up to a bar); nothing entered: the loop as it was */
static int crl_step_commit(cr_loop_t *lp, cr_t *c, int play)
{
    uint32_t i, n, len;
    crl_close_open(lp);
    lp->cap = CRL_CAP_NONE;
    lp->full = 0;
    n = lp->d.nev > lp->layer_ev0 ? lp->d.nev - lp->layer_ev0 : 0u;
    lp->gen++;
    if (!n) {
        lp->d.nev = lp->layer_ev0;
        if (play && lp->state == CRL_STOPPED) crl_start(lp, c);
        return CRL_DID_EMPTY_TAKE;
    }
    for (i = 0; i < n; i++) {
        lp->d.ev[i] = lp->d.ev[lp->layer_ev0 + i];
        lp->d.ev[i].layer = 0;
    }
    lp->d.nev = (uint16_t)n;
    lp->d.nlayers = 1;
    lp->lev0[0] = 0;
    lp->layer_ev0 = 0;
    crl_st_zero(lp);                                 /* (the loop there before is gone, its steps' properties too) */
    if (lp->step_len) len = lp->step_len;
    else len = (uint32_t)(lp->step_end ? lp->step_end : 1u) * lp->step_g;
    lp->d.len = len;
    lp->d.sig = lp->sig;
    lp->dirty = 1;
    lp->next_on = 0;
    lp->state = CRL_STOPPED;
    if (play) crl_start(lp, c);
    return CRL_DID_STEP_DONE;
}

static int crl_commit(cr_loop_t *lp, cr_t *c)      /* the capture ends */
{
    uint32_t elapsed, bar = cr_loop_bar(lp), len;
    int cap = lp->cap;
    if (cap == CRL_CAP_REC) {
        crl_close_open(lp);
        lp->cap = CRL_CAP_NONE;
        lp->full = 0;
        if (!lp->d.nev) {
            crl_drop_take(lp);
            return CRL_DID_EMPTY_TAKE;
        }
        elapsed = lp->t - lp->rec0;
        if (lp->rec_len) {
            len = elapsed >= lp->rec_len ? lp->rec_len : (elapsed + bar - 1u) / bar * bar;
            if (len < bar) len = bar;
        } else {
            len = elapsed < CRL_MIN_LEN ? CRL_MIN_LEN : elapsed > CRL_MAX_LEN ? CRL_MAX_LEN : elapsed;
        }
        lp->d.len = len;
        lp->d.nlayers = 1;
        lp->lev0[0] = 0;
        lp->d.sig = lp->sig;
        crl_quantize(lp, 0);
        lp->gen++;
        lp->dirty = 1;
        lp->state = CRL_PLAYING;                     /* the take was the first cycle: playback goes on from here */
        lp->pass12 = lp->pass_n = 0;
        lp->cyc0 = lp->rec0;
        lp->rcyc0 = lp->rrec0;
        lp->scan = elapsed + 1u;
        crl_play_upto(lp, c);
        return CRL_DID_COMMIT;
    }
    if (cap == CRL_CAP_OD) {
        crl_close_open(lp);
        lp->cap = CRL_CAP_NONE;
        lp->full = 0;
        lp->rep = lp->rep_n = 0;
        if (lp->d.nev > lp->layer_ev0) {
            crl_quantize(lp, lp->layer_ev0);
            lp->lev0[lp->d.nlayers] = lp->layer_ev0;
            lp->d.nlayers++;
            lp->dirty = 1;
        } else {
            crl_restore(lp, lp->d.nlayers);
        }
        lp->gen++;
        return CRL_DID_OD_END;
    }
    if (cap == CRL_CAP_STEP) return crl_step_commit(lp, c, 0);
    lp->cap = CRL_CAP_NONE;
    lp->ow = 0;
    return CRL_DID_CANCEL;
}

/* ----------------------------------------------------------- the clock --- */
/* one tempo tick (adv 0: the clock's first call, tick 0). The loop's own time first: CRL_RATE_Q48 48ths of a loop
 * tick a tempo tick (x1: one each, exactly the clock there was before the Rate), each with the take's end, the
 * voices' ends and the events due; then the tempo's: the count-in's end, the click */
static void crl_tick1(cr_loop_t *lp, cr_t *c, int adv)
{
    uint32_t bar = cr_loop_bar(lp), beat = cr_loop_beat(lp), anchor = 0, rel, n = 1;
    int on = 0;
    if (adv) {
        lp->rt++;
        uint32_t f = (uint32_t)lp->rfrac + CRL_RATE_Q48[lp->rate < CRL_NRATE ? lp->rate : CRL_RATE_1];   /* <= 47 + 192 */
        for (n = 0; f >= CRL_Q48; f -= CRL_Q48) n++;  /* (0 .. 4 loop ticks: no divide in the ISR) */
        lp->rfrac = (uint8_t)f;
    }
    for (; n; n--) {
        if (adv) lp->t++;
        if (lp->cap == CRL_CAP_REC && ((lp->rec_len && lp->t - lp->rec0 >= lp->rec_len) ||
                                       (!lp->rec_len && lp->t - lp->rec0 >= CRL_MAX_LEN) || lp->full))
            crl_commit(lp, c);
        if (lp->cap == CRL_CAP_OD && lp->full) crl_commit(lp, c);
        crl_offs(lp, c);
        crl_play_upto(lp, c);
    }
    if (lp->cap == CRL_CAP_COUNTIN && lp->rt - lp->cin0 >= bar) {
        crl_take_start(lp);
        lp->cap = CRL_CAP_REC;
        lp->rec0 = lp->t;
        lp->rrec0 = lp->rt;
        lp->rfrac = 0;
        lp->layer_ev0 = 0;
    }
    /* the click: the count-in always (it is on), else the metronome on the take's / the loop's / its own grid */
    if (lp->cap == CRL_CAP_COUNTIN) { anchor = lp->cin0; on = 1; }
    else if (lp->metro) {
        on = 1;
        anchor = lp->cap == CRL_CAP_REC ? lp->rrec0 : lp->state == CRL_PLAYING ? lp->rcyc0 : lp->met0;
    }
    if (on) {
        rel = lp->rt - anchor;
        if (rel % beat == 0u) lp->click = rel % bar == 0u ? 2u : 1u;
    }
}

void cr_loop_tick(cr_loop_t *lp, cr_t *c, uint32_t now_ms)
{
    uint32_t dms, bpm = c->bpm ? c->bpm : 120u;
    if (!lp->clk_on) {
        lp->clk_on = 1;
        lp->last_ms = now_ms;
        crl_tick1(lp, c, 0);                         /* tick 0 */
        return;
    }
    if (lp->pre_end) crl_pre_off(lp, c);             /* (a key played, the step cleared: the preview ends here) */
    dms = now_ms - lp->last_ms;
    lp->last_ms = now_ms;
    if (dms > 1000u) dms = 1000u;
    lp->acc += dms * bpm * CRL_PPQN;
    while (lp->acc >= 60000u) {
        if (lp->ext_on && (int32_t)(lp->rt - lp->ext_rt) >= (int32_t)(CRL_PPQN / 24u - 1u)) {
            lp->acc = 0;                             /* ahead of the master: wait for its next pulse */
            break;
        }
        lp->acc -= 60000u;
        crl_tick1(lp, c, 1);
    }
    while (lp->ext_on && (int32_t)(lp->ext_rt - lp->rt) > 0)
        crl_tick1(lp, c, 1);                         /* behind the master: to its pulse now */
}

/* Felucca's external clock fix (ebdf867) in the looper's terms: the position advances by the pulses, the ms clock
 * only fills in between them (Felucca's midi_clock_advance: at most a pulse less one) */
void cr_loop_ext_pulse(cr_loop_t *lp)
{
    if (!lp->ext_on) {
        lp->ext_on = 1;
        lp->ext_rt = lp->rt;                         /* the first pulse: where the loop is now */
    } else {
        lp->ext_rt += CRL_PPQN / 24u;
    }
}

void cr_loop_ext_off(cr_loop_t *lp) { lp->ext_on = 0; }

void cr_loop_set_rate(cr_loop_t *lp, int rate)
{
    lp->rate_want = (uint8_t)(rate >= 0 && rate < (int)CRL_NRATE ? rate : (int)CRL_RATE_1);
    if (lp->state != CRL_PLAYING && lp->cap != CRL_CAP_REC && lp->cap != CRL_CAP_OD) {
        lp->rate = lp->rate_want;
        lp->rfrac = 0;
    }
}

/* ----------------------------------------------------------- transport --- */
void cr_loop_stop(cr_loop_t *lp, cr_t *c)
{
    if (lp->cap == CRL_CAP_REC || lp->cap == CRL_CAP_OD || lp->cap == CRL_CAP_STEP) crl_commit(lp, c);
    lp->cap = CRL_CAP_NONE;
    lp->ow = 0;
    crl_voices_off(lp, c);
    lp->next_on = 0;
    lp->state = lp->d.len ? CRL_STOPPED : CRL_EMPTY;
}

int cr_loop_rec(cr_loop_t *lp, cr_t *c)
{
    int mode = lp->mode < CRL_NMODE ? lp->mode : CRL_MODE_OVERWRITE;
    uint32_t i;
    switch (lp->cap) {
    case CRL_CAP_NONE:
        if (mode == CRL_MODE_STEP) {                 /* step entry: the loop stops, the cursor on the first step */
            if (lp->state == CRL_PLAYING) {
                crl_voices_off(lp, c);
                lp->state = CRL_STOPPED;
            }
            lp->next_on = 0;
            lp->cap = CRL_CAP_STEP;
            lp->layer_ev0 = lp->d.nev;               /* (entered after the loop's events: it stays until the commit) */
            lp->step = lp->step_end = 0;
            lp->step_w = 0;
            lp->step_g = CRL_STEP;                   /* a sixteenth, whatever Quantize says (a take's grid, not this) */
            lp->step_len = lp->sync ? cr_loop_step_len(lp) : 0u;
            for (i = 0; i < CRL_OPEN; i++) lp->open[i].t0 = 0;   /* (a key held into step entry: the first step) */
            lp->full = lp->d.nev >= CRL_MAX_EV;
            lp->gen++;
            return CRL_DID_STEP;
        }
        if (lp->state == CRL_EMPTY || mode == CRL_MODE_OVERWRITE || mode == CRL_MODE_ADVANCE) {
            if (lp->state == CRL_EMPTY) {
                crl_drop_take(lp);
                lp->ow = 0;
            } else {                                 /* Overwrite: stopped; the loop goes when the take starts */
                crl_voices_off(lp, c);
                lp->state = CRL_STOPPED;
                lp->next_on = 0;
                lp->ow = 1;
            }
            lp->full = 0;
            lp->layer_ev0 = 0;
            if (!lp->sync) {
                lp->cap = CRL_CAP_ARMED;
                return CRL_DID_ARM;
            }
            lp->rec_len = CRL_BARS[lp->sync < CRL_NSYNC ? lp->sync : 1] * cr_loop_bar(lp);
            if (lp->count_in) {
                lp->cap = CRL_CAP_COUNTIN;
                lp->cin0 = lp->rt;
                lp->click = 2;                       /* the count-in's first beat now */
                return CRL_DID_COUNTIN;
            }
            crl_take_start(lp);
            lp->cap = CRL_CAP_REC;
            lp->rec0 = lp->t;
            lp->rrec0 = lp->rt;
            lp->rfrac = 0;
            if (lp->metro) lp->click = 2;
            return CRL_DID_REC;
        }
        if (lp->d.nlayers >= CRL_MAX_LAYERS || lp->d.nev >= CRL_MAX_EV) {
            lp->full = 1;
            return CRL_DID_NOTHING;
        }
        if (lp->state == CRL_STOPPED) crl_start(lp, c);
        lp->next_on = 0;                             /* a queued switch: cancelled, this loop is being edited */
        lp->cap = CRL_CAP_OD_ARMED;
        lp->rep = mode == CRL_MODE_REPLACE;
        lp->rep_n = 0;
        return lp->rep ? CRL_DID_REP_ARM : CRL_DID_OD_ARM;
    case CRL_CAP_REC:
    case CRL_CAP_OD:
    case CRL_CAP_STEP:
        return crl_commit(lp, c);
    default:                                         /* armed / counting in: cancelled (Overwrite: the loop kept) */
        lp->cap = CRL_CAP_NONE;
        lp->ow = 0;
        lp->rep = 0;
        return CRL_DID_CANCEL;
    }
}

int cr_loop_play(cr_loop_t *lp, cr_t *c)
{
    int r;
    switch (lp->cap) {
    case CRL_CAP_ARMED:
    case CRL_CAP_COUNTIN:
        lp->cap = CRL_CAP_NONE;
        lp->ow = 0;
        return CRL_DID_CANCEL;
    case CRL_CAP_REC:
        return crl_commit(lp, c);
    case CRL_CAP_STEP:                               /* step entry ends, the loop plays */
        r = crl_step_commit(lp, c, 1);
        return lp->state == CRL_PLAYING ? CRL_DID_PLAY : r;
    case CRL_CAP_OD:
    case CRL_CAP_OD_ARMED:
        r = crl_commit(lp, c);
        (void)r;
        cr_loop_stop(lp, c);
        return CRL_DID_STOP;
    default:
        break;
    }
    if (lp->state == CRL_PLAYING) {
        cr_loop_stop(lp, c);
        return CRL_DID_STOP;
    }
    if (lp->state == CRL_STOPPED) {
        crl_pre_off(lp, c);                          /* (the step preview, if one sounds: the loop's voices now) */
        crl_start(lp, c);
        return CRL_DID_PLAY;
    }
    return CRL_DID_NOTHING;
}

int cr_loop_undo(cr_loop_t *lp, cr_t *c)
{
    uint32_t i;
    (void)c;
    if (lp->cap == CRL_CAP_OD || lp->cap == CRL_CAP_OD_ARMED) {     /* the layer in progress goes */
        int had = lp->cap == CRL_CAP_OD && lp->d.nev > lp->layer_ev0;
        lp->d.nev = lp->layer_ev0;
        crl_restore(lp, lp->d.nlayers);              /* (Replace: what it erased comes back) */
        lp->cap = CRL_CAP_NONE;
        lp->rep = lp->rep_n = 0;
        lp->full = 0;
        lp->gen++;
        return had ? CRL_DID_UNDO : CRL_DID_CANCEL;
    }
    if (lp->cap != CRL_CAP_NONE || lp->d.nlayers <= 1u) return CRL_DID_NOTHING;
    i = lp->lev0[lp->d.nlayers - 1u];
    lp->d.nev = (uint16_t)(i < lp->d.nev ? i : lp->d.nev);   /* (its sounding notes finish) */
    lp->d.nlayers--;
    crl_restore(lp, lp->d.nlayers);                  /* the events a Replace layer hid come back */
    crl_st_prune(lp);                                /* (a step the layer alone filled: its properties go with it) */
    lp->gen++;
    lp->full = 0;
    lp->dirty = 1;
    lp->next_on = 0;                                 /* a queued switch: cancelled, this loop is being edited */
    return CRL_DID_UNDO;
}

int cr_loop_clear(cr_loop_t *lp, cr_t *c)
{
    lp->cap = CRL_CAP_NONE;
    lp->ow = lp->rep = lp->rep_n = 0;
    crl_voices_off(lp, c);
    crl_drop_take(lp);
    lp->gen++;
    lp->full = 0;
    lp->next_on = 0;
    lp->dirty = 1;
    return CRL_DID_CLEAR;
}

void cr_loop_panic(cr_loop_t *lp)
{
    uint32_t i;
    for (i = 0; i < CR_MAX_LOOPV; i++) lp->pv[i].used = 0;          /* cr_panic ended the voices */
    lp->pre = lp->pre_end = 0;
    lp->hit_on = 0;
    if (lp->cap == CRL_CAP_REC) {
        crl_drop_take(lp);                                          /* a fresh take is dropped */
    } else if (lp->cap == CRL_CAP_OD && lp->d.nev > lp->layer_ev0) {
        crl_quantize(lp, lp->layer_ev0);                            /* a partial overdub is kept */
        lp->lev0[lp->d.nlayers] = lp->layer_ev0;
        lp->d.nlayers++;
        lp->dirty = 1;
    } else if (lp->cap == CRL_CAP_OD || lp->cap == CRL_CAP_OD_ARMED || lp->cap == CRL_CAP_STEP) {
        lp->d.nev = lp->layer_ev0;                                  /* (armed / counting in: the loop is kept) */
        if (lp->cap != CRL_CAP_STEP) crl_restore(lp, lp->d.nlayers);
    }
    lp->ow = lp->rep = lp->rep_n = 0;
    lp->gen++;
    for (i = 0; i < CRL_OPEN; i++) lp->open[i].used = 0;
    lp->cap = CRL_CAP_NONE;
    lp->full = 0;
    lp->next_on = 0;
    lp->state = lp->d.len ? CRL_STOPPED : CRL_EMPTY;
}

void cr_loop_set(cr_loop_t *lp, cr_t *c, const crl_data_t *d)
{
    lp->cap = CRL_CAP_NONE;
    lp->ow = lp->rep = lp->rep_n = 0;
    crl_voices_off(lp, c);
    lp->next = d;
    lp->next_on = 0;
    crl_take_next(lp);
    lp->full = 0;
    lp->state = lp->d.len ? CRL_STOPPED : CRL_EMPTY;
}

void cr_loop_queue(cr_loop_t *lp, cr_t *c, const crl_data_t *d)
{
    if (lp->state != CRL_PLAYING) {
        cr_loop_set(lp, c, d);
        return;
    }
    if (lp->cap == CRL_CAP_OD) crl_commit(lp, c);
    lp->cap = CRL_CAP_NONE;
    lp->ow = lp->rep = lp->rep_n = 0;
    lp->next = d;
    lp->next_on = 1;
}

void cr_loop_step(cr_loop_t *lp, int op)
{
    uint32_t nst = crl_step_n(lp);
    if (lp->cap != CRL_CAP_STEP) return;
    if (op == CRL_STEP_KEYSUP && !lp->step_w) return;    /* (no chord written here yet) */
    if (op == CRL_STEP_BACK) {
        lp->step = (uint16_t)(lp->step ? lp->step - 1u : lp->step_len ? nst - 1u : 0u);
        lp->step_w = 0;
        lp->gen++;
    } else {
        crl_step_adv(lp);                                /* (a rest is a step used) */
    }
}

/* ------------------------------------------------------- the loop editor --- */
/* the events of [t0, t1) on a side (0 chord, 1 bass, 2 both; hid: the hidden ones instead) leave the array: the layers'
 * first events and the voices' events follow (a voice sounding a removed event ends at its own time) */
static uint32_t crl_ed_kill(cr_loop_t *lp, uint32_t t0, uint32_t t1, int side, int hid)
{
    uint32_t i, w = 0, l = 0, v, n;
    for (i = 0; i < lp->d.nev; i++) {
        const crl_ev_t *e = &lp->d.ev[i];
        int kill = hid ? (e->layer & CRL_HID) != 0
                       : !(e->layer & CRL_HID) && e->t >= t0 && e->t < t1 && (side > 1 || (CRL_EV_BASS(e) != 0) == side);
        while (l < lp->d.nlayers && lp->lev0[l] <= i) lp->lev0[l++] = (uint16_t)w;
        for (v = 0; v < CR_MAX_LOOPV; v++)
            if (lp->pv[v].used && lp->pv[v].ev == i) {
                lp->pv[v].ev = (uint16_t)(kill ? 0xFFFFu : w);
                if (kill) {                          /* (a ratchet: its hits to come go with the event) */
                    lp->pv[v].hits = 0;
                    if (lp->pv[v].used == 2u) lp->pv[v].used = 0;
                }
            }
        if (kill) continue;
        if (w != i) lp->d.ev[w] = *e;
        w++;
    }
    while (l < lp->d.nlayers) lp->lev0[l++] = (uint16_t)w;
    n = lp->d.nev - w;
    lp->d.nev = (uint16_t)w;
    if (n) {
        lp->gen++;
        lp->full = 0;
    }
    return n;
}

/* the length changed under a playing loop: the playhead where it is in the new cycle (no burst of the events passed) */
static void crl_ed_renorm(cr_loop_t *lp)
{
    uint32_t pos;
    if (lp->state != CRL_PLAYING || !lp->d.len) return;
    pos = lp->t - lp->cyc0;
    if (pos < lp->d.len) return;
    pos %= lp->d.len;
    lp->cyc0 = lp->t - pos;
    lp->scan = pos + 1u;
}

/* the first edit: one layer (the overdubs merged; what a Replace layer hid goes: it could not follow the edits) */
static void crl_ed_flat(cr_loop_t *lp)
{
    uint32_t i;
    if (lp->ed_flat) return;
    lp->ed_flat = 1;
    crl_ed_kill(lp, 0, 0, 0, 1);
    for (i = 0; i < lp->d.nev; i++) lp->d.ev[i].layer = 0;
    lp->d.nlayers = 1;
    lp->lev0[0] = 0;
}

/* a chord played on the keys: the cursor's step is that chord (its side: a bass-side note replaces the bass-side
 * notes, anything else the chord side: Split Bass's rule, docs/LOOPER.md); the keys of one push add up. The new notes
 * start on the step's tick and are as long as the step's notes were (a step: an empty one) */
static void crl_ed_write(cr_loop_t *lp, int16_t root, uint8_t q, uint8_t ext, uint8_t vel)
{
    crl_ev_t *e;
    uint32_t side, n;
    if (q >= CR_Q_COUNT) q = 0;
    side = !q && (ext & CR_GX_BASS) ? 1u : 0u;
    if (lp->ed_t0 >= lp->d.len) return;
    crl_ed_flat(lp);
    if (!lp->ed_w) {
        n = cr_loop_ed_nlen(lp);
        lp->ed_n = (uint8_t)(n ? n : 1u);
        lp->ed_nf = (uint8_t)cr_loop_ed_nfrac(lp);
    }
    if (!(lp->ed_w & (1u << side))) {
        crl_ed_kill(lp, lp->ed_t0, lp->ed_t0 + CRL_STEP, (int)side, 0);
        lp->ed_w |= (uint8_t)(1u << side);
    }
    if (lp->d.nev >= CRL_MAX_EV) { lp->full = 1; return; }
    e = &lp->d.ev[lp->d.nev++];
    e->t = (uint16_t)lp->ed_t0;
    e->dur = (uint16_t)(lp->ed_nf ? lp->ed_nf * CRL_FRAC : (lp->ed_n ? lp->ed_n : 1u) * CRL_STEP);
    e->root = (uint8_t)(root < 0 ? 0 : root > 127 ? 127 : root);
    e->vel = (uint8_t)(vel ? vel : 1u);
    e->qx = (uint8_t)(q << 4 | (q ? ext & 15u : side ? CRL_X_BASS : 0u));
    e->layer = 0;
    lp->gen++;
    lp->dirty = 1;
}

int cr_loop_ed_open(cr_loop_t *lp)
{
    uint32_t empty = 1, i;
    if (lp->cap != CRL_CAP_NONE) return CRL_ED_BUSY;
    for (i = 0; lp->d.len && i < lp->d.nev; i++)
        if (!(lp->d.ev[i].layer & CRL_HID)) empty = 0;
    lp->next_on = 0;                                 /* a queued switch: cancelled, this loop is being edited */
    lp->ed_flat = 0;
    if (empty) {                                     /* an empty pattern of the set Length (Free: a bar) */
        crl_drop_take(lp);
        lp->d.len = lp->sync ? cr_loop_step_len(lp) : cr_loop_bar(lp);
        lp->d.nlayers = 1;
        lp->d.sig = lp->sig;
        lp->lev0[0] = 0;
        lp->state = CRL_STOPPED;
        lp->ed_flat = 1;
    }
    lp->ed = 1;
    lp->ed_g = CRL_STEP;
    lp->ed_len = lp->d.len;
    lp->ed_t0 = 0;
    lp->ed_n = 1;
    lp->ed_nf = 0;
    lp->ed_w = 0;
    lp->gen++;
    return CRL_ED_OK;
}

void cr_loop_ed_close(cr_loop_t *lp, cr_t *c)
{
    uint32_t i, live = 0;
    if (!lp->ed) return;
    lp->ed = 0;
    crl_pre_off(lp, c);                              /* (a step preview sounding: it goes with the editor) */
    crl_ed_kill(lp, lp->d.len, 0xFFFFFFFFu, 2, 0);   /* the steps Loop length hid */
    for (i = 0; i < lp->d.nev; i++)
        live += !(lp->d.ev[i].layer & CRL_HID);
    if (!live) {                                     /* nothing left (or nothing entered): no loop */
        crl_voices_off(lp, c);
        crl_drop_take(lp);
        lp->next_on = 0;
    }
    crl_st_prune(lp);                                /* (the hidden steps' properties went with them) */
    lp->gen++;
}

void cr_loop_ed_map(const cr_loop_t *lp, uint32_t first, crl_edmap_t *m)
{
    uint32_t ext = lp->ed_len > lp->d.len ? lp->ed_len : lp->d.len, i;
    crl_zero(m, sizeof *m);
    m->first = (uint16_t)first;
    m->nstep = (uint16_t)((lp->d.len + CRL_STEP - 1u) / CRL_STEP);
    m->nall = (uint16_t)((ext + CRL_STEP - 1u) / CRL_STEP);
    for (i = 0; i < lp->d.nev; i++) {
        const crl_ev_t *e = &lp->d.ev[i];
        uint32_t s = e->t / CRL_STEP, end = (uint32_t)e->t + e->dur, lim = e->t < lp->d.len ? m->nstep : m->nall, a, b;
        if (e->layer & CRL_HID) continue;
        if (s >= first && s < first + CRL_STEPS) {
            uint32_t k = s - first, lv = CRL_EC_VEL(e->vel), pr = 0;
            m->start |= (uint64_t)1 << k;
            if (s < CRL_STEPS) {                     /* (the step's own: the same for each of its events) */
                uint32_t a0 = lp->d.st[s][0];
                pr = (uint32_t)(lp->d.st[s][1] >> 5) << 4 | (a0 >> 2 ? CRL_EC_COND : 0u);
                m->rat[0] |= (uint64_t)(a0 & 1u) << k;
                m->rat[1] |= (uint64_t)((a0 >> 1) & 1u) << k;
            }
            if (lv < (m->cell[k] & 15u)) lv = m->cell[k] & 15u;
            m->cell[k] = (uint8_t)(pr | lv);
        }
        /* the steps after its own that it sounds through, half or more: [a, b), on this page, up to the loop's end */
        a = s + 1u > first ? s + 1u : first;
        b = (end + CRL_STEP / 2u) / CRL_STEP;
        if (b > first + CRL_STEPS) b = first + CRL_STEPS;
        if (b > lim) b = lim;
        if (b > a)
            m->tail |= (b - a >= 64u ? ~(uint64_t)0 : ((uint64_t)1 << (b - a)) - 1u) << (a - first);
    }
    m->tail &= ~m->start;
}

void cr_loop_ed_cursor(cr_loop_t *lp, uint32_t step)
{
    uint32_t t0 = step * CRL_STEP;
    if (t0 != lp->ed_t0) lp->ed_w = 0;               /* (another step: the keys still down replace there) */
    lp->ed_t0 = t0;
}

int cr_loop_ed_clear(cr_loop_t *lp)
{
    uint32_t n;
    if (!lp->ed) return 0;
    crl_ed_flat(lp);
    n = crl_ed_kill(lp, lp->ed_t0, lp->ed_t0 + CRL_STEP, 2, 0);
    lp->ed_w = 0;
    if (lp->pre) lp->pre_end = 1;                    /* (its preview, if it sounds: ended at the next tick) */
    if (n) {
        lp->dirty = 1;
        crl_st_prune(lp);                            /* a cleared step has no ratchet, condition or voicing */
    }
    return (int)n;
}

int cr_loop_ed_preview(cr_loop_t *lp, cr_t *c)
{
    uint32_t i, n = 0, t0 = lp->ed_t0, t1 = lp->ed_t0 + CRL_STEP;
    crl_pre_off(lp, c);                              /* the one before (the next step empty: silence) */
    if (!lp->ed || lp->state == CRL_PLAYING || lp->cap != CRL_CAP_NONE || t0 >= lp->d.len) return 0;
    for (i = 0; i < lp->d.nev; i++) {
        const crl_ev_t *e = &lp->d.ev[i];
        if ((e->layer & CRL_HID) || e->t < t0 || e->t >= t1) continue;
        crl_play_ev(lp, c, e, i, 1);
        n++;
    }
    return (int)n;
}

uint32_t cr_loop_ed_nlen(const cr_loop_t *lp)
{
    uint32_t i, top = 0;
    for (i = 0; i < lp->d.nev; i++) {
        const crl_ev_t *e = &lp->d.ev[i];
        uint32_t n;
        if ((e->layer & CRL_HID) || e->t < lp->ed_t0 || e->t >= lp->ed_t0 + CRL_STEP) continue;
        n = (e->t - lp->ed_t0 + e->dur + CRL_STEP - 1u) / CRL_STEP;
        if (n > top) top = n;
    }
    return top > CRL_STEPS ? CRL_STEPS : top;
}

int cr_loop_ed_setnlen(cr_loop_t *lp, uint32_t n)
{
    uint32_t i;
    if (!lp->ed || n < 1u || n > CRL_STEPS || !cr_loop_ed_nlen(lp)) return 0;
    crl_ed_flat(lp);
    for (i = 0; i < lp->d.nev; i++) {
        crl_ev_t *e = &lp->d.ev[i];
        if ((e->layer & CRL_HID) || e->t < lp->ed_t0 || e->t >= lp->ed_t0 + CRL_STEP) continue;
        e->dur = (uint16_t)(n * CRL_STEP - (e->t - lp->ed_t0));   /* (to the grid: n steps from the step's start) */
    }
    lp->gen++;
    lp->dirty = 1;
    return 1;
}

uint32_t cr_loop_ed_nfrac(const cr_loop_t *lp)
{
    uint32_t i, top = 0;
    for (i = 0; i < lp->d.nev; i++) {
        const crl_ev_t *e = &lp->d.ev[i];
        if ((e->layer & CRL_HID) || e->t < lp->ed_t0 || e->t >= lp->ed_t0 + CRL_STEP) continue;
        if (e->t - lp->ed_t0 + e->dur > CRL_STEP) return 0;       /* (it sounds past the step: whole steps) */
        if (e->dur > top) top = e->dur;
    }
    return top == CRL_FRAC || top == 2u * CRL_FRAC || top == 3u * CRL_FRAC ? top / CRL_FRAC : 0u;
}

int cr_loop_ed_setnfrac(cr_loop_t *lp, uint32_t f)
{
    uint32_t i;
    if (!lp->ed || f < 1u || f > 3u || !cr_loop_ed_nlen(lp)) return 0;
    crl_ed_flat(lp);
    for (i = 0; i < lp->d.nev; i++) {
        crl_ev_t *e = &lp->d.ev[i];
        if ((e->layer & CRL_HID) || e->t < lp->ed_t0 || e->t >= lp->ed_t0 + CRL_STEP) continue;
        e->dur = (uint16_t)(f * CRL_FRAC);           /* (from where the note starts: a gate, not a place) */
    }
    lp->gen++;
    lp->dirty = 1;
    return 1;
}

/* the cursor's step has an event (what sounds) and a place in the table: its index, else -1 */
static int crl_ed_sp(const cr_loop_t *lp)
{
    uint32_t i, s = lp->ed_t0 / CRL_STEP;
    if (s >= CRL_STEPS) return -1;
    for (i = 0; i < lp->d.nev; i++) {
        const crl_ev_t *e = &lp->d.ev[i];
        if (!(e->layer & CRL_HID) && e->t >= lp->ed_t0 && e->t < lp->ed_t0 + CRL_STEP) return (int)s;
    }
    return -1;
}

int cr_loop_ed_prop(const cr_loop_t *lp, int which)
{
    int s = crl_ed_sp(lp);
    if (s < 0) return -1;
    return which == CRL_SP_RATCH ? lp->d.st[s][0] & 3 : which == CRL_SP_COND ? (lp->d.st[s][0] >> 2) & 15
         : which == CRL_SP_VOIC ? lp->d.st[s][1] & 31 : lp->d.st[s][1] >> 5;
}

int cr_loop_ed_setprop(cr_loop_t *lp, int which, int v)
{
    int s = crl_ed_sp(lp);
    uint8_t *st;
    if (!lp->ed || s < 0 || v < 0) return 0;
    if ((which == CRL_SP_RATCH && v >= (int)CRL_NRATCH) || (which == CRL_SP_COND && v >= (int)CRL_NCOND) ||
        (which == CRL_SP_VOIC && v >= (int)CRL_NVOIC) || (which == CRL_SP_CHANCE && v >= (int)CRL_NCHANCE) ||
        which < CRL_SP_RATCH || which > CRL_SP_CHANCE)
        return 0;
    crl_ed_flat(lp);
    st = lp->d.st[s];
    if (v && !st[0] && !st[1]) {                     /* the step's first property: an entry more in the record: does
                                                      * it fit beside the events? (cr_loop_pack; only a loop within a
                                                      * few events of CRL_MAX_EV can fail this) */
        uint64_t has = 0;
        uint32_t i, k = 1, live = 0;
        for (i = 0; i < lp->d.nev; i++) {
            const crl_ev_t *e = &lp->d.ev[i];
            if ((e->layer & CRL_HID) || e->t >= lp->d.len) continue;
            live++;
            if (e->t / CRL_STEP < CRL_STEPS) has |= (uint64_t)1 << (e->t / CRL_STEP);
        }
        for (i = 0; i < CRL_STEPS; i++)
            k += ((has >> i) & 1u) && (lp->d.st[i][0] || lp->d.st[i][1]);
        if (CRL_REC_HDR + 2u * lp->d.nlayers + 7u * live + 1u + 3u * k > CRL_REC_MAX) return CRL_ED_NOROOM;
    }
    if (which == CRL_SP_RATCH) st[0] = (uint8_t)((st[0] & ~3u) | (uint32_t)v);
    else if (which == CRL_SP_COND) st[0] = (uint8_t)((st[0] & 3u) | (uint32_t)v << 2);
    else if (which == CRL_SP_VOIC) st[1] = (uint8_t)((st[1] & ~31u) | (uint32_t)v);
    else st[1] = (uint8_t)((st[1] & 31u) | (uint32_t)v << 5);
    lp->gen++;
    lp->dirty = 1;
    return 1;
}

int cr_loop_ed_len(cr_loop_t *lp, uint32_t len)
{
    if (!lp->ed || !len || len > CRL_MAX_LEN || len == lp->d.len) return 0;
    crl_ed_flat(lp);
    lp->d.len = len;
    if (len > lp->ed_len) lp->ed_len = len;
    lp->gen++;
    lp->dirty = 1;
    crl_ed_renorm(lp);
    return 1;
}

int cr_loop_ed_vel(cr_loop_t *lp, int delta)
{
    uint32_t i, B = lp->ed_t0 + CRL_STEP;
    int top = 0;
    if (!lp->ed) return 0;
    if (delta) crl_ed_flat(lp);
    for (i = 0; i < lp->d.nev; i++) {
        crl_ev_t *e = &lp->d.ev[i];
        int v;
        if ((e->layer & CRL_HID) || e->t < lp->ed_t0 || e->t >= B) continue;
        v = (int)e->vel + delta;
        e->vel = (uint8_t)(v < 1 ? 1 : v > 127 ? 127 : v);
        if (e->vel > top) top = e->vel;
    }
    if (top && delta) {
        lp->gen++;
        lp->dirty = 1;
    }
    return top;
}

int cr_loop_ed_transpose(cr_loop_t *lp, int semis)
{
    uint32_t i, B = lp->ed_t0 + CRL_STEP, n = 0;
    if (!lp->ed || !semis) return 0;
    for (i = 0; i < lp->d.nev; i++) {                /* every note of the step fits, or none moves */
        const crl_ev_t *e = &lp->d.ev[i];
        int r;
        if ((e->layer & CRL_HID) || e->t < lp->ed_t0 || e->t >= B) continue;
        r = (int)e->root + semis;
        if (r < CRL_ROOT_MIN || r > CRL_ROOT_MAX) return 0;
        n++;
    }
    if (!n) return 0;
    crl_ed_flat(lp);                                 /* (an edit like the others: one layer; it removes hidden events
                                                      * only, so the step's are the ones counted) */
    for (i = 0; i < lp->d.nev; i++) {
        crl_ev_t *e = &lp->d.ev[i];
        if ((e->layer & CRL_HID) || e->t < lp->ed_t0 || e->t >= B) continue;
        e->root = (uint8_t)((int)e->root + semis);
    }
    lp->gen++;
    lp->dirty = 1;
    return 1;
}

uint32_t cr_loop_pos(const cr_loop_t *lp)
{
    if (lp->cap == CRL_CAP_REC) return lp->t - lp->rec0;
    if (lp->state == CRL_PLAYING && lp->d.len) return (lp->t - lp->cyc0) % lp->d.len;
    return 0;
}

void cr_loop_metro(cr_loop_t *lp, int on)
{
    lp->metro = (uint8_t)(on != 0);
    lp->met0 = lp->rt;
    lp->click = lp->metro ? 2u : 0u;
}

/* ------------------------------------------------------------- queries --- */
uint16_t cr_loop_ring(const cr_loop_t *lp)
{
    uint32_t bar = cr_loop_bar(lp), r = 0;
    if (lp->cap == CRL_CAP_COUNTIN) r = (lp->rt - lp->cin0) * 256u / bar;
    else if (lp->cap == CRL_CAP_REC)
        r = lp->rec_len ? (lp->t - lp->rec0) * 256u / lp->rec_len : (lp->t - lp->rec0) % bar * 256u / bar;
    else if (lp->state == CRL_PLAYING && lp->d.len) r = (lp->t - lp->cyc0) % lp->d.len * 256u / lp->d.len;
    return (uint16_t)(r > 256u ? 256u : r);
}

void cr_loop_where(const cr_loop_t *lp, uint32_t *bar, uint32_t *beat)
{
    uint32_t bt = cr_loop_bar(lp), be = cr_loop_beat(lp), rel = 0;
    if (lp->cap == CRL_CAP_COUNTIN) {
        rel = lp->rt - lp->cin0;
        *bar = 0;
        *beat = rel >= bt ? 0u : (bt - rel + be - 1u) / be;
        return;
    }
    if (lp->cap == CRL_CAP_REC) rel = lp->t - lp->rec0;
    else if (lp->state == CRL_PLAYING && lp->d.len) rel = (lp->t - lp->cyc0) % lp->d.len;
    *bar = rel / bt + 1u;
    *beat = rel % bt / be + 1u;
}

/* -------------------------------------------------------- the slot record --- */
static void crl_put16(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void crl_put32(uint8_t *p, uint32_t v) { crl_put16(p, v); crl_put16(p + 2, v >> 16); }
static uint32_t crl_get16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }
static uint32_t crl_get32(const uint8_t *p) { return crl_get16(p) | crl_get16(p + 2) << 16; }

uint32_t cr_loop_pack(const crl_data_t *d, uint8_t *buf, uint32_t max)
{
    uint32_t i, live = 0, n, o, part = 0, nst = 0, s;
    uint64_t has = 0;                                /* the steps (of the first CRL_STEPS) a stored event starts in */
#define CRL_PK(e) (!((e)->layer & CRL_HID) && (e)->t < d->len)   /* stored: what sounds (not hidden by a Replace layer,
                                                      * nor beyond the end: the editor's Loop length keeps those in RAM
                                                      * until it closes) */
    for (i = 0; i < d->nev && i < CRL_MAX_EV; i++)
        if (CRL_PK(&d->ev[i])) {
            live++;
            part |= CRL_EV_BASS(&d->ev[i]);
            if (d->ev[i].t / CRL_STEP < CRL_STEPS) has |= (uint64_t)1 << (d->ev[i].t / CRL_STEP);
        }
    n = CRL_REC_HDR + 2u * d->nlayers + 7u * live;
    if (!d->len || !live || !d->nlayers || d->nlayers > CRL_MAX_LAYERS || d->nev > CRL_MAX_EV || n > max) return 0;
    /* the steps' properties (CRL_RF_STEP): an entry per step that has one AND an event, after the events, in the room
     * they leave (CRL_REC_MAX is what it was, so every older firmware's length check passes: with 512 events and one
     * layer 20 entries fit, all 64 up to 494 events; the editor refuses a property that would not fit,
     * cr_loop_ed_setprop). None: not a byte differs from what 0.14 .. 0.17 wrote */
    for (s = 0; s < CRL_STEPS; s++)
        if (((has >> s) & 1u) && (d->st[s][0] || d->st[s][1]) && n + 1u + 3u * (nst + 1u) <= max) nst++;
    crl_put32(buf, CRL_REC_MAGIC);
    buf[4] = 1;                                      /* record version */
    buf[5] = d->sig;
    buf[6] = d->nlayers;
    buf[7] = (uint8_t)((part ? CRL_RF_PART : 0u) | (nst ? CRL_RF_STEP : 0u));   /* flags (0.14 .. 0.17 wrote 0 and
                                                      * never read it: they load this record, its bass-side notes as
                                                      * chord-side single notes, its steps without their properties) */
    crl_put32(buf + 8, d->len);
    crl_put16(buf + 12, live);
    crl_put16(buf + 14, CRL_PPQN);
    for (i = 0; i < d->nlayers; i++) crl_put16(buf + CRL_REC_HDR + 2u * i, 0);
    for (i = 0; i < d->nev; i++) {                   /* events per layer */
        uint32_t l;
        uint8_t *p;
        if (!CRL_PK(&d->ev[i])) continue;
        l = d->ev[i].layer < d->nlayers ? d->ev[i].layer : d->nlayers - 1u;
        p = buf + CRL_REC_HDR + 2u * l;
        crl_put16(p, crl_get16(p) + 1u);
    }
    o = CRL_REC_HDR + 2u * d->nlayers;
    for (i = 0; i < d->nev; i++) {
        const crl_ev_t *e = &d->ev[i];
        if (!CRL_PK(e)) continue;
        crl_put16(buf + o, e->t);
        crl_put16(buf + o + 2, e->dur);
        buf[o + 4] = e->root;
        buf[o + 5] = e->vel;
        buf[o + 6] = e->qx;
        o += 7u;
    }
#undef CRL_PK
    if (nst) {
        buf[o++] = (uint8_t)nst;
        for (s = i = 0; s < CRL_STEPS && i < nst; s++)
            if (((has >> s) & 1u) && (d->st[s][0] || d->st[s][1])) {
                buf[o] = (uint8_t)s;
                buf[o + 1] = d->st[s][0];
                buf[o + 2] = d->st[s][1];
                o += 3u;
                i++;
            }
        n = o;
    }
    return n;
}

int cr_loop_unpack(const uint8_t *buf, uint32_t n, crl_data_t *d)
{
    uint32_t i, l, k, nl, nev, len, o, cnt;
    crl_zero(d, crl_data_size(0));
    if (!buf || n < CRL_REC_HDR || crl_get32(buf) != CRL_REC_MAGIC || buf[4] != 1u || buf[5] >= CRL_NSIG ||
        crl_get16(buf + 14) != CRL_PPQN)
        return 0;
    nl = buf[6];
    len = crl_get32(buf + 8);
    nev = crl_get16(buf + 12);
    if (!nl || nl > CRL_MAX_LAYERS || !nev || nev > CRL_MAX_EV || !len || len > CRL_MAX_LEN ||
        n < CRL_REC_HDR + 2u * nl + 7u * nev)
        return 0;
    for (l = cnt = 0; l < nl; l++) cnt += crl_get16(buf + CRL_REC_HDR + 2u * l);
    if (cnt != nev) return 0;
    o = CRL_REC_HDR + 2u * nl;
    for (l = k = 0; l < nl; l++)
        for (i = crl_get16(buf + CRL_REC_HDR + 2u * l); i; i--, k++, o += 7u) {
            crl_ev_t *e = &d->ev[k];
            e->t = (uint16_t)crl_get16(buf + o);
            e->dur = (uint16_t)crl_get16(buf + o + 2);
            e->root = buf[o + 4];
            e->vel = buf[o + 5];
            e->qx = buf[o + 6];
            if (!(e->qx >> 4))                       /* a single note: its part, when the record has one (an older
                                                      * record's nibble: the extension keys held, never heard) */
                e->qx = (uint8_t)(buf[7] & CRL_RF_PART ? e->qx & CRL_X_BASS : 0u);
            e->layer = (uint8_t)l;
            if (e->t >= len || !e->dur || e->root > 127u || !e->vel || e->vel > 127u || (e->qx >> 4) >= CR_Q_COUNT) {
                crl_zero(d, crl_data_size(0));
                return 0;
            }
        }
    if (buf[7] & CRL_RF_STEP) {                      /* the steps' properties: checked as everything else is */
        uint32_t cnt2 = n > o ? buf[o] : 0u, prev = 0;
        if (!cnt2 || cnt2 > CRL_STEPS || n < o + 1u + 3u * cnt2) {
            crl_zero(d, crl_data_size(0));
            return 0;
        }
        for (i = 0, o++; i < cnt2; i++, o += 3u) {
            uint32_t s = buf[o];
            if (s >= CRL_STEPS || (i && s <= prev) || (buf[o + 1] & 0xC0u) || (buf[o + 1] >> 2) >= CRL_NCOND ||
                (buf[o + 2] & 31u) >= CRL_NVOIC || (!buf[o + 1] && !buf[o + 2])) {
                crl_zero(d, crl_data_size(0));
                return 0;
            }
            d->st[s][0] = buf[o + 1];
            d->st[s][1] = buf[o + 2];
            prev = s;
        }
    }
    d->len = len;
    d->nev = (uint16_t)nev;
    d->nlayers = (uint8_t)nl;
    d->sig = buf[5];
    return 1;
}
