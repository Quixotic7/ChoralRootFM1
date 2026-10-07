/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* The FM6 patch store (eng_fm6.c's blob, docs/FM6.md): one 128-byte blob (the voice and its function settings) per
 * user slot, slot k <-> patch k, as the VA's va_store.c. 32 x 128 bytes do not fit one storage.c object (3840 bytes
 * of payload), so the store is two: slots 1..16 in OBJ_FM6STORE0 = OBJ_PROJECT0 + 1 (A/B at 0x99000 / 0x9A000), 17..32
 * in OBJ_FM6STORE1 = OBJ_PROJECT0 + 2 (0x9B000 / 0x9C000), sectors ChoralRoot's projects never use (the VA's store
 * has OBJ_PROJECT0). Each: a 16-byte header and 16 blobs, 2064 bytes; mirrored in the pool so loading a sound never
 * reads flash; the commit protocol is storage.c's; the backup's objects 10 and 11 (cr_backup.c).
 *
 * upreset.c calls it as it calls va_store.c: up_put saving an FM6 sound stores the blob of the part it came from
 * (fm6u_saved), erasing a slot or saving another engine's sound over it clears blob k; up_values loading an FM6 record
 * marks the slot (eng_fm6.c fm6_user_pending) and the load's fm6_track_loaded reads blob k back. A slot without a
 * blob (saved before the store, a full erase) loads its PTCH slot with Dexed's function settings, a Felucca-numbered
 * B slot moved to today's number (eng_fm6.c). Included by upreset.c after va_store.c (ChoralRoot only: FELUCCA_VA). */
#if FELUCCA_FLASH
#define OBJ_FM6STORE0 (OBJ_PROJECT0 + 1)
#endif
#define FM6U_MAGIC 0x55364D46u                   /* "FM6U" */
#define FM6U_VER 1u
#define FM6U_HALF (UP_SLOTS / 2u)
typedef struct {
    uint32_t magic;
    uint16_t ver, nslot;                         /* FM6U_VER, FM6U_HALF */
    uint16_t first, blob;                        /* the first slot it holds (0, 16), FM6_BLOB */
    uint32_t used;                               /* bit k: slot first + k holds a blob */
    uint8_t b[FM6U_HALF][FM6_BLOB];
} fm6u_t;
_Static_assert(sizeof(fm6u_t) == 16u + FM6U_HALF * FM6_BLOB && sizeof(fm6u_t) + 256u <= 4096u - 256u,
               "FM6 store layout: header + 16 blobs, the sector's tail erased");
static fm6u_t fm6u[2] __attribute__((section(".pool")));

static int fm6u_valid(const fm6u_t *s, uint32_t h)
{
    uint32_t k;
    if (s->magic != FM6U_MAGIC || s->ver != FM6U_VER || s->nslot != FM6U_HALF || s->first != h * FM6U_HALF ||
        s->blob != FM6_BLOB || (s->used >> FM6U_HALF))
        return 0;
    for (k = 0; k < FM6U_HALF; k++)
        if (((s->used >> k) & 1u) && !fm6_blob_ok(s->b[k]))
            return 0;
    return 1;
}

static void fm6u_empty(uint32_t h)
{
    memset(&fm6u[h], 0, sizeof fm6u[h]);
    fm6u[h].magic = FM6U_MAGIC;
    fm6u[h].ver = FM6U_VER;
    fm6u[h].nslot = FM6U_HALF;
    fm6u[h].first = (uint16_t)(h * FM6U_HALF);
    fm6u[h].blob = FM6_BLOB;
}

#if defined(CR_TRACE) && CR_TRACE
static uint16_t fm6u_crc16(const uint8_t *b) { return (uint16_t)st_crc32(b, FM6_BLOB); }   /* the traces' "crc" */
#endif

/* eng_fm6.c fm6_store_read: slot k's blob, 0 = there is one */
static int fm6u_get(uint32_t k, uint8_t *b)
{
    const fm6u_t *s = &fm6u[(k / FM6U_HALF) & 1u];
    if (k >= UP_SLOTS || s->magic != FM6U_MAGIC || !((s->used >> (k % FM6U_HALF)) & 1u))
        return 1;
    memcpy(b, s->b[k % FM6U_HALF], FM6_BLOB);
#if defined(CR_TRACE) && CR_TRACE
    printf("fm6: load slot %u patch crc %04x\n", (unsigned)k + 1u, (unsigned)fm6u_crc16(b));
#endif
    return 0;
}

