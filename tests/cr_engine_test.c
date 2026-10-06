/* SPDX-License-Identifier: GPL-3.0-only
 * Host tests of the ChoralRoot engine (firmware/src/cr_engine.c), ported from choralroot's
 * dev/test_choralroot.lua engine cases, plus the parts the grid build did not have (bass behaviours, global
 * transpose, BPM-scaled finite modes, chord names, wrap-around time, pool limits, two instances).
 *   cc -std=c99 -Wall -Wextra -Werror -o build/host/cr_engine_test tests/cr_engine_test.c firmware/src/cr_engine.c
 * The time-driven suite runs four times: 1 ms ticks, the firmware's 2.9 ms audio block, the Lua's 5 ms tick, and
 * 2.9 ms with the millisecond clock wrapping through 2^32 mid-run. One line per check; exit 1 on any failure.
 * Grid-test names are kept in the check names where a case is a port (C3 = MIDI 48; row y -> velocity). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "../firmware/src/cr_engine.h"

/* ------------------------------------------------------------ harness --- */
typedef struct { char k; uint8_t s, note, vel; uint64_t t; uint32_t ms; } logev_t;
#define MAXLOG 30000
static logev_t LOG[MAXLOG];
static int nlog;
static uint8_t SND[3][128];
static int dup_on, stray_off, n_alloff[3];
static uint64_t T, next_tick;     /* simulated time, microseconds */
static uint32_t TICK_US, BASE_MS;
static cr_t C;
static int passed, failed;
static char TAG[32];
static const uint8_t VEL[8] = { 127, 111, 95, 79, 64, 48, 32, 16 };   /* grid rows y1..y8 */

static uint32_t now_ms(void) { return BASE_MS + (uint32_t)(T / 1000u); }

static void cb_on(void *ud, cr_stream_t s, uint8_t n, uint8_t v)
{
    (void)ud;
    if (nlog < MAXLOG) { LOG[nlog].k = '+'; LOG[nlog].s = (uint8_t)s; LOG[nlog].note = n; LOG[nlog].vel = v; LOG[nlog].t = T; LOG[nlog].ms = C.now; nlog++; }
    if (SND[s][n]) dup_on++;
    SND[s][n] = 1;
}
static void cb_off(void *ud, cr_stream_t s, uint8_t n)
{
    (void)ud;
    if (nlog < MAXLOG) { LOG[nlog].k = '-'; LOG[nlog].s = (uint8_t)s; LOG[nlog].note = n; LOG[nlog].vel = 0; LOG[nlog].t = T; LOG[nlog].ms = C.now; nlog++; }
    if (!SND[s][n]) stray_off++;
    SND[s][n] = 0;
}
static void cb_all(void *ud, cr_stream_t s)
{
    (void)ud;
    memset(SND[s], 0, sizeof SND[s]);
    n_alloff[s]++;
}
static const cr_out_t OUT = { cb_on, cb_off, cb_all, NULL, NULL };

static void ok(int cond, const char *name)
{
    if (cond) passed++; else failed++;
    printf("%s [%s] %s\n", cond ? "ok  " : "FAIL", TAG, name);
}
static void clear(void) { nlog = 0; n_alloff[0] = n_alloff[1] = n_alloff[2] = 0; }
static void step(int ms)
{
    uint64_t target = T + (uint64_t)ms * 1000u;
    while (next_tick <= target) {
        T = next_tick;
        cr_tick(&C, now_ms());
        next_tick += TICK_US;
    }
    T = target;
}
static int sounding(void)
{
    int s, n, k = 0;
    for (s = 0; s < 3; s++) for (n = 0; n < 128; n++) k += SND[s][n];
    return k;
}
static double tms(uint64_t t) { return (double)t / 1000.0; }
static double TOL(void) { return (double)TICK_US / 1000.0 + 1.5; }
/* each on lands within one tick after its exact due time first + round(k * num / den) (engine ms) */
static int grid_ok(const int *idx, int n, uint32_t num, uint32_t den)
{
    int k;
    for (k = 0; k < n; k++) {
        uint32_t due = LOG[idx[0]].ms + (2u * (uint32_t)k * num + den) / (2u * den), d = LOG[idx[k]].ms - due;
        if (d > TICK_US / 1000u + 1u) return 0;
    }
    return 1;
}

/* events: "+48/95" on with velocity, "+48" on any velocity, "-48" off; suffix b = bass, r = raw stream */
static void expect(const char *spec, const char *name)
{
    const char *q = spec;
    int i = 0, same = 1, k;
    logev_t want[64];
    int nw = 0;
    while (*q && nw < 64) {
        logev_t e;
        char *p;
        while (*q == ' ') q++;
        if (!*q) break;
        memset(&e, 0, sizeof e);
        e.k = *q++;
        e.note = (uint8_t)strtol(q, &p, 10);
        if (*p == '/') e.vel = (uint8_t)strtol(p + 1, &p, 10);
        e.s = (uint8_t)(*p == 'b' ? 1 : *p == 'r' ? 2 : 0);
        if (*p == 'b' || *p == 'r') p++;
        q = p;
        want[nw++] = e;
    }
    for (k = 0; k < nlog && same; k++) {
        if (i >= nw) { same = 0; break; }
        if (LOG[k].k != want[i].k || LOG[k].note != want[i].note || LOG[k].s != want[i].s
            || (want[i].vel && LOG[k].vel != want[i].vel)) same = 0;
        i++;
    }
    if (i != nw) same = 0;
    if (!same) {
        printf("  expected: %s\n  got:     ", spec);
        for (k = 0; k < nlog; k++)
            printf(" %c%d/%d%s", LOG[k].k, LOG[k].note, LOG[k].vel, LOG[k].s == 1 ? "b" : LOG[k].s == 2 ? "r" : "");
        printf("\n");
    }
    ok(same, name);
    clear();
}
/* main-stream note-ons since the last clear */
static int ons(int *idx, int max, int s)
{
    int k, n = 0;
    for (k = 0; k < nlog; k++)
        if (LOG[k].k == '+' && LOG[k].s == s && n < max) idx[n++] = k;
    return n;
}
static int count(char kind, int s)
{
    int k, n = 0;
    for (k = 0; k < nlog; k++) if (LOG[k].k == kind && (s < 0 || LOG[k].s == s)) n++;
    return n;
}
static int on_notes_sorted(char *out, int s)   /* "48 52 55" of the note-ons since clear */
{
    int idx[64], n = ons(idx, 64, s), i, j, v[64];
    for (i = 0; i < n; i++) v[i] = LOG[idx[i]].note;
    for (i = 1; i < n; i++) for (j = i; j > 0 && v[j - 1] > v[j]; j--) { int t = v[j]; v[j] = v[j - 1]; v[j - 1] = t; }
    out[0] = 0;
    for (i = 0; i < n; i++) snprintf(out + strlen(out), 8, i ? " %d" : "%d", v[i]);
    return n;
}

static void K(int note, int row) { cr_key_ex(&C, (uint8_t)note, (uint8_t)(row - 1), VEL[row - 1], 1); }
static void R(int note, int row) { cr_key_ex(&C, (uint8_t)note, (uint8_t)(row - 1), VEL[row - 1], 0); }
static void MD(cr_mod_t m) { cr_mod(&C, m, 1); }
static void MU(cr_mod_t m) { cr_mod(&C, m, 0); }
static void TAPM(cr_mod_t m) { MD(m); step(50); MU(m); step(20); }

static void fresh(const char *section)
{
    char name[96];
    snprintf(name, sizeof name, "%s: previous section left no stuck note, double note-on or stray note-off", section);
    ok(sounding() == 0 && dup_on == 0 && stray_off == 0, name);
    memset(SND, 0, sizeof SND);
    dup_on = stray_off = 0;
    cr_init(&C, &OUT);
    cr_tick(&C, now_ms());
    clear();
}

/* ============================================================ suite === */

static void s_keyboard(void)
{
    int k;
    fresh("keyboard");
    K(48, 3); expect("+48/95", "single note: C3 at velocity row 3");
    R(48, 3); expect("-48", "single note off");
    K(48, 1); R(48, 1); K(48, 8); R(48, 8);
    expect("+48/127 -48 +48/16 -48", "velocity extremes: y1=127, y8=16");
    K(59, 5); expect("+59/64", "B3 at mid velocity");
    R(59, 5); clear();
    /* 5.3 same-column supersede */
    K(48, 3); K(48, 1);
    expect("+48/95 -48 +48/127", "supersede: retrigger with new velocity");
    R(48, 3); expect("", "supersede: old key release ignored");
    R(48, 1); expect("-48", "supersede: new key release stops note");
    K(48, 3); K(48, 1); R(48, 1);
    expect("+48/95 -48 +48/127 -48", "supersede: releasing the newer press ends the root (Lua order)");
    R(48, 3); expect("", "supersede: the older press's later release is a no-op");
    /* FM-1 keys have one press id: a repeated note-on retriggers at the new velocity */
    cr_key(&C, 62, 100, 1); cr_key(&C, 62, 40, 1);
    expect("+62/100 -62 +62/40", "same-id retrigger of a held root at a new velocity");
    cr_key(&C, 62, 40, 0); expect("-62", "same-id release ends the retriggered root");
    ok(cr_voices(&C) == 0, "no voices left after the keyboard cases");
    for (k = 0; k < 3; k++) { cr_key(&C, 127, 90, 1); cr_key(&C, 127, 90, 0); }
    ok(count('+', 0) == 3 && count('-', 0) == 3, "MIDI 127 root plays and releases cleanly");
    clear();
    cr_key(&C, 200, 90, 1);
    ok(nlog == 0 && cr_voices(&C) == 0, "an out-of-range root key is ignored");
}

static void s_sticky(void)
{
    int offs, onsn;
    fresh("sticky");
    cr_set_sticky(&C, 1);
    ok(cr_latching(&C), "Sticky Keys on: latching");
    MD(CR_MOD_MAJ); K(48, 2); K(50, 1);
    expect("+48/111 +52/111 +55/111 +50/127 +54/127 +57/127", "sticky accumulates two held chords");
    R(48, 2); expect("", "sticky preserves released chord while another key is down");
    R(50, 1); MU(CR_MOD_MAJ);
    expect("", "completed Sticky gesture remains latched after every key release");
    ok(cr_voices(&C) == 2, "completed Sticky group retains both roots");
    K(52, 1);
    offs = count('-', 0); onsn = count('+', 0);
    ok(offs == 6 && onsn == 1 && cr_voices(&C) == 1, "first key of next gesture replaces the previous Sticky group");
    clear(); K(52, 8);
    ok(count('+', 0) == 1 && LOG[nlog - 1].vel == 16, "Sticky same-column press retriggers velocity");
    clear(); R(52, 8); expect("", "same-column voice holds while its earlier key remains down");
    R(52, 1); expect("", "same-column Sticky session remains latched after final release");
    cr_set_sticky(&C, 0);
    expect("-52", "disabling Sticky releases held manual voices immediately");
    ok(!C.sticky && cr_voices(&C) == 0, "Sticky Keys disabled cleanly");
    /* a held key survives the latch flush */
    cr_set_sticky(&C, 1);
    K(48, 1); K(50, 1); R(50, 1); clear();
    cr_set_sticky(&C, 0);
    expect("-50", "latch flush releases only the voices no finger holds");
    R(48, 1); expect("-48", "the still-held root ends on its own release");
}

static void s_chords(void)
{
    fresh("chords");
    MD(CR_MOD_MAJ); K(48, 2);
    expect("+48/111 +52/111 +55/111", "C major chord");
    MU(CR_MOD_MAJ); expect("", "modifier release does not change sounding chord");
    R(48, 2); expect("-48 -52 -55", "chord off on root release");
    MD(CR_MOD_MAJ); MD(CR_MOD_MAJ7); K(50, 4);
    expect("+50/79 +54/79 +57/79 +61/79", "Dmaj7");
    R(50, 4); clear(); MU(CR_MOD_MAJ7); MU(CR_MOD_MAJ);
    MD(CR_MOD_MAJ); MD(CR_MOD_6); MD(CR_MOD_9); K(48, 1);
    expect("+48/127 +52/127 +55/127 +57/127 +62/127", "C69 = C E G A D (Maj + 6 + 9)");
    R(48, 1); MU(CR_MOD_9); MU(CR_MOD_6); MU(CR_MOD_MAJ); clear();
    MD(CR_MOD_MAJ); MD(CR_MOD_6); MD(CR_MOD_MAJ7); MD(CR_MOD_9); K(48, 1);
    expect("+48 +52 +55 +57 +59 +62", "CJazz = C E G A B D (Maj + 6 + M7 + 9)");
    R(48, 1); MU(CR_MOD_9); MU(CR_MOD_MAJ7); MU(CR_MOD_6); MU(CR_MOD_MAJ); clear();
    MD(CR_MOD_MAJ); MD(CR_MOD_6); MD(CR_MOD_M7); MD(CR_MOD_MAJ7); MD(CR_MOD_9); K(48, 1);
    expect("+48 +52 +55 +57 +58 +59 +62", "CWTF = C E G A Bb B D (Maj + 6 + m7 + M7 + 9)");
    R(48, 1); MU(CR_MOD_9); MU(CR_MOD_MAJ7); MU(CR_MOD_M7); MU(CR_MOD_6); MU(CR_MOD_MAJ); clear();
    MD(CR_MOD_DIM); MD(CR_MOD_M7); K(48, 1);
    expect("+48 +51 +54 +58", "DIM + m7 = C Eb Gb Bb");
    R(48, 1); MU(CR_MOD_M7); MU(CR_MOD_DIM); clear();
    MD(CR_MOD_SUS); MD(CR_MOD_M7); K(55, 1);
    expect("+55 +60 +62 +65", "SUS + m7 + G = G7sus4");
    R(55, 1); MU(CR_MOD_M7); MU(CR_MOD_SUS); clear();
    /* Simple: modifiers captured at note-on */
    MD(CR_MOD_MAJ); K(48, 2); clear();
    MD(CR_MOD_MIN); expect("", "Simple: modifier change while held does not alter chord");
    MU(CR_MOD_MIN); MU(CR_MOD_MAJ); R(48, 2);
    expect("-48 -52 -55", "Simple: original chord released intact");
    MD(CR_MOD_MIN); MD(CR_MOD_MAJ); K(48, 1);
    expect("+48/127 +52/127 +55/127", "most recently pressed quality wins (MAJ over MIN)");
    R(48, 1); MU(CR_MOD_MAJ); clear();
    K(48, 1); expect("+48 +51 +55", "releasing the newer quality falls back to the still-held one (MIN)");
    R(48, 1); MU(CR_MOD_MIN); clear();
    MD(CR_MOD_M7); K(48, 1);
    expect("+48/127", "extension without quality is ignored");
    R(48, 1); MU(CR_MOD_M7); clear();
    /* shared-note refcount: C maj (48 52 55) + G maj (55 59 62) share 55 */
    MD(CR_MOD_MAJ); K(48, 1); K(55, 1);
    expect("+48/127 +52/127 +55/127 -55 +55/127 +59/127 +62/127", "shared note retriggers on second chord");
    ok(cr_sounding(&C, CR_STREAM_MAIN, 55) == 2, "shared note refcount = 2 owners");
    R(48, 1); expect("-48 -52", "shared note survives first owner release");
    R(55, 1); expect("-55 -59 -62", "shared note off with last owner");
    MU(CR_MOD_MAJ);
    /* top of the MIDI range: notes past 127 are dropped, never wrapped */
    MD(CR_MOD_MAJ); MD(CR_MOD_9); K(120, 1);
    expect("+120 +124 +127", "chord near MIDI 127 drops out-of-range notes (G#8 maj9 -> 3 notes)");
    R(120, 1); MU(CR_MOD_9); MU(CR_MOD_MAJ); clear();
}

