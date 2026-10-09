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

void cr_loop_init(cr_loop_t *lp)
{
    crl_zero(lp, sizeof *lp);
    lp->count_in = 1;
    lp->level = 100;
    lp->metro_vol = 70;
    lp->state = CRL_EMPTY;
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
}

static void crl_play_ev(cr_loop_t *lp, cr_t *c, const crl_ev_t *e)
{
    int lv, best = -1;
    uint32_t vel = (uint32_t)e->vel * lp->level / 100u;
    if (!vel) return;
    for (lv = 0; lv < CR_MAX_LOOPV; lv++)
        if (!lp->pv[lv].used) { best = lv; break; }
    if (best < 0)                                   /* all busy: the one that would end first */
        for (lv = 0; lv < CR_MAX_LOOPV; lv++)
            if (best < 0 || (int32_t)(lp->pv[lv].off - lp->pv[best].off) < 0) best = lv;
    cr_loop_event(c, best, e->root, (uint8_t)(e->qx >> 4), (uint8_t)(e->qx & 15u), (uint8_t)vel, 1);
    lp->pv[best].used = 1;
    lp->pv[best].off = lp->t + (e->dur ? e->dur : 1u);
    lp->played++;
}

static void crl_fire(cr_loop_t *lp, cr_t *c, uint32_t lo, uint32_t hi)   /* events at positions lo..hi */
{
    uint32_t i;
    for (i = 0; i < lp->d.nev; i++) {
        const crl_ev_t *e = &lp->d.ev[i];
        if (e->t >= lo && e->t <= hi) crl_play_ev(lp, c, e);
    }
}

static void crl_offs(cr_loop_t *lp, cr_t *c)
{
    int lv;
    for (lv = 0; lv < CR_MAX_LOOPV; lv++) {
        if (!lp->pv[lv].used) continue;
        if (!cr_loop_busy(c, lv)) lp->pv[lv].used = 0;            /* (a panic took it) */
        else if ((int32_t)(lp->t - lp->pv[lv].off) >= 0) {
            cr_loop_event(c, lv, 0, 0, 0, 0, 0);
            lp->pv[lv].used = 0;
        }
    }
}

static void crl_take_next(cr_loop_t *lp)            /* a queued slot replaces the loop at the cycle's end */
{
    const crl_data_t *n = lp->next;
    lp->next = 0;
    if (n && n->len && n->nev) crl_copy(&lp->d, n, crl_data_size(n->nev));
    else crl_zero(&lp->d, crl_data_size(0));
    lp->dirty = 0;
    lp->loads++;
    if (!lp->d.len) lp->state = CRL_EMPTY;
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
        if (lp->next_on) {
            lp->next_on = 0;
            crl_take_next(lp);
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
    lp->scan = 0;
    crl_play_upto(lp, c);
    if (lp->metro) lp->click = 2;                    /* the grid restarts here: its first beat now */
}

/* ------------------------------------------------------------- capture --- */
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
    } else {
        return;
    }
    if (lp->d.nev >= CRL_MAX_EV) { lp->full = 1; return; }
    dur = lp->t - start;
    dur = dur < 1u ? 1u : dur > CRL_MAX_LEN ? CRL_MAX_LEN : dur;
    e = &lp->d.ev[lp->d.nev++];
    e->t = (uint16_t)rel;
    e->dur = (uint16_t)dur;
    e->root = (uint8_t)(root < 0 ? 0 : root > 127 ? 127 : root);
    e->vel = (uint8_t)(vel ? vel : 1u);
    e->qx = (uint8_t)((q < CR_Q_COUNT ? q : 0u) << 4 | (ext & 15u));
    e->layer = lp->d.nlayers;
    if (lp->d.nev >= CRL_MAX_EV) lp->full = 1;
}

