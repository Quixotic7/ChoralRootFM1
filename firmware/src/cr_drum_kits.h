/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 CoralFlower contributors (FicusFicusov, <https://github.com/FicusFicusov/CoralFlower>)
 * Modifications Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* The drums' kits (cr_drum.c): CoralFlower's drum_kits.c (0.6, 7c44dc1), voiced by ear and named by CoralFlower's
 * owner (as X Club .. X Break there; ChoralRoot's names drop the X, its owner 2026-11-11). All six of its kits, and of each only the five sounds ChoralRoot's drums play (kick,
 * snare, clap, closed and open hat: the tom, rim, bell and cymbal rows left out).
 *
 * A row: wave, noise, filter, teeth, route, pitch (p16), metal pitch (p16), ratio (Q12), then BEND BTAU ODEC O2 OLEV
 * CUT RES NDEC NLEV FM GAP CLICK DRIVE LEVEL (0..127; LEVEL 0.5 dB a step, 96 as is). Times (BTAU ODEC NDEC): 0.3 ms x
 * 2^(v / 10), so 40 is 4.8 ms, 60 19 ms, 80 77 ms, 90 154 ms, 100 307 ms, 110 614 ms. CUT: 60 is 0.6 kHz, 70 1 kHz,
 * 80 1.6 kHz, 90 2.6 kHz, 100 4.3 kHz, 110 7.1 kHz, 120 11.6 kHz. B is the harder hit: more bend, click, noise and
 * drive, 2 dB louder. The last six numbers: the kit's MORPH (each drum's Character at rest), VARY, RATE, BITS, ROOM,
 * RTAU. The table lives in a resource region (CRD_RES, choralroot.c): the FM-1's second (.res2.drum, cr_res.h CR_RES2),
 * the SMK-37's own (.res.drum); cr_drum.c crd_kit_load copies a kit into RAM, never from a region that is not there.
 * 266 B a kit (CoralFlower's nine sounds: 514) */

#define DS(w, n, f, t, r, p, mp, ra, bend, btau, odec, o2, olev, cut, res, ndec, nlev, fm, gap, click, drive, level) \
    {w, n, f, t, r, p, mp, ra, bend, btau, odec, o2, olev, cut, res, ndec, nlev, fm, gap, click, drive, level}

#ifndef CRD_RES
#define CRD_RES          /* (choralroot.c: the resource region's section on the FM-1) */
#endif

static const char *const CRD_KIT_NAME[] = {"Club", "Deep", "Lo-Fi", "Live", "Tech", "Break"};
static const crd_kit_t CRD_KITS[] CRD_RES = {
    {   /* Club: club 909: a punchy kick, a bright snare, metallic cymbals */
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
    {   /* Deep: deep 808: a long sub boom, a two-tone snare, square-wave cymbals */
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
    {   /* Lo-Fi: lo-fi: dusty and dull, short, soft cymbals, an old sampler's grit */
     {
        DS(DSW_ADD, DSN_WHITE, DSF_LP, 0, 0, 552, 0, 12288, 30, 55, 80, 35, 127, 70, 0, 40, 15, 0, 0, 0, 70, 102),
        DS(DSW_ADD, DSN_WHITE, DSF_LP, 0, 0, 912, 0, 6554, 6, 50, 72, 70, 115, 93, 20, 74, 100, 0, 0, 0, 50, 96),
        DS(DSW_SINE, DSN_WHITE, DSF_BP, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 68, 30, 74, 110, 0, 55, 0, 40, 112),
        DS(DSW_SINE, DSN_RING, DSF_BP, 0, 0, 0, 1270, 0, 0, 0, 0, 0, 0, 102, 20, 55, 110, 0, 16, 15, 30, 94),
        DS(DSW_SINE, DSN_RING, DSF_BP, 0, 0, 0, 1270, 0, 0, 0, 0, 0, 0, 100, 20, 80, 105, 0, 16, 0, 30, 90),
     },
     {
        DS(DSW_ADD, DSN_WHITE, DSF_LP, 0, 0, 552, 0, 12288, 40, 57, 82, 35, 127, 70, 0, 40, 25, 0, 0, 0, 82, 106),
        DS(DSW_ADD, DSN_WHITE, DSF_LP, 0, 0, 912, 0, 6554, 10, 50, 74, 80, 123, 96, 20, 77, 112, 0, 0, 0, 60, 100),
        DS(DSW_SINE, DSN_WHITE, DSF_BP, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 70, 30, 77, 120, 0, 53, 0, 50, 116),
        DS(DSW_SINE, DSN_RING, DSF_BP, 0, 0, 0, 1270, 0, 0, 0, 0, 0, 0, 104, 20, 58, 123, 0, 16, 30, 30, 98),
        DS(DSW_SINE, DSN_RING, DSF_BP, 0, 0, 0, 1270, 0, 0, 0, 0, 0, 0, 102, 20, 83, 117, 0, 16, 0, 30, 94),
     },
     40, 50, 3, 8, 0, 0},
    {   /* Live: acoustic-like: a beater kick, wired snare, natural toms, a room */
     {
        DS(DSW_ADD, DSN_WHITE, DSF_LP, 0, 0, 574, 0, 6144, 18, 50, 74, 40, 127, 92, 0, 44, 60, 0, 0, 110, 0, 110),
        DS(DSW_ADD, DSN_WHITE, DSF_BP, 0, 0, 885, 0, 6513, 6, 45, 70, 70, 105, 104, 0, 86, 120, 0, 0, 90, 10, 92),
        DS(DSW_SINE, DSN_WHITE, DSF_BP, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 86, 20, 66, 110, 0, 75, 0, 0, 121),
        DS(DSW_SINE, DSN_RING, DSF_BP, 0, 0, 0, 1300, 0, 0, 0, 0, 0, 0, 104, 10, 60, 110, 40, 12, 50, 0, 101),
        DS(DSW_SINE, DSN_RING, DSF_BP, 0, 0, 0, 1300, 0, 0, 0, 0, 0, 0, 102, 10, 96, 105, 40, 12, 40, 0, 92),
     },
     {
        DS(DSW_ADD, DSN_WHITE, DSF_LP, 0, 0, 574, 0, 6144, 28, 52, 76, 40, 127, 92, 0, 44, 70, 0, 0, 127, 12, 114),
        DS(DSW_ADD, DSN_WHITE, DSF_BP, 0, 0, 885, 0, 6513, 10, 45, 72, 80, 113, 107, 0, 89, 127, 0, 0, 110, 20, 96),
        DS(DSW_SINE, DSN_WHITE, DSF_BP, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 88, 20, 69, 120, 0, 73, 0, 10, 125),
        DS(DSW_SINE, DSN_RING, DSF_BP, 0, 0, 0, 1300, 0, 0, 0, 0, 0, 0, 106, 10, 63, 123, 48, 12, 65, 0, 105),
        DS(DSW_SINE, DSN_RING, DSF_BP, 0, 0, 0, 1300, 0, 0, 0, 0, 0, 0, 104, 10, 99, 117, 48, 12, 55, 0, 96),
     },
     50, 60, 1, 0, 20, 94},
    {   /* Tech: raw techno: a hard distorted kick, a ringing clap, metallic hats */
     {
        DS(DSW_ADD, DSN_WHITE, DSF_LP, 0, 0, 502, 0, 12288, 112, 42, 80, 60, 127, 100, 0, 34, 50, 0, 0, 110, 127, 97),
        DS(DSW_ADD, DSN_WHITE, DSF_BP, 0, 0, 960, 0, 7496, 10, 50, 58, 60, 90, 96, 40, 72, 125, 0, 0, 60, 70, 94),
        DS(DSW_SINE, DSN_WHITE, DSF_BP, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 82, 80, 96, 120, 0, 26, 0, 70, 84),
        DS(DSW_SINE, DSN_FM, DSF_HP, 0, 0, 0, 1550, 0, 0, 0, 0, 0, 0, 110, 60, 44, 110, 40, 10, 30, 50, 91),
        DS(DSW_SINE, DSN_FM, DSF_HP, 0, 0, 0, 1550, 0, 0, 0, 0, 0, 0, 108, 60, 90, 105, 40, 15, 0, 50, 65),
     },
     {
        DS(DSW_ADD, DSN_WHITE, DSF_LP, 0, 0, 502, 0, 12288, 122, 44, 82, 60, 127, 100, 0, 34, 60, 0, 0, 127, 127, 101),
        DS(DSW_ADD, DSN_WHITE, DSF_BP, 0, 0, 960, 0, 7496, 14, 50, 60, 70, 98, 99, 40, 75, 127, 0, 0, 80, 80, 98),
        DS(DSW_SINE, DSN_WHITE, DSF_BP, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 84, 80, 99, 127, 0, 24, 0, 80, 88),
        DS(DSW_SINE, DSN_FM, DSF_HP, 0, 0, 0, 1550, 0, 0, 0, 0, 0, 0, 112, 60, 47, 123, 48, 10, 45, 50, 95),
        DS(DSW_SINE, DSN_FM, DSF_HP, 0, 0, 0, 1550, 0, 0, 0, 0, 0, 0, 110, 60, 93, 117, 48, 15, 0, 50, 69),
     },
     64, 0, 1, 0, 0, 0},
    {   /* Break: breaks: a punchy kick, a fat crunchy snare, soft cymbals, a room */
     {
        DS(DSW_ADD, DSN_WHITE, DSF_LP, 0, 0, 595, 0, 8192, 40, 50, 78, 60, 127, 94, 0, 40, 50, 0, 0, 90, 90, 98),
        DS(DSW_ADD, DSN_WHITE, DSF_BP, 0, 0, 874, 0, 6554, 8, 50, 74, 100, 120, 98, 10, 82, 125, 0, 0, 90, 90, 88),
        DS(DSW_SINE, DSN_WHITE, DSF_BP, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 82, 30, 76, 115, 0, 45, 0, 50, 103),
        DS(DSW_SINE, DSN_RING, DSF_BP, 0, 0, 0, 1300, 0, 0, 0, 0, 0, 0, 103, 20, 62, 110, 30, 12, 40, 40, 93),
        DS(DSW_SINE, DSN_RING, DSF_BP, 0, 0, 0, 1300, 0, 0, 0, 0, 0, 0, 101, 20, 90, 105, 30, 12, 30, 40, 84),
     },
     {
        DS(DSW_ADD, DSN_WHITE, DSF_LP, 0, 0, 595, 0, 8192, 50, 52, 80, 60, 127, 94, 0, 40, 60, 0, 0, 110, 102, 102),
        DS(DSW_ADD, DSN_WHITE, DSF_BP, 0, 0, 874, 0, 6554, 12, 50, 76, 110, 127, 101, 10, 85, 127, 0, 0, 110, 100, 92),
        DS(DSW_SINE, DSN_WHITE, DSF_BP, 3, 0, 0, 0, 0, 0, 0, 0, 0, 0, 84, 30, 79, 125, 0, 43, 0, 60, 107),
        DS(DSW_SINE, DSN_RING, DSF_BP, 0, 0, 0, 1300, 0, 0, 0, 0, 0, 0, 105, 20, 65, 123, 38, 12, 55, 40, 97),
        DS(DSW_SINE, DSN_RING, DSF_BP, 0, 0, 0, 1300, 0, 0, 0, 0, 0, 0, 103, 20, 93, 117, 38, 12, 45, 40, 88),
     },
     48, 55, 2, 12, 15, 88},
};
#define CRD_NKIT ((uint32_t)(sizeof CRD_KITS / sizeof CRD_KITS[0]))
