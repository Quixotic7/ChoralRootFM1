/* SPDX-License-Identifier: GPL-3.0-only
 * Host tests of the ChoralRoot looper (firmware/src/cr_loop.c) on the real engine (cr_engine.c):
 *   cc -std=c99 -Wall -Wextra -Werror -pedantic -o build/host/cr_loop_test tests/cr_loop_test.c \
 *      firmware/src/cr_loop.c firmware/src/cr_engine.c
 * Time runs in 1 ms steps: cr_tick then cr_loop_tick, as cr_out.c's audio block does. One line per check. */
#include <stdio.h>
#include <string.h>
#include "../firmware/src/cr_loop.h"

static cr_t C;
static cr_loop_t L;
static uint32_t NOW;
static int passed, failed;
static uint8_t SND[3][128];
static int dup_on, stray_off;
typedef struct { char k; uint8_t s, note, vel; uint32_t ms; } ev_t;
static ev_t LOG[20000];
static int nlog;
static int clicks[3];

static void cb_on(void *ud, cr_stream_t s, uint8_t n, uint8_t v)
{
    (void)ud;
    if (SND[s][n]) dup_on++;
    SND[s][n] = 1;
    if (nlog < 20000) { LOG[nlog].k = '+'; LOG[nlog].s = (uint8_t)s; LOG[nlog].note = n; LOG[nlog].vel = v; LOG[nlog].ms = NOW; nlog++; }
}
static void cb_off(void *ud, cr_stream_t s, uint8_t n)
{
    (void)ud;
    if (!SND[s][n]) stray_off++;
    SND[s][n] = 0;
    if (nlog < 20000) { LOG[nlog].k = '-'; LOG[nlog].s = (uint8_t)s; LOG[nlog].note = n; LOG[nlog].vel = 0; LOG[nlog].ms = NOW; nlog++; }
}
static void cb_all(void *ud, cr_stream_t s) { (void)ud; memset(SND[s], 0, sizeof SND[s]); }
static void cb_gesture(void *ud, uint32_t gid, int16_t root, uint8_t q, uint8_t ext, uint8_t vel, int on)
{
    (void)ud;
    cr_loop_gesture(&L, gid, root, q, ext, vel, on);
}
static const cr_out_t OUT = { cb_on, cb_off, cb_all, NULL, cb_gesture };

static void ok(int c, const char *name)
{
    if (c) passed++; else failed++;
    printf("%s %s\n", c ? "ok  " : "FAIL", name);
}
static void step(uint32_t ms)
{
    while (ms--) {
        NOW++;
        cr_tick(&C, NOW);
        cr_loop_tick(&L, &C, NOW);
        if (L.click) { clicks[L.click]++; L.click = 0; }
    }
}
static int sounding(void)
{
    int s, n, k = 0;
    for (s = 0; s < 3; s++) for (n = 0; n < 128; n++) k += SND[s][n];
    return k;
}
static void reset(void)
{
    memset(SND, 0, sizeof SND);
    dup_on = stray_off = nlog = 0;
    NOW = 1000;
    cr_init(&C, &OUT);
    cr_loop_init(&L);
    L.mode = CRL_MODE_OVERDUB;                       /* (the scenarios before the record modes: REC over a loop dubs) */
    cr_tick(&C, NOW);
    cr_loop_tick(&L, &C, NOW);
    memset(clicks, 0, sizeof clicks);
}
static void chord(int note, cr_mod_t m, uint32_t hold)   /* hold a chord type + a root for hold ms */
{
    cr_mod(&C, m, 1); cr_key(&C, (uint8_t)note, 100, 1);
    step(hold);
    cr_key(&C, (uint8_t)note, 100, 0); cr_mod(&C, m, 0);
}
/* the times (ms) of the note-ons of `note` on MAIN since log entry from */
static int ons_of(int note, int from, uint32_t *t, int max)
{
    int k, n = 0;
    for (k = from; k < nlog && n < max; k++)
        if (LOG[k].k == '+' && LOG[k].s == 0 && LOG[k].note == note) t[n++] = LOG[k].ms;
    return n;
}
static int near(uint32_t a, uint32_t b, uint32_t tol) { return a > b ? a - b <= tol : b - a <= tol; }

