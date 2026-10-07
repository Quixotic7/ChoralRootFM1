/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments
 * Modifications Copyright (C) 2026 Kerem Kilic (Ellic Studio)
 * Modifications Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* CZ-1 (engine ENGI_CZ, docs/CZ1.md): Melodee's native Casio CZ-1 tone engine (Kerem Kilic's fork of Felucca,
 * <https://github.com/keremimo/melodee>, GPL-3.0). CZ-1 is a separate engine: native Casio tone data (cz_patch.h:
 * the 144-byte tone), never PHASE knob conversion. The renderer is cz_native.c (Devin Acker's uPD933 model,
 * BSD-3-Clause); the 64 factory tones are Casio's (build/gen/melodee_cz1.h, tools/gen_cz1_factory.py).
 *
 * From Melodee's eng_phase.c (the phase family's shared parts; ChoralRoot's PHASE keeps Felucca's own envelopes, so
 * they live here): the eight-point rate / target envelope (cz_env_tick), the per-voice state (cz_voice_t, in the
 * part's pooled engine state: engines.c eng_state.cz) and the note reset (cz_note_on = Melodee's phase_note_on).
 * From Melodee's eng_cz.c: the factory presets, BANK / PTCH, the done test, the USB SysEx collector.
 *
 * ChoralRoot's additions (as eng_fm6.c's): the deep pages for the sound editor (cz_edit.h's panel values: LINE n,
 * DETUNE, DCW, the six eight-step envelopes, VIB, TONE), the patch blob (the 144-byte tone: eng_deep_t.blob_*) and
 * the user slot's tone (cz_ustore.c: cz_user_pending / cz_store_read, cz_track_loaded on every load path through
 * eng_fm6.c fm6_track_loaded). Melodee's CZ TOOLS page (NAME, 1 > 2, 2 > 1, COMPARE) is not ported: ChoralRoot's
 * editor pages hold values, not actions (docs/CZ1.md). */
#include "cz_patch.h"
static cz_patch_t cz_patch[NTRK] __attribute__((section(".pool")));
static void cz_init(void) { for (uint32_t k = 0; k < NTRK; k++) cz_patch_init(cz_patch[k].raw); }

/* Eight rate/target points, with explicit sustain/end. Rates are Q24 per
 * control tick. No exponential asymptote and no extra release after END. */
typedef struct { uint32_t rate[8]; int32_t level[8]; uint8_t sustain, end; } cz_env_def_t;
typedef struct { int32_t level; uint8_t stage, gate; } cz_env_t;
typedef struct { cz_env_t eg[2][3]; uint32_t vib_phase, vib_ticks, noise; } cz_voice_t;
typedef struct { cz_voice_t v[NPOLY]; } cz_part_t;
static cz_part_t *cz_part(uint32_t part);              /* engines.c: shared runtime pool */
static cz_voice_t *cz_voice(track_t *t, voice_t *v)
{
    return &cz_part((uint32_t)(t - trk) % NPART)->v[(uint32_t)(v - t->v) % NPOLY];
}

static int32_t cz_env_tick(cz_env_t *e, const cz_env_def_t *d, uint32_t gate)
{
    uint32_t left = 65536u;
    if (e->gate && !gate && d->sustain <= d->end && e->stage <= d->sustain)
        e->stage = d->sustain + 1u;                    /* release even during attack */
    e->gate = (uint8_t)gate;
    /* Carry unused tick time across points, including zero-distance points.
     * A short segment must not acquire an extra 32-sample delay. */
    for (uint32_t k = 0; k < 8u && e->stage <= d->end; k++) {
        uint32_t st = e->stage, rate = d->rate[st];
        int32_t target = st == d->end ? 0 : d->level[st];
        int32_t delta = target - e->level;
        uint32_t dist = (uint32_t)(delta < 0 ? -delta : delta);
        uint32_t step = (uint32_t)mulq16((int32_t)rate, left);
        if (dist > step) {
            e->level += delta < 0 ? -(int32_t)step : (int32_t)step;
            break;
        }
        e->level = target;
        if (gate && st == d->sustain) break;
        e->stage++;
        if (dist && rate) {
            uint32_t used = (uint32_t)(((uint64_t)dist << 16) / rate);
            left = used < left ? left - used : 0;
        }
        if (!left) break;
    }
    return e->level;
}

