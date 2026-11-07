/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: the favourites (0.22; docs/PRESETS.md "Favourites"): a mark on a SOUND, a bit per sound.
 *
 * What a bit names (cr_ui.c cu_fav_key turns a pool entry into it):
 *   a factory sound      (its engine's index, a stable id): the id is the preset's index in the engine's own table
 *                        (engine_t.presets: the index a user record's binding already stores, cr_bank.c, so it has to
 *                        stay what it is from release to release; nothing was reordered since 0.14), and for P5 the
 *                        program's SEQUENTIAL NUMBER 1..200 (eng_prophet_test.c p5_preset_id), never its place in the
 *                        pool: the shipped list (assets/prophet5-factory/shipped.txt) is trimmed and reordered, the
 *                        marks stay on the programs
 *   a user sound         its slot 0..127 (CRF_T_USER; 0.22, the sound pool: slots 0..31 in section 0x80 as before,
 *                        32..127 in section 0x81, CRF_T_USER2). A record that OVERWRITES a factory preset is that factory sound
 *                        (the same place in the pool): its factory key, so the mark survives Overwrite and Reset
 *   INIT                 nothing: it cannot be a favourite
 * Felucca's favorites.c (a 16 x 32 byte bitmap inside its settings record, ui.c's browser filter) is not compiled into
 * ChoralRoot (ui.c is not part of the unit) and is not used here: its bitmap would have to live in the settings
 * record, which never grows, it has no room for engine 16, and it knows P5's programs only by their place.
 *
 * A sound has a TYPE (0.22, the owner 2026-10-13: "Both, Chord, or Bass"): Both (every sound until it is given
 * another), Chord (a favourite of the chord part only) or Bass (of the bass part only). It says where the favourites
 * FILTER shows the sound once it is marked (cr_ui.c cuf_*: PRESETS / ALGORITHM turn through the part's fitting
 * favourites only while the filter is on); the star shows on every marked sound whatever its type. The type is the
 * SOUND's, kept whether the sound is marked or not (the owner: "favorite/unfavorite them and the type does not
 * change"): taking the mark off and putting it back leaves it. Same keys as the marks. The filter itself, one Off /
 * On, is kept here too.
 *
 * The RAM form: crf_bits, a fixed run of bytes per section (CRF_SEC: 72 bytes), and crf_only[0] / [1], the same shape:
 * the sounds typed Chord / typed Bass (never both; marked or not); crf_filter. 217 bytes (and the filter's two lists
 * of pool places, 88).
 * The flash form, a storage.c object of
 * its own (OBJ_FAVS, the A/B pair 0xB6000 / 0xB7000: a save goes to the other copy, the commit record last, so a torn
 * write leaves the marks as they were), written by crf_flush:
 *   byte 0   the version, CRF_VER (2). Version 1 (0.22's first builds: marks without types) is read as it is: every
 *            mark Both, the filter off; it is rewritten as version 2 by the next change, not by the power-on.
 *            Another version: read as "no marks" (and replaced by the next change)
 *   byte 1   the number of sections that follow
 *   then per section: tag, len, len bytes. Tags:
 *            the engine's index (0..31), 0x80 the user slots: the marks (bit i of byte i / 8 is id i)
 *            that tag | 0x40: the sounds of it typed Chord; that tag | 0x20: those typed Bass (marked or not)
 *            0xFF: the options, one byte, bit 0 the filter on
 *            (the zero bytes at the end are not stored; a section of zeros is not stored)
 * A section this firmware does not know is skipped, bits past a section's size are ignored: a newer firmware's object
 * is read for what is known of it. Empty (no mark anywhere, the filter off): two bytes, {2, 0}. The longest: 262.
 * A version-1 reader (40acb01) meeting this object: "another version" (or, over 96 bytes, one it cannot load), so no
 * marks and no filter, the object left as it is in flash until a mark is changed THERE, which writes a version-1
 * object over it (the types and the marks of this one are then gone; nothing else is touched). A backup's PUT of an
 * object over 96 bytes it refuses (rc 1).
 *
 * When it is written: cr_settings.c crf_poll, as the Options are (the change settled, nothing sounding, no loop
 * playing: a flash erase holds the audio 45 ms), and with the global save and a backup's LIST. Not in SAFE MODE.
 * Main loop only. Needs storage.c (FELUCCA_FLASH with ChoralRoot's object list); without it the marks live in RAM. */
#define CR_FAVS 1
#define CRF_VER 2u
#define CRF_T_USER 0x80u                             /* the section of the user slots 0..31 (and, in RAM, 32..127: crf_off) */
#define CRF_T_USER2 0x81u                            /* 0.22: the section of the pool's slots 32..127 (CRF_T_USER's ids 32..) */
#define CRF_T_P5 16u                                 /* P5 (engines.c: indices are append-only): ids are 1..200 */
#define CRF_T_CHORD 0x40u                            /* tag | this: the section's Chord-only bits */
#define CRF_T_BASS 0x20u                             /* tag | this: its Bass-only bits */
#define CRF_T_OPT 0xFFu                              /* one byte: bit 0 the filter */
#define CRF_PACK_MAX 308u                            /* (0.22: 377264f's reader takes 264 at most: a longer object reads there as
                                                      * no marks, docs/PRESETS.md "Favourites") */
enum { CRF_BOTH, CRF_CHORD, CRF_BASS };              /* a mark's type (crf_type; the SAVE menu's Favorite type) */
static const struct { uint8_t tag, bytes; } CRF_SEC[] = {
    {CRF_T_USER, 4},                                 /* slots 0..31 */
    {CRF_T_USER2, 12},                               /* slots 32..127 (0.22; right after 0x80's: one run of 16 bytes) */
    {7, 2},                                          /* WHEEL: 5 presets */
    {12, 8},                                         /* FM6: 25 */
    {13, 8},                                         /* VA: 25 */
    {14, 16},                                        /* CZ-1: 64 and INIT TONE */
    {15, 8},                                         /* FM TONE (QUAD): 16 */
    {CRF_T_P5, 26},                                  /* P5: Sequential's 1..200 (bit 0 unused) */
};
#define CRF_NSEC ((uint32_t)(sizeof CRF_SEC / sizeof CRF_SEC[0]))
#define CRF_BYTES 84u
typedef char crf_pack_ok[2u + 3u * (2u * 8u + CRF_BYTES) + 3u <= CRF_PACK_MAX && sizeof CRF_SEC == 16u ? 1 : -1];
static uint8_t crf_bits[CRF_BYTES];
/* What 3y42 added to RAM is ONE object, crf_x (240 bytes), on purpose: the target compiler merges the small statics of
 * the image into blocks by size, and four new small ones moved a block's edge between fx.c's mix_r and mix_l, which
 * cost the mix loop an instruction a sample (tests/target_budget.py mix_loop_u: 38 -> 39, over its limit). One
 * object larger than those buffers leaves every block before it as it was */
static struct {
    uint8_t only[2][CRF_BYTES];                      /* [0]: the sounds typed Chord, [1]: typed Bass (marked or not) */
    uint8_t filter;                                  /* the favourites filter: PRESETS turns through favourites only */
    uint8_t last_ver;                                /* power-on: the version of the object read (0: none) */
    uint32_t gen;                                    /* bumped by every change of a mark, a type, the whole set (the
                                                      * filter's lists below) */
    struct { uint32_t gen, upgen; uint8_t e, n, bits[32]; } list[2];   /* cr_ui.c cuf_n: per part, the pool places of
                                                      * its engine that hold a fitting favourite; e: the engine + 1
                                                      * (0: not built) */
} crf_x;
#define crf_only crf_x.only
#define crf_filter crf_x.filter
#define crf_last_ver crf_x.last_ver
#define crf_gen crf_x.gen
static uint8_t crf_dirty;                            /* a mark changed since the last write */
static uint8_t crf_saves;                            /* flash writes done (diagnostics, the emulator) */
static int8_t crf_last_rc;                           /* power-on: 1 read, 0 none, -1 an object not understood */
static uint32_t crf_change_ms;                       /* when the last mark changed (the poll's quiet time) */

static uint32_t crf_off(uint32_t tag, uint32_t *bytes)   /* the section's offset in a bitmap, CRF_BYTES: no such section */
{
    uint32_t k, off = 0;
    for (k = 0; k < CRF_NSEC; off += CRF_SEC[k].bytes, k++)
        if (CRF_SEC[k].tag == tag) {
            *bytes = CRF_SEC[k].bytes;
            if (tag == CRF_T_USER)                   /* (ids 0..127: 0x80's 4 bytes and 0x81's 12, one after the other) */
                *bytes += CRF_SEC[k + 1u].bytes;
            return off;
        }
    return CRF_BYTES;
}
static uint8_t *crf_sec(uint32_t tag, uint32_t *bytes)   /* the section's bits, 0: no such section */
{
    uint32_t off = crf_off(tag, bytes);
    return off < CRF_BYTES ? crf_bits + off : 0;
}
static int crf_has(uint32_t tag, uint32_t id)
{
    uint32_t n = 0;
    const uint8_t *b = crf_sec(tag, &n);
    return b && id < n * 8u && ((b[id / 8u] >> (id % 8u)) & 1u);
}
static void crf_touch(void)
{
    crf_dirty = 1;
    crf_change_ms = fm1_ms;
    crf_gen++;
}
/* a sound's type, marked or not (CRF_BOTH: never given one, or no such sound) */
static uint32_t crf_type(uint32_t tag, uint32_t id)
{
    uint32_t n = 0, off = crf_off(tag, &n);
    if (off >= CRF_BYTES || id >= n * 8u)
        return CRF_BOTH;
    off += id / 8u;
    return (crf_only[0][off] >> (id % 8u)) & 1u ? CRF_CHORD : (crf_only[1][off] >> (id % 8u)) & 1u ? CRF_BASS : CRF_BOTH;
}
/* the sound is a favourite OF THE PART (0 chord, 1 bass): marked, and not the other part's only (the filter) */
static int crf_fits(uint32_t tag, uint32_t id, uint32_t part)
{
    return crf_has(tag, id) && crf_type(tag, id) != (part ? CRF_CHORD : CRF_BASS);
}
/* 1: the mark changed (to be written: crf_dirty); 0: it was so already, or there is no such sound to mark. The mark
 * alone: the sound's type stays what it is, on and off */
static int crf_set(uint32_t tag, uint32_t id, int on)
{
    uint32_t n = 0;
    uint8_t *b = crf_sec(tag, &n), m = (uint8_t)(1u << (id % 8u));
    if (!b || id >= n * 8u || (tag == CRF_T_P5 && (!id || id > 200u)) || crf_has(tag, id) == !!on)
        return 0;
    b[id / 8u] = (uint8_t)(on ? b[id / 8u] | m : b[id / 8u] & ~m);
    crf_touch();
    return 1;
}
/* a sound's type, the mark untouched. 1: it changed; 0: it was so already, or there is no such sound or type */
static int crf_set_type(uint32_t tag, uint32_t id, uint32_t type)
{
    uint32_t n = 0, off = crf_off(tag, &n);
    uint8_t m = (uint8_t)(1u << (id % 8u));
    if (off >= CRF_BYTES || id >= n * 8u || (tag == CRF_T_P5 && (!id || id > 200u)) || type > CRF_BASS ||
        crf_type(tag, id) == type)
        return 0;
    off += id / 8u;
    crf_only[0][off] = (uint8_t)(type == CRF_CHORD ? crf_only[0][off] | m : crf_only[0][off] & ~m);
    crf_only[1][off] = (uint8_t)(type == CRF_BASS ? crf_only[1][off] | m : crf_only[1][off] & ~m);
    crf_touch();
    return 1;
}
static int crf_set_filter(int on)                    /* 1: it changed (to be written, as a mark) */
{
    if (crf_filter == (uint8_t)!!on)
        return 0;
    crf_filter = (uint8_t)!!on;
    crf_touch();
    return 1;
}
static uint32_t crf_count(void)                      /* the marks in all */
{
    uint32_t i, n = 0;
    for (i = 0; i < CRF_BYTES * 8u; i++)
        n += (crf_bits[i / 8u] >> (i % 8u)) & 1u;
    return n;
}
static uint32_t crf_pack(uint8_t *out)               /* the flash form into out (CRF_PACK_MAX); its length */
{
    static const uint8_t OR[3] = {0, CRF_T_CHORD, CRF_T_BASS};
    uint32_t k, off = 0, n = 2, i, w;
    out[0] = CRF_VER;
    out[1] = 0;
    for (k = 0; k < CRF_NSEC; off += CRF_SEC[k].bytes, k++)
        for (w = 0; w < 3u; w++) {
            const uint8_t *src = (w ? crf_only[w - 1u] : crf_bits) + off;
            uint32_t len = CRF_SEC[k].bytes;
            while (len && !src[len - 1u])
                len--;
            if (!len)
                continue;
            out[n++] = (uint8_t)(CRF_SEC[k].tag | OR[w]);
            out[n++] = (uint8_t)len;
            for (i = 0; i < len; i++)
                out[n++] = src[i];
            out[1]++;
        }
    if (crf_filter) {
        out[n++] = CRF_T_OPT;
        out[n++] = 1;
        out[n++] = 1;
        out[1]++;
    }
    return n;
}
static void crf_clear(void)
{
    uint32_t i;
    for (i = 0; i < CRF_BYTES; i++)
        crf_bits[i] = crf_only[0][i] = crf_only[1][i] = 0;
    crf_filter = 0;
    crf_gen++;
}
/* the flash form -> the marks. 0: read; -1: not understood (another version, cut short): no marks */
static int crf_unpack(const uint8_t *in, uint32_t n)
{
    uint32_t k, p = 2, i;
    crf_clear();
    crf_last_ver = 0;
    if (n < 2u || (in[0] != CRF_VER && in[0] != 1u))
        return -1;
    for (k = 0; k < in[1]; k++) {
        uint32_t tag, len, bytes = 0, off, w = 0;
        if (p + 2u > n || p + 2u + in[p + 1u] > n) {
            crf_clear();
            return -1;
        }
        tag = in[p];
        len = in[p + 1u];
        if (in[0] != 1u && tag == CRF_T_OPT) {
            crf_filter = (uint8_t)(len && (in[p + 2u] & 1u));
        } else {
            if (in[0] != 1u && (tag & (CRF_T_CHORD | CRF_T_BASS))) {   /* (version 1 had marks only) */
                w = tag & CRF_T_CHORD ? 1u : 2u;
                tag &= ~(uint32_t)(w == 1u ? CRF_T_CHORD : CRF_T_BASS);
            }
            off = crf_off(tag, &bytes);
            for (i = 0; off < CRF_BYTES && i < len && i < bytes; i++)
                (w ? crf_only[w - 1u] : crf_bits)[off + i] = in[p + 2u + i];
        }
        p += 2u + len;
    }
    {   /* P5: ids 1..200 only (a foreign object's stray bits) */
        uint32_t bytes = 0, off = crf_off(CRF_T_P5, &bytes);
        for (k = 0; k < 3u; k++) {
            uint8_t *b = (k ? crf_only[k - 1u] : crf_bits) + off;
            b[0] &= 0xFEu;
            b[25] &= 0x01u;
        }
    }
    for (i = 0; i < CRF_BYTES; i++) {                /* both types at once (a foreign object): Both */
        uint8_t two = crf_only[0][i] & crf_only[1][i];
        crf_only[0][i] &= (uint8_t)~two;
        crf_only[1][i] &= (uint8_t)~two;
    }
    crf_last_ver = in[0];
    return 0;
}
#define CRF_FLASH (FELUCCA_FLASH && FELUCCA_CZ && FELUCCA_QUAD)   /* (storage.c: OBJ_FAVS is in ChoralRoot's list) */
static void crf_boot(void)                           /* power-on, a restored object: flash -> the marks */
{
    uint8_t b[CRF_PACK_MAX];
    int n = -1;
    crf_dirty = 0;
    crf_last_rc = 0;
#if CRF_FLASH
    n = st_load(OBJ_FAVS, b, sizeof b);
#endif
    if (n <= 0) {                                    /* none (a new unit, SAFE MODE, an empty object), or one too long to be ours */
        (void)crf_unpack(b, 0);
        return;
    }
    crf_last_rc = crf_unpack(b, (uint32_t)n) == 0 ? 1 : -1;
}
/* the marks -> flash when one changed; 0: written or nothing to write, -1: a flash error (still to be written) */
static int crf_flush(void)
{
#if CRF_FLASH
    uint8_t b[CRF_PACK_MAX];
    uint32_t n;
    if (!crf_dirty)
        return 0;
    if (ST_BLOCKED()) {                              /* SAFE MODE: the flash untouched, the marks in RAM */
        crf_dirty = 0;
        return 0;
    }
    n = crf_pack(b);
    if (st_save(OBJ_FAVS, b, n) != 0)
        return -1;
    crf_saves++;
#endif
    crf_dirty = 0;
    return 0;
}
/* upreset.c up_put: slot k was erased, or a record went into an empty slot: the slot's mark is not that sound's */
static void crf_slot_new(uint32_t k)                 /* (.. nor is its type) */
{
    (void)crf_set(CRF_T_USER, k, 0);
    (void)crf_set_type(CRF_T_USER, k, CRF_BOTH);
}
