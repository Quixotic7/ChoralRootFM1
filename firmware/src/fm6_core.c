/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modifications Copyright (C) 2026 Kerem Kilic (Ellic Studio) */
/* FM6 core: six-operator FM that renders a DX7 voice the samples Dexed renders.
 *
 * The synthesis is Dexed's (Pascal Gauthier; on MSFA by Raph Levien, Google), restated in C: ENGINE (the FM6
 * function settings, fm6_fn) picks Dexed's MODERN (MSFA, 24-bit), MARK I (the DX7's log-sine and exponent
 * tables, its 2- and 3-operator feedback loops in algorithms 6 and 4) or OPL resolution. Envelopes in the log
 * domain with the DX7 attack curve and static times, output, level and rate key scaling, velocity, the 32
 * algorithms, the LFO, pitch envelope, pitch bend, portamento and the controllers (wheel, foot, breath,
 * aftertouch to pitch, amplitude and EG bias) follow Dexed's code and its DX7 measurements, at 44.1 kHz in
 * Dexed's 64-sample blocks (two of the platform's 32-sample control ticks). tests/fm6_parity.sh renders scores
 * through both and compares the samples. Tables: tools/gen_tables.py ("DX7 data": Apache License 2.0 /
 * GPL-3.0-or-later, see LICENSING.md). The voice handling (which of Dexed's 16 voices a key takes, MONO's
 * hand-over, the voices that run on after their notes) is eng_fm6.c's.
 *
 * Units: logs in Q24 (1 << 24 = one octave or 6 dB); an operator's output is Q24 with 1 << 24 = a unit sine,
 * added to the next operator's phase as 1 << 24 = one cycle (OUTPUT 99 at the top of its envelope: 2.0, a 4 pi
 * index). The patch is the 155-byte single-voice layout (FP_* below), operator 0 = the sixth operator. */

/* the 155-byte patch: 6 x 21 operator bytes (the sixth operator first), then the voice */
enum {
    FP_R1 = 0, FP_L1 = 4, FP_BP = 8, FP_LD, FP_RD, FP_LC, FP_RC, FP_RS, FP_AMS, FP_KVS, FP_OL, FP_MODE, FP_FC, FP_FF,
    FP_DET, FP_OP = 21,
    FP_PR1 = 126, FP_PL1 = 130, FP_ALG = 134, FP_FB, FP_OKS, FP_LFS, FP_LFD, FP_LPMD, FP_LAMD, FP_LKS, FP_LFW,
    FP_LPMS, FP_TRNSP, FP_NAME, FP_SIZE = 155
};

/* the FM6 function settings (a DX7's function mode; each track its own, as each Dexed instance has, saved with the
 * project and the template; project.c):
 * pitch bend range up / down and step (0 = smooth), portamento (PEDAL: while CC 65 is down, ON) and its time
 * (CC 5 sets it) and glissando, then wheel, foot, breath and aftertouch: range and target (bit 0 pitch, 1
 * amplitude, 2 EG bias), Dexed's velocity scaling to the DX7's range, and ENGINE */
enum { FN_PBUP, FN_PBDN, FN_PBSTEP, FN_PMODE, FN_PTIME, FN_GLISS, FN_MWR, FN_MWA, FN_FCR, FN_FCA, FN_BCR, FN_BCA,
       FN_ATR, FN_ATA, FN_VNORM, FN_ENGINE, FM6_NFN };
enum { FM6_MODERN, FM6_MARK1, FM6_OPL };
static const uint8_t FM6_FNMAX[FM6_NFN] = {12, 12, 12, 1, 127, 1, 99, 7, 99, 7, 99, 7, 99, 7, 1, 2};
static const uint8_t FM6_FNDEF[FM6_NFN] = {3, 3, 0, 0, 0, 0, 99, 1, 0, 0, 0, 0, 0, 0, 0, FM6_MARK1};   /* Dexed's */
static uint8_t fm6_fn[NTRK][FM6_NFN];                    /* per track (fm6_fn_reset at power-on, a new project) */
static void fm6_fn_reset(void)
{
    uint32_t k;
    for (k = 0; k < NTRK; k++)
        memcpy(fm6_fn[k], FM6_FNDEF, FM6_NFN);
}
static void fm6_fn_set(uint32_t tr, uint32_t k, int32_t v)
{
    if (tr < NTRK && k < FM6_NFN)
        fm6_fn[tr][k] = (uint8_t)clamp(v, 0, FM6_FNMAX[k]);
}
static int fm6_fn_ok(const uint8_t *f)                    /* a stored set: every value in its range */
{
    uint32_t k;
    for (k = 0; k < FM6_NFN; k++)
        if (f[k] > FM6_FNMAX[k])
            return 0;
    return 1;
}

/* ---------------------------------------------------------- DX7 data --- */
/* DX7 data, measured for MSFA (Copyright 2012 Google Inc.) and Dexed (Copyright 2013-2017 Pascal Gauthier),
 * Apache License 2.0: output levels below 20; the static times of the envelope (samples at 44.1 kHz, rates
 * 0..76); the velocity curve; the exponential key-scaling curve; pitch-modulation sensitivity; the pitch
 * envelope's rates and steps (1/32 octave). The sine, 2^x, frequency, MARK I / OPL, detune, LFO and
 * portamento tables are in felucca_tables.h (tools/gen_tables.py; ChoralRoot keeps Felucca's header name) */