static void s_panic(void)
{
    int k;
    fresh("panic");
    cr_set_bass(&C, 0);
    MD(CR_MOD_MAJ); K(48, 1); clear();
    cr_panic(&C);
    ok(count('-', -1) == 3, "panic: note-offs for all tracked notes");
    ok(n_alloff[0] == 1 && n_alloff[1] == 1 && n_alloff[2] == 1, "panic: CC123 (all_off) on all three streams, raw disabled too");
    ok(sounding() == 0 && cr_voices(&C) == 0 && cr_pending(&C) == 0, "panic: nothing sounding, no voices, queue empty");
    clear(); R(48, 1);
    expect("", "post-panic release of cleared root is ignored");
    MU(CR_MOD_MAJ);
    cr_set_stream(&C, CR_STREAM_RAW, 1);
    MD(CR_MOD_MAJ); K(48, 1); clear();
    cr_panic(&C);
    ok(count('-', 2) == 3 && n_alloff[2] == 1, "panic with raw stream on: raw offs + CC123 on raw too");
    R(48, 1); MU(CR_MOD_MAJ); cr_set_stream(&C, CR_STREAM_RAW, 0);
    /* panic stops the schedulers */
    cr_set_perform(&C, 1); cr_set_perform_mode(&C, CR_PM_ARP); cr_set_param(&C, CR_PM_ARP, CR_P_HOLD, 0);
    MD(CR_MOD_MAJ); K(48, 1); step(300);
    cr_panic(&C); clear(); step(1000);
    ok(count('+', -1) == 0 && sounding() == 0, "panic mid-arp: no later notes");
    R(48, 1); MU(CR_MOD_MAJ);
    /* mash test: interleaved chords, no stuck notes */
    cr_set_perform(&C, 0); clear();
    {
        static const int cols[8] = { 48, 50, 52, 55, 57, 59, 49, 51 };
        MD(CR_MOD_MAJ);
        for (k = 0; k < 8; k++) {
            int i = k + 1;
            K(cols[k], (i % 8) + 1);
            if (i % 2 == 0) MD(CR_MOD_M7); else MU(CR_MOD_M7);
            if (i > 2) R(cols[k - 2], ((i - 2) % 8) + 1);
            step(7);
        }
        MU(CR_MOD_M7); MU(CR_MOD_MAJ);
        for (k = 0; k < 8; k++) R(cols[k], ((k + 1) % 8) + 1);
        step(50);
        ok(sounding() == 0 && cr_voices(&C) == 0, "mash: no stuck notes");
    }
}

static void s_voicing(void)
{
    int idx[64], n, k;
    cr_chord_info_t ci;
    fresh("voicing");
    MD(CR_MOD_MAJ); K(48, 1); clear();
    cr_voicing_step(&C, 1);
    expect("-48 -52 -55 +52/127 +55/127 +60/127", "voice up: live re-voice of sounding chord");
    cr_chord_info(&C, &ci);
    ok(ci.nnotes == 3 && ci.notes[0] == 52 && ci.notes[2] == 60, "chord info: voiced notes follow the voicing");
    R(48, 1); expect("-52 -55 -60", "voiced chord releases cleanly");
    K(48, 1); expect("+52/127 +55/127 +60/127", "voicing +1 applied to new chord");
    R(48, 1); MU(CR_MOD_MAJ); clear();
    cr_voicing_step(&C, -1);
    ok(C.voicing == 0, "voicing back to 0");
    MD(CR_MOD_MAJ); MD(CR_MOD_MAJ7); K(48, 1); clear();
    for (k = 0; k < 4; k++) { cr_voicing_step(&C, 1); step(70); }
    n = ons(idx, 64, 0);
    ok(n >= 4 && LOG[idx[n - 4]].note == 60 && LOG[idx[n - 3]].note == 64 && LOG[idx[n - 2]].note == 67 && LOG[idx[n - 1]].note == 71,
       "4 voice-up steps shift Cmaj7 one octave (60,64,67,71)");
    R(48, 1); MU(CR_MOD_MAJ7); MU(CR_MOD_MAJ); clear();
    for (k = 0; k < 4; k++) cr_voicing_step(&C, -1);
    K(48, 1); clear();
    cr_voicing_step(&C, -1);
    expect("-48 +36/127", "voice down: highest note drops an octave");
    R(48, 1); clear();
    cr_voicing_step(&C, 1);
    for (k = 0; k < 40; k++) cr_voicing_step(&C, 1);
    ok(C.voicing == 12, "voicing clamps at +12");
    for (k = 0; k < 40; k++) cr_voicing_step(&C, -1);
    ok(C.voicing == -12, "voicing clamps at -12");
    /* a step that would leave MIDI 0..127 stops the rotation (the position still moves) */
    {
        uint8_t base[3] = { 110, 114, 117 }, out[CR_CHORD_MAX];
        n = cr_voice_apply(base, 3, 3, out);
        ok(n == 3 && out[0] == 117 && out[1] == 122 && out[2] == 126, "voice_apply: rotation stops below 128 (117 122 126)");
        n = cr_voice_apply(base, 3, -1, out);
        ok(out[0] == 105 && out[1] == 110 && out[2] == 114, "voice_apply -1: highest drops an octave");
        base[0] = 2; base[1] = 6; base[2] = 9;
        n = cr_voice_apply(base, 3, -5, out);
        ok(out[0] == 2 && out[1] == 6 && out[2] == 9, "voice_apply: rotation stops above -1");
        base[0] = 48; base[1] = 52; base[2] = 55;
        n = cr_voice_apply(base, 3, 3, out);
        ok(out[0] == 60 && out[1] == 64 && out[2] == 67, "voice_apply +3 on a triad = one octave up");
    }
    for (k = 0; k < 12; k++) cr_voicing_step(&C, 1);
    ok(C.voicing == 0, "voicing reset");
    /* voicing mash: two chords + many steps, then everything off */
    MD(CR_MOD_MAJ); K(48, 1); K(55, 2);
    for (k = 0; k < 9; k++) { cr_voicing_step(&C, 1); step(100); }
    R(48, 1); R(55, 2); MU(CR_MOD_MAJ); step(50);
    ok(sounding() == 0, "voicing mash: no stuck notes");
    for (k = 0; k < 9; k++) cr_voicing_step(&C, -1);
    ok(C.voicing == 0, "voicing back to 0 after the mash");
}

static void s_bass(void)
{
    int k;
    fresh("bass");
    cr_set_bass(&C, 1);
    MD(CR_MOD_MAJ); K(48, 1);
    expect("+48/127 +52/127 +55/127 +36/127b", "bass: root C2 on the bass stream");
    R(48, 1);
    expect("-48 -52 -55 -36b", "bass released with chord");
    K(48, 1); clear();
    cr_bass_voicing_step(&C, 1);
    expect("-36b +48/127b", "bass voicing up: live re-pitch on the bass stream");
    ok(C.bass_voicing == 1, "bass voicing state +1");
    R(48, 1); clear();
    for (k = 0; k < 10; k++) cr_bass_voicing_step(&C, 1);
    ok(C.bass_voicing == 4, "bass voicing clamps at +4");
    for (k = 0; k < 10; k++) cr_bass_voicing_step(&C, -1);
    ok(C.bass_voicing == -2, "bass voicing clamps at -2");
    K(48, 1); ok(SND[1][12], "bass voicing -2 plays C0 (12)");
    R(48, 1); clear();
    cr_bass_voicing_step(&C, 2);
    K(48, 1); clear();
    K(48, 3);
    expect("-36b -48 -52 -55 +48/95 +52/95 +55/95 +36/95b", "supersede: chord and bass retrigger at new velocity");
    R(48, 3); clear();
    K(48, 1); cr_panic(&C);
    ok(sounding() == 0, "panic clears bass notes as well");
    R(48, 1); MU(CR_MOD_MAJ);
    /* the bass toggle applies to the next gesture; a sounding bass finishes with its chord */
    MD(CR_MOD_MAJ); K(48, 1); clear();
    cr_set_bass(&C, 0);
    expect("", "bass off while held: no change");
    R(48, 1); expect("-48 -52 -55 -36b", "the held chord's bass ends with it");
    K(48, 1); expect("+48 +52 +55", "bass off: the next chord has no bass");
    R(48, 1); MU(CR_MOD_MAJ); clear();
    cr_set_bass(&C, 1);
    cr_set_stream(&C, CR_STREAM_BASS, 0);
    MD(CR_MOD_MAJ); K(48, 1);
    expect("+48 +52 +55", "bass stream route off: bass on but nothing on the bass stream");
    R(48, 1); MU(CR_MOD_MAJ); clear();
    cr_set_stream(&C, CR_STREAM_BASS, 1);
}

static void s_keymode(void)
{
    cr_chord_info_t ci;
    fresh("key mode");
    cr_set_key(&C, 1, 0, CR_SCALE_MAJOR);
    K(47, 1); expect("+47/127 +50/127 +53/127", "Key C major: B2 generates B diminished");
    R(47, 1); clear();
    K(50, 1); expect("+50/127 +53/127 +57/127", "Key C major: D -> Dm");
    R(50, 1); clear();
    K(59, 1); expect("+59/127 +62/127 +65/127", "Key C major: B -> Bdim");
    R(59, 1); clear();
    K(49, 1); expect("+48/127 +53/127 +55/127", "out-of-scale C# resolves to Csus (measured, manual 9.3)");
    cr_chord_info(&C, &ci);
    ok(ci.root_pc == 0 && ci.quality == CR_Q_SUS && !strcmp(ci.root, "C") && !strcmp(ci.qual, "sus"),
       "chord info shows the resolved Csus, not the pressed C#");
    R(49, 1); clear();
    MD(CR_MOD_M7); K(55, 1);
    expect("+55/127 +59/127 +62/127 +65/127", "Key Mode: G + m7 -> G7");
    cr_chord_info(&C, &ci);
    ok(!strcmp(ci.root, "G") && !strcmp(ci.qual, "") && !strcmp(ci.sup, "7"), "Key Mode G7 is named G 7 (dominant anomaly)");
    R(55, 1); MU(CR_MOD_M7); clear();
    MD(CR_MOD_MIN); K(48, 1);
    expect("+48/127 +51/127 +55/127", "held quality overrides diatonic quality");
    R(48, 1); MU(CR_MOD_MIN); clear();
    cr_set_bass(&C, 1);
    K(49, 1);
    ok(SND[1][36] && !SND[1][37], "bass uses the quantized root (C2, not C#2)");
    R(49, 1); cr_set_bass(&C, 0); clear();
    cr_set_key(&C, 1, 9, CR_SCALE_MINOR);
    K(48, 1); expect("+48/127 +52/127 +55/127", "A minor: C -> C major");
    R(48, 1); clear();
    K(59, 1); expect("+59/127 +62/127 +65/127", "A minor: B -> Bdim");
    R(59, 1); clear();
    K(57, 1); expect("+57 +60 +64", "A minor: A -> Am");
    R(57, 1); clear();
    cr_set_key(&C, 0, C.tonic, (cr_scale_t)C.scale);
    K(49, 1); expect("+49/127", "Key Mode off: C# is chromatic single note");
    R(49, 1); clear();
    cr_set_key(&C, 1, C.tonic, (cr_scale_t)C.scale);
    ok(C.key_on && C.tonic == 9 && C.scale == CR_SCALE_MINOR, "Key Mode re-enabled with remembered A minor");
    ok(cr_scale_mask(&C) == 0xAB5, "scale mask A minor = the C major pitch classes");
    cr_set_key(&C, 1, 0, CR_SCALE_MINOR);
    ok(cr_scale_mask(&C) == ((1 << 0) | (1 << 2) | (1 << 3) | (1 << 5) | (1 << 7) | (1 << 8) | (1 << 10)), "scale mask C minor");
    K(48, 1); expect("+48/127 +51/127 +55/127", "Key C minor: C -> Cm");
    cr_set_key(&C, 1, 0, CR_SCALE_MAJOR);
    expect("", "Scale change leaves the sounding chord untouched");
    R(48, 1); expect("-48 -51 -55", "held voice releases its captured notes");
    K(48, 1); expect("+48/127 +52/127 +55/127", "next gesture uses the new scale (C major)");
    R(48, 1); clear();
    K(54, 1); expect("+55/127 +60/127 +62/127", "C major: F# -> Gsus");
    R(54, 1); clear();
    K(58, 1); expect("+57/127 +62/127 +64/127", "C major: A# -> Asus");
    R(58, 1); clear();
    K(51, 1); expect("+50 +55 +57", "C major: D# -> Dsus (fallback)");
    R(51, 1); clear();
    K(56, 1); expect("+55 +60 +62", "C major: G# -> Gsus (fallback)");
    R(56, 1); clear();
    MD(CR_MOD_MIN); K(49, 1);
    expect("+48/127 +51/127 +55/127", "held MIN overrides the sus rule: C# -> Cm");
    R(49, 1); MU(CR_MOD_MIN); clear();
    cr_set_key(&C, 1, 9, CR_SCALE_MINOR);
    K(58, 1); expect("+57/127 +62/127 +64/127", "A minor: A# -> Asus");
    R(58, 1); clear();
    K(54, 1); expect("+55/127 +60/127 +62/127", "A minor: F# -> Gsus");
    R(54, 1); clear();
    K(56, 1); expect("+55/127 +60/127 +62/127", "A minor: G# -> Gsus");
    R(56, 1); clear();
    K(49, 1); expect("+48 +53 +55", "A minor: C# -> Csus (fallback)");
    R(49, 1); clear();
    /* transposable: the same relative rule in D major (D# -> Dsus) */
    cr_set_key(&C, 1, 2, CR_SCALE_MAJOR);
    K(51, 1); expect("+50 +55 +57", "D major: D# -> Dsus (rule transposes with the tonic)");
    R(51, 1); clear();
    K(54, 1); expect("+54 +57 +61", "D major: F# -> F#m");
    R(54, 1); clear();
    ok(sounding() == 0, "key tests leave nothing sounding");
}

