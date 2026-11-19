/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: the progressions' store (GitHub issue 64, Ripchord style; cr_prog.h is the contract and the format,
 * docs/PROGRESSIONS.md the prose).
 *
 * One storage.c object, OBJ_PROG (an A/B pair: FM-1 0xB2000 / 0xB3000, SMK-37 0xB3000 / 0xFE000), and its RAM copy,
 * crp_x: the whole object (CRPG_SIZE bytes), so that no lookup touches the flash (a loop may resolve a progression's
 * chord from the audio side, and the flash cannot be read during an erase). The copy is the stored object when
 * crp_valid takes it, else the factory set (crp_factory); the power-on never writes flash (the factory set is written
 * only when the user sends progressions: a backup PUT of CRPG_BACKUP_ID, cr_backup.c).
 *
 * crp_init / crp_reload: main loop only (st_buf is the scratch: the object is checked there, then copied in one run).
 * The lookups (crp_nkeys, crp_count, crp_name, crp_chord) read the RAM copy only and stay inside it whatever its bytes
 * (every offset checked against crp_x.len <= CRPG_SIZE), so a reader that meets a reload half done (a restore, which
 * holds the audio for the erase anyway) gets a wrong chord at worst, never a read outside the copy. */
#include "cr_prog.h"
#define CRPG_FLASH (FELUCCA_FLASH && FELUCCA_CZ && FELUCCA_QUAD)   /* (storage.c: OBJ_PROG is in ChoralRoot's list) */

static struct {
    uint8_t blk[CRPG_SIZE];                          /* the object (stored or the factory set); zeros past len */
    uint16_t len;                                    /* its length (the header's used) */
    uint8_t stored;                                  /* 1: the flash's object, 0: the factory set */
} crp_x __attribute__((aligned(4)));
#if CRPG_FLASH
_Static_assert(CRPG_SIZE == ST_PAYLOAD_MAX && CRPG_DATA == 272u && CRPG_SIZE <= 0xFFFFu,
               "progressions: the object fills at most one sector's payload (st_buf is crp_load's scratch)");
#endif

static uint32_t crp_rd16(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8; }