static const uint8_t FM6_LEVELLUT[20] = {0, 5, 9, 13, 17, 20, 23, 25, 27, 29, 31, 33, 35, 37, 39, 41, 42, 43, 45, 46};
static const int32_t FM6_STATICS[77] = {
    1764000, 1764000, 1411200, 1411200, 1190700, 1014300, 992250, 882000, 705600, 705600,
    584325, 507150, 502740, 441000, 418950, 352800, 308700, 286650, 253575, 220500,
    220500, 176400, 145530, 145530, 125685, 110250, 110250, 88200, 88200, 74970,
    61740, 61740, 55125, 48510, 44100, 37485, 31311, 30870, 27562, 27562,
    22050, 18522, 17640, 15435, 14112, 13230, 11025, 9261, 9261, 7717,
    6615, 6615, 5512, 5512, 4410, 3969, 3969, 3439, 2866, 2690,
    2249, 1984, 1896, 1808, 1411, 1367, 1234, 1146, 926, 837,
    837, 705, 573, 573, 529, 441, 441};
static const uint8_t FM6_VELOCITY[64] = {
    0, 70, 86, 97, 106, 114, 121, 126, 132, 138, 142, 148, 152, 156, 160, 163,
    166, 170, 173, 174, 178, 181, 184, 186, 189, 190, 194, 196, 198, 200, 202, 205,
    206, 209, 211, 214, 216, 218, 220, 222, 224, 225, 227, 229, 230, 232, 233, 235,
    237, 238, 240, 241, 242, 243, 244, 246, 246, 248, 249, 250, 251, 252, 253, 254};
static const uint8_t FM6_EXPSCALE[33] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 11, 14, 16, 19, 23, 27, 33,
                                         39, 47, 56, 66, 80, 94, 110, 126, 142, 158, 174, 190, 206, 222, 238, 250};
static const uint8_t FM6_PMS[8] = {0, 10, 20, 33, 55, 92, 153, 255};
static const uint32_t FM6_AMS[4] = {0, 4342338, 7171437, 16777216};   /* Q24 */
static const uint8_t FM6_PEG_RATE[100] = {
    1, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11,
    12, 12, 13, 13, 14, 14, 15, 16, 16, 17, 18, 18, 19, 20, 21, 22, 23, 24, 25, 26,
    27, 28, 30, 31, 33, 34, 36, 37, 38, 39, 41, 42, 44, 46, 47, 49, 51, 53, 54, 56,
    58, 60, 62, 64, 66, 68, 70, 72, 74, 76, 79, 82, 85, 88, 91, 94, 98, 102, 106, 110,
    115, 120, 125, 130, 135, 141, 147, 153, 159, 165, 171, 178, 185, 193, 202, 211, 232, 243, 254, 255};
static const int8_t FM6_PEG_STEP[100] = {
    -128, -116, -104, -95, -85, -76, -68, -61, -56, -52, -49, -46, -43, -41, -39, -37, -35, -33, -32, -31,
    -30, -29, -28, -27, -26, -25, -24, -23, -22, -21, -20, -19, -18, -17, -16, -15, -14, -13, -12, -11,
    -10, -9, -8, -7, -6, -5, -4, -3, -2, -1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,
    10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29,
    30, 31, 32, 33, 34, 35, 38, 40, 43, 46, 49, 53, 58, 65, 73, 82, 92, 103, 115, 127};
_Static_assert(CTL * 2 == FM6_N, "FM6: a Dexed block is two control ticks");

/* ------------------------------------------------------- the algorithms --- */
/* The 32 algorithms as Dexed (MSFA fm_core.cc) has them, operators in the order they run (the sixth first):
 * bits 0-1 where the output goes (0 = the voice, 1 / 2 = bus A / B), bit 2 adds to it, bits 4-5 the bus that
 * modulates the operator, bits 6 + 7 its own feedback (bit 7 alone: the end of a feedback loop through OP6,
 * algorithms 4 and 6). tests/fm6_test.c checks the modulation graph of each against the DX7 diagrams */
