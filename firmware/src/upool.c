/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot 0.22: THE SOUND POOL (docs/PRESETS.md "The pool"; the owner, 2026-10-14: "For preset slots can we have a
 * larger shared pool?"). UP_SLOTS (128) user slots shared by every engine, behind upreset.c's API. A slot is SP_SLOT
 * (384) bytes in storage.c's floating banks (sp_*: 13 banks of 10 slots over 0xB8000..0xC7FFF):
 *   0..191    the user record (up_rec_t, upreset.c's format, unchanged)
 *   192       the engine of the patch that follows + 1 (0: no patch), 193 its length, 194 / 195 0
 *   196..383  the patch: the engine's deep blob (eng_deep_t.blob_get: VA 110, FM6 128, CZ-1 144, FM TONE 80, P5 138)
 * No RAM mirror: upn[] (in the pool section) keeps per slot what browsing needs (name, engine, used, has a pattern,
 * the binding's step 16); up_rec reads a record from flash into one of two cached copies (valid until the next call
 * but one); a patch is read when a sound loads (the main loop, never the audio ISR). A save composes the slot in
 * up_work (record + the patch of the part it came from) and writes it with one sp_write.
 *
 * 0.21's 32 slots (the user banks and the VA / FM6 / CZ-1 / FM TONE stores at 0x97000..0x9EFFF, 0xA0000, 0xB2000,
 * 0xDC000) were copied into slots 1..32 by the 0.22 .. 0.29 firmwares at their first power-on; this firmware never reads
 * them (HISTORY 3y231: a unit still on 0.21 or older goes through 0.29.2 first). Included by upreset.c. */
#define UPP_HDR 192u
#define UPP_DATA 196u
#define UPP_MAX (SP_SLOT - UPP_DATA)                  /* 188 */
_Static_assert(sizeof(up_rec_t) == UPP_HDR && UP_SLOTS <= SP_SLOTS && UP_SLOTS <= 128u,
               "a pool slot: the record, then the patch (slots: 7-bit SysEx numbers)");
#define UPN_USED 1u
#define UPN_PAT 2u
#define UPN_GRID 4u
typedef struct {
    char name[12];
    uint8_t engine, flags, n15, f15;                  /* the record's engine, UPN_*, its step 16 (cr_bank.c's binding) */
} upn_t;
static upn_t upn[UP_SLOTS] __attribute__((section(".pool")));
static up_rec_t up_cache[2];
static uint8_t up_cache_slot[2] = {0xFFu, 0xFFu}, up_cache_ix;
static uint8_t up_work[SP_SLOT] __attribute__((aligned(4)));   /* a slot being saved */

static void upn_note(uint32_t k, const up_rec_t *r)   /* slot k's line of the table from its record */
{
    upn_t *n = &upn[k];
    cbc.ok = 0;                                       /* (cr_bank.c: the pools' order is resolved again) */
    memset(n, 0, sizeof *n);
    if (!r || !up_valid(r))
        return;
    memcpy(n->name, r->name, 12);
    n->engine = r->engine;
    n->flags = (uint8_t)(UPN_USED | (up_pat_empty(r) ? 0u : UPN_PAT) | (up_grid(r) ? UPN_GRID : 0u));
    n->n15 = r->note[15];
    n->f15 = r->flags[15];
}

static up_rec_t *up_rec(uint32_t k)            /* slot k's record (read now; upgraded in the copy, up_migrate) */
{
    up_rec_t *r;
    up_cache_ix ^= 1u;
    r = &up_cache[up_cache_ix];
    up_cache_slot[up_cache_ix] = (uint8_t)(k < UP_SLOTS ? k : 0xFFu);
    if (k >= UP_SLOTS || !flash_ok || sp_read(k, 0, r, sizeof *r) != 0)
        memset(r, 0, sizeof *r);
    up_migrate(r);
    return r;
}
static uint32_t up_slot_of(const up_rec_t *r)  /* the slot r was read from (up_rec), UP_SLOTS: another record */
{
    uint32_t i;
    for (i = 0; i < 2u; i++)
        if (r == &up_cache[i] && up_cache_slot[i] < UP_SLOTS)
            return up_cache_slot[i];
    return UP_SLOTS;
}
static int up_used(uint32_t k) { return k < UP_SLOTS && (upn[k].flags & UPN_USED); }

/* slot k's patch when it is engine e's and n bytes long -> b: 0, else 1 (none) */
static int up_patch_get(uint32_t k, uint32_t e, uint8_t *b, uint32_t n)
{
    uint8_t h[4];
    if (!up_used(k) || n > UPP_MAX || sp_read(k, UPP_HDR, h, 4) != 0 || h[0] != e + 1u ||
        (h[1] != n && !(e == ENGI_QUAD && h[1] == QUAD_BLOB_V3) &&         /* (FM TONE: an older slot's 80-byte version 1..3
                                                                     * blob, the rest zeros; eng_quad.c converts it; GRAIN:
                                                                     * the first build's 20-byte version 1, 3y139) */
         !(FELUCCA_GRAIN && e == ENGI_GRAIN && h[1] == GRAIN_BLOB_V1)))
        return 1;
    memset(b, 0, n);
    return sp_read(k, UPP_DATA, b, h[1]) != 0;
}
static uint32_t up_patch_len(uint32_t e)        /* the length of engine e's patch in a slot, 0: it has none */
{
    const eng_deep_t *d = e < NENGINES ? ENGINES[e]->deep : 0;
    if (e != ENGI_VA && e != ENGI_FM6 && e != ENGI_CZ && e != ENGI_QUAD
#if FELUCCA_PROPHET
        && e != ENGI_PROPHET
#endif
#if FELUCCA_GRAIN
        && e != ENGI_GRAIN
#endif
    )
        return 0;
    return d && d->blob_get && d->blob_size <= UPP_MAX ? d->blob_size : 0u;
}
#if defined(CR_TRACE) && CR_TRACE
static const char *up_patch_tag(uint32_t e, const char **what)   /* the traces' names (as the stores printed them) */
{
    *what = e == ENGI_CZ ? "tone" : "patch";
    return e == ENGI_VA ? "va" : e == ENGI_FM6 ? "fm6" : e == ENGI_CZ ? "cz" : e == ENGI_QUAD ? "quad" :
           e == ENGI_GRAIN ? "grain" : "p5";
}
#endif
/* the loaders' readers (eng_*.c X_store_read): slot k's patch of that engine, 0 = there is one */
static int up_read_engine(uint32_t k, uint8_t *b, uint32_t e)
{
    uint32_t n = up_patch_len(e);
    if (!n || up_patch_get(k, e, b, n))
        return 1;
#if defined(CR_TRACE) && CR_TRACE
    {
        const char *what, *tag = up_patch_tag(e, &what);
        printf("%s: load slot %u %s crc %04x\n", tag, (unsigned)k + 1u, what, (unsigned)(uint16_t)st_crc32(b, n));
    }
#endif
    return 0;
}
static int up_read_va(uint32_t k, uint8_t *b) { return up_read_engine(k, b, ENGI_VA); }
static int up_read_fm6(uint32_t k, uint8_t *b) { return up_read_engine(k, b, ENGI_FM6); }
static int up_read_cz(uint32_t k, uint8_t *b) { return up_read_engine(k, b, ENGI_CZ); }
static int up_read_quad(uint32_t k, uint8_t *b) { return up_read_engine(k, b, ENGI_QUAD); }
#if FELUCCA_PROPHET
static int up_read_p5(uint32_t k, uint8_t *b) { return up_read_engine(k, b, ENGI_PROPHET); }
#endif
#if FELUCCA_GRAIN
static int up_read_grain(uint32_t k, uint8_t *b) { return up_read_engine(k, b, ENGI_GRAIN); }
#endif

/* up_values loads record r: a record of an engine with a patch marks its slot for that engine's track_loaded */
static void up_loading(const up_rec_t *r)
{
    uint32_t k = up_slot_of(r);
    uint8_t pend = (uint8_t)(k < UP_SLOTS ? k + 1u : 0u);
    va_user_pending = r->engine == ENGI_VA ? pend : 0u;
    fm6_user_pending = r->engine == ENGI_FM6 ? pend : 0u;
    cz_user_pending = r->engine == ENGI_CZ ? pend : 0u;
    quad_user_pending = r->engine == ENGI_QUAD ? pend : 0u;
#if FELUCCA_PROPHET
    p5_user_pending = r->engine == ENGI_PROPHET ? pend : 0u;
#endif
#if FELUCCA_GRAIN
    grain_user_pending = r->engine == ENGI_GRAIN ? pend : 0u;
#endif
}

/* the patch of the part record r was saved from -> b: its length, 0: none (another engine, or no part holds the
 * record's values: the slot then loads its engine's init patch / PTCH with the record's macros, as the stores did).
 * The part: one of the engine's (eng_req) whose values are the record's (P_VOICE aside: a bass is saved MONO), the
 * selected part first */
static uint32_t up_patch_of(uint32_t k, const up_rec_t *r, uint8_t *b)
{
    uint32_t e = r ? r->engine : NENGINES, len = r ? up_patch_len(e) : 0u, n, tr, i;
    if (!len)
        return 0;
    for (n = 0; n < 2u; n++) {
        const track_t *t;
        tr = n ? (song.sel ? 0u : 1u) : (song.sel < 2u ? song.sel : 0u);
        t = &trk[tr];
        if (t->eng_req != e)
            continue;
        for (i = 0; i < P_COUNT && i < r->np; i++)   /* (the sends aside too: an FM TONE sound with SENDS stores the
                                                      * part's amounts, cr_ui.c cu_store) */
            if (i != P_VOICE && !(i >= P_CHOR && i <= P_REV) && up_value(r, i) != (int16_t)clamp(t->p[i], -64, 127))
                break;
        if (i < P_COUNT && i < r->np)
            continue;
        ENGINES[e]->deep->blob_get(t, b);
#if defined(CR_TRACE) && CR_TRACE
        {
            const char *what, *tag = up_patch_tag(e, &what);
            printf("%s: save slot %u part %u %s crc %04x\n", tag, (unsigned)k + 1u, (unsigned)tr, what,
                   (unsigned)(uint16_t)st_crc32(b, len));
        }
#else
        (void)k;
#endif
        return len;
    }
    return 0;
}

static void upool_table(void)                      /* upn[] from flash */
{
    uint32_t k;
    for (k = 0; k < UP_SLOTS; k++)
        upn_note(k, flash_ok ? up_rec(k) : 0);
    up_gen++;
}
static void upool_boot(void)                       /* persist_boot: the table */
{
    memset(upn, 0, sizeof upn);
    cbc.ok = 0;
    if (!flash_ok || ST_BLOCKED())
        return;
    sp_scan();
    upool_table();
    va_store_read = up_read_va;
    fm6_store_read = up_read_fm6;
    cz_store_read = up_read_cz;
    quad_store_read = up_read_quad;
#if FELUCCA_GRAIN
    grain_store_read = up_read_grain;
#endif
#if FELUCCA_PROPHET
    p5_store_read = up_read_p5;
#endif
}

/* slot k = record r (0: erased) with the patch of the part it came from (keep: the patch stored now stays, a rename),
 * one bank write: 0 ok, else the flash's error (nothing changed) */
static int upool_put(uint32_t k, const up_rec_t *r, int keep)
{
    uint32_t len;
    int rc;
    if (k >= UP_SLOTS)
        return -1;
    memset(up_work, 0, sizeof up_work);
    if (r) {
        memcpy(up_work, r, sizeof *r);
        if (keep) {
            if (sp_read(k, UPP_HDR, up_work + UPP_HDR, SP_SLOT - UPP_HDR) < 0)
                return -1;
        } else if ((len = up_patch_of(k, r, up_work + UPP_DATA)) != 0) {
            up_work[UPP_HDR] = (uint8_t)(r->engine + 1u);
            up_work[UPP_HDR + 1u] = (uint8_t)len;
        }
    }
    if ((rc = sp_write(k, 0, up_work, SP_SLOT)) != 0)
        return rc;
    upn_note(k, r);
    return 0;
}