static void cz_note_on(track_t *t, voice_t *v)        /* Melodee's phase_note_on */
{
    cz_voice_t *c = cz_voice(t, v);
    memset(c, 0, sizeof *c);
    for (uint32_t l = 0; l < 2u; l++)
        for (uint32_t e = 0; e < 3u; e++) c->eg[l][e].gate = 1;
    v->ph[0] = v->ph[1] = v->ph[2] = 0;
    v->s[0] = v->s[1] = v->s[4] = 0;
    if (!voice_was) v->s[2] = v->s[3] = 0;
    c->noise = 0x6D2B79F5u ^ (uint32_t)(v - t->v) * 0x9E3779B9u;
}

#include "cz_native.c"
#include "melodee_cz1.h"            /* Casio's 64 CZ-1 preset tones (tools/gen_cz1_factory.py) */
#include "cz_edit.h"                /* the panel values of a tone (Melodee's on-device editing) */

static int (*cz_user_bank_read)(uint32_t,uint32_t,uint8_t *);
static uint16_t cz_user_pick[NTRK];
static const char *const N_CZ_BANK[]={"A","B","C","D","E","F","G","H"};
static void cz_track_accept(track_t *t){uint32_t tr=(uint32_t)(t-trk)%NTRK;cz_user_pick[tr]=(uint16_t)(t->p[P_E0]*17+t->p[P_E1]);}
static int cz_native_done(track_t *t, voice_t *v)
{
    cz_voice_t *c = cz_voice(t, v);
    const uint8_t *b = cz_patch[(uint32_t)(t - trk) % NTRK].raw;
    uint32_t ls = b[0] & 3u;
    uint32_t a = ls == 1u ? 1u : 0u, z = ls >= 2u ? 1u : a;
    return c->eg[a][2].stage > (b[CZ_ENV_END[a][2]] & 7u) &&
        c->eg[z][2].stage > (b[CZ_ENV_END[ls == 2u ? 0u : z][2]] & 7u);
}
#define CZ_FACTORY_PRESET(n, bank, ptch, pat) \
    {n, {bank, ptch, 0, 0, 0, 0, 0, CZ_NATIVE}, {0, 70, 127, 60}, 0, 0, FX(0, 0, 0, 0), PAT(pat)},
static const preset_t CZ_PRESETS[] = {
    {"INIT TONE", {0, 0, 0, 0, 0, 0, 0, CZ_NATIVE}, {0, 70, 127, 60}, 0, 0, FX(0, 0, 0, 0), PAT(1)},
    CZ_FACTORY_PRESETS(CZ_FACTORY_PRESET)   /* Casio's, dry as the CZ-1 (no effects) */
};
_Static_assert(NELEM(CZ_PRESETS) == 1u + CZ_FACTORY_N, "CZ-1 presets: INIT TONE and Casio's 64");

/* ----------------------------------------------------- ChoralRoot: the tone of a part --- */
/* A user slot's tone (cz_ustore.c): cz_user_pending = the slot + 1 being loaded (upreset.c up_values), cz_store_read
 * its tone (0 = there is one) */
static uint8_t cz_user_pending;
static int (*cz_store_read)(uint32_t k, uint8_t *raw);

static void cz_tone_put(uint32_t tr, const uint8_t *raw)    /* a whole tone into part tr (main loop) */
{
    tr %= NTRK;
    memcpy(cz_patch[tr].raw, raw, CZ_BYTES);
    RING_PUBLISH();
}

/* factory preset k's tone (0 INIT TONE, 1..64 Casio's A-1 .. H-8) */
static void cz_factory_tone(uint32_t k, uint8_t *raw)
{
    if (k && k - 1u < CZ_FACTORY_N)
        memcpy(raw, CZ_FACTORY[k - 1u], CZ_BYTES);
    else
        cz_patch_init(raw);
}

