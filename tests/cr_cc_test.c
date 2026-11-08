/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* Host test of MIDI CC on the sounds' parameters (HISTORY 3y269; docs/MANUAL.md section 15 "MIDI in"; cr_midi.c crm_cc_*,
 * cr_out.c cr_cc_post, cr_edit.c ce_cc_*), on the firmware as the emulator builds it:
 * - the common CCs on every engine (10 Pan, 92 Drive, 74 / 71 the filter, 73 75 79 72 the amp envelope, 76 / 77 LFO 1):
 *   the cell each reaches, none where the engine has no such thing (FM6 and CZ-1: no filter, no ADSR);
 * - the blocks: CC -> the cell SELECT reaches at that lane of the group (the editor's own order, ce_select), KNOB n; a few
 *   exact pairs on FM TONE and the VA; past a block's end and on an empty cell: nothing;
 * - a CC in through the MIDI queue (usb.c midi_enqueue -> cr_out.c cr_midi_in -> cr_ui.c cu_midi_poll): the value
 *   scaled, the sound edited, the cell hot in the editor, a popup outside it; a burst coalesced (the latest value);
 *   the bass channel the bass sound; CC 38 with RPN 0 selected still its data entry;
 * - out: an editor knob, the FX group's send, KNOB 4 post their CCs (CRE_CCOUT) on the part's channel, its echo is
 *   ignored for a second; Options > MIDI CC Off / In / Out; a part's channel Off sends nothing.
 *   ./cr_cc_test --map prints each engine's map (the tables in docs/MANUAL.md section 15) */
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
#define OK(c, ...)                                                      \
    do {                                                                \
        checks++;                                                       \
        if (!(c)) {                                                     \
            fails++;                                                    \
            printf("  FAIL %s:%d: ", __FILE__, __LINE__);               \
        } else {                                                        \
            printf("ok   ");                                            \
        }                                                               \
        printf(__VA_ARGS__);                                            \
        printf("\n");                                                   \
    } while (0)

static track_t *T0 = &trk[CR_PART_CHORD];

/* a ref's built-in (page, col) on the engine (a compiled layout's column: the one it binds), 0 not a deep cell */
static int builtin(const track_t *t, ce_ref_t r, uint32_t *bp, uint32_t *bc)
{
    if (r.k != CE_R_DEEP)
        return 0;
#if CR_LAYC_N
    if (cp_deep(t) != ENGINES[eng_idx(t->eng_req)]->deep)
        return cp_lay_src(t, r.a, r.b & 3u, bp, bc);
#endif
    *bp = r.a;
    *bc = r.b & 3u;
    return 1;
}
/* "TITLE/LABEL" of a ref (the built-in page's title, its column's label), "trk N", "-" none */
static const char *refname(const track_t *t, ce_ref_t r, int ok)
{
    static char b[40];
    uint32_t bp, bc;
    const eng_deep_t *d = ENGINES[eng_idx(t->eng_req)]->deep;
    if (!ok)
        return "-";
    if (r.k == CE_R_TRK) {
        snprintf(b, sizeof b, "trk %u", (unsigned)r.a);
        return b;
    }
    if (d && builtin(t, r, &bp, &bc)) {
        snprintf(b, sizeof b, "%s/%s", d->pages[bp].title, d->pages[bp].col[bc].label);
        return b;
    }
    return "?";
}
static uint32_t eng_by_name(const char *n)
{
    uint32_t e;
    for (e = 0; e < NENGINES; e++)
        if (ENGINES[e] && ENGINES[e]->name && !strcmp(ENGINES[e]->name, n))
            return e;
    return 0;
}

/* a CC in on a channel, through usb.c's queue, the ISR's MIDI in and the UI's poll */
static void cc_in(uint32_t ch, uint32_t cc, uint32_t v)
{
    midi_enqueue(0x0Bu | (0xB0u | ch) << 8 | cc << 16 | v << 24, 0);
    cr_midi_in();
    cu_midi_poll();
}
/* the CRE_CCOUT events posted since *r (cr_out.c's queue, not drained here): the last one's channel, CC and value */
static uint32_t outs(uint32_t *r, uint32_t *ch, uint32_t *cc, int32_t *v)
{
    uint32_t n = 0;
    for (; *r != cr_evq_w; (*r)++) {
        const cr_ev_in_t *e = &cr_evq[*r % CR_EVQ];
        if (e->op != CRE_CCOUT)
            continue;
        *ch = e->a;
        *cc = e->b;
        *v = e->v;
        n++;
    }
    cr_evq_r = cr_evq_w;                          /* (nothing else reads it here) */
    return n;
}

static const char *const GN[5] = {"OSC", "FILT", "ENV", "LFO", "MOD"};

/* --map: each engine's CCs as docs/MANUAL.md section 15 has them */
static void print_map(void)
{
    static const uint8_t COMMON[10] = {10, 92, 74, 71, 73, 75, 79, 72, 76, 77};
    uint32_t r, e, g, n, k, ln, c, i;
    for (r = 0; r < NENG_SHOWN; r++) {
        e = eng_vis(r);
        if (!cu_engine_melodic(e))
            continue;
        cu_pool_load(0, e, 1);
        printf("\n**%s**\n\n| CC | cell |\n| --- | --- |\n", ENGINES[e]->name);
        printf("| common |");
        for (i = 0; i < NELEM(COMMON); i++) {
            ce_ref_t x;
            int32_t v;
            const param_desc_t *d;
            char lb[16];
            if (!ce_cc_ref(T0, COMMON[i], &x) || !(d = ce_param(T0, x, &v)))
                continue;
            ce_label(T0, x, d, lb, sizeof lb);
            printf(" %u %s%s", (unsigned)COMMON[i], lb, i + 1u < NELEM(COMMON) ? "," : "");
        }
        printf(" |\n");
        for (g = 0; g < 5u; g++)
            for (n = 0; crm_cc_of(g, n, 0); n++) {
                ce_view_t vw;
                char cell[4][20];
                if (!ce_has(T0, g) || !ce_cc_lane(T0, g, n, &k, &ln))
                    break;
                ce_view_at(T0, g, k, ln, &vw);
                for (c = 0; c < 4u; c++) {
                    ce_ref_t x = vw.ref[vw.active][c];
                    const param_desc_t *d;
                    int32_t v;
                    cell[c][0] = 0;
                    if (!crm_cc_of(g, n, c))
                        strcpy(cell[c], "(none)");
                    else if ((x.k == CE_R_DEEP || x.k == CE_R_TRK) && (d = ce_param(T0, x, &v)))
                        ce_label(T0, x, d, cell[c], sizeof cell[c]);
                    else if (x.k == CE_R_NONE && g == CE_MOD && vw.ref[vw.active][0].k == CE_R_DEEP &&
                             ce_is_matrix(cp_deep(T0), vw.ref[vw.active][0].a) &&
                             cp_deep(T0)->pages[vw.ref[vw.active][0].a].col[c].label) {
                        x = ce_r(CE_R_DEEP, vw.ref[vw.active][0].a, c);   /* an unused matrix slot's DST / AMT (they
                                                                           * take a CC once it has a source) */
                        ce_label(T0, x, &cp_deep(T0)->pages[x.a].col[c], cell[c], sizeof cell[c]);
                    }
                    else
                        strcpy(cell[c], "-");
                }
                printf("| %u-%u | %s ", (unsigned)crm_cc_of(g, n, 0),
                       (unsigned)(crm_cc_of(g, n, 3) ? crm_cc_of(g, n, 3) : crm_cc_of(g, n, 2) ? crm_cc_of(g, n, 2) : crm_cc_of(g, n, 1)),
                       GN[g]);
                for (c = 0; vw.right[c]; c++)     /* (the title's middle dot, Latin-1 0xB7: UTF-8 here) */
                    if ((uint8_t)vw.right[c] == 0xB7u)
                        printf("\xc2\xb7");
                    else
                        putchar(vw.right[c]);
                printf(": %s, %s, %s, %s |\n", cell[0], cell[1], cell[2], cell[3]);
            }
    }
}

int main(int argc, char **argv)
{
    uint32_t e, g, n, k, ln, c, i, bad, r0, och, occ, steps;
    int32_t v, ov;
    ce_ref_t r;
    const param_desc_t *d;
    const eng_deep_t *dd;

    emu_flash_open();
    memset(emu_flash, 0xFF, sizeof emu_flash);
    cr_bank_boot();
    cr_settings_boot();
    cr_ui_init();
    if (argc > 1 && !strcmp(argv[1], "--map")) {
        print_map();
        return 0;
    }

    /* the common CCs on every engine */
    {
        static const struct { const char *eng; uint8_t cc; const char *want; } W[] = {
            {"VA", 74, "FILTER/CUT"}, {"VA", 71, "FILTER/RES"}, {"VA", 73, "ENV 1/ATK"}, {"VA", 75, "ENV 1/DEC"},
            {"VA", 79, "ENV 1/SUS"}, {"VA", 72, "ENV 1/REL"}, {"VA", 76, "LFO 1/RATE"}, {"VA", 77, "LFO 1/DEPTH"},
            {"FM TONE", 74, "FILTER+/FREQ"}, {"FM TONE", 71, "FILTER+/RESO"}, {"FM TONE", 73, "AMP/ATK"},
            {"FM TONE", 75, "AMP/DEC"}, {"FM TONE", 79, "AMP/SUS"}, {"FM TONE", 72, "AMP/REL"},
            {"FM TONE", 76, "LFO 1/SPEED"}, {"FM TONE", 77, "LFO 1+/DEPTH"},
            {"GRAIN", 74, "FILTER/CUT"}, {"GRAIN", 71, "FILTER/RES"}, {"GRAIN", 73, "ENV 1/ATK"}, {"GRAIN", 72, "ENV 1/REL"},
            {"GRAIN", 76, "LFO 1/RATE"}, {"GRAIN", 77, "LFO 1/DEPTH"},
            {"P5", 74, "FILTER/CUT"}, {"P5", 71, "FILTER/RES"}, {"P5", 73, "ENV 1/ATK"}, {"P5", 79, "ENV 1/SUS"},
            {"P5", 76, "LFO/RATE"}, {"P5", 77, "LFO/AMOUNT"},
            {"FM6", 74, "-"}, {"FM6", 71, "-"}, {"FM6", 73, "-"}, {"FM6", 72, "-"}, {"FM6", 76, "LFO/SPEED"},
            {"FM6", 77, "LFO/PMD"},
            {"CZ-1", 74, "-"}, {"CZ-1", 71, "-"}, {"CZ-1", 73, "-"}, {"CZ-1", 79, "-"}, {"CZ-1", 76, "VIB/RATE"},
            {"CZ-1", 77, "VIB/DEPTH"},
            {"WHEEL", 74, "-"}, {"WHEEL", 71, "-"}};
        char want[16];
        for (i = 0, bad = 0; i < NELEM(W); i++) {
            int ok;
            e = eng_by_name(W[i].eng);
            cu_pool_load(0, e, 1);
            ok = ce_cc_ref(T0, W[i].cc, &r);
            if (strcmp(refname(T0, r, ok), W[i].want)) {
                bad++;
                printf("  %s cc %u: %s, not %s\n", W[i].eng, (unsigned)W[i].cc, refname(T0, r, ok), W[i].want);
            }
        }
        OK(!bad && eng_by_name("P5") && eng_by_name("WHEEL") && eng_by_name("GRAIN"),
           "common: %u pairs on the seven engines (P5 through its compiled rows' columns; FM6 / CZ-1 no filter, no ADSR)",
           (unsigned)NELEM(W));
        for (e = 0, bad = 0; e < NENGINES; e++) {
            if (!ENGINES[e] || !cu_engine_melodic(e) || !eng_ok(e))
                continue;
            cu_pool_load(0, e, 1);
            if (!ce_cc_ref(T0, 10, &r) || r.k != CE_R_TRK || r.a != P_PAN || !ce_cc_ref(T0, 92, &r) ||
                ce_send_ref(T0, r) != P_DIST)
                bad++;
        }
        cu_pool_load(0, eng_by_name("WHEEL"), 1);
        snprintf(want, sizeof want, "trk %u", (unsigned)P_ATK);
        OK(!bad && ce_cc_ref(T0, 73, &r) && !strcmp(refname(T0, r, 1), want) && ce_cc_ref(T0, 76, &r) && r.a == P_LRATE &&
           ce_cc_ref(T0, 77, &r) && r.a == P_LD_PIT,
           "common: 10 Pan and 92 Drive on every engine; WHEEL (no deep pages): the platform's ENV and LFO");
    }

    /* the blocks: the cell SELECT reaches at lane n, KNOB c, on every engine */
    for (e = 0, bad = 0, steps = 0; e < NENGINES; e++) {
        if (!ENGINES[e] || !cu_engine_melodic(e) || !eng_ok(e))
            continue;
        cu_pool_load(0, e, 1);
        cu_edit_open(0);
        for (g = 0; g < 5u; g++) {
            if (!ce_has(T0, g))
                continue;
            cx.grp[0] = (uint8_t)g;
            cx.scr[0][g] = cx.lane[0][g] = 0;
            for (n = 0; crm_cc_of(g, n, 0); n++) {
                ce_view_t vw;
                if (n)
                    ce_select(1);
                if (!cx.scr[0][g] && !cx.lane[0][g] && n) {   /* (wrapped: the group has fewer lanes) */
                    for (c = 0; c < 4u; c++)
                        if (crm_cc_of(g, n, c) && ce_cc_ref(T0, crm_cc_of(g, n, c), &r))
                            bad++;
                    break;
                }
                ce_view(T0, 0, &vw);
                for (c = 0; c < 4u; c++) {
                    ce_ref_t x = vw.ref[vw.active][c];
                    int want = x.k == CE_R_DEEP || x.k == CE_R_TRK, got;
                    if (!crm_cc_of(g, n, c))
                        continue;
                    got = ce_cc_ref(T0, crm_cc_of(g, n, c), &r);
                    if (got != want || (got && !ce_ref_eq(r, x)))
                        bad++;
                    steps++;
                }
            }
        }
        ce_close();
    }
    OK(!bad && steps > 300u, "blocks: on every engine each block CC is the cell SELECT reaches at its lane, KNOB n (%u cells)",
       (unsigned)steps);
    {
        static const struct { const char *eng; uint8_t cc; const char *want; } W[] = {
            {"FM TONE", 16, "SYN 1/ALGO"}, {"FM TONE", 19, "SYN 1/RATIO B"}, {"FM TONE", 20, "SYN 1+/HARM"},
            {"FM TONE", 23, "SYN 1+/MIX"}, {"FM TONE", 24, "SYN 2/OFS C"}, {"FM TONE", 28, "-"},
            {"FM TONE", 32, "FILTER/ATK"}, {"FM TONE", 36, "FILTER+/FREQ"}, {"FM TONE", 37, "FILTER+/RESO"},
            {"FM TONE", 38, "FILTER+/TYPE"}, {"FM TONE", 40, "FILT 2/DELAY"}, {"FM TONE", 47, "-"},
            {"FM TONE", 48, "ENV A/A ATK"}, {"FM TONE", 52, "ENV B/B ATK"}, {"FM TONE", 59, "ENV 2/PHRT"}, {"FM TONE", 63, "ENV 2+/VEL"},
            {"FM TONE", 102, "LFO 1/SPEED"}, {"FM TONE", 109, "LFO 1+/DEPTH"}, {"FM TONE", 110, "LFO 2/SPEED"},
            {"VA", 16, "OSC 1/WAVE"}, {"VA", 17, "OSC 1/LEVEL"}, {"VA", 20, "OSC 2/WAVE"}, {"VA", 31, "OSC 4/FINE"},
            {"VA", 32, "FILTER/CUT"}, {"VA", 35, "FILTER/FENV"}, {"VA", 36, "FILTER+/KTRK"}, {"VA", 37, "-"},
            {"VA", 39, "FILTER+/DRIVE"}, {"VA", 40, "-"}, {"VA", 48, "ENV 1/ATK"}, {"VA", 53, "ENV 1+/VEL"},
            {"VA", 56, "ENV 2/ATK"}, {"VA", 63, "-"}, {"VA", 102, "LFO 1/RATE"}, {"VA", 106, "LFO 2/RATE"},
            {"VA", 113, "LFO 3/FADE"}, {"VA", 80, "MOD 1/SRC"}, {"VA", 81, "MOD 1/DST"}, {"VA", 84, "MOD 2/SRC"}};
        for (i = 0, bad = 0; i < NELEM(W); i++) {
            int ok;
            cu_pool_load(0, eng_by_name(W[i].eng), 1);
            ok = ce_cc_ref(T0, W[i].cc, &r);
            if (strcmp(refname(T0, r, ok), W[i].want)) {
                bad++;
                printf("  %s cc %u: %s, not %s\n", W[i].eng, (unsigned)W[i].cc, refname(T0, r, ok), W[i].want);
            }
        }
        OK(!bad, "blocks: %u exact CC -> cell pairs on FM TONE and the VA (an empty cell keeps its slot, nothing there)",
           (unsigned)NELEM(W));
    }
    cu_pool_load(0, eng_by_name("FM TONE"), 1);
    OK(ce_cc_ref(T0, 82, &r) && r.k == CE_R_TRK && r.a == P_ED_FLT && !ce_cc_ref(T0, 80, &r),
       "blocks: FM TONE's MOD (the platform's routes): 82 = MOD 1's amount (Env>Filter), 80 its fixed source: nothing");

    /* in, through the queue */
    cu_pool_load(0, eng_by_name("VA"), 1);
    cu_pool_load(1, eng_by_name("FM TONE"), 1);
    dd = cp_deep(T0);
    psnd[0].edited = psnd[1].edited = 0;
    fm1_ms = 100000;
    cc_in(0, 74, 127);
    OK(dd->get(T0, 8, 0) == 127 && psnd[0].edited && !psnd[1].edited && cu.pop.until > fm1_ms && !strcmp(cu.pop.label, "cutoff"),
       "in: CC 74 = 127 on ch 1: the VA chord sound's cutoff 127, edited, the popup \"%s\" %s %s", cu.pop.label,
       cu.pop.value, cu.pop.sub);
    cc_in(0, 74, 0);
    OK(dd->get(T0, 8, 0) == 0, "in: CC 74 = 0: cutoff 0");
    cc_in(1, 74, 64);
    v = cp_deep(&trk[CR_PART_BASS])->get(&trk[CR_PART_BASS], 4, 0);
    OK(v == crm_cc_val(64, 0, 12700, 0) && v == 6400 && psnd[1].edited && dd->get(T0, 8, 0) == 0,
       "in: CC 74 = 64 on ch 2: the FM TONE bass sound's FREQ 64.00 (hundredths), the chord sound untouched");
    for (i = 0; i < 50u; i++)                     /* a burst on one CC: no overflow, the last value */
        midi_enqueue(0x0Bu | 0xB0u << 8 | 73u << 16 | (i & 127u) << 24, 0);
    cr_midi_in();
    OK(cr_minq_w - cr_minq_r <= 1u, "in: a burst of 50 CC 73s coalesced: one queue entry (%u)", (unsigned)(cr_minq_w - cr_minq_r));
    cu_midi_poll();
    OK(dd->get(T0, 10, 0) == 49, "in: .. the latest value applied (ENV 1 ATK 49)");
    for (c = 16; c < 64u; c++)                    /* 48 different CCs on both parts, more than the queue holds */
        midi_enqueue(0x0Bu | 0xB0u << 8 | c << 16 | 5u << 24, 0);
    cr_midi_in();
    for (c = 16; c < 64u; c++)
        midi_enqueue(0x0Bu | 0xB1u << 8 | c << 16 | 5u << 24, 0);
    cr_midi_in();
    OK(cr_cc_sweep, "in: 96 different CCs: the queue full, the sweep asked for");
    cu_midi_poll();
    for (i = 0, bad = 0; i < 2u; i++)
        for (k = 0; k < CRM_NCC; k++)
            bad += (cr_ccv[i][k] & 0x80u) != 0;
    OK(!bad && !cr_cc_sweep && dd->get(T0, 12, 0) == crm_cc_val(5, 0, 127, 0), "in: .. every one applied, none left pending");
    cu_edit_open(0);
    cx.grp[0] = CE_FILT;
    cx.scr[0][CE_FILT] = cx.lane[0][CE_FILT] = 0;
    cx.hot_r = 0;
    cu.pop.until = 0;
    cc_in(0, 71, 100);
    OK(dd->get(T0, 8, 1) == 100 && cx.hot_r == 1 && cx.hot_c == 1 && !cu.pop.until,
       "in: the editor on FILT: CC 71 makes the Reso cell hot (no popup)");
    cc_in(0, 92, 90);
    OK(cu_send(0, P_DIST) == 90 && cu.pop.until > fm1_ms, "in: CC 92 = 90: the chord part's Drive send, its popup");
    cc_in(0, 10, 0);
    OK(T0->p[P_PAN] == track_desc(T0, P_PAN)->min, "in: CC 10 = 0: Pan hard left (%d)", (int)T0->p[P_PAN]);
    ce_close();
    cc_in(0, 101, 0);                             /* RPN 0 selected: CC 38 its data entry LSB, as before */
    cc_in(0, 100, 0);
    v = dd->get(T0, 9, 2);
    cc_in(0, 38, 50);
    OK(cr_mchan(0)->cents == 50 && dd->get(T0, 9, 2) == v, "in: CC 38 with RPN 0 selected: the bend range's cents (50), "
       "no sound parameter");
    cc_in(0, 101, 127);
    cc_in(0, 100, 127);
    cc_in(0, 38, 127);
    OK(dd->get(T0, 9, 2) == 127, "in: CC 38 with no RPN selected: FILT's block (the VA's SPREAD)");

    /* out */
    cu_pool_load(0, eng_by_name("VA"), 1);
    r0 = cr_evq_w;
    (void)outs(&r0, &och, &occ, &ov);
    cu_edit_open(0);
    cx.grp[0] = CE_FILT;
    cx.scr[0][CE_FILT] = cx.lane[0][CE_FILT] = 0;
    dd->set(T0, 8, 0, 40);
    ce_knob(0, 1, 0);
    v = dd->get(T0, 8, 0);
    n = outs(&r0, &och, &occ, &ov);
    OK(v == 41 && n == 2 && och == 0 && occ == 74 && ov == crm_cc_out(41, 0, 127, 0),
       "out: KNOB 1 on the VA's cutoff: CC 32 (FILT's block) and CC 74, ch 1, value 41 (%u CCs)", (unsigned)n);
    ce_knob(0, 1, 0);
    (void)outs(&r0, &och, &occ, &ov);
    fm1_ms += 20;
    cc_in(0, 74, 41);
    OK(dd->get(T0, 8, 0) == 42, "out: the echo of an older value sent (41) a moment later: ignored (42 stays)");
    cc_in(0, 74, 30);
    OK(dd->get(T0, 8, 0) == 30, "out: another value: applied");
    ce_knob(0, 1, 0);
    (void)outs(&r0, &och, &occ, &ov);
    fm1_ms += 1100;
    cc_in(0, 74, 31);
    OK(dd->get(T0, 8, 0) == 31, "out: the same value a second later: applied");
    cx.grp[0] = CE_FX;
    ce_knob(3, 1, 0);
    n = outs(&r0, &och, &occ, &ov);
    OK(n == 1 && occ == 91 && ov == cu_send(0, P_REV), "out: the FX group's Reverb: CC 91 (%d)", (int)ov);
    cx.grp[0] = CE_MIX;
    cx.lane[0][CE_MIX] = 0;
    ce_knob(1, 1, 0);
    n = outs(&r0, &och, &occ, &ov);
    OK(n == 1 && occ == 10 && ov == T0->p[P_PAN] + 64, "out: the MIX group's Pan: CC 10 (%d)", (int)ov);
    ce_close();
    cs.fx_sel = 0;
    cu_knob(EN_K4, 1);
    n = outs(&r0, &och, &occ, &ov);
    OK(n == 1 && och == 0 && occ == 91 && ov == cu_send(0, P_REV), "out: KNOB 4 (the main view's Reverb send): CC 91");
    ce_open(1);
    cx.grp[1] = CE_ENV;
    cx.scr[1][CE_ENV] = cx.lane[1][CE_ENV] = 0;
    ce_knob(0, 1, 0);
    n = outs(&r0, &och, &occ, &ov);
    OK(n == 1 && och == 1 && occ == 48, "out: the bass sound's (FM TONE) ENV A A ATK: CC 48 on ch 2 (not an amp ADSR: no 73)");

    /* Options > MIDI CC */
    opt_set(O_MIDI_CC, 2);                        /* Out only */
    v = cp_deep(&trk[CR_PART_BASS])->get(&trk[CR_PART_BASS], 7, 0);
    cc_in(1, 48, 127);
    OK(cp_deep(&trk[CR_PART_BASS])->get(&trk[CR_PART_BASS], 7, 0) == v && cr_cc_mode == 2u,
       "option Out: CC 48 in changes nothing (forwarded as before)");
    ce_knob(0, 1, 0);
    OK(outs(&r0, &och, &occ, &ov) == 1, "option Out: a knob still sends");
    opt_set(O_MIDI_CC, 1);                        /* In only */
    ce_knob(0, 1, 0);
    OK(outs(&r0, &och, &occ, &ov) == 0, "option In: a knob sends nothing");
    cc_in(1, 48, 127);
    OK(cp_deep(&trk[CR_PART_BASS])->get(&trk[CR_PART_BASS], 7, 0) == 127, "option In: CC 48 in sets A ATK");
    opt_set(O_MIDI_CC, 0);
    cc_in(1, 48, 0);
    ce_knob(0, -1, 0);
    OK(cp_deep(&trk[CR_PART_BASS])->get(&trk[CR_PART_BASS], 7, 0) == 126 && outs(&r0, &och, &occ, &ov) == 0,
       "option Off: neither (the CC forwarded, nothing sent)");
    opt_set(O_MIDI_CC, 3);
    opt_set(O_CH_BASS, 0);
    ce_knob(0, 1, 0);
    OK(outs(&r0, &och, &occ, &ov) == 0, "out: the bass part's channel Off: nothing sent");
    opt_set(O_CH_BASS, 2);
    ce_close();
    {
        char t[24];
        opt_text(O_MIDI_CC, t, sizeof t);
        OK(!strcmp(t, "In + Out") && !strcmp(O_NAME[O_MIDI_CC], "MIDI CC"), "option: \"MIDI CC\" reads \"%s\"", t);
    }
    (void)d;
    printf("cr_cc_test: %d checks, %d failed\n", checks, fails);
    return fails != 0;
}