enum { FM6_OB1 = 1, FM6_OB2 = 2, FM6_OADD = 4, FM6_IB1 = 16, FM6_IB2 = 32, FM6_FBIN = 64, FM6_FBOUT = 128 };
static const uint8_t FM6_ALG[32][6] = {
    {0xc1, 0x11, 0x11, 0x14, 0x01, 0x14}, {0x01, 0x11, 0x11, 0x14, 0xc1, 0x14}, {0xc1, 0x11, 0x14, 0x01, 0x11, 0x14},
    {0xc1, 0x11, 0x94, 0x01, 0x11, 0x14}, {0xc1, 0x14, 0x01, 0x14, 0x01, 0x14}, {0xc1, 0x94, 0x01, 0x14, 0x01, 0x14},
    {0xc1, 0x11, 0x05, 0x14, 0x01, 0x14}, {0x01, 0x11, 0xc5, 0x14, 0x01, 0x14}, {0x01, 0x11, 0x05, 0x14, 0xc1, 0x14},
    {0x01, 0x05, 0x14, 0xc1, 0x11, 0x14}, {0xc1, 0x05, 0x14, 0x01, 0x11, 0x14}, {0x01, 0x05, 0x05, 0x14, 0xc1, 0x14},
    {0xc1, 0x05, 0x05, 0x14, 0x01, 0x14}, {0xc1, 0x05, 0x11, 0x14, 0x01, 0x14}, {0x01, 0x05, 0x11, 0x14, 0xc1, 0x14},
    {0xc1, 0x11, 0x02, 0x25, 0x05, 0x14}, {0x01, 0x11, 0x02, 0x25, 0xc5, 0x14}, {0x01, 0x11, 0x11, 0xc5, 0x05, 0x14},
    {0xc1, 0x14, 0x14, 0x01, 0x11, 0x14}, {0x01, 0x05, 0x14, 0xc1, 0x14, 0x14}, {0x01, 0x14, 0x14, 0xc1, 0x14, 0x14},
    {0xc1, 0x14, 0x14, 0x14, 0x01, 0x14}, {0xc1, 0x14, 0x14, 0x01, 0x14, 0x04}, {0xc1, 0x14, 0x14, 0x14, 0x04, 0x04},
    {0xc1, 0x14, 0x14, 0x04, 0x04, 0x04}, {0xc1, 0x05, 0x14, 0x01, 0x14, 0x04}, {0x01, 0x05, 0x14, 0xc1, 0x14, 0x04},
    {0x04, 0xc1, 0x11, 0x14, 0x01, 0x14}, {0xc1, 0x14, 0x01, 0x14, 0x04, 0x04}, {0x04, 0xc1, 0x11, 0x14, 0x04, 0x04},
    {0xc1, 0x14, 0x04, 0x04, 0x04, 0x04}, {0xc4, 0x04, 0x04, 0x04, 0x04, 0x04}};
static int fm6_carrier(uint32_t alg, uint32_t n) { return !(FM6_ALG[alg & 31u][6u - n] & 3u); }   /* OP n: to the voice */
/* bit k: operator k (the sixth first) is a carrier of algorithm a: it writes the voice */
static uint32_t fm6_carriers(uint32_t a)
{
    uint32_t k, c = 0;
    for (k = 0; k < 6u; k++)
        c |= ((FM6_ALG[a & 31u][k] & 3u) ? 0u : 1u) << k;
    return c;
}

/* ------------------------------------------------ Dexed's lookups (exact) --- */
static inline int32_t fm6_sin(int32_t ph)                /* Q24 phase -> Q24 (MSFA Sin::lookup) */
{
    uint32_t i = ((uint32_t)ph >> 14) & 1023u;
    int32_t y0 = FM6_SIN[i];
    return y0 + (int32_t)(((int64_t)(FM6_SIN[i + 1u] - y0) * (ph & 0x3FFF)) >> 14);
}

static int32_t fm6_exp2(int32_t x)                       /* 2^x, Q24 -> Q24 (Exp2::lookup), x > -26 << 24 */
{
    uint32_t i = ((uint32_t)x >> 14) & 1023u;
    int32_t y = (int32_t)FM6_EXP2[i] + (int32_t)(((int64_t)(int32_t)(FM6_EXP2[i + 1u] - FM6_EXP2[i]) * (x & 0x3FFF)) >> 14);
    return y >> (6 - (x >> 24));
}

static int32_t fm6_freq(int32_t lf)                      /* log frequency (Q24, Hz) -> phase step (Freqlut) */
{
    uint32_t i = ((uint32_t)lf & 0xFFFFFFu) >> 14;
    int32_t y = FM6_FREQ[i] + (int32_t)(((int64_t)(FM6_FREQ[i + 1u] - FM6_FREQ[i]) * (lf & 0x3FFF)) >> 14);
    int32_t sh = 20 - (lf >> 24);
    return sh <= 0 ? y : sh > 31 ? 0 : y >> sh;
}

/* 2^(x / 65536) in Q16, x from -16 to +15.99 octaves (the FIXED frequency display) */
static uint32_t fm6_pow2(int32_t x)
{
    int32_t ip = x >> 16;
    uint32_t y = (uint32_t)fm6_exp2((x & 0xFFFF) << 8) << 6;   /* Q30 */
    if (ip > 15)
        return 0xFFFFFFFFu;
    return ip >= 14 ? y << (ip - 14) : ip < -16 ? 0u : y >> (14 - ip);
}

/* a 32-bit value as the nearest float holds it (Dexed's pitch bend is figured in float) */
static int32_t fm6_f32(int32_t x)
{
    uint32_t a = x < 0 ? (uint32_t)-x : (uint32_t)x, sh = 0, r, q;
    while ((a >> sh) >= (1u << 24))
        sh++;
    if (sh) {
        r = a & ((1u << sh) - 1u);
        q = a >> sh;
        if (r > 1u << (sh - 1u) || (r == 1u << (sh - 1u) && (q & 1u)))
            q++;
        a = q << sh;
    }
    return x < 0 ? -(int32_t)a : (int32_t)a;
}

/* AMS: Dexed takes this share of the level, pt / 2^24, pt = exp((float)sa / 262144 * 0.07 + 12.2), in
 * doubles. Figured here exactly (every sa, tests: fm6_ams_test): the argument rounded as Dexed's doubles
 * round it, then e^x = e^(a / 16) e^(b / 256) e^r to 2^-60 */