/* a sound load put CZ-1 into track t (eng_fm6.c fm6_track_loaded calls it on every load path: a factory preset, a
 * user slot, an engine change, INIT, the picker): a user slot's own tone (its blob), else BANK / PTCH: PTCH 0 the init
 * tone, a factory preset Casio's tone at BANK A..D / PTCH 1..16 (whatever the banks hold, as Melodee's
 * cz_factory_loaded), a user slot of no tone its BANK / PTCH from the banks (cz_bank.c) */
static void cz_track_loaded(track_t *t)
{
    uint32_t tr = (uint32_t)(t - trk), pend = cz_user_pending, k;
    uint8_t raw[CZ_BYTES];
    cz_user_pending = 0;
    if (tr >= NTRK || t->eng_req != ENGI_CZ)
        return;
    k = (uint32_t)t->p[P_E0] * 16u + (uint32_t)t->p[P_E1];
    if (pend && cz_store_read && !cz_store_read(pend - 1u, raw) && cz_patch_valid(raw)) {
        /* (the slot's own tone) */
    } else if (!t->p[P_E1] || t->p[P_E7] != CZ_NATIVE) {
        cz_patch_init(raw);
    } else if (!pend && k - 1u < CZ_FACTORY_N) {
        cz_factory_tone(k, raw);
    } else if (!cz_user_bank_read || cz_user_bank_read((uint32_t)t->p[P_E0], (uint32_t)t->p[P_E1] - 1u, raw)) {
        cz_patch_init(raw);                       /* (an empty bank slot) */
    }
    cz_tone_put(tr, raw);
    cz_track_accept(t);
}

/* the blob: the tone as it is (144 bytes: 128 synthesis bytes + the 16-character LCD name) */
static void cz_blob_get(const track_t *t, uint8_t *out)
{
    uint32_t tr = (uint32_t)(t - trk);
    if (tr < NTRK)
        memcpy(out, cz_patch[tr].raw, CZ_BYTES);
    else
        cz_patch_init(out);
}
static void cz_blob_set(track_t *t, const uint8_t *in)    /* 0 or a bad blob: the init tone */
{
    uint8_t raw[CZ_BYTES];
    uint32_t tr = (uint32_t)(t - trk);
    if (tr >= NTRK)
        return;
    if (in && cz_patch_valid(in))
        memcpy(raw, in, CZ_BYTES);
    else
        cz_patch_init(raw);
    cz_tone_put(tr, raw);
    cz_track_accept(t);                           /* (cz_bank_poll: the tone stays) */
}
static void cz_blob_preset(track_t *t, uint32_t k)        /* factory preset k: its tone */
{
    uint8_t raw[CZ_BYTES];
    uint32_t tr = (uint32_t)(t - trk);
    if (tr >= NTRK || k >= NELEM(CZ_PRESETS))
        return;
    cz_factory_tone(k, raw);
    cz_tone_put(tr, raw);
    cz_track_accept(t);
}

/* ------------------------------------------------- ChoralRoot: deep pages --- */
/* The sound editor's sections (cr_edit.c builds its screens from these titles, docs/EDITOR.md, docs/CZ1.md):
 *   OSC     LINE 1, LINE 2      WAVE WAVE2 WINDOW LEVEL           (a stack of the two lines; the mixer: LEVEL)
 *           DETUNE              SIGN OCT NOTE FINE                (line 2's detune)
 *   FILTER  DCW, DCW+           W.KEY1 W.KEY2 V.WAV1 V.WAV2 / A.KEY1 A.KEY2 V.AMP1 V.AMP2 (key follow, velocity)
 *   ENV     PITCH n, DCW n, DCA n (n = the line): five pages each: R1..R4, L1..L4 (n+), R5..R8, L5..L8 (n B, n B+),
 *           SUS END (n S; PITCH: + V.PIT); three screens an envelope under the "cz" band (cr_edit.c ce_is_cz)
 *   LFO     VIB                 WAVE DELAY RATE DEPTH             (the vibrato)
 *   MOD     TONE                LINE MOD OCT                      (line select, RING / NOISE on line 2, octave)
 * A column is a panel value of cz_edit.h (LCZ_* ids): get decodes the tone, set writes back only the bytes whose
 * encoding changed (cz_ed_put: an imported tone keeps its exact rates and levels until that value is turned). No
 * matrix destination (mod_dst -1). */
