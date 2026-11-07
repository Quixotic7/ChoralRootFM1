/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of the sound pool (0.22, firmware/src/upool.c over storage.c sp_*) on the firmware as the emulator builds
 * it: every engine's sound (P5 included) saved into slot 128 and loaded back after a power cycle, its patch byte for
 * byte; the table; a full pool (128 slots) and "no free slot"; erase and rename; the favourites' section 0x81 (slots
 * 33..128) and what 377264f's reader keeps of the object; the quiet writes' spare (the pool's erase ahead); a cut
 * during a save on the firmware's own path. The floating banks' power cuts are tests/pool_test.c's. (0.21's slots
 * are not read any more, HISTORY 3y231: no migration to test.) */
#include <os/lock.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../tools/emu/emu_hooks.h"

emu_hal_t emu_hal;
const int8_t emu_keymap[6][EMU_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},
};
#include "../tools/emu/emu_firmware.h"

static int fails, checks;
#define OK(c, ...)                                                      \
    do {                                                                \
        checks++;                                                       \
        if (!(c)) {                                                     \
            fails++;                                                    \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);               \
        } else {                                                        \
            printf("ok   ");                                            \
        }                                                               \
        printf(__VA_ARGS__);                                            \
        printf("\n");                                                   \
    } while (0)

static void power_cycle(void)                        /* the table and the stores from flash, as at power-on */
{
    memset(upn, 0, sizeof upn);
    sp_scanned = 0;
    up_boot();
}

