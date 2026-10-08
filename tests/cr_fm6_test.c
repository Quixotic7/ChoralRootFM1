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
    trk[1].eng_req = 2;                              /* (PHASE: not FM6) */
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
        OK(fm6u_get(slot, b2) == 0 && !memcmp(b, b2, FM6_BLOB) && fm6u[0].used == 1u << slot,
           "store: its blob in the FM6 patch store (object 1 of 2, slot 5)");
        cu_load(t, ENGI_FM6, 4, 0);                  /* PAD */
        cu_load_user(t, slot, 0);
        OK(!memcmp(fm6_patch[0], v, FP_SIZE) && !memcmp(fm6_fn[0], fn, FM6_NFN) && t->eng_req == ENGI_FM6,
           "store: U05 loads its own patch and function settings (OP 2's LEVEL 0, DX vel on)");
        memset(fm6u, 0, sizeof fm6u);
        up_boot();
        OK(fm6u_get(slot, b2) == 0 && !memcmp(b, b2, FM6_BLOB), "store: from flash after a reboot");
        OK(up_store(20, "UPPER") == 0 && fm6u[1].used == 1u << 4, "store: U21 in the second object");
        cu_load(t, 2, 0, 0);                         /* PHASE */
        up_store(slot, "PHASE NOW");
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
        up_put(20, 0);
        OK(fm6u_get(20, b2) != 0 && !fm6u[1].used, "store: an erased slot clears its blob");
        /* a slot of before the store: Felucca's PTCH numbering (F1..F8, then B1..B27 = 8..34) */
        cu_load(t, ENGI_FM6, 0, 0);
        t->p[P_E7] = 9;                              /* B2 then */
        up_store(slot, "OLD FM6");
        fm6u_put(slot, 0);                           /* no blob: saved before the store */
        cu_load_user(t, slot, 0);
        pe7 = t->p[P_E7];
        fm6_slot_get(FM6_NFAC + 1u, pk);
        fm6_unpack(pk, v);
        OK(pe7 == (int16_t)(FM6_NFAC + 1u) && !memcmp(fm6_patch[0], v, FP_SIZE) && !memcmp(fm6_fn[0], FM6_FNDEF, FM6_NFN),
           "store: a slot without a blob, PTCH 9 (Felucca's B2) -> B2 today (25), Dexed's function settings");
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
