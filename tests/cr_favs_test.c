/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of firmware/src/cr_favs.c, the favourites' store (docs/PRESETS.md "Favourites"), through Felucca's flash
 * layer (storage.c on a simulated NOR: A/B copies, a torn write):
 *   cc -std=c11 -Wall -Wextra -Werror -pedantic -o build/host/cr_favs_test tests/cr_favs_test.c && build/host/cr_favs_test
 *   build/host/cr_favs_test --emit FILE    the object as the firmware's own writer stores it (crf_pack through
 *                                          st_save, read back from the NOR), for a fixed set of marks: what
 *                                          tests/install_test.py decodes with tools/fm1_install.py --favorites' reader
 * (tests/run_cr_tests.sh runs it. The marks through the UI, P5's across shipped lists: tests/cr_prophet_test.c.)
 * The sound pool's layout (0.22: user slots 1..128, the sections 0x80 and 0x81), in every build since 3y232; before,
 * this test, built without core.h, checked 0.21's (32 slots). */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int passed, failed;
static void ok(int c, const char *n)
{
    if (c) passed++;
    else { failed++; printf("FAIL %s\n", n); }
}

/* ------------------------------------------------- a NOR for storage.c --- */
static uint8_t nor[0x100000];
static int fail_after = -1, erases;
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, nor + off, n); return 0; }
static int st_erase(uint32_t off) { memset(nor + off, 0xFF, 4096); erases++; return 0; }
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    if (fail_after == 0) return -9;
    if (fail_after > 0) fail_after--;
    for (i = 0; i < n; i++) nor[off + i] &= ((const uint8_t *)src)[i];
    return 0;
}
static int blocked;
#define ST_BLOCKED() blocked
#define __attribute__(x)
#define FELUCCA_FLASH 1
#define FELUCCA_CZ 1                               /* ChoralRoot's object list (OBJ_FAVS is in it) */
#define FELUCCA_QUAD 1
static uint32_t fm1_ms;
#define ST_AHEAD 1                                 /* (choralroot.c's: the erase ahead) */
#include "../firmware/src/storage.c"
#include "../firmware/src/cr_favs.c"
#undef __attribute__

static void power_cycle(void)                      /* the RAM gone, the marks from flash */
{
    memset(crf_bits, 0xA5, sizeof crf_bits);
    memset(crf_only, 0x5A, sizeof crf_only);
    crf_filter = 1;
    crf_dirty = 1;
    crf_boot();
}
/* the fixed set --emit writes (tests/install_test.py knows it): FM6 0, 24 (Chord); VA 3 (Bass); CZ-1 64; FM TONE 15;
 * WHEEL 4; P5 1, 15 (Chord), 44 (Bass), 199, 200; user slots 0, 31 (Bass); FM6 5 typed Bass and P5 7 typed Chord
 * without a mark; the filter on */
static void fixed_set(void)
{
    static const uint8_t M[][2] = {{12, 0}, {12, 24}, {13, 3}, {14, 64}, {15, 15}, {7, 4}, {16, 1}, {16, 15}, {16, 44},
                                   {16, 199}, {16, 200}, {CRF_T_USER, 0}, {CRF_T_USER, 31}};
    uint32_t i;
    (void)crf_unpack(0, 0);
    for (i = 0; i < sizeof M / sizeof M[0]; i++)
        (void)crf_set(M[i][0], M[i][1], 1);
    (void)crf_set_type(12, 24, CRF_CHORD);
    (void)crf_set_type(13, 3, CRF_BASS);
    (void)crf_set_type(16, 15, CRF_CHORD);
    (void)crf_set_type(16, 44, CRF_BASS);
    (void)crf_set_type(CRF_T_USER, 31, CRF_BASS);
    (void)crf_set_type(12, 5, CRF_BASS);             /* (FM6 06 and P5 7: typed, NOT marked: not favourites) */
    (void)crf_set_type(16, 7, CRF_CHORD);
    (void)crf_set_filter(1);
}