int crp_valid(const uint8_t *b, uint32_t n)
{
    uint32_t used, p, i, k, ne = 0, ext[CRPG_N][2];
    if (!b || n < CRPG_DATA || n > CRPG_SIZE)
        return 0;
    if (((uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24) != CRPG_MAGIC ||
        b[4] != CRPG_VERSION || b[5] != CRPG_N)
        return 0;
    used = crp_rd16(b + 6);
    if (used != n)
        return 0;
    for (p = 0; p < CRPG_N; p++) {
        const uint8_t *d = b + CRPG_HDR + p * CRPG_DIRENT;
        uint32_t nk = d[CRPG_NAME + 1u], off = crp_rd16(d + CRPG_NAME + 2u), at, z = 0,
                 home = (uint32_t)(b[8u + p / 2u] >> (p & 1u ? 4 : 0)) & 15u;
        if (home > 11u || (!nk && home))             /* the home key: a pitch class, none on an empty slot */
            return 0;
        for (i = 0; i < CRPG_NAME; i++) {            /* printable ASCII, 0-padded */
            if (!d[i])
                z = 1;
            else if (z || d[i] < 0x20u || d[i] > 0x7Eu)
                return 0;
        }
        if (d[CRPG_NAME] > 127u || nk > CRPG_KEYS)
            return 0;
        if (!nk) {                                   /* an empty slot: no data, no name */
            if (off || d[0])
                return 0;
            continue;
        }
        if (off < CRPG_DATA || off >= used)
            return 0;
        for (at = off, k = 0; k < nk; k++) {
            uint32_t m;
            if (at >= used)
                return 0;
            m = b[at++];
            if (m > CRPG_NOTES || m > used - at)
                return 0;
            for (i = 0; i < m; i++)
                if (b[at + i] > 127u || (i && b[at + i] <= b[at + i - 1u]))
                    return 0;
            at += m;
        }
        for (i = 0; i < ne; i++)                     /* the slots' data apart */
            if (off < ext[i][1] && ext[i][0] < at)
                return 0;
        ext[ne][0] = off;
        ext[ne][1] = at;
        ne++;
    }
    return 1;
}

/* ---- the factory set: ChoralRoot's own six, on keys 0..7 from C3, the bass lowest ---- */
#define CRPG_FAC_N 6u
#define CRPG_FAC_KEYS 8u
#define CRPG_FAC_BASE 48u
static const char CRPG_FAC_NAME[CRPG_FAC_N][CRPG_NAME + 1u] = {
    "Pop Four", "Jazz ii-V-I", "Neo Soul", "Gospel Walk", "Minor Epic", "Dorian Days"};
static const uint8_t CRPG_FAC_HOME[CRPG_FAC_N] = {0, 0, 3, 0, 9, 2};   /* C, C, Eb, C, A (minor), D (dorian) */
/* per progression, per key: n, then n notes ascending (the bass 36..47, the upper voices 52..72) */
static const uint8_t CRPG_FAC[] = {
    /* 1 Pop Four: C, G/B, Am, F, C/E, G, Fmaj7, Gsus4 */
    5, 36, 52, 55, 60, 64,   5, 47, 55, 59, 62, 67,   5, 45, 52, 57, 60, 64,   5, 41, 53, 57, 60, 65,
    5, 40, 55, 60, 64, 67,   5, 43, 55, 59, 62, 67,   5, 41, 53, 57, 60, 64,   5, 43, 55, 60, 62, 67,
    /* 2 Jazz ii-V-I: Dm9, G13, Cmaj9, A7b9, Dm9, G7b9, Cmaj7, C6/9 */
    5, 38, 53, 57, 60, 64,   5, 43, 53, 59, 64, 69,   5, 36, 52, 55, 59, 62,   5, 45, 55, 58, 61, 64,
    5, 38, 53, 60, 64, 69,   5, 43, 53, 56, 59, 62,   5, 36, 52, 55, 59, 64,   5, 36, 52, 57, 62, 67,
    /* 3 Neo Soul: Ebmaj9, Fm9, Gm7, Abmaj7, Bb13, Cm11, Abmaj9, Bb7sus4 */
    5, 39, 55, 58, 62, 65,   5, 41, 56, 60, 63, 67,   5, 43, 53, 58, 62, 67,   5, 44, 55, 60, 63, 68,
    5, 46, 56, 60, 62, 67,   5, 36, 55, 58, 63, 65,   5, 44, 60, 63, 67, 70,   5, 46, 56, 58, 63, 65,
    /* 4 Gospel Walk: C, C7/E, F, F#dim7, C/G, A7, Dm7, G7 */
    5, 36, 52, 55, 60, 64,   5, 40, 55, 58, 60, 64,   5, 41, 53, 57, 60, 65,   5, 42, 57, 60, 63, 66,
    5, 43, 55, 60, 64, 67,   5, 45, 55, 61, 64, 69,   5, 38, 53, 57, 60, 65,   5, 43, 53, 59, 62, 67,
    /* 5 Minor Epic: Am, F, C, G, Dm, E7, Am/E, E */
    5, 45, 52, 57, 60, 64,   5, 41, 53, 57, 60, 65,   5, 36, 52, 55, 60, 64,   5, 43, 55, 59, 62, 67,
    5, 38, 53, 57, 62, 65,   5, 40, 56, 59, 62, 64,   5, 40, 57, 60, 64, 69,   5, 40, 56, 59, 64, 68,
    /* 6 Dorian Days: Dm7, Em7, Fmaj7, G7, Am7, Cmaj7, Dm9, G13 */
    5, 38, 53, 57, 60, 65,   5, 40, 55, 59, 62, 67,   5, 41, 53, 57, 60, 64,   5, 43, 53, 59, 62, 67,
    5, 45, 55, 60, 64, 67,   5, 36, 52, 55, 59, 64,   5, 38, 53, 57, 60, 64,   5, 43, 53, 59, 64, 69,
};

uint32_t crp_factory(uint8_t *b)
{
    uint32_t p, k, i, at = CRPG_DATA, src = 0;
    memset(b, 0, CRPG_SIZE);
    b[0] = (uint8_t)CRPG_MAGIC;
    b[1] = (uint8_t)(CRPG_MAGIC >> 8);
    b[2] = (uint8_t)(CRPG_MAGIC >> 16);
    b[3] = (uint8_t)(CRPG_MAGIC >> 24);
    b[4] = CRPG_VERSION;
    b[5] = CRPG_N;
    for (p = 0; p < CRPG_FAC_N; p++) {
        uint8_t *d = b + CRPG_HDR + p * CRPG_DIRENT;
        for (i = 0; i < CRPG_NAME && CRPG_FAC_NAME[p][i]; i++)
            d[i] = (uint8_t)CRPG_FAC_NAME[p][i];
        b[8u + p / 2u] |= (uint8_t)(CRPG_FAC_HOME[p] << (p & 1u ? 4 : 0));
        d[CRPG_NAME] = CRPG_FAC_BASE;
        d[CRPG_NAME + 1u] = CRPG_FAC_KEYS;
        d[CRPG_NAME + 2u] = (uint8_t)at;
        d[CRPG_NAME + 3u] = (uint8_t)(at >> 8);
        for (k = 0; k < CRPG_FAC_KEYS; k++) {
            uint32_t m = 1u + CRPG_FAC[src];
            for (i = 0; i < m; i++)
                b[at++] = CRPG_FAC[src++];
        }
    }
    b[6] = (uint8_t)at;
    b[7] = (uint8_t)(at >> 8);
    return at;
}

/* ---- the RAM copy ---- */
static void crp_load(void)                           /* the stored object if valid, else the factory set */
{
#if CRPG_FLASH
    uint32_t n = 0, i;
    uint8_t stored = 0;
    st_hdr_t h;
    if (!ST_BLOCKED() && st_current(OBJ_PROG, &h) >= 0 && crp_valid(st_buf, h.len)) {   /* (st_buf: its payload) */
        n = h.len;
        stored = 1;
    } else {
        n = crp_factory(st_buf);
    }
    for (i = 0; i < CRPG_SIZE; i++)                  /* (one run, checked first: see the top) */
        crp_x.blk[i] = i < n ? st_buf[i] : 0u;
    crp_x.len = (uint16_t)n;
    crp_x.stored = stored;
#else
    crp_x.len = (uint16_t)crp_factory(crp_x.blk);    /* (no store: the factory set) */
    crp_x.stored = 0;
#endif
}
void crp_init(void) { crp_load(); }
void crp_reload(void) { crp_load(); }

static const uint8_t *crp_ent(unsigned p) { return p < CRPG_N ? crp_x.blk + CRPG_HDR + p * CRPG_DIRENT : 0; }
unsigned crp_nkeys(unsigned p)
{
    const uint8_t *e = crp_ent(p);
    return e && e[CRPG_NAME + 1u] <= CRPG_KEYS ? e[CRPG_NAME + 1u] : 0u;
}
/* key i's record in the copy: its offset (the count byte), 0 none (an empty slot, i past nkeys, a broken copy) */
static uint32_t crp_rec(unsigned p, unsigned i)
{
    const uint8_t *e = crp_ent(p);
    uint32_t at, k, end = crp_x.len;
    if (i >= crp_nkeys(p))
        return 0;
    at = crp_rd16(e + CRPG_NAME + 2u);
    for (k = 0; ; k++) {
        if (at < CRPG_DATA || at >= end || end > CRPG_SIZE)
            return 0;
        if (k == i)
            return at;
        at += 1u + crp_x.blk[at];
    }
}
unsigned crp_count(unsigned p)
{
    unsigned i, n = 0, nk = crp_nkeys(p);
    uint32_t at = nk ? crp_rec(p, 0) : 0u, end = crp_x.len;
    for (i = 0; i < nk && at >= CRPG_DATA && at < end && end <= CRPG_SIZE; i++) {
        n += crp_x.blk[at] != 0u;
        at += 1u + crp_x.blk[at];
    }
    return n;
}
void crp_name(unsigned p, char out[CRPG_NAME + 1])
{
    const uint8_t *e = crp_ent(p);
    uint32_t i;
    for (i = 0; e && crp_nkeys(p) && i < CRPG_NAME && e[i]; i++)
        out[i] = (char)e[i];
    out[i] = 0;
}
unsigned crp_home(unsigned p)
{
    unsigned h;
    if (p >= CRPG_N || !crp_nkeys(p))
        return 0;
    h = (unsigned)(crp_x.blk[8u + p / 2u] >> (p & 1u ? 4 : 0)) & 15u;
    return h < 12u ? h : 0u;
}
unsigned crp_chord(unsigned p, unsigned i, uint8_t notes[CRPG_NOTES])
{
    uint32_t at = crp_rec(p, i), m, k;
    if (!at)
        return 0;
    m = crp_x.blk[at];
    if (m > CRPG_NOTES || at + 1u + m > crp_x.len)
        return 0;
    for (k = 0; k < m; k++)
        notes[k] = crp_x.blk[at + 1u + k];
    return m;
}