static const char *const N_CZD_SIGN[] = {"+", "-"};
#define CZD_(l, f, mn, mx, df, n) {l, f, mn, mx, df, n, 0}
#define CZD_NONE {0, 0, 0, 0, 0, 0, 0}
#define CZD_LINE {CZD_("WAVE", F_ENUM, 0, 7, 0, CZ_CARRIERS), CZD_("WAVE2", F_ENUM, 0, 8, 0, CZ_CARRIERS2), \
                  CZD_("WINDOW", F_ENUM, 0, 7, 0, CZ_WINDOWS), CZD_("LEVEL", F_INT, 1, 15, 15, 0)}
#define CZD_R(a, b, c, d) {CZD_(a, F_INT, 0, 99, 70, 0), CZD_(b, F_INT, 0, 99, 70, 0), CZD_(c, F_INT, 0, 99, 70, 0), \
                           CZD_(d, F_INT, 0, 99, 70, 0)}
#define CZD_L(a, b, c, d) {CZD_(a, F_INT, 0, 99, 0, 0), CZD_(b, F_INT, 0, 99, 0, 0), CZD_(c, F_INT, 0, 99, 0, 0), \
                           CZD_(d, F_INT, 0, 99, 0, 0)}
#define CZD_SE(...) {CZD_("SUS", F_ENUM, 0, 8, 1, CZ_STAGES), CZD_("END", F_ENUM, 0, 7, 2, CZ_STAGES), __VA_ARGS__, CZD_NONE}
#define CZD_ENV(t, ...) {t, CZD_R("R1", "R2", "R3", "R4")}, {t "+", CZD_L("L1", "L2", "L3", "L4")}, \
                      {t " B", CZD_R("R5", "R6", "R7", "R8")}, {t " B+", CZD_L("L5", "L6", "L7", "L8")}, \
                      {t " S", CZD_SE(__VA_ARGS__)}
static const eng_page_t CZ_PAGES[] = {
    {"LINE 1", CZD_LINE}, {"LINE 2", CZD_LINE},                                                       /* 0 OSC */
    {"DETUNE", {CZD_("SIGN", F_ENUM, 0, 1, 0, N_CZD_SIGN), CZD_("OCT", F_INT, 0, 3, 0, 0),
                CZD_("NOTE", F_INT, 0, 11, 0, 0), CZD_("FINE", F_INT, 0, 60, 0, 0)}},
    {"DCW", {CZD_("W.KEY1", F_INT, 0, 9, 0, 0), CZD_("W.KEY2", F_INT, 0, 9, 0, 0),                  /* 3 FILTER */
             CZD_("V.WAV1", F_INT, 0, 15, 0, 0), CZD_("V.WAV2", F_INT, 0, 15, 0, 0)}},
    {"DCW+", {CZD_("A.KEY1", F_INT, 0, 9, 0, 0), CZD_("A.KEY2", F_INT, 0, 9, 0, 0),
              CZD_("V.AMP1", F_INT, 0, 15, 0, 0), CZD_("V.AMP2", F_INT, 0, 15, 0, 0)}},
    CZD_ENV("PITCH 1", CZD_("V.PIT", F_INT, 0, 15, 0, 0)), CZD_ENV("DCW 1", CZD_NONE),             /* 5 ENV */
    CZD_ENV("DCA 1", CZD_NONE),
    CZD_ENV("PITCH 2", CZD_("V.PIT", F_INT, 0, 15, 0, 0)), CZD_ENV("DCW 2", CZD_NONE),
    CZD_ENV("DCA 2", CZD_NONE),
    {"VIB", {CZD_("WAVE", F_ENUM, 0, 3, 0, CZ_VWAVES), CZD_("DELAY", F_INT, 0, 99, 0, 0),             /* 35 LFO */
             CZD_("RATE", F_INT, 0, 99, 50, 0), CZD_("DEPTH", F_INT, 0, 99, 0, 0)}},
    {"TONE", {CZD_("LINE", F_ENUM, 0, 3, 2, CZ_LINES), CZD_("MOD", F_ENUM, 0, 2, 0, CZ_MODES),       /* 36 MOD */
              CZD_("OCT", F_OFS, 0, 2, 1, 0), CZD_NONE}},
};
#undef CZD_
#undef CZD_NONE
#undef CZD_LINE
#undef CZD_R
#undef CZD_L
#undef CZD_SE
#undef CZD_ENV
enum { CZ_PG_LINE = 0, CZ_PG_DCW = 3, CZ_PG_ENV = 5, CZ_PG_VIB = 35, CZ_PG_TONE = 36 };
_Static_assert(NELEM(CZ_PAGES) == 37u, "CZ-1 pages");

