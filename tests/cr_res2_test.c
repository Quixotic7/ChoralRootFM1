/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of the FM-1's second resource region refused (HISTORY 3y296; firmware/src/cr_res.h CR_RES2,
 * docs/BOOT-SAFETY.md "The second resource region"), on the firmware as the emulator builds it, compiled with CR_RES2 1
 * and the tables the device keeps there named here (CR_RES2_IN: the host has no XIP window). With cr_res2_ok 1 every
 * reader gets the tables as the emulator does; with 0 (cr_shim.c cr_res2_boot refused the region: a cut install the
 * stub did not catch, the Transporter's rescue, a region of another build) none reads them:
 *   - the factory presets of FM6, VA, CZ-1 and PROPHET: their names "NO PRESET" (the pool, PRESETS, the reset
 *     question), a load gives the engine's defaults (VA: the init patch, FM6: the init voice), the counts and the
 *     pool's places as before; QUAD and GRAIN (in the image) as before
 *   - FM6's factory voices F1..F24: the init voice
 *   - the kerning of M and CRB: none (S's, the fallback font's, as before)
 * The power-on runs through it, and a sound saved before still loads as it was. */
#include <os/lock.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define CR_RES2 1
static int t_res2_in(const void *p);
#define CR_RES2_IN(p) t_res2_in((const void *)(p))
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

/* what the device's .res2 holds (firmware/app.ld; the sections RES2_PRESET, RES2_FM6, AF_RES2) */
static int t_res2_in(const void *p)
{
    return p == (const void *)VA_PRESETS || p == (const void *)FM6_PRESETS || p == (const void *)CZ_PRESETS ||
           p == (const void *)P5_TEST_PRESETS || p == (const void *)FM6_FACTORY || p == (const void *)FM6_ROM ||
           p == (const void *)AF_M_KB || p == (const void *)AF_M_KD || p == (const void *)AF_CRB_KB ||
           p == (const void *)AF_CRB_KD;
}

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

static uint32_t kern_pairs(const aafont_t *f)        /* the font's pairs that kern() finds (a sum of |delta|) */
{
    uint32_t a, b, n = 0;
    for (a = 32; a < 127u; a++)
        for (b = 32; b < 127u; b++) {
            int32_t d = kern(f, a, b);
            n += (uint32_t)(d < 0 ? -d : d);
        }
    return n;
}

int main(void)
{
    static const uint32_t MOVED[] = {ENGI_VA, ENGI_FM6, ENGI_CZ, ENGI_PROPHET};
    static const uint32_t KEPT[] = {ENGI_QUAD, ENGI_GRAIN};
    track_t *t = &trk[CR_PART_CHORD];
    uint8_t pk[FM6_PACKED], pk0[FM6_PACKED];
    uint32_t i, k, e, bad, s_kern, m_kern, crb_kern, n0[NELEM(MOVED)];
    char nm[13];
    int16_t saved[P_COUNT];

    emu_flash_open();
    memset(emu_flash, 0xFF, sizeof emu_flash);

    /* ---- the region this build's: as the emulator ---- */
    cr_res2_ok = 1;
    cr_bank_boot();
    cr_settings_boot();
    cr_ui_init();
    s_kern = kern_pairs(&AF_S);
    m_kern = kern_pairs(&AF_M);
    crb_kern = kern_pairs(&AF_CRB);
    OK(s_kern && m_kern && crb_kern, "region in place: S, M and CRB kern (|delta| sums %u, %u, %u)", (unsigned)s_kern,
       (unsigned)m_kern, (unsigned)crb_kern);
    cu_pool_load(0, ENGI_VA, pool_pos_factory(ENGI_VA, 0));
    OK(!strcmp(psnd[0].name, VA_PRESETS[0].name) && t->p[P_E0] == VA_PRESETS[0].e[0],
       "region in place: VA's first factory preset loads as %s", VA_PRESETS[0].name);
    OK(cb_store(t, 0, 0, "MY PAD", 0) == 0 && up_used(0), "a sound of it saved into U001");
    memcpy(saved, t->p, sizeof saved);
    for (i = 0; i < NELEM(MOVED); i++)
        n0[i] = pool_count(MOVED[i]);
    fm6_slot_get(0, pk);
    fm6_factory(0, pk0);
    OK(!memcmp(pk, pk0, FM6_PACKED) && memcmp(pk, FM6_INIT, FM6_PACKED), "region in place: FM6's F1 is its factory voice");

    /* ---- refused: the power-on, then every reader ---- */
    cr_res2_ok = 0;
    cb_booted = 0;                                   /* (a power cycle: the table, the stores, the trims' names) */
    memset(upn, 0, sizeof upn);
    sp_scanned = 0;
    cr_bank_boot();
    cr_settings_boot();
    cr_ui_init();
    cb_init();
    for (k = 0, bad = 0; k < NELEM(CB_TRIM); k++)
        bad += (CB_TRIM[k].engine == ENGI_VA || CB_TRIM[k].engine == ENGI_FM6 || CB_TRIM[k].engine == ENGI_CZ ||
                CB_TRIM[k].engine == ENGI_PROPHET) && cb_trim_k[k] != 0xFFu;
    OK(up_used(0) && !bad, "refused: the power-on runs; U001 is there; the loud presets' trims of the lists in the "
                           "region resolve to none");
    OK(kern_pairs(&AF_S) == s_kern && !kern_pairs(&AF_M) && !kern_pairs(&AF_CRB),
       "refused: no kerning on M and CRB, S's as before");
    for (i = 0; i < NELEM(MOVED); i++) {
        const engine_t *en;
        e = MOVED[i];
        en = ENGINES[e];
        OK(pool_count(e) == n0[i], "%s: the pool's count as before (%u)", en->name, (unsigned)n0[i]);
        for (k = 0, bad = 0; k < en->npresets; k++)
            bad += strcmp(cu_preset_name(e, k), "NO PRESET") != 0;
        for (k = pool_f0(e); k < en->npresets; k++) {
            pool_name(e, pool_pos_factory(e, k), nm);
            bad += strcmp(nm, "NO PRESET") != 0;
        }
        OK(!bad, "%s: each of its %u factory presets is named NO PRESET (cu_preset_name, the pool)", en->name,
           (unsigned)en->npresets);
        for (k = pool_f0(e), bad = 0; k < en->npresets; k++) {
            uint32_t j;
            cu_pool_load(0, e, pool_pos_factory(e, k));
            bad += t->eng_req != e || t->preset != k || strcmp(psnd[0].name, "NO PRESET") != 0;
            for (j = 0; j < 8u; j++)
                bad += t->p[P_E0 + j] != en->edit[j].def;
            bad += t->p[P_SUS] != 127 || t->p[P_ATK] || t->p[P_REL];
        }
        OK(!bad, "%s: a factory load gives the engine's defaults under NO PRESET, the place kept", en->name);
    }
    cu_pool_load(0, ENGI_VA, pool_pos_factory(ENGI_VA, 0));
    {
        int8_t p[VA_NP];
        va_init_patch(p);
        for (k = 0; k < 8u; k++)
            p[VA_MACRO[k]] = va_clampv(VA_MACRO[k], t->p[P_E0 + k]);
        OK(!memcmp(va_patch[CR_PART_CHORD], p, VA_NP), "refused: VA's factory load plays the init patch under the macros");
    }
    for (k = 0, bad = 0; k < FM6_NFAC; k++) {
        fm6_slot_get(k, pk);
        bad += memcmp(pk, FM6_INIT, FM6_PACKED) != 0;
    }
    OK(!bad, "refused: FM6's F1..F%u read as the init voice", (unsigned)FM6_NFAC);
    cu_pool_load(0, ENGI_FM6, pool_pos_factory(ENGI_FM6, 3));
    fm6_poll();
    {
        uint8_t v[FP_SIZE + 1u];
        fm6_unpack(FM6_INIT, v);
        OK(!memcmp(fm6_patch[CR_PART_CHORD], v, FP_SIZE) && fm6_slot[CR_PART_CHORD] == 0,
           "refused: an FM6 factory load plays the init voice (PTCH F1)");
    }
    for (i = 0; i < NELEM(KEPT); i++) {
        e = KEPT[i];
        OK(strcmp(cu_preset_name(e, pool_f0(e)), "NO PRESET") && !strcmp(cu_preset_name(e, pool_f0(e)),
                                                                          ENGINES[e]->presets[pool_f0(e)].name),
           "%s (in the image): its presets as before (%s)", ENGINES[e]->name, ENGINES[e]->presets[pool_f0(e)].name);
    }
    cu_pool_load(0, ENGI_VA, pool_pos_slot(ENGI_VA, 0));
    OK(!strcmp(psnd[0].name, "MY PAD") && !memcmp(&t->p[P_E0], &saved[P_E0], 8 * sizeof saved[0]),
       "refused: U001 saved before loads as it was (MY PAD)");

    /* ---- this build's again: the tables back ---- */
    cr_res2_ok = 1;
    cu_pool_load(0, ENGI_CZ, pool_pos_factory(ENGI_CZ, 1));
    OK(!strcmp(psnd[0].name, CZ_PRESETS[1].name) && kern_pairs(&AF_M) == m_kern,
       "in place again: CZ-1's first factory preset %s, M kerns", CZ_PRESETS[1].name);

    printf("%s: %d of %d checks\n", fails ? "SECOND REGION TEST FAILED" : "second region test passed", checks - fails,
           checks);
    return fails != 0;
}