static const uint64_t FM6_EXPA[73] = {                   /* e^(a / 16), a = 195..267: Q63 mantissa */
    0xbfb7f02b61b2beceu, 0xcc15527cd7647e60u, 0xd93edb2321f0312cu, 0xe741b4bfbc5fdb7au,
    0xf62be35734bc7827u, 0x8306292b2dd32d3fu, 0x8b7971bf77bcbe0fu, 0x94783f655873a4cbu,
    0x9e0b91aa62b6bf88u, 0xa83cfcad293ab022u, 0xb316b2b2299b4246u, 0xbea38e56ea65fcafu,
    0xcaef1d6d80358746u, 0xd805ac8b564aa9aau, 0xe5f45356ca62140au, 0xf4c9019fea69488au,
    0x824946a8b6f3e449u, 0x8ab060a3ee9fb198u, 0x93a2368edf93c407u, 0x9d27bafe4cf114abu,
    0xa74a7441cd4916b9u, 0xb21485eae56c5b8au, 0xbd90baf17242b974u, 0xc9ca907f86ef32f4u,
    0xd6ce416f8c041c3du, 0xe4a8d2881edbe8d1u, 0xf3681f81edfaffe1u, 0x818d74724cb66a73u,
    0x89e871643a923469u, 0x92cd6245f82eb77cu, 0x9c452cc62c5a5005u, 0xa6594979598f8d18u,
    0xb113cd533c8ac78du, 0xbc7f73bc9722598fu, 0xc8a7a94f7e037c45u, 0xd5989744e68696a8u,
    0xe35f2f9ee31ca5b1u, 0xf2093a1bb995b8fau, 0x80d2b0ff63b58ab8u, 0x8921a25e7f0bb27cu,
    0x91f9c0cdd2993006u, 0x9b63e528812fa978u, 0xa5697a5bc48c98c2u, 0xb01486d2a382d5c6u,
    0xbb6fb67d32e6caedu, 0xc786657d69543779u, 0xd464ab843385094bu, 0xe21767ea2740196bu,
    0xf0ac4e8fee94d18du, 0x8018fac9a670d97du, 0x885bf1f339e79d1eu, 0x9127506c2065b5f4u,
    0x9a83e24e764775cau, 0xa47b04f3db5df385u, 0xaf16b053945cfd25u, 0xba6180fb56d0d2ccu,
    0xc666c2acb93c6363u, 0xd3327ba9e5b2a48fu, 0xe0d178bcdc8eb64fu, 0xef515a054f8e2dcau,
    0xfec0a099e439d252u, 0x87975e8540010249u, 0x90560f6910c83e7au, 0x99a52263dd391d63u,
    0xa38de74f3da90b42u, 0xae1a47c38a42cd04u, 0xb954d10246dd1458u, 0xc548be8445a0a268u,
    0xd20205360f81d45eu, 0xdf8d5f6dcfe5cea2u, 0xedf859a6ba5e1d7du, 0xfd51600ea6f02d97u,
    0x86d3e679b9d307e5u};
static const uint8_t FM6_EXPAE[73] = {17, 17, 17, 17, 17, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 18, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 19, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 20, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 21, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 22, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 23, 24};   /* its scale: 2^17..2^24 */
static const uint64_t FM6_EXPB[16] = {                   /* e^(b / 256), Q63 */
    0x8000000000000000u, 0x808040155aabbbe9u, 0x810100ab00222d86u, 0x81824241b103b504u,
    0x8204055aaef1c8bdu, 0x82864a77bd1036e1u, 0x8309121b2086e8a7u, 0x838c5cc7a104277eu,
    0x84102b00893f64c7u, 0x84947d49a77c8498u, 0x851954274e0fac0au, 0x859eb01e53e19398u,
    0x862491b414f45e15u, 0x86aaf96e72e8f4b3u, 0x8731e7d3d584e8bau, 0x87b95d6b2b38db4cu};

static uint64_t fm6_mulhi(uint64_t a, uint64_t b)        /* (a * b) >> 64 */
{
    uint32_t a1 = (uint32_t)(a >> 32), a0 = (uint32_t)a, b1 = (uint32_t)(b >> 32), b0 = (uint32_t)b;
    uint64_t m = (uint64_t)a1 * b0, n = (uint64_t)a0 * b1, l = (uint64_t)a0 * b0;
    return (uint64_t)a1 * b1 + (m >> 32) + (n >> 32) + (((l >> 32) + (uint32_t)m + (uint32_t)n) >> 32);
}

