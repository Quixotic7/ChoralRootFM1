/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SLICE: a sample slicer, played from the keys and the sequencer. Felucca's own design.
 *
 * Material (SRC): the built-in BREAK (tools/gen_samples.py: one bar of 16ths at 120 BPM arranged
 * from Felucca's generated drums, stored after the SAMPLE sets) or a user slot USR1..3 (the same
 * slots as SAMPLE's, eng_sample.c), whose zones are played one after the other as one recording
 * (a zone that shares its data with an earlier one is skipped). An empty slot plays BREAK.
 *
 * Slices (DIV): 4 / 8 / 16 / 32 equal ones, AUTO: one per onset, or MAN: set by hand on the EDIT family's SLICES
 * page (ui_slice.c) for a user slot (BREAK and an empty slot: MAN plays AUTO). IMA ADPCM can only be entered
 * at a known decoder state, so each source keeps a table (slc_src_t): the states at the 128 grid
 * points k * len / 128 (every equal slice starts on one, and they are the reverse checkpoints) and
 * up to 32 AUTO starts with their states. BREAK's table is written at build time (its hits are
 * the AUTO slices); a user slot's is computed by slc_scan when the slot becomes valid
 * (smp_user_scan: boot, upload end; main loop, never the audio ISR): two decoding passes; per hop
 * of 32 samples the peak of the first difference (hats, snares, clicks stand out, a kick's tail
 * does not) and the plain peak (low hits); an onset is a hop where either rises 1.75x over its
 * follower (~23 ms / ~93 ms, so a low tone's cycles are no rises), above 1/12 (1/8) of its
 * largest value, at least 50 ms after the last one; the slice starts one hop before the hop that
 * rose (1.5..3 ms of pre-roll, never after the hit). RAM: 3 x 972 B of tables, 3 KiB of reverse
 * windows; a full 7 s slot scans in ~0.3 M decoder steps.
 *
 * Playing: note n plays slice (n - 60 - ROOT + START) mod the slice count (the keys: the lowest
 * key is slice 0, seq.c kb_map). Slices play at the source rate times PITCH (varispeed: faster is
 * higher). MODE: ONE plays the slice to its end (a note-off does not stop it), GATE until the
 * note-off (then the ADSR release) or the slice end, LOOP repeats the slice while the note is held.
 * REV plays a slice backwards: windows of 64 samples are decoded forwards from the nearest
 * checkpoint (grid point or the slice start) into the voice's own buffer and read from the top
 * down; it costs about (len / 128) / 128 extra decodes per source sample (BREAK: ~3; a full
 * 7 s user slot: ~10). DECAY: an exponential fade per slice (127 = none). TONE: a gentle low-pass
 * (SAMPLE's CUT). The last ~1.5 ms of a slice fade out (no click at the cut), a new slice fades in
 * over one block (0.7 ms).
 * Voice state: ph[0] position (reverse: position + 1), ph[1] fraction Q16, ph[2] slice end (reverse:
 * its start), s[0] predictor (reverse: window start), s[1] step index, s[2] / s[3] previous / current
 * sample, s[4] source | segment << 2 | reverse << 6 | slice << 8 | DIV << 16, s[5] DECAY level Q15,
 * s[6] 0 to prime, 1 playing, 3 fading out, 2 ended, s[7] low-pass. */
#define SLC_GRID 128u                /* grid points: decoder states at k * len / SLC_GRID */
#define SLC_GRID_LOG2 7
#define SLC_AUTO 32u                 /* AUTO slices at most */
#define SLC_SEGS 16u                 /* zones of a user slot */
#define SLC_RB 64u                   /* reverse: samples decoded per window */
#define SLC_BASE 60                  /* note of slice 0 (+ the track's ROOT) */
#define SLC_HOP 32u                  /* AUTO detector: samples per hop */

typedef struct {
    uint32_t off, at, n;             /* data (SMP_DATA index), material position of its first sample, samples */
} slc_seg_t;
typedef struct {
    uint32_t len, rate, nseg, nauto; /* material samples (0 = none), rate (as smp_zone_t), segments, AUTO slices */
    slc_seg_t seg[SLC_SEGS];
    uint32_t grid[SLC_GRID];         /* decoder state before sample k * len / SLC_GRID: */
                                     /* (uint16_t)predictor | step index << 16 | segment << 24 */
    uint32_t apos[SLC_AUTO], ast[SLC_AUTO];   /* AUTO slice starts (apos[0] = 0) and their states */
} slc_src_t;

static const slc_src_t SLC_BREAK = SLC_BREAK_INIT;
static slc_src_t slc_usr[SMP_USER_SLOTS];
static int16_t slc_rbuf[NPART][NPOLY][SLC_RB];        /* reverse windows, one per part voice (its cap: NPOLY) */
static const char *const N_SLC_SRC[] = {"BREAK", "USR1", "USR2", "USR3"};
static const char *const N_SLC_DIV[] = {"4", "8", "16", "32", "AUTO", "MAN"};
static const char *const N_SLC_MODE[] = {"ONE", "GATE", "LOOP"};
static const char *const N_SLC_REV[] = {"OFF", "ON"};
enum { SLC_ONE, SLC_GATE, SLC_LOOP };
#define SLC_DIV_AUTO 4u
#define SLC_DIV_MAN 5u               /* the slice starts set by hand (the SLICES page); none yet: the AUTO ones */
#define SLC_MIN 64u                  /* MAN: the shortest slice (samples) */

/* MAN slices (based on hugelton/Felucca#27 by andreahaku): per user slot, in RAM; slice_store.c keeps them in flash
 * with the sample. Two copies: the audio side reads slc_man[k][slc_man_cur[k]], the SLICES page (main loop) changes
 * the other one and then flips slc_man_cur (one byte), so a note-on never sees a half-edited table. n = 0: none */
typedef struct {
    uint32_t n;                      /* slices, 0 = none */
    uint32_t end;                    /* where the last slice ends (0 = the material's end) */
    uint32_t pos[SLC_AUTO], st[SLC_AUTO];     /* slice starts and their decoder states (as ast) */
} slc_man_t;
static slc_man_t slc_man[SMP_USER_SLOTS][2];
static volatile uint8_t slc_man_cur[SMP_USER_SLOTS];
static void (*slc_man_load)(uint32_t k);        /* slice_store.c: a slot was read (smp_user_scan): its stored slices */
static uint8_t slc_man_save;                     /* bit k: slot k's slices changed, to flash (slice_store.c) */

/* source 0 = BREAK, 1..3 = USR1..3; 0 = no material (an empty or erased slot) */
static const slc_src_t *slc_get(uint32_t src)
{
    if (!src)
        return SLC_BREAK.len ? &SLC_BREAK : 0;
    src--;
    return src < SMP_USER_SLOTS && usr_nz[src] && slc_usr[src].len ? &slc_usr[src] : 0;
}

/* ---- the ADPCM decoder over the material (segments one after the other, each from 0 / 0) */
typedef struct {
    int32_t pred, idx;
    uint32_t pos, seg;
} slc_dec_t;
static inline void slc_dec_at(slc_dec_t *d, uint32_t pos, uint32_t st)
{
    d->pos = pos;
    d->pred = (int16_t)(st & 0xFFFFu);
    d->idx = (int32_t)((st >> 16) & 127u);
    d->seg = st >> 24;
}
static inline uint32_t slc_dec_st(const slc_dec_t *d)
{
    return (uint32_t)(uint16_t)d->pred | (uint32_t)d->idx << 16 | d->seg << 24;
}
/* the sample at d->pos (< len), then the next position */
static inline int32_t slc_dec_next(const slc_src_t *s, slc_dec_t *d)
{
    const slc_seg_t *g = &s->seg[d->seg];
    uint32_t rel = d->pos - g->at, b, code;
    int32_t step, vd;
    if (rel >= g->n && d->seg + 1u < s->nseg) {         /* into the next zone: its data starts from 0 / 0 */
        d->seg++;
        g++;
        rel = d->pos - g->at;
        d->pred = 0;
        d->idx = 0;
    }
    b = SMP_DATA[g->off + (rel >> 1)];
    code = (rel & 1u) ? (b >> 4) : (b & 15u);
    step = IMA_STEP[d->idx];
    vd = step >> 3;
    if (code & 4u)
        vd += step;
    if (code & 2u)
        vd += step >> 1;
    if (code & 1u)
        vd += step >> 2;
    d->pred = clamp(d->pred + ((code & 8u) ? -vd : vd), -32768, 32767);
    d->idx = clamp(d->idx + IMA_IDX[code & 7u], 0, 88);
    d->pos++;
    return d->pred;
}

static inline uint32_t slc_gpos(const slc_src_t *s, uint32_t k) { return (k * s->len) >> SLC_GRID_LOG2; }
/* the user slot of s (0..2), SMP_USER_SLOTS for BREAK; its MAN slices in use, 0 = none (BREAK has none) */
static uint32_t slc_slot_of(const slc_src_t *s)
{
    return s >= slc_usr && s < slc_usr + SMP_USER_SLOTS ? (uint32_t)(s - slc_usr) : SMP_USER_SLOTS;
}
static const slc_man_t *slc_man_of(const slc_src_t *s)
{
    uint32_t k = slc_slot_of(s);
    const slc_man_t *m;
    if (k >= SMP_USER_SLOTS)
        return 0;
    m = &slc_man[k][slc_man_cur[k] & 1u];
    return m->n ? m : 0;
}
static uint32_t slc_man_end(const slc_src_t *s, const slc_man_t *m) { return m->end && m->end < s->len ? m->end : s->len; }
static uint32_t slc_count(const slc_src_t *s, uint32_t div)
{
    const slc_man_t *m = div == SLC_DIV_MAN ? slc_man_of(s) : 0;
    return div < SLC_DIV_AUTO ? 4u << div : m ? m->n : s->nauto;
}
/* the slice a note plays (n slices): note - C4 - ROOT + START, mod n */
static uint32_t slc_note_slice(const int16_t *p, uint32_t note, uint32_t n)
{
    int32_t j = ((int32_t)note - SLC_BASE - p[P_ROOT] + p[P_E2]) % (int32_t)n;
    return (uint32_t)(j < 0 ? j + (int32_t)n : j);
}

/* slice j of DIV div: [*a, *b), *st the state at *a */
static void slc_bounds(const slc_src_t *s, uint32_t div, uint32_t j, uint32_t *a, uint32_t *b, uint32_t *st)
{
    const slc_man_t *m = div == SLC_DIV_MAN ? slc_man_of(s) : 0;
    if (div < SLC_DIV_AUTO) {
        uint32_t w = SLC_GRID >> (2u + div);
        *a = slc_gpos(s, j * w);
        *b = slc_gpos(s, (j + 1u) * w);
        *st = s->grid[j * w];
    } else if (m) {
        j = j < m->n ? j : m->n - 1u;
        *a = m->pos[j];
        *b = j + 1u < m->n ? m->pos[j + 1u] : slc_man_end(s, m);
        *st = m->st[j];
    } else {
        j = j < s->nauto ? j : s->nauto - 1u;
        *a = s->apos[j];
        *b = j + 1u < s->nauto ? s->apos[j + 1u] : s->len;
        *st = s->ast[j];
    }
}
static void slc_bounds_v(const slc_src_t *s, const voice_t *v, uint32_t *a, uint32_t *b, uint32_t *st)
{
    uint32_t pk = (uint32_t)v->s[4];
    slc_bounds(s, (pk >> 16) & 7u, (pk >> 8) & 63u, a, b, st);
}

/* ---- the slice table of a user slot (main loop) */
/* the grid states and the AUTO slices of s (len samples; s->len stays 0 meanwhile): see the top */
static void slc_scan(slc_src_t *s, uint32_t len)
{
    slc_dec_t d;
    uint32_t pos = 0, k = 0, gap = ((s->rate >> 4) * 44100u >> 12) / 20u, last = 0, h0 = 0, st0 = 0;
    int32_t prev = 0, pkd = 0, pkr = 0, envd = 0, envr = 0, thd, thr;
    slc_dec_at(&d, 0, 0);
    while (pos++ < len) {                               /* pass 1: the largest step and sample */
        int32_t x = slc_dec_next(s, &d), a = x - prev;
        a = a < 0 ? -a : a;
        pkd = a > pkd ? a : pkd;
        a = x < 0 ? -x : x;
        pkr = a > pkr ? a : pkr;
        prev = x;
    }
    thd = pkd / 12;
    thr = pkr / 8;
    s->nauto = 1;
    s->apos[0] = 0;
    s->ast[0] = 0;
    slc_dec_at(&d, 0, 0);
    prev = 0;
    for (pos = 0; pos < len;) {                         /* pass 2: grid states, onsets */
        uint32_t hs = pos, hst = slc_dec_st(&d), e = len - pos > SLC_HOP ? pos + SLC_HOP : len;
        int32_t hd = 0, hr = 0;
        for (; pos < e; pos++) {
            int32_t x, a;
            while (k < SLC_GRID && ((k * len) >> SLC_GRID_LOG2) == pos)
                s->grid[k++] = slc_dec_st(&d);
            x = slc_dec_next(s, &d);
            a = x - prev;
            a = a < 0 ? -a : a;
            hd = a > hd ? a : hd;
            a = x < 0 ? -x : x;
            hr = a > hr ? a : hr;
            prev = x;
        }
        /* a rise of the high-passed peak (hats, snares, clicks) or of the peak (low hits: a slow
         * follower, so a low tone's cycles do not look like rises) */
        if (hs && ((hd > thd && hd * 4 > envd * 7) || (hr > thr && hr * 4 > envr * 7)) && h0 >= last + gap &&
            s->nauto < SLC_AUTO) {
            s->apos[s->nauto] = h0;                     /* from the hop before: pre-roll */
            s->ast[s->nauto++] = st0;
            last = h0;
        }
        envd = hd > envd ? hd : envd - envd / 16;
        envr = hr > envr ? hr : envr - envr / 64;
        h0 = hs;
        st0 = hst;
    }
}

/* smp_user_scan (eng_sample.c): valid 0 = the slot changes (SLICE voices on it stop), 1 = it is valid */
static void slc_user_scan(uint32_t k, int valid)
{
    slc_src_t *s = &slc_usr[k];
    uint32_t i, j, at = 0;
    s->len = 0;
    RING_PUBLISH();
    slc_man[k][0].n = slc_man[k][1].n = 0;          /* new material: its MAN slices are gone (slc_man_load: the stored) */
    if (!valid)
        return;
    s->nseg = 0;
    for (i = 0; i < usr_nz[k] && i < SLC_SEGS; i++) {
        const smp_zone_t *z = &usr_zone[k][i];
        for (j = 0; j < i && usr_zone[k][j].off != z->off; j++)
            ;
        if (j < i || !z->n)
            continue;                                   /* the same data under another key range: once */
        s->seg[s->nseg].off = z->off;
        s->seg[s->nseg].at = at;
        s->seg[s->nseg++].n = z->n;
        at += z->n;
    }
    if (!s->nseg)
        return;
    s->rate = usr_zone[k][0].rate;
    slc_scan(s, at);
    RING_PUBLISH();
    s->len = at;
    if (slc_man_load)
        slc_man_load(k);
}

/* ---- MAN: editing (main loop, the SLICES page; based on hugelton/Felucca#27 by andreahaku). Begin with
 * slc_man_begin, change the copy it returns with move / split / join, then slc_man_commit puts it in use. */
/* the decoder state before sample pos: decoded from the grid point at or below it */
static uint32_t slc_state_at(const slc_src_t *s, uint32_t pos)
{
    slc_dec_t d;
    uint32_t k = (pos << SLC_GRID_LOG2) / s->len;            /* (pos < 2^18: a slot holds < 200 K samples) */
    k = k < SLC_GRID ? k : SLC_GRID - 1u;
    while (k && slc_gpos(s, k) > pos)
        k--;
    while (k + 1u < SLC_GRID && slc_gpos(s, k + 1u) <= pos)
        k++;
    slc_dec_at(&d, slc_gpos(s, k), s->grid[k]);
    while (d.pos < pos)
        slc_dec_next(s, &d);
    return slc_dec_st(&d);
}

/* the copy to edit for user slot k: the MAN slices in use, or the AUTO ones when there are none (those that keep
 * SLC_MIN apart and from the end); 0 = no material in the slot */
static slc_man_t *slc_man_begin(uint32_t k)
{
    const slc_src_t *s = k < SMP_USER_SLOTS ? slc_get(k + 1u) : 0;
    const slc_man_t *cur;
    slc_man_t *m;
    uint32_t i;
    if (!s)
        return 0;
    cur = &slc_man[k][slc_man_cur[k] & 1u];
    m = &slc_man[k][(slc_man_cur[k] & 1u) ^ 1u];
    if (cur->n) {
        *m = *cur;
        return m;
    }
    m->n = 0;
    m->end = 0;
    for (i = 0; i < s->nauto; i++)
        if (s->apos[i] < s->len && s->len - s->apos[i] >= SLC_MIN &&
            (!m->n || s->apos[i] - m->pos[m->n - 1u] >= SLC_MIN)) {
            m->pos[m->n] = s->apos[i];
            m->st[m->n++] = s->ast[i];
        }
    return m;
}
static void slc_man_commit(uint32_t k)
{
    RING_PUBLISH();
    slc_man_cur[k] ^= 1u;
}

/* move the start of slice j by d samples (slice 0's start trims the material's head); keeps SLC_MIN to both
 * neighbours. Returns the new start. */
static uint32_t slc_man_move(const slc_src_t *s, slc_man_t *m, uint32_t j, int32_t d)
{
    int32_t lo, hi, p;
    if (j >= m->n)
        return 0u;
    lo = j ? (int32_t)(m->pos[j - 1u] + SLC_MIN) : 0;
    hi = (int32_t)(j + 1u < m->n ? m->pos[j + 1u] : slc_man_end(s, m)) - (int32_t)SLC_MIN;
    if (hi < lo)                                    /* no room (a very short slice pair): it stays */
        return m->pos[j];
    p = clamp((int32_t)m->pos[j] + d, lo, hi);
    if (p != (int32_t)m->pos[j]) {
        m->pos[j] = (uint32_t)p;
        m->st[j] = slc_state_at(s, (uint32_t)p);
    }
    return m->pos[j];
}

/* move the end of all slices (the last slice's end: a tail trim) by d samples, at most the material's end */
static uint32_t slc_man_move_end(const slc_src_t *s, slc_man_t *m, int32_t d)
{
    if (!m->n)
        return 0u;
    if (m->pos[m->n - 1u] + SLC_MIN <= s->len)       /* (else no room: the end stays) */
        m->end = (uint32_t)clamp((int32_t)slc_man_end(s, m) + d, (int32_t)(m->pos[m->n - 1u] + SLC_MIN), (int32_t)s->len);
    return slc_man_end(s, m);
}

/* split slice j in two at its middle; returns the new slice (j + 1), or j if it cannot (32 slices, too short) */
static uint32_t slc_man_split(const slc_src_t *s, slc_man_t *m, uint32_t j)
{
    uint32_t a, b, i;
    if (j >= m->n || m->n >= SLC_AUTO)
        return j;
    a = m->pos[j];
    b = j + 1u < m->n ? m->pos[j + 1u] : slc_man_end(s, m);
    if (b - a < 2u * SLC_MIN)
        return j;
    for (i = m->n; i > j + 1u; i--) {
        m->pos[i] = m->pos[i - 1u];
        m->st[i] = m->st[i - 1u];
    }
    m->pos[j + 1u] = a + (b - a) / 2u;
    m->st[j + 1u] = slc_state_at(s, m->pos[j + 1u]);
    m->n++;
    return j + 1u;
}

/* join slice j (1 .. n - 1) to the one before (its start goes); returns that one */
static uint32_t slc_man_join(slc_man_t *m, uint32_t j)
{
    uint32_t i;
    if (!j || j >= m->n)
        return j < m->n ? j : 0u;
    for (i = j; i + 1u < m->n; i++) {
        m->pos[i] = m->pos[i + 1u];
        m->st[i] = m->st[i + 1u];
    }
    m->n--;
    return j - 1u;
}

/* put stored slices in use for user slot k (n starts pos[], end; 0 = the material's end); 0 = done, -1 = they do
 * not fit its material (nothing changes). Junk is refused: no sums that can wrap */
static int slc_man_restore(uint32_t k, uint32_t n, uint32_t end, const uint32_t *pos)
{
    const slc_src_t *s = k < SMP_USER_SLOTS ? slc_get(k + 1u) : 0;
    slc_man_t *m;
    uint32_t i, e;
    if (!s || !n || n > SLC_AUTO || end > s->len)
        return -1;
    e = end ? end : s->len;
    for (i = 0; i < n; i++)
        if (pos[i] >= e || e - pos[i] < SLC_MIN || (i && pos[i] < pos[i - 1u]) || (i && pos[i] - pos[i - 1u] < SLC_MIN))
            return -1;
    m = slc_man_begin(k);
    m->n = n;
    m->end = end;
    for (i = 0; i < n; i++) {
        m->pos[i] = pos[i];
        m->st[i] = slc_state_at(s, pos[i]);
    }
    slc_man_commit(k);
    return 0;
}

/* ---- voices */
static int16_t *slc_rb(track_t *t, voice_t *v)
{
    uint32_t p = (uint32_t)(t - trk), i = (uint32_t)(v - t->v);
    return p < NPART && i < NPOLY ? slc_rbuf[p][i] : 0;
}

static void slice_note_on(track_t *t, voice_t *v)
{
    const int16_t *p = t->p;
    uint32_t src = (uint32_t)p[P_E0] & 3u, div = (uint32_t)clamp(p[P_E1], 0, SLC_DIV_MAN), rev = p[P_E5] != 0;
    uint32_t a, b, st, j;
    const slc_src_t *s = slc_get(src);
    if (!s) {                                           /* an empty slot: the built-in BREAK */
        src = 0;
        s = slc_get(0);
    }
    v->ph[1] = 0;
    v->s[2] = v->s[3] = 0;
    v->s[5] = 32767;
    v->s[6] = 2;
    v->s[7] = 0;
    v->env_out = 0;                                     /* a new slice fades in over one block */
    if (!s)
        return;
    j = slc_note_slice(p, v->note, slc_count(s, div));
    slc_bounds(s, div, j, &a, &b, &st);
    if (b <= a || (rev && !slc_rb(t, v)))
        return;
    v->s[4] = (int32_t)(src | (st >> 24) << 2 | rev << 6 | j << 8 | div << 16);
    v->s[6] = 0;
    if (rev) {
        v->ph[0] = b;
        v->ph[2] = a;
        v->s[0] = 0x7FFFFFFF;                           /* no window yet */
    } else {
        v->ph[0] = a;
        v->ph[2] = b;
        v->s[0] = (int16_t)(st & 0xFFFFu);
        v->s[1] = (int32_t)((st >> 16) & 127u);
    }
}

/* the reverse window [ws, ws + n): decoded from the nearest checkpoint at or below ws (a grid point,
 * or a = the slice start with its state sa) */
static void slc_fill(const slc_src_t *s, int16_t *rb, uint32_t ws, uint32_t n, uint32_t a, uint32_t sa)
{
    slc_dec_t d;
    uint32_t k = (ws << SLC_GRID_LOG2) / s->len, i;
    while (k + 1u < SLC_GRID && slc_gpos(s, k + 1u) <= ws)
        k++;
    if (a <= ws && a > slc_gpos(s, k))
        slc_dec_at(&d, a, sa);
    else
        slc_dec_at(&d, slc_gpos(s, k), s->grid[k]);
    while (d.pos < ws)
        slc_dec_next(s, &d);
    for (i = 0; i < n; i++)
        rb[i] = (int16_t)slc_dec_next(s, &d);
}

/* LOOP: back to the slice's start; its end again too (MAN: a JOIN / move meanwhile changed it). Out of line: once
 * per loop, it keeps slc_fwd inlined in slice_render */
static __attribute__((noinline)) void slc_loop_fwd(const slc_src_t *s, voice_t *v, slc_dec_t *d)
{
    uint32_t a, b, st;
    slc_bounds_v(s, v, &a, &b, &st);
    slc_dec_at(d, a, st);
    v->ph[2] = b;
}
/* the next source sample into *x; 0 = the slice ended */
static inline int slc_fwd(const slc_src_t *s, voice_t *v, slc_dec_t *d, int loop, int32_t *x)
{
    if (d->pos >= v->ph[2]) {
        if (!loop)
            return 0;
        slc_loop_fwd(s, v, d);
    }
    *x = slc_dec_next(s, d);
    return 1;
}
static inline int slc_rev(const slc_src_t *s, voice_t *v, int16_t *rb, int loop, int32_t *x)
{
    uint32_t q = v->ph[0], ws = (uint32_t)v->s[0], a, b, st;
    if (q <= v->ph[2]) {
        if (!loop)
            return 0;
        slc_bounds_v(s, v, &a, &b, &st);
        q = b;
        v->ph[2] = a;                                   /* (MAN: a JOIN / move meanwhile changed the start too) */
    }
    q--;
    if (q < ws || q >= ws + SLC_RB) {
        slc_bounds_v(s, v, &a, &b, &st);
        if (q < a)                                      /* MAN: the start moved past this voice: it ends */
            return 0;
        ws = q + 1u >= a + SLC_RB ? q + 1u - SLC_RB : a;
        slc_fill(s, rb, ws, q + 1u - ws, a, st);
        v->s[0] = (int32_t)ws;
    }
    *x = rb[q - ws];
    v->ph[0] = q;
    return 1;
}

/* the ADSR (ONE: no release), DECAY, the fade at the slice end */
static int32_t slice_amp(track_t *t, voice_t *v, int32_t adsr)
{
    const int16_t *p = t->p;
    if (p[P_E4] == SLC_ONE && v->stage == 3u)
        v->stage = 2;                                   /* ONE: the slice plays to its end */
    if (p[P_E6] < 127) {
        v->s[5] -= mulq16(v->s[5], ENV_EXP[p[P_E6] & 127]);
        if (v->s[5] < 8)
            v->s[6] = 2;                                /* faded out: the voice ends */
    }
    return v->s[6] == 3 ? 0 : mulq15(adsr, v->s[5]);
}

static void slice_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    const int16_t *p = t->p;
    uint32_t pk = (uint32_t)v->s[4], i, frac = v->ph[1], rev = (pk >> 6) & 1u, stepq, rem;
    int32_t lp = 4000 + ((clamp((p[P_E7] << 8) + m->cutoff, 0, 127 << 8) * 28767) >> 15), x;
    int loop = p[P_E4] == SLC_LOOP && v->gate;
    const slc_src_t *s = v->s[6] == 2 ? 0 : slc_get(pk & 3u);
    int16_t *rb = slc_rb(t, v);
    slc_dec_t d;
    if (!s) {                                           /* ended, or its slot was erased */
        v->active = 0;
        return;
    }
    stepq = (pow2_q16(clamp(m->pitch16 - v->pitch_cur + p[P_E3] * 16, -1536, 576)) >> 8) * (s->rate >> 8);
    d.pos = v->ph[0];
    d.pred = v->s[0];
    d.idx = v->s[1];
    d.seg = (pk >> 2) & 15u;
    if (!v->s[6]) {                                     /* prime the interpolator */
        if (!(rev ? slc_rev(s, v, rb, 0, &x) : slc_fwd(s, v, &d, 0, &x))) {
            v->active = 0;
            return;
        }
        v->s[3] = x;
        v->s[6] = 1;
    }
    rem = rev ? v->ph[0] - v->ph[2] : v->ph[2] - d.pos;
    if (v->s[6] == 1 && !loop && rem < ((stepq * 3u * n) >> 16))
        v->s[6] = 3;                                    /* the next block fades to 0 (slice_amp) */
    for (i = 0; i < n; i++) {
        int32_t y;
        frac += stepq;
        while (frac >= 65536u) {
            frac -= 65536u;
            v->s[2] = v->s[3];
            if (!(rev ? slc_rev(s, v, rb, loop, &x) : slc_fwd(s, v, &d, loop, &x))) {
                v->s[6] = 2;                            /* the slice ended */
                v->s[3] = 0;
                break;
            }
            v->s[3] = x;
        }
        y = v->s[2] + (((v->s[3] - v->s[2]) * (int32_t)(frac >> 1)) >> 15);
        v->s[7] += mulq15(y - v->s[7], lp);             /* TONE */
        out[i] += voice_amp(v->s[7], m, i) << 1;
        if (v->s[6] == 2)
            break;
    }
    v->ph[1] = frac;
    if (!rev) {
        v->ph[0] = d.pos;
        v->s[0] = d.pred;
        v->s[1] = d.idx;
        v->s[4] = (int32_t)((pk & ~(15u << 2)) | d.seg << 2);
    }
}

/* two factory sounds, both on BREAK (always there; an empty slot plays it too). PATTERNS (engines.c): CHOP suggests
 * 9 CHOP (16 slices re-ordered), STUTTER 10 STUTTER (8 slices, repeats); 11 SLICES plays 0..15 in order */
static const preset_t SLICE_PRESETS[] = {
    {"CHOP", {0, 2, 0, 0, SLC_ONE, 0, 127, 127}, {0, 127, 127, 30}, 0, 0, FX(0, 0, 0, 12), PAT(9)},
    {"STUTTER", {0, 1, 0, 0, SLC_GATE, 0, 90, 110}, {0, 127, 127, 12}, 0, 0, FX(10, 0, 20, 10), PAT(10)},
};

/* the lowest key is slice 0 (C4 + ROOT), no scale */
static int32_t slice_keys(const track_t *t, uint32_t k)
{
    return clamp(SLC_BASE + t->p[P_ROOT] + 12 * song.octave + (int32_t)k, 0, 127);
}

static const engine_t ENG_SLICE = {
    .name = "SLICE",
    .page_title = {"SLCE", "PLAY"},
    .edit = {
        {"SRC", F_ENUM, 0, 3, 0, N_SLC_SRC, 0},
        {"DIV", F_ENUM, 0, 5, 2, N_SLC_DIV, 0},
        {"START", F_INT, 0, 31, 0, 0, 0},
        {"PTCH", F_SEMI, -24, 24, 0, 0, 0},
        {"MODE", F_ENUM, 0, 2, 0, N_SLC_MODE, 0},
        {"REV", F_ENUM, 0, 1, 0, N_SLC_REV, 0},
        {"DCAY", F_TIME, 0, 127, 127, 0, 0},
        {"TONE", F_INT, 0, 127, 127, 0, 0},
    },
    .presets = SLICE_PRESETS,
    .npresets = NELEM(SLICE_PRESETS),
    .note_on = slice_note_on,
    .render = slice_render,
    .knob = {P_E0, P_E1, P_E3, P_E6},
    .sampled = 1,
    .keys = slice_keys,
    .amp = slice_amp,
};