/* page column -> its panel value (LCZ_*), -1 none */
static int32_t cz_dref(uint32_t page, uint32_t col)
{
    static const uint8_t DET[4] = {LCZ_SIGN, LCZ_DOCT, LCZ_NOTE, LCZ_FINE};
    static const uint8_t KV[2][2] = {{LCZ_KW, LCZ_VW}, {LCZ_KA, LCZ_VA}};
    static const uint8_t VIB[4] = {LCZ_VWAVE, LCZ_VDELAY, LCZ_VRATE, LCZ_VDEP};
    static const uint8_t TONE[3] = {LCZ_LINE, LCZ_MOD, LCZ_OCT};
    static const uint8_t LINE[4] = {LCZ_W1, LCZ_W2, 0xFF, LCZ_LEVEL};
    uint32_t k, l, e, q;
    if (page >= NELEM(CZ_PAGES) || col >= 4u || !CZ_PAGES[page].col[col].label)
        return -1;
    if (page < 2u)
        return LINE[col] == 0xFFu ? (int32_t)LCZ_WIN(page) : (int32_t)(LCZ_LBASE(page) + LINE[col]);
    if (page == 2u)
        return DET[col];
    if (page < CZ_PG_ENV)                                /* DCW: key follow (cols 0, 1), velocity (2, 3) of line col & 1 */
        return (int32_t)(LCZ_LBASE(col & 1u) + KV[page - CZ_PG_DCW][col >> 1]);
    if (page < CZ_PG_VIB) {
        k = page - CZ_PG_ENV;
        l = k / 15u;                                     /* the line, the envelope (0 pitch, 1 DCW, 2 DCA), its page */
        e = k / 5u % 3u;
        q = k % 5u;
        if (q < 4u)                                      /* R1..4, L1..4, R5..8, L5..8 */
            return (int32_t)(LCZ_EBASE(l, e) + (q & 2u ? 4u : 0u) + (q & 1u ? 8u : 0u) + col);
        return col < 2u ? (int32_t)(LCZ_EBASE(l, e) + 16u + col) : (int32_t)(LCZ_LBASE(l) + LCZ_VP);
    }
    return page == CZ_PG_VIB ? VIB[col] : col < 3u ? TONE[col] : -1;
}

static int32_t cz_dget(const track_t *t, uint32_t page, uint32_t col)
{
    uint32_t tr = (uint32_t)(t - trk);
    int32_t id = cz_dref(page, col);
    uint8_t p[LCZ_PACKED];
    if (id < 0)
        return 0;
    if (tr >= NTRK)
        return CZ_PAGES[page].col[col].def;
    cz_ed_decode(p, cz_patch[tr].raw);
    return p[id];
}