static void s_pads(void)
{
    int16_t root; uint8_t q, x, vel;
    fresh("pads");
    cr_pad_arm(&C, 0);
    ok(cr_pad_armed(&C) == 0, "chord pads: arm slot 1");
    MD(CR_MOD_MAJ); MD(CR_MOD_MAJ7); K(48, 2); clear();
    R(48, 2);
    ok(cr_pad_armed(&C) == 0, "chord pads: still armed while a modifier is held");
    MU(CR_MOD_MAJ7); MU(CR_MOD_MAJ);
    ok(cr_pad_armed(&C) == -1, "chord pads: lifting all fingers ends recording");
    ok(cr_pad_get(&C, 0, &root, &q, &x, &vel) && root == 48 && q == CR_Q_MAJ && x == CR_EXT_MAJ7 && vel == 111,
       "chord pads: stored the resolved chord (Cmaj7 @111)");
    clear();
    cr_pad_down(&C, 0);
    expect("+48/111 +52/111 +55/111 +59/111", "chord pads: pressing the pad plays the stored chord at its recorded velocity");
    cr_pad_up(&C, 0);
    expect("-48 -52 -55 -59", "chord pads: releasing the pad stops the chord");
    cr_pad_arm(&C, 7);
    MD(CR_MOD_MIN); K(57, 1); R(57, 1); MU(CR_MOD_MIN);
    ok(cr_pad_get(&C, 7, &root, &q, NULL, NULL) && root == 57 && q == CR_Q_MIN, "chord pads: pad 8 stored Am");
    clear();
    cr_pad_down(&C, 7);
    ok(sounding() > 0, "chord pads: pad 8 sounding");
    cr_pad_stop_all(&C);
    ok(sounding() == 0, "chord pads: stopping all pads (tab change) stops a held pad");
    cr_pad_up(&C, 7);
    ok(sounding() == 0, "chord pads: no stuck note after a stray release");
    /* Key Mode result is stored, replay ignores Key Mode */
    cr_set_key(&C, 1, 0, CR_SCALE_MAJOR);
    cr_pad_arm(&C, 2); K(49, 1); R(49, 1);
    cr_set_key(&C, 0, 0, CR_SCALE_MAJOR);
    clear(); cr_pad_down(&C, 2);
    expect("+48 +53 +55", "chord pads: a Key Mode chord (C# -> Csus) replays resolved with Key Mode off");
    cr_pad_up(&C, 2); clear();
    /* replay follows the current voicing and bass */
    cr_voicing_step(&C, 1); cr_set_bass(&C, 1);
    cr_pad_down(&C, 0);
    expect("+52/111 +55/111 +59/111 +60/111 +36/111b", "chord pads: replay through the current voicing and bass");
    cr_voicing_step(&C, -1);
    expect("-52 -55 -59 -60 +48/111 +52/111 +55/111 +59/111", "chord pads: a sounding pad re-voices live");
    cr_pad_up(&C, 0); clear(); cr_set_bass(&C, 0);
    /* through a performance mode */
    cr_set_perform(&C, 1);
    cr_pad_down(&C, 0); step(200);
    ok(count('+', 0) == 4, "chord pads: a pad strums when Perform is on");
    cr_pad_up(&C, 0); cr_set_perform(&C, 0); clear();
    cr_pad_down(&C, 0);
    ok(sounding() > 0, "chord pads: replay after returning");
    cr_panic(&C);
    ok(sounding() == 0 && cr_voices(&C) == 0, "chord pads: panic clears slot playback");
    cr_pad_set(&C, 4, 1, 62, CR_Q_MIN, CR_EXT_M7, 90);
    clear(); cr_pad_down(&C, 4);
    expect("+62/90 +65/90 +69/90 +72/90", "chord pads: a restored pad (persistence) plays Dm7");
    cr_pad_up(&C, 4); clear();
    cr_pad_down(&C, 5);
    ok(nlog == 0, "chord pads: an empty pad plays nothing");
}

static void secret(const cr_mod_t *keys, int n, const char *want, const char *name)
{
    char got[128];
    int i;
    for (i = 0; i < n; i++) MD(keys[i]);
    K(48, 1);
    on_notes_sorted(got, 0);
    if (strcmp(got, want)) printf("  got %s want %s\n", got, want);
    ok(!strcmp(got, want), name);
    R(48, 1);
    for (i = n - 1; i >= 0; i--) MU(keys[i]);
    clear();
}

static void s_secret(void)
{
    static const cr_mod_t MAJSUS[2] = { CR_MOD_MAJ, CR_MOD_SUS }, DIMSUS[2] = { CR_MOD_DIM, CR_MOD_SUS },
        MINSUS[2] = { CR_MOD_MIN, CR_MOD_SUS }, MAJMIN[2] = { CR_MOD_MAJ, CR_MOD_MIN },
        SUSMAJ[2] = { CR_MOD_SUS, CR_MOD_MAJ }, DIMMAJ[2] = { CR_MOD_DIM, CR_MOD_MAJ },
        MAJSUS7[3] = { CR_MOD_MAJ, CR_MOD_SUS, CR_MOD_MAJ7 }, MINSUS6[3] = { CR_MOD_MIN, CR_MOD_SUS, CR_MOD_6 },
        DIMMINSUS[3] = { CR_MOD_DIM, CR_MOD_MIN, CR_MOD_SUS };
    cr_chord_info_t ci;
    fresh("secret");
    secret(MAJSUS, 2, "48 53 55", "Secret Chords off: MAJ+SUS keeps the last-pressed quality");
    cr_set_secret(&C, CR_SECRET_SIMPLE);
    secret(DIMSUS, 2, "48 55 60", "DIM+SUS -> power chord [0,7,12]");
    secret(MAJSUS, 2, "48 52 56", "MAJ+SUS -> augmented [0,4,8]");
    secret(MINSUS, 2, "48 51 53", "MIN+SUS -> [0,3,5]");
    secret(SUSMAJ, 2, "48 52 56", "SUS then MAJ -> augmented (the pair is unordered)");
    secret(MAJMIN, 2, "48 51 55", "an unmapped Chord Type pair falls back to last-pressed (Cm)");
    secret(DIMMAJ, 2, "48 52 55", "DIM+MAJ unmapped -> last-pressed (C)");
    secret(MAJSUS7, 3, "48 52 56 59", "modifiers stack normally on a secret chord (aug + M7)");
    secret(MINSUS6, 3, "48 51 53 57", "modifiers stack normally on a secret chord ([0,3,5] + 6)");
    secret(DIMMINSUS, 3, "48 51 53", "three Chord Types: the two most recent decide (MIN+SUS)");
    MD(CR_MOD_DIM); MD(CR_MOD_SUS); K(50, 1);
    cr_chord_info(&C, &ci);
    ok(!strcmp(ci.root, "D") && !strcmp(ci.qual, "") && !strcmp(ci.sup, "5") && ci.secret, "power chord named D + superscript 5 (secret)");
    R(50, 1); MU(CR_MOD_SUS); MU(CR_MOD_DIM); clear();
    MD(CR_MOD_MAJ); MD(CR_MOD_SUS); K(48, 1);
    cr_chord_info(&C, &ci);
    ok(!strcmp(ci.qual, "+") && ci.quality == CR_Q_AUG, "augmented named C+");
    R(48, 1); MU(CR_MOD_SUS); MU(CR_MOD_MAJ); clear();
    MD(CR_MOD_MIN); MD(CR_MOD_SUS); K(48, 1);
    cr_chord_info(&C, &ci);
    ok(!strcmp(ci.root, "C") && !strcmp(ci.qual, "m") && !strcmp(ci.sup, "add4") && ci.quality == CR_Q_MIN4 && ci.secret,
       "MIN+SUS secret named Cm add4 (manual's secret-chord table)");
    R(48, 1); MU(CR_MOD_SUS); MU(CR_MOD_MIN); clear();
    cr_set_playstyle(&C, CR_PS_ADVANCED);
    secret(MAJSUS, 2, "48 53 55", "scope Simple: no secret chord in Advanced");
    cr_set_secret(&C, CR_SECRET_ALL);
    K(48, 1); MD(CR_MOD_MAJ); clear();
    MD(CR_MOD_SUS);
    expect("-48 -52 -55 +48/127 +52/127 +56/127",
           "scope All + Advanced: a second Chord Type retriggers as the secret chord (augmented)");
    MU(CR_MOD_SUS); MU(CR_MOD_MAJ); R(48, 1);
    expect("-48 -52 -56", "augmented voice releases cleanly");
    cr_set_playstyle(&C, CR_PS_SIMPLE);
    secret(MINSUS, 2, "48 51 53", "scope All in Simple: secret chord");
}

static void s_advanced(void)
{
    uint8_t a;
    fresh("advanced");
    cr_set_playstyle(&C, CR_PS_ADVANCED);
    ok(C.playstyle == CR_PS_ADVANCED, "playstyle advanced");
    K(48, 1); expect("+48/127", "advanced: bare root is a single note");
    MD(CR_MOD_MAJ);
    expect("-48 +48/127 +52/127 +55/127", "advanced: a Chord Type press retriggers the full chord");
    MD(CR_MOD_MAJ7); expect("+59/127", "advanced: an extension adds its note only (Add Note)");
    MU(CR_MOD_MAJ); expect("", "advanced: modifier release does not shrink chord");
    MD(CR_MOD_MIN);
    expect("-48 -52 -55 -59 +48/127 +51/127 +55/127 +59/127",
           "advanced: a Chord Type change retriggers the whole chord (CminM7)");
    MU(CR_MOD_MIN); MU(CR_MOD_MAJ7); R(48, 1);
    expect("-48 -51 -55 -59", "advanced: full chord off on root release");
    MD(CR_MOD_MAJ); K(48, 1); clear();
    TAPM(CR_MOD_6); expect("+57/127", "advanced: extension press adds the 6th");
    TAPM(CR_MOD_6); expect("-57", "advanced: pressing the same extension again removes it");
    TAPM(CR_MOD_M7); expect("+58/127", "advanced: a different extension still adds");
    TAPM(CR_MOD_M7); expect("-58", "advanced: and toggles back off");
    R(48, 1); MU(CR_MOD_MAJ); clear();
    /* modifier LEDs: held or latched in the held chord */
    MD(CR_MOD_MAJ); K(48, 1);
    a = cr_mods_active(&C);
    ok(cr_mods_held(&C) == (1 << CR_MOD_MAJ) && (a & (1 << CR_MOD_MAJ)) && !(a & (1 << CR_MOD_DIM)) && !(a & (1 << CR_MOD_6)),
       "LED: held MAJ lit, others off");
    MU(CR_MOD_MAJ);
    ok(cr_mods_held(&C) == 0 && (cr_mods_active(&C) & (1 << CR_MOD_MAJ)), "LED: MAJ latched after release (active in the held chord)");
    TAPM(CR_MOD_6);
    ok(cr_mods_active(&C) & (1 << CR_MOD_6), "LED: toggled-on 6 latches even though released");
    TAPM(CR_MOD_MAJ7);
    ok((cr_mods_active(&C) & (1 << CR_MOD_6)) && (cr_mods_active(&C) & (1 << CR_MOD_MAJ7)), "LED: multiple latched extensions");
    TAPM(CR_MOD_6);
    ok(!(cr_mods_active(&C) & (1 << CR_MOD_6)) && (cr_mods_active(&C) & (1 << CR_MOD_MAJ7)), "LED: toggled-off 6 goes dark, M7 stays latched");
    R(48, 1);
    ok(cr_mods_active(&C) == 0, "LED: all mod keys dark once the chord is released");
    clear();
    MD(CR_MOD_MAJ); K(48, 1); MU(CR_MOD_MAJ); clear();
    TAPM(CR_MOD_MAJ);
    expect("-48 -52 -55 +48/127 +52/127 +55/127", "advanced: re-pressing a Chord Type retriggers the chord");
    MD(CR_MOD_MAJ); clear();
    MD(CR_MOD_MAJ);
    expect("", "a duplicate down of a held modifier is ignored");
    R(48, 1); MU(CR_MOD_MAJ); clear();
    /* sequential secret chord in Advanced (scope all) */
    cr_set_secret(&C, CR_SECRET_ALL);
    K(48, 1); MD(CR_MOD_DIM); MD(CR_MOD_SUS); step(5);
    ok(sounding() == 3 && SND[0][48] && SND[0][55] && SND[0][60], "advanced secret: DIM+SUS -> clean power chord 48 55 60");
    MU(CR_MOD_SUS); MU(CR_MOD_DIM); R(48, 1);
    cr_set_secret(&C, CR_SECRET_OFF); clear();
    /* Extension Addition = Play Chord */
    cr_set_ext_addition(&C, CR_EXTADD_PLAY_CHORD);
    MD(CR_MOD_MAJ); K(48, 1); clear();
    TAPM(CR_MOD_6);
    ok(count('-', 0) == 3 && count('+', 0) == 4, "play_chord: adding the 6th re-attacks the full chord (3 off, 4 on)");
    clear();
    TAPM(CR_MOD_6);
    ok(count('-', 0) == 4 && count('+', 0) == 3, "play_chord: removing the 6th re-attacks the triad (4 off, 3 on)");
    R(48, 1); MU(CR_MOD_MAJ); clear();
    cr_set_ext_addition(&C, CR_EXTADD_ADD_NOTE);
    /* Add Note keeps common notes sounding across two held voices */
    MD(CR_MOD_MAJ); K(48, 1); K(55, 1); clear();
    TAPM(CR_MOD_9);
    expect("-62 +62/127 +69/127", "add_note on two held roots: each adds its 9th, C's 9th retriggers G's shared D");
    ok(cr_sounding(&C, CR_STREAM_MAIN, 62) == 2, "shared 9th owned twice");
    R(48, 1); R(55, 1); MU(CR_MOD_MAJ); step(5);
    ok(sounding() == 0, "two transformed voices release cleanly");
    clear();
    cr_set_playstyle(&C, CR_PS_SIMPLE);
    K(48, 1); clear();
    MD(CR_MOD_MAJ); expect("", "simple: modifier press while root held is ignored");
    MU(CR_MOD_MAJ); R(48, 1); clear();
}

