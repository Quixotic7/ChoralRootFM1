/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* The CZ-1 tone store (eng_cz.c's blob, docs/CZ1.md): one 144-byte native tone per user slot, slot k <-> tone k, as
 * fm6_ustore.c for FM6. 32 x 144 bytes do not fit one storage.c object (3840 bytes of payload), so the store is two:
 * slots 1..16 in OBJ_CZSTORE0 = OBJ_PROJECT0 + 3 (A/B at 0x9D000 / 0x9E000, the last project pair ChoralRoot's
 * projects never use), 17..32 in OBJ_CZSTORE1 (0xA0000 / 0xA1000: the user sample slot 1's flash, storage.c's map).
 * Each: a 16-byte header ("CZ1U", version 1, 16 slots, the first slot, the tone size, the used mask) and 16 tones,
 * 2320 bytes; mirrored in the pool so loading a sound never reads flash; the commit protocol is storage.c's; the
 * backup's objects 12 and 13 (cr_backup.c).
 *
 * upreset.c calls it as it calls fm6_ustore.c: up_put saving a CZ-1 sound stores the tone of the part it came from
 * (czu_saved), erasing a slot or saving another engine's sound over it clears tone k; up_values loading a CZ-1 record
 * marks the slot (eng_cz.c cz_user_pending) and the load's cz_track_loaded reads tone k back. A slot without a tone
 * (none stored: a full erase, a restored bank without the store) loads its BANK / PTCH (cz_bank.c). Included by
 * upreset.c after fm6_ustore.c (FELUCCA_CZ). */
#if FELUCCA_FLASH
#define OBJ_CZSTORE0 (OBJ_PROJECT0 + 3)
#endif
#define CZU_MAGIC 0x55315A43u                    /* "CZ1U" */
#define CZU_VER 1u
#define CZU_HALF (UP_SLOTS / 2u)
typedef struct {
    uint32_t magic;
    uint16_t ver, nslot;                         /* CZU_VER, CZU_HALF */
    uint16_t first, blob;                        /* the first slot it holds (0, 16), CZ_BYTES */
    uint32_t used;                               /* bit k: slot first + k holds a tone */
    uint8_t b[CZU_HALF][CZ_BYTES];
} czu_t;
_Static_assert(sizeof(czu_t) == 16u + CZU_HALF * CZ_BYTES && sizeof(czu_t) <= 4096u - 256u,
               "CZ-1 store layout: header + 16 tones in one object");
static czu_t czu[2] __attribute__((section(".pool")));

static int czu_valid(const czu_t *s, uint32_t h)
{
    uint32_t k;
    if (s->magic != CZU_MAGIC || s->ver != CZU_VER || s->nslot != CZU_HALF || s->first != h * CZU_HALF ||
        s->blob != CZ_BYTES || (s->used >> CZU_HALF))
        return 0;
    for (k = 0; k < CZU_HALF; k++)
        if (((s->used >> k) & 1u) && !cz_patch_valid(s->b[k]))
            return 0;
    return 1;
}

static void czu_empty(uint32_t h)
{
    memset(&czu[h], 0, sizeof czu[h]);
    czu[h].magic = CZU_MAGIC;
    czu[h].ver = CZU_VER;
    czu[h].nslot = CZU_HALF;
    czu[h].first = (uint16_t)(h * CZU_HALF);
    czu[h].blob = CZ_BYTES;
}

static uint32_t czu_obj(uint32_t h) { return h ? (uint32_t)OBJ_CZSTORE1 : (uint32_t)OBJ_CZSTORE0; }

#if defined(CR_TRACE) && CR_TRACE
static uint16_t czu_crc16(const uint8_t *b) { return (uint16_t)st_crc32(b, CZ_BYTES); }   /* the traces' "crc" */
#endif

/* eng_cz.c cz_store_read: slot k's tone, 0 = there is one */
static int czu_get(uint32_t k, uint8_t *b)
{
    const czu_t *s = &czu[(k / CZU_HALF) & 1u];
    if (k >= UP_SLOTS || s->magic != CZU_MAGIC || !((s->used >> (k % CZU_HALF)) & 1u))
        return 1;
    memcpy(b, s->b[k % CZU_HALF], CZ_BYTES);
#if defined(CR_TRACE) && CR_TRACE
    printf("cz: load slot %u tone crc %04x\n", (unsigned)k + 1u, (unsigned)czu_crc16(b));
#endif
    return 0;
}

static void czu_boot(void)                       /* persist_boot (upreset.c up_boot) */
{
    uint32_t h;
    for (h = 0; h < 2u; h++) {
        int n = -1;
#if FELUCCA_FLASH
        n = flash_ok ? st_load(czu_obj(h), &czu[h], sizeof czu[h]) : -1;
#endif
        if (n != (int)sizeof czu[h] || !czu_valid(&czu[h], h))
            czu_empty(h);
    }
    cz_store_read = czu_get;
}

/* slot k = tone b (0: cleared), then its half to flash: 0 ok (or nothing to change), 1 bad, 2 flash error (the mirror
 * kept as it was) */
static int czu_put(uint32_t k, const uint8_t *b)
{
    uint8_t old[CZ_BYTES];
    uint32_t h = (k / CZU_HALF) & 1u, i = k % CZU_HALF, used;
    czu_t *s = &czu[h];
    if (k >= UP_SLOTS || (b && !cz_patch_valid(b)))
        return 1;
    if (!czu_valid(s, h))
        czu_empty(h);
    if (!b && !((s->used >> i) & 1u))
        return 0;                                /* (nothing stored: no flash write) */
    if (b && ((s->used >> i) & 1u) && !memcmp(s->b[i], b, CZ_BYTES))
        return 0;
    memcpy(old, s->b[i], CZ_BYTES);
    used = s->used;
    if (b) {
        memcpy(s->b[i], b, CZ_BYTES);
        s->used |= 1u << i;
    } else {
        memset(s->b[i], 0, CZ_BYTES);
        s->used &= ~(1u << i);
    }
    cz_store_read = czu_get;
#if FELUCCA_FLASH
    if (flash_ok && st_save(czu_obj(h), s, sizeof *s)) {
        memcpy(s->b[i], old, CZ_BYTES);
        s->used = used;
        return 2;
    }
#else
    (void)used;
#endif
    return 0;
}

/* up_put stored record r in slot k: a CZ-1 sound -> the tone of the part it was saved from, another engine's -> tone k
 * cleared. The part: a CZ-1 part (eng_req) whose values are the record's (as fm6u_saved), the selected part first */
static void czu_saved(uint32_t k, const up_rec_t *r)
{
    uint32_t n, tr, i;
    if (!r || r->engine != ENGI_CZ) {
        czu_put(k, 0);
        return;
    }
    for (n = 0; n < 2u; n++) {
        const track_t *t;
        tr = n ? (song.sel ? 0u : 1u) : (song.sel < 2u ? song.sel : 0u);
        t = &trk[tr];
        if (t->eng_req != ENGI_CZ)
            continue;
        for (i = 0; i < P_COUNT && i < r->np; i++)
            if (i != P_VOICE && up_value(r, i) != (int16_t)clamp(t->p[i], -64, 127))
                break;
        if (i < P_COUNT && i < r->np)
            continue;
        czu_put(k, cz_patch[tr].raw);
#if defined(CR_TRACE) && CR_TRACE
        printf("cz: save slot %u part %u tone crc %04x\n", (unsigned)k + 1u, (unsigned)tr,
               (unsigned)czu_crc16(cz_patch[tr].raw));
#endif
        return;
    }
    czu_put(k, 0);                               /* (no part holds it: the slot loads its BANK / PTCH) */
}

/* up_values loads record r: a CZ-1 record marks its slot for cz_track_loaded */
static void czu_loading(const up_rec_t *r)
{
    uint32_t b;
    cz_user_pending = 0;
    if (r->engine != ENGI_CZ)
        return;
    for (b = 0; b < UP_SLOTS / UP_PER_BANK; b++)
        if (r >= up_bank[b].r && r < up_bank[b].r + UP_PER_BANK)
            cz_user_pending = (uint8_t)(b * UP_PER_BANK + (uint32_t)(r - up_bank[b].r) + 1u);
}
