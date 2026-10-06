/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments */
/* SLICE engine tests on the Mac (same sources as the firmware, through hostsim.c; run_tests.sh):
 *   build/host/slice_test LOOP DEMO_DIR
 * LOOP.hdr / LOOP.bin: a user slot image written by `fm1_sample_upload.py build` from tests/slice_loop.py's
 * WAV (two bars of drums at 96 BPM with a swing), LOOP.hits its onsets (s). The slot is put into a RAM
 * image of the flash slots (SMP_USER_XIP) and read by smp_user_scan as at boot.
 * 1. BREAK's build-time table (gen_samples.py ima_states) == the firmware decoder's states.
 * 2. AUTO: the detector (slc_scan) on BREAK finds its hits; on the user loop every onset has a slice
 *    start at most 6 ms before it and 1 ms after it, and no slice starts away from an onset.
 * 3. REV: a slice read backwards (64-sample windows from checkpoints) == the forward decode reversed.
 * 4. keys / steps -> slices (mod the count, START, ROOT, an empty slot plays BREAK); ONE / GATE / LOOP.
 * 5. demos into DEMO_DIR: both presets with their patterns, BREAK re-sequenced, the user loop sliced AUTO and MAN.
 * 6. MAN, the slices set by hand (the SLICES page; ported from hugelton/Felucca#27 by andreahaku): none set = AUTO,
 *    move / end / split / join within their limits, the decoder states, keys / bounds / REV after a commit, a slot
 *    scan clearing them, restore == save, bad tables refused, a start moved past a reverse voice, no room.
 * 7. the store (slice_store.c) on the slot image: written when asked and stopped (not while playing or on the
 *    page), read back at a scan, kept for the same sample only, junk and torn writes ignored, a long sample. */
#include <stdarg.h>
#include <stdint.h>
static uint32_t host_slots[3u * 0x14000u / 4u];          /* USR1..3, as the flash at 0xA0000 */
#define SMP_USER_XIP(k) ((const uint8_t *)host_slots + (k) * SMP_USER_SIZE)
#define main hostsim_main
#include "hostsim.c"
#undef main
#if !FELUCCA_SLICE
#error "slice_test needs the SLICE engine (FELUCCA_SLICE=1, the default)"
#endif
#define SLC_ENG 13u                                      /* SLICE's engine number (engines.c, the protocol's) */

/* 7: the store (src/slice_store.c) on the slot image: storage.c's hooks over host_slots, the UI's stubs */
static uint8_t flash_ok = 1, host_busy, host_page, host_torn;
static char host_msg[32];
static int transport_busy(void) { return host_busy; }
static int slice_page_on(void) { return host_page; }
static void ui_message(const char *s) { snprintf(host_msg, sizeof host_msg, "%s", s); }
static uint32_t host_erases;
static int st_read(uint32_t off, void *dst, uint32_t n) { (void)off; (void)dst; (void)n; return -1; }
static int st_erase(uint32_t off)
{
    if (off < SMP_USER_BASE || off + 0x1000u > SMP_USER_BASE + sizeof host_slots || (off & 0xFFFu))
        return -8;
    memset((uint8_t *)host_slots + (off - SMP_USER_BASE), 0xFF, 0x1000u);
    host_erases++;
    return 0;
}
static int st_prog(uint32_t off, const void *src, uint32_t n)
{
    uint32_t i;
    if (off < SMP_USER_BASE || off + n > SMP_USER_BASE + sizeof host_slots)
        return -8;
    if (host_torn)
        return -1;                                       /* power gone after the erase */
    for (i = 0; i < n; i++)                              /* NOR: bits only go 1 -> 0 */
        ((uint8_t *)host_slots)[off - SMP_USER_BASE + i] &= ((const uint8_t *)src)[i];
    return 0;
}
#include "../firmware/src/storage.c"
#include "../firmware/src/slice_store.c"

static int fails;
static void check(const char *what, int ok, const char *fmt, ...)
{
    printf("slice: %-58s %s", what, ok ? "ok" : "FAIL");
    if (fmt) {
        va_list ap;
        va_start(ap, fmt);
        printf("  (");
        vprintf(fmt, ap);
        printf(")");
        va_end(ap);
    }
    printf("\n");
    fails += !ok;
}

/* 1: every stored state == the decoder's state there; returns the mismatches */
static uint32_t table_check(const slc_src_t *s)
{
    slc_dec_t d;
    uint32_t pos, k = 0, a = 0, bad = 0;
    slc_dec_at(&d, 0, 0);
    for (pos = 0; pos < s->len; pos++) {
        while (k < SLC_GRID && slc_gpos(s, k) == pos)
            bad += s->grid[k++] != slc_dec_st(&d);
        while (a < s->nauto && s->apos[a] == pos)
            bad += s->ast[a++] != slc_dec_st(&d);
        slc_dec_next(s, &d);
    }
    return bad + (SLC_GRID - k) + (s->nauto - a);
}

