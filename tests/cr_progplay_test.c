/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of the progressions' PLAYING side (GitHub issue 64; cr_engine.h cr_prog_key, CR_Q_PROG; cr_loop.h
 * CRL_Q_PROG0) on the real engine, looper and store (the factory set: cr_prog.c without flash):
 *   cc -std=c99 -Wall -Wextra -Werror -pedantic -O2 -o build/host/cr_progplay_test tests/cr_progplay_test.c
 * (tests/run_cr_tests.sh runs it.) Checks: the chord recogniser (the factory set's 48 chords by name, the table, slash
 * and bass-less names, no match); the UI's key and velocity maths (CR_PROG_I, CR_PROG_VEL); a trigger's chord (the
 * stored notes + Transpose, folded into 0..127, voiced by KNOB 1), a silent key (no note, no gesture), a release, the
 * bass (the lowest note's pitch class), Humanize's bounds; the reference a gesture, a pad and a loop carry (the loop
 * record's packing, its transpose window, the editor's step transpose, unpack's checks); a loop and a pad playing a
 * reference, and silent once its key is emptied; the chord-info naming while held and after. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../firmware/src/cr_engine.c"
#include "../firmware/src/cr_loop.c"
#include "../firmware/src/cr_prog.c"

static int passed, failed;
static void ok(int c, const char *n)
{
    if (c) passed++;
    else { failed++; printf("FAIL %s\n", n); }
}

static cr_t C;
static cr_loop_t L;
static uint32_t NOW;
static struct { uint8_t s, note, vel, on; } EV[4096];
static int nev;
static int gest_n, gest_on;
static int16_t g_root;
static uint8_t g_q, g_ext;
static void cb_on(void *ud, cr_stream_t s, uint8_t n, uint8_t v)
{
    (void)ud;
    if (nev < 4096) { EV[nev].s = (uint8_t)s; EV[nev].note = n; EV[nev].vel = v; EV[nev].on = 1; nev++; }
}
static void cb_off(void *ud, cr_stream_t s, uint8_t n)
{
    (void)ud;
    if (nev < 4096) { EV[nev].s = (uint8_t)s; EV[nev].note = n; EV[nev].vel = 0; EV[nev].on = 0; nev++; }
}
static void cb_all(void *ud, cr_stream_t s) { (void)ud; (void)s; }
static void cb_gesture(void *ud, uint32_t gid, int16_t root, uint8_t q, uint8_t ext, uint8_t vel, int on)
{
    (void)ud;
    gest_n++;
    gest_on = on;
    g_root = root; g_q = q; g_ext = ext;
    cr_loop_gesture(&L, gid, root, q, ext, vel, on);
}
static const cr_out_t OUT = { cb_on, cb_off, cb_all, NULL, cb_gesture };
static void step(uint32_t ms)
{
    while (ms--) {
        NOW++;
        cr_tick(&C, NOW);
        cr_loop_tick(&L, &C, NOW);
    }
}
static void reset(void)
{
    nev = gest_n = 0;
    NOW = 1000;
    crp_init();                                    /* the factory set */
    cr_prog_source(crp_chord);
    cr_init(&C, &OUT);
    cr_loop_init(&L);
    cr_tick(&C, NOW);
    cr_loop_tick(&L, &C, NOW);
}
/* the MAIN stream's notes started since log entry from, ascending; their count */
static int ons(int from, uint8_t *out, uint8_t *vel)
{
    int k, n = 0, i, j;
    for (k = from; k < nev; k++)
        if (EV[k].on && EV[k].s == CR_STREAM_MAIN && n < 32) { out[n] = EV[k].note; if (vel) vel[n] = EV[k].vel; n++; }
    for (i = 1; i < n; i++)
        for (j = i; j > 0 && out[j - 1] > out[j]; j--) {
            uint8_t t = out[j]; out[j] = out[j - 1]; out[j - 1] = t;
            if (vel) { t = vel[j]; vel[j] = vel[j - 1]; vel[j - 1] = t; }
        }
    return n;
}
static int same(const uint8_t *a, int n, const uint8_t *b, int m)
{
    return n == m && !memcmp(a, b, (size_t)n);
}
static int sounding(int s)
{
    int n, k = 0;
    for (n = 0; n < 128; n++) k += C.reg[s][n] != 0;
    return k;
}
static void name_of(const uint8_t *n, int m, char *out)
{
    char r[3], q[5], s[CR_SUP_MAX];
    cr_chord_recognise(n, m, r, q, s);
    sprintf(out, "%s%s%s", r, q, s);
}

/* a long progression in the RAM copy: slot 9 (index 8) "Long", 48 keys, key k = a C major triad on 48 + k (k % 5 == 4:
 * silent) */
static void long_prog(void)
{
    uint8_t *b = crp_x.blk, *d = b + CRPG_HDR + 8u * CRPG_DIRENT;
    uint32_t at = crp_x.len, k;
    memcpy(d, "Long", 4);
    d[12] = 48; d[13] = 48; d[14] = (uint8_t)at; d[15] = (uint8_t)(at >> 8);
    for (k = 0; k < 48u; k++) {
        if (k % 5u == 4u) { b[at++] = 0; continue; }
        b[at++] = 3; b[at++] = (uint8_t)(48u + k); b[at++] = (uint8_t)(52u + k); b[at++] = (uint8_t)(55u + k);
    }
    b[6] = (uint8_t)at; b[7] = (uint8_t)(at >> 8);
    crp_x.len = (uint16_t)at;
}

static void t_names(void)
{
    static const char *const FAC[6][8] = {
        {"CM", "G/B", "Am", "FM", "C/E", "GM", "FM7", "Gsus4"},
        {"Dm9", "G13", "CM9", "A7b9", "Dm9", "G7b9", "CM7", "C6/9"},
        {"D#M9", "Fm9", "Gm7", "G#M7", "A#13", "Cm11", "G#M9", "A#7sus4"},
        {"CM", "C7/E", "FM", "F#dim7", "C/G", "A7", "Dm7", "G7"},
        {"Am", "FM", "CM", "GM", "Dm", "E7", "Am/E", "EM"},
        {"Dm7", "Em7", "FM7", "G7", "Am7", "CM7", "Dm9", "G13"}};
    char nm[32], msg[96];
    unsigned p, i, bad = 0;
    uint8_t n[8];
    reset();
    for (p = 0; p < 6u; p++)
        for (i = 0; i < 8u; i++) {
            unsigned m = crp_chord(p, i, n);
            name_of(n, (int)m, nm);
            if (strcmp(nm, FAC[p][i])) {
                printf("  factory %u key %u: %s, not %s\n", p + 1u, i + 1u, nm, FAC[p][i]);
                bad++;
            }
        }
    ok(!bad, "names: the factory set's 48 chords as docs/PROGRESSIONS.md names them (sharps, the M of a major triad)");
    {
        static const struct { uint8_t n[8], m; const char *nm; } T[] = {
            {{60, 63, 66, 69}, 4, "Cdim7"}, {{60, 63, 66, 70}, 4, "Cm7b5"}, {{60, 64, 67, 70, 75}, 5, "C7#9"},
            {{60, 64, 68, 70, 75}, 5, "C7#5#9"}, {{60, 61, 64, 66, 70}, 5, "C7b5b9"}, {{60, 62, 64, 67}, 4, "Cadd9"},
            {{60, 62, 64, 65, 67, 70}, 6, "C11"}, {{60, 62, 63, 65, 67, 70}, 6, "Cm11"}, {{60, 64, 68}, 3, "C+"},
            {{60, 62, 67}, 3, "Csus2"}, {{48, 55}, 2, "C5"}, {{60, 63, 67, 71}, 4, "CmM7"}, {{60, 64, 70}, 3, "C7"},
            {{60, 61}, 2, "C"}, {{60}, 1, "C"}, {{52, 60, 64, 67, 70}, 5, "C7/E"}, {{50, 60, 64, 67}, 4, "Cadd9/D"},
            {{62, 60, 64, 67}, 4, "Cadd9"}, {{48, 52, 55, 59, 62, 64, 67, 71}, 8, "CM9"},
            {{40, 49, 53, 56}, 4, "C#/E"}};
        unsigned k;
        for (k = 0, bad = 0; k < sizeof T / sizeof T[0]; k++) {
            name_of(T[k].n, T[k].m, nm);
            if (strcmp(nm, T[k].nm)) { printf("  case %u: %s, not %s\n", k, nm, T[k].nm); bad++; }
        }
        snprintf(msg, sizeof msg, "names: %u cases (dim7, m7b5, 7#9, 7#5#9, 7b5b9, 11, a slash bass, no match: the bass)",
                 (unsigned)(sizeof T / sizeof T[0]));
        ok(!bad, msg);
    }
    {                                              /* every root of the table: names stay inside the strings */
        unsigned r, k, fit = 1;
        char rr[3], q[5], s[CR_SUP_MAX];
        for (r = 0; r < 12u; r++)
            for (k = 0; k < (unsigned)CRX_NRECOG; k++) {
                uint8_t m[12], x = 0, j;
                for (j = 0; j < 12u; j++)
                    if ((CRX_RECOG[k].m >> j) & 1u) m[x++] = (uint8_t)(48u + r + j);
                cr_chord_recognise(m, x > 8 ? 8 : x, rr, q, s);
                if (strcmp(rr, CR_NOTE_NAMES[r]) || strcmp(q, CRX_RECOG[k].q) || strcmp(s, CRX_RECOG[k].s)) {
                    if (r == 0u) printf("  table %u on C: %s%s%s\n", k, rr, q, s);
                    fit = 0;
                }
                fit &= strlen(rr) <= 2u && strlen(q) <= 4u && strlen(s) < (size_t)CR_SUP_MAX;
            }
        ok(fit, "names: every table chord over every root: named on that root (the table has no two alike), and fits");
    }
}

static void t_maths(void)
{
    ok(CR_PROG_I(0, 0) == 0u && CR_PROG_I(26, 0) == 26u && CR_PROG_I(0, 1) == 12u && CR_PROG_I(26, 1) == 38u &&
       CR_PROG_I(11, 3) == 47u && CR_PROG_I(36, 3) == 72u, "maths: key i = trigger + 12 x window (FM-1 F3 = 0, window 0..3)");
    ok(CR_PROG_VEL(100, 100) == 100u && CR_PROG_VEL(100, 50) == 50u && CR_PROG_VEL(100, 150) == 127u &&
       CR_PROG_VEL(1, 50) == 1u && CR_PROG_VEL(80, 150) == 120u && CR_PROG_VEL(127, 100) == 127u && CR_PROG_VEL(3, 50) == 1u,
       "maths: velocity x Velocity % (50..150), 1..127");
}

static void t_play(void)
{
    uint8_t n[32], v[32], want[8];
    int k, m, from;
    char nm[32];
    cr_chord_info_t ci;
    reset();
    from = nev;
    cr_prog_key(&C, 3, 0, 0, 90, 1);               /* Pop Four key 1: C (36 52 55 60 64) */
    m = ons(from, n, v);
    memcpy(want, (const uint8_t[]){36, 52, 55, 60, 64}, 5);
    ok(same(n, m, want, 5) && v[0] == 90 && v[4] == 90, "play: a trigger sounds its stored notes at its velocity");
    ok(gest_n == 1 && gest_on == 1 && g_q == CR_Q_PROG && g_ext == 0 && CR_PROG_KEY(g_root) == 0u && CR_PROG_TR(g_root) == 0,
       "play: the gesture is the reference (CR_Q_PROG, progression 0, key 0, transpose 0)");
    cr_chord_info(&C, &ci);
    snprintf(nm, sizeof nm, "%s%s%s", ci.root, ci.qual, ci.sup);
    ok(ci.sounding && ci.quality == CR_Q_PROG && ci.prog_key == 0 && ci.ext == 0 && !strcmp(nm, "CM") && ci.root_pc == 0,
       "play: the chord shown: CR_Q_PROG, its key, named CM, the bass's pitch class");
    cr_prog_key(&C, 3, 0, 0, 0, 0);
    ok(!sounding(CR_STREAM_MAIN) && gest_on == 0, "play: the release ends it (and the gesture)");
    cr_chord_info(&C, &ci);
    snprintf(nm, sizeof nm, "%s%s%s", ci.root, ci.qual, ci.sup);
    ok(!ci.sounding && ci.valid && ci.quality == CR_Q_PROG && !strcmp(nm, "CM"), "play: let go: still named (the last chord)");
    /* a silent key: nothing at all */
    from = nev; k = gest_n;
    cr_prog_key(&C, 4, 0, 8, 100, 1);              /* Pop Four has keys 0..7 */
    ok(nev == from && gest_n == k && !cr_voices(&C) && !C.phys_n, "play: a key past the progression: no note, no gesture, no voice");
    cr_prog_key(&C, 4, 0, 8, 0, 0);
    cr_prog_key(&C, 4, 7, 0, 100, 1);              /* an empty slot (8) */
    ok(nev == from && !cr_voices(&C), "play: an empty progression's key: silent");
    cr_prog_key(&C, 4, 7, 0, 0, 0);
    /* Transpose, then the voicing */
    cr_set_transpose(&C, 2);
    from = nev;
    cr_prog_key(&C, 5, 1, 0, 100, 1);              /* Jazz key 1: Dm9 38 53 57 60 64, + 2 */
    m = ons(from, n, NULL);
    memcpy(want, (const uint8_t[]){40, 55, 59, 62, 66}, 5);
    ok(same(n, m, want, 5) && CR_PROG_TR(g_root) == 2, "play: + Transpose 2 (the reference carries it)");
    cr_voicing_step(&C, 2);                        /* two rotations: the two lowest an octave up */
    for (k = 0, m = 0; k < 128; k++) if (C.reg[CR_STREAM_MAIN][k]) n[m++] = (uint8_t)k;
    memcpy(want, (const uint8_t[]){55, 59, 62, 64, 66}, 5);
    ok(same(n, m, want, 5), "play: KNOB 1 voices it live (+2: 40 -> 52 -> 64)");
    cr_chord_info(&C, &ci);
    snprintf(nm, sizeof nm, "%s%s%s", ci.root, ci.qual, ci.sup);
    ok(!strcmp(nm, "Em9") && ci.nnotes == 5 && ci.notes[0] == 55, "play: named from the notes as stored (Em9), shown as voiced");
    cr_prog_key(&C, 5, 1, 0, 0, 0);
    cr_voicing_step(&C, -2);
    cr_set_transpose(&C, 24);                      /* fold into 0..127: 70 + 24 = 94 .. fine; a high one: */
    from = nev;
    cr_prog_key(&C, 6, 2, 6, 100, 1);              /* Neo Soul key 7: 44 60 63 67 70 + 24 */
    m = ons(from, n, NULL);
    ok(m == 5 && n[0] == 68 && n[4] == 94, "play: + 24 (inside 0..127)");
    cr_prog_key(&C, 6, 2, 6, 0, 0);
    cr_set_transpose(&C, 0);
    {                                              /* the fold: notes beyond 127 move down by octaves */
        cr_voice_t t;
        memset(&t, 0, sizeof t);
        ok(crx_prog_build(&C, &t, 0, 0, 24) && t.nbase == 5 && t.base[0] == 60 && t.base[4] == 88 &&
           crx_prog_build(&C, &t, 0, 0, -24) && t.base[0] == 12, "play: the build: transpose -24 / +24 inside 0..127");
    }
    /* the bass: the lowest note's pitch class */
    cr_set_bass(&C, 1);
    from = nev;
    cr_prog_key(&C, 7, 0, 1, 100, 1);              /* Pop Four key 2: G/B, bass B */
    for (k = from, m = 0; k < nev; k++) if (EV[k].on && EV[k].s == CR_STREAM_BASS) n[m++] = EV[k].note;
    ok(m == 1 && n[0] % 12u == 11u, "play: the bass part plays the lowest note's pitch class (B of G/B)");
    cr_prog_key(&C, 7, 0, 1, 0, 0);
    cr_set_bass(&C, 0);
    /* Key Mode, the chord keys: nothing to it */
    cr_set_key(&C, 1, 2, CR_SCALE_MINOR);
    cr_mod(&C, CR_MOD_MIN, 1);
    from = nev;
    cr_prog_key(&C, 8, 0, 0, 100, 1);
    m = ons(from, n, NULL);
    memcpy(want, (const uint8_t[]){36, 52, 55, 60, 64}, 5);
    ok(same(n, m, want, 5), "play: Key Mode and a chord key held change nothing");
    cr_mod(&C, CR_MOD_MAJ7, 1);                    /* (Advanced would re-type a root's voice: not this one) */
    for (k = 0, m = 0; k < 128; k++) if (C.reg[CR_STREAM_MAIN][k]) n[m++] = (uint8_t)k;
    ok(same(n, m, want, 5), "play: a chord key pressed over it: the chord stays");
    cr_mod(&C, CR_MOD_MAJ7, 0);
    cr_mod(&C, CR_MOD_MIN, 0);
    cr_prog_key(&C, 8, 0, 0, 0, 0);
    cr_set_key(&C, 0, 0, CR_SCALE_MAJOR);
    ok(!cr_voices(&C) && !sounding(CR_STREAM_MAIN) && !C.phys_n, "play: all let go: no voice, no note, nothing held");
    /* the home key (crp_home) moved to Key Mode's tonic, the nearest way (the owner, 2026-11-19) */
    cr_prog_home_source(crp_home);
    cr_set_key(&C, 1, 2, CR_SCALE_MAJOR);          /* D: Pop Four (home C) up 2 */
    from = nev;
    cr_prog_key(&C, 8, 0, 0, 100, 1);
    m = ons(from, n, NULL);
    memcpy(want, (const uint8_t[]){38, 54, 57, 62, 66}, 5);
    ok(same(n, m, want, 5), "home key: Pop Four (C) in D: every note up 2");
    cr_prog_key(&C, 8, 0, 0, 0, 0);
    cr_set_key(&C, 1, 9, CR_SCALE_MINOR);          /* A: the nearest way is down 3 */
    from = nev;
    cr_prog_key(&C, 8, 0, 0, 100, 1);
    m = ons(from, n, NULL);
    memcpy(want, (const uint8_t[]){33, 49, 52, 57, 61}, 5);
    ok(same(n, m, want, 5), "home key: Pop Four in A: down 3 (not up 9)");
    cr_prog_key(&C, 8, 0, 0, 0, 0);
    cr_set_key(&C, 1, 3, CR_SCALE_MAJOR);          /* Neo Soul (home Eb) in Eb: as stored */
    from = nev;
    cr_prog_key(&C, 8, 2, 0, 100, 1);
    m = ons(from, n, NULL);
    memcpy(want, (const uint8_t[]){39, 55, 58, 62, 65}, 5);
    ok(same(n, m, want, 5), "home key: Neo Soul (Eb) in Eb: as stored");
    cr_prog_key(&C, 8, 2, 0, 0, 0);
    cr_set_key(&C, 0, 3, CR_SCALE_MAJOR);          /* Key Mode off: no move */
    from = nev;
    cr_prog_key(&C, 8, 0, 0, 100, 1);
    m = ons(from, n, NULL);
    memcpy(want, (const uint8_t[]){36, 52, 55, 60, 64}, 5);
    ok(same(n, m, want, 5), "home key: Key Mode off: as stored");
    cr_prog_key(&C, 8, 0, 0, 0, 0);
    cr_prog_home_source(0);
    cr_set_key(&C, 0, 0, CR_SCALE_MAJOR);
    step(300);
    /* a root (MIDI Roots) on the same number: its own */
    cr_key(&C, 3, 100, 1);
    cr_prog_key(&C, 3, 0, 0, 100, 1);
    cr_key(&C, 3, 0, 0);
    ok(cr_voices(&C) == 1 && sounding(CR_STREAM_MAIN) == 5, "play: a root and a trigger of the same number apart (the root's release ends the root)");
    cr_prog_key(&C, 3, 0, 0, 0, 0);
    ok(!cr_voices(&C), "play: .. the trigger's its own");
}

static void t_humanize(void)
{
    uint8_t n[32], v[32];
    int k, m, lo = 127, hi = 0, from, varied = 0;
    reset();
    cr_seed(&C, 12345);
    cr_set_prog_humanize(&C, 100);                 /* +-40 */
    for (k = 0; k < 200; k++) {
        int i;
        from = nev = 0;
        cr_prog_key(&C, 1, 0, (uint8_t)(k % 8), 100, 1);
        m = ons(from, n, v);
        for (i = 0; i < m; i++) {
            if (v[i] < lo) lo = v[i];
            if (v[i] > hi) hi = v[i];
            varied |= v[i] != v[0];
        }
        cr_prog_key(&C, 1, 0, 0, 0, 0);
    }
    ok(lo >= 60 && hi <= 127 && lo <= 64 && hi >= 125 && varied, "humanize 100 %: 100 +- 40 (60 .. 127 clamped), each note its own");
    lo = 127; hi = 0;
    for (k = 0; k < 200; k++) {
        int i;
        from = nev = 0;
        cr_prog_key(&C, 1, 0, 0, 10, 1);
        m = ons(from, n, v);
        for (i = 0; i < m; i++) { if (v[i] < lo) lo = v[i]; if (v[i] > hi) hi = v[i]; }
        cr_prog_key(&C, 1, 0, 0, 0, 0);
    }
    ok(lo == 1 && hi <= 50, "humanize 100 % at velocity 10: clamped at 1");
    cr_set_prog_humanize(&C, 25);                  /* +-10 */
    lo = 127; hi = 0;
    for (k = 0; k < 200; k++) {
        int i;
        from = nev = 0;
        cr_prog_key(&C, 1, 0, 0, 64, 1);
        m = ons(from, n, v);
        for (i = 0; i < m; i++) { if (v[i] < lo) lo = v[i]; if (v[i] > hi) hi = v[i]; }
        cr_prog_key(&C, 1, 0, 0, 0, 0);
    }
    ok(lo >= 54 && hi <= 74 && lo <= 56 && hi >= 72, "humanize 25 %: 64 +- 10");
    cr_set_prog_humanize(&C, 0);
    from = nev;
    cr_prog_key(&C, 1, 0, 0, 64, 1);
    m = ons(from, n, v);
    ok(m == 5 && v[0] == 64 && v[4] == 64, "humanize 0: as played");
    cr_prog_key(&C, 1, 0, 0, 0, 0);
    cr_set_prog_humanize(&C, 300);
    ok(C.prog_hum == 100, "humanize: clamped to 100");
    cr_set_prog_humanize(&C, 0);
    from = nev;                                    /* a root's chord: never humanized */
    cr_set_prog_humanize(&C, 100);
    cr_mod(&C, CR_MOD_MAJ, 1);
    cr_key(&C, 60, 64, 1);
    m = ons(from, n, v);
    ok(m == 3 && v[0] == 64 && v[1] == 64 && v[2] == 64, "humanize: a root's chord as played");
    cr_key(&C, 60, 0, 0);
    cr_mod(&C, CR_MOD_MAJ, 0);
}

static void t_loop(void)
{
    uint8_t n[32], rec[CRL_REC_MAX];
    crl_data_t *d2 = (crl_data_t *)calloc(1, sizeof *d2);
    int m, from, k;
    uint32_t len;
    reset();
    L.mode = CRL_MODE_OVERDUB;
    /* the packing */
    {
        crl_ev_t e;
        int t, bad = 0;
        for (t = -24; t <= 24; t++) {
            int want = t;
            while (want > 7) want -= 12;
            while (want < -7) want += 12;
            for (k = 0; k < 48; k++) {
                int16_t r;
                crl_ev_chord(&e, CR_PROG_REF(k, t), CR_Q_PROG, 15);
                r = CRL_PROG_REF(e.root, e.qx);
                bad += !CRL_EV_PROG(e.qx) || (e.qx & 15u) != 15u || CR_PROG_KEY(r) != (unsigned)k || CR_PROG_TR(r) != want ||
                       e.root > 239u || (e.qx >> 4) < 13u;
            }
        }
        ok(!bad, "loop record: a reference packs into root / qx (key, progression, transpose -7..+7; outside it by octaves)");
    }
    /* record a take: Pop Four key 1, then key 4 with transpose 3 */
    cr_loop_rec(&L, &C);
    cr_prog_key(&C, 0, 0, 0, 100, 1);
    step(500);
    cr_prog_key(&C, 0, 0, 0, 0, 0);
    step(500);
    cr_set_transpose(&C, 3);
    cr_prog_key(&C, 1, 0, 3, 90, 1);
    step(500);
    cr_prog_key(&C, 1, 0, 3, 0, 0);
    cr_set_transpose(&C, 0);
    step(500);
    ok(cr_loop_rec(&L, &C) == CRL_DID_COMMIT && L.d.nev == 2 && CRL_EV_PROG(L.d.ev[0].qx) && CRL_EV_PROG(L.d.ev[1].qx) &&
       L.d.ev[0].root % 48u == 0u && L.d.ev[1].root % 48u == 3u && CRL_PROG_T(L.d.ev[1].root, L.d.ev[1].qx) == 3,
       "loop: a take of two Prog chords: two references (keys 1 and 4, transpose 0 and +3)");
    from = nev;
    step(2000);                                    /* a cycle */
    m = ons(from, n, NULL);
    ok(m == 10 && n[0] == 36 && n[9] == 68 + 0 && n[1] == 41 + 3, "loop: it plays them back: C, then F + 3 (41 53 57 60 65 -> 44 ..)");
    /* the record: packs and unpacks */
    len = cr_loop_pack(&L.d, rec, sizeof rec);
    ok(len && cr_loop_unpack(rec, len, d2) && d2->nev == 2 && d2->ev[1].root == L.d.ev[1].root && d2->ev[1].qx == L.d.ev[1].qx,
       "loop record: written and read back with its references");
    {                                              /* a firmware before the progressions (0.36 .. 0.41): its unpack
                                                    * checks every event's quality < 13 and root <= 127 (cr_loop_test.c
                                                    * old_unpack_ok): the whole record refused, the slot empty there */
        uint32_t o = CRL_REC_HDR + 2u, i, refused = 1;
        for (i = 0; i < 2u; i++, o += 7u)
            refused &= rec[o + 4] > 127u || (rec[o + 6] >> 4) >= 13u;
        ok(refused, "loop record: each reference fails an older firmware's event check (the slot reads as empty there)");
    }
    rec[CRL_REC_HDR + 2u + 7u + 4u] = 240;         /* the second event's root out of range */
    ok(!cr_loop_unpack(rec, len, d2), "loop record: a reference's root > 239 refused");
    /* the editor's step transpose */
    cr_loop_stop(&L, &C);
    ok(cr_loop_ed_open(&L) == CRL_ED_OK, "loop editor: opens");
    cr_loop_ed_cursor(&L, L.d.ev[1].t / CRL_STEP);
    ok(cr_loop_ed_transpose(&L, 4) && CRL_PROG_T(L.d.ev[1].root, L.d.ev[1].qx) == 7 && L.d.ev[1].root % 48u == 3u,
       "loop editor: a Prog step transposed +4: +7, the same key");
    ok(!cr_loop_ed_transpose(&L, 1) && CRL_PROG_T(L.d.ev[1].root, L.d.ev[1].qx) == 7, "loop editor: past +7 refused");
    cr_loop_ed_close(&L, &C);
    /* a key emptied: the loop is silent there */
    {
        uint32_t at = crp_rec(0, 0);
        crp_x.blk[at] = 0;                         /* (Pop Four key 1: no chord: the records after it move) */
        memmove(crp_x.blk + at + 1, crp_x.blk + at + 6, crp_x.len - at - 6);
        crp_x.len = (uint16_t)(crp_x.len - 5u);
        for (k = 1; k < 6; k++) {
            uint8_t *dd = crp_x.blk + CRPG_HDR + (unsigned)k * CRPG_DIRENT;
            uint32_t off = (uint32_t)dd[14] | (uint32_t)dd[15] << 8;
            off -= 5u;
            dd[14] = (uint8_t)off; dd[15] = (uint8_t)(off >> 8);
        }
        crp_x.blk[6] = (uint8_t)crp_x.len; crp_x.blk[7] = (uint8_t)(crp_x.len >> 8);
        ok(crp_valid(crp_x.blk, crp_x.len) && !crp_chord(0, 0, n), "store: key 1 of Pop Four emptied (still a valid object)");
    }
    cr_loop_play(&L, &C);
    from = nev;
    step(2000);
    m = ons(from, n, NULL);
    ok(m == 5 && n[0] == 48, "loop: the emptied key's step is silent, the other plays (F + 7: references, not notes)");
    cr_loop_stop(&L, &C);
    /* a stored record from before: plays the same way */
    free(d2);
}

static void t_pad(void)
{
    uint8_t n[32];
    int m, from;
    int16_t root;
    uint8_t q, x, vel;
    reset();
    cr_pad_arm(&C, 2);
    cr_set_transpose(&C, -5);
    cr_prog_key(&C, 0, 4, 5, 77, 1);               /* Minor Epic key 6: E7 */
    cr_prog_key(&C, 0, 4, 5, 0, 0);
    cr_set_transpose(&C, 0);
    ok(cr_pad_get(&C, 2, &root, &q, &x, &vel) && q == CR_Q_PROG && x == 4 && CR_PROG_KEY(root) == 5u && CR_PROG_TR(root) == -5 &&
       vel == 77, "pad: records the reference (progression 5, key 6, transpose -5)");
    from = nev;
    cr_pad_down(&C, 2);
    m = ons(from, n, NULL);
    ok(m == 5 && n[0] == 35 && n[4] == 59, "pad: plays it (E7 - 5)");
    cr_pad_up(&C, 2);
    cr_pad_set(&C, 3, 1, CR_PROG_REF(9, 0), CR_Q_PROG, 0, 100);   /* Pop Four key 10: none */
    from = nev;
    cr_pad_down(&C, 3);
    ok(nev == from && !cr_voices(&C), "pad: a reference to an empty key: silent");
    /* a loop step: the long progression's key 40 by reference */
    long_prog();
    from = nev;
    ok(cr_loop_event(&C, 0, CR_PROG_REF(40, 0), CR_Q_PROG, 8, 100, 1) == 1, "loop voice: a reference to key 41 of a 48-key progression");
    m = ons(from, n, NULL);
    ok(m == 3 && n[0] == 88 && n[2] == 95, "loop voice: its notes (88 92 95)");
    cr_loop_event(&C, 0, 0, 0, 0, 0, 0);
    ok(!cr_loop_event(&C, 0, CR_PROG_REF(44, 0), CR_Q_PROG, 8, 100, 1) && !cr_voices(&C),
       "loop voice: a silent key of it: nothing");
    from = nev;
    cr_prog_key(&C, 0, 8, CR_PROG_I(11, 3), 100, 1);   /* window 3, trigger 11: key 47 */
    m = ons(from, n, NULL);
    ok(m == 3 && n[0] == 95, "window 3: trigger 11 plays key 48 (95 99 102)");
    cr_prog_key(&C, 0, 8, 0, 0, 0);
}

int main(void)
{
    t_names();
    t_maths();
    t_play();
    t_humanize();
    t_loop();
    t_pad();
    printf("cr_progplay_test: %d passed, %d failed\n", passed, failed);
    return failed != 0;
}
