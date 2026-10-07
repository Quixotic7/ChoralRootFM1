/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modifications Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* The FM6 patch bank (eng_fm6.c PTCH B1..B32, a DX7 cartridge): 32 packed 128-byte patches, each 7-bit packed into 112 bytes (eight 7-bit bytes ride in seven), in one storage.c
 * object (OBJ_FM6BANK, A/B: 0x9F000 / 0xFE000), mirrored in RAM (the pool) so a PTCH turn never reads flash. The
 * web editor writes it (EDITOR_PROTOCOL.md FM6_PUT / FM6_ERASE), so do a DX7 bank dump and STORE (fm6_store.c);
 * a full backup carries it (id 8). An empty slot plays the init voice. The banks of earlier firmware load: Felucca
 * 1.0's (27 unpacked slots, no function settings) and Melodee's before it (32 voices, 7-bit packed, every slot used).
 * Included by upreset.c (the hosts that build the user presets build this too). */
#define FM6_BANK_MAGIC 0x42364D46u               /* "FM6B" */
#define FM6_BANK_VER 2u
#define FM6_BANK_PK 112u                         /* a slot's 128 bytes, 7-bit packed */
typedef struct {
    uint32_t magic;
    uint16_t ver, nslot;                         /* FM6_BANK_VER, FM6_BANK_N */
    uint32_t used;                               /* bit k: slot k holds a patch */
    uint8_t fn[FM6_NFN];                         /* inert: the function settings, which are the tracks' now (kept
                                                  * as stored; a new bank: Dexed's) */
    uint8_t pk[FM6_BANK_N][FM6_BANK_PK];
} fm6_bank_t;
_Static_assert(sizeof(fm6_bank_t) == 3612u && sizeof(fm6_bank_t) <= 3840u, "FM6 bank layout (a backup object)");
static fm6_bank_t fm6_bank __attribute__((section(".pool")));

/* (fm6_pack7 / fm6_unpack7, eight 7-bit bytes <-> seven: eng_fm6.c, which the patch blob needs first) */

static int fm6_bank_valid(const fm6_bank_t *b)
{
    uint32_t k;
    if (b->magic != FM6_BANK_MAGIC || b->ver != FM6_BANK_VER || b->nslot != FM6_BANK_N)
        return 0;
    for (k = 0; k < FM6_NFN; k++)
        if (b->fn[k] > FM6_FNMAX[k])
            return 0;
    return 1;
}

/* eng_fm6.c fm6_bank_read: slot k's record, 0 = there is one */
static int fm6_bank_get(uint32_t k, uint8_t *pk)
{
    if (k >= FM6_BANK_N || fm6_bank.magic != FM6_BANK_MAGIC || !((fm6_bank.used >> k) & 1u))
        return 1;
    fm6_unpack7(pk, fm6_bank.pk[k], FM6_PACKED);
    return 0;
}

static int fm6_bank_used(uint32_t k) { return k < FM6_BANK_N && !fm6_bank_get(k, (uint8_t[FM6_PACKED]){0}); }

static void fm6_bank_empty(void)
{
    memset(&fm6_bank, 0, sizeof fm6_bank);
    fm6_bank.magic = FM6_BANK_MAGIC;
    fm6_bank.ver = FM6_BANK_VER;
    fm6_bank.nslot = FM6_BANK_N;
    memcpy(fm6_bank.fn, FM6_FNDEF, FM6_NFN);
}

/* the mirror after a load of n bytes (-1: none): a valid bank, else empty */
static void fm6_bank_check(int n)
{
    if (n != (int)sizeof fm6_bank || !fm6_bank_valid(&fm6_bank))
        fm6_bank_empty();
    fm6_bank_read = fm6_bank_get;
    fm6_bank_gen++;
}