static void s_free(void)
{
    uint32_t t[8];
    int n, mark;
    char name[128];
    reset();
    ok(cr_loop_rec(&L, &C) == CRL_DID_ARM && L.cap == CRL_CAP_ARMED, "free: REC arms (nothing runs until a chord)");
    step(100);
    ok(L.cap == CRL_CAP_ARMED, "free: still armed after 100 ms of silence");
    chord(62, CR_MOD_MAJ, 500);                      /* D at bar 1 beat 1 */
    ok(L.cap == CRL_CAP_REC, "free: the first chord started the take");
    step(500);
    chord(64, CR_MOD_MIN, 500);                      /* Em at beat 3 */
    step(500);
    mark = nlog;
    ok(cr_loop_rec(&L, &C) == CRL_DID_COMMIT, "free: REC again commits");
    ok(L.state == CRL_PLAYING && L.d.nlayers == 1 && L.d.nev == 2, "free: playing, 1 layer, 2 events");
    snprintf(name, sizeof name, "free: 2 s at 120 BPM = one 4/4 bar (len %u ticks)", (unsigned)L.d.len);
    ok(L.d.len >= 383 && L.d.len <= 385, name);
    ok(L.d.ev[0].t == 0 && L.d.ev[1].t >= 191 && L.d.ev[1].t <= 193 && (L.d.ev[0].qx >> 4) == CR_Q_MAJ &&
       (L.d.ev[1].qx >> 4) == CR_Q_MIN && L.d.ev[0].root == 62 && L.d.ev[1].root == 64,
       "free: events D at 0, Em at beat 3 (resolved roots and qualities)");
    ok(L.d.ev[0].dur >= 95 && L.d.ev[0].dur <= 97, "free: a 500 ms chord lasts a beat (96 ticks)");
    step(3990);
    n = ons_of(62, mark, t, 8);
    snprintf(name, sizeof name, "playback: D re-triggers every 2 s (%d ons: %u %u)", n, n > 0 ? (unsigned)t[0] : 0u,
             n > 1 ? (unsigned)t[1] : 0u);
    ok(n == 2 && near(t[1] - t[0], 2000, 6), name);
    {
        uint32_t e[8];
        int m = ons_of(64, mark, e, 8);
        ok(m == 2 && near(e[0] - t[0], 1000, 6) && near(e[1] - t[1], 1000, 6), "playback: Em 1 s after each D");
    }
    ok(cr_loop_ring(&L) < 256, "the ring position is a fraction of the cycle");
    /* voicing applies live */
    cr_voicing_step(&C, 1);
    mark = nlog;
    step(2000);
    n = ons_of(74, mark, t, 8);
    ok(n >= 1 && ons_of(62, mark, t, 8) == 0, "voicing +1 at playback: D's lowest note moved up (D5, no D4)");
    cr_voicing_step(&C, -1);
    /* tempo: 240 BPM halves the cycle */
    cr_set_tempo(&C, 240);
    step(1500);
    mark = nlog;
    step(3000);
    n = ons_of(62, mark, t, 8);
    snprintf(name, sizeof name, "240 BPM: the loop cycles in 1 s (%d ons)", n);
    ok(n >= 2 && near(t[1] - t[0], 1000, 6), name);
    cr_set_tempo(&C, 120);
    /* overdub, undo */
    ok(cr_loop_rec(&L, &C) == CRL_DID_OD_ARM && L.cap == CRL_CAP_OD_ARMED, "overdub: REC while playing arms it");
    step(300);
    chord(60, CR_MOD_MAJ, 300);
    ok(L.cap == CRL_CAP_OD, "overdub: the chord opened a layer");
    ok(cr_loop_rec(&L, &C) == CRL_DID_OD_END && L.d.nlayers == 2 && L.d.nev == 3, "overdub: REC ends it: 2 layers, 3 events");
    mark = nlog;
    step(2000);
    ok(ons_of(60, mark, t, 8) == 1, "overdub: C plays in the next cycle");
    ok(cr_loop_undo(&L, &C) == CRL_DID_UNDO && L.d.nlayers == 1 && L.d.nev == 2, "undo: the overdub layer is gone");
    step(500);
    mark = nlog;
    step(2000);
    ok(ons_of(60, mark, t, 8) == 0 && ons_of(62, mark, t, 8) == 1, "undo: C no longer plays, D still does");
    ok(cr_loop_undo(&L, &C) == CRL_DID_NOTHING, "undo: the first layer stays (CLEAR empties)");
    ok(cr_loop_play(&L, &C) == CRL_DID_STOP && L.state == CRL_STOPPED, "LOOP tap: stop");
    step(1500);
    ok(sounding() == 0, "stop: nothing left sounding");
    ok(cr_loop_play(&L, &C) == CRL_DID_PLAY && L.state == CRL_PLAYING, "LOOP tap: play again from the start");
    step(10);
    ok(SND[0][62] == 1, "play: the event at 0 sounds at once");
    ok(cr_loop_clear(&L, &C) == CRL_DID_CLEAR && L.state == CRL_EMPTY && L.d.nev == 0, "clear: empty");
    step(100);
    ok(sounding() == 0 && dup_on == 0 && stray_off == 0, "clear: no stuck, double or stray note");
}

