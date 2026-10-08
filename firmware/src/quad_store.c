/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* The QUAD patch store (eng_quad.c, docs/QUAD.md): one packed patch (QUAD_BLOB = 80 bytes, 'Q' version 1) per user
 * slot, slot k <-> patch k, in one storage.c object (OBJ_QUADSTORE: A/B at 0xB2000 / 0xB3000, the user sample slot
 * 2's flash, unused since the all-synth change), mirrored in the pool so loading a sound never reads flash. The commit
 * protocol is storage.c's. The payload: a 16-byte header ("QUDS", version 1, 32 slots, the used mask, the blob size)
 * and 32 x 80 bytes = 2576 bytes; the backup's object 22 (cr_backup.c).
 *
 * upreset.c calls it as it calls va_store.c: up_put saving a QUAD sound stores the patch of the part it came from
 * (quad_store_saved), erasing a slot (up_put(k, 0)) or saving another engine's sound over it clears patch k;
 * up_values loading a QUAD record marks the slot (eng_quad.c quad_user_pending) and the load's fm6_track_loaded ->
 * quad_track_loaded reads patch k back. A slot whose patch is missing (a full erase, a restored bank without the
 * store) loads the init patch with the record's macros (or its preset's patch when the macros are a preset's).
 * Included by upreset.c after the other stores (ChoralRoot only: FELUCCA_QUAD). */
#define QUAD_STORE_MAGIC 0x53445551u             /* "QUDS" */
#define QUAD_STORE_VER 1u
typedef struct {
    uint32_t magic;
    uint16_t ver, nslot;                         /* QUAD_STORE_VER, UP_SLOTS */
    uint32_t used;                               /* bit k: slot k holds a patch */
    uint16_t blob, rsv;                          /* QUAD_BLOB, 0 */
    uint8_t p[UP_SLOTS][QUAD_BLOB];
} quad_store_t;
_Static_assert(sizeof(quad_store_t) == 16u + UP_SLOTS * QUAD_BLOB && sizeof(quad_store_t) == 2576u &&
               sizeof(quad_store_t) <= 4096u - 256u, "QUAD store layout: header + 32 patches in one object");
static quad_store_t quad_store __attribute__((section(".pool")));

static int quad_store_valid(const quad_store_t *s)
{
    uint32_t k;
    if (s->magic != QUAD_STORE_MAGIC || s->ver != QUAD_STORE_VER || s->nslot != UP_SLOTS || s->blob != QUAD_BLOB)
        return 0;
    for (k = 0; k < UP_SLOTS; k++)
        if (((s->used >> k) & 1u) && !quad_blob_ok(s->p[k]))
            return 0;
    return 1;
}

#if defined(CR_TRACE) && CR_TRACE
static uint16_t quad_crc16(const uint8_t *b)    /* the trace's "patch crc": storage.c's CRC-32, low 16 bits */
{
    return (uint16_t)st_crc32(b, QUAD_BLOB);
}
#endif

/* eng_quad.c quad_store_read: slot k's blob, 0 = there is one */
static int quad_store_get(uint32_t k, uint8_t *b)
{
    if (k >= UP_SLOTS || quad_store.magic != QUAD_STORE_MAGIC || !((quad_store.used >> k) & 1u))
        return 1;
    memcpy(b, quad_store.p[k], QUAD_BLOB);
#if defined(CR_TRACE) && CR_TRACE
    printf("quad: load slot %u patch crc %04x\n", (unsigned)k + 1u, (unsigned)quad_crc16(b));
#endif
    return 0;
}

static void quad_store_boot(void)                /* persist_boot (upreset.c up_boot) */
{
    int n = -1;
#if FELUCCA_FLASH
    n = flash_ok ? st_load(OBJ_QUADSTORE, &quad_store, sizeof quad_store) : -1;
#endif
    if (n != (int)sizeof quad_store || !quad_store_valid(&quad_store))
        memset(&quad_store, 0, sizeof quad_store);
    quad_store_read = quad_store_get;
}

/* slot k = blob b (0: cleared), then the store to flash: 0 ok (or nothing to change), 1 bad slot, 2 flash error (the
 * mirror kept as it was) */
static int quad_store_put(uint32_t k, const uint8_t *b)
{
    uint8_t old[QUAD_BLOB];
    uint32_t used = quad_store.used, magic = quad_store.magic;
    if (k >= UP_SLOTS)
        return 1;
    if (!quad_store_valid(&quad_store))
        memset(&quad_store, 0, sizeof quad_store);
    if (!b && !((quad_store.used >> k) & 1u))
        return 0;                                /* (nothing stored: no flash write) */
    if (b && ((quad_store.used >> k) & 1u) && !memcmp(quad_store.p[k], b, QUAD_BLOB))
        return 0;
    memcpy(old, quad_store.p[k], QUAD_BLOB);
    quad_store.magic = QUAD_STORE_MAGIC;
    quad_store.ver = QUAD_STORE_VER;
    quad_store.nslot = UP_SLOTS;
    quad_store.blob = QUAD_BLOB;
    quad_store.rsv = 0;
    if (b) {
        memcpy(quad_store.p[k], b, QUAD_BLOB);
        quad_store.used |= 1u << k;
    } else {
        memset(quad_store.p[k], 0, QUAD_BLOB);
        quad_store.used &= ~(1u << k);
    }
    quad_store_read = quad_store_get;
#if FELUCCA_FLASH
    if (flash_ok && st_save(OBJ_QUADSTORE, &quad_store, sizeof quad_store)) {
        memcpy(quad_store.p[k], old, QUAD_BLOB);
        quad_store.used = used;
        quad_store.magic = magic;
        return 2;
    }
#else
    (void)used;
    (void)magic;
#endif
    return 0;
}

/* up_put stored record r in slot k: a QUAD sound -> the patch of the part it was saved from, another engine's -> patch
 * k cleared. The part: a QUAD part (eng_req) whose values are the record's (P_VOICE aside: a bass is saved MONO), the
 * selected part first (as va_store_saved) */
static void quad_store_saved(uint32_t k, const up_rec_t *r)
{
    uint8_t b[QUAD_BLOB];
    uint32_t n, tr, i;
    if (!r || r->engine != ENGI_QUAD) {
        quad_store_put(k, 0);
        return;
    }
    for (n = 0; n < QUAD_NPART; n++) {
        const track_t *t;
        tr = n ? (song.sel ? 0u : 1u) : (song.sel < QUAD_NPART ? song.sel : 0u);
        t = &trk[tr];
        if (t->eng_req != ENGI_QUAD)
            continue;
        for (i = 0; i < P_COUNT && i < r->np; i++)
            if (i != P_VOICE && up_value(r, i) != (int16_t)clamp(t->p[i], -64, 127))
                break;
        if (i < P_COUNT && i < r->np)
            continue;
        quad_blob_get(t, b);
        quad_store_put(k, b);
#if defined(CR_TRACE) && CR_TRACE
        printf("quad: save slot %u part %u patch crc %04x\n", (unsigned)k + 1u, (unsigned)tr, (unsigned)quad_crc16(b));
#endif
        return;
    }
    quad_store_put(k, 0);                        /* (no part holds it: the slot loads the init patch + its macros) */
}

/* up_values loads record r: a QUAD record marks its slot for quad_track_loaded */
static void quad_store_loading(const up_rec_t *r)
{
    uint32_t b;
    quad_user_pending = 0;
    if (r->engine != ENGI_QUAD)
        return;
    for (b = 0; b < UP_SLOTS / UP_PER_BANK; b++)
        if (r >= up_bank[b].r && r < up_bank[b].r + UP_PER_BANK)
            quad_user_pending = (uint8_t)(b * UP_PER_BANK + (uint32_t)(r - up_bank[b].r) + 1u);
}