static uint32_t fm6_ams_pt(uint32_t sa)
{
    const uint64_t M7 = 5044031582654956u, M12 = 6867989431740006u;   /* 0.07 = M7 2^-56, 12.2 = M12 2^-49 */
    uint64_t lo = (uint64_t)sa * (uint32_t)M7, mid = (uint64_t)sa * (uint32_t)(M7 >> 32), l, q, x, r, d, m, r2;
    uint32_t h, s = 0, k, e, t, dd, rem, half;
    l = lo + (mid << 32);                                /* P = sa M7 in units of 2^-74: h:l, under 2^78 */
    h = (uint32_t)(mid >> 32) + (l < lo);
    for (t = h ? h : (uint32_t)(l >> 32); t; t >>= 1)
        s++;
    s += h ? 64u : 32u;
    s = s > 53u ? s - 53u : 0u;                          /* P rounds to 53 bits: q 2^s, s <= 24 */
    q = l;
    if (s) {
        q = (uint64_t)((uint32_t)(l >> 32) >> s | h << (32u - s)) << 32 | ((uint32_t)l >> s | (uint32_t)(l >> 32) << (32u - s));
        rem = (uint32_t)l & ((1u << s) - 1u);
        half = 1u << (s - 1u);
        q += rem > half || (rem == half && (q & 1u));
    }
    /* + 12.2, rounded to the double: 2^-49 steps under 16, 2^-48 from 16 (P >= 2^25 (2^53 - M12)) */
    dd = 25u - s;
    if (dd <= 10u && q >= (2139264564000986u << dd)) {
        dd++;
        x = M12 >> 1;
    } else {
        x = M12;
    }
    rem = (uint32_t)q & ((1u << dd) - 1u);
    half = 1u << (dd - 1u);
    x += (uint64_t)((uint32_t)(q >> 32) >> dd) << 32 | ((uint32_t)q >> dd | (dd < 32u ? (uint32_t)(q >> 32) << (32u - dd) : 0u));
    x += rem > half || (rem == half && (x & 1u));
    if (dd > 25u - s)
        x <<= 1;                                         /* x 2^49 */
    k = (uint32_t)(x >> 41);                             /* e^x = e^(a / 16) e^(b / 256) e^r */
    r = (x & ((1ull << 41) - 1u)) << 15;                 /* r, Q64 (under 1 / 256) */
    r2 = fm6_mulhi(r, r);
    d = fm6_mulhi(r2, r);                                /* e^r - 1 = r + r^2 / 2 + r^3 / 6 + ... */
    d = r + (r2 >> 1) + fm6_mulhi(d, 3074457345618258603u) + fm6_mulhi(fm6_mulhi(d, r), 768614336404564651u) +
        fm6_mulhi(fm6_mulhi(fm6_mulhi(d, r), r), 153722867280912930u);
    m = fm6_mulhi(FM6_EXPA[(k >> 4) - 195u], FM6_EXPB[k & 15u]);   /* Q62 */
    e = FM6_EXPAE[(k >> 4) - 195u];
    if (m >> 63) {
        m >>= 1;
        e++;
    }
    m += fm6_mulhi(m, d);
    return (uint32_t)(m >> 32) >> (30u - e);
}

/* -------------------------------------------------- scaling (DX7 rules) --- */
static int32_t fm6_scaleout(int32_t l) { return l >= 20 ? 28 + l : FM6_LEVELLUT[l < 0 ? 0 : l]; }

static int32_t fm6_curve(int32_t group, int32_t depth, int32_t curve)   /* key level scaling, 0.75 dB steps */
{
    int32_t s = curve == 0 || curve == 3 ? (group * depth * 329) >> 12
                                         : (FM6_EXPSCALE[group > 32 ? 32 : group] * depth * 329) >> 15;
    return curve < 2 ? -s : s;
}

/* an operator's output level for a note and velocity, in 0.023 dB steps (99 * 32 = full) */
static int32_t fm6_outlevel(const uint8_t *op, uint32_t note, uint32_t vel)
{
    int32_t off = (int32_t)note - op[FP_BP] - 17, l, v;
    l = fm6_scaleout(op[FP_OL]) + (off >= 0 ? fm6_curve((off + 1) / 3, op[FP_RD], op[FP_RC])
                                            : fm6_curve(-(off - 1) / 3, op[FP_LD], op[FP_LC]));
    v = FM6_VELOCITY[(vel > 127u ? 127u : vel) >> 1] - 239;
    l = ((l > 127 ? 127 : l) << 5) + (((op[FP_KVS] * v + 7) >> 3) << 4);
    return l < 0 ? 0 : l;
}

static int32_t fm6_rscale(const uint8_t *op, uint32_t note)   /* key rate scaling, in qrate units */
{
    int32_t x = (int32_t)note / 3 - 7;
    return (op[FP_RS] * clamp(x, 0, 31)) >> 3;
}

static int32_t fm6_logfreq(const uint8_t *op, uint32_t note)   /* an operator's pitch for a note (osc_freq) */
{
    int32_t lf;
    if (op[FP_MODE]) {                                  /* fixed: 10 ^ (COARSE % 4 + FINE / 100) Hz */
        lf = (4458616 * ((op[FP_FC] & 3) * 100 + op[FP_FF])) >> 3;
        return lf + (op[FP_DET] > 7 ? 13457 * (op[FP_DET] - 7) : 0);
    }
    lf = 50857777 + ((1 << 24) / 12) * (int32_t)note;
    lf += (int32_t)((FM6_DETUNE[note] * (op[FP_DET] - 7)) >> 24) + FM6_COARSE[op[FP_FC] & 31];
    return lf + FM6_FINE[op[FP_FF]];
}

/* ------------------------------------------------------------ envelopes --- */
typedef struct {
    int32_t level, target, inc, hold;                    /* Q24 log2; samples a static segment has left */
    uint8_t ix, rising;                                  /* segment 0..3 (L1..L4), 4 = done */
} fm6_eg_t;

typedef struct {                                         /* a pitch envelope (PitchEnv) */
    int32_t pl, pt, pi;                                  /* level, target, step (Q24 octaves) */
    uint8_t pix, prise, pr[4], pv[4];                    /* segment, direction, rates and levels (at the key) */
} fm6_peg_t;

