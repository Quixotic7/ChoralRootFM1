/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* The FM6 patch bank (eng_fm6.c PTCH B1..B27): 27 packed 128-byte patches in one storage.c object
 * (OBJ_FM6BANK, A/B: 0x9F000 / 0xFE000), mirrored in RAM (the pool) so a PTCH turn never reads flash. The web
 * editor writes it (EDITOR_PROTOCOL.md FM6_PUT / FM6_ERASE); a full backup carries it (id 8). An empty slot,
 * or a bank of another layout, plays the init voice. The payload ends 3472 + 256 bytes into its sector: the
 * sector's tail stays erased (no byte of it can look like a boot record).
 * Included by upreset.c (the hosts that build the user presets build this too). */
#define FM6_BANK_MAGIC 0x42364D46u               /* "FM6B" */
typedef struct {
    uint32_t magic;
    uint16_t ver, nslot;                         /* 1, FM6_BANK_N */
    uint32_t used;                               /* bit k: slot k holds a patch */
    uint32_t rsv;
    uint8_t v[FM6_BANK_N][FM6_PACKED];
} fm6_bank_t;
_Static_assert(sizeof(fm6_bank_t) == 3472u, "FM6 bank layout");
static fm6_bank_t fm6_bank __attribute__((section(".pool")));

static int fm6_bank_valid(const fm6_bank_t *b)
{
    uint32_t k, i;
    if (b->magic != FM6_BANK_MAGIC || b->ver != 1u || b->nslot != FM6_BANK_N || (b->used >> FM6_BANK_N))
        return 0;
    for (k = 0; k < FM6_BANK_N; k++)
        for (i = 0; i < FM6_PACKED; i++)
            if (b->v[k][i] > 127u)
                return 0;
    return 1;
}

/* eng_fm6.c fm6_bank_read: slot k's record, 0 = there is one */
static int fm6_bank_get(uint32_t k, uint8_t *pk)
{
    if (k >= FM6_BANK_N || fm6_bank.magic != FM6_BANK_MAGIC || !((fm6_bank.used >> k) & 1u))
        return 1;
    memcpy(pk, fm6_bank.v[k], FM6_PACKED);
    return 0;
}

static int fm6_bank_used(uint32_t k) { return k < FM6_BANK_N && !fm6_bank_get(k, (uint8_t[FM6_PACKED]){0}); }

/* the mirror after a load of n bytes (-1: none): a valid bank, else empty */
static void fm6_bank_check(int n)
{
    if (n != (int)sizeof fm6_bank || !fm6_bank_valid(&fm6_bank))
        memset(&fm6_bank, 0, sizeof fm6_bank);
    fm6_bank_read = fm6_bank_get;
}

static void fm6_bank_boot(void)                  /* persist_boot */
{
#if FELUCCA_FLASH
    fm6_bank_check(flash_ok ? st_load(OBJ_FM6BANK, &fm6_bank, sizeof fm6_bank) : -1);
#else
    fm6_bank_check(-1);
#endif
}

/* slot k = the packed record pk (0: erase), then the bank to flash: 0 ok, 1 bad slot, 2 flash error or the
 * transport runs (nothing changed). The record is stored through unpack / pack: every value in range */
static int fm6_bank_put(uint32_t k, const uint8_t *pk)
{
    uint8_t old[FM6_PACKED], v[FP_SIZE + 1u];
    uint32_t used, magic;
    if (k >= FM6_BANK_N)
        return 1;
    if (transport_busy()) {                      /* no flash erase while playing (project_save) */
        ui_message("STOP TO SAVE");
        return 2;
    }
    if (!fm6_bank_valid(&fm6_bank))
        memset(&fm6_bank, 0, sizeof fm6_bank);
    memcpy(old, fm6_bank.v[k], FM6_PACKED);
    used = fm6_bank.used;
    magic = fm6_bank.magic;
    fm6_bank.magic = FM6_BANK_MAGIC;
    fm6_bank.ver = 1;
    fm6_bank.nslot = FM6_BANK_N;
    if (pk) {
        fm6_unpack(pk, v);
        fm6_pack(v, fm6_bank.v[k]);
        fm6_bank.used |= 1u << k;
    } else {
        memset(fm6_bank.v[k], 0, FM6_PACKED);
        fm6_bank.used &= ~(1u << k);
    }
    fm6_bank_read = fm6_bank_get;
#if FELUCCA_FLASH
    if (flash_ok && st_save(OBJ_FM6BANK, &fm6_bank, sizeof fm6_bank)) {
        memcpy(fm6_bank.v[k], old, FM6_PACKED);
        fm6_bank.used = used;
        fm6_bank.magic = magic;
        return 2;
    }
#else
    (void)used;
    (void)magic;
#endif
    {   /* tracks playing this slot hear the new patch (fm6_poll reloads it) */
        uint32_t t;
        for (t = 0; t < NTRK; t++)
            if (fm6_slot[t] == FM6_NFACTORY + k)
                fm6_slot[t] = 0xFFu;
    }
    return 0;
}