/* 6: MAN slices are sorted, keep SLC_MIN apart (and from their end), and each state == the decoder's there */
static uint32_t man_check(const slc_src_t *s, const slc_man_t *m)
{
    slc_dec_t d;
    uint32_t pos, a, bad = 0;
    if (!m || !m->n || slc_man_end(s, m) > s->len)
        return 1;
    for (a = 1; a < m->n; a++)
        bad += m->pos[a] < m->pos[a - 1u] + SLC_MIN;
    bad += slc_man_end(s, m) < m->pos[m->n - 1u] + SLC_MIN;
    slc_dec_at(&d, 0, 0);
    for (a = 0, pos = 0; pos < s->len && a < m->n; pos++) {
        while (a < m->n && m->pos[a] == pos)
            bad += m->st[a++] != slc_dec_st(&d);
        slc_dec_next(s, &d);
    }
    return bad + (m->n - a);
}

/* 2: hits h[] (samples) against slice starts: each hit has a start in [h - early, h + late], each start
 * (but the first, at 0) is within that of a hit. Writes a list into msg. */
static int match(const slc_src_t *s, const uint32_t *h, uint32_t nh, uint32_t early, uint32_t late, char *msg,
                 size_t mn, int32_t *worst)
{
    uint32_t i, j, miss = 0, extra = 0;
    int32_t w = 0;
    for (i = 0; i < nh; i++) {
        int found = 0;
        for (j = 0; j < s->nauto; j++)
            if (s->apos[j] + early >= h[i] && s->apos[j] <= h[i] + late) {
                int32_t e = (int32_t)h[i] - (int32_t)s->apos[j];
                found = 1;
                w = abs(e) > abs(w) ? e : w;
            }
        miss += !found;
    }
    for (j = 1; j < s->nauto; j++) {
        int near = 0;
        for (i = 0; i < nh; i++)
            near |= s->apos[j] + early >= h[i] && s->apos[j] <= h[i] + late;
        extra += !near;
    }
    snprintf(msg, mn, "%u onsets, %u slices, %u missed, %u extra, worst %+.1f ms", nh, s->nauto, miss, extra,
             w * 1000.0 / (s->rate * 44100.0 / 65536.0));
    *worst = w;
    return !miss && !extra && (nh == 0 || h[0] > early || s->apos[0] == 0);
}

/* 3: slice j of DIV div read backwards == forwards reversed */
static uint32_t rev_check(uint32_t src, uint32_t div, uint32_t j)
{
    const slc_src_t *s = slc_get(src);
    static int32_t fw[1 << 18];
    static int16_t rb[SLC_RB];
    voice_t v;
    slc_dec_t d;
    uint32_t a, b, st, i, n, bad = 0;
    int32_t x;
    slc_bounds(s, div, j, &a, &b, &st);
    slc_dec_at(&d, a, st);
    for (n = 0; d.pos < b && n < (1u << 18); n++)
        fw[n] = slc_dec_next(s, &d);
    memset(&v, 0, sizeof v);
    v.ph[0] = b;
    v.ph[2] = a;
    v.s[0] = 0x7FFFFFFF;
    v.s[4] = (int32_t)(src | (st >> 24) << 2 | 1u << 6 | j << 8 | div << 16);
    for (i = 0; i < n; i++)
        bad += !slc_rev(s, &v, rb, 0, &x) || x != fw[n - 1u - i];
    bad += slc_rev(s, &v, rb, 0, &x) != 0;               /* and then it ends */
    return bad;
}

static uint32_t slice_of(const voice_t *v) { return ((uint32_t)v->s[4] >> 8) & 63u; }
static voice_t *voice_of(track_t *t, uint32_t note)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active && t->v[i].note == note)
            return &t->v[i];
    return 0;
}
static void blocks(uint32_t n)
{
    int32_t o[2 * CTL];
    while (n--)
        mix_block(o, CTL);
}

/* 5: a pattern on track 1 (SLICE preset pi, then edits), the sequencer for `bars` bars, a tail */
typedef struct {
    const char *file;
    uint32_t preset, bpm, nsteps, bars;
    const uint8_t *notes;
    int16_t src, div;                            /* -1 = the preset's */
} demo_t;
static int demo(const char *dir, const demo_t *dm)
{
    char path[512];
    FILE *w;
    track_t *t = &trk[0];
    uint32_t f, i, frames, peak = 0, bar = 0;
    snprintf(path, sizeof path, "%s/%s", dir, dm->file);
    if (!(w = fopen(path, "wb")))
        return 1;
    host_tracks_init();
    song.g[G_BPM] = (int16_t)dm->bpm;
    host_preset(t, SLC_ENG, dm->preset);
    if (dm->src >= 0)
        t->p[P_E0] = dm->src;
    if (dm->div >= 0)
        t->p[P_E1] = dm->div;
    for (i = 0; i < dm->nsteps; i++) {
        uint8_t n = dm->notes[i];
        put_step(t, i, n ? 1u : 0u, &n, n ? ST_NOTE : ST_REST, i % 4u == 0u ? SF_ACCENT : 0u);
    }
    t->p[P_SLEN] = (int16_t)dm->nsteps;
    bar = (uint32_t)(4.0 * 60.0 / dm->bpm * FS);
    frames = dm->bars * bar + FS;
    wav_hdr(w, frames);
    transport_req = 1;
    for (f = 0; f < frames; f += CTL) {
        int32_t o[2 * CTL];
        if (f >= dm->bars * bar && f < dm->bars * bar + CTL)
            transport_req = 2;
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++) {
            uint32_t a = (uint32_t)abs(o[2 * i]);
            peak = a > peak ? a : peak;
            wav_put(w, o[2 * i], o[2 * i + 1]);
        }
    }
    fclose(w);
    printf("slice: demo %-30s %u bars at %u BPM, peak %u\n", dm->file, dm->bars, dm->bpm, peak);
    return peak < 2000u || peak > 32767u;
}