static void s_sync(void)
{
    char name[128];
    uint32_t t[8];
    int mark;
    reset();
    L.sync = 2;                                      /* 2 bars */
    L.count_in = 1;
    cr_loop_metro(&L, 0);
    ok(cr_loop_rec(&L, &C) == CRL_DID_COUNTIN && L.cap == CRL_CAP_COUNTIN, "sync: REC starts a one-bar count-in");
    step(1000);
    {
        uint32_t bar, beat;
        cr_loop_where(&L, &bar, &beat);
        ok(bar == 0 && beat == 2, "sync: half-way through the count-in, 2 beats left");
    }
    step(1000);
    ok(L.cap == CRL_CAP_REC, "sync: recording after one bar");
    snprintf(name, sizeof name, "sync: the count-in clicked 4 beats, the first accented (%d + %d)", clicks[2], clicks[1]);
    ok(clicks[2] == 1 && clicks[1] == 3, name);
    step(30);
    chord(62, CR_MOD_MAJ, 400);                      /* 30 ms late: 1/16 quantize puts it on 0 */
    step(1570);
    chord(69, CR_MOD_MIN, 1000);                     /* bar 2 */
    ok(L.cap == CRL_CAP_REC, "sync: still recording in bar 2");
    step(2000);
    ok(L.cap == CRL_CAP_NONE && L.state == CRL_PLAYING && L.d.len == 768,
       "sync: committed by itself after 2 bars (768 ticks), playing");
    ok(L.d.nev == 2 && L.d.ev[0].t >= 5 && L.d.ev[0].t <= 6, "sync: no quantize: D 30 ms (5.76 ticks) late as played");
    mark = nlog;
    step(4000);
    ok(ons_of(62, mark, t, 8) == 1, "sync: D plays once per 4 s cycle");
    cr_loop_play(&L, &C);
    cr_loop_clear(&L, &C);
    L.quant = 4;                                     /* 1/16 */
    cr_loop_rec(&L, &C);
    step(2000 + 30);
    chord(62, CR_MOD_MAJ, 400);
    step(4000);
    ok(L.d.nev == 1 && L.d.ev[0].t == 0, "quantize 1/16: a chord 6 ticks late lands on 0");
    ok(L.d.ev[0].dur >= 81 && L.d.ev[0].dur <= 84, "quantize: its note-off kept (the length grows by the shift)");
    /* panic while playing: stopped, content kept */
    cr_panic(&C);
    cr_loop_panic(&L);
    ok(L.state == CRL_STOPPED && L.d.nev == 1 && sounding() == 0, "panic: stopped, loop kept, silence");
    step(500);
    ok(cr_loop_play(&L, &C) == CRL_DID_PLAY, "after panic: plays again");
    step(100);
    cr_loop_play(&L, &C);
    step(1500);
    ok(sounding() == 0, "stop after panic: silence");
    /* panic during a fresh take drops it */
    cr_loop_clear(&L, &C);
    cr_loop_rec(&L, &C);
    step(2100);
    chord(62, CR_MOD_MAJ, 200);
    cr_panic(&C);
    cr_loop_panic(&L);
    ok(L.state == CRL_EMPTY && L.d.nev == 0, "panic during a take: dropped");
}