void cr_loop_gesture(cr_loop_t *lp, uint32_t gid, int16_t root, uint8_t q, uint8_t ext, uint8_t vel, int on)
{
    uint32_t i, k = CRL_OPEN;
    if (on) {
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
        if (lp->cap == CRL_CAP_ARMED) {                                /* Free: the first chord starts the take */
            lp->cap = CRL_CAP_REC;
            lp->rec0 = lp->t;
            lp->rec_len = 0;
            lp->layer_ev0 = 0;
            lp->open[k].t0 = lp->t;
        } else if (lp->cap == CRL_CAP_OD_ARMED) {                      /* the first chord opens the layer */
            lp->cap = CRL_CAP_OD;
            lp->od0 = lp->t;
            lp->layer_ev0 = lp->d.nev;
        }
        return;
    }
    for (i = 0; i < CRL_OPEN; i++)
        if (lp->open[i].used && lp->open[i].gid == gid) {
            lp->open[i].used = 0;
            crl_write(lp, lp->open[i].t0, root, q, ext, vel);
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
    lp->d.nev = 0;
    lp->d.len = 0;
    lp->d.nlayers = 0;
    lp->state = CRL_EMPTY;
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
        lp->d.sig = lp->sig;
        crl_quantize(lp, 0);
        lp->dirty = 1;
        lp->state = CRL_PLAYING;                     /* the take was the first cycle: playback goes on from here */
        lp->cyc0 = lp->rec0;
        lp->scan = elapsed + 1u;
        crl_play_upto(lp, c);
        return CRL_DID_COMMIT;
    }
    if (cap == CRL_CAP_OD) {
        crl_close_open(lp);
        lp->cap = CRL_CAP_NONE;
        lp->full = 0;
        if (lp->d.nev > lp->layer_ev0) {
            crl_quantize(lp, lp->layer_ev0);
            lp->d.nlayers++;
            lp->dirty = 1;
        }
        return CRL_DID_OD_END;
    }
    lp->cap = CRL_CAP_NONE;
    return CRL_DID_CANCEL;
}

/* ----------------------------------------------------------- the clock --- */
static void crl_tick1(cr_loop_t *lp, cr_t *c)
{
    uint32_t bar = cr_loop_bar(lp), beat = cr_loop_beat(lp), anchor = 0, rel;
    int on = 0;
    if (lp->cap == CRL_CAP_COUNTIN && lp->t - lp->cin0 >= bar) {
        lp->cap = CRL_CAP_REC;
        lp->rec0 = lp->cin0 + bar;
        lp->layer_ev0 = 0;
    }
    if (lp->cap == CRL_CAP_REC && ((lp->rec_len && lp->t - lp->rec0 >= lp->rec_len) ||
                                   (!lp->rec_len && lp->t - lp->rec0 >= CRL_MAX_LEN) || lp->full))
        crl_commit(lp, c);
    if (lp->cap == CRL_CAP_OD && lp->full) crl_commit(lp, c);
    crl_offs(lp, c);
    crl_play_upto(lp, c);
    /* the click: the count-in always (it is on), else the metronome on the take's / the loop's / its own grid */
    if (lp->cap == CRL_CAP_COUNTIN) { anchor = lp->cin0; on = 1; }
    else if (lp->metro) {
        on = 1;
        anchor = lp->cap == CRL_CAP_REC ? lp->rec0 : lp->state == CRL_PLAYING ? lp->cyc0 : lp->met0;
    }
    if (on) {
        rel = lp->t - anchor;
        if (rel % beat == 0u) lp->click = rel % bar == 0u ? 2u : 1u;
    }
}

void cr_loop_tick(cr_loop_t *lp, cr_t *c, uint32_t now_ms)
{
    uint32_t dms, bpm = c->bpm ? c->bpm : 120u;
    if (!lp->clk_on) {
        lp->clk_on = 1;
        lp->last_ms = now_ms;
        crl_tick1(lp, c);                            /* tick 0 */
        return;
    }
    dms = now_ms - lp->last_ms;
    lp->last_ms = now_ms;
    if (dms > 1000u) dms = 1000u;
    lp->acc += dms * bpm * CRL_PPQN;
    while (lp->acc >= 60000u) {
        lp->acc -= 60000u;
        lp->t++;
        crl_tick1(lp, c);
    }
}