static void fm6u_boot(void)                      /* persist_boot (upreset.c up_boot) */
{
    uint32_t h;
    for (h = 0; h < 2u; h++) {
        int n = -1;
#if FELUCCA_FLASH
        n = flash_ok ? st_load(OBJ_FM6STORE0 + h, &fm6u[h], sizeof fm6u[h]) : -1;
#endif
        if (n != (int)sizeof fm6u[h] || !fm6u_valid(&fm6u[h], h))
            fm6u_empty(h);
    }
    fm6_store_read = fm6u_get;
}

/* slot k = blob b (0: cleared), then its half to flash: 0 ok (or nothing to change), 2 flash error (the mirror kept as
 * it was) */
static int fm6u_put(uint32_t k, const uint8_t *b)
{
    uint8_t old[FM6_BLOB];
    uint32_t h = (k / FM6U_HALF) & 1u, i = k % FM6U_HALF, used;
    fm6u_t *s = &fm6u[h];
    if (k >= UP_SLOTS || (b && !fm6_blob_ok(b)))
        return 1;
    if (!fm6u_valid(s, h))
        fm6u_empty(h);
    if (!b && !((s->used >> i) & 1u))
        return 0;                                /* (nothing stored: no flash write) */
    if (b && ((s->used >> i) & 1u) && !memcmp(s->b[i], b, FM6_BLOB))
        return 0;
    memcpy(old, s->b[i], FM6_BLOB);
    used = s->used;
    if (b) {
        memcpy(s->b[i], b, FM6_BLOB);
        s->used |= 1u << i;
    } else {
        memset(s->b[i], 0, FM6_BLOB);
        s->used &= ~(1u << i);
    }
    fm6_store_read = fm6u_get;
#if FELUCCA_FLASH
    if (flash_ok && st_save(OBJ_FM6STORE0 + h, s, sizeof *s)) {
        memcpy(s->b[i], old, FM6_BLOB);
        s->used = used;
        return 2;
    }
#else
    (void)used;
#endif
    return 0;
}

/* up_put stored record r in slot k: an FM6 sound -> the blob of the part it was saved from, another engine's -> blob k
 * cleared. The part: an FM6 part (eng_req) whose values are the record's (as va_store_saved), the selected part first */
static void fm6u_saved(uint32_t k, const up_rec_t *r)
{
    uint8_t b[FM6_BLOB];
    uint32_t n, tr, i;
    if (!r || r->engine != ENGI_FM6) {
        fm6u_put(k, 0);
        return;
    }
    for (n = 0; n < 2u; n++) {
        const track_t *t;
        tr = n ? (song.sel ? 0u : 1u) : (song.sel < 2u ? song.sel : 0u);
        t = &trk[tr];
        if (t->eng_req != ENGI_FM6)
            continue;
        for (i = 0; i < P_COUNT && i < r->np; i++)
            if (i != P_VOICE && up_value(r, i) != (int16_t)clamp(t->p[i], -64, 127))
                break;
        if (i < P_COUNT && i < r->np)
            continue;
        fm6_blob_get(t, b);
        fm6u_put(k, b);
#if defined(CR_TRACE) && CR_TRACE
        printf("fm6: save slot %u part %u patch crc %04x\n", (unsigned)k + 1u, (unsigned)tr, (unsigned)fm6u_crc16(b));
#endif
        return;
    }
    fm6u_put(k, 0);                              /* (no part holds it: the slot loads its PTCH patch + its macros) */
}

/* up_values loads record r: an FM6 record marks its slot for fm6_track_loaded */
static void fm6u_loading(const up_rec_t *r)
{
    uint32_t b;
    fm6_user_pending = 0;
    if (r->engine != ENGI_FM6)
        return;
    for (b = 0; b < UP_SLOTS / UP_PER_BANK; b++)
        if (r >= up_bank[b].r && r < up_bank[b].r + UP_PER_BANK)
            fm6_user_pending = (uint8_t)(b * UP_PER_BANK + (uint32_t)(r - up_bank[b].r) + 1u);
}
