/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of ChoralRoot's CZ-1 (Melodee's engine, docs/CZ1.md) on the firmware as the emulator builds it
 * (tools/emu/emu_firmware.h: hostsim's sound side, the ChoralRoot UI, storage.c on emu_hal_fw.h's RAM NOR):
 *   sh tests/run_cr_tests.sh
 * Checks: the engine's place (14, after PHASE in the picker order), Casio's 64 tones (valid, A-1 BRASS 1 .. H-8
 * TYPHOON, the PRESETS names); every factory tone through the 144-byte blob and back, a bad blob = the init tone;
 * every factory tone rendered (three notes, velocity 127 and 1, held then released) without overflow and not
 * silent; Casio SysEx: a 144-byte tone dump (7n 30) -> the part's tone -> the dump cz_store.c sends on a send
 * request (7n 11 60) identical; a 128-byte (CZ-101) dump gets the CZ-1 defaults; a bad dump is refused; the banks
 * (A..D Casio's, E..H empty; BANK / PTCH load their tone; a bank exported and imported through the backup's objects
 * 14..21); the deep page table (37 pages: titles, sections, every column's get / set at its min / max / default, the
 * tone bytes it is); the tone store (a user slot keeps its tone in two objects, loads it back, survives a reboot, an
 * erase clears it, a slot of no tone loads its BANK / PTCH); the backup's CZ-1 objects (12, 13, 14..21). */
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

/* the frames cz_store.c sends (the device: ota_wire_send) */
static uint8_t sx_out[600];
static uint32_t sx_out_n, sx_out_count;
#define CZ_SEND(p, n) (memcpy(sx_out, (p), (n)), sx_out_n = (n), sx_out_count++)
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
        cz_sx_byte(b[i]);
    cz_service();
}

static uint32_t dump(const uint8_t *raw, uint32_t nb, uint8_t *f)   /* F0 44 00 00 70 30 <2 nb nibbles> F7 */
{
    static const uint8_t H[6] = {0xF0, 0x44, 0x00, 0x00, 0x70, 0x30};
    uint32_t j;
    memcpy(f, H, 6);
    for (j = 0; j < nb; j++) {
        f[6 + 2 * j] = raw[j] & 15u;
        f[7 + 2 * j] = raw[j] >> 4;
    }
    f[6 + 2 * nb] = 0xF7;
    return 2 * nb + 7;
}

/* tone k rendered on its own (cz_native_render): the largest |sample| of three notes held 1 s, released 1 s */
static int32_t render_peak(track_t *t, uint32_t vel)
{
    static const uint8_t N[3] = {36, 60, 84};
    vmod_t m;
    int32_t out[CTL], peak = 0;
    uint32_t k, b, i;
    memset(t->v, 0, sizeof t->v);
    memset(&m, 0, sizeof m);
    m.amp0 = m.amp1 = 32767;
    m.shape = 64 << 8;                               /* (the neutral SHAPE: cz_native_render adds cutoff + shape - 64) */
    for (k = 0; k < 3u; k++) {
        t->v[k].note = N[k];
        t->v[k].vel = (uint8_t)vel;
        t->v[k].gate = 1;
        t->v[k].active = 1;
        voice_was = 0;
        ENG_CZ.note_on(t, &t->v[k]);
    }
    for (b = 0; b < 2u * FS / CTL; b++) {
        memset(out, 0, sizeof out);
        for (k = 0; k < 3u; k++) {
            if (b == FS / CTL)
                t->v[k].gate = 0;
            m.pitch16 = (int32_t)N[k] * 16;
            m.inc = pitch_inc((uint32_t)m.pitch16);
            ENG_CZ.render(t, &t->v[k], out, CTL, &m);
        }
        for (i = 0; i < CTL; i++)
            if ((out[i] < 0 ? -out[i] : out[i]) > peak)
                peak = out[i] < 0 ? -out[i] : out[i];
    }
    memset(t->v, 0, sizeof t->v);
    return peak;
}

