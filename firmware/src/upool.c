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
 * MIGRATION (upool_import): the first power-on of this firmware, when the pool holds no bank and the legacy objects
 * have records, copies legacy slot k (0..31: the record of OBJ_UPRESET0 + k / 16 and the patch k of the store of that
 * record's engine: the VA store, the FM6 store's halves, the CZ-1 store's halves, the QUAD store) byte for byte into
 * pool slot k, four bank writes. The legacy objects are only read, never written again: going back to 0.21 finds its 32
 * slots as they were at the update. Later, only on request (the device manager, cr_backup.c CRB_IMPORT): the same
 * again over slots 0..31 (slots 32.. stay). Included by upreset.c (UP_POOL). */
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

/* ---------------------------------------------------------------- the legacy objects (0.21's, read only) --- */
static struct {
    uint32_t rec[2];                                  /* the payload of OBJ_UPRESET0 + b's current copy, 0: none */
    uint32_t va, va_used, fm6[2], fm6_used[2], cz[2], cz_used[2], quad, quad_used;
    uint8_t va_ver;
} upl;
static uint32_t upl_at(uint32_t obj, int copy) { return st_sector(obj, (uint32_t)copy) + ST_PAYLOAD_OFF; }
/* where each legacy object's current copy is and whether it is valid (st_current leaves its payload in st_buf, where
 * the stores' own checks read it): 1 when a legacy bank holds a record */
static int upl_scan(void)
{
    st_hdr_t h;
    uint32_t b, k;
    int c, any = 0;
    memset(&upl, 0, sizeof upl);
    if (!flash_ok || ST_BLOCKED())
        return 0;
    for (b = 0; b < 2u; b++) {
        const up_bank_t *bk = (const up_bank_t *)st_buf;
        if ((c = st_current(OBJ_UPRESET0 + b, &h)) < 0 || h.len != sizeof(up_bank_t) || bk->magic != UP_BANK_MAGIC ||
            bk->rsize != sizeof(up_rec_t) || bk->nslot != UP_PER_BANK)
            continue;
        upl.rec[b] = upl_at(OBJ_UPRESET0 + b, c) + 8u;
        for (k = 0; k < UP_PER_BANK; k++)
            any |= up_valid(&bk->r[k]);
    }
    if ((c = st_current(OBJ_VASTORE, &h)) >= 0) {
        const va_store_t *s = (const va_store_t *)st_buf;
        if (h.len == sizeof(va_store_t) && s->magic == VA_STORE_MAGIC && s->nslot == UP_LEGACY &&
            ((s->ver == 3u && va_store_valid(s)) || (s->ver == 2u && s->blob == VA_BLOB))) {
            upl.va_ver = (uint8_t)s->ver;
        } else if (h.len == 16u + UP_LEGACY * VA_BLOB1 && s->magic == VA_STORE_MAGIC && s->ver == 1u &&
                   s->nslot == UP_LEGACY && s->blob == VA_BLOB1) {
            upl.va_ver = 1;
        }
        if (upl.va_ver) {
            upl.va = upl_at(OBJ_VASTORE, c) + 16u;
            upl.va_used = s->used;
        }
    }
    for (b = 0; b < 2u; b++) {
        if ((c = st_current(OBJ_FM6STORE0 + b, &h)) >= 0 && h.len == sizeof(fm6u_t) &&
            fm6u_valid((const fm6u_t *)st_buf, b)) {
            upl.fm6[b] = upl_at(OBJ_FM6STORE0 + b, c) + 16u;
            upl.fm6_used[b] = ((const fm6u_t *)st_buf)->used;
        }
        if ((c = st_current(czu_obj(b), &h)) >= 0 && h.len == sizeof(czu_t) && czu_valid((const czu_t *)st_buf, b)) {
            upl.cz[b] = upl_at(czu_obj(b), c) + 16u;
            upl.cz_used[b] = ((const czu_t *)st_buf)->used;
        }
    }
    if ((c = st_current(OBJ_QUADSTORE, &h)) >= 0 && h.len == sizeof(quad_store_t) &&
        quad_store_valid((const quad_store_t *)st_buf)) {
        upl.quad = upl_at(OBJ_QUADSTORE, c) + 16u;
        upl.quad_used = ((const quad_store_t *)st_buf)->used;
    }
    return any;
}
/* legacy slot k's patch for a record of engine e -> slot + UPP_DATA (st_read only: called inside sp_commit) */
static void upl_patch(uint32_t k, uint32_t e, uint8_t *slot)
{
    uint8_t *d = slot + UPP_DATA;
    uint32_t h = k / 16u, i = k % 16u, n = 0;
    if (e == ENGI_VA && upl.va && ((upl.va_used >> k) & 1u)) {
        if (upl.va_ver == 3u) {
            if (!st_read(upl.va + k * VA_BLOB, d, VA_BLOB))
                n = VA_BLOB;
        } else {                                       /* versions 1 / 2: converted as va_store_v1 / _v2 did */
            uint8_t old[VA_BLOB];
            int8_t p[VA_NP];
            uint32_t sz = upl.va_ver == 1u ? VA_BLOB1 : VA_BLOB;
            memset(old, 0, sizeof old);
            if (!st_read(upl.va + k * sz, old, sz) && (upl.va_ver == 2u || old[1] == 1u) && va_unpack(old, p)) {
                va_pack(p, d);
                n = VA_BLOB;
            }
        }
    } else if (e == ENGI_FM6 && upl.fm6[h] && ((upl.fm6_used[h] >> i) & 1u)) {
        if (!st_read(upl.fm6[h] + i * FM6_BLOB, d, FM6_BLOB))
            n = FM6_BLOB;
    } else if (e == ENGI_CZ && upl.cz[h] && ((upl.cz_used[h] >> i) & 1u)) {
        if (!st_read(upl.cz[h] + i * CZ_BYTES, d, CZ_BYTES))
            n = CZ_BYTES;
    } else if (e == ENGI_QUAD && upl.quad && ((upl.quad_used >> k) & 1u)) {
        if (!st_read(upl.quad + k * QUAD_BLOB_V3, d, QUAD_BLOB_V3))   /* (a version 1..3 blob: converted on load) */
            n = QUAD_BLOB_V3;
    }
    if (n) {
        slot[UPP_HDR] = (uint8_t)(e + 1u);
        slot[UPP_HDR + 1u] = (uint8_t)n;
    } else {
        memset(d, 0, UPP_MAX);
    }
}
static uint32_t upl_bank;                          /* the pool bank upl_fill fills */
static int upl_fill(uint32_t j, uint8_t *slot)     /* sp_commit: pool slot upl_bank * 10 + j from legacy slot k */
{
    uint32_t k = upl_bank * SP_PER + j;
    if (k >= UP_LEGACY)
        return 0;                                  /* (slots 32..: as they are) */
    memset(slot, 0, SP_SLOT);
    if (upl.rec[k / UP_PER_BANK] && st_read(upl.rec[k / UP_PER_BANK] + (k % UP_PER_BANK) * sizeof(up_rec_t), slot,
                                            sizeof(up_rec_t)))
        return -1;
    if (up_valid((const up_rec_t *)slot))
        upl_patch(k, slot[2], slot);
    return 0;
}
static void upool_table(void)                      /* upn[] from flash */
{
    uint32_t k;
    for (k = 0; k < UP_SLOTS; k++)
        upn_note(k, flash_ok ? up_rec(k) : 0);
    up_gen++;
}
/* legacy slots 1..32 -> pool slots 1..32 (see the top): the number of records copied, -1 nothing legacy, < -1 a flash
 * error (the pool as it was, bank by bank: a bank written stays written) */
static int upool_import(void)
{
    uint32_t b, k;
    int rc = 0, n = 0;
    if (!upl_scan())
        return -1;
    for (b = 0; b * SP_PER < UP_LEGACY && !rc; b++) {
        upl_bank = b;
        rc = sp_commit(b, upl_fill);
    }
    upool_table();
    for (k = 0; k < UP_LEGACY; k++)
        n += up_used(k);
    return rc ? (rc < -1 ? rc : -2) : n;
}
static int8_t upool_boot_rc;                       /* power-on: 0 the pool as found, n > 0 n records migrated, < 0 */
static void upool_boot(void)                       /* persist_boot: the table; the first time, the migration */
{
    uint32_t b;
    int any = 0;
    memset(upn, 0, sizeof upn);
    cbc.ok = 0;
    upool_boot_rc = 0;
    if (!flash_ok || ST_BLOCKED())
        return;
    sp_scan();
    for (b = 0; b < SP_NBANK; b++)
        any |= sp_used(b);
    if (!any) {
        int rc = upool_import();
        upool_boot_rc = (int8_t)(rc == -1 ? 0 : rc > 100 ? 100 : rc);
#if defined(CR_TRACE) && CR_TRACE
        printf("pool: migration %d\n", rc);
#endif
    }
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