static void s_slots(void)
{
    static crl_data_t a, b;
    static uint8_t buf[CRL_REC_MAX];
    uint32_t n, t[8];
    int mark;
    reset();
    cr_loop_rec(&L, &C);
    chord(62, CR_MOD_MAJ, 300);
    step(700);
    cr_loop_rec(&L, &C);                             /* a 1 s loop of D */
    cr_loop_rec(&L, &C);
    step(200);
    chord(65, CR_MOD_MIN, 200);
    cr_loop_rec(&L, &C);                             /* + an overdub of Fm */
    n = cr_loop_pack(&L.d, buf, sizeof buf);
    ok(n == CRL_REC_HDR + 2u * 2u + 7u * 2u, "pack: header + 2 layer counts + 2 x 7 bytes");
    ok(cr_loop_unpack(buf, n, &a) == 1 && a.len == L.d.len && a.nev == 2 && a.nlayers == 2 &&
       memcmp(a.ev, L.d.ev, sizeof a.ev[0] * 2) == 0, "unpack: the same loop back (layers kept)");
    buf[n - 2] = 200;                                /* a velocity out of range */
    ok(cr_loop_unpack(buf, n, &b) == 0 && b.nev == 0, "unpack: a bad event: refused, empty");
    ok(cr_loop_unpack(buf, 10, &b) == 0, "unpack: short: refused");
    /* a queued slot switches at the cycle's end */
    memset(&b, 0, sizeof b);
    b.len = 384; b.nev = 1; b.nlayers = 1; b.ev[0].t = 0; b.ev[0].dur = 96; b.ev[0].root = 60; b.ev[0].vel = 90;
    b.ev[0].qx = CR_Q_MAJ << 4;
    step(2000 - (NOW % 1000));
    cr_loop_queue(&L, &C, &b);
    ok(L.next_on == 1 && L.d.nev == 2, "queue while playing: waits for the cycle's end");
    mark = nlog;
    step(2500);
    ok(L.next_on == 0 && L.d.len == 384 && ons_of(60, mark, t, 8) >= 1, "the next cycle is the queued slot's (C plays)");
    cr_loop_play(&L, &C);
    cr_loop_set(&L, &C, &a);
    ok(L.state == CRL_STOPPED && L.d.nev == 2 && !L.dirty, "set when stopped: loaded at once, not dirty");
    cr_loop_set(&L, &C, 0);
    ok(L.state == CRL_EMPTY, "set(0): empty");
    step(1500);
    ok(sounding() == 0 && dup_on == 0 && stray_off == 0, "slots: no stuck, double or stray note");
}

/* a queued switch is taken at the cycle's end (loads + 1) or cancelled by an edit of the loop still playing, a
 * stop (loads unchanged): the UI keeps its slot number right by it (cr_ui.c cu_loop_frame) */
static void s_switch(void)
{
    static crl_data_t b;
    uint8_t n0;
    reset();
    cr_loop_rec(&L, &C);
    chord(62, CR_MOD_MAJ, 300);
    step(700);
    cr_loop_rec(&L, &C);                             /* a 1 s loop of D, playing */
    cr_loop_rec(&L, &C);
    step(200);
    chord(65, CR_MOD_MIN, 200);
    cr_loop_rec(&L, &C);                             /* + an overdub of Fm: 2 layers */
    ok(L.dirty && L.d.nlayers == 2 && L.state == CRL_PLAYING, "a take and an overdub: dirty (the UI saves it on leaving)");
    memset(&b, 0, sizeof b);
    b.len = 384; b.nev = 1; b.nlayers = 1; b.ev[0].t = 0; b.ev[0].dur = 96; b.ev[0].root = 60; b.ev[0].vel = 90;
    b.ev[0].qx = CR_Q_MAJ << 4;
    n0 = L.loads;
    cr_loop_queue(&L, &C, &b);
    step(100);
    ok(cr_loop_rec(&L, &C) == CRL_DID_OD_ARM && !L.next_on, "queued, then an overdub armed: the switch cancelled");
    step(1500);
    ok(L.loads == n0 && L.d.nlayers == 2 && L.d.nev == 2 && L.d.len != 384, "cancelled: the loop stays (no load)");
    cr_loop_rec(&L, &C);                             /* (the armed overdub: cancelled) */
    cr_loop_queue(&L, &C, &b);
    ok(cr_loop_undo(&L, &C) == CRL_DID_UNDO && !L.next_on && L.d.nlayers == 1 && L.loads == n0,
       "queued, then an undo: the switch cancelled, the undo done");
    cr_loop_queue(&L, &C, &b);
    cr_loop_play(&L, &C);                            /* LOOP tap before the cycle's end: stop */
    ok(!L.next_on && L.state == CRL_STOPPED && L.loads == n0 && L.d.nev == 1, "queued, then a stop: cancelled, kept");
    cr_loop_play(&L, &C);
    cr_loop_queue(&L, &C, &b);
    step(1100);
    ok(!L.next_on && L.loads == (uint8_t)(n0 + 1u) && L.d.len == 384 && !L.dirty && L.state == CRL_PLAYING,
       "queued, the cycle's end: taken (loads + 1), not dirty, playing on");
    cr_loop_set(&L, &C, 0);
    ok(L.loads == (uint8_t)(n0 + 2u) && L.state == CRL_EMPTY, "set: a load (loads + 1)");
    step(500);
    ok(sounding() == 0 && dup_on == 0 && stray_off == 0, "switches: no stuck, double or stray note");
}

