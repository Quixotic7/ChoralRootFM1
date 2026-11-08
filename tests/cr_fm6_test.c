/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of ChoralRoot's FM6 (Melodee's engine, docs/FM6.md) on the firmware as the emulator builds it
 * (tools/emu/emu_firmware.h: hostsim's sound side, the ChoralRoot UI, storage.c on emu_hal_fw.h's RAM NOR):
 *   sh tests/run_cr_tests.sh
 * Checks: the factory patches F1..F24 load (names, PTCH), every one through the 128-byte blob and back bit for bit
 * (voice and function settings), a bad blob = the init voice; a DX7 single voice (VCED) by SysEx -> the part's
 * patch -> the VCED fm6_store.c sends (built here from the patch as fm6_send_voice does: the emulator has no SysEx
 * out) identical, through the USB byte collector (fm6_sx_byte) and fm6_service; a parameter change; a 32-voice
 * bulk dump (VMEM) -> the bank B1..B32, saved to flash, PTCH B n loads it; the deep page table (39 pages: titles,
 * sections, every column's get / set at its min / max / default and the patch byte or function setting it is);
 * the FM6 patch store: a user slot saved with an edited FM6 sound keeps its blob (2 objects), loads it back, survives
 * a reboot, an erase clears it; a user slot of before the store (Felucca's PTCH numbering: B2 = 9) loads B2 (= 25);
 * Felucca 1.0's FM6 bank imported; the voice model: 8 FM6 voices at one budget unit each (FM6_POLY 8), 16 slots. */
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
#include "cr_pool_shim.h"                       /* (0.22: the patches in the sound pool) */

static int fails, checks;
#define CHECK(c, ...)                                                   \
    do {                                                                \
        checks++;                                                       \
        if (!(c)) {                                                     \
            fails++;                                                    \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);               \
            printf(__VA_ARGS__);                                        \
            printf("\n");                                               \
        }                                                               \
    } while (0)
#define OK(c, ...)                                                      \
    do {                                                                \
        int ok_ = (c);                                                  \
        CHECK(ok_, __VA_ARGS__);                                        \
        if (ok_) {                                                      \
            printf("ok   ");                                            \
            printf(__VA_ARGS__);                                        \
            printf("\n");                                               \
        }                                                               \
    } while (0)

static void feed(const uint8_t *b, uint32_t n)       /* a frame through the USB collector, then the main loop */
{
    uint32_t i;
    for (i = 0; i < n; i++)
        fm6_sx_byte(b[i]);
    fm6_service();
}

static uint32_t vced(const uint8_t *v, uint8_t *f)   /* F0 43 00 00 01 1B <155> <checksum> F7 */
{
    static const uint8_t H[6] = {0xF0, 0x43, 0x00, 0x00, 0x01, 0x1B};
    memcpy(f, H, 6);
    memcpy(f + 6, v, 155);
    f[161] = fm6_chk(f + 6, 155);
    f[162] = 0xF7;
    return 163;
}

int main(void)
{
    static uint8_t frame[FM6_RX];
    uint8_t b[FM6_BLOB], b2[FM6_BLOB], v[FP_SIZE + 1u], v2[FP_SIZE + 1u], pk[FM6_PACKED], fn[FM6_NFN];
    track_t *t = &trk[0];
    uint32_t k, i, bad;

    emu_flash_open();
    memset(emu_flash, 0xFF, sizeof emu_flash);
    cr_bank_boot();
    cr_settings_boot();
    cr_ui_init();

    /* ---- the factory patches ---- */
    OK(NELEM(FM6_PRESETS) == 25u && FM6_NFAC == 24u && FM6_NSLOT == 56u,
       "presets: Felucca's 8, Melodee's 16 and ChoralRoot's PIANO (25); PTCH F1..F24, B1..B32");
    {
        static const char *const N[24] = {"TINE EP", "GLASS BELL", "ROUND BASS", "BRASS SECT", "SOFT PAD", "WOOD BARS",
                                          "DRAWBARS", "NYLON PICK", "TINE EP", "BRASS SECT", "SOLID BASS", "BELLS",
                                          "MARIMBA", "CLAVINET", "DRAWBARS", "STRINGS", "GLASS PAD", "SYNC LEAD", "HARP",
                                          "KALIMBA", "FLUTE", "STEEL DRUM", "SAW BASS", "TUBULAR"};
        char nm[12];
        for (k = 0, bad = 0; k < FM6_NFAC; k++) {
            fm6_load_slot(0, k);
            fm6_name(nm, fm6_patch[0]);
            bad += strcmp(nm, N[k]) != 0 || fm6_slot[0] != k;
        }
        OK(!bad, "F1..F24 load: their voice names (Felucca's F1..F8, Melodee's F9..F24)");
    }
    for (k = 0, bad = 0; k < FM6_NFAC; k++) {       /* every factory patch through the blob and back */
        fm6_load_slot(0, k);
        for (i = 0; i < FM6_NFN; i++)
            fm6_fn[0][i] = (uint8_t)((k * 7u + i * 13u) % (FM6_FNMAX[i] + 1u));
        memcpy(v, fm6_patch[0], FP_SIZE);
        memcpy(fn, fm6_fn[0], FM6_NFN);
        t->eng_req = ENGI_FM6;
        fm6_blob_get(t, b);
        fm6_load_slot(0, (k + 1u) % FM6_NFAC);
        memcpy(fm6_fn[0], FM6_FNDEF, FM6_NFN);
        fm6_blob_set(t, b);
        bad += memcmp(v, fm6_patch[0], FP_SIZE) != 0 || memcmp(fn, fm6_fn[0], FM6_NFN) != 0 || !fm6_blob_ok(b);
        fm6_blob_get(t, b2);
        bad += memcmp(b, b2, FM6_BLOB) != 0 || b[112] != 'F' || b[113] != 1u;
    }
    OK(!bad, "blob: 128 bytes (the voice 7-bit packed, 'F' 1, 16 function settings in 64 bits): F1..F24 round trip");
    memcpy(b2, b, FM6_BLOB);
    b2[114 + 7] = 0xFF;                              /* ENGINE 3: out of its range */
    OK(!fm6_blob_ok(b2), "blob: a function setting out of range is refused");
    fm6_blob_set(t, b2);
    fm6_unpack(FM6_INIT, v);
    OK(!memcmp(fm6_patch[0], v, FP_SIZE) && !memcmp(fm6_fn[0], FM6_FNDEF, FM6_NFN), "blob: a bad blob -> the init voice, Dexed's functions");
    fm6_blob_set(t, 0);
    OK(!memcmp(fm6_patch[0], v, FP_SIZE), "blob: none -> the init voice");
    fm6_blob_preset(t, 4);
    fm6_unpack(FM6_FACTORY[4], v);
    OK(!memcmp(fm6_patch[0], v, FP_SIZE), "blob_preset 4 (PAD): F5's patch");

    /* ---- DX7 SysEx: a voice in, the same voice out ---- */
    t->eng_req = ENGI_FM6;
    trk[1].eng_req = 7;                              /* (WHEEL: not FM6) */
    fm6_rom_patch(&FM6_ROM[5], v);                   /* F14 CLAVINET */
    v[FP_ALG] = 12;
    v[5 * FP_OP + FP_OL] = 77;
    memcpy(v + FP_NAME, "SYSEX TEST", 10);
    feed(frame, vced(v, frame));
    OK(!memcmp(fm6_patch[0], v, FP_SIZE), "SysEx in: a VCED (F0 43 00 00 01 1B) -> part 0's patch, as sent");
    {
        uint8_t out[163];
        vced(fm6_patch[0], out);                     /* fm6_send_voice's frame */
        OK(!memcmp(out, frame, 163), "SysEx out: the part's VCED dump is the frame received, byte for byte");
    }
    frame[20] ^= 1;
    feed(frame, 163);
    OK(!memcmp(fm6_patch[0], v, FP_SIZE), "SysEx in: a bad checksum is ignored");
    {
        static const uint8_t P[7] = {0xF0, 0x43, 0x10, 0x01, 0x06, 0x05, 0xF7};   /* voice parameter 134 (ALG) = 5 */
        feed(P, 7);
        OK(fm6_patch[0][FP_ALG] == 5u, "SysEx in: a voice parameter change (ALG -> 6)");
    }
    {
        static const uint8_t F[7] = {0xF0, 0x43, 0x10, 0x08, 0x41, 0x07, 0xF7};   /* function 65: bend range 7 */
        feed(F, 7);
        OK(fm6_fn[0][FN_PBUP] == 7u && fm6_fn[0][FN_PBDN] == 7u, "SysEx in: a function parameter (bend range 7)");
    }

    /* ---- a 32-voice bulk dump -> the bank ---- */
    {
        static const uint8_t H[6] = {0xF0, 0x43, 0x00, 0x09, 0x20, 0x00};
        uint32_t n = 0;
        memcpy(frame, H, 6);
        for (k = 0; k < 32u; k++) {
            fm6_factory(k % FM6_NFAC, frame + 6 + k * 128u);
            frame[6 + k * 128u + 118] = (uint8_t)('A' + k % 26u);   /* (a name of its own) */
        }
        frame[4102] = fm6_chk(frame + 6, 4096);
        frame[4103] = 0xF7;
        feed(frame, FM6_RX);
        for (k = 0, bad = 0; k < 32u; k++) {
            bad += fm6_bank_get(k, pk) != 0 || memcmp(pk, frame + 6 + k * 128u, 128) != 0;
            n += fm6_bank_used(k);
        }
        OK(!bad && n == 32u, "SysEx in: a VMEM bulk dump (F0 43 00 09 20 00) -> the bank B1..B32, each record as sent");
        memset(&fm6_bank, 0, sizeof fm6_bank);
        fm6_bank_boot();
        OK(fm6_bank_get(31, pk) == 0 && !memcmp(pk, frame + 6 + 31 * 128u, 128), "the bank saved to flash (B32 after a reload)");
        t->p[P_E7] = (int16_t)(FM6_NFAC + 2u);
        fm6_poll();
        fm6_unpack(frame + 6 + 2 * 128u, v);
        OK(!memcmp(fm6_patch[0], v, FP_SIZE) && fm6_slot[0] == FM6_NFAC + 2u, "PTCH B3 (26): the bank's third voice");
    }

    /* ---- the deep pages ---- */
    {
        const eng_deep_t *d = ENGINES[ENGI_FM6]->deep;
        static const char *const T[] = {"OP 1", "OP 6", "OP 1+", "OP 6+", "SCALE 1", "SCALE 6", "ALGO", "ENV 1",
                                        "ENV 1+", "ENV 6+", "PITCH EG", "PITCH EG+", "LFO", "LFO+", "FUNC", "FUNC+",
                                        "CTRL", "CTRL+"};
        static const uint8_t TI[] = {0, 5, 6, 11, 12, 17, 18, 19, 20, 30, 31, 32, 33, 34, 35, 36, 37, 38};
        uint32_t pg, c, cols = 0;
        for (k = 0, bad = 0; k < NELEM(T); k++)
            bad += strcmp(d->pages[TI[k]].title, T[k]) != 0;
        OK(d && d->npages == 39u && !bad && d->section[0] == 0 && d->section[1] == 18 && d->section[2] == 19 &&
           d->section[3] == 33 && d->section[4] == 35 && d->section[5] == 0xFF && d->blob_size == 128u,
           "deep: 39 pages, sections OSC 0 (OP n, OP n+, SCALE n) FILTER 18 (ALGO) ENV 19 (ENV n, PITCH EG) LFO 33 MOD 35");
        fm6_load_slot(0, 0);
        memcpy(fm6_fn[0], FM6_FNDEF, FM6_NFN);
        for (pg = 0, bad = 0; pg < d->npages; pg++)
            for (c = 0; c < 4u; c++) {
                const param_desc_t *p = &d->pages[pg].col[c];
                int32_t vals[3], r = fm6_dref(pg, c);
                if (!p->label) {
                    bad += r >= 0;
                    continue;
                }
                cols++;
                bad += r < 0;
                vals[0] = p->min;
                vals[1] = p->max;
                vals[2] = p->def;
                for (i = 0; i < 3u; i++) {
                    d->set(t, pg, c, vals[i]);
                    bad += d->get(t, pg, c) != vals[i];
                    if (r >= 0 && r < 155)
                        bad += fm6_patch[0][r] != vals[i];
                }
            }
        OK(!bad && cols == 154u, "deep: every column (%u) set to its min, max and default reads it back, in the patch byte it names",
           (unsigned)cols);
        d->set(t, 0, 0, 33);                         /* OP 1 LEVEL: the sixth record of the patch */
        d->set(t, 5, 2, 44);                         /* OP 6 FINE: the first */
        d->set(t, 12 + 2, 3, 2 * 4 + 3);             /* SCALE 3 CURVE: +EXP left, +LIN right */
        d->set(t, 19 + 2 * 3 + 1, 3, 12);            /* ENV 4+ L4 */
        d->set(t, 35, 0, 9);                         /* FUNC BEND: up and down */
        d->set(t, 37, 1, 6);                         /* CTRL W.DST: AE */
        OK(fm6_patch[0][5 * FP_OP + FP_OL] == 33 && fm6_patch[0][FP_FF] == 44 && fm6_patch[0][3 * FP_OP + FP_LC] == 2 &&
           fm6_patch[0][3 * FP_OP + FP_RC] == 3 && fm6_patch[0][2 * FP_OP + FP_L1 + 3] == 12 && fm6_fn[0][FN_PBUP] == 9 &&
           fm6_fn[0][FN_PBDN] == 9 && fm6_fn[0][FN_MWA] == 6, "deep: OP n is record 6 - n; CURVE = L x 4 + R; BEND both ways");
        OK(d->mod_dst(t, 0, 0) == -1 && d->mod_dst(t, ENG_MOD_TRK, P_LEVEL) == -1, "deep: no matrix destination (not modulatable)");
        d->set(t, 6, 0, 1);                          /* OP 1+ MODE: FIXED */
        OK(d->desc(t, 0, 1) && d->desc(t, 0, 1)->names == N_FM6D_FIX && !d->desc(t, 1, 1),
           "deep: COARSE of a FIXED operator reads as its decade (1Hz .. 1kHz)");
    }

    /* ---- the FM6 patch store: a user slot keeps its blob ---- */
    {
        uint32_t slot = 4;                           /* U05 */
        int16_t pe7;
        cu_load(t, ENGI_FM6, 0, 0);                  /* TINE EP */
        ENGINES[ENGI_FM6]->deep->set(t, 1, 0, 0);    /* OP 2 LEVEL 0 */
        ENGINES[ENGI_FM6]->deep->set(t, 36, 3, 1);   /* DXVEL on */
        memcpy(v, fm6_patch[0], FP_SIZE);
        memcpy(fn, fm6_fn[0], FM6_NFN);
        fm6_blob_get(t, b);
        k = (uint32_t)up_store(slot, "FM6 EDIT");
        OK(k == 0u || k == 3u, "store: U05 saved (an FM6 sound)");
        OK(fm6u_get(slot, b2) == 0 && !memcmp(b, b2, FM6_BLOB) && pool_used_of(ENGI_FM6, 0) == 1u << slot,
           "store: its blob in the FM6 patch store (object 1 of 2, slot 5)");
        cu_load(t, ENGI_FM6, 4, 0);                  /* PAD */
        cu_load_user(t, slot, 0);
        OK(!memcmp(fm6_patch[0], v, FP_SIZE) && !memcmp(fm6_fn[0], fn, FM6_NFN) && t->eng_req == ENGI_FM6,
           "store: U05 loads its own patch and function settings (OP 2's LEVEL 0, DX vel on)");
        memset(upn, 0, sizeof upn);
        up_boot();
        OK(fm6u_get(slot, b2) == 0 && !memcmp(b, b2, FM6_BLOB), "store: from flash after a reboot");
        OK(up_store(20, "UPPER") == 0 && pool_used_of(ENGI_FM6, 1) == 1u << 4, "store: U21 in the second object");
        cu_load(t, 7, 0, 0);                         /* WHEEL (PHASE until 0.18: retired) */
        up_store(slot, "WHEEL NOW");
        OK(fm6u_get(slot, b2) != 0, "store: another engine's sound over U05 clears its blob");
        {   /* an ANALOG record (engine 0, retired in 0.14: a 0.13 slot) bound to its STRINGS: the VA's pool, added,
             * its macros on the VA by name (cr_ui.c cu_analog_va) */
            up_rec_t ar = *up_rec(slot);
            ar.engine = 0;
            up_set_value(&ar, P_E0 + 4, 50);         /* CUT */
            up_set_value(&ar, P_E0 + 5, 100);        /* RES */
            up_set_value(&ar, P_E0 + 2, 30);         /* MIX */
            up_set_value(&ar, P_ATK, 7);
            ar.note[15] = CB_BIND_MARK;
            ar.flags[15] = 12;                       /* (ANALOG's STRINGS: the VA's 12th is HOLLOW) */
            up_put(slot, &ar);
            cu_load(t, ENGI_FM6, 0, 0);
            cu_load_user(t, slot, 0);
            OK(t->eng_req == ENGI_VA && t->p[P_E0] == 50 && t->p[P_E0 + 1] == 100 &&
               t->p[P_E0 + 6] == 7 && cb_rec_engine(slot) == ENGI_VA && cb_bound(slot) == PF_NONE &&
               pool_bound_slot(ENGI_VA, 11) == UP_SLOTS,
               "an ANALOG record: on the VA with CUT RES ATK carried (MIX: the VA patch's own scale), an added preset of the VA's pool (%d %d %d %d %d, %u %u %u)", t->eng_req, t->p[P_E0], t->p[P_E0 + 1], t->p[P_E0 + 4], t->p[P_E0 + 6], (unsigned)cb_rec_engine(slot), (unsigned)cb_bound(slot), (unsigned)pool_bound_slot(ENGI_VA, 11));
        }
        {   /* a record of an engine retired in 0.18 (PHASE 2, LOFI 3, VOICE 5, TRIO 6, NOISE 11, PHYS 9: a 0.17 slot): the VA's
             * pool, added whatever it binds, the VA's init sound with the record's ATK / REL (a stand-in; its sends are
             * not loaded: since 0.18 the sends are the part's, here 61 62 63 64 before and after);
             * the record itself untouched. SAMPLE's (4, a Felucca slot): the plain init sound, as before */
            static const uint8_t FIVE[] = {2, 3, 5, 6, 11, ENGI_PHYS};   /* (six since PHYS went) */
            uint32_t q, good = 0;
            for (q = 0; q < sizeof FIVE; q++) {
                up_rec_t pr = *up_rec(slot), back;
                pr.engine = FIVE[q];
                up_set_value(&pr, P_E0, 99);         /* (the engine's own macro: not the VA's CUT) */
                up_set_value(&pr, P_ATK, 31);
                up_set_value(&pr, P_REL, 77);
                up_set_value(&pr, P_DIST, 5);
                up_set_value(&pr, P_CHOR, 11);
                up_set_value(&pr, P_DLY, 22);
                up_set_value(&pr, P_REV, 33);
                pr.note[15] = CB_BIND_MARK;
                pr.flags[15] = 2;                    /* (its engine's second factory preset then) */
                up_put(slot, &pr);
                t->p[P_DIST] = 61; t->p[P_CHOR] = 62; t->p[P_DLY] = 63; t->p[P_REV] = 64;   /* the part's sends */
                cu_load(t, ENGI_FM6, 0, 0);
                cu_load_user(t, slot, 0);
                back = *up_rec(slot);
                good += !eng_ok(FIVE[q]) && t->eng_req == ENGI_VA && t->p[P_E0] == ENG_VA.edit[0].def &&
                        t->p[P_E0 + 6] == 31 && t->p[P_E0 + 7] == 77 && t->p[P_DIST] == 61 && t->p[P_CHOR] == 62 &&
                        t->p[P_DLY] == 63 && t->p[P_REV] == 64 && t->user == slot + 1u &&
                        cb_rec_engine(slot) == ENGI_VA && cb_bound(slot) == PF_NONE &&
                        pool_pos_slot(ENGI_VA, slot) > pool_nf(ENGI_VA) && back.engine == FIVE[q] &&
                        !memcmp(&back, &pr, sizeof pr);
            }
            OK(good == sizeof FIVE, "a PHASE / LOFI / VOICE / TRIO / NOISE / PHYS record (retired in 0.18): the VA's init sound with its ATK, REL, the part's sends untouched by a factory or a user load; an added preset of the VA's pool; the record kept (%u of 5)", (unsigned)good);
            {
                up_rec_t sr = *up_rec(slot);
                sr.engine = 4;
                up_put(slot, &sr);
                cu_load(t, ENGI_FM6, 0, 0);
                cu_load_user(t, slot, 0);
                OK(t->eng_req == ENGI_VA && t->p[P_E0 + 6] == ENG_VA.edit[6].def && t->p[P_REV] == 64,
                   "a SAMPLE record (a Felucca slot): the VA's plain init sound, as before (the part's sends stay)");
            }
#if FELUCCA_PROPHET
            OK(NENG_SHOWN == 7 && eng_vis(0) == ENGI_FM6 && eng_vis(1) == ENGI_QUAD && eng_vis(2) == ENGI_VA &&
               eng_vis(3) == ENGI_CZ && eng_vis(4) == ENGI_PROPHET && eng_vis(5) == 7u && eng_vis(6) == ENGI_GRAIN &&
               !eng_ok(ENGI_PHYS) && eng_step(7u, 1) == ENGI_GRAIN && eng_step(ENGI_GRAIN, 1) == ENGI_FM6 &&
               eng_rank(ENGI_PHYS) == 0u && ENGINES[ENGI_PHYS]->npresets == 0u,
               "seven engines shown: FM6, FM TONE, VA, CZ-1, P5, WHEEL, GRAIN (PHYS, engine 9, retired: a silent slot without presets, FM6's place)");
#else
            OK(NENG_SHOWN == 6 && eng_vis(0) == ENGI_FM6 && eng_vis(1) == ENGI_QUAD && eng_vis(2) == ENGI_VA &&
               eng_vis(3) == ENGI_CZ && eng_vis(4) == 7u && eng_vis(5) == ENGI_GRAIN && !eng_ok(ENGI_PHYS) &&
               eng_step(7u, 1) == ENGI_GRAIN && eng_step(ENGI_GRAIN, 1) == ENGI_FM6 &&
               eng_rank(ENGI_PHYS) == 0u && ENGINES[ENGI_PHYS]->npresets == 0u,
               "six engines shown: FM6, FM TONE, VA, CZ-1, WHEEL, GRAIN (PHYS, engine 9, retired: a silent slot without presets, FM6's place)");
#endif
            {   /* the message a PHYS record's load shows, as the five's */
                up_rec_t pr = *up_rec(slot);
                pr.engine = ENGI_PHYS;
                up_put(slot, &pr);
                cu_load(t, ENGI_FM6, 0, 0);
                cu_load_user(t, slot, 0);
                OK(!strcmp(cu.msg.text, "PHYS retired: stand-in") && t->eng_req == ENGI_VA,
                   "a PHYS record's load says \"PHYS retired: stand-in\" (\"%s\")", cu.msg.text);
            }
        }
        up_put(20, 0);
        OK(fm6u_get(20, b2) != 0 && !pool_used_of(ENGI_FM6, 1), "store: an erased slot clears its blob");
        /* a slot of before the store: Felucca's PTCH numbering (F1..F8, then B1..B27 = 8..34) */
        cu_load(t, ENGI_FM6, 0, 0);
        t->p[P_E7] = 9;                              /* B2 then */
        up_store(slot, "OLD FM6");
        pool_patch_clear(slot);                           /* no blob: saved before the store */
        cu_load_user(t, slot, 0);
        pe7 = t->p[P_E7];
        fm6_slot_get(FM6_NFAC + 1u, pk);
        fm6_unpack(pk, v);
        OK(pe7 == (int16_t)(FM6_NFAC + 1u) && !memcmp(fm6_patch[0], v, FP_SIZE) && !memcmp(fm6_fn[0], FM6_FNDEF, FM6_NFN),
           "store: a slot without a blob, PTCH 9 (Felucca's B2) -> B2 today (25), Dexed's function settings");
    }

    /* ---- FM TONE's Drive is its preset's (docs/FX.md "FM TONE's Drive"; cr_ui.c cu_drive_load) ---- */
    {
        track_t *c = &trk[CR_PART_CHORD], *bs = &trk[CR_PART_BASS];
        const eng_deep_t *qd = ENGINES[ENGI_QUAD]->deep;
        static cr_session_t x, y;
        up_rec_t r;
        uint32_t slot = cb_free_slot(), good, nz;
        int v29 = -1;
        /* the parts' own drive sends: 40 (chord, FX on) and 33 (bass), on FM6 and the VA */
        cu_load(c, ENGI_FM6, 0, 0);
        cu_load(bs, ENGI_VA, 0, 1);
        cu_send_set(0, P_DIST, 40);
        cu_send_set(1, P_DIST, 33);
        OK(cs.fx_on && c->p[P_DIST] == 40 && bs->p[P_DIST] == 33 && cu_send(0, P_DIST) == 40 && cu_send(1, P_DIST) == 33 &&
           !psnd[0].edited && !psnd[1].edited,
           "drive: another engine's is the part's send (chord 40, bass 33), not an edit of the sound");
        /* every factory preset, on both parts: the Drive of its table row; the parts' sends wait */
        for (k = 0, good = 0, nz = 0; k < QUAD_NPRESETS; k++) {
            int32_t d = cu_preset_drive(&QUAD_PRESETS[k]);
            cu_load(c, ENGI_QUAD, k, 0);
            cu_load(bs, ENGI_QUAD, k, 1);
            nz += d != 0;
            good += c->p[P_DIST] == d && bs->p[P_DIST] == d && cu_send(0, P_DIST) == d && cu_send(1, P_DIST) == d &&
                    qd->get(c, 13, 2) == d && cs.fx_amt[3] == 40 && cu_bdrive == 33 && *cu_bdrive_p() == 33;
        }
        OK(good == 16u && nz == 1u && cu_preset_drive(&QUAD_PRESETS[9]) == 10,
           "drive: each of the 16 FM TONE presets loads its own Drive on either part (WOBBLE 10, the other 15: 0); the parts' sends wait (40, 33) (%u, %u)",
           (unsigned)good, (unsigned)nz);
        /* the AMP cell and the Sends knob are one value, an edit of the sound; FX off does not close it, and turning
         * it does not switch FX on */
        cu_load(c, ENGI_QUAD, 9, 0);
        cs.fx_on = 0;
        cu_fx_apply();
        good = c->p[P_DIST] == 10 && c->p[P_REV] == 0 && c->p[P_CHOR] == 0 && c->p[P_DLY] == 0 && !psnd[0].edited;
        cu_send_set(0, P_DIST, 91);
        good += c->p[P_DIST] == 91 && qd->get(c, 13, 2) == 91 && cu_send(0, P_DIST) == 91 && psnd[0].edited && !cs.fx_on &&
                cs.fx_amt[3] == 40 && !c->user;
        cu_fx_toggle();
        cu_fx_toggle();
        good += c->p[P_DIST] == 91 && !cs.fx_on;
        OK(good == 3u, "drive: FM TONE, FX off: the Drive stays in the track (10; the three sends 0); turned to 91: the AMP cell's value, the sound edited, FX still off, the part's send still 40 (%u of 3)", (unsigned)good);
        /* the store round trip: saved with the sound, loaded with it, on either part */
        OK(slot < UP_SLOTS && cb_store(c, 0, slot, "DRV91", 0) != 1 && up_used(slot) && up_rec(slot)->engine == ENGI_QUAD &&
           up_value(up_rec(slot), P_DIST) == 91, "drive: saved: the record's P_DIST field holds 91 (slot %u)", (unsigned)slot + 1u);
        cu_load(c, ENGI_QUAD, 0, 0);
        good = c->p[P_DIST] == 0;
        cu_load_user(c, slot, 0);
        good += c->p[P_DIST] == 91 && !psnd[0].edited && c->user == slot + 1u;
        cu_load(bs, ENGI_VA, 0, 1);
        good += bs->p[P_DIST] == 33;
        cu_load_user(bs, slot, 1);
        good += bs->p[P_DIST] == 91 && cu_bdrive == 33;
        OK(good == 4u, "drive: 01 EP: 0; the saved sound again: 91, not edited; on the bass part (from the VA, its send 33): 91, the send waits (%u of 4)", (unsigned)good);
        /* an old record: the layout of an older firmware (fewer common parameters: np smaller, mapped by count), its
         * P_DIST byte as that firmware left it (the part's drive when it was saved: 23) */
        memset(&r, 0, sizeof r);
        r.used = UP_USED;
        r.ver = UP_VER;
        r.engine = ENGI_QUAD;
        r.np = (uint8_t)(P_COUNT - 5u);
        memcpy(r.name, "OLD", 3);
        for (k = 0; k < r.np; k++)
            up_set_value(&r, k, k < r.np - 8u ? TP[k].def : (int16_t)QUAD_PRESETS[1].e[k - (r.np - 8u)]);
        up_set_value(&r, P_DIST, 23);
        up_set_value(&r, P_REV, 99);
        up_put(slot, &r);
        cu_load(c, ENGI_QUAD, 0, 0);
        cu_load_user(c, slot, 0);
        OK(up_valid(&r) && c->eng_req == ENGI_QUAD && c->p[P_DIST] == 23 && c->p[P_REV] == 0 && cs.fx_amt[0] != 99 &&
           c->p[P_E0 + 2] == QUAD_PRESETS[1].e[2],
           "drive: an older record (np %u): its P_DIST byte is the Drive (23); its other send fields are still not loaded", (unsigned)r.np);
        /* leaving FM TONE: the part's send is back (the chord part's gated by FX), whatever the sound's Drive was;
         * a record of another engine brings no drive */
        r.engine = ENGI_FM6;
        r.np = P_COUNT;
        for (k = 0; k < P_COUNT; k++)
            up_set_value(&r, k, k < P_E0 ? TP[k].def : 0);
        up_set_value(&r, P_DIST, 55);
        up_put(slot, &r);
        cu_load_user(c, slot, 0);
        good = c->eng_req == ENGI_FM6 && c->p[P_DIST] == 0 && cu_send(0, P_DIST) == 40;       /* (FX is off) */
        cu_fx_toggle();
        good += c->p[P_DIST] == 40;
        cu_load(bs, ENGI_VA, 0, 1);
        good += bs->p[P_DIST] == 33;
        cu_engine_switch(0, ENGI_QUAD);
        cu_engine_switch(1, ENGI_QUAD);
        good += c->p[P_DIST] == 0 && bs->p[P_DIST] == 0 && cs.fx_amt[3] == 40 && cu_bdrive == 33;
        cu_send_set(1, P_DIST, 17);
        good += bs->p[P_DIST] == 17 && psnd[1].edited && cu_bdrive == 33;
        cu_engine_switch(0, ENGI_VA);
        cu_engine_switch(1, ENGI_VA);
        good += c->p[P_DIST] == 40 && bs->p[P_DIST] == 33;
        cu_init_load(0, ENGI_QUAD);
        good += c->p[P_DIST] == 0 && cs.fx_amt[3] == 40;
        up_put(slot, 0);
        OK(good == 7u, "drive: an FM6 record (its field 55): the part's send, 0 with FX off, 40 with FX on; the bass part back on the VA: 33; the engine switched to FM TONE: 01 EP's 0, back: 40 / 33; FM TONE's INIT: 0 (%u of 7)", (unsigned)good);
        /* the session: the sounds' Drives (edited, saved to no preset) in the parts' p[], the parts' sends beside them */
        cu_load(c, ENGI_QUAD, 9, 0);
        cu_load(bs, ENGI_QUAD, 0, 1);
        cu_send_set(0, P_DIST, 91);
        cu_send_set(1, P_DIST, 17);
        cu_fx_toggle();                               /* off */
        css_capture(&x);
        OK(!cs.fx_on && x.part[0].p[P_DIST] == 91 && x.part[1].p[P_DIST] == 17 && x.fx.amt[CSS_FXE_DRIVE] == 40 &&
           cr_session_fx_get(&x, CSS_FXP_BS_DRIVE, &v29) && v29 == 33 && x.part[0].edited && x.part[1].edited,
           "drive: the session record: the FM TONE sounds' Drives 91 / 17 in the parts' parameters, the parts' sends 40 / 33 under their tags (bass tag: %d)", v29);
        cu_load(c, ENGI_FM6, 0, 0);
        cu_load(bs, ENGI_VA, 0, 1);
        cu_sends_default();
        good = c->p[P_DIST] == 0 && bs->p[P_DIST] == 0;
        css_apply(&x);
        css_capture(&y);
        good += c->p[P_DIST] == 91 && bs->p[P_DIST] == 17 && cs.fx_amt[3] == 40 && cu_bdrive == 33 && !cs.fx_on &&
                cr_session_hash(&x) == cr_session_hash(&y);
        cu_load(c, ENGI_FM6, 0, 0);
        cu_load(bs, ENGI_VA, 0, 1);
        good += c->p[P_DIST] == 0 && bs->p[P_DIST] == 33;
        OK(good == 3u, "drive: the session applied over other sounds: 91 / 17 again with FX off, the same record captured; the parts' sends 40 / 33 behind them (%u of 3)", (unsigned)good);
        /* a session written before this change (main's): an FM TONE chord part's p[P_DIST] is what cu_fx_apply had
         * left there (the amount, or 0 with FX off: what sounded), the bass part's tag and its p[P_DIST] the same value */
        x.part[0].p[P_DIST] = 0;
        x.part[1].p[P_DIST] = 21;
        cr_session_fx_set(&x, CSS_FXP_BS_DRIVE, 21);
        css_apply(&x);
        OK(c->eng_req == ENGI_QUAD && c->p[P_DIST] == 0 && cs.fx_amt[3] == 40 && bs->p[P_DIST] == 21 && cu_bdrive == 21,
           "drive: an older session (FX off, FM TONE on both parts): the chord sound's Drive 0 as it sounded, its send 40 kept; the bass sound's 21, and 21 the part's send");
        /* 3y270, the Filter's choice (tag 34, xfp[XP_FON]): 0.22's On (1) loads as All, Chord (2) and Bass (3) load
         * and are captured again, a value past Bass leaves what is there; with the Filter Off, a cutoff turned
         * switches it to All, and with Chord a cutoff turned keeps Chord */
        {
            uint32_t fg = 0;
            int vf = -1;
            uint8_t fon = cs.fx_on, fpg = cs.fx_page;
            xfp[XP_FON] = 0;
            cr_session_fx_set(&x, CSS_FXP_XQ_ON, 1);
            css_apply(&x);
            fg += xfp[XP_FON] == 1;
            cr_session_fx_set(&x, CSS_FXP_XQ_ON, 2);
            css_apply(&x);
            css_capture(&y);
            fg += xfp[XP_FON] == 2 && cr_session_fx_get(&y, CSS_FXP_XQ_ON, &vf) && vf == 2;
            cr_session_fx_set(&x, CSS_FXP_XQ_ON, 3);
            css_apply(&x);
            fg += xfp[XP_FON] == 3;
            cr_session_fx_set(&x, CSS_FXP_XQ_ON, 4);
            css_apply(&x);
            fg += xfp[XP_FON] == 3;
            xfp[XP_FON] = 0;
            xfp[XP_FHIGH] = 127;
            cs.fx_page = CU_FXPG_FILTER;
            cu_layer_knob(L_FX, 1u, -1);                    /* (KNOB 2: the high cut in) */
            fg += xfp[XP_FON] == 1 && xfp[XP_FHIGH] < 127;
            xfp[XP_FON] = 2;
            cu_layer_knob(L_FX, 1u, -1);
            fg += xfp[XP_FON] == 2;
            OK(fg == 6u, "filter: the session's tag 34: 1 (0.22's On) loads as All, 2 / 3 load and are captured, 4 is refused; a cutoff turned while Off: All, while Chord: Chord (%u of 6)", (unsigned)fg);
            xfp[XP_FON] = 0;
            xfp[XP_FHIGH] = 127;
            cr_session_fx_set(&x, CSS_FXP_XQ_ON, 0);
            if (cs.fx_on != fon)
                cu_fx_toggle();
            cs.fx_page = fpg;
        }
        cu_fx_toggle();                               /* FX on again, the power-on sounds, a new unit's sends */
        cu_load(c, ENGI_FM6, 0, 0);
        cu_load(bs, ENGI_VA, 0, 1);
        cu_sends_default();
        psnd[0].edited = psnd[1].edited = 0;
    }

    /* ---- the voice model ---- */
    {
        uint32_t busy;
        cu_load(t, ENGI_FM6, 0, 0);
        t->engine = ENGI_FM6;
        for (k = 0; k < 10u; k++)
            trk_note_on(t, 48u + 3u * k, 100);
        for (k = 0, busy = 0; k < NVOICE; k++)
            busy += t->v[k].active && t->v[k].stage != 4u;
        OK(NVOICE == 16u && trk_nvoice(t) == 8u && busy == 8u && voices_busy() == 8u && voice_units(t) == 1u,
           "voices: 16 slots a part, FM6 capped at 8 (FM6_POLY), one budget unit each (busy %u, units %u)", (unsigned)busy,
           (unsigned)voices_busy());
        for (k = 0; k < 10u; k++)
            trk_note_off(t, 48u + 3u * k);
    }

    printf("%s: %d checks, %d failed\n", fails ? "CR FM6 TESTS FAILED" : "cr fm6 tests passed", checks, fails);
    return fails != 0;
}