static int in_child(int (*fn)(const char *, const demo_t *), const char *dir, const demo_t *dm)
{
    pid_t pid;
    int st = 0;
    fflush(stdout);
    if (!(pid = fork())) {
        int rc = fn(dir, dm);
        fflush(stdout);
        _exit(rc);
    }
    waitpid(pid, &st, 0);
    return !WIFEXITED(st) || WEXITSTATUS(st);
}

static long load(const char *path, void *dst, long max)
{
    FILE *f = fopen(path, "rb");
    long n;
    if (!f)
        return -1;
    n = (long)fread(dst, 1, (size_t)max, f);
    fclose(f);
    return n;
}

int main(int argc, char **argv)
{
    static const uint8_t CHOP[16] = {60, 61, 62, 67, 64, 65, 60, 69, 68, 70, 62, 67, 72, 72, 74, 64};   /* ui.c 9..11 */
    static const uint8_t STUTTER[16] = {60, 60, 61, 61, 62, 0, 63, 63, 64, 65, 65, 0, 66, 66, 66, 67};
    static const uint8_t SLICES[16] = {60, 61, 62, 63, 64, 65, 66, 67, 68, 69, 70, 71, 72, 73, 74, 75};
    static const uint8_t RESEQ[32] = {60, 0, 62, 60, 64, 0, 67, 62, 60, 69, 70, 0, 72, 0, 64, 72,   /* 2 bars */
                                      60, 61, 60, 61, 64, 0, 66, 67, 68, 64, 70, 71, 72, 72, 72, 72};
    static const uint8_t USR[32] = {60, 0, 61, 0, 62, 0, 63, 64, 65, 0, 66, 0, 67, 0, 60, 61,
                                    60, 0, 60, 63, 62, 0, 68, 0, 65, 66, 67, 0, 62, 70, 71, 72};
    const char *loop = argc > 1 ? argv[1] : "build/host/slice_loop";
    const char *dir = argc > 2 ? argv[2] : "build/slice_demo";
    char path[512], msg[160];
    uint32_t hits[64], nh = 0, i, bad;
    int32_t worst;
    long n;

    /* 1 */
    check("BREAK: build-time table == decoder states", !(bad = table_check(&SLC_BREAK)), "%u of %u differ", bad,
          SLC_GRID + SLC_BREAK.nauto);

    /* 2: the detector on BREAK (hits = its build-time AUTO table) */
    {
        static slc_src_t tmp;
        tmp = SLC_BREAK;
        tmp.len = 0;
        slc_scan(&tmp, SLC_BREAK.len);
        tmp.len = SLC_BREAK.len;
        bad = memcmp(tmp.grid, SLC_BREAK.grid, sizeof tmp.grid) != 0;
        check("BREAK: slc_scan grid == build-time grid", !bad, 0);
        check("BREAK: slc_scan finds the hits (-6 .. +1 ms)",
              match(&tmp, SLC_BREAK.apos, SLC_BREAK.nauto, 132, 22, msg, sizeof msg, &worst), "%s", msg);
    }

    /* 2: the user loop, through the slot image and smp_user_scan */
    snprintf(path, sizeof path, "%s.hdr", loop);
    n = load(path, host_slots, SMP_USER_DATA);
    snprintf(path, sizeof path, "%s.bin", loop);
    n = n == (long)sizeof(smp_user_hdr_t) ? load(path, (uint8_t *)host_slots + SMP_USER_DATA, SMP_USER_SIZE - SMP_USER_DATA) : -1;
    {
        FILE *f;
        double s;
        snprintf(path, sizeof path, "%s.hits", loop);
        if ((f = fopen(path, "r"))) {
            while (nh < 64u && fscanf(f, "%lf", &s) == 1)
                hits[nh++] = (uint32_t)(s * 22050.0 + 0.5);
            fclose(f);
        }
    }
    if (n <= 0 || !nh) {
        printf("slice: no user loop (%s.hdr / .bin / .hits: tests/slice_loop.py + fm1_sample_upload.py build)\n", loop);
        return 1;
    }
    if ((uintptr_t)host_slots < (uintptr_t)SMP_DATA || (uintptr_t)host_slots - (uintptr_t)SMP_DATA > 0xF0000000u) {
        printf("slice: the slot image must lie above SMP_DATA within 4 GiB (32-bit offsets)\n");
        return 1;
    }
    smp_user_scan(0);
    check("USR1: valid after smp_user_scan, slice table built", usr_nz[0] && slc_usr[0].len && slc_get(1) != 0,
          "%u zones, %u samples", usr_nz[0], slc_usr[0].len);
    check("USR1: table == decoder states", !(bad = table_check(&slc_usr[0])), "%u differ", bad);
    check("USR1: AUTO slices at the onsets (-6 .. +1 ms)", match(&slc_usr[0], hits, nh, 132, 22, msg, sizeof msg, &worst),
          "%s", msg);
    printf("slice: USR1 AUTO starts (ms):");
    for (i = 0; i < slc_usr[0].nauto; i++)
        printf(" %.1f", slc_usr[0].apos[i] * 1000.0 / 22050.0);
    printf("\nslice: onsets (ms):          ");
    for (i = 0; i < nh; i++)
        printf(" %.1f", hits[i] * 1000.0 / 22050.0);
    printf("\n");
    check("USR2 / USR3: empty, no material", !slc_get(2) && !slc_get(3), 0);

    /* 3 */
    for (bad = 0, i = 0; i < 16u; i++)
        bad += rev_check(0, 2, i);
    for (i = 0; i < 4u; i++)
        bad += rev_check(0, 0, i);
    for (i = 0; i < SLC_BREAK.nauto; i++)
        bad += rev_check(0, SLC_DIV_AUTO, i);
    for (i = 0; i < 4u; i++)
        bad += rev_check(1, 0, i);
    for (i = 0; i < slc_usr[0].nauto; i++)
        bad += rev_check(1, SLC_DIV_AUTO, i);
    check("REV: backwards == forwards reversed (BREAK, USR1; 4 / 16 / AUTO)", !bad, "%u samples differ", bad);

    /* 4: keys / steps -> slices */
    {
        track_t *t = &trk[0];
        voice_t *v;
        uint32_t a, b, st, ok = 1;
        host_tracks_init();
        host_preset(t, SLC_ENG, 0);                              /* BREAK 16 */
        trk_note_on(t, 65, 100);
        v = voice_of(t, 65);
        slc_bounds(&SLC_BREAK, 2, 5, &a, &b, &st);
        ok &= v && slice_of(v) == 5u && v->ph[0] == a && v->ph[2] == b;
        trk_note_on(t, 59, 100);
        ok &= (v = voice_of(t, 59)) && slice_of(v) == 15u;
        trk_note_on(t, 76, 100);
        ok &= (v = voice_of(t, 76)) && slice_of(v) == 0u;
        t->p[P_E2] = 3;                                    /* START */
        trk_note_on(t, 60, 100);
        ok &= (v = voice_of(t, 60)) && slice_of(v) == 3u;
        t->p[P_E2] = 0;
        t->p[P_ROOT] = 2;                                  /* ROOT D: D4 is slice 0 */
        trk_note_on(t, 62, 100);
        ok &= (v = voice_of(t, 62)) && slice_of(v) == 0u;
        check("keys: note - C4 - ROOT + START mod 16", ok, 0);
        ok = 1;
        t->p[P_ROOT] = 0;
        t->p[P_E1] = SLC_DIV_AUTO;
        trk_note_on(t, 72, 100);                           /* 12 mod 10 hits */
        ok &= (v = voice_of(t, 72)) && slice_of(v) == 12u % SLC_BREAK.nauto && v->ph[0] == SLC_BREAK.apos[12u % SLC_BREAK.nauto];
        t->p[P_E0] = 2;                                    /* USR2: empty -> BREAK */
        trk_note_on(t, 61, 100);
        ok &= (v = voice_of(t, 61)) && ((uint32_t)v->s[4] & 3u) == 0u && v->ph[0] == SLC_BREAK.apos[1];
        t->p[P_E0] = 1;                                    /* USR1 */
        trk_note_on(t, 63, 100);
        ok &= (v = voice_of(t, 63)) && ((uint32_t)v->s[4] & 3u) == 1u && v->ph[0] == slc_usr[0].apos[3];
        check("keys: AUTO count, an empty slot plays BREAK, USR1", ok, 0);
        ok = 1;
        song.octave = 0;
        t->p[P_E0] = 0;
        ok &= kb_map(t, 0) == 60u && kb_map(t, 9) == 69u;
        check("keys: the lowest key is slice 0 (no scale)", ok, 0);
    }
    {   /* ONE / GATE / LOOP: a 125 ms slice, the note released after 10 ms */
        static const char *const MN[3] = {"ONE", "GATE", "LOOP"};
        uint32_t m;
        for (m = 0; m < 3u; m++) {
            track_t *t = &trk[0];
            voice_t *v;
            uint32_t held, after, k;
            host_tracks_init();
            host_preset(t, SLC_ENG, 0);
            t->p[P_E4] = (int16_t)m;
            t->p[P_REL] = 10;
            trk_note_on(t, 60, 100);
            v = voice_of(t, 60);
            if (m == SLC_LOOP) {
                blocks(FS / CTL);                          /* 1 s held: still looping */
                held = v && v->active;
            } else {
                held = 1;
            }
            blocks(FS / 100u / CTL);
            trk_note_off(t, 60);
            blocks(FS / 20u / CTL);                        /* 50 ms after the note-off */
            after = v && v->active;
            for (k = 0; k < FS / 4u / CTL && v && v->active; k++)
                blocks(1);
            snprintf(msg, sizeof msg, "MODE %s: %s", MN[m], m == SLC_ONE ? "plays on after the note-off, ends at the slice end" :
                     m == SLC_GATE ? "stops at the note-off" : "loops while held, ends after the note-off");
            check(msg, held && (m == SLC_ONE ? after : !after) && !(v && v->active), 0);
        }
    }

    /* 6: MAN, the slices set by hand (the SLICES page; from hugelton/Felucca#27 by andreahaku) */
    {
        const slc_src_t *s = slc_get(1);
        slc_man_t *m;
        track_t *t = &trk[0];
        voice_t *v;
        uint32_t a, b, st, ok = 1, j, p0;
        check("engine 13 is SLICE (the protocol's number), two factory presets",
              ENGINES[SLC_ENG] == &ENG_SLICE && ENG_SLICE.npresets == 2u && str_eq(ENG_SLICE.presets[0].name, "CHOP") &&
              str_eq(ENG_SLICE.presets[1].name, "STUTTER") && ENG_SLICE.presets[0].pat == 9u && ENG_SLICE.presets[1].pat == 10u, 0);
        slc_bounds(s, SLC_DIV_MAN, 3, &a, &b, &st);
        ok &= slc_count(s, SLC_DIV_MAN) == s->nauto && a == s->apos[3] && b == s->apos[4] && st == s->ast[3];
        ok &= slc_count(&SLC_BREAK, SLC_DIV_MAN) == SLC_BREAK.nauto && !slc_man_begin(SMP_USER_SLOTS);   /* BREAK: AUTO */
        check("MAN: none set = the AUTO slices (BREAK always)", ok, 0);

        ok = 1;
        m = slc_man_begin(0);
        ok &= m->n == s->nauto && !man_check(s, m);
        p0 = m->pos[3];
        ok &= slc_man_move(s, m, 3, 1000) == p0 + 1000u;
        ok &= slc_man_move(s, m, 3, -1000) == p0;
        ok &= slc_man_move(s, m, 3, -1000000) == m->pos[2] + SLC_MIN;
        ok &= slc_man_move(s, m, 3, 1000000) == m->pos[4] - SLC_MIN;
        ok &= slc_man_move(s, m, m->n - 1u, 1000000) == s->len - SLC_MIN;
        slc_man_move(s, m, 3, -1000000);                                     /* (room again) */
        slc_man_move(s, m, m->n - 1u, -1000000);
        ok &= slc_man_move(s, m, 0, 500) == 500u && slc_man_move(s, m, 0, -1000) == 0u;   /* slice 0: head trim */
        ok &= slc_man_move_end(s, m, -3000) == s->len - 3000u && slc_man_end(s, m) == s->len - 3000u;
        ok &= slc_man_move(s, m, m->n - 1u, 1000000) == s->len - 3000u - SLC_MIN;        /* within the end */
        ok &= slc_man_move_end(s, m, -1000000) == m->pos[m->n - 1u] + SLC_MIN;
        ok &= slc_man_move_end(s, m, 1000000) == s->len;
        slc_bounds(s, SLC_DIV_MAN, 3, &a, &b, &st);
        ok &= slc_count(s, SLC_DIV_MAN) == s->nauto && a == s->apos[3];
        check("MAN: move a start / the end, SLC_MIN, head and tail trim; unused until commit", ok && !man_check(s, m), 0);

        ok = 1;
        j = m->n;
        ok &= slc_man_split(s, m, 0) == 1u && m->n == j + 1u && m->pos[1] == m->pos[2] / 2u;
        ok &= slc_man_join(m, 1) == 0u && m->n == j;
        ok &= slc_man_join(m, 0) == 0u && m->n == j;
        for (i = 0; m->n < SLC_AUTO && i < 1000u; i++)
            slc_man_split(s, m, i % m->n);
        ok &= m->n == SLC_AUTO && slc_man_split(s, m, 0) == 0u && m->n == SLC_AUTO;
        check("MAN: split at the middle, join to the one before, at most 32", ok && !man_check(s, m), "%u slices", m->n);

        ok = 1;
        slc_man_commit(0);
        ok &= slc_count(s, SLC_DIV_MAN) == SLC_AUTO;
        slc_bounds(s, SLC_DIV_MAN, 5, &a, &b, &st);
        ok &= a == m->pos[5] && b == m->pos[6] && st == m->st[5];
        ok &= slc_count(s, SLC_DIV_AUTO) == s->nauto;           /* AUTO keeps its own */
        host_tracks_init();
        host_preset(t, SLC_ENG, 0);
        t->p[P_E0] = 1;
        t->p[P_E1] = SLC_DIV_MAN;
        trk_note_on(t, 65, 100);
        ok &= (v = voice_of(t, 65)) && slice_of(v) == 5u && v->ph[0] == m->pos[5] && v->ph[2] == m->pos[6];
        trk_note_on(t, 60 + 33, 100);                          /* 33 mod 32 */
        ok &= (v = voice_of(t, 93)) && slice_of(v) == 1u;
        for (bad = 0, i = 0; i < SLC_AUTO; i++)
            bad += rev_check(1, SLC_DIV_MAN, i);
        check("MAN: in use after commit: keys, bounds, REV", ok && !bad, "%u samples differ", bad);

        ok = 1;
        m = slc_man_begin(0);                                  /* the copy of the table in use */
        ok &= m->n == SLC_AUTO && slc_man_join(m, 31) == 30u;
        slc_man_commit(0);
        ok &= slc_count(s, SLC_DIV_MAN) == SLC_AUTO - 1u && !man_check(s, slc_man_of(s));
        smp_user_scan(0);                                      /* the slot is read again: MAN is gone */
        ok &= slc_count(slc_get(1), SLC_DIV_MAN) == slc_get(1)->nauto && !slc_man_of(slc_get(1));
        ok &= !slc_man_begin(1);                               /* USR2 is empty: nothing to edit */
        check("MAN: edits stack, a slot scan clears them, an empty slot has none", ok, 0);

        {   /* what the store keeps and restores: the starts and the end (the states are decoded again) */
            static uint32_t pos[SLC_AUTO], bad_pos[SLC_AUTO];
            slc_man_t keep;
            uint32_t n, end, k;
            ok = 1;
            s = slc_get(1);
            m = slc_man_begin(0);                          /* a table with every kind of edit */
            slc_man_move(s, m, 0, 300);
            slc_man_split(s, m, 2);
            slc_man_move(s, m, 5, -150);
            slc_man_move_end(s, m, -2000);
            slc_man_commit(0);
            keep = *slc_man_of(s);
            n = keep.n;
            end = keep.end;
            memcpy(pos, keep.pos, sizeof pos);
            smp_user_scan(0);                              /* as at boot: the scan clears them */
            s = slc_get(1);
            ok &= !slc_man_of(s);
            ok &= slc_man_restore(0, n, end, pos) == 0;
            ok &= slc_man_of(s) && slc_man_of(s)->n == keep.n && slc_man_of(s)->end == keep.end &&
                  !memcmp(slc_man_of(s)->pos, keep.pos, n * 4u) && !memcmp(slc_man_of(s)->st, keep.st, n * 4u);
            check("MAN restore: restored == saved (starts, end, states)", ok && !man_check(s, slc_man_of(s)), 0);

            ok = 1;
            memcpy(bad_pos, pos, sizeof bad_pos);
            bad_pos[3] = bad_pos[2] + SLC_MIN - 1u;        /* too close */
            ok &= slc_man_restore(0, n, end, bad_pos) < 0;
            memcpy(bad_pos, pos, sizeof bad_pos);
            k = bad_pos[3], bad_pos[3] = bad_pos[4], bad_pos[4] = k;   /* out of order */
            ok &= slc_man_restore(0, n, end, bad_pos) < 0;
            memcpy(bad_pos, pos, sizeof bad_pos);
            bad_pos[n - 1u] = 0xFFFFFFF0u;                 /* junk that wraps */
            ok &= slc_man_restore(0, n, end, bad_pos) < 0;
            ok &= slc_man_restore(0, n, s->len + 1u, pos) < 0;           /* end past the material */
            ok &= slc_man_restore(0, n, pos[n - 1u] + SLC_MIN - 1u, pos) < 0;   /* end over the last start */
            ok &= slc_man_restore(0, SLC_AUTO + 1u, end, pos) < 0 && slc_man_restore(0, 0, end, pos) < 0;
            ok &= slc_man_restore(1, n, end, pos) < 0 && slc_man_restore(3, n, end, pos) < 0;   /* USR2 empty; none */
            ok &= slc_man_of(s)->n == keep.n && !memcmp(slc_man_of(s)->pos, keep.pos, n * 4u);   /* untouched */
            check("MAN restore: bad tables are refused and change nothing", ok, 0);
        }

        {   /* edits while a slice plays backwards; tables with no room to move */
            static slc_src_t tiny, keep_src;
            slc_man_t t2;
            uint32_t pa, k;
            ok = 1;
            s = slc_get(1);
            host_tracks_init();
            host_preset(t, SLC_ENG, 0);
            t->p[P_E0] = 1;
            t->p[P_E1] = SLC_DIV_MAN;
            t->p[P_E5] = 1;                                /* REV */
            t->p[P_E4] = SLC_LOOP;
            trk_note_on(t, 62, 100);                       /* slice 2, backwards, looping */
            v = voice_of(t, 62);
            blocks(8);
            pa = slc_man_of(s)->pos[2];
            m = slc_man_begin(0);
            slc_man_move(s, m, 2, 100000);                 /* its start past the playhead */
            slc_man_commit(0);
            for (k = 0; k < 400u && v && v->active; k++)
                blocks(1);
            ok &= slc_man_of(s)->pos[2] > pa && !(v && v->active);
            trk_note_off(t, 62);
            check("MAN: a start moved past a reverse voice ends it (no window past its buffer)", ok, 0);

            {   /* a held LOOP voice whose slice index shifts (JOIN / SPLIT of an earlier slice): at its next loop
                 * it plays the new slice whole, not one sample over and over (the end refreshed too) */
                const slc_man_t *c;
                uint32_t r;
                for (r = 0; r < 2u; r++) {
                    int32_t o[2 * CTL], mn = 1 << 30, mx = -(1 << 30);
                    m = slc_man_begin(0);
                    m->n = 8;
                    m->end = 0;
                    for (k = 0; k < 8u; k++) {
                        m->pos[k] = k * (s->len / 8u);
                        m->st[k] = slc_state_at(s, m->pos[k]);
                    }
                    slc_man_commit(0);
                    t->p[P_E5] = (int16_t)r;                   /* forward, then REV */
                    trk_note_on(t, 65, 100);                   /* slice 5, looping */
                    v = voice_of(t, 65);
                    blocks(20);
                    m = slc_man_begin(0);
                    if (r)
                        slc_man_split(s, m, 1);                /* slice 5 is now the old slice 4 (ends where it began) */
                    else
                        slc_man_join(m, 2);                    /* slice 5 is now the old slice 6 (starts at its end) */
                    slc_man_commit(0);
                    c = slc_man_of(s);
                    for (k = 0; k < 8000u && v && v->active && v->ph[2] != (r ? c->pos[5] : c->pos[6]); k++)
                        blocks(1);                             /* (to its next loop) */
                    blocks(50);
                    for (k = 0; k < 200u; k++) {
                        mix_block(o, CTL);
                        for (i = 0; i < 2u * CTL; i++) {
                            mn = o[i] < mn ? o[i] : mn;
                            mx = o[i] > mx ? o[i] : mx;
                        }
                    }
                    ok = v && v->active && slice_of(v) == 5u && v->ph[2] == (r ? c->pos[5] : c->pos[6]) &&
                         v->ph[0] >= c->pos[5] && v->ph[0] <= c->pos[6] && mx - mn > 1000;
                    check(r ? "MAN: a held REV LOOP voice after a SPLIT before it loops the new slice"
                            : "MAN: a held LOOP voice after a JOIN before it loops the new slice",
                          ok, "ph %u end %u, output %d..%d", v ? v->ph[0] : 0u, v ? v->ph[2] : 0u, mn, mx);
                    trk_note_off(t, 65);
                    blocks(100);
                }
                t->p[P_E5] = 1;
            }

            ok = 1;
            tiny = SLC_BREAK;                              /* a material shorter than two slices */
            tiny.len = 100;
            memset(&t2, 0, sizeof t2);
            t2.n = 1;
            ok &= slc_man_move(&tiny, &t2, 0, 50) == 36u && slc_man_move(&tiny, &t2, 0, -50) == 0u;
            ok &= slc_man_split(&tiny, &t2, 0) == 0u && t2.n == 1u;            /* 100 < 2 x 64 */
            tiny.len = 50;                                  /* shorter than one */
            ok &= slc_man_move(&tiny, &t2, 0, 10) == 0u && slc_man_move_end(&tiny, &t2, -10) == 50u && !t2.end;
            keep_src = slc_usr[0];                         /* AUTO's last hit 10 samples before the end */
            slc_usr[0].apos[slc_usr[0].nauto - 1u] = slc_usr[0].len - 10u;
            slc_man[0][0].n = slc_man[0][1].n = 0;         /* no MAN in use: begin copies AUTO */
            m = slc_man_begin(0);
            ok &= m->n == slc_usr[0].nauto - 1u && !man_check(s, m);
            slc_usr[0] = keep_src;
            check("MAN: no room to move or split: nothing moves; AUTO hits too close are left out", ok, 0);
        }
    }

    /* 7: the store, in the slot's last sector (slice_store.c) */
    {
        const smp_user_hdr_t *h = (const smp_user_hdr_t *)smp_user_xip(0);
        const slc_rec_t *r = (const slc_rec_t *)(smp_user_xip(0) + SLC_REC_OFF);
        const slc_src_t *s;
        slc_man_t *m, keep;
        smp_user_hdr_t h0;
        uint32_t ok = 1, e0;
        slc_store_boot();                                  /* (persist_boot: the scans read the record) */
        smp_user_scan(0);
        s = slc_get(1);
        ok &= !slc_man_of(s) && slc_rec_room(0) && h->data_len + SMP_USER_DATA <= SLC_REC_OFF;
        m = slc_man_begin(0);
        slc_man_move(s, m, 1, 777);
        slc_man_split(s, m, 3);
        slc_man_move_end(s, m, -1234);
        slc_man_commit(0);
        keep = *slc_man_of(s);
        slc_man_save |= 1u;
        host_busy = 1;
        host_msg[0] = 0;
        e0 = host_erases;
        slc_store_poll();
        ok &= host_erases == e0 && slc_man_save == 1u;     /* playing: waits */
        host_busy = 0;
        host_page = 1;
        slc_store_poll();
        ok &= host_erases == e0 && slc_man_save == 1u;     /* the page still open: waits */
        host_page = 0;
        slc_store_poll();
        ok &= host_erases == e0 + 1u && !slc_man_save && str_eq(host_msg, "SLICES SAVED") && r->magic == SLC_REC_MAGIC &&
              r->n == keep.n && r->end == keep.end && r->len == s->len && r->crc == h->crc;
        slc_man_save |= 1u;                                /* the same again: no erase */
        slc_store_poll();
        ok &= host_erases == e0 + 1u;
        check("store: written once stopped and off the page, only when it changed (\"SLICES SAVED\")", ok, 0);

        ok = 1;
        smp_user_scan(0);                                  /* boot / the same sample uploaded again */
        s = slc_get(1);
        ok &= slc_man_of(s) && slc_man_of(s)->n == keep.n && slc_man_of(s)->end == keep.end &&
              !memcmp(slc_man_of(s)->pos, keep.pos, keep.n * 4u) && !memcmp(slc_man_of(s)->st, keep.st, keep.n * 4u);
        check("store: a slot scan puts the stored slices back (starts, end, states)", ok, 0);

        ok = 1;
        h0 = *h;
        ((smp_user_hdr_t *)host_slots)->crc ^= 1u;         /* another sample in the slot */
        smp_user_scan(0);
        ok &= !slc_man_of(slc_get(1));
        *(smp_user_hdr_t *)host_slots = h0;
        ((uint8_t *)host_slots)[SLC_REC_OFF + 20] ^= 4u;    /* a start changed: the record's CRC fails */
        smp_user_scan(0);
        ok &= !slc_man_of(slc_get(1));
        ((uint8_t *)host_slots)[SLC_REC_OFF + 20] ^= 4u;
        smp_user_scan(0);
        ok &= slc_man_of(slc_get(1)) != 0;
        check("store: another sample's record or a broken one is ignored", ok, 0);

        ok = 1;
        {   /* a record with a valid CRC but starts that do not fit: refused by slc_man_restore */
            slc_rec_t c = *r;
            c.pos[1] = c.pos[0] + 1u;
            c.rcrc = st_crc32(&c, sizeof c - 4u);
            st_erase(SMP_USER_BASE + SLC_REC_OFF);
            st_prog(SMP_USER_BASE + SLC_REC_OFF, &c, sizeof c);
            smp_user_scan(0);
            ok &= !slc_man_of(slc_get(1));
        }
        s = slc_get(1);                                    /* a save torn after the erase: no slices, the sample stays */
        m = slc_man_begin(0);
        slc_man_split(s, m, 0);
        slc_man_commit(0);
        slc_man_save |= 1u;
        host_torn = 1;
        slc_store_poll();
        host_torn = 0;
        ok &= str_eq(host_msg, "SAVE ERROR");
        smp_user_scan(0);
        ok &= usr_nz[0] && !slc_man_of(slc_get(1));
        check("store: bad starts with a good CRC refused; a torn save loses the slices, not the sample", ok, 0);

        ok = 1;
        h0 = *h;
        ((smp_user_hdr_t *)host_slots)->data_len = SLC_REC_OFF - SMP_USER_DATA + 1u;   /* into the last sector */
        s = slc_get(1);
        m = slc_man_begin(0);
        slc_man_split(s, m, 0);
        slc_man_commit(0);
        e0 = host_erases;
        slc_man_save |= 1u;
        slc_store_poll();
        ok &= host_erases == e0 && str_eq(host_msg, "TOO LONG TO SAVE") && !slc_rec_room(0);
        *(smp_user_hdr_t *)host_slots = h0;
        smp_user_scan(0);
        check("store: a sample reaching the last sector keeps its slices in RAM only (TOO LONG TO SAVE)", ok, 0);

        ok = 1;                                            /* a save pending for a slot emptied / being uploaded: */
        m = slc_man_begin(0);                              /* dropped without a word */
        slc_man_split(slc_get(1), m, 0);
        slc_man_commit(0);
        slc_man_save |= 1u;
        usr_nz[0] = 0;                                     /* (ED_SMP_BEGIN) */
        host_msg[0] = 0;
        e0 = host_erases;
        slc_store_poll();
        ok &= host_erases == e0 && !host_msg[0] && !slc_man_save;
        smp_user_scan(0);
        check("store: a slot emptied or being uploaded: the save is dropped (no TOO LONG TO SAVE)", ok, "msg '%s'", host_msg);
        slc_man_load = 0;
        smp_user_scan(0);                                  /* (the demos: no MAN slices) */
    }

    /* 5 */
    {
        const demo_t D[] = {
            {"preset_chop.wav", 0, 120, 16, 4, CHOP, -1, -1},
            {"preset_stutter.wav", 1, 120, 16, 4, STUTTER, -1, -1},
            {"break_in_order.wav", 0, 120, 16, 2, SLICES, -1, -1},
            {"break_resequenced.wav", 0, 120, 32, 4, RESEQ, -1, -1},
            {"break_resequenced_32.wav", 0, 120, 32, 4, RESEQ, -1, 3},
            {"usr_empty_plays_break.wav", 0, 120, 16, 2, SLICES, 2, SLC_DIV_AUTO},   /* USR2 empty: BREAK, AUTO */
            {"usr_loop_auto_in_order.wav", 0, 96, 16, 2, SLICES, 1, SLC_DIV_AUTO},
            {"usr_loop_auto_resequenced.wav", 0, 96, 32, 4, USR, 1, SLC_DIV_AUTO},
            {"usr_loop_16_resequenced.wav", 0, 96, 32, 4, USR, 1, 2},
        };
        int df = 0;
        for (i = 0; i < sizeof D / sizeof D[0]; i++)
            df += in_child(demo, dir, &D[i]);
        check("demos rendered (peak above -24 dBFS, no clipping)", !df, "%s", dir);
    }
    printf("slice: %s\n", fails ? "FAILED" : "all checks ok");
    return fails != 0;
}
