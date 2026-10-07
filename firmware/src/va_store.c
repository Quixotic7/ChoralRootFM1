/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* The VA patch store (eng_va.c, docs/VA.md): one packed patch (VA_BLOB bytes) per user slot, slot k <-> patch k,
 * in one storage.c object (OBJ_VASTORE = OBJ_PROJECT0: A/B at 0x97000 / 0x98000, sectors ChoralRoot's projects
 * never use), mirrored in the pool so loading a sound never reads flash. The commit protocol is storage.c's. The
 * payload (16 + 32 x 110 = 3536 bytes) ends 3792 bytes into its sector: the tail stays erased, as fm6_bank.c's.
 * Store version 3 holds version-3 blobs (VA_BLOB 110: FTYPE in one value); a version-2 store (the same size, version-2
 * blobs: TYPE and MORPH) is converted in place at boot (va_store_v2: FTYPE = TYPE x 32 + MORPH, the sound as it was),
 * a version-1 store (104-byte blobs, 3344 bytes) too (va_store_v1: every patch kept, the new values at their init);
 * either is written as version 3 by the next save.
 *
 * upreset.c calls it: up_put saving a VA sound stores the patch of the part it came from (va_store_saved), erasing
 * a slot (up_put(k, 0)) or saving another engine's sound over it clears patch k; up_values loading a VA record marks
 * the slot (eng_va.c va_user_pending) and the load's fm6_track_loaded -> va_track_loaded reads patch k back.
 * A slot whose patch is missing (an older store, a full erase) loads the init patch with the record's macros.
 * Included by upreset.c after its record types (ChoralRoot only: FELUCCA_VA). */
#if FELUCCA_FLASH
#define OBJ_VASTORE OBJ_PROJECT0
#endif
#define VA_STORE_MAGIC 0x31534156u               /* "VAS1" */
typedef struct {
    uint32_t magic;
    uint16_t ver, nslot;                         /* 3, UP_SLOTS */
    uint32_t used;                               /* bit k: slot k holds a patch */
    uint16_t blob, rsv;                          /* VA_BLOB */
    uint8_t p[UP_SLOTS][VA_BLOB];
} va_store_t;
_Static_assert(sizeof(va_store_t) == 16u + UP_SLOTS * VA_BLOB && sizeof(va_store_t) + 256u <= 4096u - 256u,
               "VA store layout: header + 32 patches, the sector's tail erased");
static va_store_t va_store __attribute__((section(".pool")));

static int va_store_valid(const va_store_t *s)
{
    uint32_t k;
    if (s->magic != VA_STORE_MAGIC || s->ver != 3u || s->nslot != UP_SLOTS || s->blob != VA_BLOB)
        return 0;
    for (k = 0; k < UP_SLOTS; k++)
        if (((s->used >> k) & 1u) && !va_blob_ok(s->p[k]))
            return 0;
    return 1;
}

#if defined(CR_TRACE) && CR_TRACE
static uint16_t va_crc16(const uint8_t *b)      /* the trace's "patch crc": storage.c's CRC-32, low 16 bits */
{
    return (uint16_t)st_crc32(b, VA_BLOB);
}
#endif

/* eng_va.c va_store_read: slot k's blob, 0 = there is one */
static int va_store_get(uint32_t k, uint8_t *b)
{
    if (k >= UP_SLOTS || va_store.magic != VA_STORE_MAGIC || !((va_store.used >> k) & 1u))
        return 1;
    memcpy(b, va_store.p[k], VA_BLOB);
#if defined(CR_TRACE) && CR_TRACE
    printf("va: load slot %u patch crc %04x\n", (unsigned)k + 1u, (unsigned)va_crc16(b));
#endif
    return 0;
}

/* a version-1 store just loaded into the mirror (n bytes): converted in place to version 3, the last slot first (a
 * slot's version-2 place starts at or after its version-1 one, so no slot is overwritten before it is read); a slot
 * whose blob is not valid is dropped. 1 = it was one */
static int va_store_v1(int n)
{
    uint8_t *raw = (uint8_t *)&va_store, old[VA_BLOB1];
    int8_t p[VA_NP];
    uint32_t k;
    if (n != (int)(16u + UP_SLOTS * VA_BLOB1) || va_store.magic != VA_STORE_MAGIC || va_store.ver != 1u ||
        va_store.nslot != UP_SLOTS || va_store.blob != VA_BLOB1)
        return 0;
    for (k = UP_SLOTS; k-- > 0;) {
        memcpy(old, raw + 16u + k * VA_BLOB1, VA_BLOB1);
        if (((va_store.used >> k) & 1u) && old[1] == 1u && va_unpack(old, p))
            va_pack(p, va_store.p[k]);
        else {
            va_store.used &= ~(1u << k);
            memset(va_store.p[k], 0, VA_BLOB);
        }
    }
    va_store.ver = 3;
    va_store.blob = VA_BLOB;
#if defined(CR_TRACE) && CR_TRACE
    printf("va: store version 1 imported (%u patches)\n", (unsigned)__builtin_popcount(va_store.used));
#endif
    return 1;
}

/* a version-2 store just loaded into the mirror (n bytes, the same layout): each patch converted in place to a
 * version-3 blob (eng_va.c va_unpack: FTYPE = TYPE x 32 + MORPH); a slot whose blob is not valid is dropped.
 * 1 = it was one */