static void s_limits(void)
{
    int i;
    reset();
    L.sync = 0;
    cr_loop_rec(&L, &C);
    for (i = 0; i < 600 && (L.cap == CRL_CAP_ARMED || L.cap == CRL_CAP_REC); i++) {
        cr_key(&C, 62, 100, 1);
        step(3);
        cr_key(&C, 62, 100, 0);
        step(2);
    }
    ok(L.state == CRL_PLAYING && L.d.nev == CRL_MAX_EV, "the event cap ends the take by itself (512 events)");
    step(3000);
    cr_loop_play(&L, &C);
    step(500);
    ok(sounding() == 0, "a full loop played and stopped: silence");
    /* determinism: the same take twice gives the same events */
    {
        static crl_data_t first;
        int run;
        for (run = 0; run < 2; run++) {
            reset();
            cr_loop_rec(&L, &C);
            chord(62, CR_MOD_MAJ, 333);
            step(171);
            chord(66, CR_MOD_SUS, 222);
            step(400);
            cr_loop_rec(&L, &C);
            if (!run) first = L.d;
        }
        ok(memcmp(&first, &L.d, sizeof first) == 0, "determinism: the same take, the same loop");
    }
    /* loop level scales the velocity; 0 mutes */
    L.level = 50;
    cr_loop_play(&L, &C);
    nlog = 0;
    cr_loop_play(&L, &C);
    step(5);
    ok(nlog > 0 && LOG[0].vel == 50, "loop level 50%: velocity 100 plays at 50");
    cr_loop_play(&L, &C);
    L.level = 0;
    step(500);
    nlog = 0;
    cr_loop_play(&L, &C);
    step(1500);
    ok(nlog == 0, "loop level 0: silent");
}