static void s_free(void)
{
    int idx[16];
    cr_chord_info_t ci;
    fresh("free");
    cr_set_playstyle(&C, CR_PS_FREE);
    ok(C.playstyle == CR_PS_FREE, "playstyle free");
    /* Free transition table (design.md 7, implemented, parity unverified) */
    MD(CR_MOD_M7);
    expect("", "Free: idle, nothing remembered + modifier press -> nothing sounds");
    MU(CR_MOD_M7);
    MD(CR_MOD_MAJ); K(48, 1); R(48, 1); clear();
    MD(CR_MOD_MAJ7);
    expect("+48/127 +52/127 +55/127 +59/127", "Free: remembered root + modifier press -> free voice with last spec + modifier");
    cr_chord_info(&C, &ci);
    ok(!strcmp(ci.root, "C") && !strcmp(ci.sup, "M7") && ci.sounding, "free voice named CM7");
    MU(CR_MOD_MAJ7); expect("", "Free: a modifier released while others stay held -> no change");
    MU(CR_MOD_MAJ);
    expect("-48 -52 -55 -59", "Free: last modifier released -> free voice released");
    MD(CR_MOD_MAJ); K(48, 1); R(48, 1); MD(CR_MOD_MAJ7); clear();
    MD(CR_MOD_9); expect("+62/127", "Free: free voice + further modifier press -> additive (Add Note) transform");
    MU(CR_MOD_9); expect("", "Free: releasing it while others are held changes nothing");
    MU(CR_MOD_MAJ7); MU(CR_MOD_MAJ);
    expect("-48 -52 -55 -59 -62", "Free: ... and the last release ends the transformed free voice");
    MD(CR_MOD_MAJ); K(48, 1); R(48, 1); MD(CR_MOD_MAJ7); clear();
    K(48, 5);
    ok(nlog > 0 && LOG[0].k == '-', "free voice releases before real root press");
    ons(idx, 16, 0);
    ok(count('+', 0) == 4 && LOG[idx[0]].vel == 64 && SND[0][59], "Free: the real press is a normal gesture with the held modifiers (Cmaj7 @64)");
    R(48, 5); MU(CR_MOD_MAJ7); MU(CR_MOD_MAJ); step(50);
    ok(sounding() == 0, "free interleavings leave nothing sounding");
    MD(CR_MOD_MAJ); K(48, 1); R(48, 1); MD(CR_MOD_MAJ7); clear();
    K(50, 1);
    ok(count('-', -1) == 0 && count('+', 0) == 4 && SND[0][48], "Free: a real press in another column leaves the free voice sounding");
    R(50, 1);
    ok(SND[0][48] && SND[0][59] && !SND[0][50], "Free: the other column's release ends only its own chord");
    MU(CR_MOD_MAJ7); MU(CR_MOD_MAJ); step(5);
    ok(sounding() == 0, "Free: modifiers up release the free voice");
    clear();
    /* root held: as Advanced; root release remembers root + transformed spec */
    K(48, 1); MD(CR_MOD_MAJ);
    expect("+48/127 -48 +48/127 +52/127 +55/127", "Free: root held + modifier press -> as Advanced");
    MU(CR_MOD_MAJ); expect("", "Free: root held + modifier release -> no change");
    R(48, 1); expect("-48 -52 -55", "Free: root release -> voice off");
    MD(CR_MOD_9);
    expect("+48/127 +52/127 +55/127 +62/127", "Free: root and its transformed spec are remembered (C + MAJ + 9)");
    MU(CR_MOD_9); clear();
    MD(CR_MOD_MAJ); K(48, 1); R(48, 1); MD(CR_MOD_MAJ7); clear();
    cr_set_playstyle(&C, CR_PS_ADVANCED);
    expect("-48 -52 -55 -59", "Free: playstyle change releases free voices");
    MU(CR_MOD_MAJ7); MU(CR_MOD_MAJ);
    cr_set_playstyle(&C, CR_PS_FREE); clear();
    MD(CR_MOD_6); expect("", "Free: playstyle change cleared the remembered root");
    MU(CR_MOD_6);
    K(48, 1); R(48, 1); cr_panic(&C); clear();
    MD(CR_MOD_MAJ); expect("", "Free: panic cleared the remembered root");
    MU(CR_MOD_MAJ);
    MD(CR_MOD_MAJ); K(48, 1); R(48, 1); MD(CR_MOD_MAJ7);
    cr_set_sticky(&C, 1); clear();
    K(50, 1);
    ok(count('-', 0) == 4 && count('+', 0) == 4 && SND[0][50] && !SND[0][48], "Free + Sticky: first press of the next gesture replaces the free voice too");
    R(50, 1); MU(CR_MOD_MAJ7); MU(CR_MOD_MAJ);
    ok(SND[0][50], "Free + Sticky: the new gesture stays latched");
    cr_set_sticky(&C, 0); step(5);
    ok(sounding() == 0, "Free + Sticky: latch off releases everything");
    cr_set_playstyle(&C, CR_PS_SIMPLE); clear();
    K(48, 1); R(48, 1); clear();
    MD(CR_MOD_MAJ); expect("", "Simple: modifier with nothing held never retriggers");
    MU(CR_MOD_MAJ);
}

static void s_strum(void)
{
    int idx[64], n, k;
    double d1, d2, rate;
    char got[128];
    fresh("strum");
    cr_set_perform(&C, 1);
    ok(C.perform_on && C.perform_mode == CR_PM_STRUM, "perform enabled, Strum selected");
    MD(CR_MOD_MAJ); K(48, 1); step(200);
    n = ons(idx, 64, 0);
    ok(n == 3 && LOG[idx[0]].note == 48 && LOG[idx[1]].note == 52 && LOG[idx[2]].note == 55, "strum: 3 ascending note-ons");
    d1 = tms(LOG[idx[1]].t - LOG[idx[0]].t); d2 = tms(LOG[idx[2]].t - LOG[idx[0]].t);
    ok(n == 3 && d1 > 40 - TOL() && d1 < 40 + TOL() && d2 > 80 - TOL() && d2 < 80 + TOL(), "strum: ~40 ms spacing");
    ok(count('-', 0) == 0, "strum: notes sustain");
    clear(); R(48, 1);
    expect("-48 -52 -55", "strum: sustained notes off on release");
    K(48, 1); step(50); clear(); R(48, 1); step(200);
    ok(count('+', -1) == 0 && count('-', 0) == 2 && sounding() == 0, "mid-strum release: no late notes, fired notes released");
    clear();
    K(48, 1); step(50); K(48, 3); step(200);
    {
        int v95 = 0;
        n = ons(idx, 64, 0);
        for (k = 0; k < n; k++) v95 += LOG[idx[k]].vel == 95;
        ok(v95 == 3, "supersede mid-strum: full restrum at new velocity");
    }
    R(48, 3); R(48, 1); step(50);
    ok(sounding() == 0, "supersede strum: clean release");
    clear();
    cr_set_param(&C, CR_PM_STRUM, CR_P_DIR, CR_DIR_DOWN);
    K(48, 1); step(200);
    n = ons(idx, 64, 0);
    ok(n == 3 && LOG[idx[0]].note == 55 && LOG[idx[1]].note == 52 && LOG[idx[2]].note == 48, "strum down: descending order");
    R(48, 1); clear();
    cr_set_param(&C, CR_PM_STRUM, CR_P_DIR, CR_DIR_UPDOWN);
    K(48, 1); step(300);
    n = ons(idx, 64, 0);
    ok(n == 5 && LOG[idx[2]].note == 55 && LOG[idx[3]].note == 52 && LOG[idx[4]].note == 48, "strum updown (one-shot): 48 52 55 52 48");
    R(48, 1); step(5);
    ok(sounding() == 0, "strum updown: repeated notes release cleanly");
    clear();
    cr_set_param(&C, CR_PM_STRUM, CR_P_DIR, CR_DIR_RANDOM);
    K(48, 1); step(200);
    on_notes_sorted(got, 0);
    ok(!strcmp(got, "48 52 55"), "strum random: every chord note once");
    R(48, 1); clear();
    cr_set_param(&C, CR_PM_STRUM, CR_P_DIR, CR_DIR_UP);
    cr_set_param(&C, CR_PM_STRUM, CR_P_RANGE, 2);
    K(48, 1); step(400);
    n = ons(idx, 64, 0);
    ok(n == 6 && LOG[idx[3]].note == 60 && LOG[idx[4]].note == 64 && LOG[idx[5]].note == 67, "strum range 2: expanded one octave up (48..67)");
    R(48, 1); clear();
    cr_set_param(&C, CR_PM_STRUM, CR_P_RANGE, 1);
    /* the strum works on the VOICED chord (design.md 13): Cmaj7 voiced +1 strums E G B C */
    cr_voicing_step(&C, 1); MD(CR_MOD_MAJ7);
    K(48, 1); step(200);
    n = ons(idx, 64, 0);
    ok(n == 4 && LOG[idx[0]].note == 52 && LOG[idx[3]].note == 60, "strum up plays the voiced Cmaj7: E3 G3 B3 C4");
    R(48, 1); MU(CR_MOD_MAJ7); cr_voicing_step(&C, -1); clear();
    /* harp */
    cr_set_perform_mode(&C, CR_PM_HARP);
    K(48, 1); step(300);
    ok(count('+', 0) == 9 && count('-', 0) == 9, "harp: 9 gated notes across 3 octaves");
    ok(sounding() == 0, "harp: self-terminated before release");
    clear(); R(48, 1);
    expect("", "harp: release after sweep is silent");
    K(48, 1); step(25); R(48, 1); step(300);
    ok(count('+', 0) <= 4 && sounding() == 0, "mid-harp release: sweep cancelled cleanly");
    clear();
    /* sub-tick rates batch into same-tick bursts, in order */
    cr_set_param(&C, CR_PM_HARP, CR_P_RATE, 1);
    K(48, 1); step(30);
    n = ons(idx, 64, 0);
    {
        int order = n == 9;
        for (k = 1; k < n; k++) if (LOG[idx[k]].note <= LOG[idx[k - 1]].note) order = 0;
        ok(order, "harp 1 ms: 9 notes in ascending order");
        ok(n == 9 && tms(LOG[idx[8]].t - LOG[idx[0]].t) <= 8 + TOL(), "harp 1 ms: the sweep collapses into tick bursts within one tick of 8 ms");
    }
    R(48, 1); step(20);
    ok(sounding() == 0, "harp 1 ms: every gated note ended");
    cr_set_param(&C, CR_PM_HARP, CR_P_RATE, 8);
    clear();
    /* slop */
    cr_set_perform_mode(&C, CR_PM_SLOP);
    K(48, 1); step(300);
    ok(count('+', 0) == 3, "slop: all 3 notes fire");
    R(48, 1); step(50);
    ok(sounding() == 0, "slop: clean release");
    clear();
    cr_set_param(&C, CR_PM_SLOP, CR_P_AMOUNT, 100);
    {
        int all = 1, late = 0, t;
        for (t = 0; t < 8; t++) {
            uint64_t t0 = T;
            K(48, 1); step(200);
            n = ons(idx, 64, 0);
            on_notes_sorted(got, 0);
            if (strcmp(got, "48 52 55")) all = 0;
            for (k = 0; k < n; k++) if (tms(LOG[idx[k]].t - t0) > 120 + TOL()) late = 1;
            R(48, 1); step(5); clear();
        }
        ok(all, "slop 100%: every gesture plays each chord note once");
        ok(!late, "slop 100%: jitter stays within one rate of the nominal time");
    }
    cr_set_param(&C, CR_PM_SLOP, CR_P_AMOUNT, 30);
    /* perform off re-sounds held chords directly */
    cr_set_perform_mode(&C, CR_PM_STRUM);
    K(48, 1); step(300); clear();
    cr_set_perform(&C, 0);
    ok(!C.perform_on, "perform toggled off");
    step(50);
    ok(sounding() == 3, "chord re-sounded directly after bypass");
    R(48, 1); step(50);
    ok(sounding() == 0, "clean after bypass and release");
    clear();
    /* perform + bass: bass fires immediately */
    cr_set_perform(&C, 1); cr_set_bass(&C, 1);
    K(48, 1); step(20);
    ok(SND[1][36], "bass fires immediately under strum");
    ok(count('+', 0) == 1, "only the first strum note before 40 ms");
    step(300); R(48, 1); step(50);
    ok(sounding() == 0, "perform+bass: clean release");
    cr_set_bass(&C, 0); cr_set_perform(&C, 0);
    MU(CR_MOD_MAJ); clear();
    /* advanced + perform: transform restarts the performance */
    cr_set_playstyle(&C, CR_PS_ADVANCED); cr_set_perform(&C, 1);
    K(48, 1); step(50); MD(CR_MOD_MAJ); step(50); MD(CR_MOD_MAJ7); step(300);
    ok(SND[0][59] && SND[0][48], "advanced transform under perform restarts the strum with the new chord");
    MU(CR_MOD_MAJ7); MU(CR_MOD_MAJ); R(48, 1); step(100);
    ok(sounding() == 0, "advanced transform under perform: no stuck notes");
    cr_set_perform(&C, 0); cr_set_playstyle(&C, CR_PS_SIMPLE);
    /* strum / harp rates follow the BPM (120-BPM reference, design.md 11) */
    cr_set_perform(&C, 1); cr_set_tempo(&C, 60);
    MD(CR_MOD_MAJ); clear(); K(48, 1); step(300);
    n = ons(idx, 64, 0);
    rate = n == 3 ? tms(LOG[idx[1]].t - LOG[idx[0]].t) : 0;
    ok(n == 3 && rate > 80 - TOL() && rate < 80 + TOL(), "strum 40 ms @120 BPM -> 80 ms @60 BPM");
    R(48, 1); step(5); clear();
    /* a tempo change mid-gesture does NOT restart it: scheduled notes keep their times (deviation from 20) */
    K(48, 1); step(100); clear();
    cr_set_tempo(&C, 240);
    ok(nlog == 0, "tempo change does not restart the held strum (no offs, no re-strum)");
    step(100);
    n = ons(idx, 64, 0);
    ok(n == 1 && LOG[idx[0]].note == 55 && count('-', 0) == 0, "the held strum's last note still fires at its scheduled time");
    ok(sounding() == 3, "the held strum keeps sounding all three notes");
    R(48, 1); step(5); clear();
    K(48, 1); step(100);
    n = ons(idx, 64, 0);
    rate = n == 3 ? tms(LOG[idx[1]].t - LOG[idx[0]].t) : 0;
    ok(n == 3 && rate > 20 - TOL() && rate < 20 + TOL(), "the next gesture uses the new rate (20 ms @240 BPM)");
    R(48, 1); step(5); clear();
    cr_set_perform_mode(&C, CR_PM_HARP);
    K(48, 1); step(300); clear();
    cr_set_tempo(&C, 60); step(300);
    ok(nlog == 0, "tempo change does not re-sweep a held harp");
    R(48, 1); MU(CR_MOD_MAJ); step(10);
    cr_set_perform_mode(&C, CR_PM_STRUM);
    cr_set_tempo(&C, 120); cr_set_perform(&C, 0); clear();
}