static void cz_dset(track_t *t, uint32_t page, uint32_t col, int32_t val)
{
    uint32_t tr = (uint32_t)(t - trk);
    int32_t id = cz_dref(page, col);
    uint8_t raw[CZ_BYTES];
    const param_desc_t *d;
    if (id < 0 || tr >= NTRK)
        return;
    d = &CZ_PAGES[page].col[col];
    val = clamp(val, d->min, d->max);
    if (cz_ed_put(tr, (uint32_t)id, (uint32_t)val, raw)) {
        memcpy(cz_patch[tr].raw, raw, 128u);             /* (the synthesis bytes; the name stays) */
        RING_PUBLISH();
    }
}

static int32_t cz_mod_dst(const track_t *t, uint32_t page, uint32_t col)   /* CZ-1 has no matrix */
{
    (void)t;
    (void)page;
    (void)col;
    return -1;
}

static const eng_deep_t CZ_DEEP = {
    .npages = NELEM(CZ_PAGES),
    .pages = CZ_PAGES,
    .section = {CZ_PG_LINE, CZ_PG_DCW, CZ_PG_ENV, CZ_PG_VIB, CZ_PG_TONE, 0xFF, 0xFF, 0xFF},
    .get = cz_dget,
    .set = cz_dset,
    .blob_size = CZ_BYTES,
    .blob_get = cz_blob_get,
    .blob_set = cz_blob_set,
    .blob_preset = cz_blob_preset,
    .mod_dst = cz_mod_dst,
};
_Static_assert(CZ_BYTES <= ENG_BLOB_MAX, "the CZ-1 tone fits the blob cap");

static const engine_t ENG_CZ = {
    .name = "CZ-1", .page_title = {"CZ-1", "TONE"},
    .edit = {
        {"BANK", F_ENUM, 0, 7, 0, N_CZ_BANK, 0}, {"PTCH", F_INT, 0, 16, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0}, {"-", F_INT, 0, 0, 0, 0, 0},
        {"-", F_INT, 0, 0, 0, 0, 0}, {"TONE", F_INT, CZ_NATIVE, CZ_NATIVE, CZ_NATIVE, 0, 0},
    },
    .presets = CZ_PRESETS, .npresets = NELEM(CZ_PRESETS),
    .ownenv = 1, .done = cz_native_done, .keep = 0x0fu,
    .note_on = cz_note_on, .render = cz_native_render,
    .knob = {P_E0, P_E1, P_E7, P_E7},
#ifdef CZ_POLY
    .poly = CZ_POLY,
#endif
    .deep = &CZ_DEEP,
};

/* USB interrupt only collects bytes; the main loop validates and publishes (cz_store.c cz_service). */
#define CZ_RX 296u
static uint8_t cz_rx[CZ_RX] __attribute__((section(".pool")));
static uint8_t cz_rx_on,cz_rx_req,cz_rx_ready,cz_rx_go,cz_rx_abort;
static uint16_t cz_rx_n;
static void cz_sx_byte(uint8_t b)
{
    if(b>=0xf8)return;
    if(b==0xf0){if(cz_rx_ready)return;cz_rx_on=1;cz_rx_n=0;cz_rx_req=cz_rx_go=0;}
    if(!cz_rx_on||cz_rx_ready)return;
    if((b&128)&&b!=0xf0&&b!=0xf7){cz_rx_on=cz_rx_req=cz_rx_go=0;cz_rx_abort=1;return;}
    if(cz_rx_n>=CZ_RX){cz_rx_on=cz_rx_req=cz_rx_go=0;cz_rx_abort=1;return;}
    cz_rx[cz_rx_n++]=b;
    if(cz_rx_n==2&&b!=0x44){cz_rx_on=0;return;}
    if(cz_rx_n==7 && cz_rx[2]==0 && cz_rx[3]==0 && (cz_rx[4]&0xf0)==0x70){RING_PUBLISH();cz_rx_req=1;}
    if(cz_rx_n==9&&(cz_rx[5]==0x10||cz_rx[5]==0x11)&&cz_rx[7]==cz_rx[4]&&b==0x31){RING_PUBLISH();cz_rx_go=1;}
    if(b==0xf7){cz_rx_on=0;RING_PUBLISH();cz_rx_ready=1;}
}