/* the record modes (docs/LOOPER-MODES.md) */
static void until_pos(uint32_t p)                    /* step until the cycle position reaches p (ticks) */
{
    int i;
    for (i = 0; i < 10000 && cr_loop_pos(&L) != p; i++) step(1);
}
static int find_ev(int root, int live)               /* the first event of root (live: not hidden), -1 */
{
    int i;
    for (i = 0; i < L.d.nev; i++)
        if (L.d.ev[i].root == root && (!live || !(L.d.ev[i].layer & CRL_HID))) return i;
    return -1;
}
static void take_dga(void)                           /* Free: D (a beat), G (two beats), Ab (a beat): one bar
                                                      * (no chord holds another's root: ons_of counts roots) */
{
    cr_loop_rec(&L, &C);
    chord(62, CR_MOD_MAJ, 500);
    chord(67, CR_MOD_MAJ, 1000);
    chord(68, CR_MOD_MAJ, 500);
    cr_loop_rec(&L, &C);
}
static void s_overwrite(void)
{
    char name[128];
    reset();
    L.mode = CRL_MODE_OVERWRITE;
    take_dga();
    ok(L.state == CRL_PLAYING && L.d.nev == 3 && L.d.len == 384, "overwrite: a 1-bar take of 3 events plays");
    step(700);
    ok(cr_loop_rec(&L, &C) == CRL_DID_ARM && L.state == CRL_STOPPED && L.d.nev == 3 && L.ow,
       "overwrite: REC while playing stops and arms; the loop is still there");
    step(300);
    ok(sounding() == 0, "overwrite: armed, the loop silent");
    ok(cr_loop_rec(&L, &C) == CRL_DID_CANCEL && L.d.nev == 3 && L.d.len == 384 && !L.ow && L.state == CRL_STOPPED,
       "overwrite: REC again before the first chord cancels, the loop intact");
    ok(cr_loop_rec(&L, &C) == CRL_DID_ARM && L.d.nev == 3, "overwrite: armed again (stopped, a loop)");
    cr_mod(&C, CR_MOD_MIN, 1); cr_key(&C, 64, 100, 1);
    step(1);
    ok(L.cap == CRL_CAP_REC && L.d.nev == 0 && L.d.len == 0 && L.dirty, "overwrite: the first chord starts the take: the loop goes now");
    step(500);
    cr_key(&C, 64, 100, 0); cr_mod(&C, CR_MOD_MIN, 0);
    step(500);
    ok(cr_loop_rec(&L, &C) == CRL_DID_COMMIT && L.d.nev == 1 && L.d.ev[0].root == 64 && L.d.nlayers == 1 &&
       L.state == CRL_PLAYING, "overwrite: the new take replaced it (Em alone, 1 layer, playing)");
    /* synced: the count-in keeps the loop; its end clears it */
    L.sync = 1;
    L.count_in = 1;
    ok(cr_loop_rec(&L, &C) == CRL_DID_COUNTIN && L.d.nev == 1, "overwrite, 1 bar synced: REC starts the count-in, the loop kept");
    step(1000);
    ok(L.cap == CRL_CAP_COUNTIN && L.d.nev == 1, "overwrite: halfway through the count-in, still kept");
    step(1010);
    snprintf(name, sizeof name, "overwrite: the count-in's end starts the take, the loop cleared (cap %u nev %u)",
             (unsigned)L.cap, (unsigned)L.d.nev);
    ok(L.cap == CRL_CAP_REC && L.d.nev == 0, name);
    chord(62, CR_MOD_MAJ, 400);
    step(2000);
    ok(L.cap == CRL_CAP_NONE && L.state == CRL_PLAYING && L.d.len == 384 && L.d.nev == 1 && L.d.ev[0].root == 62,
       "overwrite: the synced take committed by itself (D, 1 bar)");
    /* Advance is Overwrite in the engine (the UI loads the next slot first): the slot math */
    ok(CRL_NEXT_SLOT(2u) == 3u && CRL_NEXT_SLOT(9u) == 0u && CRL_NEXT_SLOT(0u) == 1u, "advance: slot 3 -> 4, 10 wraps to 1");
    L.mode = CRL_MODE_ADVANCE;
    ok(cr_loop_rec(&L, &C) == CRL_DID_COUNTIN && L.ow, "advance: over a loop (the slot jumped to), as Overwrite");
    cr_loop_play(&L, &C);
    ok(L.cap == CRL_CAP_NONE && L.d.nev == 1 && !L.ow, "advance: LOOP during the count-in cancels, the loop kept");
    cr_loop_panic(&L);
    L.mode = CRL_MODE_OVERWRITE;
    L.sync = 0;
    cr_loop_rec(&L, &C);
    cr_loop_panic(&L);
    ok(L.d.nev == 1 && L.state == CRL_STOPPED, "overwrite: a panic while armed keeps the loop");
    step(200);
    ok(sounding() == 0 && dup_on == 0 && stray_off == 0, "overwrite: no stuck, double or stray note");
}
static void s_replace(void)
{
    char name[160];
    static uint8_t buf[CRL_REC_MAX];
    static crl_data_t back;
    uint32_t t[8], n;
    int g, a, mark, gc;
    reset();
    L.mode = CRL_MODE_REPLACE;
    take_dga();
    g = find_ev(67, 1);
    a = find_ev(68, 1);
    snprintf(name, sizeof name, "replace: the take D G Ab (G at %u for %u, A at %u)", g >= 0 ? (unsigned)L.d.ev[g].t : 0u,
             g >= 0 ? (unsigned)L.d.ev[g].dur : 0u, a >= 0 ? (unsigned)L.d.ev[a].t : 0u);
    ok(L.d.nev == 3 && g == 1 && a == 2 && L.d.ev[g].t < 144 && L.d.ev[g].t + L.d.ev[g].dur > 160 &&
       L.d.ev[a].t > 150 && L.d.ev[a].t < 300, name);
    ok(cr_loop_rec(&L, &C) == CRL_DID_REP_ARM && L.cap == CRL_CAP_OD_ARMED && L.rep, "replace: REC while playing arms it");
    until_pos(144);
    cr_mod(&C, CR_MOD_MAJ, 1); cr_key(&C, 65, 100, 1);  /* F held from tick 144 (G sounds there) */
    step(1);
    ok(L.cap == CRL_CAP_OD && L.rep_n == 1 && (L.d.ev[g].layer & CRL_HID), "replace: the chord opened the layer, G (sounding) hidden");
    gc = find_ev(67, 1);
    ok(gc >= 3 && L.d.ev[gc].t == L.d.ev[g].t && L.d.ev[gc].t + L.d.ev[gc].dur == 144 && L.d.ev[gc].layer == 1,
       "replace: G cut at 144 (a copy in the new layer, ending there)");
    until_pos(310);
    ok(L.d.ev[a].layer == (CRL_HID | 1u), "replace: A, starting inside the held span, hidden by layer 2");
    ok(!(L.d.ev[0].layer & CRL_HID), "replace: D, before the span, untouched");
    cr_key(&C, 65, 100, 0); cr_mod(&C, CR_MOD_MAJ, 0);
    step(1);
    ok(L.rep_n == 0, "replace: released, nothing more is erased");
    ok(cr_loop_rec(&L, &C) == CRL_DID_OD_END && L.d.nlayers == 2 && !L.rep, "replace: REC ends it: 2 layers");
    n = cr_loop_pack(&L.d, buf, sizeof buf);
    ok(n == CRL_REC_HDR + 2u * 2u + 7u * 3u && cr_loop_unpack(buf, n, &back) && back.nev == 3,
       "replace: the record holds the live events only (D, G cut, F)");
    until_pos(380);
    mark = nlog;
    step(2000);
    snprintf(name, sizeof name, "replace: the next cycle plays D, G (cut), F; not Ab (%d %d %d %d)", ons_of(62, mark, t, 8),
             ons_of(67, mark, t, 8), ons_of(65, mark, t, 8), ons_of(68, mark, t, 8));
    ok(ons_of(62, mark, t, 8) == 1 && ons_of(65, mark, t, 8) == 1 && ons_of(68, mark, t, 8) == 0 &&
       ons_of(67, mark, t, 8) == 1, name);
    ok(cr_loop_undo(&L, &C) == CRL_DID_UNDO && L.d.nlayers == 1 && L.d.nev == 3 && !(L.d.ev[g].layer & CRL_HID) &&
       !(L.d.ev[a].layer & CRL_HID) && L.d.ev[g].t + L.d.ev[g].dur > 160, "undo: the layer goes, G (whole) and Ab come back");
    until_pos(380);
    mark = nlog;
    step(2000);
    ok(ons_of(68, mark, t, 8) == 1 && ons_of(65, mark, t, 8) == 0, "undo: Ab plays again, F no longer");
    /* undo of a replace in progress restores too */
    cr_loop_rec(&L, &C);
    until_pos(10);
    cr_mod(&C, CR_MOD_MAJ, 1); cr_key(&C, 65, 100, 1);
    until_pos(200);
    ok((L.d.ev[g].layer & CRL_HID) && (L.d.ev[0].layer & CRL_HID), "replace again: D and G hidden while C is held");
    ok(cr_loop_undo(&L, &C) == CRL_DID_UNDO && L.d.nev == 3 && !(L.d.ev[0].layer & CRL_HID) && !(L.d.ev[g].layer & CRL_HID),
       "undo while replacing: the layer in progress goes, D and G back");
    cr_key(&C, 65, 100, 0); cr_mod(&C, CR_MOD_MAJ, 0);
    cr_loop_play(&L, &C);
    step(300);
    ok(sounding() == 0 && dup_on == 0 && stray_off == 0, "replace: no stuck, double or stray note");
}
static void step_chord(int note, cr_mod_t m)      /* a chord pressed and released, then every key up */
{
    chord(note, m, 60);
    step(5);
    cr_loop_step(&L, CRL_STEP_KEYSUP);
}
static void s_step(void)
{
    char name[128];
    uint32_t t[8], e[8], gg[8];
    int mark;
    reset();
    L.mode = CRL_MODE_OVERWRITE;
    take_dga();                                      /* a loop there before: replaced by the steps */
    L.mode = CRL_MODE_STEP;
    L.quant = 0;                                     /* none: 1/16 steps (24 ticks) */
    ok(cr_loop_rec(&L, &C) == CRL_DID_STEP && L.cap == CRL_CAP_STEP && L.state == CRL_STOPPED && L.step == 0 &&
       L.step_g == 24, "step: REC while playing stops the loop, step entry at step 1 (1/16)");
    step(200);
    ok(sounding() == 0, "step: the loop stopped");
    step_chord(62, CR_MOD_MAJ);
    ok(L.step == 1 && L.d.nev == 4 && L.d.ev[3].t == 0 && L.d.ev[3].dur == 24, "step: D written at step 1 (one step long), the cursor on 2");
    cr_loop_step(&L, CRL_STEP_KEYSUP);
    ok(L.step == 1, "step: keys up again with nothing written: the cursor stays");
    step_chord(64, CR_MOD_MIN);
    step_chord(68, CR_MOD_MAJ);
    ok(L.step == 3 && L.d.ev[4].t == 24 && L.d.ev[5].t == 48, "step: Em at step 2, Ab at step 3");
    cr_loop_step(&L, CRL_STEP_REST);
    ok(L.step == 4, "step: OCT+ a rest, the cursor on 5");
    cr_loop_step(&L, CRL_STEP_BACK);
    ok(L.step == 3 && L.d.nev == 6, "step: OCT- back to 4 (nothing erased)");
    ok(cr_loop_rec(&L, &C) == CRL_DID_STEP_DONE && L.d.nev == 3 && L.d.nlayers == 1 && L.state == CRL_STOPPED,
       "step: REC commits the steps as the loop (the old one gone), stopped");
    snprintf(name, sizeof name, "step: Free, 4 steps used: one bar (len %u)", (unsigned)L.d.len);
    ok(L.d.len == 384, name);
    mark = nlog;
    cr_loop_play(&L, &C);
    step(1990);
    ok(ons_of(62, mark, t, 8) == 1 && ons_of(64, mark, e, 8) == 1 && ons_of(68, mark, gg, 8) == 1 &&
       near(e[0] - t[0], 125, 6) && near(gg[0] - t[0], 250, 6), "step: playback D, Em 125 ms later, Ab 250 ms (1/16s at 120)");
    cr_loop_play(&L, &C);
    step(300);
    /* nothing entered: the loop as it was; LOOP leaves and plays */
    ok(cr_loop_rec(&L, &C) == CRL_DID_STEP && cr_loop_rec(&L, &C) == CRL_DID_EMPTY_TAKE && L.d.nev == 3 && L.d.len == 384,
       "step: REC REC with nothing entered keeps the loop");
    /* synced 1 bar: the cursor wraps; two gestures at one step; LOOP leaves and plays */
    L.sync = 1;
    L.quant = 2;                                     /* 1/8: 8 steps a bar */
    cr_loop_rec(&L, &C);
    ok(L.step_g == 48 && L.step_len == 384, "step, 1 bar at 1/8: 8 steps");
    cr_mod(&C, CR_MOD_MIN, 1);
    cr_key(&C, 62, 100, 1); step(30); cr_key(&C, 62, 100, 0); step(10);
    cr_key(&C, 69, 100, 1); step(30); cr_key(&C, 69, 100, 0); step(10);
    cr_mod(&C, CR_MOD_MIN, 0);
    cr_loop_step(&L, CRL_STEP_KEYSUP);
    ok(L.d.nev == 5 && L.d.ev[3].t == 0 && L.d.ev[4].t == 0 && L.step == 1, "step: two presses before every key is up: both at step 1");
    cr_loop_step(&L, CRL_STEP_BACK);
    cr_loop_step(&L, CRL_STEP_BACK);
    ok(L.step == 7, "step, synced: back past step 1 wraps to the bar's last step");
    cr_loop_step(&L, CRL_STEP_REST);
    ok(L.step == 0, "step, synced: the cursor wraps at the length");
    ok(cr_loop_play(&L, &C) == CRL_DID_PLAY && L.state == CRL_PLAYING && L.d.nev == 2 && L.d.len == 384,
       "step: LOOP leaves step entry and plays the steps (2 events, the bar)");
    cr_loop_play(&L, &C);
    step(300);
    ok(sounding() == 0 && dup_on == 0 && stray_off == 0, "step: no stuck, double or stray note");
}

int main(void)
{
    s_overwrite();
    s_replace();
    s_step();
    s_free();
    s_sync();
    s_slots();
    s_switch();
    s_limits();
    printf("\ncr_loop: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