static void s_arp(void)
{
    int idx[256], n, k, all;
    uint64_t t_press, t0;
    double sp1, sp2, drift;
    uint32_t ep;
    fresh("arp");
    cr_set_perform(&C, 1);
    cr_set_perform_mode(&C, CR_PM_ARP);
    ok(C.perform_mode == CR_PM_ARP && cr_get_param(&C, CR_PM_ARP, CR_P_HOLD) == 1, "arp selected, Hold default on");
    cr_set_param(&C, CR_PM_ARP, CR_P_HOLD, 0);
    MD(CR_MOD_MAJ);
    t_press = T; K(48, 1); step(900);
    n = ons(idx, 256, 0);
    ok(n == 4, "arp: 4 steps in 900 ms at 1/8");
    ok(n == 4 && LOG[idx[0]].note == 48 && LOG[idx[1]].note == 52 && LOG[idx[2]].note == 55 && LOG[idx[3]].note == 48, "arp: cycles up through the chord");
    sp1 = tms(LOG[idx[1]].t - LOG[idx[0]].t); sp2 = tms(LOG[idx[2]].t - LOG[idx[1]].t);
    ok(sp1 > 250 - TOL() && sp1 < 250 + TOL() && sp2 > 250 - TOL() && sp2 < 250 + TOL(), "arp: 250 ms steps");
    ok(tms(LOG[idx[0]].t - t_press) < TOL(), "arp retrig on: first step immediate");
    for (k = 0; k < nlog; k++) if (LOG[k].k == '-' && LOG[k].note == 48) break;
    ok(k < nlog && LOG[k].ms - LOG[idx[0]].ms >= 175 && LOG[k].ms - LOG[idx[0]].ms <= 175 + TICK_US / 1000 + 1,
       "arp: 70% gate = 175 ms note length (engine ms, within one tick)");
    clear(); R(48, 1); step(600);
    ok(count('+', -1) == 0 && sounding() == 0, "arp stops on release, clean");
    clear();
    t0 = T; K(48, 1); step(6010);
    n = ons(idx, 256, 0);
    ok(n == 25, "arp: 25 steps in 6 s incl. t=0 and t=6 s");
    drift = n ? tms(LOG[idx[n - 1]].t - t0) - (n - 1) * 250.0 : 99;
    ok(n == 25 && drift > -TOL() && drift < TOL(), "arp: no cumulative drift");
    ok(n == 25 && grid_ok(idx, n, 250, 1), "arp: every one of 25 steps within one tick of its exact due time");
    R(48, 1); step(300); clear();
    cr_set_param(&C, CR_PM_ARP, CR_P_SWING, 75);
    K(48, 1); step(1100);
    n = ons(idx, 256, 0);
    sp1 = tms(LOG[idx[1]].t - LOG[idx[0]].t); sp2 = tms(LOG[idx[2]].t - LOG[idx[0]].t);
    ok(sp1 > 375 - TOL() && sp1 < 375 + TOL() && sp2 > 500 - TOL() && sp2 < 500 + TOL(), "arp swing 75: steps at 375/500 ms");
    R(48, 1); cr_set_param(&C, CR_PM_ARP, CR_P_SWING, 50); step(300); clear();
    /* orders */
    {
        static const struct { int dir; const char *name; int want[6]; } O[] = {
            { CR_DIR_UPDOWN, "arp updown: 48 52 55 52 48 52", { 48, 52, 55, 52, 48, 52 } },
            { CR_DIR_DOWN, "arp down: 55 52 48 55 52 48", { 55, 52, 48, 55, 52, 48 } },
            { CR_DIR_DOWNUP, "arp downup: 55 52 48 52 55 52", { 55, 52, 48, 52, 55, 52 } },
            { CR_DIR_PLAYED, "arp played: voiced order 48 52 55 48 52 55", { 48, 52, 55, 48, 52, 55 } },
        };
        for (k = 0; k < 4; k++) {
            int j, good;
            cr_set_param(&C, CR_PM_ARP, CR_P_DIR, O[k].dir);
            K(48, 1); step(1400);
            n = ons(idx, 256, 0);
            good = n >= 6;
            for (j = 0; good && j < 6; j++) if (LOG[idx[j]].note != O[k].want[j]) good = 0;
            ok(good, O[k].name);
            R(48, 1); step(300); clear();
        }
    }
    cr_set_param(&C, CR_PM_ARP, CR_P_DIR, CR_DIR_PLAYED);
    cr_set_param(&C, CR_PM_ARP, CR_P_RANGE, 2);
    K(48, 1); step(1400);
    n = ons(idx, 256, 0);
    ok(n >= 6 && LOG[idx[3]].note == 60 && LOG[idx[5]].note == 67, "arp played range 2: chord, then the chord an octave up");
    R(48, 1); step(300); clear();
    cr_set_param(&C, CR_PM_ARP, CR_P_RANGE, 1);
    cr_set_param(&C, CR_PM_ARP, CR_P_DIR, CR_DIR_RANDOM);
    K(48, 1); step(2400);
    n = ons(idx, 256, 0);
    all = n >= 9;
    for (k = 0; k < n; k++) if (LOG[idx[k]].note != 48 && LOG[idx[k]].note != 52 && LOG[idx[k]].note != 55) all = 0;
    ok(all, "arp random: every step is a chord tone, on the 1/8 grid");
    R(48, 1); step(300); clear();
    cr_set_param(&C, CR_PM_ARP, CR_P_DIR, CR_DIR_UP);
    /* retrig off: notes land on the global grid, never immediately */
    cr_set_param(&C, CR_PM_ARP, CR_P_RETRIG, 0);
    step(137);
    t0 = T; K(48, 1); step(800);
    n = ons(idx, 256, 0);
    ep = cr_clock_epoch(&C);
    ok(n >= 3, "arp retrig off: running");
    ok(n && LOG[idx[0]].t > t0 && tms(LOG[idx[0]].t - t0) < 250 + TOL(), "retrig off: first note waits for the grid (never immediate)");
    all = n > 0;
    for (k = 0; k < n; k++) if ((LOG[idx[k]].ms - ep) % 250u > (uint32_t)(TICK_US / 1000u + 1u)) all = 0;
    ok(all, "retrig off: aligned to the global 250 ms grid");
    cr_set_playstyle(&C, CR_PS_ADVANCED);
    R(48, 1); step(100); clear();
    K(48, 1); step(600);
    MD(CR_MOD_MIN); step(800);
    n = ons(idx, 256, 0);
    {
        int minor3 = 0;
        all = 1;
        for (k = 0; k < n; k++) {
            if (LOG[idx[k]].note == 51) minor3 = 1;
            if ((LOG[idx[k]].ms - ep) % 250u > (uint32_t)(TICK_US / 1000u + 1u)) all = 0;
        }
        ok(minor3, "retrig off substitution: minor third appears");
        ok(all, "retrig off substitution: every step stays on the grid");
    }
    MU(CR_MOD_MIN); R(48, 1); step(400);
    ok(sounding() == 0, "retrig off: clean stop");
    cr_set_playstyle(&C, CR_PS_SIMPLE);
    /* supersede with retrig off: the sequence continues, at the new velocity */
    K(48, 1); step(300); clear();
    K(48, 3);
    ok(nlog == 0, "retrig off: a same-root retrigger does not restart the arp");
    step(600);
    n = ons(idx, 256, 0);
    ok(n >= 2 && LOG[idx[0]].vel == 95, "retrig off: following steps use the new velocity");
    R(48, 3); R(48, 1); step(300); clear();
    cr_set_param(&C, CR_PM_ARP, CR_P_RETRIG, 1);
    /* a note-off due in the same tick as the release (gate 100%) */
    cr_set_param(&C, CR_PM_ARP, CR_P_GATE, 100);
    K(48, 1); step(250); R(48, 1); step(300);
    ok(sounding() == 0 && stray_off == 0, "batch-drain: release in the tick of a pending note-off, no stray or stuck note");
    clear();
    cr_set_param(&C, CR_PM_ARP, CR_P_GATE, 120);
    K(48, 1); step(1000);
    ok(cr_sounding(&C, CR_STREAM_MAIN, 48) + cr_sounding(&C, CR_STREAM_MAIN, 52) + cr_sounding(&C, CR_STREAM_MAIN, 55) >= 1, "arp gate 120%: notes overlap");
    R(48, 1); step(500);
    ok(sounding() == 0, "arp gate 120%: overlaps resolve, nothing stuck");
    cr_set_param(&C, CR_PM_ARP, CR_P_GATE, 70);
    clear();
    /* single-note arp with gate 200%: the same note overlaps itself */
    cr_set_param(&C, CR_PM_ARP, CR_P_GATE, 200);
    MU(CR_MOD_MAJ); K(60, 1); step(1100);
    ok(cr_sounding(&C, CR_STREAM_MAIN, 60) == 2, "single-note arp at 200% gate: the note is owned twice (refcount)");
    R(60, 1); step(600);
    ok(sounding() == 0, "single-note arp overlap releases cleanly");
    cr_set_param(&C, CR_PM_ARP, CR_P_GATE, 70); MD(CR_MOD_MAJ); clear();
    /* Arp Hold (latch) */
    cr_set_param(&C, CR_PM_ARP, CR_P_HOLD, 1);
    ok(cr_latching(&C), "arp Hold on: latching");
    K(48, 1); step(300); R(48, 1); clear(); step(600);
    ok(count('+', 0) > 0 && cr_voices(&C) > 0, "arp Hold: chord keeps arping after release (latched voice)");
    clear();
    K(55, 1); step(600); R(55, 1);
    {
        int g = 0;
        for (k = 0; k < nlog; k++) if (LOG[k].k == '+' && LOG[k].note % 12 == 7) g = 1;
        ok(g && cr_voices(&C) == 1 && !SND[0][48], "arp Hold: a new chord replaces and keeps arping (G present)");
    }
    clear(); step(400);
    ok(count('+', 0) > 0, "arp Hold: still latched and arping after releasing the new chord");
    cr_set_param(&C, CR_PM_ARP, CR_P_HOLD, 0);
    ok(!cr_latching(&C), "arp Hold tapped off");
    step(200);
    ok(sounding() == 0 && cr_voices(&C) == 0, "arp Hold off releases the latched voice (no stuck note)");
    cr_set_param(&C, CR_PM_ARP, CR_P_HOLD, 1);
    K(48, 1); step(200); R(48, 1); clear(); step(400);
    ok(count('+', 0) > 0 && cr_voices(&C) > 0, "arp Hold: latched again");
    cr_set_perform(&C, 0); step(100);
    ok(!C.perform_on && sounding() == 0 && cr_voices(&C) == 0, "disabling Perform releases the latched voice");
    cr_set_perform(&C, 1);
    K(48, 1); step(200); R(48, 1);
    cr_set_perform_mode(&C, CR_PM_STRUM); step(100);
    ok(sounding() == 0 && cr_voices(&C) == 0, "switching away from Arp releases a Hold-latched voice");
    cr_set_perform_mode(&C, CR_PM_ARP);
    cr_set_param(&C, CR_PM_ARP, CR_P_HOLD, 0);
    MU(CR_MOD_MAJ); clear();
}

static void s_pattern(void)
{
    int idx[256], n;
    fresh("pattern");
    cr_set_perform(&C, 1);
    cr_set_perform_mode(&C, CR_PM_PATTERN);
    ok(C.perform_mode == CR_PM_PATTERN && !cr_latching(&C), "pattern selected (no Hold)");
    MD(CR_MOD_MAJ); K(48, 1); step(900);
    n = ons(idx, 256, 0);
    ok(n >= 4 && LOG[idx[0]].note == 48 && LOG[idx[1]].note == 52 && LOG[idx[2]].note == 55 && LOG[idx[3]].note == 48,
       "pattern 1 over triad: 48 52 55 48 (wrap)");
    ok(n >= 4 && LOG[idx[0]].vel == 127 && LOG[idx[1]].vel == 71 && LOG[idx[2]].vel == 95 && LOG[idx[3]].vel == 71,
       "pattern 1 accents scale the trigger velocity (127, 71, 95, 71)");
    R(48, 1); step(300); clear();
    cr_set_param(&C, CR_PM_PATTERN, CR_P_ROTATE, 1);
    K(48, 1); step(700);
    n = ons(idx, 256, 0);
    ok(n >= 3 && LOG[idx[0]].note == 52 && LOG[idx[1]].note == 55 && LOG[idx[2]].note == 48, "pattern rotate 1: 52 55 48");
    clear();
    cr_set_param(&C, CR_PM_PATTERN, CR_P_ROTATE, 0);
    cr_set_param(&C, CR_PM_PATTERN, CR_P_PATTERN, 6);
    step(100);
    n = ons(idx, 256, 0);
    ok(cr_voices(&C) == 1 && (n == 0 || LOG[idx[0]].note == 48 || LOG[idx[0]].note == 52 || LOG[idx[0]].note == 55),
       "pattern selection changes the next step without releasing the root");
    step(600);
    ok(count('+', 0) >= 1 && cr_voices(&C) == 1, "the held root keeps playing the new pattern");
    cr_set_param(&C, CR_PM_PATTERN, CR_P_PATTERN, 1);
    R(48, 1); step(300); clear();
    cr_set_param(&C, CR_PM_PATTERN, CR_P_ROTATE, -1);
    K(48, 1); step(200);
    n = ons(idx, 256, 0);
    ok(n == 1 && LOG[idx[0]].note == 55, "pattern rotate -1: index 1 wraps to the top note (55)");
    R(48, 1); step(300); clear();
    cr_set_param(&C, CR_PM_PATTERN, CR_P_ROTATE, 0);
    cr_set_param(&C, CR_PM_PATTERN, CR_P_PATTERN, 6);
    K(48, 3); step(1100);
    n = ons(idx, 256, 0);
    ok(n == 3 && LOG[idx[0]].note == 48 && LOG[idx[1]].note == 52 && LOG[idx[2]].note == 55, "pattern 6: rest steps emit nothing (1 . 2 3 .)");
    ok(n == 3 && LOG[idx[0]].vel == 95 && LOG[idx[1]].vel == 77 && LOG[idx[2]].vel == 59, "pattern 6 accents: 95, 77, 59 from velocity 95");
    R(48, 3); step(300); clear();
    cr_set_param(&C, CR_PM_PATTERN, CR_P_PATTERN, 4);
    cr_set_param(&C, CR_PM_PATTERN, CR_P_RANGE, 2);
    K(48, 1); step(200);
    n = ons(idx, 256, 0);
    ok(n == 1 && LOG[idx[0]].note == 60, "pattern 4 (cascade) index 4 over range 2 picks the 4th expanded note (60)");
    R(48, 1); step(300); clear();
    cr_set_param(&C, CR_PM_PATTERN, CR_P_RANGE, 1);
    cr_set_param(&C, CR_PM_PATTERN, CR_P_PATTERN, 1);
    cr_set_param(&C, CR_PM_PATTERN, CR_P_DIV, CR_DIV_1_16);
    K(48, 1); step(450);
    n = ons(idx, 256, 0);
    ok(n == 4 && tms(LOG[idx[1]].t - LOG[idx[0]].t) > 125 - TOL() && tms(LOG[idx[1]].t - LOG[idx[0]].t) < 125 + TOL(), "pattern 1/16: 125 ms steps");
    R(48, 1); step(300); clear();
    cr_set_param(&C, CR_PM_PATTERN, CR_P_DIV, CR_DIV_1_8T);
    K(48, 1); step(700);
    n = ons(idx, 256, 0);
    ok(n == 5 && grid_ok(idx, n, 500, 3), "pattern 1/8T: steps at k * 166.67 ms, each rounded once (no drift)");
    R(48, 1); step(300); clear();
    cr_set_param(&C, CR_PM_PATTERN, CR_P_DIV, CR_DIV_1_8);
    MU(CR_MOD_MAJ); cr_set_perform(&C, 0); step(100);
    ok(sounding() == 0, "pattern end: nothing sounding");
}