/* segment ix begins (Env::advance); where it would not move (or an attack to L1 0) the DX7 waits */
static void fm6_eg_go(fm6_eg_t *e, const uint8_t *op, int32_t ol, int32_t rs, uint32_t ix)
{
    e->ix = (uint8_t)ix;
    if (ix < 4u) {
        int32_t lv = op[FP_L1 + ix], a = ((fm6_scaleout(lv) >> 1) << 6) + ol - 4256;
        int32_t q = clamp(((op[FP_R1 + ix] * 41) >> 6) + rs, 0, 63);
        e->target = (a < 16 ? 16 : a) << 16;
        e->rising = e->target > e->level;
        e->hold = 0;
        if (e->target == e->level || (!ix && !lv)) {
            int32_t r = clamp(op[FP_R1 + ix] + rs, 0, 99);
            e->hold = r < 77 ? FM6_STATICS[r] : 20 * (99 - r);
            if (r < 77 && !ix && !lv)
                e->hold /= 20;
        }
        e->inc = (4 + (q & 3)) << (2 + FM6_LG_N + (q >> 2));
    }
}

/* one block of an operator envelope (Env::getsample); returns its level. Segments 0..2 run
 * while the key is down, 3 after the release */
static int32_t fm6_eg_step(fm6_eg_t *e, const uint8_t *op, int32_t ol, int32_t rs, int down)
{
    if (e->hold) {
        e->hold -= FM6_N;
        if (e->hold <= 0) {
            e->hold = 0;
            fm6_eg_go(e, op, ol, rs, e->ix + 1u);
        }
    }
    if ((e->ix < 3u || (e->ix < 4u && !down)) && !e->hold) {
        if (e->rising) {                                 /* the DX7 attack: from -48 dB, faster when low */
            if (e->level < (1716 << 16))
                e->level = 1716 << 16;
            e->level += (((17 << 24) - e->level) >> 24) * e->inc;
            if (e->level >= e->target) {
                e->level = e->target;
                fm6_eg_go(e, op, ol, rs, e->ix + 1u);
            }
        } else {
            e->level -= e->inc;
            if (e->level <= e->target) {
                e->level = e->target;
                fm6_eg_go(e, op, ol, rs, e->ix + 1u);
            }
        }
    }
    return e->level;
}

static void fm6_peg_go(fm6_peg_t *e, uint32_t ix)        /* pitch EG segment ix (PitchEnv::advance) */
{
    e->pix = (uint8_t)ix;
    if (ix < 4u) {
        e->pt = FM6_PEG_STEP[e->pv[ix]] * (1 << 19);
        e->prise = e->pt > e->pl;
        e->pi = FM6_PEG_RATE[e->pr[ix]] * FM6_PEG_UNIT;
    }
}

static void fm6_peg_set(fm6_peg_t *e, const uint8_t *p)  /* a key: the voice's pitch EG starts from L4 (set) */
{
    uint32_t k;
    for (k = 0; k < 4u; k++) {
        e->pr[k] = p[FP_PR1 + k];
        e->pv[k] = p[FP_PL1 + k];
    }
    e->pl = FM6_PEG_STEP[e->pv[3]] * (1 << 19);
    fm6_peg_go(e, 0);
}

static int32_t fm6_peg_step(fm6_peg_t *e, int down)      /* pitch envelope, Q24 octaves */
{
    if (e->pix < 3u || (e->pix < 4u && !down)) {
        if (e->prise) {
            e->pl += e->pi;
            if (e->pl >= e->pt) {
                e->pl = e->pt;
                fm6_peg_go(e, e->pix + 1u);
            }
        } else {
            e->pl -= e->pi;
            if (e->pl <= e->pt) {
                e->pl = e->pt;
                fm6_peg_go(e, e->pix + 1u);
            }
        }
    }
    return e->pl;
}

/* ------------------------------------------------------------ the voice --- */
typedef struct fm6_voice {                               /* a voice; operators in Dexed's order: 0 = OP6 */
    fm6_eg_t eg[6];
    uint32_t ph[6];                                      /* phase, Q24 = a cycle */
    int32_t fq[6], gout[6], g[6], dg[6];                 /* step; gain at the block's end, now, per sample */
    int32_t base[6], porta[6];                           /* the note's log frequency; portamento's */
    int16_t ol[6];                                       /* output level (note, velocity), microsteps */
    int8_t rs[6];                                        /* rate scaling */
    uint8_t plan[6];                                     /* this block's routing (FM6_P_*) */
    int32_t fb[2];                                       /* the feedback operator's last two outputs */
    fm6_peg_t pe;                                        /* the pitch envelope */
    uint8_t down, sub, note, vel, eng, loop, fbs, quiet, played;
    uint8_t frozen;                                      /* stopped as by Dexed's panic: not kept running */
    uint8_t still;                                       /* over and at rest: 1 only time goes on, 2 nothing matters */
    int32_t spb;                                         /* the bend and tune it was at rest with */
} fm6_voice_t;
#define FM6_SILENT 1638400                               /* an envelope level no engine renders (MARK I: -96 dB) */
enum { FM6_P_ADD = 4, FM6_P_FB = 0x40, FM6_P_RUN = 0x80 };   /* plan: bits 0-1 out bus, 4-5 in bus */