/* ----------------------------------------------------------- transport --- */
void cr_loop_stop(cr_loop_t *lp, cr_t *c)
{
    if (lp->cap == CRL_CAP_REC || lp->cap == CRL_CAP_OD) crl_commit(lp, c);
    lp->cap = CRL_CAP_NONE;
    crl_voices_off(lp, c);
    lp->next_on = 0;
    lp->state = lp->d.len ? CRL_STOPPED : CRL_EMPTY;
}

int cr_loop_rec(cr_loop_t *lp, cr_t *c)
{
    switch (lp->cap) {
    case CRL_CAP_NONE:
        if (lp->state == CRL_EMPTY) {
            crl_drop_take(lp);
            lp->full = 0;
            lp->layer_ev0 = 0;
            if (!lp->sync) {
                lp->cap = CRL_CAP_ARMED;
                return CRL_DID_ARM;
            }
            lp->rec_len = CRL_BARS[lp->sync < CRL_NSYNC ? lp->sync : 1] * cr_loop_bar(lp);
            if (lp->count_in) {
                lp->cap = CRL_CAP_COUNTIN;
                lp->cin0 = lp->t;
                lp->click = 2;                       /* the count-in's first beat now */
                return CRL_DID_COUNTIN;
            }
            lp->cap = CRL_CAP_REC;
            lp->rec0 = lp->t;
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
        return CRL_DID_OD_ARM;
    case CRL_CAP_REC:
    case CRL_CAP_OD:
        return crl_commit(lp, c);
    default:                                         /* armed / counting in: cancelled */
        lp->cap = CRL_CAP_NONE;
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
        return CRL_DID_CANCEL;
    case CRL_CAP_REC:
        return crl_commit(lp, c);
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
        lp->cap = CRL_CAP_NONE;
        lp->full = 0;
        return had ? CRL_DID_UNDO : CRL_DID_CANCEL;
    }
    if (lp->cap != CRL_CAP_NONE || lp->d.nlayers <= 1u) return CRL_DID_NOTHING;
    for (i = 0; i < lp->d.nev && lp->d.ev[i].layer < lp->d.nlayers - 1u; i++) {}
    lp->d.nev = (uint16_t)i;                         /* (its sounding notes finish) */
    lp->d.nlayers--;
    lp->full = 0;
    lp->dirty = 1;
    lp->next_on = 0;                                 /* a queued switch: cancelled, this loop is being edited */
    return CRL_DID_UNDO;
}

int cr_loop_clear(cr_loop_t *lp, cr_t *c)
{
    lp->cap = CRL_CAP_NONE;
    crl_voices_off(lp, c);
    crl_drop_take(lp);
    lp->full = 0;
    lp->next_on = 0;
    lp->dirty = 1;
    return CRL_DID_CLEAR;
}

void cr_loop_panic(cr_loop_t *lp)
{
    uint32_t i;
    for (i = 0; i < CR_MAX_LOOPV; i++) lp->pv[i].used = 0;          /* cr_panic ended the voices */
    if (lp->cap == CRL_CAP_REC || lp->cap == CRL_CAP_ARMED || lp->cap == CRL_CAP_COUNTIN) {
        crl_drop_take(lp);                                          /* a fresh take is dropped */
    } else if (lp->cap == CRL_CAP_OD && lp->d.nev > lp->layer_ev0) {
        crl_quantize(lp, lp->layer_ev0);                            /* a partial overdub is kept */
        lp->d.nlayers++;
        lp->dirty = 1;
    } else if (lp->cap == CRL_CAP_OD || lp->cap == CRL_CAP_OD_ARMED) {
        lp->d.nev = lp->layer_ev0;
    }
    for (i = 0; i < CRL_OPEN; i++) lp->open[i].used = 0;
    lp->cap = CRL_CAP_NONE;
    lp->full = 0;
    lp->next_on = 0;
    lp->state = lp->d.len ? CRL_STOPPED : CRL_EMPTY;
}

void cr_loop_set(cr_loop_t *lp, cr_t *c, const crl_data_t *d)
{
    lp->cap = CRL_CAP_NONE;
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
    lp->next = d;
    lp->next_on = 1;
}

void cr_loop_metro(cr_loop_t *lp, int on)
{
    lp->metro = (uint8_t)(on != 0);
    lp->met0 = lp->t;
    lp->click = lp->metro ? 2u : 0u;
}

/* ------------------------------------------------------------- queries --- */
uint16_t cr_loop_ring(const cr_loop_t *lp)
{
    uint32_t bar = cr_loop_bar(lp), r = 0;
    if (lp->cap == CRL_CAP_COUNTIN) r = (lp->t - lp->cin0) * 256u / bar;
    else if (lp->cap == CRL_CAP_REC)
        r = lp->rec_len ? (lp->t - lp->rec0) * 256u / lp->rec_len : (lp->t - lp->rec0) % bar * 256u / bar;
    else if (lp->state == CRL_PLAYING && lp->d.len) r = (lp->t - lp->cyc0) % lp->d.len * 256u / lp->d.len;
    return (uint16_t)(r > 256u ? 256u : r);
}

void cr_loop_where(const cr_loop_t *lp, uint32_t *bar, uint32_t *beat)
{
    uint32_t bt = cr_loop_bar(lp), be = cr_loop_beat(lp), rel = 0;
    if (lp->cap == CRL_CAP_COUNTIN) {
        rel = lp->t - lp->cin0;
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
    uint32_t i, n = CRL_REC_HDR + 2u * d->nlayers + 7u * d->nev, o;
    if (!d->len || !d->nev || !d->nlayers || d->nlayers > CRL_MAX_LAYERS || d->nev > CRL_MAX_EV || n > max) return 0;
    crl_put32(buf, CRL_REC_MAGIC);
    buf[4] = 1;                                      /* record version */
    buf[5] = d->sig;
    buf[6] = d->nlayers;
    buf[7] = 0;
    crl_put32(buf + 8, d->len);
    crl_put16(buf + 12, d->nev);
    crl_put16(buf + 14, CRL_PPQN);
    for (i = 0; i < d->nlayers; i++) crl_put16(buf + CRL_REC_HDR + 2u * i, 0);
    for (i = 0; i < d->nev; i++) {                   /* events per layer */
        uint32_t l = d->ev[i].layer < d->nlayers ? d->ev[i].layer : d->nlayers - 1u;
        uint8_t *p = buf + CRL_REC_HDR + 2u * l;
        crl_put16(p, crl_get16(p) + 1u);
    }
    o = CRL_REC_HDR + 2u * d->nlayers;
    for (i = 0; i < d->nev; i++, o += 7u) {
        const crl_ev_t *e = &d->ev[i];
        crl_put16(buf + o, e->t);
        crl_put16(buf + o + 2, e->dur);
        buf[o + 4] = e->root;
        buf[o + 5] = e->vel;
        buf[o + 6] = e->qx;
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
            e->layer = (uint8_t)l;
            if (e->t >= len || !e->dur || e->root > 127u || !e->vel || e->vel > 127u || (e->qx >> 4) >= CR_Q_COUNT) {
                crl_zero(d, crl_data_size(0));
                return 0;
            }
        }
    d->len = len;
    d->nev = (uint16_t)nev;
    d->nlayers = (uint8_t)nl;
    d->sig = buf[5];
    return 1;
}