/* a bank of earlier firmware (n bytes at b) -> the mirror. 0 = it was one */
static int fm6_bank_import(const uint8_t *b, int n)
{
    uint32_t magic = b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24, k;
    if (magic != FM6_BANK_MAGIC)
        return 1;
    if (n == 4 + (int)(FM6_BANK_N * FM6_BANK_PK)) {      /* Melodee before: 32 packed voices, all of them used */
        fm6_bank_empty();
        memcpy(fm6_bank.pk, b + 4, FM6_BANK_N * FM6_BANK_PK);
        fm6_bank.used = 0xFFFFFFFFu;
        return 0;
    }
    if (n == 16 + 27 * 128 && b[4] == 1u && b[6] == 27u) {   /* Felucca 1.0: 27 records, a used mask */
        uint32_t used = b[8] | (uint32_t)b[9] << 8 | (uint32_t)b[10] << 16 | (uint32_t)b[11] << 24;
        uint8_t v[FP_SIZE + 1u], pk[FM6_PACKED];
        fm6_bank_empty();
        for (k = 0; k < 27u; k++)
            if ((used >> k) & 1u) {
                fm6_unpack(b + 16 + k * 128u, v);           /* (every value into its range) */
                memset(pk, 0, sizeof pk);
                fm6_pack(v, pk);
                fm6_pack7(fm6_bank.pk[k], pk, FM6_PACKED);
                fm6_bank.used |= 1u << k;
            }
        return 0;
    }
    return 1;
}

static int fm6_bank_save(void)                   /* the mirror to flash: 0 saved, else RAM only */
{
#if FELUCCA_FLASH
    return flash_ok && !st_save(OBJ_FM6BANK, &fm6_bank, sizeof fm6_bank) ? 0 : -1;
#else
    return -1;
#endif
}

static void fm6_bank_boot(void)                  /* persist_boot (fm6_rx: the SysEx frame, idle at boot, as scratch) */
{
#if FELUCCA_FLASH
    int n = flash_ok ? st_load(OBJ_FM6BANK, fm6_rx, sizeof fm6_rx) : -1;
    if (n == (int)sizeof fm6_bank) {
        memcpy(&fm6_bank, fm6_rx, sizeof fm6_bank);
        fm6_bank_check(n);
    } else if (n > 0 && !fm6_bank_import(fm6_rx, n)) {
        fm6_bank_check((int)sizeof fm6_bank);
    } else {
        fm6_bank_check(-1);
    }
#else
    fm6_bank_check(-1);
#endif
}

/* slot k = the packed record pk (0: erase), then the bank to flash: 0 ok, 1 bad slot, 2 flash error or the
 * transport runs (nothing changed). The record is stored through unpack / pack: every value in range */
static int fm6_bank_put(uint32_t k, const uint8_t *pk)
{
    uint8_t old[FM6_BANK_PK], v[FP_SIZE + 1u], rec[FM6_PACKED];
    uint32_t used;
    if (k >= FM6_BANK_N)
        return 1;
    if (transport_busy()) {                      /* no flash erase while playing (project_save) */
        ui_message("STOP TO SAVE");
        return 2;
    }
    if (!fm6_bank_valid(&fm6_bank))
        fm6_bank_empty();
    memcpy(old, fm6_bank.pk[k], FM6_BANK_PK);
    used = fm6_bank.used;
    if (pk) {
        fm6_unpack(pk, v);
        memset(rec, 0, sizeof rec);
        fm6_pack(v, rec);
        fm6_pack7(fm6_bank.pk[k], rec, FM6_PACKED);
        fm6_bank.used |= 1u << k;
    } else {
        memset(fm6_bank.pk[k], 0, FM6_BANK_PK);
        fm6_bank.used &= ~(1u << k);
    }
    fm6_bank_read = fm6_bank_get;
    fm6_bank_gen++;
#if FELUCCA_FLASH
    if (fm6_bank_save() && flash_ok) {
        memcpy(fm6_bank.pk[k], old, FM6_BANK_PK);
        fm6_bank.used = used;
        return 2;
    }
#else
    (void)used;
    (void)old;
#endif
    {   /* tracks playing this slot hear the new patch (fm6_poll reloads it) */
        uint32_t t;
        for (t = 0; t < NTRK; t++)
            if (fm6_slot[t] == FM6_NFAC + k)
                fm6_slot[t] = 0xFFu;
    }
    return 0;
}