/* ------------------------------------------------------ the operators --- */
/* a render's scratch: the modulator buses and the voice's sum. Two with ChoralRoot's second core (docs/DUALCORE.md:
 * core 1 renders part 1 while core 0 renders part 0, cr_cpu1.h CR_C1_IDX), one otherwise */
typedef struct { int32_t bus[2][CTL], sum[CTL]; } fm6_scr_t;
static fm6_scr_t fm6_scr[1 + CR_CPU1];

/* MARK I: a sine from the log-sine and exponent tables (mkiSin); env: attenuation, 1024 an octave.
 * As in Dexed the sum is 16 bits, the sign its top bit (a gain ramp that overshoots wraps it) */
static inline int32_t fm6_mki(int32_t ph, int32_t env)
{
    uint32_t e = ((uint32_t)FM6_MKI_LOG[((uint32_t)ph >> 12) & 2047u] + (((uint32_t)ph >> 8) & 0x8000u) + (uint32_t)env) &
                 0xFFFFu;
    int32_t y = (int32_t)(((uint32_t)FM6_MKI_EXP[e & 0x3FFu] >> ((e & 0x7FFFu) >> 10)) << 13);
    return e & 0x8000u ? -y - 8192 : y;
}

/* OPL: the OPL's quarter-wave ROMs (oplSin); env: attenuation, 8 a step of 3/8 dB; 16 bits as above */
static inline int32_t fm6_opl(int32_t ph, int32_t env)
{
    uint32_t e = ((uint32_t)FM6_OPL_LOG[((uint32_t)ph >> 14) & 511u] + (((uint32_t)ph >> 8) & 0x8000u) +
                  ((uint32_t)env << 3)) & 0xFFFFu, sh = (e & 0x7FFFu) >> 8;
    int32_t y = sh > 31u ? 0 : (int32_t)((uint32_t)FM6_OPL_EXP[e & 0xFFu] >> sh);
    return (e & 0x8000u ? -y - 1 : y) * (1 << 14);
}

/* an operator over n samples, modulated by in[] (0: none), added to or into out[] */
#define FM6_LOOP(Y)                                                                                \
    do {                                                                                           \
        if (add)                                                                                   \
            for (i = 0; i < n; i++) {                                                              \
                g += dg;                                                                           \
                out[i] += (Y);                                                                     \
                ph += fq;                                                                          \
            }                                                                                      \
        else                                                                                       \
            for (i = 0; i < n; i++) {                                                              \
                g += dg;                                                                           \
                out[i] = (Y);                                                                      \
                ph += fq;                                                                          \
            }                                                                                      \
    } while (0)
#define FM6_SIN_G(x) ((int32_t)(((int64_t)fm6_sin(x) * g) >> 24))
static void fm6_op(fm6_voice_t *s, uint32_t k, int32_t *out, const int32_t *in, int add, uint32_t eng, uint32_t n)
{
    uint32_t ph = s->ph[k], i, fq = (uint32_t)s->fq[k];
    int32_t g = s->g[k], dg = s->dg[k];
    if (eng == FM6_MODERN) {
        if (in)
            FM6_LOOP(FM6_SIN_G((int32_t)(ph + (uint32_t)in[i])));
        else
            FM6_LOOP(FM6_SIN_G((int32_t)ph));
    } else if (eng == FM6_MARK1) {
        if (in)
            FM6_LOOP(fm6_mki((int32_t)(ph + (uint32_t)in[i]), g));
        else
            FM6_LOOP(fm6_mki((int32_t)ph, g));
    } else {
        if (in)
            FM6_LOOP(fm6_opl((int32_t)(ph + (uint32_t)in[i]), g));
        else
            FM6_LOOP(fm6_opl((int32_t)ph, g));
    }
    s->ph[k] = ph;
    s->g[k] = g;
}

/* the feedback operator: its last two outputs, averaged, modulate it */
#define FM6_LOOP_FB(Y)                                                                             \
    do {                                                                                           \
        if (add)                                                                                   \
            for (i = 0; i < n; i++) {                                                              \
                g += dg;                                                                           \
                m = (y0 + y) >> sh;                                                                \
                y0 = y;                                                                            \
                y = (Y);                                                                           \
                out[i] += y;                                                                       \
                ph += fq;                                                                          \
            }                                                                                      \
        else                                                                                       \
            for (i = 0; i < n; i++) {                                                              \
                g += dg;                                                                           \
                m = (y0 + y) >> sh;                                                                \
                y0 = y;                                                                            \
                y = (Y);                                                                           \
                out[i] = y;                                                                        \
                ph += fq;                                                                          \
            }                                                                                      \
    } while (0)
static void fm6_op_fb(fm6_voice_t *s, uint32_t k, int32_t *out, int add, uint32_t eng, uint32_t n)
{
    uint32_t ph = s->ph[k], i, fq = (uint32_t)s->fq[k], sh = s->fbs + 1u;
    int32_t g = s->g[k], dg = s->dg[k], y0 = s->fb[0], y = s->fb[1], m;
    if (eng == FM6_MODERN)
        FM6_LOOP_FB(FM6_SIN_G((int32_t)(ph + (uint32_t)m)));
    else if (eng == FM6_MARK1)
        FM6_LOOP_FB(fm6_mki((int32_t)(ph + (uint32_t)m), g));
    else
        FM6_LOOP_FB(fm6_opl((int32_t)(ph + (uint32_t)m), g));
    s->ph[k] = ph;
    s->g[k] = g;
    s->fb[0] = y0;
    s->fb[1] = y;
}