static void s_live(void)
{
    int idx[256], n, k;
    uint8_t pn;
    fresh("live params");
    cr_set_perform(&C, 1); cr_set_perform_mode(&C, CR_PM_ARP); cr_set_param(&C, CR_PM_ARP, CR_P_HOLD, 0);
    MD(CR_MOD_MAJ); K(48, 1); step(600);
    ok(cr_perform_pos(&C, &pn) >= 0, "perform position available while the arp runs");
    cr_set_param(&C, CR_PM_ARP, CR_P_DIR, CR_DIR_DOWN); step(600);
    cr_set_param(&C, CR_PM_ARP, CR_P_RANGE, 2); step(600);
    ok(cr_voices(&C) == 1, "live order / range edits keep the voice running");
    clear();
    cr_set_param(&C, CR_PM_ARP, CR_P_DIV, CR_DIV_1_1); step(30);
    ok(count('+', 0) >= 1, "live slow division rephases the held arp immediately");
    clear();
    cr_set_param(&C, CR_PM_ARP, CR_P_DIV, CR_DIV_1_16); step(300);
    ok(count('+', 0) >= 3, "live fast division continues without root retrigger");
    cr_set_param(&C, CR_PM_ARP, CR_P_DIV, CR_DIV_1_8);
    R(48, 1); step(400);
    ok(sounding() == 0, "live param edits on running arp: clean");
    cr_set_param(&C, CR_PM_ARP, CR_P_DIR, CR_DIR_UP); cr_set_param(&C, CR_PM_ARP, CR_P_RANGE, 1);
    clear();
    /* tempo rephases a running arp keeping its melodic position */
    K(48, 1); step(260);   /* steps 0 (48), 1 (52) fired */
    clear();
    cr_set_tempo(&C, 60);
    n = ons(idx, 256, 0);
    ok(n == 1 && LOG[idx[0]].note == 55, "tempo change: the arp rephases now with its next melodic note (55)");
    step(1100);
    n = ons(idx, 256, 0);
    ok(n == 3 && tms(LOG[idx[1]].t - LOG[idx[0]].t) > 500 - TOL() - 3 && tms(LOG[idx[2]].t - LOG[idx[1]].t) < 500 + TOL(), "... and steps at the new tempo (500 ms @60 BPM)");
    ok(n == 3 && LOG[idx[1]].note == 48 && LOG[idx[2]].note == 52, "... continuing the sequence (48 52)");
    R(48, 1); step(600);
    cr_set_tempo(&C, 300);
    ok(C.bpm == 300, "tempo 300");
    cr_set_tempo(&C, 400);
    ok(C.bpm == 300, "tempo clamps at 300");
    cr_set_tempo(&C, 5);
    ok(C.bpm == 20, "tempo clamps at 20");
    cr_set_tempo(&C, 120); clear();
    /* retrig off at a new tempo lands on the new grid */
    cr_set_param(&C, CR_PM_ARP, CR_P_RETRIG, 0);
    cr_set_tempo(&C, 90); step(77);
    K(48, 1); step(1500);
    n = ons(idx, 256, 0);
    {
        uint32_t ep = cr_clock_epoch(&C);
        int all = n >= 3;
        for (k = 0; k < n; k++) {
            uint32_t ph = (uint32_t)(((uint64_t)(LOG[idx[k]].ms - ep) * 90u) % 30000u);   /* 1/8 @90 = 333.3 ms */
            if (ph > 90u * (TICK_US / 1000u + 2u) && ph < 30000u - 90u) all = 0;
        }
        ok(all, "retrig off @90 BPM: steps on the 333.3 ms grid");
    }
    R(48, 1); step(500); cr_set_tempo(&C, 120);
    cr_set_param(&C, CR_PM_ARP, CR_P_RETRIG, 1); clear();
    /* the global grid stays exact across its 16-beat rebasing (with a remainder at 70 BPM) */
    cr_set_param(&C, CR_PM_ARP, CR_P_RETRIG, 0);
    cr_set_tempo(&C, 70);
    {
        uint32_t t_reset = C.now;
        int all;
        step(40000);
        clear();
        K(48, 1); step(30000);
        n = ons(idx, 256, 0);
        all = n >= 69;
        for (k = 0; k < n; k++) {
            /* 1/8 @70 BPM = 30000/70 ms: (ms - reset) * 70 is a multiple of 30000, late by at most a tick */
            uint32_t ph = (uint32_t)((uint64_t)(LOG[idx[k]].ms - t_reset) * 70u % 30000u);
            if (ph > 70u * (TICK_US / 1000u + 2u) && ph < 30000u - 70u) all = 0;
        }
        ok(all, "retrig off @70 BPM after 40 s of grid rebasing: 70 steps on the tempo-change grid");
        all = n >= 69;
        for (k = 1; k < n; k++) {
            uint32_t d = LOG[idx[k]].ms - LOG[idx[k - 1]].ms;
            if (d + TICK_US / 1000u + 1u < 428u || d > 429u + TICK_US / 1000u + 1u) all = 0;
        }
        ok(all, "... consecutive steps 428-429 ms apart");
    }
    R(48, 1); step(600); cr_set_tempo(&C, 120); cr_set_param(&C, CR_PM_ARP, CR_P_RETRIG, 1); clear();
    /* finite performance parameters refresh from the held chord */
    cr_set_perform_mode(&C, CR_PM_STRUM);
    K(48, 1); step(150); clear();
    cr_set_param(&C, CR_PM_STRUM, CR_P_RANGE, 2); step(250);
    {
        int upper = 0;
        for (k = 0; k < nlog; k++) if (LOG[k].k == '+' && LOG[k].note >= 60) upper = 1;
        ok(upper, "finite performance parameters refresh from the held chord");
    }
    R(48, 1); step(300); cr_set_param(&C, CR_PM_STRUM, CR_P_RANGE, 1);
    cr_set_perform(&C, 0); MU(CR_MOD_MAJ);
    ok(C.perform_mode == CR_PM_STRUM && !C.perform_on, "live params end state reset");
    ok(cr_get_param(&C, CR_PM_ARP, CR_P_GATE) == 70 && cr_get_param(&C, CR_PM_HARP, CR_P_RANGE) == 3
       && cr_get_param(&C, CR_PM_SLOP, CR_P_AMOUNT) == 30 && cr_get_param(&C, CR_PM_HARP, CR_P_RATE) == 8,
       "performance defaults (design.md 21)");
    cr_set_param(&C, CR_PM_ARP, CR_P_GATE, 0);
    ok(cr_get_param(&C, CR_PM_ARP, CR_P_GATE) == 1, "gate clamps to >= 1%");
    cr_set_param(&C, CR_PM_PATTERN, CR_P_PATTERN, 13);
    ok(cr_get_param(&C, CR_PM_PATTERN, CR_P_PATTERN) == 12, "pattern clamps to 12");
    clear();
}

static void s_streams(void)
{
    fresh("streams");
    cr_set_stream(&C, CR_STREAM_RAW, 1);
    MD(CR_MOD_MAJ); K(48, 1);
    expect("+48/127 +52/127 +55/127 +48/127r +52/127r +55/127r", "raw chord stream mirrors voiced chord");
    R(48, 1); MU(CR_MOD_MAJ);
    expect("-48 -52 -55 -48r -52r -55r", "raw chord stream releases with root");
    cr_set_perform(&C, 1);
    MD(CR_MOD_MAJ); K(48, 1);
    ok(count('+', 2) == 3 && count('+', 0) == 1, "perform on: raw block chord + first strum note");
    R(48, 1); MU(CR_MOD_MAJ); step(200); clear();
    cr_set_perform(&C, 0);
    K(52, 4); R(52, 4);
    expect("+52/79 +52/79r -52 -52r", "a single note goes to main and raw");
    cr_set_stream(&C, CR_STREAM_MAIN, 0);
    MD(CR_MOD_MAJ); K(48, 1);
    expect("+48/127r +52/127r +55/127r", "main route off leaves raw chord stream sounding");
    cr_set_stream(&C, CR_STREAM_MAIN, 1);
    R(48, 1);
    expect("-48r -52r -55r", "a route turned on mid-note leaves the owner as it started");
    MU(CR_MOD_MAJ);
    /* raw follows Advanced transforms additively */
    cr_set_playstyle(&C, CR_PS_ADVANCED);
    MD(CR_MOD_MAJ); K(48, 1); clear();
    TAPM(CR_MOD_MAJ7);
    expect("+59/127r +59/127", "advanced add-note: raw and main both add the M7");
    R(48, 1); MU(CR_MOD_MAJ); step(5);
    ok(sounding() == 0, "raw + transform: clean");
    cr_set_playstyle(&C, CR_PS_SIMPLE);
    /* raw turned off while sounding: the owner still ends */
    MD(CR_MOD_MAJ); K(48, 1); cr_set_stream(&C, CR_STREAM_RAW, 0); clear();
    R(48, 1);
    expect("-48 -52 -55 -48r -52r -55r", "raw route off mid-note: its notes still end with the root");
    MU(CR_MOD_MAJ);
}

static void s_bassmodes(void)
{
    fresh("bass behaviour");
    cr_set_bass(&C, 1);
    ok(C.bass_mode == CR_BASS_CHORDS_ONLY, "Bass Behaviour default: Chords Only");
    K(50, 1); expect("+50/127", "Chords Only: a single note has no bass");
    R(50, 1); clear();
    MD(CR_MOD_MAJ); K(50, 1); expect("+50 +54 +57 +38/127b", "Chords Only: a chord adds its root in the bass register (D2)");
    R(50, 1); MU(CR_MOD_MAJ); clear();
    cr_set_playstyle(&C, CR_PS_ADVANCED);
    K(48, 1); MD(CR_MOD_MAJ);
    expect("+48 -48 +48 +52 +55 +36/127b", "Chords Only + Advanced: the bass joins when the note becomes a chord");
    R(48, 1); MU(CR_MOD_MAJ);
    expect("-48 -52 -55 -36b", "... and ends with it");
    cr_set_playstyle(&C, CR_PS_SIMPLE);
    cr_set_bass_mode(&C, CR_BASS_UNISON);
    K(50, 1); expect("+50/127 +50/127b", "Unison: a single note is mirrored at the same pitch");
    R(50, 1); clear();
    MD(CR_MOD_MAJ); K(50, 1); expect("+50 +54 +57 +50/127b", "Unison: a chord's bass is the played key's pitch");
    R(50, 1); MU(CR_MOD_MAJ); clear();
    cr_bass_voicing_step(&C, -1);
    K(50, 1); expect("+50 +38/127b", "Unison: bass voicing shifts the mirror by octaves");
    R(50, 1); clear(); cr_bass_voicing_step(&C, 1);
    cr_set_key(&C, 1, 0, CR_SCALE_MAJOR);
    K(49, 1); expect("+48 +53 +55 +49/127b", "Unison: the literal played note (C#) under the quantized Csus");
    R(49, 1); clear(); cr_set_key(&C, 0, 0, CR_SCALE_MAJOR);
    cr_set_bass_mode(&C, CR_BASS_SINGLE_NOTES);
    K(50, 1); expect("+38/127b", "Single Notes: a single note sounds only on the bass");
    R(50, 1); expect("-38b", "Single Notes: its release");
    MD(CR_MOD_MIN); K(50, 1); expect("+50 +53 +57 +38/127b", "Single Notes: a chord sounds on main and bass");
    R(50, 1); MU(CR_MOD_MIN); clear();
    cr_set_stream(&C, CR_STREAM_RAW, 1);
    K(50, 1); expect("+38/127b", "Single Notes: no raw stream for a single note either");
    R(50, 1); clear();
    cr_set_playstyle(&C, CR_PS_ADVANCED);
    K(48, 1); MD(CR_MOD_MAJ);
    expect("+36/127b +48 +52 +55 +48r +52r +55r", "Single Notes + Advanced: main and raw join when the note becomes a chord");
    R(48, 1); MU(CR_MOD_MAJ); step(5);
    ok(sounding() == 0, "Single Notes transform: clean release");
    cr_set_playstyle(&C, CR_PS_SIMPLE); clear();
    cr_set_bass_mode(&C, CR_BASS_SOLO);
    MD(CR_MOD_MAJ); K(48, 1);
    expect("+36/127b", "Solo: bass without main or raw");
    R(48, 1); MU(CR_MOD_MAJ); expect("-36b", "Solo: release");
    cr_set_perform(&C, 1); cr_set_perform_mode(&C, CR_PM_ARP); cr_set_param(&C, CR_PM_ARP, CR_P_HOLD, 0);
    MD(CR_MOD_MAJ); K(48, 1); step(600);
    ok(count('+', 0) == 0 && count('+', 1) == 1, "Solo: no performance output, one bass note");
    R(48, 1); MU(CR_MOD_MAJ); step(5); cr_set_perform(&C, 0); clear();
    cr_set_bass(&C, 0);
    MD(CR_MOD_MAJ); K(48, 1);
    expect("+48 +52 +55 +48r +52r +55r", "Bass off: the behaviour does not silence main / raw");
    R(48, 1); MU(CR_MOD_MAJ); clear();
    cr_set_stream(&C, CR_STREAM_RAW, 0);
}