int main(void)
{
    static uint8_t frame[600];
    uint8_t raw[CZ_BYTES], raw2[CZ_BYTES], b[CZ_BYTES], b2[CZ_BYTES];
    track_t *t = &trk[0];
    uint32_t k, i, bad;

    emu_flash_open();
    memset(emu_flash, 0xFF, sizeof emu_flash);
    cr_bank_boot();
    cr_settings_boot();
    cr_ui_init();

    /* ---- the engine and its tones ---- */
    for (k = 0, bad = 1; k < NENG_SHOWN; k++)
        if (ENGINE_ORDER[k] == 2u)
            bad = !(k + 1u < NENG_SHOWN && ENGINE_ORDER[k + 1u] == ENGI_CZ);
    OK(ENGI_CZ == 14u && NENGINES == 15u + FELUCCA_QUAD && ENGINES[ENGI_CZ] == &ENG_CZ && !strcmp(ENG_CZ.name, "CZ-1") && eng_ok(ENGI_CZ) &&
       !bad, "engine: CZ-1 is 14 (after VA's 13; Melodee: 15), shown after PHASE");
    for (k = 0, bad = 0; k < CZ_FACTORY_N; k++)
        bad += !cz_patch_valid(CZ_FACTORY[k]);
    OK(CZ_FACTORY_N == 64u && !bad && NELEM(CZ_PRESETS) == 65u && !strcmp(CZ_PRESETS[0].name, "INIT TONE") &&
       !strcmp(CZ_PRESETS[1].name, "BRASS 1") && !strcmp(CZ_PRESETS[64].name, "TYPHOON") &&
       !memcmp(CZ_FACTORY[0] + 128, "    BRASS 1     ", 16) && CZ_PRESETS[25].e[0] == 1 && CZ_PRESETS[25].e[1] == 9,
       "tones: Casio's 64 (A-1 BRASS 1 .. H-8 TYPHOON) valid; PRESETS = INIT TONE + the 64, BANK / PTCH A..D 1..16");

    /* ---- the blob ---- */
    t->eng_req = ENGI_CZ;
    for (k = 0, bad = 0; k <= CZ_FACTORY_N; k++) {
        cz_blob_preset(t, k);
        cz_blob_get(t, b);
        cz_factory_tone(k, raw);
        bad += memcmp(b, raw, CZ_BYTES) != 0;
        cz_blob_preset(t, (k + 7u) % 65u);
        cz_blob_set(t, b);
        bad += memcmp(cz_patch[0].raw, raw, CZ_BYTES) != 0;
    }
    OK(!bad && ENG_CZ.deep->blob_size == 144u && ENG_CZ.deep->blob_size <= ENG_BLOB_MAX,
       "blob: the 144-byte tone (128 synthesis bytes + the LCD name): INIT TONE and the 64 round trip");
    memcpy(b2, b, CZ_BYTES);
    b2[3] = 48;                                      /* a detune past 47 */
    cz_blob_set(t, b2);
    cz_patch_init(raw);
    OK(!memcmp(cz_patch[0].raw, raw, CZ_BYTES), "blob: a bad tone -> the init tone");

    /* ---- every factory tone renders ---- */
    {
        int32_t pk, mx = 0, mn = 0x7FFFFFFF;
        uint32_t silent = 0;
        for (k = 1, bad = 0; k <= CZ_FACTORY_N; k++) {
            cz_blob_preset(t, k);
            pk = render_peak(t, 127);
            bad += pk >= 4 * VOICE_FS * 3;           /* (three voices at most at full scale each) */
            silent += pk < 64;
            mx = pk > mx ? pk : mx;
            mn = pk < mn ? pk : mn;
            pk = render_peak(t, 1);
            bad += pk >= 4 * VOICE_FS * 3;
        }
        OK(!bad && !silent, "render: the 64 tones (3 notes, velocity 127 / 1, held 1 s, released 1 s) within range, none "
                            "silent (peaks %d .. %d of %d)", (int)mn, (int)mx, 4 * VOICE_FS * 3);
    }

    /* ---- Casio SysEx ---- */
    cu.page = PG_NONE;
    trk[0].eng_req = ENGI_CZ;
    trk[1].eng_req = 0;
    memcpy(raw, CZ_FACTORY[24], CZ_BYTES);           /* D-1 PIANO 1 */
    raw[3] = 7;                                      /* (a tone of its own: detune 7) */
    memcpy(raw + 128, " SYSEX  TEST    ", 16);
    k = dump(raw, CZ_BYTES, frame);
    feed(frame, k);
    OK(!memcmp(cz_patch[0].raw, raw, CZ_BYTES) && t->p[P_E1] == 0 && psnd[0].edited,
       "SysEx in: a CZ-1 tone dump (F0 44 00 00 70 30, 288 nibbles) -> the chord part's tone, as sent (edited)");
    {
        static const uint8_t R[8] = {0xF0, 0x44, 0x00, 0x00, 0x70, 0x11, 0x60, 0xF7};
        sx_out_count = 0;
        feed(R, 8);
        OK(sx_out_count == 1u && sx_out_n == k && !memcmp(sx_out, frame, k),
           "SysEx out: a send request (70 11 60) -> the part's tone dump, the frame received byte for byte");
        memcpy(b, sx_out, sx_out_n);
    }
    {
        static const uint8_t R[8] = {0xF0, 0x44, 0x00, 0x00, 0x70, 0x10, 0x60, 0xF7};
        uint8_t f128[300];
        feed(R, 8);                                  /* the 128-byte (CZ-101) dump */
        memcpy(f128, sx_out, sx_out_n);
        OK(sx_out_n == 263u && f128[6 + 2 * 3] == 7u, "SysEx out: a CZ-101 send request (70 10 60) -> a 128-byte dump");
        cz_blob_preset(t, 1);
        feed(f128, 263);
        OK(cz_patch[0].raw[3] == 7u && !memcmp(cz_patch[0].raw + 128, CZ_FACTORY[0] + 128, 16) &&
           (cz_patch[0].raw[20] >> 4) == 15u && cz_patch_valid(cz_patch[0].raw),
           "SysEx in: a 128-byte dump -> the tone, the CZ-1 velocity defaults (the name the part had)");
    }
    memcpy(raw2, cz_patch[0].raw, CZ_BYTES);
    frame[6 + 2 * 3] = 0x0F;                         /* detune 63: out of range */
    frame[7 + 2 * 3] = 0x03;
    feed(frame, k);
    OK(!memcmp(cz_patch[0].raw, raw2, CZ_BYTES), "SysEx in: an invalid tone is refused (the part keeps its tone)");
    trk[0].eng_req = 0;
    feed(b, k);
    OK(!memcmp(cz_patch[0].raw, raw2, CZ_BYTES), "SysEx in: no CZ-1 part: nothing changes");

    /* ---- the banks ---- */
    {
        cz_bank_t *bk;
        int16_t e0, e1;
        cu_load(t, ENGI_CZ, 0, 0);                   /* INIT TONE */
        t->engine = ENGI_CZ;
        t->p[P_E0] = 1;                              /* BANK B, PTCH 9: D-1 PIANO 1 */
        t->p[P_E1] = 9;
        cz_bank_poll();
        OK(!memcmp(cz_patch[0].raw, CZ_FACTORY[16 + 8], CZ_BYTES), "bank: BANK B PTCH 9 -> D-1 (BANK A..D: Casio's)");
        OK(cz_bank_get(4, 0, raw) != 0 && cz_bank_get(7, 15, raw) != 0, "bank: E..H start empty");
        t->p[P_E0] = 4;
        t->p[P_E1] = 1;
        cz_bank_poll();
        OK(!memcmp(cz_patch[0].raw, CZ_FACTORY[16 + 8], CZ_BYTES), "bank: an empty slot keeps the tone");
        /* a bank out (the backup's GET of object 14 + k reads this) and in (PUT, commit) */
        bk = cz_bank_load(0);
        memcpy(cu_loop_buf, bk, sizeof *bk);
        {
            cz_bank_t *nb = (cz_bank_t *)cu_loop_buf;
            memcpy(nb->name, "MY BANK E", 10);
            memcpy(nb->tone[0].raw, raw2, CZ_BYTES);
            nb->used = 0x0003u;
        }
        crb.put = 1;
        crb.id = 18;                                 /* bank E */
        crb.len = sizeof(cz_bank_t);
        crb.crc = st_crc32(cu_loop_buf, sizeof(cz_bank_t));
        crb.pos = crb.len;
        OK(crb_size_ok(18, sizeof(cz_bank_t)) && crb_commit() == 0, "bank: a bank restored as backup object 18 (bank E)");
        cz_bank_poll();
        OK(!memcmp(cz_patch[0].raw, raw2, CZ_BYTES) && cz_bank_get(4, 1, raw) == 0 && !memcmp(raw, CZ_FACTORY[1], CZ_BYTES),
           "bank: a part on BANK E PTCH 1 reloads the restored tone; E2 holds A-2");
        cz_bank_cached = 255;
        OK(cz_bank_load(4) && cz_bank_saved && !memcmp(cz_bank_cache.name, "MY BANK E", 9),
           "bank: bank E from flash (its own name)");
        {
            st_hdr_t h;
            OK(crb_current(18, &h) >= 0 && h.len == sizeof(cz_bank_t) && crb_current(14, &h) < 0 &&
               st_sector(OBJ_CZBANK0 + 4, 0) == 0xAA000u && st_sector(OBJ_CZBANK0 + 7, 1) == 0xB1000u,
               "bank: object 18 at 0xAA000 (banks 0xA2000..0xB1FFF), bank A never saved: no object");
        }
        e0 = t->p[P_E0];
        e1 = t->p[P_E1];
        (void)e0;
        (void)e1;
    }

    /* ---- the deep pages ---- */
    {
        const eng_deep_t *d = ENGINES[ENGI_CZ]->deep;
        static const char *const T[] = {"LINE 1", "LINE 2", "DETUNE", "DCW", "DCW+", "PITCH 1", "PITCH 1+", "PITCH 1 B",
                                        "PITCH 1 B+", "PITCH 1 S", "DCW 1", "DCA 1 S", "PITCH 2", "DCA 2 S", "VIB", "TONE"};
        static const uint8_t TI[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 19, 20, 34, 35, 36};
        uint32_t pg, c, cols = 0;
        for (k = 0, bad = 0; k < NELEM(T); k++)
            bad += strcmp(d->pages[TI[k]].title, T[k]) != 0;
        OK(d && d->npages == 37u && !bad && d->section[0] == 0 && d->section[1] == 3 && d->section[2] == 5 &&
           d->section[3] == 35 && d->section[4] == 36 && d->section[5] == 0xFF && d->mod_dst(t, 0, 0) == -1,
           "deep: 37 pages, sections OSC 0 (LINE n, DETUNE) FILTER 3 (DCW) ENV 5 (PITCH / DCW / DCA n: 5 pages) LFO 35 "
           "(VIB) MOD 36 (TONE); no matrix");
        for (pg = 0, bad = 0; pg < d->npages; pg++)
            for (c = 0; c < 4u; c++) {
                const param_desc_t *p = &d->pages[pg].col[c];
                int32_t vals[3], id = cz_dref(pg, c);
                if (!p->label) {
                    bad += id >= 0;
                    continue;
                }
                cols++;
                bad += id < 0 || p->min < CZ_PD[id].min || p->max > LCZ_MAX[id];
                vals[0] = p->min;
                vals[1] = p->max;
                vals[2] = p->def;
                for (i = 0; i < 3u; i++) {
                    uint8_t pv[LCZ_PACKED];
                    int32_t want = vals[i], got;
                    cz_blob_preset(t, 1);            /* (BRASS 1: SUS before END everywhere it has one) */
                    d->set(t, pg, c, vals[i]);
                    got = d->get(t, pg, c);
                    cz_ed_decode(pv, cz_patch[0].raw);
                    if (!strcmp(p->label, "SUS") || (p->label[0] == 'L' && p->label[1] >= '1' && p->label[1] <= '8'))
                        want = got;                  /* (SUS only before END, END's level 0: the CZ's rules, below) */
                    bad += got != want || pv[id] != got || !cz_patch_valid(cz_patch[0].raw);
                    if (got != want)
                        printf("  page %s col %s: set %d got %d\n", d->pages[pg].title, p->label, (int)vals[i], (int)got);
                }
            }
        OK(!bad && cols == 137u, "deep: every column (%u) set to its min, max and default reads it back, the panel value it "
                                 "names (cz_edit.h), the tone valid", (unsigned)cols);
        cz_blob_preset(t, 1);
        memcpy(raw, cz_patch[0].raw, CZ_BYTES);
        d->set(t, 10 + 2, 1, 40);                    /* DCW 1 B: R6 40 */
        for (i = 0, bad = 0; i < CZ_BYTES; i++)
            bad += cz_patch[0].raw[i] != raw[i];
        OK(d->get(t, 12, 1) == 40 && bad == 1u && cz_patch[0].raw[38 + 10] != raw[38 + 10],
           "deep: DCW 1's R6 is the one tone byte it encodes (the others verbatim)");
        d->set(t, 19, 1, 1);                         /* DCA 1 S: END on step 2 */
        OK(d->get(t, 19, 1) == 1 && (d->get(t, 19, 0) == 8 || d->get(t, 19, 0) < 1) && d->get(t, 16, 1) == 0,
           "deep: END on step 2 drops a SUS at or after it, END's level (DCA 1+ L2) reads 0 (the CZ's rules)");
        d->set(t, 36, 1, 1);                         /* TONE MOD: RING */
        d->set(t, 35, 3, 30);                        /* VIB DEPTH 30 */
        d->set(t, 2, 3, 20);                         /* DETUNE FINE 20 */
        OK(((cz_patch[0].raw[15] >> 3) & 7u) == 4u && cz_patch[0].raw[11] == 30u && (cz_patch[0].raw[2] >> 2) == 21u,
           "deep: RING, vibrato depth, fine detune in Casio's encoding (Casio p. 84: FINE 20 is 21)");
    }

    /* ---- the tone store: a user slot keeps its tone ---- */
    {
        uint32_t slot = 4;                           /* U05 */
        cu_load(t, ENGI_CZ, 9, 0);                   /* B-1 ACO.GUITAR */
        OK(!memcmp(cz_patch[0].raw, CZ_FACTORY[8], CZ_BYTES) && t->p[P_E0] == 0 && t->p[P_E1] == 9,
           "load: PRESETS B-1 loads Casio's tone (BANK A PTCH 9)");
        ENGINES[ENGI_CZ]->deep->set(t, 0, 0, 5);     /* LINE 1 WAVE: DBL SINE */
        ENGINES[ENGI_CZ]->deep->set(t, 25, 2, 33);   /* DCW 2 B: R7 */
        memcpy(raw, cz_patch[0].raw, CZ_BYTES);
        k = (uint32_t)up_store(slot, "CZ EDIT");
        OK(k == 0u || k == 3u, "store: U05 saved (a CZ-1 sound)");
        OK(czu_get(slot, b2) == 0 && !memcmp(raw, b2, CZ_BYTES) && czu[0].used == 1u << slot,
           "store: its tone in the CZ-1 tone store (object 1 of 2, slot 5)");
        cu_load(t, ENGI_CZ, 3, 0);
        cu_load_user(t, slot, 0);
        OK(!memcmp(cz_patch[0].raw, raw, CZ_BYTES) && t->eng_req == ENGI_CZ, "store: U05 loads its own tone");
        memset(czu, 0, sizeof czu);
        up_boot();
        OK(czu_get(slot, b2) == 0 && !memcmp(raw, b2, CZ_BYTES), "store: from flash after a reboot");
        OK(up_store(20, "UPPER") == 0 && czu[1].used == 1u << 4 && st_sector(OBJ_CZSTORE0, 0) == 0x9D000u &&
           st_sector(OBJ_CZSTORE1, 0) == 0xA0000u, "store: U21 in the second object (0x9D000 / 0xA0000)");
        cu_load(t, 0, 0, 0);                         /* ANALOG */
        up_store(slot, "ANALOG NOW");
        OK(czu_get(slot, b2) != 0, "store: another engine's sound over U05 clears its tone");
        up_put(20, 0);
        OK(czu_get(20, b2) != 0 && !czu[1].used, "store: an erased slot clears its tone");
        cu_load(t, ENGI_CZ, 0, 0);                   /* a slot without a tone: its BANK / PTCH */
        t->p[P_E0] = 3;
        t->p[P_E1] = 16;
        up_store(slot, "NO TONE");
        czu_put(slot, 0);
        cu_load(t, ENGI_CZ, 5, 0);
        cu_load_user(t, slot, 0);
        OK(!memcmp(cz_patch[0].raw, CZ_FACTORY[63], CZ_BYTES), "store: a slot of no tone loads BANK D PTCH 16 (H-8)");
        {
            uint32_t n = 0;
            for (k = 0; k < CRB_N; k++)
                n += crb_is_cz(CRB_IDS[k]);
            OK(n == 10u && crb_obj(12) == OBJ_CZSTORE0 && crb_obj(13) == OBJ_CZSTORE1 && crb_obj(21) == OBJ_CZBANK0 + 7u &&
               crb_size_ok(12, sizeof(czu_t)) && !crb_size_ok(12, sizeof(czu_t) - 1u) && sizeof(czu_t) == 2320u &&
               sizeof(cz_bank_t) == 2332u, "backup: objects 12, 13 (the tone store, 2320 B) and 14..21 (the banks, 2332 B)");
        }
    }

    printf("%s: %d checks, %d failed\n", fails ? "CR CZ TESTS FAILED" : "cr cz tests passed", checks, fails);
    return fails != 0;
}