/* MARK I, algorithms 4 and 6 with feedback: OP6 -> OP5 (-> OP4) and back to OP6, into the voice */
static void fm6_op_loop(fm6_voice_t *s, int32_t *out, uint32_t n)
{
    uint32_t i, j, nl = s->loop, sh = s->fbs + 1u;
    int32_t y0 = s->fb[0], y = s->fb[1], m;
    for (i = 0; i < n; i++) {
        m = (y0 + y) >> sh;
        s->g[0] += s->dg[0];
        y0 = y;
        y = fm6_mki((int32_t)(s->ph[0] + (uint32_t)m), s->g[0]);
        s->ph[0] += (uint32_t)s->fq[0];
        for (j = 1; j < nl; j++) {
            y = fm6_mki((int32_t)(s->ph[j] + (uint32_t)y), s->g[j]);
            s->ph[j] += (uint32_t)s->fq[j];
        }
        out[i] = y;
    }
    s->fb[0] = y0;
    s->fb[1] = y;
}

/* the block's routing (FmCore / EngineMkI / EngineOpl render): which operators sound, into what,
 * from what, their gain ramps; operators below the threshold only keep time */
static void fm6_plan(fm6_voice_t *s, const int32_t *lv, uint32_t alg, uint32_t eng, uint32_t fbshift)
{
    uint32_t k, has = 1u, fb_on = fbshift < 16u;
    s->eng = (uint8_t)eng;
    s->loop = 0;
    for (k = 0; k < 6u; k++) {
        uint32_t f = FM6_ALG[alg][k], out = f & 3u, in = (f >> 4) & 3u, add = f & 4u, run;
        int32_t g1, g2;
        if (eng == FM6_MARK1 && !k && fb_on && (alg == 3u || alg == 5u))
            f = 0xc4u, out = 0, in = 0, add = 4u;        /* MARK I: the loop runs from OP6 */
        if (eng == FM6_MODERN) {
            g1 = s->gout[k];
            g2 = fm6_exp2(lv[k] - (14 << 24));
            run = g1 >= 1120 || g2 >= 1120;
        } else if (eng == FM6_MARK1) {
            g1 = s->gout[k] ? s->gout[k] : 16383;
            g2 = 16384 - (lv[k] >> 14);
            run = g1 <= 16284 || g2 <= 16284;
        } else {
            g1 = s->gout[k] ? s->gout[k] : 511;
            g2 = 512 - (lv[k] >> 19);
            run = g1 <= 507 || g2 <= 507;
        }
        s->gout[k] = g2;
        s->plan[k] = 0;
        if (!run) {
            if (!add)
                has &= ~(1u << out);
            s->ph[k] += (uint32_t)s->fq[k] << FM6_LG_N;
            continue;
        }
        if (!((has >> out) & 1u))
            add = 0;
        if (in && !((has >> in) & 1u))
            in = 0;
        s->g[k] = g1;
        s->dg[k] = (g2 - g1 + (FM6_N >> 1)) >> FM6_LG_N;
        s->plan[k] = (uint8_t)(FM6_P_RUN | out | add | in << 4);
        if (!in && (f & 0xc0u) == 0xc0u && fb_on) {
            s->plan[k] |= FM6_P_FB;
            s->fbs = (uint8_t)fbshift;
            if (eng == FM6_MARK1 && (alg == 3u || alg == 5u || alg == 31u))
                s->fbs = (uint8_t)(fbshift + 2u < 16u ? fbshift + 2u : 16u);
            if (eng == FM6_MARK1 && (alg == 3u || alg == 5u)) {   /* the loop's other operators: a fixed gain */
                uint32_t j;
                s->loop = (uint8_t)(alg == 3u ? 3 : 2);
                for (j = 1; j < s->loop; j++) {
                    s->gout[j] = 16384 - (lv[j] >> 14);
                    s->g[j] = s->gout[j] ? s->gout[j] : 16383;
                    s->dg[j] = 0;
                    s->plan[j] = 0;
                }
                k += s->loop - 1u;
            }
        }
        has |= 1u << out;
    }
}

static void fm6_run(fm6_voice_t *s, uint32_t n, fm6_scr_t *S)   /* the planned operators over n samples into S->sum */
{
    uint32_t k, eng = s->eng;
    for (k = 0; k < n; k++)
        S->sum[k] = 0;
    for (k = 0; k < 6u; k++) {
        uint32_t pl = s->plan[k], o = pl & 3u, in = (pl >> 4) & 3u;
        int32_t *out = o ? S->bus[o - 1u] : S->sum;
        if (!(pl & FM6_P_RUN))
            continue;
        if (!k && s->loop)
            fm6_op_loop(s, out, n);
        else if (pl & FM6_P_FB)
            fm6_op_fb(s, k, out, pl & FM6_P_ADD, eng, n);
        else
            fm6_op(s, k, out, in ? S->bus[in - 1u] : 0, pl & FM6_P_ADD, eng, n);
    }
}