static void s_transpose(void)
{
    cr_chord_info_t ci;
    int16_t root; uint8_t q;
    fresh("transpose");
    cr_set_transpose(&C, 2);
    cr_set_bass(&C, 1);
    MD(CR_MOD_MAJ); K(48, 1);
    expect("+50/127 +54/127 +57/127 +38/127b", "transpose +2: C major sounds D major, bass D2");
    cr_chord_info(&C, &ci);
    ok(!strcmp(ci.root, "D") && ci.notes[0] == 50, "chord info names the sounding (transposed) chord");
    cr_set_transpose(&C, -3);
    expect("", "changing transpose leaves the sounding chord alone");
    cr_voicing_step(&C, 1);
    expect("-50 -54 -57 +54/127 +57/127 +62/127", "a held chord re-voices with its captured transpose");
    cr_voicing_step(&C, -1); clear();
    R(48, 1);
    expect("-50 -54 -57 -38b", "a held chord releases its captured notes");
    K(48, 1); expect("+45 +49 +52 +33/127b", "transpose -3: the next gesture is A major, bass A1");
    R(48, 1); MU(CR_MOD_MAJ); clear();
    cr_set_key(&C, 1, 0, CR_SCALE_MAJOR);
    cr_set_transpose(&C, 1);
    K(50, 1); expect("+51 +54 +58 +39/127b", "Key Mode resolves the pressed key, then transposes (Dm -> D#m)");
    R(50, 1); clear();
    cr_pad_arm(&C, 0); K(50, 1); R(50, 1);
    ok(cr_pad_get(&C, 0, &root, &q, NULL, NULL) && root == 50 && q == CR_Q_MIN, "a pad stores the untransposed chord");
    cr_set_transpose(&C, 0); cr_set_key(&C, 0, 0, CR_SCALE_MAJOR); clear();
    cr_pad_down(&C, 0); expect("+50 +53 +57 +38/127b", "the pad replays with the current transpose (0)");
    cr_pad_up(&C, 0); clear();
    cr_set_transpose(&C, 40); ok(C.transpose == 24, "transpose clamps at +24");
    cr_set_transpose(&C, -40); ok(C.transpose == -24, "transpose clamps at -24");
    cr_set_transpose(&C, 0); cr_set_bass(&C, 0);
}

static void s_queries(void)
{
    cr_chord_info_t ci;
    uint8_t note;
    int pos, k;
    fresh("queries");
    cr_chord_info(&C, &ci);
    ok(!ci.valid, "chord info: nothing played yet");
    MD(CR_MOD_MAJ); MD(CR_MOD_MAJ7); K(50, 1);
    cr_chord_info(&C, &ci);
    ok(ci.valid && ci.sounding && !strcmp(ci.root, "D") && !strcmp(ci.qual, "") && !strcmp(ci.sup, "M7"), "Dmaj7 -> D M7");
    ok(ci.nnotes == 4 && ci.notes[0] == 50 && ci.notes[3] == 61 && ci.quality == CR_Q_MAJ && ci.ext == CR_EXT_MAJ7, "Dmaj7 info: notes and ids");
    R(50, 1);
    cr_chord_info(&C, &ci);
    ok(ci.valid && !ci.sounding && !strcmp(ci.root, "D") && !strcmp(ci.sup, "M7"), "after release the last chord stays shown, not sounding");
    MU(CR_MOD_MAJ7);
    K(48, 1); K(55, 1); R(55, 1);
    cr_chord_info(&C, &ci);
    ok(ci.sounding && !strcmp(ci.root, "C"), "releasing the newest of two chords shows the one still sounding");
    R(48, 1); MU(CR_MOD_MAJ);
    cr_set_perform(&C, 1); cr_set_perform_mode(&C, CR_PM_ARP); cr_set_param(&C, CR_PM_ARP, CR_P_HOLD, 0);
    MD(CR_MOD_MIN); K(57, 1); step(10);
    pos = cr_perform_pos(&C, &note);
    ok(pos == 0 && note == 57, "perform position: first arp step on the root");
    step(250);
    pos = cr_perform_pos(&C, &note);
    ok(pos == 1 && note == 60, "perform position hops to the 3rd");
    cr_set_param(&C, CR_PM_ARP, CR_P_RANGE, 2);
    for (k = 0; k < 3; k++) step(250);
    pos = cr_perform_pos(&C, &note);
    ok(pos == 1 && note == 72, "perform position folds an upper-octave note onto its chord index");
    R(57, 1); MU(CR_MOD_MIN); step(300);
    ok(cr_perform_pos(&C, &note) == -1, "no perform position when nothing performs");
    cr_set_param(&C, CR_PM_ARP, CR_P_RANGE, 1);
    cr_set_perform(&C, 0);
    cr_set_key(&C, 0, 4, CR_SCALE_MAJOR);
    ok(cr_scale_mask(&C) == 0, "scale mask 0 when Key Mode is off");
    cr_set_key(&C, 1, 4, CR_SCALE_MAJOR);
    ok(cr_scale_mask(&C) == ((1 << 4) | (1 << 6) | (1 << 8) | (1 << 9) | (1 << 11) | (1 << 1) | (1 << 3)), "scale mask E major");
    cr_set_key(&C, 0, 0, CR_SCALE_MAJOR);
    clear();
}

static void s_limits(void)
{
    int k;
    cr_t B;
    fresh("limits");
    /* the voice pool: more roots than voices are ignored, never stranded */
    for (k = 0; k < 20; k++) K(40 + k, 1);
    ok(cr_voices(&C) == CR_MAX_VOICES && count('+', 0) == CR_MAX_VOICES, "voice pool: 16 roots sound, the rest are ignored");
    for (k = 0; k < 20; k++) R(40 + k, 1);
    ok(sounding() == 0 && cr_voices(&C) == 0, "voice pool: releasing everything leaves nothing sounding");
    clear();
    /* the event pool: 16 harp sweeps of 7-note chords over 4 octaves overflow 128 events safely */
    cr_set_perform(&C, 1); cr_set_perform_mode(&C, CR_PM_HARP);
    cr_set_param(&C, CR_PM_HARP, CR_P_RANGE, 4); cr_set_param(&C, CR_PM_HARP, CR_P_RATE, 30);
    MD(CR_MOD_MAJ); MD(CR_MOD_6); MD(CR_MOD_M7); MD(CR_MOD_MAJ7);
    for (k = 0; k < 16; k++) K(30 + k * 2, 1);
    ok(C.ev_overflow > 0 && cr_pending(&C) <= CR_MAX_EV, "event pool: overflow is counted, the queue never exceeds 128");
    step(1500);
    ok(sounding() == 0, "event pool: every harp note that started was gated off");
    for (k = 0; k < 16; k++) R(30 + k * 2, 1);
    MU(CR_MOD_MAJ7); MU(CR_MOD_M7); MU(CR_MOD_6); MU(CR_MOD_MAJ);
    step(50);
    ok(sounding() == 0 && cr_pending(&C) == 0, "event pool: clean after the overflow");
    cr_set_perform(&C, 0); clear();
    /* two instances coexist */
    cr_init(&B, NULL);
    cr_tick(&B, now_ms());
    cr_mod(&B, CR_MOD_MAJ, 1); cr_key(&B, 48, 100, 1);
    ok(cr_sounding(&B, CR_STREAM_MAIN, 52) == 1 && cr_voices(&B) == 1, "a second instance plays its own chord");
    ok(cr_voices(&C) == 0 && nlog == 0 && cr_sounding(&C, CR_STREAM_MAIN, 52) == 0, "... without touching the first");
    MD(CR_MOD_MIN); K(48, 1);
    ok(cr_sounding(&C, CR_STREAM_MAIN, 51) == 1 && cr_sounding(&B, CR_STREAM_MAIN, 51) == 0, "the first plays Cm while the second holds C");
    R(48, 1); MU(CR_MOD_MIN);
    cr_key(&B, 48, 100, 0);
    ok(cr_voices(&B) == 0 && cr_sounding(&B, CR_STREAM_MAIN, 48) == 0, "the second instance releases (no output callbacks)");
    clear();
}

static void s_torture(void)
{
    static const cr_pmode_t modes[5] = { CR_PM_ARP, CR_PM_PATTERN, CR_PM_HARP, CR_PM_STRUM, CR_PM_SLOP };
    int mi;
    fresh("torture");
    cr_set_param(&C, CR_PM_ARP, CR_P_HOLD, 0);
    cr_set_perform(&C, 1);
    MD(CR_MOD_MAJ); K(48, 1); step(30);
    cr_set_perform_mode(&C, CR_PM_ARP); step(300);
    R(48, 1); MU(CR_MOD_MAJ); step(80);
    ok(sounding() == 0, "torture: mode switch mid-strum, then release");
    MD(CR_MOD_MAJ); K(48, 1); step(400);
    cr_set_perform(&C, 0); step(100); cr_set_perform(&C, 1); step(300);
    R(48, 1); MU(CR_MOD_MAJ); step(80);
    ok(sounding() == 0, "torture: toggle perform mid-arp");
    MD(CR_MOD_MAJ); K(48, 1); step(400);
    cr_voicing_step(&C, 1); step(70); cr_voicing_step(&C, 1); step(300);
    cr_voicing_step(&C, -1); step(300);
    R(48, 1); MU(CR_MOD_MAJ); step(80);
    ok(sounding() == 0, "torture: voicing change mid-arp");
    cr_voicing_step(&C, -1);
    cr_set_perform(&C, 0);
    cr_set_playstyle(&C, CR_PS_FREE);
    MD(CR_MOD_MAJ); K(48, 1); R(48, 1); MD(CR_MOD_MAJ7);
    cr_set_playstyle(&C, CR_PS_SIMPLE);
    MU(CR_MOD_MAJ7); MU(CR_MOD_MAJ); step(80);
    ok(sounding() == 0, "torture: playstyle change with Free voice active");
    cr_set_perform(&C, 1);
    for (mi = 0; mi < 5; mi++) {
        cr_set_perform_mode(&C, modes[mi]);
        MD(CR_MOD_MAJ);
        K(48, (mi % 8) + 1);
        K(52, ((mi + 3) % 8) + 1);
        step(120);
        cr_voicing_step(&C, 1);
        step(80);
        R(48, (mi % 8) + 1);
        R(52, ((mi + 3) % 8) + 1);
        MU(CR_MOD_MAJ);
        step(60);
    }
    step(400);
    ok(sounding() == 0, "torture: rapid mash across all five perform modes");
    cr_set_perform(&C, 0);
    for (mi = 0; mi < 5; mi++) cr_voicing_step(&C, -1);
    step(50);
    ok(sounding() == 0 && cr_pending(&C) == 0, "torture: fully silent at end, queue empty");
    /* a chaotic gesture stream: random keys, modifiers, settings, ticks; nothing may stick */
    {
        uint32_t r = 12345u;
        int k;
        for (k = 0; k < 4000; k++) {
            int a;
            r = r * 1103515245u + 12345u;
            a = (int)((r >> 16) % 23u);
            if (a < 6) K(48 + (int)((r >> 8) % 12u), 1 + (int)((r >> 4) % 3u));
            else if (a < 12) R(48 + (int)((r >> 8) % 12u), 1 + (int)((r >> 4) % 3u));
            else if (a < 14) cr_mod(&C, (cr_mod_t)((r >> 8) % 8u), (int)((r >> 4) & 1u));
            else if (a == 14) cr_set_perform(&C, (int)((r >> 8) & 1u));
            else if (a == 15) cr_set_perform_mode(&C, (cr_pmode_t)((r >> 8) % 5u));
            else if (a == 16) cr_voicing_step(&C, (r >> 8) & 1u ? 1 : -1);
            else if (a == 17) cr_set_playstyle(&C, (cr_playstyle_t)((r >> 8) % 3u));
            else if (a == 18) cr_set_sticky(&C, (int)((r >> 8) & 1u));
            else if (a == 19) cr_set_param(&C, (cr_pmode_t)((r >> 8) % 5u), (cr_param_t)((r >> 12) % 11u), (int)((r >> 4) % 100u));
            else if (a == 20) cr_set_bass(&C, (int)((r >> 8) & 1u));
            else if (a == 21) cr_set_tempo(&C, 60 + (int)((r >> 8) % 120u));
            else step(1 + (int)((r >> 8) % 40u));
        }
        cr_set_sticky(&C, 0);
        for (k = 0; k < 8; k++) MU((cr_mod_t)k);
        for (k = 0; k < 12; k++) { R(48 + k, 1); R(48 + k, 2); R(48 + k, 3); }
        for (k = 0; k < 5; k++) cr_set_param(&C, (cr_pmode_t)k, CR_P_HOLD, 0);
        step(3000);
        ok(sounding() == 0 && cr_voices(&C) == 0, "torture: 4000 random gestures, then everything up -> silence");
        ok(dup_on == 0 && stray_off == 0, "torture: no double note-on, no stray note-off in the random run");
        cr_panic(&C); clear();
    }
}