static int va_store_v2(int n)
{
    int8_t p[VA_NP];
    uint32_t k;
    if (n != (int)sizeof va_store || va_store.magic != VA_STORE_MAGIC || va_store.ver != 2u ||
        va_store.nslot != UP_SLOTS || va_store.blob != VA_BLOB)
        return 0;
    for (k = 0; k < UP_SLOTS; k++) {
        if (((va_store.used >> k) & 1u) && va_unpack(va_store.p[k], p))
            va_pack(p, va_store.p[k]);
        else {
            va_store.used &= ~(1u << k);
            memset(va_store.p[k], 0, VA_BLOB);
        }
    }
    va_store.ver = 3;
#if defined(CR_TRACE) && CR_TRACE
    printf("va: store version 2 imported (%u patches)\n", (unsigned)__builtin_popcount(va_store.used));
#endif
    return 1;
}

static void va_store_boot(void)                  /* persist_boot (upreset.c up_boot) */
{
    int n = -1;
#if FELUCCA_FLASH
    n = flash_ok ? st_load(OBJ_VASTORE, &va_store, sizeof va_store) : -1;
#endif
    if (n > 0 && n != (int)sizeof va_store && va_store_v1(n))
        n = (int)sizeof va_store;
    else if (n == (int)sizeof va_store)
        va_store_v2(n);
    if (n != (int)sizeof va_store || !va_store_valid(&va_store))
        memset(&va_store, 0, sizeof va_store);
    va_store_read = va_store_get;
}

/* slot k = blob b (0: cleared), then the store to flash: 0 ok (or nothing to change), 2 flash error (the mirror kept
 * as it was) */
static int va_store_put(uint32_t k, const uint8_t *b)
{
    uint8_t old[VA_BLOB];
    uint32_t used = va_store.used, magic = va_store.magic;
    if (k >= UP_SLOTS)
        return 1;
    if (!va_store_valid(&va_store))
        memset(&va_store, 0, sizeof va_store);
    if (!b && !((va_store.used >> k) & 1u))
        return 0;                                /* (nothing stored: no flash write) */
    if (b && ((va_store.used >> k) & 1u) && !memcmp(va_store.p[k], b, VA_BLOB))
        return 0;
    memcpy(old, va_store.p[k], VA_BLOB);
    va_store.magic = VA_STORE_MAGIC;
    va_store.ver = 3;
    va_store.nslot = UP_SLOTS;
    va_store.blob = VA_BLOB;
    if (b) {
        memcpy(va_store.p[k], b, VA_BLOB);
        va_store.used |= 1u << k;
    } else {
        memset(va_store.p[k], 0, VA_BLOB);
        va_store.used &= ~(1u << k);
    }
    va_store_read = va_store_get;
#if FELUCCA_FLASH
    if (flash_ok && st_save(OBJ_VASTORE, &va_store, sizeof va_store)) {
        memcpy(va_store.p[k], old, VA_BLOB);
        va_store.used = used;
        va_store.magic = magic;
        return 2;
    }
#else
    (void)used;
    (void)magic;
#endif
    return 0;
}

/* up_put stored record r in slot k: a VA sound -> the patch of the part it was saved from, another engine's -> patch k
 * cleared. The part: a VA part (eng_req) whose values are the record's (cr_ui.c cu_save_commit and up_store copy
 * them; P_VOICE aside: a bass is saved MONO), the selected part first */
static void va_store_saved(uint32_t k, const up_rec_t *r)
{
    uint8_t b[VA_BLOB];
    uint32_t n, tr, i;
    if (!r || r->engine != ENGI_VA) {
        va_store_put(k, 0);
        return;
    }
    for (n = 0; n < VA_NPART; n++) {
        const track_t *t;
        tr = n ? (song.sel ? 0u : 1u) : (song.sel < VA_NPART ? song.sel : 0u);
        t = &trk[tr];
        if (t->eng_req != ENGI_VA)
            continue;
        for (i = 0; i < P_COUNT && i < r->np; i++)
            if (i != P_VOICE && up_value(r, i) != (int16_t)clamp(t->p[i], -64, 127))
                break;
        if (i < P_COUNT && i < r->np)
            continue;
        va_blob_get(t, b);
        va_store_put(k, b);
#if defined(CR_TRACE) && CR_TRACE
        printf("va: save slot %u part %u patch crc %04x\n", (unsigned)k + 1u, (unsigned)tr, (unsigned)va_crc16(b));
#endif
        return;
    }
    va_store_put(k, 0);                          /* (no part holds it: the slot loads the init patch + its macros) */
}

/* up_values loads record r: a VA record marks its slot for va_track_loaded */
static void va_store_loading(const up_rec_t *r)
{
    uint32_t b;
    va_user_pending = 0;
    if (r->engine != ENGI_VA)
        return;
    for (b = 0; b < UP_SLOTS / UP_PER_BANK; b++)
        if (r >= up_bank[b].r && r < up_bank[b].r + UP_PER_BANK)
            va_user_pending = (uint8_t)(b * UP_PER_BANK + (uint32_t)(r - up_bank[b].r) + 1u);
}