int main(int argc, char **argv)
{
    uint8_t b[CRF_PACK_MAX], c[CRF_PACK_MAX];
    uint32_t n, i, k;
    int e0;
    memset(nor, 0xFF, sizeof nor);
    if (argc == 3 && !strcmp(argv[1], "--emit")) {
        FILE *f = fopen(argv[2], "wb");
        int len;
        fixed_set();
        if (!f || crf_flush() != 0 || (len = st_load(OBJ_FAVS, b, sizeof b)) < 2)
            return 1;
        fwrite(b, 1, (size_t)len, f);
        fclose(f);
        printf("cr_favs: %d bytes, %u marks, as the firmware writes them\n", len, (unsigned)crf_count());
        return 0;
    }

    ok(st_sector(OBJ_FAVS, 0) == 0xB6000u && st_sector(OBJ_FAVS, 1) == 0xB7000u && st_sector(OBJ_SESSION, 1) == 0xB5000u &&
       OBJ_FAVS == OBJ_SESSION + 1 && OBJ_COUNT == OBJ_FAVS + 1,
       "the object: the pair 0xB6000 / 0xB7000, the first free sectors after the session; the last of the list");
    for (i = 0, n = 0; i < CRF_NSEC; i++)
        n += CRF_SEC[i].bytes;
    ok(n == CRF_BYTES && sizeof crf_bits == 84u, "the marks: 84 bytes of bits in RAM");

    /* ---- a new unit ---- */
    crf_boot();
    ok(crf_count() == 0 && crf_last_rc == 0 && !crf_dirty, "a new unit: no object, no marks");
    ok(crf_flush() == 0 && erases == 0, ".. and nothing to write: no erase");

    /* ---- set, clear ---- */
    ok(crf_set(12, 0, 1) == 1 && crf_has(12, 0) && crf_dirty && crf_count() == 1, "set: FM6's first preset");
    ok(crf_set(12, 0, 1) == 0 && crf_count() == 1, "set again: no change");
    ok(crf_set(16, 200, 1) == 1 && crf_set(16, 1, 1) == 1 && crf_has(16, 200) && crf_has(16, 1) && !crf_has(16, 2),
       "set: P5's programs 1 and 200, by their Sequential numbers");
    ok(crf_set(CRF_T_USER, 31, 1) == 1 && crf_has(CRF_T_USER, 31) && !crf_has(CRF_T_USER, 30), "set: user slot 32");
    ok(crf_set(12, 0, 0) == 1 && !crf_has(12, 0) && crf_count() == 3, "clear: FM6's first preset");
    ok(crf_set(12, 0, 0) == 0, "clear again: no change");

    /* ---- ids out of range, sections that are not ---- */
    k = crf_count();
    crf_dirty = 0;
    ok(crf_set(16, 0, 1) == 0 && crf_set(16, 201, 1) == 0 && crf_set(16, 207, 1) == 0 && crf_set(16, 5000, 1) == 0,
       "out of range: P5's 0, 201, 207, 5000 are no programs");
    ok(crf_set(CRF_T_USER, 128, 1) == 0 && crf_set(12, 64, 1) == 0 && crf_set(14, 128, 1) == 0 && crf_set(7, 16, 1) == 0,
       "out of range: user slot 129, FM6 64, CZ-1 128, WHEEL 16");
    ok(crf_set(0, 1, 1) == 0 && crf_set(9, 1, 1) == 0 && crf_set(17, 1, 1) == 0 && crf_set(255, 0, 1) == 0 &&
       !crf_has(0, 1) && !crf_has(9, 1) && !crf_has(255, 0), "out of range: a retired engine (0, 9), no engine (17, 255)");
    ok(crf_count() == k && !crf_dirty, ".. none of them changed a bit or asked for a write");
    crf_dirty = 1;

    /* ---- persist across a power cycle ---- */
    ok(crf_flush() == 0 && !crf_dirty && erases == 0 && crf_saves == 1, "flush: one write (a blank copy: no erase)");
    ok(crf_flush() == 0 && erases == 0 && crf_saves == 1, "flush again: nothing changed, no write");
    power_cycle();
    ok(crf_last_rc == 1 && crf_count() == 3 && crf_has(16, 200) && crf_has(16, 1) && crf_has(CRF_T_USER, 31) && !crf_has(12, 0) &&
       !crf_dirty, "a power cycle: the three marks are back");
    n = crf_pack(b);
    ok(n == 2u + (2u + 26u) + (2u + 4u) && b[0] == CRF_VER && b[1] == 2u && b[2] == CRF_T_USER && b[3] == 4u && b[7] == 0x80u &&
       b[8] == 16u && b[9] == 26u && b[10] == 0x02u && b[35] == 0x01u,
       "the flash form: version 1, two sections (the user slots: 4 bytes, P5: 26), 36 bytes");

    /* ---- a torn write ---- */
    (void)crf_set(13, 7, 1);
    e0 = erases;
    fail_after = 0;                                /* not a byte programmed (the spare copy was blank: no erase) */
    ok(crf_flush() != 0 && crf_dirty && erases == e0, "a torn write (the power cut before the first page): the flush fails, still to be written");
    fail_after = -1;
    power_cycle();
    ok(crf_last_rc == 1 && crf_count() == 3 && !crf_has(13, 7) && crf_has(16, 200), ".. the power back: the marks as before it (the other copy)");
    (void)crf_set(13, 7, 1);
    fail_after = 1;                                /* the payload written, no commit record */
    ok(crf_flush() != 0, "a torn write (the payload in, no commit record): the flush fails");
    fail_after = -1;
    power_cycle();
    ok(crf_count() == 3 && !crf_has(13, 7), ".. the marks as before it");
    (void)crf_set(13, 7, 1);
    ok(crf_flush() == 0, "the next write goes through");
    power_cycle();
    ok(crf_count() == 4 && crf_has(13, 7), ".. and is what the power-on reads");
    for (i = 0; i < 6; i++) {                      /* both copies in turn */
        (void)crf_set(15, i, 1);
        ok(crf_flush() == 0, "a write");
    }
    power_cycle();
    ok(crf_count() == 10 && crf_has(15, 5), "six more writes (A, B, A ..): the newest copy wins");

    /* ---- an unknown version, a foreign object ---- */
    n = crf_pack(b);
    memcpy(c, b, n);
    c[0] = CRF_VER + 1u;
    ok(st_save(OBJ_FAVS, c, n) == 0, "(an object of version 3 in flash)");
    power_cycle();
    ok(crf_last_rc == -1 && crf_count() == 0 && !crf_dirty, "an unknown version: read as no marks, not rewritten by the power-on");
    ok(crf_set(12, 1, 1) == 1 && crf_flush() == 0, ".. the next mark writes this firmware's version over it");
    power_cycle();
    ok(crf_last_rc == 1 && crf_count() == 1 && crf_has(12, 1), ".. which reads back");
    memcpy(c, b, n);
    c[1] = 9;                                      /* more sections than there are bytes */
    ok(crf_unpack(c, n) == -1 && crf_count() == 0, "an object cut short: no marks (not half of them)");
    ok(crf_unpack(b, n) == 0 && crf_count() == 10, "(the good one again)");
    {   /* a newer firmware's object: a section this one does not know, a longer P5 section, stray bits */
        uint8_t x[64];
        uint32_t m = 0;
        x[m++] = CRF_VER; x[m++] = 4;
        x[m++] = 99; x[m++] = 3; x[m++] = 0xFF; x[m++] = 0xFF; x[m++] = 0xFF;        /* engine 99: unknown */
        x[m++] = 7; x[m++] = 4; x[m++] = 0x10; x[m++] = 0; x[m++] = 0; x[m++] = 0xFF;   /* WHEEL, 4 bytes (2 known) */
        x[m++] = 12; x[m++] = 1; x[m++] = 0x01;                                      /* FM6, 1 byte */
        x[m++] = 16; x[m++] = 28;                                                    /* P5, 28 bytes */
        memset(x + m, 0xFF, 28);
        m += 28;
        ok(crf_unpack(x, m) == 0 && crf_has(7, 4) && crf_has(12, 0) && !crf_has(99, 0) && crf_has(16, 1) && crf_has(16, 200) &&
           !crf_has(16, 0) && !crf_has(16, 201) && crf_count() == 2 + 200,
           "a newer object: the unknown section skipped, the bytes past a section's size and P5's bits 0 and 201.. ignored");
    }
    ok(crf_unpack(b, 1) == -1 && crf_unpack(b, 0) == -1 && crf_count() == 0, "one byte, none: no marks");

    /* ---- an empty object (a restore clears it), SAFE MODE ---- */
    ok(st_save(OBJ_FAVS, b, 0) == 0, "(an empty object in flash)");
    power_cycle();
    ok(crf_last_rc == 0 && crf_count() == 0, "an empty object: no marks, as a new unit");
    (void)crf_set(12, 3, 1);
    blocked = 1;
    e0 = erases;
    ok(crf_flush() == 0 && erases == e0 && crf_has(12, 3), "SAFE MODE: the mark stays in RAM, the flash untouched");
    power_cycle();
    ok(crf_count() == 0, "SAFE MODE: nothing is read either");
    blocked = 0;

    /* ---- a slot emptied or filled anew loses its mark (upreset.c up_put) ---- */
    (void)crf_unpack(b, n);
    (void)crf_set(CRF_T_USER, 4, 1);
    crf_dirty = 0;
    crf_slot_new(4);
    ok(!crf_has(CRF_T_USER, 4) && crf_dirty, "a user slot erased: its mark goes, to be written");
    crf_dirty = 0;
    crf_slot_new(5);
    ok(!crf_dirty, ".. a slot that had none: nothing to write");

    /* ---- every bit of every section packs and unpacks ---- */
    {
        uint32_t bad = 0, marks = 0;
        (void)crf_unpack(0, 0);
        for (k = 0; k < CRF_NSEC; k++)
            for (i = 0; i < CRF_SEC[k].bytes * 8u; i++) {
                int want = !(CRF_SEC[k].tag == CRF_T_P5 && (!i || i > 200u));
                bad += crf_set(CRF_SEC[k].tag, i, 1) != want;
                marks += (uint32_t)want;
            }
        n = crf_pack(c);
        memset(crf_bits, 0, sizeof crf_bits);
        ok(!bad && n <= CRF_PACK_MAX && n == 2u + 2u * CRF_NSEC + CRF_BYTES && crf_unpack(c, n) == 0 && crf_count() == marks &&
           marks == 128u + 16u + 64u + 64u + 128u + 64u + 200u,
           "every sound marked: 664 marks in 102 bytes, read back whole");
    }
    /* ---- the type (Both / Chord / Bass) and the filter (0.22, the owner 2026-10-13); version 1 -> version 2 ---- */
    {
        /* version 1 as 40acb01 wrote it: FM6 0 and 3, P5 12, user slot 2 */
        static const uint8_t V1[] = {1, 3, CRF_T_USER, 1, 0x04, 12, 1, 0x09, 16, 2, 0x00, 0x10};
        uint32_t gen;
        ok(CRF_VER == 2u && sizeof crf_bits + sizeof crf_only + sizeof crf_filter == 253u, "version 2; 253 bytes of marks, types and the filter in RAM");
        ok(st_save(OBJ_FAVS, V1, sizeof V1) == 0, "(a version-1 object in flash)");
        e0 = erases;
        power_cycle();
        ok(crf_last_rc == 1 && crf_last_ver == 1u && crf_count() == 4 && crf_has(12, 0) && crf_has(12, 3) && crf_has(16, 12) &&
           crf_has(CRF_T_USER, 2) && !crf_filter && !crf_dirty && erases == e0,
           "version 1 is read: its four marks, the filter off, nothing rewritten by the power-on");
        ok(crf_type(12, 0) == CRF_BOTH && crf_type(16, 12) == CRF_BOTH && crf_type(CRF_T_USER, 2) == CRF_BOTH &&
           crf_fits(12, 0, 0) && crf_fits(12, 0, 1) && crf_fits(16, 12, 0) && crf_fits(16, 12, 1),
           ".. every mark of it is Both: a favourite of the chord part and of the bass part");
        ok(!crf_fits(12, 1, 0) && !crf_fits(12, 1, 1) && crf_type(12, 1) == CRF_BOTH, "a sound without a mark fits neither part");
        gen = crf_gen;
        ok(crf_set_type(12, 0, CRF_CHORD) == 1 && crf_type(12, 0) == CRF_CHORD && crf_fits(12, 0, 0) && !crf_fits(12, 0, 1) &&
           crf_dirty && crf_gen != gen, "type Chord: the chord part's only; to be written; the filter's cache is told");
        ok(crf_set_type(12, 0, CRF_CHORD) == 0, "the same type again: no change");
        ok(crf_set_type(16, 12, CRF_BASS) == 1 && crf_type(16, 12) == CRF_BASS && !crf_fits(16, 12, 0) && crf_fits(16, 12, 1),
           "type Bass: the bass part's only");
        ok(crf_set_type(13, 5, CRF_BASS) == 1 && !crf_has(13, 5) && crf_type(13, 5) == CRF_BASS && crf_count() == 4 &&
           !crf_fits(13, 5, 1), "a type on a sound without a mark: kept, the sound not marked by the store (the UI's turn marks it)");
        ok(crf_set(13, 5, 1) == 1 && crf_type(13, 5) == CRF_BASS && crf_fits(13, 5, 1) && !crf_fits(13, 5, 0),
           ".. marked: it is a Bass favourite");
        ok(crf_set_type(14, 9, CRF_CHORD) == 1 && !crf_has(14, 9), "(CZ-1 09 typed Chord, not marked)");
        ok(crf_set_type(16, 0, CRF_BASS) == 0 && crf_set_type(16, 201, CRF_CHORD) == 0 && crf_set_type(9, 1, CRF_BASS) == 0 &&
           crf_set_type(12, 3, 7) == 0 && crf_count() == 5 && crf_type(12, 3) == CRF_BOTH && crf_type(16, 201) == CRF_BOTH,
           "a type for no sound (P5 0, 201, a retired engine) or no type (7): nothing");
        ok(crf_set_filter(1) == 1 && crf_set_filter(1) == 0 && crf_filter, "the filter on: a change once");
        n = crf_pack(b);
        ok(b[0] == 2u && b[1] == 9u && n == 2u + 3u + 3u + 3u + 3u + 3u + 4u + 4u + 4u + 3u,
           "the object: version 2, 9 sections (user; FM6 and its Chord bits; VA and its Bass bits; CZ-1's Chord bits alone; P5 and "
           "its Bass bits; the options): 32 bytes");
        ok(crf_flush() == 0 && !crf_dirty, "(written)");
        power_cycle();
        ok(crf_last_rc == 1 && crf_last_ver == 2u && crf_count() == 5 && crf_type(12, 0) == CRF_CHORD && crf_type(12, 3) == CRF_BOTH &&
           crf_type(16, 12) == CRF_BASS && crf_type(13, 5) == CRF_BASS && crf_type(CRF_T_USER, 2) == CRF_BOTH && crf_filter &&
           !crf_has(14, 9) && crf_type(14, 9) == CRF_CHORD,
           "a power cycle: version 2 read back: the marks, each type, the type of the sound that is not marked, the filter on");
        ok(crf_pack(c) == n && !memcmp(b, c, n), ".. and packs to the same bytes");
        ok(crf_set_type(12, 0, CRF_BASS) == 1 && crf_set(12, 0, 0) == 1 && crf_type(12, 0) == CRF_BASS && !crf_fits(12, 0, 1) &&
           crf_flush() == 0, "marked, typed Bass, the mark taken off: the type stays (written)");
        power_cycle();
        ok(!crf_has(12, 0) && crf_type(12, 0) == CRF_BASS && crf_set(12, 0, 1) == 1 && crf_type(12, 0) == CRF_BASS &&
           crf_fits(12, 0, 1) && !crf_fits(12, 0, 0), ".. over a power cycle too; marked again it is a Bass favourite");
        ok(crf_set_type(12, 0, CRF_BOTH) == 1 && crf_fits(12, 0, 0) && crf_fits(12, 0, 1), "Both again");
        crf_slot_new(2);
        ok(!crf_has(CRF_T_USER, 2), "(a user slot's mark goes with the slot)");
        ok(crf_set_type(CRF_T_USER, 2, CRF_CHORD) == 1 && crf_type(CRF_T_USER, 2) == CRF_CHORD, "a user slot typed Chord");
        (void)crf_set(CRF_T_USER, 2, 1);
        crf_slot_new(2);
        ok(crf_set(CRF_T_USER, 2, 1) == 1 && crf_type(CRF_T_USER, 2) == CRF_BOTH, ".. the slot erased: the next sound there starts Both");
        ok(crf_set_filter(0) == 1 && crf_flush() == 0, "(the filter off, written)");
        power_cycle();
        ok(!crf_filter && crf_count() == 5, "the filter off is what the power-on reads");
        {   /* a foreign version-2 object: a type bit without its mark, both types at once, an unknown option byte */
            static const uint8_t X[] = {2, 5, 12, 1, 0x03, 12 | CRF_T_CHORD, 1, 0x07, 12 | CRF_T_BASS, 1, 0x02, CRF_T_OPT, 2, 0xFF, 0xFF,
                                        0x7E, 1, 0xFF};
            ok(crf_unpack(X, sizeof X) == 0 && crf_count() == 2 && crf_type(12, 0) == CRF_CHORD && crf_type(12, 1) == CRF_BOTH &&
               !crf_has(12, 2) && crf_type(12, 2) == CRF_CHORD && crf_filter == 1,
               "a foreign object: a type without a mark is kept, both types at once read as Both, an unknown section skipped");
            ok(crf_unpack(X, sizeof X - 2u) == -1 && crf_count() == 0 && !crf_filter, ".. cut short: no marks, no filter");
        }
        {   /* a version-1 object never carries types: bytes that would be type sections in version 2 are skipped */
            static const uint8_t Y[] = {1, 3, 12, 1, 0x01, 12 | CRF_T_CHORD, 1, 0x01, CRF_T_OPT, 1, 0x01};
            ok(crf_unpack(Y, sizeof Y) == 0 && crf_count() == 1 && crf_type(12, 0) == CRF_BOTH && !crf_filter,
               "version 1 with sections it never had: the mark alone");
        }
        {   /* the longest object: every sound marked and typed, the filter on */
            uint32_t bad = 0;
            (void)crf_unpack(0, 0);
            for (k = 0; k < CRF_NSEC; k++)
                for (i = 0; i < CRF_SEC[k].bytes * 8u; i++)
                    (void)crf_set(CRF_SEC[k].tag, i, 1), (void)crf_set_type(CRF_SEC[k].tag, i, 1u + (i & 1u));
            (void)crf_set_filter(1);
            n = crf_pack(c);
            (void)crf_unpack(c, n);
            for (k = 0; k < CRF_NSEC; k++)
                for (i = 0; i < CRF_SEC[k].bytes * 8u; i++)
                    bad += crf_has(CRF_SEC[k].tag, i) && crf_type(CRF_SEC[k].tag, i) != 1u + (i & 1u);
            ok(!bad && n == 304u && n <= CRF_PACK_MAX && crf_count() == 664 && crf_filter && crf_pack(b) == n && !memcmp(b, c, n),
               "every sound marked and typed, the filter on: 304 bytes (P5's Bass bits end a byte early), read back whole");
            (void)crf_set_filter(0);
            crf_dirty = 0;
        }
    }
    printf("cr_favs: %d passed, %d failed\n", passed, failed);
    return failed != 0;
}
