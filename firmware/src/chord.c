/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* Chord keys (SCL > CHORD: P_CHRD, P_VOIC): one key, MIDI note or arp input plays a chord. Included by seq.c
 * after kb_map; runs in the audio ISR with the rest of the note input.
 *   CHRD OFF; DIA3 / DIA7: the triad / seventh of the track's scale (ROOT, SCALE) built on the key, every tone
 *        in key (a key outside the scale takes the scale note below it, as QNT SNAP does; CHR builds on the
 *        major scale of ROOT); MAJ MIN DOM7 MAJ7 MIN7 SUS4 POW: that shape on the key, whatever the scale.
 *   VOIC CLOSE (root position); OPEN (the second tone an octave up: 1-5-3); INV1 / INV2 (the lowest tone, then
 *        the next, an octave up); +OCT (the root an octave down added; a seventh drops its fifth). At most
 *        CHORD_MAX notes, notes outside 0..127 left out.
 * The key is mapped first (kb_map: QNT SNAP / WHITE, TRN, the octave), the chord is built on the note it gives.
 * MONO / LEGATO / UNISON parts play the chord's root only; a kit (the DRUM engine, SAMPLE PERC, slices: an
 * engine that maps the keys itself) ignores CHRD. Every source keeps the notes it started (kb_chord for a
 * key, mchord for a MIDI note) and its release ends exactly those, so CHRD / VOIC changed while it is held
 * leave nothing hanging. A note several sources hold sounds once and ends with the last of them. */
enum { CH_OFF, CH_DIA3, CH_DIA7, CH_MAJ, CH_MIN, CH_DOM7, CH_MAJ7, CH_MIN7, CH_SUS4, CH_POW };
enum { VC_CLOSE, VC_OPEN, VC_INV1, VC_INV2, VC_BASS };
#define CHORD_MAX 4u
#define MCHORD_N 24u                     /* MIDI notes held as chords at once (more: their root alone) */

static const int8_t CHORD_SHAPE[CH_POW - CH_MAJ + 1][CHORD_MAX] = {
    {0, 4, 7, -1}, {0, 3, 7, -1}, {0, 4, 7, 10}, {0, 4, 7, 11}, {0, 3, 7, 10}, {0, 5, 7, -1}, {0, 7, 12, -1},
};

static uint8_t kb_chord[27][CHORD_MAX], kb_chn[27];   /* the notes key k started (kb_chn 0: none) */
typedef struct { uint8_t id, ch, src, n, note[CHORD_MAX]; } mchord_t;   /* id: track + 1, 0 = free */
static mchord_t mchord[MCHORD_N];
/* the last chord played per track (the CHORD page shows it): its root, notes, and the shape's tones above the
 * root (bit i = i semitones, 0..11) */
static struct { uint8_t root, n, note[CHORD_MAX]; uint16_t mask; uint8_t gen; } chord_last[NTRK];

/* 1 = the track's keys are a kit: no chords */
static int chord_kit(const track_t *t)
{
    const engine_t *e = ENGINES[eng_idx(t->eng_req)];
    return e->oneshot || (e->keys && e->keys(t, 0) >= 0);
}

/* the tones of the chord (semitones above *root, ascending, iv[0] = 0) -> their number; DIA may move *root
 * down onto the scale */
static uint32_t chord_tones(const track_t *t, uint32_t mode, int32_t *root, int32_t *iv)
{
    uint32_t n = 0, i;
    if (mode >= CH_MAJ) {
        for (i = 0; i < CHORD_MAX && CHORD_SHAPE[mode - CH_MAJ][i] >= 0; i++)
            iv[n++] = CHORD_SHAPE[mode - CH_MAJ][i];
        return n;
    }
    {
        uint32_t mask = scale_mask(t), deg[12], c = 0, at = 0, guard = 12;
        int32_t r = t->p[P_ROOT];
        if (mask == 0xFFFu)
            mask = SCALE_MASK[1];                       /* CHR: no key of its own, the major scale of ROOT */
        while (guard-- && !((mask >> (uint32_t)((*root - r + 120) % 12)) & 1u))
            (*root)--;                                  /* outside the scale: the scale note below */
        for (i = 0; i < 12u; i++)
            if ((mask >> i) & 1u) {
                if (i == (uint32_t)((*root - r + 120) % 12))
                    at = c;
                deg[c++] = i;
            }
        for (i = 0; i < (mode == CH_DIA7 ? 4u : 3u); i++) {   /* every other note of the scale */
            uint32_t k = at + 2u * i;
            iv[n++] = (int32_t)deg[k % c] + 12 * (int32_t)(k / c) - (int32_t)deg[at];
        }
        return n;
    }
}

/* the chord on note `root` as track t plays it now -> out (ascending), the number of notes (1..CHORD_MAX);
 * *rp its root, *maskp its tones above the root (bit i: i semitones; 0 = no chord). The root alone when CHRD
 * is OFF, on a kit, or for MONO / LEGATO / UNISON (their chord's root) */
