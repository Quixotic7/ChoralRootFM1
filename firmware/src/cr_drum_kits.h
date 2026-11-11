/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 CoralFlower contributors (FicusFicusov, <https://github.com/FicusFicusov/CoralFlower>)
 * Modifications Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* The drums' kits (cr_drum.c): CoralFlower's drum_kits.c (0.6, 7c44dc1), voiced by ear and named by CoralFlower's
 * owner. Two of its six for now, X Club and X Deep, and of each only the five sounds ChoralRoot's drums play (kick,
 * snare, clap, closed and open hat: the tom, rim, bell and cymbal rows left out). The rest come with a second
 * resource region (the DRUMS plan).
 *
 * A row: wave, noise, filter, teeth, route, pitch (p16), metal pitch (p16), ratio (Q12), then BEND BTAU ODEC O2 OLEV
 * CUT RES NDEC NLEV FM GAP CLICK DRIVE LEVEL (0..127; LEVEL 0.5 dB a step, 96 as is). Times (BTAU ODEC NDEC): 0.3 ms x
 * 2^(v / 10), so 40 is 4.8 ms, 60 19 ms, 80 77 ms, 90 154 ms, 100 307 ms, 110 614 ms. CUT: 60 is 0.6 kHz, 70 1 kHz,
 * 80 1.6 kHz, 90 2.6 kHz, 100 4.3 kHz, 110 7.1 kHz, 120 11.6 kHz. B is the harder hit: more bend, click, noise and
 * drive, 2 dB louder. The last six numbers: the kit's MORPH (each drum's Character at rest), VARY, RATE, BITS, ROOM,
 * RTAU. On the FM-1 the table lives in the resource region (CRD_RES, choralroot.c): cr_drum.c crd_kit_load copies a
 * kit into RAM, never from a region that is not there */

#define DS(w, n, f, t, r, p, mp, ra, bend, btau, odec, o2, olev, cut, res, ndec, nlev, fm, gap, click, drive, level) \
    {w, n, f, t, r, p, mp, ra, bend, btau, odec, o2, olev, cut, res, ndec, nlev, fm, gap, click, drive, level}

#ifndef CRD_RES
#define CRD_RES          /* (choralroot.c: the resource region's section on the FM-1) */
#endif

static const char *const CRD_KIT_NAME[] = {"X Club", "X Deep"};
static const crd_kit_t CRD_KITS[] CRD_RES = {
    {   /* X Club: club 909, a punchy kick, a bright snare, metallic cymbals */
     {
        DS(DSW_ADD, DSN_WHITE, DSF_LP, 0, 0, 528, 0, 12288, 80, 58, 84, 50, 127, 95, 10, 30, 30, 0, 0, 70, 70, 99),
        DS(DSW_ADD, DSN_WHITE, DSF_BP, 0, 0, 864, 0, 7496, 12, 50, 66, 90, 110, 106, 0, 76, 110, 0, 0, 40, 20, 104),
        DS(DSW_SINE, DSN_WHITE, DSF_BP, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 82, 40, 82, 115, 0, 40, 0, 15, 109),
        DS(DSW_SINE, DSN_FM, DSF_HP, 0, 0, 0, 1383, 0, 0, 0, 0, 0, 0, 104, 20, 55, 110, 70, 20, 50, 0, 99),
        DS(DSW_SINE, DSN_FM, DSF_HP, 0, 0, 0, 1383, 0, 0, 0, 0, 0, 0, 100, 20, 88, 105, 70, 25, 40, 0, 81),
     },
     {
        DS(DSW_ADD, DSN_WHITE, DSF_LP, 0, 0, 528, 0, 12288, 90, 62, 86, 60, 127, 100, 10, 33, 40, 0, 0, 90, 85, 103),
        DS(DSW_ADD, DSN_WHITE, DSF_BP, 0, 0, 864, 0, 7496, 16, 50, 68, 100, 118, 109, 0, 79, 122, 0, 0, 60, 30, 108),
        DS(DSW_SINE, DSN_WHITE, DSF_BP, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 84, 40, 85, 125, 0, 38, 0, 25, 113),
        DS(DSW_SINE, DSN_FM, DSF_HP, 0, 0, 0, 1383, 0, 0, 0, 0, 0, 0, 106, 20, 58, 123, 78, 20, 65, 0, 103),
        DS(DSW_SINE, DSN_FM, DSF_HP, 0, 0, 0, 1383, 0, 0, 0, 0, 0, 0, 102, 20, 91, 117, 78, 25, 55, 0, 85),
     },
     32, 40, 1, 0, 0, 0},
    {   /* X Deep: deep 808, a long sub boom, a two-tone snare, square-wave cymbals */
     {
        DS(DSW_ADD, DSN_NONE, DSF_LP, 0, 0, 485, 0, 12288, 24, 62, 106, 35, 127, 64, 0, 0, 0, 0, 0, 15, 45, 95),
        DS(DSW_ADD, DSN_WHITE, DSF_HP, 0, 0, 934, 0, 8192, 4, 50, 70, 90, 115, 102, 10, 70, 90, 0, 0, 10, 0, 93),
        DS(DSW_SINE, DSN_WHITE, DSF_BP, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 72, 70, 94, 120, 0, 30, 0, 0, 97),
        DS(DSW_SINE, DSN_METAL, DSF_HP, 0, 0, 0, 894, 0, 0, 0, 0, 0, 0, 108, 30, 50, 110, 0, 8, 20, 0, 106),
        DS(DSW_SINE, DSN_METAL, DSF_HP, 0, 0, 0, 894, 0, 0, 0, 0, 0, 0, 106, 30, 92, 105, 0, 8, 15, 0, 90),
     },
     {
        DS(DSW_ADD, DSN_NONE, DSF_LP, 0, 0, 485, 0, 12288, 34, 64, 108, 35, 127, 64, 0, 0, 0, 0, 0, 35, 57, 99),
        DS(DSW_ADD, DSN_WHITE, DSF_HP, 0, 0, 934, 0, 8192, 8, 50, 72, 100, 123, 105, 10, 73, 102, 0, 0, 30, 10, 97),
        DS(DSW_SINE, DSN_WHITE, DSF_BP, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 74, 70, 97, 127, 0, 28, 0, 10, 101),
        DS(DSW_SINE, DSN_METAL, DSF_HP, 0, 0, 0, 894, 0, 0, 0, 0, 0, 0, 110, 30, 53, 123, 0, 8, 35, 0, 110),
        DS(DSW_SINE, DSN_METAL, DSF_HP, 0, 0, 0, 894, 0, 0, 0, 0, 0, 0, 108, 30, 95, 117, 0, 8, 30, 0, 94),
     },
     24, 30, 1, 0, 0, 0},
};
#define CRD_NKIT ((uint32_t)(sizeof CRD_KITS / sizeof CRD_KITS[0]))