int main(void)
{
    static const uint32_t ENG[] = {ENGI_VA, ENGI_FM6, ENGI_CZ, ENGI_QUAD, ENGI_PROPHET, ENGI_GRAIN};
    static const char *const NM[] = {"VA SAVED", "FM6 SAVED", "CZ SAVED", "QUAD SAVED", "P5 SAVED", "GRAIN SAVED"};
    track_t *t = &trk[CR_PART_CHORD];
    uint8_t b0[UPP_MAX], b1[UPP_MAX];
    uint32_t i, k, n, bad;
    char nm[13];

    emu_flash_open();
    memset(emu_flash, 0xFF, sizeof emu_flash);
    cr_bank_boot();
    cr_settings_boot();
    cr_ui_init();
    uint32_t fm6_n0 = pool_count(ENGI_FM6);
    OK(UP_POOL && UP_SLOTS == 128u && !up_count(), "a fresh unit: 128 slots, none used");

    /* ---- every engine into slot 128, back after a power cycle ---- */
    for (i = 0; i < NELEM(ENG); i++) {
        const eng_deep_t *d = ENGINES[ENG[i]]->deep;
        uint32_t len = up_patch_len(ENG[i]), pos;
        int16_t p[P_COUNT];
        cu_pool_load(0, ENG[i], pool_count(ENG[i]) > 2u ? 2u : 1u);
        if (d->npages && d->get && d->set) {         /* an edit of the deep patch: its first column a step */
            int32_t v = d->get(t, 0, 0);
            d->set(t, 0, 0, v > 0 ? v - 1 : v + 1);
        }
        d->blob_get(t, b0);
        memcpy(p, t->p, sizeof p);
        OK(len == d->blob_size && len <= UPP_MAX && cb_store(t, 0, 127, NM[i], 0) == 0 && up_used(127) &&
           up_rec(127)->engine == ENG[i], "%s: saved into U128 (%u bytes of patch)", ENGINES[ENG[i]]->name, (unsigned)len);
        power_cycle();
        up_name(127, nm);
        OK(up_used(127) && !strcmp(nm, NM[i]) && up_engine(127) == ENG[i] && up_patch_get(127, ENG[i], b1, len) == 0 &&
           !memcmp(b0, b1, len), "%s: after a power cycle U128 is \"%s\", its patch byte for byte", ENGINES[ENG[i]]->name, nm);
        cu_pool_load(0, ENG[i], 1);
        pos = pool_pos_slot(ENG[i], 127);
        cu_pool_load(0, ENG[i], pos);
        d->blob_get(t, b1);
        for (k = 0, bad = 0; k < P_COUNT; k++)
            bad += !param_kept(k) && k != P_VOICE && p[k] != t->p[k];
        OK(psnd[0].kind == PK_USER && psnd[0].slot == 127 && !memcmp(b0, b1, len) && !bad,
           "%s: PRESETS reaches it at %u (the factory presets before it); loaded, the part's patch and values are the saved ones",
           ENGINES[ENG[i]]->name, (unsigned)pos);
        OK(up_put(127, 0) == 0 && !up_used(127) && up_patch_get(127, ENG[i], b1, len) != 0, "%s: U128 erased, its patch with it",
           ENGINES[ENG[i]]->name);
    }

    /* ---- FM TONE in the Digitone's ranges (2026-10-14): a slot of before (a self-made 80-byte version-3 blob) loads
     * converted (the hi-res values x 100, FDBK 0..120 -> 0..127); the two-decimal texts (params.c, cr_edit.c) ---- */
    {
        static const struct { uint8_t pg, col; int16_t v; const char *s; } TX[] = {
            {4, 0, 4201, "42.01"}, {4, 1, 4349, "43.49"}, {14, 0, 4134, "+41.34"}, {6, 0, 12685, "126.85"},
            {4, 3, -6400, "-64.00"}, {15, 3, 5, "+0.05"}, {15, 3, -5, "-0.05"}, {1, 0, 0, "0.00"}, {1, 0, -2600, "-26.00"},
            {12, 3, 127, "INF"}, {12, 1, 127, "INF"}, {12, 3, 126, "126"}, {12, 1, 126, "126"}, {12, 3, 0, "0"},
            {13, 0, 110, "110"}, {13, 0, 127, "127"}, {4, 0, 12700, "127.00"}, {4, 0, 12699, "126.99"},
            {1, 1, 12700, "127.00"}, {1, 2, 127, "127"}, {10, 3, 64, "64"}, {3, 2, 100, "100"}};   /* (version 5: AMP DEC /
                                                   * REL 0..126 + INF, LEVEL 0..127 whole numbers, the tops 127.00) */
        uint8_t v3[UPP_MAX], h[2];
        uint32_t j, pos, tbad = 0;
        cu_pool_load(0, ENGI_QUAD, 1);
        OK(cb_store(t, 0, 127, "QUAD V3", 0) == 0 && up_used(127), "FM TONE: saved into U128");
        memset(v3, 0, sizeof v3);
        v3[0] = 'Q', v3[1] = 3;
        for (j = 0; j < QP_NP5; j++)
            v3[2 + j] = (uint8_t)(quad_range_v3(j).def - quad_range_v3(j).min);
        v3[2 + QP_FREQ] = 42, v3[2 + QP_HARM] = (uint8_t)(-19 + 26), v3[2 + QP_FDBK] = 120, v3[2 + QP_LEVEL] = 100;
        v3[2 + QP_EREL] = 127, v3[2 + QP_LFO(0, QL_SPEED)] = (uint8_t)(-10 + 64);
        h[0] = (uint8_t)(ENGI_QUAD + 1u), h[1] = QUAD_BLOB_V3;
        OK(sp_write(127, UPP_DATA, v3, UPP_MAX) == 0 && sp_write(127, UPP_HDR, h, 2) == 0, "FM TONE: U128 made a slot of before");
        power_cycle();
        OK(up_patch_get(127, ENGI_QUAD, b1, QUAD_BLOB) == 0 && !memcmp(b1, v3, QUAD_BLOB),
           "FM TONE: the slot's 80-byte patch read (the rest zeros)");
        pos = pool_pos_slot(ENGI_QUAD, 127);
        cu_pool_load(0, ENGI_QUAD, pos);
        OK(quad_patch[0][QP_FREQ] == 4200 && quad_patch[0][QP_HARM] == -1900 && quad_patch[0][QP_FDBK] == 127 &&
           quad_patch[0][QP_LEVEL] == 100 && quad_patch[0][QP_EREL] == 127 && quad_patch[0][QP_LFO(0, QL_SPEED)] == -1000 &&
           t->p[P_E4] == -19 && t->p[P_E6] == 127,
           "FM TONE: a version-3 slot loads converted (FREQ %d HARM %d FDBK %d LEVEL %d)", quad_patch[0][QP_FREQ],
           quad_patch[0][QP_HARM], quad_patch[0][QP_FDBK], quad_patch[0][QP_LEVEL]);
        for (j = 0; j < NELEM(TX); j++) {
            const param_desc_t *dd = &QUAD_PAGES[TX[j].pg].col[TX[j].col];
            char val[12], cell[16];
            const char *u;
            param_format(dd, TX[j].v, val, &u);
            ce_ptext(dd, TX[j].v, ENG_C_NUM, cell, sizeof cell);
            if (strcmp(val, TX[j].s) || strcmp(cell, TX[j].s) || u[0]) {
                printf("  %s %d: \"%s\" / cell \"%s\" unit \"%s\", want \"%s\"\n", dd->label, TX[j].v, val, cell, u, TX[j].s);
                tbad++;
            }
        }
        OK(!tbad, "FM TONE: the two-decimal texts (\"42.01\" \"43.49\" \"+41.34\" \"126.85\" \"-64.00\" INF ..): %u wrong", tbad);
        OK(up_put(127, 0) == 0 && !up_used(127), "FM TONE: U128 erased");
    }

    /* ---- an FM TONE sound that carries its sends (3y203, version 6's SENDS: a Digitone import): its load sets the part's
     * three sends (the chord part's cs.fx_amt with FX left as it is, the bass part's track values), Drive as ever; a
     * sound without SENDS leaves them; a save of the chord part's stores the amounts, not FX off's zeros ---- */
    {
        track_t *bs = &trk[CR_PART_BASS];
        uint32_t pos;
        int16_t v[P_COUNT];
        cu_pool_load(0, ENGI_QUAD, 1);
        quad_patch[0][QP_SENDS] = 1;
        cs.fx_on = 0;
        cs.fx_amt[0] = 70, cs.fx_amt[1] = 50, cs.fx_amt[2] = 30;       /* (CU_FX: Reverb, Chorus, Delay) */
        cu_fx_apply();
        OK(cu_store(t, 0, 127, "SENDS", 0) == 0 && up_used(127), "sends: an FM TONE sound with SENDS saved into U128");
        up_values(up_rec(127), v);
        OK(v[P_REV] == 70 && v[P_CHOR] == 50 && v[P_DLY] == 30, "sends: the record holds the amounts (FX off: not 0) %d %d %d",
           v[P_REV], v[P_CHOR], v[P_DLY]);
        cs.fx_amt[0] = cs.fx_amt[1] = cs.fx_amt[2] = 5;
        pos = pool_pos_slot(ENGI_QUAD, 127);
        cu_pool_load(0, ENGI_QUAD, pos);
        OK(quad_patch[0][QP_SENDS] == 1 && cs.fx_amt[0] == 70 && cs.fx_amt[1] == 50 && cs.fx_amt[2] == 30 && !cs.fx_on &&
           t->p[P_REV] == 0, "sends: its load sets the chord part's sends (%u %u %u), FX off stays off",
           cs.fx_amt[0], cs.fx_amt[1], cs.fx_amt[2]);
        cs.fx_on = 1;
        cu_fx_apply();
        cu_pool_load(0, ENGI_QUAD, 1);                                  /* a factory preset: the 0.18 rule */
        OK(cs.fx_amt[0] == 70 && t->p[P_REV] == 70 && !quad_patch[0][QP_SENDS], "sends: a sound without SENDS leaves them");
        bs->p[P_REV] = bs->p[P_DLY] = bs->p[P_CHOR] = 1;
        cu_pool_load(1, ENGI_QUAD, pos);
        OK(bs->p[P_REV] == 70 && bs->p[P_CHOR] == 50 && bs->p[P_DLY] == 30, "sends: on the bass part, its track's (%d %d %d)",
           bs->p[P_REV], bs->p[P_CHOR], bs->p[P_DLY]);
        quad_patch[0][QP_SENDS] = 0;
        cs.fx_amt[0] = 9;
        cu_pool_load(0, ENGI_QUAD, 1);
        OK(up_put(127, 0) == 0 && !up_used(127) && cs.fx_amt[0] == 9, "sends: U128 erased");
    }

    /* ---- GRAIN's first build (3y136) saved 20-byte version-1 records (the owner's U033 DARK HAZE): such a slot loads as
     * the sound it saved (its 18 values, SCAN halved and forward, the rest INIT's), the record read whole with zeros
     * after it (3y139; before: refused for its length, the slot played a scrambled INIT) ---- */
    {
        uint8_t v1[UPP_MAX], h[2];
        int8_t want[GQ_NP];
        uint32_t j, pos, mbad = 0;
        cu_pool_load(0, ENGI_GRAIN, 1);
        OK(cb_store(t, 0, 127, "DARK HAZE", 0) == 0 && up_used(127), "GRAIN: saved into U128");
        memset(v1, 0, sizeof v1);
        v1[0] = 'G', v1[1] = 1;
        for (j = 0; j < GQ_NP_V1; j++)
            v1[2 + j] = (uint8_t)(int8_t)((j * 29u + 3u) % 150u - 20u);   /* values of every knob, some past a range */
        v1[2 + GQ_SCAN] = 100;
        h[0] = (uint8_t)(ENGI_GRAIN + 1u), h[1] = GRAIN_BLOB_V1;
        OK(sp_write(127, UPP_DATA, v1, UPP_MAX) == 0 && sp_write(127, UPP_HDR, h, 2) == 0, "GRAIN: U128 made a slot of the first build");
        power_cycle();
        OK(up_patch_get(127, ENGI_GRAIN, b1, GRAIN_BLOB) == 0 && !memcmp(b1, v1, GRAIN_BLOB),
           "GRAIN: the slot's 20-byte version-1 patch read (the rest zeros)");
        grain_init_patch(want);
        for (j = 0; j < GQ_NP_V1; j++)
            want[j] = grain_clampv(j, (int8_t)v1[2 + j]);
        want[GQ_SCAN] = grain_clampv(GQ_SCAN, 50);
        pos = pool_pos_slot(ENGI_GRAIN, 127);
        cu_pool_load(0, ENGI_GRAIN, pos);
        for (j = 0; j < 8u; j++)
            mbad += t->p[P_E0 + j] != want[GRAIN_MAC[j]];
        OK(psnd[0].kind == PK_USER && psnd[0].slot == 127 && !memcmp(grain_patch[0], want, GQ_NP) && !mbad,
           "GRAIN: the version-1 slot loads as the sound it saved (TYPE %d TONE %d .. SCAN %d, %u macros off)",
           grain_patch[0][GQ_TYPE1], grain_patch[0][GQ_TONE1], grain_patch[0][GQ_SCAN], (unsigned)mbad);
        OK(up_put(127, 0) == 0 && !up_used(127), "GRAIN: U128 erased");
    }

    /* ---- a rename keeps the patch; another engine's sound over a slot drops it ---- */
    cu_pool_load(0, ENGI_CZ, 3);
    ENGINES[ENGI_CZ]->deep->blob_get(t, b0);
    OK(cb_store(t, 0, 99, "CZ HUNDRED", 0) == 0 && up_rename(99, "RENAMED") == 0 && up_patch_get(99, ENGI_CZ, b1, CZ_BYTES) == 0 &&
       !memcmp(b0, b1, CZ_BYTES) && (up_name(99, nm), !strcmp(nm, "RENAMED")), "rename U100: the name changes, the CZ-1 tone stays");
    cu_pool_load(0, ENGI_VA, 2);
    OK(cb_store(t, 0, 99, "VA OVER", 0) == 0 && up_patch_get(99, ENGI_CZ, b1, CZ_BYTES) != 0 &&
       up_patch_get(99, ENGI_VA, b1, VA_BLOB) == 0, "a VA sound over U100: the CZ-1 tone gone, the VA patch there");

    /* ---- the favourites: a mark on slot 100 (section 0x81), one on slot 32 (0x80) ---- */
    {
        static uint8_t pk[CRF_PACK_MAX];
        uint32_t len, p, s;
        int has81 = 0, has80 = 0;
        (void)crf_unpack(0, 0);
        OK(crf_set(CRF_T_USER, 99, 1) == 1 && crf_set(CRF_T_USER, 31, 1) == 1 && crf_set_type(CRF_T_USER, 99, CRF_BASS) == 1 &&
           crf_has(CRF_T_USER, 99) && crf_type(CRF_T_USER, 99) == CRF_BASS && !crf_has(CRF_T_USER, 100),
           "favourites: U100 marked (typed Bass), U32 marked");
        len = crf_pack(pk);
        for (p = 2, s = 0; s < pk[1] && p + 2u <= len; s++, p += 2u + pk[p + 1u]) {
            has81 |= pk[p] == CRF_T_USER2 && pk[p + 1u] == 9u && pk[p + 2u + 8u] == 0x08u;   /* (id 99 - 32 = 67: byte 8 bit 3) */
            has80 |= pk[p] == CRF_T_USER && pk[p + 1u] == 4u && pk[p + 2u + 3u] == 0x80u;
        }
        OK(has80 && has81 && len <= 264u, "favourites: packed, U32 in 0x80 as before, U100 in 0x81; %u bytes (377264f reads 264 at most)",
           (unsigned)len);
        OK(crf_unpack(pk, len) == 0 && crf_has(CRF_T_USER, 99) && crf_has(CRF_T_USER, 31) && crf_type(CRF_T_USER, 99) == CRF_BASS,
           "favourites: unpacked, both marks and U100's type back");
        {   /* 377264f's reader (cr_favs.c of then: a section it does not know is skipped; tools/emu/test_pool.sh built it
             * until 3y231): what the bytes say: 0x81 and its | 0x40 / | 0x20 forms are tags it lacks */
            OK(crf_off(CRF_T_USER2, &s) == 4u && s == 12u, "favourites: 0x81 is 12 bytes after 0x80's 4 (ids 32..127)");
        }
        OK(up_put(99, 0) == 0 && !crf_has(CRF_T_USER, 99) && crf_has(CRF_T_USER, 31), "favourites: U100 erased: its mark goes");
        (void)crf_unpack(0, 0);
    }

    /* ---- a full pool: 128 saves, then "no free slot" ---- */
    cu_pool_load(0, ENGI_FM6, 1);
    for (k = 0, n = 0; k < UP_SLOTS; k++)
        if (!up_used(k)) {
            snprintf(nm, sizeof nm, "FILL %03u", (unsigned)k + 1u);
            n += cb_store(t, 0, k, nm, 0) == 0;
        }
    OK(up_count() == 128u && cb_free_slot() == UP_SLOTS, "a full pool: 128 slots used (%u saved here), no free slot", (unsigned)n);
    power_cycle();
    for (k = 0, bad = 0; k < UP_SLOTS; k++) {
        char want[13];
        snprintf(want, sizeof want, "FILL %03u", (unsigned)k + 1u);
        up_name(k, nm);
        bad += k != 99u && strcmp(nm, want) != 0;
    }
    OK(up_count() == 128u && !bad && pool_count(ENGI_FM6) == fm6_n0 + 128u,
       "a full pool after a power cycle: every slot's name; FM6's pool %u places", (unsigned)pool_count(ENGI_FM6));
    {   /* the floating banks: 13 used, every sector a valid copy or free */
        uint32_t b, used = 0;
        for (b = 0; b < SP_NBANK; b++)
            used += sp_used(b);
        OK(used == SP_NBANK, "the pool's 13 banks written (slots 129, 130 of the last unused)");
    }

    /* ---- the spare (quiet writes): erased ahead, a save programs only ---- */
    {
        uint32_t e0;
        OK(sp_prep() >= 0 && sp_ready(), "spare: erased ahead (sp_prep)");
        e0 = sp_erases;
        OK(up_rename(5, "NO ERASE") == 0 && sp_erases == e0, "spare: a save on it erases nothing");
        e0 = sp_erases;
        OK(up_rename(6, "ONE ERASE") == 0 && sp_erases - e0 <= 1u, "spare: the next save (no prep between): at most one erase");
    }

    /* ---- a cut in the middle of a save: the old slot or the new one, nothing else changed ---- */
    {
        static uint8_t before[0x10000];
        char a[13], c[13];
        memcpy(before, emu_flash + SP_BASE, sizeof before);
        up_name(7, a);
        emu_flash_fail_after(8);                     /* (emu_hal_fw.h: the 9th program fails, nothing after) */
        n = (uint32_t)up_rename(7, "TORN");
        emu_flash_fail_after(-1);
        power_cycle();
        up_name(7, c);
        for (k = 0, bad = 0; k < UP_SLOTS; k++) {
            char want[13];
            snprintf(want, sizeof want, "FILL %03u", (unsigned)k + 1u);
            up_name(k, nm);
            bad += k != 7u && k != 5u && k != 6u && k != 99u && strcmp(nm, want) != 0;
        }
        OK(n == 2u && !strcmp(c, a) && !bad && up_count() == 128u, "a save cut after 8 programs: rc 2, U008 as it was (%s), the rest intact", c);
    }

    printf("%s: %d checks, %d failed\n", fails ? "CR POOL TESTS FAILED" : "cr pool tests passed", checks, fails);
    return fails ? 1 : 0;
}