static uint32_t chord_make(const track_t *t, uint32_t root, uint8_t *out, int32_t *rp, uint16_t *maskp)
{
    int32_t iv[CHORD_MAX + 1u], r = (int32_t)root, x;
    uint32_t mode = (uint32_t)t->p[P_CHRD], n, i, j, m = 0;
    uint16_t mask = 0;
    *rp = r;
    *maskp = 0;
    if (!mode || mode > CH_POW || chord_kit(t)) {
        out[0] = (uint8_t)root;
        return 1;
    }
    n = chord_tones(t, mode, &r, iv);
    for (i = 0; i < n; i++)
        mask |= (uint16_t)(1u << (iv[i] % 12));
    *rp = r;
    *maskp = mask;
    if (trk_vmode(t) != V_POLY) {                       /* one voice: the root */
        out[0] = (uint8_t)clamp(r, 0, 127);
        return 1;
    }
    switch (t->p[P_VOIC]) {
    case VC_OPEN:                                       /* 1-5-3(-7): the second tone an octave up */
        if (n >= 3u)
            iv[1] += 12;
        break;
    case VC_INV2:
    case VC_INV1:
        for (j = 0; j < (t->p[P_VOIC] == VC_INV2 ? 2u : 1u); j++) {   /* the lowest tone an octave up */
            x = iv[0] + 12;
            for (i = 1; i < n; i++)
                iv[i - 1u] = iv[i];
            iv[n - 1u] = x;
        }
        break;
    case VC_BASS:                                       /* the root an octave down; a seventh drops its fifth */
        if (n == CHORD_MAX) {
            iv[2] = iv[3];
            n--;
        }
        for (i = n; i > 0; i--)
            iv[i] = iv[i - 1u];
        iv[0] = -12;
        n++;
        break;
    default:
        break;
    }
    for (i = 1; i < n; i++)                             /* ascending */
        for (j = i; j > 0 && iv[j - 1u] > iv[j]; j--) {
            x = iv[j];
            iv[j] = iv[j - 1u];
            iv[j - 1u] = x;
        }
    for (i = 0; i < n && m < CHORD_MAX; i++) {          /* inside 0..127, each note once */
        x = r + iv[i];
        if (x < 0 || x > 127 || (m && out[m - 1u] == (uint8_t)x))
            continue;
        out[m++] = (uint8_t)x;
    }
    if (!m) {
        out[0] = (uint8_t)clamp(r, 0, 127);
        return 1;
    }
    return m;
}

/* chord_make for a note played now: a chord is kept in chord_last (the CHORD page shows it) */
static uint32_t chord_build(track_t *t, uint32_t root, uint8_t *out)
{
    int32_t r;
    uint16_t mask;
    uint32_t m = chord_make(t, root, out, &r, &mask), i, k = trk_index(t);
    if (m > 1u) {
        chord_last[k].root = (uint8_t)clamp(r, 0, 127);
        chord_last[k].mask = mask;
        chord_last[k].n = (uint8_t)m;
        for (i = 0; i < CHORD_MAX; i++)
            chord_last[k].note[i] = i < m ? out[i] : 0u;
        chord_last[k].gen++;
    }
    return m;
}

/* a chord's name from its root and tones (bit i: i semitones above the root): "C", "Cm7", "F#m7b5", "Gsus4",
 * "A5" -> b (12 bytes) */
static void chord_name(char *b, uint32_t root, uint32_t mask)
{
    uint32_t mi = (mask >> 3) & 1u, ma = (mask >> 4) & 1u, b5 = (mask >> 6) & 1u, p5 = (mask >> 7) & 1u;
    uint32_t s5 = (mask >> 8) & 1u, d7 = (mask >> 9) & 1u, m7 = (mask >> 10) & 1u, M7 = (mask >> 11) & 1u;
    const char *q, *x = "";
    str_cpy(b, N_NOTE[root % 12u], 12);
    if (mi && !ma && b5 && !p5)
        q = d7 ? "dim7" : m7 ? "m7b5" : "dim";
    else if (ma && !mi && s5 && !p5)
        q = m7 ? "+7" : M7 ? "+M7" : "aug";
    else if (ma && !mi)
        q = m7 ? "7" : M7 ? "maj7" : d7 ? "6" : "";
    else if (mi)
        q = m7 ? "m7" : M7 ? "mM7" : d7 ? "m6" : "m";
    else if ((mask >> 5) & 1u) {                        /* no third: suspended */
        q = m7 ? "7" : "";
        x = "sus4";
    } else if ((mask >> 2) & 1u) {
        q = m7 ? "7" : "";
        x = "sus2";
    } else
        q = "5";
    str_cpy(b + str_len(b), q, 12 - str_len(b));
    str_cpy(b + str_len(b), x, 12 - str_len(b));
}

/* the MIDI note (ch, src) of track id - 1 that plays a chord, 0 = none */
static mchord_t *mchord_of(uint32_t ch, uint32_t src, uint32_t id)
{
    uint32_t i;
    for (i = 0; i < MCHORD_N; i++)
        if (mchord[i].id == id && mchord[i].ch == ch && mchord[i].src == src)
            return &mchord[i];
    return 0;
}

/* a MIDI-held chord of track id - 1 sounds note */
static int mchord_held(uint32_t id, uint32_t note)
{
    uint32_t i, j;
    for (i = 0; i < MCHORD_N; i++)
        if (mchord[i].id == id)
            for (j = 0; j < mchord[i].n; j++)
                if (mchord[i].note[j] == note)
                    return 1;
    return 0;
}

static void mchord_forget(uint32_t track)               /* panic: the track's MIDI chords are gone */
{
    uint32_t i;
    for (i = 0; i < MCHORD_N; i++)
        if (mchord[i].id == track + 1u)
            mchord[i].id = 0;
}