/* -------------------------------------------- tick-independent checks --- */
static void s_tables(void)
{
    static const struct { uint8_t q, x; const char *r, *qu, *sup; } N[] = {
        { CR_Q_MAJ, 0, "C", "", "" }, { CR_Q_MIN, 0, "C", "m", "" }, { CR_Q_DIM, 0, "C", "dim", "" },
        { CR_Q_SUS, 0, "C", "sus", "" }, { CR_Q_AUG, 0, "C", "+", "" }, { CR_Q_POW, 0, "C", "", "5" },
        { CR_Q_MIN4, 0, "C", "m", "add4" }, { CR_Q_MIN4, CR_EXT_6, "C", "m", "add4 6" }, { CR_Q_POW, CR_EXT_MAJ7, "C", "", "5 M7" }, { CR_Q_MIN4, CR_EXT_6 | CR_EXT_M7 | CR_EXT_9, "C", "m", "add4 JAZZ" }, { CR_Q_AUG, CR_EXT_M7, "C", "+", "7" }, { CR_Q_MAJ, CR_EXT_M7, "C", "", "7" }, { CR_Q_MAJ, CR_EXT_MAJ7, "C", "", "M7" },
        { CR_Q_MAJ, CR_EXT_6, "C", "", "6" }, { CR_Q_MAJ, CR_EXT_9, "C", "", "9" },
        { CR_Q_MIN, CR_EXT_M7 | CR_EXT_9, "C", "m", "7 9" }, { CR_Q_MIN, CR_EXT_M7 | CR_EXT_6, "C", "m", "7 6" },
        { CR_Q_MAJ, CR_EXT_MAJ7 | CR_EXT_9, "C", "", "M7 9" }, { CR_Q_MAJ, CR_EXT_MAJ7 | CR_EXT_6, "C", "", "M7 6" },
        { CR_Q_MAJ, CR_EXT_6 | CR_EXT_9, "C", "", "6 9" }, { CR_Q_MIN, CR_EXT_MAJ7, "C", "m", "M7" },
        { CR_Q_SUS, CR_EXT_M7, "C", "sus", "m7" }, { CR_Q_SUS, CR_EXT_M7 | CR_EXT_6, "C", "sus", "m7 6" },
        { CR_Q_DIM, CR_EXT_M7, "C", "dim", "7" }, { CR_Q_MIN, CR_EXT_6 | CR_EXT_M7 | CR_EXT_9, "C", "m", "JAZZ" },
        { CR_Q_SUS, CR_EXT_6 | CR_EXT_M7 | CR_EXT_9, "C", "sus", "JAZZ" }, { CR_Q_MAJ, 15, "C", "", "WTF" },
        { CR_Q_DIM, 15, "C", "dim", "WTF" }, { CR_Q_NONE, CR_EXT_M7, "C", "", "" },
    };
    static const char *const keymap_major[12] = { "C", "Csus", "Dm", "Dsus", "Em", "F", "Gsus", "G", "Gsus", "Am", "Asus", "Bdim" };
    static const char *const keymap_minor[12] = { "Cm", "Csus", "Ddim", "D#", "D#sus", "Fm", "Fsus", "Gm", "G#", "A#sus", "A#", "A#sus" };
    char r[3], q[5], s[CR_SUP_MAX], buf[64];
    uint8_t out[CR_CHORD_MAX];
    int k, n;
    strcpy(TAG, "once");
    for (k = 0; k < (int)(sizeof N / sizeof N[0]); k++) {
        cr_chord_name(0, N[k].q, N[k].x, r, q, s);
        snprintf(buf, sizeof buf, "chord name: %s%s %s", N[k].r, N[k].qu, N[k].sup);
        ok(!strcmp(r, N[k].r) && !strcmp(q, N[k].qu) && !strcmp(s, N[k].sup), buf);
    }
    cr_chord_name(10, CR_Q_MIN, CR_EXT_M7, r, q, s);
    ok(!strcmp(r, "A#") && !strcmp(q, "m") && !strcmp(s, "7"), "chord name: A#m 7 (sharps)");
    cr_note_name(60, buf); ok(!strcmp(buf, "C4"), "note name: 60 = C4");
    cr_note_name(61, buf); ok(!strcmp(buf, "C#4"), "note name: 61 = C#4");
    cr_note_name(0, buf); ok(!strcmp(buf, "C-1"), "note name: 0 = C-1");
    cr_note_name(127, buf); ok(!strcmp(buf, "G9"), "note name: 127 = G9");
    /* the chord table, firmware-verified intervals */
    {
        static const struct { uint8_t q; const char *iv; } Q[] = {
            { CR_Q_DIM, "0 3 6" }, { CR_Q_MIN, "0 3 7" }, { CR_Q_MAJ, "0 4 7" }, { CR_Q_SUS, "0 5 7" },
            { CR_Q_AUG, "0 4 8" }, { CR_Q_POW, "0 7 12" }, { CR_Q_MIN4, "0 3 5" }, { CR_Q_NONE, "0" },
        };
        for (k = 0; k < 8; k++) {
            int j;
            char got[64] = "", nm[64];
            n = cr_chord_base(60, Q[k].q, 0, out);
            for (j = 0; j < n; j++) snprintf(got + strlen(got), 8, j ? " %d" : "%d", out[j] - 60);
            snprintf(nm, sizeof nm, "chord table: quality %d = %s", Q[k].q, Q[k].iv);
            ok(!strcmp(got, Q[k].iv), nm);
        }
    }
    n = cr_chord_base(60, CR_Q_MAJ, CR_EXT_6 | CR_EXT_M7 | CR_EXT_MAJ7 | CR_EXT_9, out);
    ok(n == 7 && out[3] == 69 && out[4] == 70 && out[5] == 71 && out[6] == 74, "extensions: 6 +9, m7 +10, M7 +11, 9 +14");
    n = cr_chord_base(60, CR_Q_POW, CR_EXT_9, out);
    ok(n == 4 && out[3] == 74, "power chord + 9 stacks normally");
    n = cr_chord_base(-3, CR_Q_MAJ, 0, out);
    ok(n == 2 && out[0] == 1 && out[1] == 4, "chord below MIDI 0 keeps only the notes in range");
    /* the whole KEYMAP, both scales over C: root + quality of every pressed pitch class */
    for (k = 0; k < 24; k++) {
        cr_t *c = &C;
        int pc = k % 12, minor = k >= 12;
        cr_chord_info_t ci;
        char nm[96], want[16];
        cr_init(c, NULL);
        cr_set_key(c, 1, 0, minor ? CR_SCALE_MINOR : CR_SCALE_MAJOR);
        cr_key(c, (uint8_t)(60 + pc), 100, 1);
        cr_chord_info(c, &ci);
        snprintf(want, sizeof want, "%s", minor ? keymap_minor[pc] : keymap_major[pc]);
        snprintf(buf, sizeof buf, "%s%s", ci.root, ci.qual);
        snprintf(nm, sizeof nm, "KEYMAP C %s: %s -> %s", minor ? "minor" : "major", (const char *[]){ "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }[pc], want);
        ok(!strcmp(buf, want), nm);
    }
    cr_init(&C, NULL);
    for (k = 1; k <= 12; k++) {
        char nm[64];
        snprintf(nm, sizeof nm, "pattern %d named %s", k, cr_pattern_name(k));
        ok(cr_pattern_name(k)[0] != 0, nm);
    }
    ok(!strcmp(cr_pattern_name(1), "pulse") && !strcmp(cr_pattern_name(12), "ladder") && !cr_pattern_name(13)[0], "pattern names: pulse .. ladder");
    ok(sizeof(cr_t) < 16384, "cr_t fits in 16 KB");
    printf("     [once] sizeof(cr_t) = %u bytes\n", (unsigned)sizeof(cr_t));
}

static void s_single(void)
{
    fresh("single notes");
    ok(C.single == CR_SINGLE_FULL && C.split_pc == 5, "Single Notes default: Full Octave, split F");
    K(62, 1); expect("+62/127", "Full Octave: D4 sounds D4");
    R(62, 1); clear();
    cr_set_single_notes(&C, CR_SINGLE_SPLIT, 5);
    K(62, 1); expect("+50/127", "Split at F: D4 (below the split) sounds D3");
    R(62, 1); expect("-50", "its release ends the lowered note");
    clear();
    K(67, 1); expect("+67/127", "Split at F: G4 (at or above the split) sounds G4");
    R(67, 1); clear();
    MD(CR_MOD_MAJ); K(62, 1); expect("+62/127 +66/127 +69/127", "Split: chords are unaffected (D major at D4)");
    R(62, 1); MU(CR_MOD_MAJ); clear();
    cr_set_single_notes(&C, CR_SINGLE_SPLIT, 0);
    K(60, 1); expect("+60/127", "split point C: nothing is below it");
    R(60, 1); clear();
    cr_set_single_notes(&C, CR_SINGLE_SPLIT, 99); ok(C.split_pc == 11, "the split point clamps to B");
    cr_set_single_notes(&C, CR_SINGLE_FULL, 5); clear();
}

/* the looper's two hooks: out.gesture (capture) and cr_loop_event (playback through loop voices) */
static struct { uint32_t gid; int16_t root; uint8_t q, ext, vel; int on; } GL[32];
static int ngl;
static void cb_gesture(void *ud, uint32_t gid, int16_t root, uint8_t q, uint8_t ext, uint8_t vel, int on)
{
    (void)ud;
    if (ngl < 32) { GL[ngl].gid = gid; GL[ngl].root = root; GL[ngl].q = q; GL[ngl].ext = ext; GL[ngl].vel = vel; GL[ngl].on = on; ngl++; }
}
static void s_loopvoice(void)
{
    fresh("loop voices");
    C.out.gesture = cb_gesture;
    ngl = 0;
    MD(CR_MOD_MAJ); K(62, 1);
    ok(ngl == 1 && GL[0].on && GL[0].root == 62 && GL[0].q == CR_Q_MAJ && GL[0].ext == 0 && GL[0].vel == 127,
       "gesture hook: MAJ + D4 begins a gesture (D, major, vel 127)");
    R(62, 1); MU(CR_MOD_MAJ);
    ok(ngl == 2 && !GL[1].on && GL[1].gid == GL[0].gid, "gesture hook: its release ends the same gesture");
    clear(); ngl = 0;
    cr_set_transpose(&C, 2);
    cr_set_key(&C, 1, 0, CR_SCALE_MAJOR);
    K(64, 1);
    ok(ngl == 1 && GL[0].root == 66 && GL[0].q == CR_Q_MIN, "gesture hook: the resolved chord (Key Mode Em, transpose +2: F#m)");
    R(64, 1); cr_set_key(&C, 0, 0, CR_SCALE_MAJOR); cr_set_transpose(&C, 0);
    clear(); ngl = 0;
    cr_set_playstyle(&C, CR_PS_ADVANCED);
    MD(CR_MOD_MIN); K(62, 1); MD(CR_MOD_M7); MU(CR_MOD_M7); R(62, 1); MU(CR_MOD_MIN);
    ok(ngl == 2 && GL[1].q == CR_Q_MIN && GL[1].ext == CR_EXT_M7, "gesture hook (Advanced): the end carries the final chord (Dm7)");
    cr_set_playstyle(&C, CR_PS_SIMPLE);
    clear(); ngl = 0;
    K(62, 1); K(62, 2);
    ok(ngl == 3 && !GL[1].on && GL[2].on && GL[2].gid != GL[0].gid && GL[2].vel == VEL[1],
       "gesture hook: a same-root retrigger ends the gesture, begins another");
    R(62, 2); clear(); ngl = 0;
    ok(cr_loop_event(&C, 0, 62, CR_Q_MAJ, 0, 90, 1) == 1 && cr_loop_busy(&C, 0), "cr_loop_event: loop voice 0 sounds");
    expect("+62/90 +66/90 +69/90", "cr_loop_event: D major at 90, no gesture");
    ok(ngl == 0, "a loop voice never reaches the gesture hook");
    cr_loop_event(&C, 0, 0, 0, 0, 0, 0);
    expect("-62 -66 -69", "cr_loop_event off: its notes end");
    clear();
    cr_voicing_step(&C, 1);
    cr_loop_event(&C, 1, 62, CR_Q_MAJ, CR_EXT_M7, 100, 1);
    expect("+66 +69 +72 +74", "loop voice: voicing applies (D7, +1: F#4 A4 C5 D5)");
    cr_voicing_step(&C, -1); clear();
    cr_loop_event(&C, 1, 0, 0, 0, 0, 0); clear();
    cr_set_bass(&C, 1);
    cr_loop_event(&C, 2, 64, CR_Q_MIN, 0, 100, 1);
    ok(count('+', 1) == 1, "loop voice: the bass follows it");
    cr_set_key(&C, 1, 2, CR_SCALE_MAJOR); cr_set_transpose(&C, 5);
    cr_loop_event(&C, 3, 60, CR_Q_NONE, 0, 100, 1);
    ok(SND[0][60] == 1, "loop voice: the recorded root is absolute (no Key Mode, no transpose)");
    cr_set_key(&C, 0, 0, CR_SCALE_MAJOR); cr_set_transpose(&C, 0);
    ok(cr_loop_busy(&C, 2) && cr_loop_busy(&C, 3), "two loop voices sound side by side");
    cr_loop_event(&C, 2, 0, 0, 0, 0, 0); cr_loop_event(&C, 3, 0, 0, 0, 0, 0);
    cr_set_perform(&C, 1);
    cr_set_perform_mode(&C, CR_PM_STRUM);
    clear();
    cr_loop_event(&C, 4, 62, CR_Q_MAJ, 0, 100, 1);
    step(400);
    {
        int idx[8], n = ons(idx, 8, 0);
        ok(n == 3 && LOG[idx[2]].t > LOG[idx[0]].t, "loop voice: performance applies (strummed: the ons spread out)");
    }
    cr_loop_event(&C, 2, 64, CR_Q_MIN, 0, 100, 1);
    cr_panic(&C);
    ok(!cr_loop_busy(&C, 2) && !cr_loop_busy(&C, 4) && sounding() == 0, "panic ends the loop voices");
    cr_set_perform(&C, 0); cr_set_bass(&C, 0);
    ok(cr_loop_event(&C, CR_MAX_LOOPV, 62, CR_Q_MAJ, 0, 100, 1) == 0, "loop voice out of range: refused");
    C.out.gesture = 0;
    fresh("loop voices end");
}

static void run_suite(uint32_t tick_us, uint32_t base_ms, const char *tag)
{
    strcpy(TAG, tag);
    TICK_US = tick_us; BASE_MS = base_ms;
    T = 0; next_tick = tick_us;
    memset(SND, 0, sizeof SND); dup_on = stray_off = 0;
    cr_init(&C, &OUT);
    cr_tick(&C, now_ms());
    s_keyboard(); s_sticky(); s_chords(); s_panic(); s_voicing(); s_bass(); s_keymode(); s_pads();
    s_secret(); s_advanced(); s_free(); s_strum(); s_arp(); s_pattern(); s_live(); s_streams();
    s_bassmodes(); s_transpose(); s_single(); s_loopvoice(); s_queries(); s_limits(); s_torture();
    fresh("end");
}

int main(void)
{
    s_tables();
    run_suite(1000, 0, "1ms");
    run_suite(2902, 0, "2.9ms");
    run_suite(5000, 0, "5ms");
    run_suite(2902, 0xFFFFFFFFu - 60000u, "2.9ms wrap");
    printf("\n%d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
