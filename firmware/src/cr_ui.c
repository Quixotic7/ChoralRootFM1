/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 ChoralRoot FM-1 contributors (a fork of Felucca) */
/* ChoralRoot FM-1: the instrument's UI. The input grammar (docs/INTEGRATION.md section 3, PLAN.md sections 3-4),
 * the view-model builder (section 5: cr_build_screen) and the LEDs (section 6: cr_leds). See cr_ui.h.
 *
 * Input: one scan of fm1_in (keys, buttons) + the HAL's edges + the encoders (panel.c panel_enc), as Felucca's
 * ui_input.c does; its LED composition (led_pos, led_put) and its tap / hold gesture (ui_layer.c layer_gesture:
 * a release before HOLD_MS with nothing else touched is a tap; held past HOLD_MS, or a key / knob / button
 * touched meanwhile, the layer opens) are copied here in ChoralRoot's terms, not included: no Felucca page is ever
 * drawn. The grammar (design/choralroot-fm1-mockups.json, third pass): a layer button (KEY = printed SEL, PERF,
 * FX, BASS, LOOP, METRO; EDIT held = the engine picker) held past HOLD_MS opens its layer and LOCKS it (cu.lock):
 * it stays open after release, its LED blinks, OCT- (back) or HOME closes it, holding another layer button
 * switches to that one, a tap is still the on/off action. SAVE held (the loop slots) and OPT (the knob shift) are
 * momentary. B3 = LOCK, the chord block's latch mode (cu_mod_press: a virtual hold of the latched keys).
 * Output: events for the engine (cr_out.c cr_post, drained by the audio ISR), Felucca's part parameters (sounds,
 * sends, levels: written as Felucca's UI writes them), fm1_led / fm1_led_dim, and one cr_screen_t per frame for
 * cr_draw.c.
 *
 * Sounds (docs/PRESETS.md): PRESETS / ALGORITHM turn through the pool of the part's engine (cr_bank.c: INIT, the
 * engine's factory presets, each replaced by the user's record bound to it, the presets the user added; the records are
 * upreset.c's 32 slots); OPT + PRESETS changes the chord part's engine; EDIT opens the sound pages (cr_pages.c), EDIT
 * held the engine picker, SAVE the save dialog (Overwrite / Save as new, then the naming screen, cr_name.c).
 * The looper (cr_loop.c, docs/LOOPER.md): LOOP / REC / METRO and their layers, SAVE held (the loop slots in flash),
 * the corner dial (the count-in's and undo's ring) and the transport's top line.
 * Included after cr_out.c, cr_anim.c, gfx.c, cr_gfx.c, cr_draw.c, panel.c, cr_bank.c, cr_pages.c and cr_name.c. */
#include "cr_ui.h"

/* ---------------------------------------------------------------- roles --- */
/* the printed buttons in ChoralRoot's roles (PLAN.md section 3) */
#define BT_KEY B_SCL                              /* printed SEL */
#define BT_PERF B_ARP
#define BT_FX B_FX
#define BT_BASS B_ENV
#define BT_LATCH B_LFO
#define BT_OPT B_GLO
#define BT_EDIT B_EDIT                             /* printed EDIT */
#define BT_HOME B_HOME
#define BT_SAVE B_SAVE
#define BT_METRO B_SEQ
#define BT_LOOP B_PLAY
#define BT_REC B_REC
#define CU_BIT(b) (1u << (b))

/* layers (a button held): their screens and the job of the root keys and KNOB 1..4 */
enum { L_NONE, L_KEY, L_PERF, L_FX, L_BASS, L_EDIT, L_SAVE, L_METRO, L_LOOP, L_N };
static const uint8_t L_BTN[L_N] = {NB, BT_KEY, BT_PERF, BT_FX, BT_BASS, BT_EDIT, BT_SAVE, BT_METRO, BT_LOOP};
static uint32_t cu_layer_of(uint32_t b)
{
    uint32_t l;
    for (l = 1; l < L_N; l++)
        if (L_BTN[l] == b)
            return l;
    return L_NONE;
}

/* the chord block: firmware key k (0 = F3) -> cr_mod_t, 0xFF = none (B3) */
static const uint8_t CU_MOD_OF_KEY[9] = {CR_MOD_6, CR_MOD_DIM, CR_MOD_M7, CR_MOD_MIN, CR_MOD_MAJ7, CR_MOD_MAJ, 0xFFu,
                                         CR_MOD_9, CR_MOD_SUS};
#define CU_ROOT0 9u                               /* D4: the first root key */
#define CU_NKEY 27u
static int cu_black(uint32_t k) { return (int)((0x54Au >> ((k + 5u) % 12u)) & 1u); }
/* a root key's place among the white root keys (D4 = 0 .. G5 = 10), -1 for a black one */
static int32_t cu_white_idx(uint32_t k)
{
    uint32_t i, n = 0;
    if (k < CU_ROOT0 || k >= CU_NKEY || cu_black(k))
        return -1;
    for (i = CU_ROOT0; i < k; i++)
        n += !cu_black(i);
    return (int32_t)n;
}
static uint32_t cu_white_key(uint32_t idx)        /* the inverse: white root idx -> key, CU_NKEY = none */
{
    uint32_t k;
    for (k = CU_ROOT0; k < CU_NKEY; k++)
        if (cu_white_idx(k) == (int32_t)idx)
            return k;
    return CU_NKEY;
}

/* -------------------------------------------------------------- strings --- */
static void cu_cpy(char *d, const char *s, uint32_t n)
{
    uint32_t i = 0;
    if (!n)
        return;
    while (s && s[i] && i + 1u < n) {
        d[i] = s[i];
        i++;
    }
    d[i] = 0;
}
static void cu_cat(char *d, const char *s, uint32_t n)
{
    uint32_t i = 0;
    while (i < n && d[i])
        i++;
    if (i < n)
        cu_cpy(d + i, s, n - i);
}
static void cu_int(char *d, int32_t v, int plus, uint32_t n)   /* decimal, "+3" with plus */
{
    char b[12];
    uint32_t i = 0, j = 0, u = (uint32_t)(v < 0 ? -v : v);
    do {
        b[i++] = (char)('0' + u % 10u);
        u /= 10u;
    } while (u && i < 10u);
    if (v < 0)
        b[i++] = '-';
    else if (plus && v > 0)
        b[i++] = '+';
    while (i && j + 1u < n)
        d[j++] = b[--i];
    d[j] = 0;
}
static void cu_2d(char *d, uint32_t v, uint32_t n)             /* "07", "13", "120" */
{
    char b[12];
    cu_int(b, (int32_t)v, 0, sizeof b);
    d[0] = 0;
    if (v < 10u)
        cu_cpy(d, "0", n);
    cu_cat(d, b, n);
}
static int cu_eq(const char *a, const char *b)
{
    while (*a && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}
static int cu_has(const char *s, const char *w)                /* w occurs in s */
{
    uint32_t i, j;
    for (i = 0; s[i]; i++) {
        for (j = 0; w[j] && s[i + j] == w[j]; j++)
            ;
        if (!w[j])
            return 1;
    }
    return 0;
}

static const char *const CU_NOTE[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

/* --------------------------------------------------------------- sounds --- */
/* PRESETS turns through the pool of the chord part's engine (cr_bank.c pool_*), ALGORITHM the bass part's (position 0
 * = OFF, then the pool). Each part remembers its sound's name, whether it was edited since (the sound pages, the
 * engine picker, a SysEx patch), and the pool entry it came from (kind PK_INIT / PK_FACTORY / PK_USER, the factory
 * preset fk, the user slot): its place in the pool follows from it (cu_part_pos), and SAVE's Overwrite writes over it
 * even once edited (trk[].user, the slot the sound is as stored, is 0 then). */
typedef struct { char name[13]; uint8_t edited, kind, fk, slot; } cu_snd_t;
static cu_snd_t psnd[2];
static void cu_snd_set(uint32_t part, uint32_t kind, uint32_t fk, uint32_t slot)
{
    psnd[part].kind = (uint8_t)kind;
    psnd[part].fk = (uint8_t)fk;
    psnd[part].slot = (uint8_t)slot;
    psnd[part].edited = 0;
}

static uint32_t cu_preset_orig(const engine_t *e, uint32_t k)  /* ui.c preset_orig: a retired alias -> the original */
{
#if FELUCCA_SAMPLE
    return e->presets == SMP_PRESET_TABLE && k < SMP_NSETS ? SMP_SET_ORIG[k] : k;
#else
    (void)e;                                     /* (the aliases were SAMPLE's sets: none) */
    return k;
#endif
}
/* the EDIT engine picker: pitched engines with presets (not DRUM, not SLICE, not the retired DIGITAL slot) */
static int cu_engine_melodic(uint32_t e)
{
#ifdef ENGI_SLICE
    if (e == ENGI_SLICE) return 0;
#endif
    return eng_ok(e) && e != ENGI_DRUM && ENGINES[e]->npresets;
}
static const char *cu_preset_name(uint32_t e, uint32_t p)
{
    const engine_t *en = ENGINES[e % NENGINES];
    return en->npresets ? en->presets[p % en->npresets].name : en->name;
}
static int cu_kept(uint32_t i) { return param_kept(i); }

/* engine e's preset pi into part t, as ui.c set_engine_of + apply_preset_to: the sound only, its sends; the part's
 * notes are released (panic_req: cr_out.c events_block, the audio side). bass: P_VOICE MONO, else POLY (a chord on a
 * mono preset) */
static void cu_load(track_t *t, uint32_t e, uint32_t pi, int bass)
{
    static const uint8_t FX_DEF[4] = {0, 24, 28, 36};
    const engine_t *en = ENGINES[e % NENGINES];
    const preset_t *pr;
    uint32_t i;
    if (!en->npresets)
        return;
    pi = cu_preset_orig(en, pi % en->npresets);
    pr = &en->presets[pi];
    fm1_irq_off();
    t->eng_req = (uint8_t)(e % NENGINES);
    t->preset = (uint8_t)pi;
    t->user = 0;
    t->trim = 0;                                  /* (a loud preset's: cu_pool_load, cr_bank.c cb_trim) */
    for (i = 0; i < P_E0; i++)
        if (!cu_kept(i))
            t->p[i] = TP[i].def;
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = (int16_t)pr->e[i];
    t->p[P_ATK] = pr->env[0];
    t->p[P_DEC] = pr->env[1];
    t->p[P_SUS] = pr->env[2];
    t->p[P_REL] = pr->env[3];
    t->p[P_ED_FLT] = pr->fenv;
    t->p[P_VOICE] = bass ? V_MONO : V_POLY;
    for (i = 0; i < 4u; i++)
        t->p[P_DIST + i] = (int16_t)(pr->fx[i] ? pr->fx[i] - 1 : FX_DEF[i]);
    fm6_track_loaded(t);
    panic_req |= (uint8_t)(1u << (uint32_t)(t - trk));
    fm1_irq_on();
    if (t - trk < 2) {
        str_cpy(psnd[t - trk].name, pr->name, sizeof psnd[0].name);
        cu_snd_set((uint32_t)(t - trk), pool_f0(e % NENGINES) > pi ? PK_INIT : PK_FACTORY, pi, PF_NONE);
    }
}

/* user slot k into part t (upreset.c up_load without Felucca's undo / pattern): engine and every parameter but
 * the part's own (param_kept); a chord part plays it POLY when it was saved MONO, a bass part MONO. An ANALOG sound
 * (engine 0, retired in 0.14: a 0.13 slot, a restored bank) loads on the VA with its macros carried by name (cu_analog_va;
 * the VA's init patch under them, eng_va.c va_track_loaded); a sound of another retired engine (SAMPLE 4, GRAIN 8,
 * DRUM 10: a Felucca slot; engines.c ENG_GONE) loads as the VA's init sound. Both under the slot's name, with a
 * message (and a trace line on the emulator) */
static void cu_message(const char *t, uint32_t col);
#if !FELUCCA_ANALOG
/* ANALOG's values -> the VA's: CUT RES DRV MIX DTN to the VA's macros of the same name, the filter envelope (ENV
 * DEST FLT) to FENV, the envelope's ATK / REL to the VA's ATK / REL macros (its own envelope); ANALOG's WAVE, NOIS
 * and KTR have no VA macro (the init patch's two saws) */
static void cu_analog_va(int16_t *v)
{
    int16_t a[8];
    uint32_t i;
    for (i = 0; i < 8u; i++)
        a[i] = v[P_E0 + i];                       /* WAVE DTN MIX NOIS CUT RES DRV KTR */
    v[P_E0 + 0] = a[4];                           /* CUT */
    v[P_E0 + 1] = a[5];                           /* RES */
    v[P_E0 + 2] = (int16_t)clamp(v[P_ED_FLT], -64, 63);   /* FENV */
    v[P_E0 + 3] = a[6];                           /* DRIVE */
    v[P_E0 + 4] = a[2];                           /* MIX */
    v[P_E0 + 5] = a[1];                           /* DTN */
    v[P_E0 + 6] = v[P_ATK];                       /* ATK */
    v[P_E0 + 7] = v[P_REL];                       /* REL */
    v[P_ED_FLT] = 0;                              /* (the VA's presets: FENV is the macro) */
    for (i = 0; i < 8u; i++)
        v[P_E0 + i] = (int16_t)clamp(v[P_E0 + i], ENG_VA.edit[i].min, ENG_VA.edit[i].max);
}
#endif
static void cu_load_user(track_t *t, uint32_t k, int bass)
{
    const up_rec_t *r;
    int16_t v[P_COUNT];
    uint32_t i;
    if (!up_used(k))
        return;
    r = up_rec(k);
    up_values(r, v);
    if (bass && v[P_VOICE] != V_MONO && v[P_VOICE] != V_LEGATO)
        v[P_VOICE] = V_MONO;
    if (!bass && (v[P_VOICE] == V_MONO || v[P_VOICE] == V_LEGATO))
        v[P_VOICE] = V_POLY;
    panic_req |= (uint8_t)(1u << (uint32_t)(t - trk));
#if !FELUCCA_FM4
    if (r->engine == ENGI_DIGITAL) {
        int16_t p[P_COUNT];
        for (i = 0; i < P_COUNT; i++)
            p[i] = cu_kept(i) ? t->p[i] : v[i];
        fm4_apply(t, p);
    } else
#endif
#if !FELUCCA_ANALOG
    if (r->engine == 0u) {                        /* ANALOG (retired): its macros on the VA */
#if defined(CR_TRACE) && CR_TRACE
        printf("load: slot %u engine 0 (ANALOG) retired -> VA, its macros\n", (unsigned)k + 1u);
#endif
        cu_analog_va(v);
        fm1_irq_off();
        t->eng_req = ENGI_VA;
        for (i = 0; i < P_COUNT; i++)
            if (!cu_kept(i))
                t->p[i] = v[i];
        t->preset = 0;
        fm1_irq_on();
        fm6_track_loaded(t);                      /* (no stored VA patch: the init patch + these macros) */
        cu_message("ANALOG retired: on the VA", CR_COL_WHITE);
    } else
#endif
    if (!eng_ok(r->engine)) {
        const engine_t *en = ENGINES[CB_RETIRED_POOL];
#if defined(CR_TRACE) && CR_TRACE
        printf("load: slot %u engine %u retired -> INIT on %s\n", (unsigned)k + 1u, (unsigned)r->engine, en->name);
#endif
        fm1_irq_off();
        t->eng_req = CB_RETIRED_POOL;
        for (i = 0; i < P_E0; i++)
            if (!cu_kept(i))
                t->p[i] = TP[i].def;
        for (i = 0; i < 8u; i++)
            t->p[P_E0 + i] = en->edit[i].def;
        t->p[P_VOICE] = bass ? V_MONO : V_POLY;
        t->preset = 0;
        fm1_irq_on();
        fm6_track_loaded(t);
        cu_message("engine retired: init sound", CR_COL_WHITE);
    } else {
        fm1_irq_off();
        t->eng_req = r->engine;
        for (i = 0; i < P_COUNT; i++)
            if (!cu_kept(i))
                t->p[i] = v[i];
        t->preset = 0;
        fm1_irq_on();
        fm6_track_loaded(t);
    }
    t->user = (uint8_t)(k + 1u);
    t->trim = 0;                                  /* an added user preset: no trim (a bound one: cu_pool_load) */
    if (t - trk < 2) {
        up_name(k, psnd[t - trk].name);
        cu_snd_set((uint32_t)(t - trk), PK_USER, cb_bound(k), k);
    }
}

/* the init sound of engine e into the part (the pool's position 0): the engine's parameter defaults, the part's own
 * kept (cu_kept), MONO on the bass, POLY on the chord part, no trim */
static void cu_init_load(uint32_t part, uint32_t e)
{
    track_t *t = &trk[part ? CR_PART_BASS : CR_PART_CHORD];
    const engine_t *en = ENGINES[e % NENGINES];
    uint32_t i;
    fm1_irq_off();
    t->eng_req = (uint8_t)(e % NENGINES);
    t->preset = 0;
    t->user = 0;
    for (i = 0; i < P_E0; i++)
        if (!cu_kept(i))
            t->p[i] = TP[i].def;
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = en->edit[i].def;
    t->p[P_VOICE] = part ? V_MONO : V_POLY;
    t->trim = 0;
    panic_req |= (uint8_t)(1u << (uint32_t)(t - trk));
    fm1_irq_on();
    fm6_track_loaded(t);
    str_cpy(psnd[part].name, "INIT", sizeof psnd[0].name);
    cu_snd_set(part, PK_INIT, PF_NONE, PF_NONE);
}

/* the part's place in its engine's pool (cr_bank.c), from the entry its sound came from (0: INIT, or a user preset
 * no longer in the pool) */
static uint32_t cu_part_pos(uint32_t part)
{
    uint32_t e = trk[part ? CR_PART_BASS : CR_PART_CHORD].eng_req % NENGINES;
    switch (psnd[part].kind) {
    case PK_USER: return pool_pos_slot(e, psnd[part].slot);
    case PK_FACTORY: return pool_pos_factory(e, psnd[part].fk);
    default: return 0u;
    }
}
/* PRESETS' and ALGORITHM's positions from the parts' sounds (after a load, a save, a delete: the pools moved), and the
 * per-engine memory of each part (cs.pool_pos: OPT + PRESETS lands there). (cs is defined below) */
static void cu_pos_sync(void);

/* pool position pos of engine e into the part (0 chord, 1 bass): INIT, a factory preset (its trim), or a user preset
 * (a bound one: its factory preset's trim) */
static void cu_pool_load(uint32_t part, uint32_t e, uint32_t pos)
{
    track_t *t = &trk[part ? CR_PART_BASS : CR_PART_CHORD];
    pool_ent_t en;
    e %= NENGINES;
    en = pool_entry(e, pos);
    if (en.kind == PK_USER) {
        cu_load_user(t, en.slot, (int)part);
        if (en.fk != PF_NONE)
            t->trim = cb_trim(e, en.fk);
    } else if (en.kind == PK_FACTORY) {
        cu_load(t, e, en.fk, (int)part);
        t->trim = cb_trim(e, en.fk);
    } else {
        cu_init_load(part, e);
    }
    cu_pos_sync();
}
/* position pos of the part's own pool (PRESETS: the chord part, bass 0; ALGORITHM's: bass 1, pos 0-based) */
static void cu_list_load(int bass, uint32_t pos)
{
    cu_pool_load(bass ? 1u : 0u, trk[bass ? CR_PART_BASS : CR_PART_CHORD].eng_req, pos);
}

/* ------------------------------------------------------------- settings --- */
/* the UI's mirror of the engine's settings (the engine is the audio ISR's: every change is posted) and the
 * instrument's own. TODO (INTEGRATION section 7): cr_settings.c, persisted in Felucca's settings record */
static const struct { const char *name, *short_name; uint8_t mode, range; } CU_PERF[7] = {
    {"Strum", "Strum", CR_PM_STRUM, 1}, {"Strum 2 Octaves", "Strum", CR_PM_STRUM, 2}, {"Slop", "Slop", CR_PM_SLOP, 0},
    {"Arpeggiate", "Arp", CR_PM_ARP, 1}, {"Arp 2 Octaves", "Arp", CR_PM_ARP, 2}, {"Pattern", "Pattern", CR_PM_PATTERN, 0},
    {"Harp", "Harp", CR_PM_HARP, 0}};
/* the four knobs of each perform mode's layer; the first is KNOB 3's main parameter on the view */
static const int8_t CU_PERF_KNOB[CR_PM_COUNT][4] = {
    {CR_P_RATE, CR_P_DIR, CR_P_RANGE, CR_P_HOLD},                 /* STRUM */
    {CR_P_AMOUNT, CR_P_RATE, CR_P_DIR, CR_P_RANGE},               /* SLOP */
    {CR_P_DIV, CR_P_DIR, CR_P_GATE, CR_P_SWING},                  /* ARP */
    {CR_P_PATTERN, CR_P_DIV, CR_P_GATE, CR_P_SWING},              /* PATTERN */
    {CR_P_RATE, CR_P_DIR, CR_P_GATE, CR_P_RANGE}};                /* HARP */
static const struct { const char *name; int16_t min, max, step; } CU_PAR[CR_P_COUNT] = {
    {"rate", 1, 1000, 5}, {"division", 0, CR_DIV_COUNT - 1, 1}, {"direction", 0, 5, 1}, {"range", 1, 4, 1},
    {"gate", 1, 200, 5}, {"swing", 50, 90, 1}, {"retrigger", 0, 1, 1}, {"pattern", 1, CR_NPATTERN, 1},
    {"rotate", -12, 12, 1}, {"amount", 0, 100, 5}, {"hold", 0, 1, 1}};
static const char *const CU_DIV[CR_DIV_COUNT] = {"2/1", "1/1", "1/2", "1/2T", "1/4", "1/4T", "1/8", "1/8T", "1/16",
                                                 "1/16T", "1/32", "1/32T"};
static const char *const CU_DIR[6] = {"up", "down", "up-down", "down-up", "as played", "random"};

/* FX: the chord part's sends (KNOB 4: the amount) and the shared buses' parameters (KNOB 1..3, song.g) */
#define CU_NFX 4u
static const struct { const char *name; uint8_t send; int8_t g[3]; const char *gname[3]; } CU_FX[CU_NFX] = {
    {"Reverb", P_REV, {G_RSIZE, G_RDAMP, G_RTYPE}, {"size", "damp", "type"}},
    {"Chorus", P_CHOR, {G_CRATE, G_CDEPTH, -1}, {"rate", "depth", ""}},
    {"Delay", P_DLY, {G_DTIME, G_DFDBK, G_DCOLOR}, {"time", "feedback", "colour"}},
    {"Drive", P_DIST, {-1, -1, -1}, {"", "", ""}}};
static const char *const CU_BASSMODE[4] = {"Chords Only", "Unison Bass", "Bass Single Notes", "Solo"};
static const char *const CU_VIEW[5] = {"Chord", "Keyboard", "Notes", "Geek Out", "Scope"};
static const char *const CU_LOOPLEN[6] = {"Free", "1 bar", "2 bars", "4 bars", "8 bars", "16 bars"};
static const char *const CU_SIG[CRL_NSIG] = {"4/4", "3/4", "6/8"};
static const char *const CU_QUANT[CRL_NQUANT] = {"none", "1/4", "1/8", "1/8T", "1/16", "1/16T", "1/32"};
static const char *const CU_LOOP_MODE[CRL_NMODE] = {"Overwrite", "Advance", "Overdub", "Replace", "Step"};
static const char *const CU_SAVE_ACT[3] = {"Save", "Load", "Delete"};
enum { V_CHORD, V_KEYBOARD, V_NOTES, V_GEEK, V_SCOPE, V_N };

static struct {
    uint8_t playstyle, extadd, secret, key_on, tonic, scale, single, vel;
    int8_t transpose;
    uint8_t bass_on, bass_mode, perform_on, perf_sel, sticky;
    uint16_t bpm;
    int16_t par[CR_PM_COUNT][CR_P_COUNT];
    uint8_t fx_on, fx_sel, fx_amt[CU_NFX];
    uint16_t sound, bass_sound;           /* pool positions: PRESETS (the chord part's), ALGORITHM (0 = OFF, pos + 1) */
    uint8_t pool_pos[2][NENG_SHOWN];      /* per part, per engine (ENGINE_ORDER rank): the pool position last played
                                           * (OPT + PRESETS lands there; Settings v6) */
    uint8_t view, leds;
    uint8_t ch[CR_NSTREAM];               /* MIDI channel 1..16, 0 = off */
    uint8_t raw_sound;                    /* RAW also plays part 0 */
    uint8_t split;                        /* Single Notes' split point, pitch class 0..11 */
    uint8_t metro, metro_sig, metro_vol;  /* the click: on, CRL_SIG_*, level 0..100 */
    uint8_t loop_len, loop_quant, loop_count_in, loop_level;   /* SYNC (0 Free, 1..16 bars), QUANT, COUNT-IN, LEVEL */
    uint8_t loop_slot, loop_target;       /* the slot in RAM (0..9); SAVE held's target */
    uint8_t loop_mode, loop_keys;         /* the record mode (CRL_MODE_*); the PLAY menu's keys: 0 Play, 1 Loops (v8) */
    uint8_t save_act;                     /* SAVE held's picker */
    uint8_t save_pending;                 /* slot + 1: saved once the loop stops (no flash erase while it plays) */
    uint16_t loop_used;                   /* bit k: slot k holds a loop in flash */
    uint8_t pick_roots;                   /* the engine picker: 1 the white roots choose engines, 0 they play */
    uint8_t usb_in, usb_fixed;            /* Options > USB Record (on: presented to the computer), USB Level */
} cs;
#if CR_HAVE_SETTINGS
static void cr_settings_load(void);       /* cr_settings.c (included after this file): the record -> the UI */
static void cr_settings_save(void);       /* .. save now (Felucca's settings_save: the panel table too) */
#endif
static uint8_t cr_restore_lock;            /* cr_backup.c: a settings record was restored into flash; the UI's state is
                                            * older, so no settings save until the restart (RESTART, or a power cycle) */
#define CR_SETTINGS_BUSY() (cr_snap.lstate == CRL_PLAYING || cr_restore_lock)   /* cr_settings.c: no flash erase while a
                                                                                 * loop plays (nor over a restore) */

static void cu_pos_sync(void)
{
    uint32_t p0 = cu_part_pos(0), p1 = cu_part_pos(1);
    cs.sound = (uint16_t)p0;
    if (cs.bass_sound)
        cs.bass_sound = (uint16_t)(p1 + 1u);
    cs.pool_pos[0][eng_rank(trk[CR_PART_CHORD].eng_req % NENGINES)] = (uint8_t)p0;
    cs.pool_pos[1][eng_rank(trk[CR_PART_BASS].eng_req % NENGINES)] = (uint8_t)p1;
}

static void cu_post_key(void) { cr_post(CRE_KEYMODE, cs.key_on, cs.tonic, cs.scale); }
static void cu_post_single(void) { cr_post(CRE_SINGLE, cs.single, 0, cs.split); }
static void cu_loop_conf(void)                    /* the looper's settings -> the ISR */
{
    cr_post(CRE_LOOP, LP_CONF, LC_SYNC, cs.loop_len);
    cr_post(CRE_LOOP, LP_CONF, LC_QUANT, cs.loop_quant);
    cr_post(CRE_LOOP, LP_CONF, LC_COUNTIN, cs.loop_count_in);
    cr_post(CRE_LOOP, LP_CONF, LC_LEVEL, cs.loop_level);
    cr_post(CRE_LOOP, LP_CONF, LC_SIG, cs.metro_sig);
    cr_post(CRE_LOOP, LP_CONF, LC_VOL, cs.metro_vol);
    cr_post(CRE_LOOP, LP_METRO, 0, cs.metro);
    cr_post(CRE_LOOP, LP_CONF, LC_MODE, cs.loop_mode);
}
static void cu_set_tempo(int32_t bpm)
{
    cs.bpm = (uint16_t)(bpm < 20 ? 20 : bpm > 300 ? 300 : bpm);
    song.g[G_BPM] = (int16_t)(cs.bpm < 40u ? 40u : cs.bpm > 240u ? 240u : cs.bpm);   /* (the delay's sync) */
    cr_post(CRE_TEMPO, 0, 0, cs.bpm);
}
static void cu_fx_apply(void)                     /* the sends of part 0: the amounts, or 0 with FX off */
{
    uint32_t i;
    for (i = 0; i < CU_NFX; i++)
        trk[CR_PART_CHORD].p[CU_FX[i].send] = (int16_t)(cs.fx_on ? cs.fx_amt[i] : 0u);
}
static void cu_sends_to_fx(void)                  /* part 0's own sends become the FX amounts */
{
    uint32_t i;
    for (i = 0; i < CU_NFX; i++)
        cs.fx_amt[i] = (uint8_t)trk[CR_PART_CHORD].p[CU_FX[i].send];
    cu_fx_apply();
}

/* the layers' knob row (cr_screen.h CR_K_KNOBROW: FX, PERF, BASS): the cell just turned stays hot CU_HOT_MS (the
 * editor's CE_HOT_MS) instead of a popup; a layer opened or another item picked clears it */
#define CU_HOT_MS 800u
static struct { uint8_t layer, knob; uint32_t until; } cu_hot;   /* knob + 1 (0: none) */
static void cu_hot_set(uint32_t l, uint32_t knob)
{
    cu_hot.layer = (uint8_t)l;
    cu_hot.knob = (uint8_t)(knob + 1u);
    cu_hot.until = fm1_ms + CU_HOT_MS;
}
static void cu_hot_row(cr_screen_t *s, uint32_t l)   /* the row's hot cell, if layer l's knob is still hot */
{
    uint32_t k = (cu_hot.knob - 1u) & 3u;
    s->n_rows = 1;
    if (cu_hot.knob && cu_hot.layer == l && (int32_t)(cu_hot.until - fm1_ms) > 0 && (s->cell[0][k].flags & CR_CF_ON) &&
        !(s->cell[0][k].flags & CR_CF_DIM)) {
        s->hot_r = 1;
        s->hot_c = (uint8_t)k;
    }
}
/* a bus parameter's value as the knob row shows it: a name capitalised ("Room", "Spring"; a division as it is:
 * "1/8"), a rate in Hz ("0.80 Hz"), else the number the popup showed ("90") */
static void cu_fx_val(uint32_t g, char *out, uint32_t n)
{
    const param_desc_t *d = &GP[g];
    int32_t v = song.g[g];
    char val[12];
    const char *unit;
    uint32_t i;
    if (d->fmt != F_ENUM && d->fmt != F_LFOHZ) {
        cu_int(out, v, 0, n);
        return;
    }
    param_format(d, v, val, &unit);
    if (val[0] == '.') {                          /* ".80" -> "0.80" */
        cu_cpy(out, "0", n);
        cu_cat(out, val, n);
    } else {
        cu_cpy(out, val, n);
    }
    if (d->fmt == F_ENUM && out[0] >= 'A' && out[0] <= 'Z')
        for (i = 1; out[i]; i++)
            if (out[i] >= 'A' && out[i] <= 'Z') out[i] = (char)(out[i] - 'A' + 'a');
    if (d->fmt == F_LFOHZ) {
        cu_cat(out, " ", n);
        cu_cat(out, unit, n);
    }
}
/* the glyph and its values (Q8 of 255) for KNOB k of effect e (FORMAT.md "cell glyphs"; docs/INTEGRATION.md 3) */
static void cu_fx_glyph(uint32_t e, uint32_t k, cr_cell_t *c)
{
    int32_t g = CU_FX[e].g[k];
    uint32_t v = g >= 0 ? (uint32_t)(song.g[g] - GP[g].min) : 0u, r = g >= 0 ? (uint32_t)(GP[g].max - GP[g].min) : 1u;
    uint32_t q = r ? v * 255u / r : 0u;
    switch (g) {
    case G_RSIZE: c->glyph = CR_G_ROOM; c->pct = (uint8_t)q; break;
    case G_RDAMP: c->glyph = CR_G_MOON; c->pct = (uint8_t)(255u - q); break;     /* more damping, darker */
    case G_RTYPE: c->glyph = song.g[g] ? CR_G_SPRING : CR_G_ROOM; c->pct = 128; break;
    case G_DCOLOR: c->glyph = CR_G_MOON; c->pct = (uint8_t)q; break;
    case G_DTIME: case G_DFDBK:                   /* the division's index, the feedback (0..120) */
        c->glyph = CR_G_ECHOES;
        c->pct = (uint8_t)((uint32_t)(song.g[G_DTIME] - GP[G_DTIME].min) * 255u / (uint32_t)(GP[G_DTIME].max - GP[G_DTIME].min));
        c->pct2 = (uint8_t)((uint32_t)song.g[G_DFDBK] * 255u / 120u);
        break;
    case G_CRATE: case G_CDEPTH:                  /* rate, depth (0..127) */
        c->glyph = CR_G_LFO;
        c->pct = (uint8_t)((uint32_t)song.g[G_CRATE] * 255u / 127u);
        c->pct2 = (uint8_t)((uint32_t)song.g[G_CDEPTH] * 255u / 127u);
        break;
    default: break;
    }
}
/* the fx layer's cells: KNOB 1..3 the bus parameters of the effect (a missing one: no cell, a dim dash), KNOB 4 the
 * amount (Drive: the clip glyph, the others the dry / wet squares; "off" while FX is off) */
static void cu_fx_cells(cr_screen_t *s)
{
    uint32_t e = cs.fx_sel % CU_NFX, k;
    for (k = 0; k < 3u; k++) {
        cr_cell_t *c = &s->cell[0][k];
        int32_t g = CU_FX[e].g[k];
        if (g < 0)
            continue;
        c->flags = CR_CF_ON;
        cu_cpy(c->label, CU_FX[e].gname[k], sizeof c->label);
        if (c->label[0] >= 'a' && c->label[0] <= 'z') c->label[0] = (char)(c->label[0] - 'a' + 'A');
        cu_fx_val((uint32_t)g, c->value, sizeof c->value);
        cu_fx_glyph(e, k, c);
    }
    {
        cr_cell_t *c = &s->cell[0][3];
        c->flags = CR_CF_ON;
        cu_cpy(c->label, "Amount", sizeof c->label);
        c->glyph = CU_FX[e].send == P_DIST ? CR_G_CLIP : CR_G_MIX;
        if (cs.fx_on) {
            cu_2d(c->value, cs.fx_amt[e] * 99u / 127u, sizeof c->value);
            c->pct = (uint8_t)(cs.fx_amt[e] * 255u / 127u);
        } else {
            cu_cpy(c->value, "off", sizeof c->value);
        }
    }
    cu_hot_row(s, L_FX);
}
static void cu_sound_go(uint32_t pos)             /* PRESETS: position pos of the chord part's pool */
{
    uint32_t n = pool_count(trk[CR_PART_CHORD].eng_req % NENGINES);
    cu_list_load(0, pos % n);
    cu_sends_to_fx();
}
static void cu_bass_go(uint32_t v)                /* ALGORITHM: 0 OFF, 1.. the bass part's pool (position v - 1) */
{
    uint32_t n = pool_count(trk[CR_PART_BASS].eng_req % NENGINES);
    cs.bass_sound = (uint16_t)(v > n ? n : v);
    if (cs.bass_sound)
        cu_list_load(1, cs.bass_sound - 1u);
    cs.bass_on = cs.bass_sound != 0u;
    cr_post(CRE_BASS, 0, 0, cs.bass_on);
}
static void cu_perf_pick(uint32_t i)
{
    cs.perf_sel = (uint8_t)(i % 7u);
    cr_post(CRE_PERFORM_MODE, 0, 0, CU_PERF[cs.perf_sel].mode);
    if (CU_PERF[cs.perf_sel].range) {
        cs.par[CU_PERF[cs.perf_sel].mode][CR_P_RANGE] = CU_PERF[cs.perf_sel].range;
        cr_post(CRE_PARAM, CU_PERF[cs.perf_sel].mode, CR_P_RANGE, CU_PERF[cs.perf_sel].range);
    }
}
static void cu_route(void)                        /* Options > MIDI: channels and the RAW stream */
{
    uint32_t s;
    for (s = 0; s < CR_NSTREAM; s++) {
        cr_route.midi_en[s] = cs.ch[s] != 0u;
        if (cs.ch[s])
            cr_route.ch[s] = (uint8_t)(cs.ch[s] - 1u);
    }
    cr_route.part[CR_STREAM_RAW] = cs.raw_sound ? CR_PART_CHORD : CR_NOPART;
    cr_post(CRE_STREAM, CR_STREAM_RAW, 0, cs.ch[CR_STREAM_RAW] || cs.raw_sound);
}

/* ------------------------------------------------------------- Options --- */
enum { O_STYLE, O_EXTADD, O_SECRET, O_VEL, O_BASSMODE, O_SINGLE, O_SPLIT, O_CH_MAIN, O_CH_BASS, O_CH_RAW, O_RAW_SOUND,
       O_CLOCK, O_VIEW, O_MOTION, O_LEDS, O_HOLD, O_USB_IN, O_USB_LEVEL, O_VERSION, O_CALIB, O_SAFE, O_ERASE,
       O_N };
#define O_N_NORMAL O_SAFE                 /* Safe Mode and Flash Data: listed in SAFE MODE only (core.h cr_safe) */
static const char *const O_NAME[O_N] = {"Play Style", "Extension Addition", "Secret Chords", "Velocity",
    "Bass Behaviour", "Single Notes", "Split Point", "MIDI Perform", "MIDI Bass", "MIDI Raw Chord", "Raw Chord Sound",
    "MIDI Clock", "View", "Motion", "LEDs", "Hold Time", "USB Record", "USB Level", "Version",
    "Calibrate", "Safe Mode", "Flash Data"};
static const int16_t O_MAX[O_N] = {2, 1, 2, 127, 3, 1, 11, 16, 16, 16, 1, 2, V_N - 1, CR_MOTION_N - 1, 1, 3, 1, 1,
                                   0, 0, 0, 0};
static uint32_t cu_opt_n(void) { return cr_safe ? O_N : O_N_NORMAL; }
static uint8_t cu_erase_ask;              /* Options > Flash Data: OCT+ pressed once (the second erases) */

/* Options > USB Record / USB Level (docs/USB-AUDIO.md; named from the FM-1's side: Record = the computer records
 * the FM-1, the device "ChoralRoot In"): USB Level at once (fx.c), USB Record once the setting has rested 0.6 s
 * (usb.c ua_off_set / ua_off_apply: the FM-1 leaves the bus for a second and the computer reads the new
 * configuration). Off: the serial console instead (usb.c usb_cdc_on). The emulator has no USB: the settings are
 * kept and saved only */
static void cu_usb_apply(void)
{
    fx_usb_fixed = cs.usb_fixed;
#if FELUCCA_UAC
    ua_off_set(UA_OFF_IN, cs.usb_in, fm1_ms);
#endif
}

static int32_t opt_get(uint32_t o)
{
    switch (o) {
    case O_STYLE: return cs.playstyle;
    case O_EXTADD: return cs.extadd;
    case O_SECRET: return cs.secret;
    case O_VEL: return cs.vel;
    case O_BASSMODE: return cs.bass_mode;
    case O_SINGLE: return cs.single;
    case O_SPLIT: return cs.split;
    case O_CH_MAIN: case O_CH_BASS: case O_CH_RAW: return cs.ch[o - O_CH_MAIN];
    case O_RAW_SOUND: return cs.raw_sound;
    case O_CLOCK: return cr_route.clock_in ? 2 : cr_route.clock_out;
    case O_VIEW: return cs.view;
    case O_MOTION: return cr_motion;
    case O_LEDS: return cs.leds;
    case O_HOLD: return settings_hold % 4u;
    case O_USB_IN: return cs.usb_in;
    case O_USB_LEVEL: return cs.usb_fixed;
    default: return 0;
    }
}
static void opt_set(uint32_t o, int32_t v)
{
    v = v < (o == O_VEL ? 1 : 0) ? (o == O_VEL ? 1 : 0) : v > O_MAX[o] ? O_MAX[o] : v;
    switch (o) {
    case O_STYLE: cs.playstyle = (uint8_t)v; cr_post(CRE_PLAYSTYLE, 0, 0, v); break;
    case O_EXTADD: cs.extadd = (uint8_t)v; cr_post(CRE_EXTADD, 0, 0, v); break;
    case O_SECRET: cs.secret = (uint8_t)v; cr_post(CRE_SECRET, 0, 0, v); break;
    case O_VEL: cs.vel = (uint8_t)v; break;
    case O_BASSMODE: cs.bass_mode = (uint8_t)v; cr_post(CRE_BASS_MODE, 0, 0, v); break;
    case O_SINGLE: cs.single = (uint8_t)v; cu_post_single(); break;
    case O_SPLIT: cs.split = (uint8_t)v; cu_post_single(); break;
    case O_CH_MAIN: case O_CH_BASS: case O_CH_RAW: cs.ch[o - O_CH_MAIN] = (uint8_t)v; cu_route(); break;
    case O_RAW_SOUND: cs.raw_sound = (uint8_t)v; cu_route(); break;
    case O_CLOCK: cr_route.clock_in = (uint8_t)(v == 2); cr_route.clock_out = (uint8_t)(v == 1); break;
    case O_VIEW: cs.view = (uint8_t)v; break;
    case O_MOTION: cr_motion = (uint8_t)v; break;
    case O_LEDS: cs.leds = (uint8_t)v; break;
    case O_HOLD: settings_hold = (uint8_t)v; break;
    case O_USB_IN: cs.usb_in = (uint8_t)v; cu_usb_apply(); break;
    case O_USB_LEVEL: cs.usb_fixed = (uint8_t)v; cu_usb_apply(); break;
    default: break;
    }
}
static void opt_text(uint32_t o, char *d, uint32_t n)
{
    static const char *const STYLE[3] = {"Simple", "Advanced", "Free"};
    static const char *const EXTADD[2] = {"Add Note", "Play Chord"};
    static const char *const SECRET[3] = {"Off", "Simple", "All"};
    static const char *const MOTION[3] = {"Full", "Calm", "Off"};
    static const char *const LEDS[2] = {"Glow", "Stock"};
    static const char *const ONOFF[2] = {"Off", "On"};
    static const char *const CLOCK[3] = {"Off", "Out", "In"};
    int32_t v = opt_get(o);
    d[0] = 0;
    switch (o) {
    case O_STYLE: cu_cpy(d, STYLE[v % 3], n); break;
    case O_EXTADD: cu_cpy(d, EXTADD[v & 1], n); break;
    case O_SECRET: cu_cpy(d, SECRET[v % 3], n); break;
    case O_VEL: cu_int(d, v, 0, n); break;
    case O_BASSMODE: cu_cpy(d, CU_BASSMODE[v & 3], n); break;
    case O_SINGLE: cu_cpy(d, v ? "Split" : "Full Octave", n); break;
    case O_SPLIT: cu_cpy(d, CU_NOTE[v % 12], n); cu_cat(d, cs.single ? "" : " (Split off)", n); break;
    case O_RAW_SOUND: cu_cpy(d, ONOFF[v & 1], n); break;
    case O_CH_MAIN: case O_CH_BASS: case O_CH_RAW:
        if (!v) cu_cpy(d, "Off", n);
        else { cu_cpy(d, "ch ", n); cu_int(d + 3, v, 0, n > 3u ? n - 3u : 0u); }
        break;
    case O_CLOCK: cu_cpy(d, CLOCK[v % 3], n); break;
    case O_VIEW: cu_cpy(d, CU_VIEW[v % V_N], n); break;
    case O_MOTION: cu_cpy(d, MOTION[v % 3], n); break;
    case O_LEDS: cu_cpy(d, LEDS[v & 1], n); break;
    case O_HOLD: cu_int(d, HOLD_MS[v & 3], 0, n); cu_cat(d, " ms", n); break;
    case O_USB_IN: cu_cpy(d, ONOFF[v & 1], n); break;
    case O_USB_LEVEL: cu_cpy(d, v ? "Fixed" : "Master", n); break;
    case O_VERSION: cu_cpy(d, CR_VERSION, n); break;
    case O_CALIB: cu_cpy(d, "OCT+ starts", n); break;
    case O_SAFE: cu_cpy(d, "flash data skipped", n); break;
    case O_ERASE: cu_cpy(d, cu_erase_ask ? "OCT+ again: erase" : "erase and reboot", n); break;
    default: break;
    }
}

/* ------------------------------------------------------------ UI state --- */
enum { PG_NONE, PG_EDIT, PG_SAVE };
enum { PU_METER, PU_VOICING, PU_BASS_VOICING };
static struct {
    uint32_t bheld;                       /* label bits held, as the scan last saw them */
    uint32_t kheld;                       /* key bits held */
    uint8_t armed;                        /* the armed button (tap / hold / layer), NB: none */
    uint8_t open, combo;                  /* its layer (or shift) is open; something else was touched */
    uint32_t t0;                          /* when it was pressed (fm1_ms) */
    uint8_t lock;                         /* the open layer (locked since its button's hold), L_NONE: none */
    uint32_t swallow;                     /* buttons pressed during another's hold: their release does nothing */
    uint8_t oct_chord;                    /* OCT- and OCT+ were down together (panic): no tap */
    uint8_t oct_mod;                      /* OCT- held was a modifier (the editor: + a knob clears its modulation): no tap */
    uint32_t clear_t0;                    /* D#4 down in the loop layer (| 1): CLEAR after 1 s held */
    uint8_t clock;                        /* B3 LOCK: the chord block latches (off at power-on, not saved) */
    uint8_t mlatch, mkill;                /* bit cr_mod_t: latched by LOCK; toggled off by this press (its release
                                           * does nothing) */
    uint8_t key_note[CU_NKEY];            /* root keys: note + 1 sent; 0xFF: the layer's / a picker's */
    int8_t octave;
    uint8_t opt_open, opt_sel;
    uint8_t page;                         /* PG_* */
    uint32_t last_sound;                  /* the last time a chord sounded or a key was held */
    struct { uint32_t until; uint8_t kind, col, segs, jump, mark; uint16_t pct; char value[8], sub[12], label[24]; } pop;
                                          /* jump: the meter's bar jumps to the value (no fill: the picker's preview);
                                           * mark: the square after the name (an overwritten factory preset) */
    uint8_t epk_on, epk_rank;             /* OPT held + PRESETS: the engine picker, the engine (ENGINE_ORDER rank) */
    struct { uint32_t until; uint8_t big, col; char text[24], label[32]; } msg;   /* big: 1 PANIC, 2 red type */
} cu;

/* the sound pages and the naming screen (cr_pages.c, cr_name.c) */
static struct {
    uint8_t part, page;                   /* the part edited (0 chord, 1 bass), its page (of cr_pages.c's sequence) */
    uint8_t turn;                         /* page turns (cu_animate: the columns slide in, from the side of dir) */
    int8_t dir;
    int8_t col;                           /* the column last turned (its glyph animates), -1 none */
    uint32_t t0;                          /* .. when */
    uint16_t from_pct, shown_pct[4];      /* the turned glyph's tween: from, and what each column last showed */
    uint8_t from_env[4], shown_env[4][4];
    uint8_t save_part, slot, save_from_edit;   /* SAVE: the part saved, the slot written (Save as new: the free one) */
    uint8_t save_step, save_choice;       /* 0 the choice (save_choice 0 Overwrite, 1 Save as new), 1 the naming page */
    uint8_t save_pos;                     /* Save as new: the place in the pool it takes ("FM6 \267 27") */
    uint8_t del_ask;                      /* SAVE held 1 s in the dialog on a user preset: 1 "delete?", 2 "reset to
                                           * factory?" (an overwritten factory preset); OCT+ does it, OCT- keeps */
} ce;
#define CU_GLYPH_MS 220u                  /* a turned column's glyph eases to its new value */
#ifdef CR_TRACE                           /* the emulator's headless logs (tools/emu/emu_firmware.h) */
#define cu_trace(...) printf(__VA_ARGS__)
#else
#define cu_trace(...) ((void)0)
#endif
static void cu_message(const char *t, uint32_t col);
static void cu_flash_erase(void);
static uint32_t cu_batt(void);
static void cu_edit_open(uint32_t part);
#define CR_EDIT_HOOKS 1                   /* the sound editor (cr_edit.c, included below) */
enum { CE_TAP, CE_HOLD, CE_SHIFT };
static int ce_owns(uint32_t b);
static int ce_button(uint32_t b, uint32_t ev);
static void ce_leds(uint8_t *nl, uint32_t blink);
static void cu_led(uint8_t *nl, uint32_t id, int on);
static void cu_save_open(uint32_t part);
static void cu_save_close(void);
static void cu_save_commit(void);
static void cu_save_delete(void);
static void cu_pick_begin(void);
static void cu_pick_end(int keep);
static void cu_epk_commit(void);
static uint32_t cu_layer(void);
static uint32_t cu_now(void) { return fm1_ms; }
static int cu_blink(uint32_t now) { return ((now / 250u) & 1u) == 0u; }   /* the blinking LEDs' phase (and the dial's dot) */

static void cu_message(const char *t, uint32_t col)
{
    cu.msg.until = cu_now() + CR_MSG_MS;
    cu.msg.big = 0;
    cu.msg.col = (uint8_t)col;
    cu_cpy(cu.msg.text, t, sizeof cu.msg.text);
}

/* a knob's meter for CR_POPUP_MS: value huge (the CRX charset: digits, + - . and A-G), sub / label under it */
static void cu_popup(const char *value, const char *sub, const char *label, uint32_t col, int32_t v, int32_t lo,
                     int32_t hi, uint32_t segs)
{
    cu.pop.until = cu_now() + CR_POPUP_MS;
    cu.pop.kind = PU_METER;
    cu.pop.jump = 0;
    cu.pop.mark = 0;
    cu_cpy(cu.pop.value, value, sizeof cu.pop.value);
    cu_cpy(cu.pop.sub, sub, sizeof cu.pop.sub);
    cu_cpy(cu.pop.label, label, sizeof cu.pop.label);
    cu.pop.col = (uint8_t)col;
    cu.pop.segs = (uint8_t)segs;
    cu.pop.pct = (uint16_t)(hi > lo ? (uint32_t)((v - lo) * 256 / (hi - lo)) : 0u);
}
static void cu_popup_num(int32_t v, int plus, const char *sub, const char *label, uint32_t col, int32_t lo,
                         int32_t hi, uint32_t segs)
{
    char b[8];
    if (plus || v < 0)
        cu_int(b, v, plus, sizeof b);
    else
        cu_2d(b, (uint32_t)v, sizeof b);
    cu_popup(b, sub, label, col, v, lo, hi, segs);
}

/* the emulator's trace of a text with the middle dot (Latin-1 0xB7) as UTF-8 */
static void cu_trace_txt(const char *t)
{
#ifdef CR_TRACE
    for (; *t; t++) {
        if ((uint8_t)*t == 0xB7u)
            printf("\xc2\xb7");
        else
            putchar(*t);
    }
#else
    (void)t;
#endif
}
/* a pool's meter (mock-up: design/choralroot-fm1-preset-screens.png 1, 5): the position big ("05"), the name (an
 * overwritten factory preset: a square mark after it), "FM6 \267 05/26" (the engine, the position, the pool's size;
 * the bass: in orange, " \267 solo" with Bass Behaviour Solo), the stripes over the pool (at most 32) */
static void cu_pool_popup(uint32_t part, uint32_t pos)
{
    char b[8], nm[13], lb[24], c[8];
    uint32_t e = trk[part ? CR_PART_BASS : CR_PART_CHORD].eng_req % NENGINES, n = pool_count(e);
    pool_ent_t en = pool_entry(e, pos);
    cu_2d(b, pos, sizeof b);
    pool_name(e, pos, nm);
    cu_cpy(lb, ENGINES[e]->name, sizeof lb);
    cu_cat(lb, " \267 ", sizeof lb);
    cu_2d(c, pos, sizeof c);
    cu_cat(lb, c, sizeof lb);
    cu_cat(lb, "/", sizeof lb);
    cu_2d(c, n, sizeof c);
    cu_cat(lb, c, sizeof lb);
    if (part && cs.bass_mode == CR_BASS_SOLO)     /* Bass Behaviour Solo: the chord part is silent while it is on */
        cu_cat(lb, " \267 solo", sizeof lb);
    cu_popup(b, nm, lb, part ? CR_COL_ORANGE : CR_COL_WHITE, (int32_t)pos, 0, (int32_t)n, n > 32u ? 32u : n);
    cu.pop.mark = en.kind == PK_USER && en.fk != PF_NONE;
    cu_trace("popup: part %u %s / %s%s / ", (unsigned)part, b, nm, cu.pop.mark ? " (mark)" : "");
    cu_trace_txt(lb);
    cu_trace("\n");
}
/* PRESETS / ALGORITHM's meter: the part's place in its pool (the bass OFF: "00 off") */
static void cu_sound_popup(int bass)
{
    if (bass && (!cs.bass_sound || !cs.bass_on)) {   /* (BASS tapped off: its sound kept for the next tap) */
        cu_popup("00", "off", "bass", CR_COL_ORANGE, 0, 0, 12, 12);
        cu_trace("popup: part 1 00 / off / bass\n");
        return;
    }
    cu_pool_popup(bass ? 1u : 0u, bass ? cs.bass_sound - 1u : cs.sound);
}
/* upreset.c's messages (cr_bank.c declares them) */
static void ui_say(const char *a, const char *b)
{
    char t[24];
    cu_cpy(t, a, sizeof t);
    cu_cat(t, b, sizeof t);
    cu_message(t, CR_COL_WHITE);
}
static void ui_message(const char *s) { cu_message(s, CR_COL_WHITE); }

/* the layer on screen: the open (locked) one, else SAVE's while it is held (momentary) */
static uint32_t cu_layer(void)
{
    if (cu.lock)
        return cu.lock;
    if (cu.armed != NB && cu.open && cu_layer_of(cu.armed) == L_SAVE)
        return L_SAVE;
    return L_NONE;
}
static int cu_shift(void) { return cu.armed == BT_OPT && ((cu.bheld >> BT_OPT) & 1u); }
/* the editor's hooks (cr_edit.c defines CR_EDIT_HOOKS with ce_owns / ce_button / ce_leds; CE_TAP CE_HOLD CE_SHIFT) */
#ifndef CR_EDIT_HOOKS
#define CR_EDIT_HOOKS 0
#endif
#if CR_EDIT_HOOKS                                 /* the editor: OCT shifts the octave as on the view */
static int cu_picker_ctx(void) { return cu.lock || cu.opt_open || cu.page == PG_SAVE; }   /* OCT-: back, OCT+: OK */
#else
static int cu_picker_ctx(void) { return cu.lock || cu.opt_open || cu.page; }   /* OCT-: back, OCT+: OK */
#endif

/* ----------------------------------------------------- perform params --- */
static void cu_param_text(uint32_t p, int32_t v, char *val, char *sub)
{
    val[0] = sub[0] = 0;
    if (p == CR_P_DIV) {
        cu_2d(val, (uint32_t)(v + 1), 8);
        cu_cpy(sub, CU_DIV[v % CR_DIV_COUNT], 12);
    } else if (p == CR_P_DIR) {
        cu_2d(val, (uint32_t)(v + 1), 8);
        cu_cpy(sub, CU_DIR[v % 6], 12);
    } else if (p == CR_P_PATTERN) {
        cu_2d(val, (uint32_t)v, 8);
        cu_cpy(sub, cr_pattern_name(v), 12);
    } else if (p == CR_P_RETRIG || p == CR_P_HOLD) {
        cu_2d(val, (uint32_t)v, 8);
        cu_cpy(sub, v ? "on" : "off", 12);
    } else {
        cu_int(val, v, p == CR_P_ROTATE, 8);
        cu_cpy(sub, p == CR_P_RATE ? "ms" : p == CR_P_RANGE ? "oct" : p == CR_P_ROTATE ? "" : "%", 12);
    }
}
static void cu_param_turn(uint32_t mode, int32_t p, int32_t s, int pop)   /* pop 0: the PERF layer's knob row */
{
    char val[8], sub[12], label[24];
    int32_t v;
    if (p < 0)
        return;
    v = cs.par[mode][p] + s * CU_PAR[p].step;
    v = v < CU_PAR[p].min ? CU_PAR[p].min : v > CU_PAR[p].max ? CU_PAR[p].max : v;
    cs.par[mode][p] = (int16_t)v;
    cr_post(CRE_PARAM, mode, (uint32_t)p, v);
    if (!pop) {
        cu_trace("perf: knob %s %s %d\n", CU_PERF[cs.perf_sel].short_name, CU_PAR[p].name, (int)v);
        return;
    }
    cu_param_text((uint32_t)p, v, val, sub);
    cu_cpy(label, CU_PERF[cs.perf_sel].short_name, sizeof label);
    label[0] = (char)(label[0] | 0x20);
    cu_cat(label, " ", sizeof label);
    cu_cat(label, CU_PAR[p].name, sizeof label);
    cu_popup(val, sub, label, CR_COL_WHITE, v, CU_PAR[p].min, CU_PAR[p].max, 12);
}
static uint32_t cu_perf_mode(void) { return CU_PERF[cs.perf_sel].mode; }

/* ------------------------------------------------------------ the looper --- */
/* The ten loop slots in flash (docs/LOOPER.md): storage.c's commit record and A/B copies (its hooks st_read /
 * st_erase / st_prog, its header and CRC), on sectors of their own: user sample slot 3's 80 KiB (0xC8000..0xDBFFF,
 * which nothing in ChoralRoot 0.1 writes; eng_sample.c's scan ignores what is not a sample set), slot k's copies at
 * 0xC8000 + (2k + copy) x 4 KiB, the record's type CRL_FL_TYPE + k (never one of storage.c's objects). */
#define CRL_FL_BASE 0xC8000u
#define CRL_FL_TYPE 0x4C30u
static uint8_t cu_loop_buf[CRL_REC_MAX] __attribute__((aligned(4)));   /* a packed record */
static uint8_t cu_loop_gen;                        /* + 1 whenever cu_loop_buf is written here (cr_backup.c stages a
                                                    * restore in it between requests: a change ends that restore) */
/* the loop being left, packed for its own slot (cu_loop_keep): written at once when stopped; while a loop plays (no
 * flash erase then) it waits here for the stop. A buffer of its own: cu_loop_buf carries the slot being loaded */
static uint8_t cu_loop_pend[CRL_REC_MAX] __attribute__((section(".pool"), aligned(4)));
static struct {
    uint16_t n;                                    /* cu_loop_pend's length (0: an empty loop, the slot's record goes) */
    uint8_t slot;                                  /* its slot + 1 (0: nothing waits) */
    uint8_t q, qfrom, qloads;                      /* a switch posted: the slot the loop in RAM belongs to, crl.loads */
    uint8_t want;                                  /* PRESETS in the PLAY menu: the slot turned to + 1 (cu_loop_want) */
    uint32_t want_t;                               /* .. when (playing: the switch waits for the knob to rest) */
    uint8_t adv_to;                                /* Advance: the slot jumped to + 1 (the strip's hop arrow) .. */
    uint32_t adv_until;                            /* .. until then */
    char adv_txt[32];                              /* "slot 3 saved \267 next: slot 4" (the menu, armed) .. */
    uint8_t adv_armed;                             /* .. while the take it armed waits */
} cu_lp;
#if FELUCCA_FLASH
static uint32_t crl_fl_sector(uint32_t k, uint32_t copy) { return CRL_FL_BASE + (2u * k + copy) * ST_SECTOR; }
static int crl_fl_head(uint32_t k, uint32_t copy, st_hdr_t *h)   /* 0: a valid commit record */
{
    if (st_read(crl_fl_sector(k, copy), h, sizeof *h))
        return -1;
    if (h->magic != ST_MAGIC || h->type != CRL_FL_TYPE + k || h->slot != copy || h->len > CRL_REC_MAX ||
        h->hcrc != st_crc32(h, sizeof *h - 4u))
        return -1;
    return 0;
}
static int crl_fl_body(uint32_t k, uint32_t copy, const st_hdr_t *h, uint8_t *dst)
{
    if (st_read(crl_fl_sector(k, copy) + ST_PAYLOAD_OFF, dst, h->len) || st_crc32(dst, h->len) != h->crc)
        return -1;
    return 0;
}
static int crl_fl_current(uint32_t k, st_hdr_t *h, uint8_t *dst)  /* the newest valid copy (its payload in dst) */
{
    st_hdr_t a, b;
    int va = crl_fl_head(k, 0, &a) == 0, vb = crl_fl_head(k, 1, &b) == 0;
    if (vb && (!va || (b.seq != a.seq && b.seq - a.seq < 0x80000000u)) && crl_fl_body(k, 1, &b, dst) == 0) {
        *h = b;
        return 1;
    }
    if (va && crl_fl_body(k, 0, &a, dst) == 0) {
        *h = a;
        return 0;
    }
    if (vb && crl_fl_body(k, 1, &b, dst) == 0) {
        *h = b;
        return 1;
    }
    return -1;
}
static int crl_fl_load(uint32_t k, uint8_t *dst)   /* the record's length, -1: none */
{
    st_hdr_t h;
    if (!flash_ok || ST_BLOCKED() || k >= CRL_SLOTS || crl_fl_current(k, &h, dst) < 0)
        return -1;
    return (int)h.len;
}
static int crl_fl_save(uint32_t k, const uint8_t *src, uint32_t len)   /* storage.c st_save's protocol: 0 ok */
{
    st_hdr_t h;
    uint32_t seq, base, off, copy;
    int cur, rc;
    if (!flash_ok || ST_BLOCKED() || k >= CRL_SLOTS || len > CRL_REC_MAX)
        return -1;
    cur = crl_fl_current(k, &h, st_buf);
    seq = cur < 0 ? 0u : h.seq;
    copy = cur == 0 ? 1u : 0u;                     /* write the other copy, the header last */
    base = crl_fl_sector(k, copy);
    if ((rc = st_erase(base)) != 0)
        return rc;
    for (off = 0; off < len; off += 256u)
        if ((rc = st_prog(base + ST_PAYLOAD_OFF + off, src + off, len - off > 256u ? 256u : len - off)) != 0)
            return rc;
    h.magic = ST_MAGIC;
    h.type = (uint16_t)(CRL_FL_TYPE + k);
    h.slot = (uint16_t)copy;
    h.seq = seq + 1u;
    h.len = len;
    h.crc = st_crc32(src, len);
    h.rsv[0] = h.rsv[1] = 0xFFFFFFFFu;
    h.hcrc = st_crc32(&h, sizeof h - 4u);
    if ((rc = st_prog(base, &h, sizeof h)) != 0)
        return rc;
    {
        st_hdr_t chk;
        if (crl_fl_head(k, copy, &chk) || memcmp(&chk, &h, sizeof h) || crl_fl_body(k, copy, &chk, st_buf))
            return -7;
    }
    return 0;
}
static int crl_fl_delete(uint32_t k)
{
    if (!flash_ok || ST_BLOCKED() || k >= CRL_SLOTS)
        return -1;
    return st_erase(crl_fl_sector(k, 0)) || st_erase(crl_fl_sector(k, 1)) ? -1 : 0;
}
static int crl_fl_ready(void) { return flash_ok && !ST_BLOCKED(); }   /* the slots can be written */
#else
static int crl_fl_ready(void) { return 0; }
static int crl_fl_load(uint32_t k, uint8_t *dst) { (void)k; (void)dst; return -1; }
static int crl_fl_save(uint32_t k, const uint8_t *src, uint32_t len) { (void)k; (void)src; (void)len; return -1; }
static int crl_fl_delete(uint32_t k) { (void)k; return -1; }
#endif

static void cu_loop_scan(void)                     /* which slots hold a loop */
{
    uint32_t k;
    crl_data_t *d = &crl_stage;
    cu_loop_gen++;
    cs.loop_used = 0;
    if (crl_stage_busy)
        return;
    for (k = 0; k < CRL_SLOTS; k++) {
        int n = crl_fl_load(k, cu_loop_buf);
        if (n > 0 && cr_loop_unpack(cu_loop_buf, (uint32_t)n, d))
            cs.loop_used |= (uint16_t)(1u << k);
    }
}
static int cu_playing(void) { return cr_snap.lstate == CRL_PLAYING; }
static void cu_slot_msg(const char *a, uint32_t k, const char *b, uint32_t col)
{
    char t[24];
    char nb[4];
    cu_cpy(t, a, sizeof t);
    cu_int(nb, (int32_t)k + 1, 0, sizeof nb);
    cu_cat(t, nb, sizeof t);
    cu_cat(t, b, sizeof t);
    cu_message(t, col);
}
/* the staged loop -> its slot (an empty loop deletes the slot's record). 0: written, or nothing staged; a flash
 * error: the message, the staged copy dropped, -1 */
static int cu_loop_flush(const char *why)
{
    uint32_t k, n;
    int rc;
    (void)why;                                     /* (the trace's) */
    if (!cu_lp.slot)
        return 0;
    k = cu_lp.slot - 1u;
    n = cu_lp.n;
    cu_lp.slot = 0;
    if (cu_playing())                              /* (a second switch while it plays: a hiccup, not a lost take) */
        cu_trace("loop: pending save flushed while playing\n");
    rc = n ? crl_fl_save(k, cu_loop_pend, n) : (cs.loop_used >> k) & 1u ? crl_fl_delete(k) : 0;
    cu_trace("loop: auto-save slot %u %u bytes rc %d (%s)\n", (unsigned)k + 1u, (unsigned)n, rc, why);
    if (rc) {
        cu_message("save error", CR_COL_RED);
        return -1;
    }
    if (n)
        cs.loop_used |= (uint16_t)(1u << k);
    else
        cs.loop_used &= (uint16_t)~(1u << k);
    return 0;
}
/* The loop in RAM is about to be replaced by slot k's: what it holds goes to its own slot first, so a slot keeps
 * what was recorded there (docs/LOOPER.md "Slots and the record"). from: the slot it belongs to (cs.loop_slot, or
 * the target of a SAVE held Save still waiting for the stop). Changed since its last save / load (crl.dirty, an
 * overdub in progress, that waiting Save): packed (an overdub in progress ends, as the switch would end it), written
 * now when stopped, staged for the stop while it plays (no flash erase then). One staged earlier and still waiting
 * is written first, whatever plays (a second switch while playing: a hiccup rather than a lost take; and the slot
 * about to be read may be that one). Nothing saved when revert loads from itself again (SAVE held + Load: its changes
 * dropped on purpose), in SAFE MODE, after a restore (the flash is newer than the RAM) or without the flash.
 * Returns 1: written now, 0: nothing to write now (or staged), -1: a flash error, the switch refused (the loop stays) */
static int cu_loop_keep(uint32_t k, uint32_t from, int revert, int play)
{
    uint8_t was = cs.save_pending;
    int need = was || crl.dirty || crl.cap == CRL_CAP_OD || crl.cap == CRL_CAP_STEP;
    if (cu_loop_flush(play ? "a second switch" : "before a load"))
        return -1;
    cs.save_pending = 0;                           /* (taken over here, or dropped with the loop) */
    if (!need || (revert && k == from) || cr_safe || cr_restore_lock || !crl_fl_ready())
        return 0;
    fm1_irq_off();
    if (crl.cap == CRL_CAP_OD || crl.cap == CRL_CAP_STEP)
        cr_loop_rec(&crl, &cr);                    /* the overdub ends, its layer kept (as cr_loop_queue ends it); step
                                                    * entry ends, its steps the loop */
    cu_lp.n = (uint16_t)cr_loop_pack(&crl.d, cu_loop_pend, sizeof cu_loop_pend);
    fm1_irq_on();
    if (!cu_lp.n && !((cs.loop_used >> from) & 1u))
        return 0;                                  /* empty (cleared), and so is its slot */
    cu_lp.slot = (uint8_t)(from + 1u);
    if (play) {
        cu_trace("loop: slot %u staged %u bytes, saved at the stop\n", (unsigned)from + 1u, (unsigned)cu_lp.n);
        return 0;
    }
    if (cu_loop_flush("leaving it")) {
        cs.save_pending = was;
        return -1;
    }
    return 1;
}
/* a slot -> the loop in RAM: now when stopped, at the end of the cycle when playing; the loop being left goes to its
 * own slot first (cu_loop_keep). revert (SAVE held + Load, power-on): the slot in RAM may be loaded again, its
 * changes dropped; LOOP held + the root of the slot it is on, changed: kept as it is (it is not being left) */
static int cu_loop_load(uint32_t k, int revert)   /* < 0: refused (the message says why), else cu_loop_keep's */
{
    int n, ok = 0, play = cu_playing(), kept;
    uint32_t from = cs.save_pending ? cs.save_pending - 1u : cs.loop_slot;
    char t[24], nb[4];
    cu_loop_gen++;
    if (crl_stage_busy) {
        cu_message("a slot is on its way", CR_COL_RED);
        return -2;
    }
    if (!revert && k == from && (crl.dirty || cs.save_pending || crl.cap == CRL_CAP_OD || crl.cap == CRL_CAP_STEP)) {
        cu_trace("loop: slot %u kept as it is (changed since its save)\n", (unsigned)k + 1u);
        cu_slot_msg("loop ", k, "", CR_COL_RED);
        return -3;
    }
    kept = cu_loop_keep(k, from, revert, play);
    if (kept < 0)
        return -1;                                 /* (the message says why: the loop and its slot stay) */
    n = (cs.loop_used >> k) & 1u ? crl_fl_load(k, cu_loop_buf) : -1;
    if (n > 0)
        ok = cr_loop_unpack(cu_loop_buf, (uint32_t)n, &crl_stage);
    cu_lp.q = 1;                                   /* (cu_loop_frame: a switch cancelled before the cycle's end) */
    cu_lp.qfrom = (uint8_t)from;
    cu_lp.qloads = crl.loads;
    cs.loop_slot = (uint8_t)k;
    crl_stage_busy = 1;
    cr_post(CRE_LOOP, play ? LP_QUEUE : LP_SET, ok ? 0u : 1u, 0);
    cu_trace("loop: slot %u %s%s\n", (unsigned)k + 1u, ok ? "loaded" : "empty", play ? " (next cycle)" : "");
    cu_cpy(t, "loop ", sizeof t);
    cu_int(nb, (int32_t)k + 1, 0, sizeof nb);
    cu_cat(t, nb, sizeof t);
    if (kept > 0) {                                /* "loop 2 \267 saved 1" ("\267 cleared 1": its loop was cleared) */
        cu_cat(t, cu_lp.n ? " \267 saved " : " \267 cleared ", sizeof t);
        cu_int(nb, (int32_t)from + 1, 0, sizeof nb);
        cu_cat(t, nb, sizeof t);
    } else {
        cu_cat(t, ok ? (play ? ": next cycle" : "") : ": empty", sizeof t);
    }
    if (kept > 0 || cu_layer() != L_LOOP)          /* (the PLAY menu's strip shows the slot) */
        cu_message(t, kept > 0 ? CR_COL_GREEN : CR_COL_RED);
    return kept;
}
static void cu_loop_save_now(uint32_t k)
{
    uint32_t n;
    int rc;
    cu_loop_flush("before a save");
    cu_loop_gen++;
    fm1_irq_off();                                 /* the ISR's loop, packed as it is now */
    n = cr_loop_pack(&crl.d, cu_loop_buf, sizeof cu_loop_buf);
    fm1_irq_on();
    if (!n) {
        cu_message("no loop to save", CR_COL_RED);
        return;
    }
    rc = crl_fl_save(k, cu_loop_buf, n);
    cu_trace("loop: save slot %u %u bytes rc %d\n", (unsigned)k + 1u, (unsigned)n, rc);
    if (rc) {
        cu_message("save error", CR_COL_RED);
        return;
    }
    fm1_irq_off();
    crl.dirty = 0;
    fm1_irq_on();
    cs.loop_used |= (uint16_t)(1u << k);
    cs.loop_slot = (uint8_t)k;
    cu_slot_msg("saved to slot ", k, "", CR_COL_GREEN);
}
static void cu_loop_save(uint32_t k)
{
    if (!cr_snap.lnev) {
        cu_message("no loop to save", CR_COL_RED);
        return;
    }
    if (cu_playing()) {                            /* no flash erase while it plays (Felucca: project_save) */
        cs.save_pending = (uint8_t)(k + 1u);
        cu_slot_msg("slot ", k, ": saves at stop", CR_COL_RED);
        return;
    }
    cu_loop_save_now(k);
}
static void cu_loop_delete(uint32_t k)
{
    if (cu_playing()) {
        cu_message("stop to delete", CR_COL_RED);
        return;
    }
    cu_loop_flush("before a delete");
    if (crl_fl_delete(k)) {
        cu_message("delete error", CR_COL_RED);
        return;
    }
    cs.loop_used &= (uint16_t)~(1u << k);
    if (cs.save_pending == k + 1u)
        cs.save_pending = 0;
    if (k == cs.loop_slot) {                       /* the slot in RAM: its loop stays, unsaved (leaving drops it) */
        fm1_irq_off();
        crl.dirty = 0;
        fm1_irq_on();
    }
    cu_trace("loop: delete slot %u\n", (unsigned)k + 1u);
    cu_slot_msg("slot ", k, " deleted", CR_COL_RED);
}
static void cu_save_act(void)                      /* SAVE held + OCT+: the action on the target slot */
{
    if (cr_safe) {                                 /* SAFE MODE: the slots are neither read nor written */
        cu_message("safe mode: no saving", CR_COL_RED);
        return;
    }
    if (cs.save_act == 0)
        cu_loop_save(cs.loop_target);
    else if (cs.save_act == 1)
        cu_loop_load(cs.loop_target, 1);           /* (the slot in RAM: reverted to its save) */
    else
        cu_loop_delete(cs.loop_target);
}
/* the slot the PLAY menu shows: PRESETS' while its switch waits, else the one in RAM */
static uint32_t cu_lm_slot(void) { return cu_lp.want ? cu_lp.want - 1u : cs.loop_slot; }
/* PRESETS turned in the PLAY menu: a slot switch (cu_loop_load): at once when stopped; playing, once the knob rests
 * 250 ms (one switch per cycle: a second waits for the first to be taken) */
static void cu_loop_want(void)
{
    uint32_t k;
    if (!cu_lp.want || crl_stage_busy || (cu_playing() && cu_now() - cu_lp.want_t < 250u))
        return;
    k = cu_lp.want - 1u;
    cu_lp.want = 0;
    if (k != cs.loop_slot)
        cu_loop_load(k, 0);
}
static void cu_loop_turn(int32_t s)
{
    uint32_t k = (cu_lm_slot() + (uint32_t)(s % (int32_t)CRL_SLOTS + (int32_t)CRL_SLOTS)) % CRL_SLOTS;
    cu_lp.want = (uint8_t)(k + 1u);
    cu_lp.want_t = cu_now();
    cu_trace("loop: slot %u turned\n", (unsigned)k + 1u);
    cu_loop_want();
}
static void cu_loop_mode_turn(int32_t s)           /* ALGORITHM in the PLAY menu: the record mode (saved) */
{
    int32_t v = (int32_t)cs.loop_mode + s;
    cs.loop_mode = (uint8_t)(v < 0 ? 0 : v >= (int32_t)CRL_NMODE ? CRL_NMODE - 1u : (uint32_t)v);
    cr_post(CRE_LOOP, LP_CONF, LC_MODE, cs.loop_mode);
    cu_trace("loop: mode %s\n", CU_LOOP_MODE[cs.loop_mode]);
}
static void cu_loop_keys(uint32_t v)               /* GLO tapped / KNOB 4 in the PLAY menu: the keys play / the slots */
{
    cs.loop_keys = (uint8_t)(v != 0);
    cu_hot_set(L_LOOP, 3);
    cu.clear_t0 = 0;
    cu_trace("loop: keys %s\n", cs.loop_keys ? "Loops" : "Play");
}
/* REC tapped, by the record mode (docs/LOOPER-MODES.md; cr_loop.c does the rest). Advance over a loop: first to the
 * next slot (10 wraps to 1), the loop left saved to its own (the slot rule: stopped first, so it is written now);
 * then as Overwrite there (that slot's loop goes when the take starts). An empty slot records where it is */
static void cu_rec(void)
{
    if (cs.loop_mode == CRL_MODE_ADVANCE && cr_snap.lcap == CRL_CAP_NONE && cr_snap.lstate != CRL_EMPTY) {
        uint32_t from = cs.loop_slot, to = CRL_NEXT_SLOT(from);
        char nb[4];
        int kept;
        cu_lp.want = 0;
        if (cu_playing()) {
            fm1_irq_off();
            cr_loop_stop(&crl, &cr);
            fm1_irq_on();
            cr_snap.lstate = crl.state;            /* (cu_loop_load: a stopped loop's switch, now) */
        }
        kept = cu_loop_load(to, 0);
        if (kept < 0 || cs.loop_slot != to)
            return;
        cu_lp.adv_to = (uint8_t)(to + 1u);
        cu_lp.adv_until = cu_now() + 800u;
        cu_lp.adv_txt[0] = 0;
        if (kept > 0) {                            /* "slot 3 saved \267 next: slot 4" */
            cu_cpy(cu_lp.adv_txt, "slot ", sizeof cu_lp.adv_txt);
            cu_int(nb, (int32_t)from + 1, 0, sizeof nb);
            cu_cat(cu_lp.adv_txt, nb, sizeof cu_lp.adv_txt);
            cu_cat(cu_lp.adv_txt, " saved \267 ", sizeof cu_lp.adv_txt);
        }
        cu_cat(cu_lp.adv_txt, "next: slot ", sizeof cu_lp.adv_txt);
        cu_int(nb, (int32_t)to + 1, 0, sizeof nb);
        cu_cat(cu_lp.adv_txt, nb, sizeof cu_lp.adv_txt);
        cu_lp.adv_armed = 1;
        if (cu_layer() != L_LOOP)                  /* (the menu: under the length, cu_loopmenu) */
            cu_message(cu_lp.adv_txt, CR_COL_RED);
        cu_trace("loop: advance slot %u -> %u (%s)\n", (unsigned)from + 1u, (unsigned)to + 1u, kept > 0 ? "saved" : "nothing to save");
    }
    cr_post(CRE_LOOP, LP_REC, 0, 0);
}
/* the transport's results (the ISR's CRL_DID_*) as messages; a deferred save once the loop stops */
static uint32_t cu_did_seen;
static void cu_loop_frame(void)
{
    static const char *const DID[] = {"", "rec: play to start", "count-in", "recording", "cancelled", "loop recorded",
                                      "overdub armed", "overdub done", "play", "stop", "undo", "loop cleared",
                                      "nothing recorded", "replace armed", "step entry", "steps recorded"};
    uint32_t n = crl_did_n;
    if (n != cu_did_seen) {
        uint32_t d = crl_did;
        cu_did_seen = n;
        if (d == CRL_DID_UNDO) {                   /* the layers left, huge in red (cr_build_screen) */
            uint32_t nl = crl.d.nlayers;
            cu.msg.until = cu_now() + CR_MSG_MS;
            cu.msg.big = 2;
            cu.msg.col = CR_COL_RED;
            cu_int(cu.msg.text, (int32_t)nl, 0, sizeof cu.msg.text);
            cu_cpy(cu.msg.label, nl == 1u ? "layer \267 undo" : "layers \267 undo", sizeof cu.msg.label);
            cu_trace("undo: %u layers left\n", (unsigned)nl);
        } else if (d == CRL_DID_NOTHING)
            cu_message(cr_snap.lfull ? "loop full" : cr_snap.lstate == CRL_EMPTY ? "no loop" : "nothing to undo",
                       CR_COL_RED);
        else if (cu_layer() == L_LOOP && (d == CRL_DID_ARM || d == CRL_DID_OD_ARM || d == CRL_DID_REP_ARM ||
                                          d == CRL_DID_STEP || d == CRL_DID_REC))
            ;                                      /* (the PLAY menu shows these itself: no box over it) */
        else if (d < sizeof DID / sizeof DID[0])
            cu_message(DID[d], d == CRL_DID_PLAY || d == CRL_DID_STOP ? CR_COL_WHITE : CR_COL_RED);
        cu_trace("loop: %s (state %u layers %u events %u len %u)\n", d < sizeof DID / sizeof DID[0] ? DID[d] : "?",
                 (unsigned)crl.state, (unsigned)crl.d.nlayers, (unsigned)crl.d.nev, (unsigned)crl.d.len);
    }
    {                                              /* the ISR's MIDI start / stop (cr_out.c) */
        static uint32_t rt_seen;
        uint32_t rn = cr_rt_n;
        if (rn != rt_seen) {
            rt_seen = rn;
            cu_trace("midi: %02X %s\n", (unsigned)cr_rt_last, cr_rt_last == 0xFAu ? "start" : "stop");
        }
    }
    cu_loop_want();                                /* PRESETS in the PLAY menu: the slot turned to */
    if (cu_lp.adv_armed && (int32_t)(cu_lp.adv_until - cu_now()) <= 0 && cr_snap.lcap != CRL_CAP_ARMED &&
        cr_snap.lcap != CRL_CAP_COUNTIN)
        cu_lp.adv_armed = 0;                       /* (Advance's text: until the take starts or is cancelled) */
    if (cu_lp.q && !crl_stage_busy) {              /* the switch posted is done: taken, or cancelled before the cycle's
                                                    * end (a stop, a clear, a panic, an overdub armed, an undo) */
        cu_lp.q = 0;
        if (crl.loads == cu_lp.qloads && cs.loop_slot != cu_lp.qfrom) {
            cu_trace("loop: switch to slot %u cancelled, slot %u stays\n", (unsigned)cs.loop_slot + 1u,
                     (unsigned)cu_lp.qfrom + 1u);
            cs.loop_slot = cu_lp.qfrom;            /* (the loop in RAM is still that slot's) */
        }
    }
    if (cu_lp.slot && !cu_playing()) {             /* stopped: the loop left while it played, to its slot */
        uint32_t k = cu_lp.slot - 1u;
        if (cu_loop_flush("at the stop") == 0)
            cu_slot_msg(cu_lp.n ? "saved to slot " : "cleared slot ", k, "", CR_COL_GREEN);
    }
    if (cs.save_pending && !cu_playing()) {
        uint32_t k = cs.save_pending - 1u;
        cs.save_pending = 0;
        cu_loop_save_now(k);
    }
}

/* -------------------------------------------------------------- actions --- */
static void cu_unlatch(void);
static void cu_panic(void)
{
    uint32_t k;
    cu_unlatch();                                 /* the LOCK latch cleared (LOCK itself stays on) */
    cr_post(CRE_PANIC, 0, 0, 0);
    cu.octave = 0;
    for (k = 0; k < CU_NKEY; k++)                 /* the keys still held end nothing more */
        if (cu.key_note[k] && cu.key_note[k] != 0xFFu)
            cu.key_note[k] = 0xFFu;
    cs.sticky = 0;                                /* (the engine's panic drops the latch: the LED follows) */
    cu.msg.until = cu_now() + CR_PANIC_MS;
    cu.msg.big = 1;
    cu.msg.col = CR_COL_RED;
    cu_cpy(cu.msg.text, "PANIC", sizeof cu.msg.text);
    cu_cpy(cu.msg.label, "all notes off", sizeof cu.msg.label);
    cu.pop.until = 0;
}

static void cu_close_all(void)                    /* back to the view */
{
    cu.lock = L_NONE;
    cu.opt_open = 0;
    cu.page = PG_NONE;
}
static void cu_layer_close(void)                  /* OCT- in a layer: back (the engine picker: to the editor) */
{
    cu_trace("layer: close %u\n", (unsigned)cu.lock);
    cu.lock = L_NONE;
}

static void cu_tap(uint32_t b)                    /* a button tapped (released before HOLD with nothing touched) */
{
#if CR_EDIT_HOOKS
    if (cu.page == PG_EDIT && !cu_layer() && ce_button(b, CE_TAP))   /* the editor's section buttons, SHIFT, EDIT */
        return;
#endif
    switch (b) {                                  /* (a layer's own button: still its on/off action) */
    case BT_KEY:
        cs.key_on ^= 1u;
        cu_post_key();
        break;
    case BT_PERF:
        cs.perform_on ^= 1u;
        cr_post(CRE_PERFORM, 0, 0, cs.perform_on);
        break;
    case BT_FX:
        cs.fx_on ^= 1u;
        cu_fx_apply();
        break;
    case BT_BASS:
        if (!cs.bass_on && !cs.bass_sound) {      /* ALGORITHM at OFF: the bass part's sound (the last one, the
                                                   * stored one at power-on) at its place in its pool */
            cs.bass_sound = (uint16_t)(cu_part_pos(1) + 1u);
            cs.bass_on = 1;
            cr_post(CRE_BASS, 0, 0, 1);
        } else {
            cs.bass_on ^= 1u;
            cr_post(CRE_BASS, 0, 0, cs.bass_on);
        }
        cu_sound_popup(1);                        /* the bass meter (mock-up 14), "00 off" when it went off */
        cu_trace("bass: %s (sound %u, %s)\n", cs.bass_on ? "on" : "off", (unsigned)cs.bass_sound,
                 CU_BASSMODE[cs.bass_mode & 3u]);
        break;
    case BT_LATCH:
        cs.sticky ^= 1u;
        cr_post(CRE_STICKY, 0, 0, cs.sticky);
        break;
    case BT_OPT:
        if (cu.lock == L_LOOP) {                  /* the PLAY menu: GLO toggles its keys (play / the slots) */
            cu_loop_keys(!cs.loop_keys);
            break;
        }
        cu.page = PG_NONE;
        cu.lock = L_NONE;
        cu.opt_open ^= 1u;
        break;
    case BT_EDIT:                                 /* the chord sound's editor (BASS held + EDIT: cu_btn_press) */
        if (cu.lock == L_EDIT) {                  /* the engine picker open: EDIT keeps its sound, closes it */
            cu_pick_end(1);
            cu_layer_close();
        }
        else if (cu.page == PG_EDIT)
            cu.page = PG_NONE;
        else
            cu_edit_open(0);
        break;
    case BT_SAVE:                                 /* naming and saving the sound (on a bass page: the bass) */
        if (cu.page == PG_SAVE && ce.del_ask)
            ce.del_ask = 0;                       /* "delete?": no */
        else if (cu.page == PG_SAVE)
            cu_save_commit();                     /* SAVE again: save (as OCT+) */
        else
            cu_save_open(cu.page == PG_EDIT ? ce.part : 0u);
        break;
    case BT_METRO:                                /* the click on / off */
        cs.metro ^= 1u;
        cr_post(CRE_LOOP, LP_METRO, 0, cs.metro);
        cu_message(cs.metro ? "metronome on" : "metronome off", CR_COL_WHITE);
        break;
    case BT_LOOP:                                 /* play / stop (a take: commit) */
        cr_post(CRE_LOOP, LP_PLAY, 0, 0);
        break;
    case BT_REC:                                  /* record by the mode / end the take */
        cu_rec();
        break;
    default:
        break;
    }
}

/* the layers that lock open on a hold (SAVE held is momentary: the loop slots while it is held) */
static int cu_lockable(uint32_t l) { return l && l != L_SAVE; }

static void cu_opened(uint32_t b)                 /* a button held past HOLD (or a combo): its layer / shift */
{
    uint32_t l = cu_layer_of(b), was = cu.lock;
    if (b == BT_REC && !cu.combo)                 /* REC held: undo the last layer */
        cr_post(CRE_LOOP, LP_UNDO, 0, 0);
    if (!l)
        return;
    cu.opt_open = 0;
    if (cu.page != PG_EDIT)                       /* a layer over the editor closes back to it (OCT- / HOME) */
        cu.page = PG_NONE;
    if (was == L_EDIT && l != L_EDIT)             /* the picker left for another layer: its sound kept */
        cu_pick_end(1);
    cu.lock = (uint8_t)(cu_lockable(l) ? l : L_NONE);   /* the layer stays open after release */
    cu_hot.knob = 0;                              /* (a layer opens with no hot cell) */
    cu_trace("layer: open %u%s\n", (unsigned)l, cu.lock ? " (locked)" : "");
    if (l == L_EDIT && was != L_EDIT)             /* the engine picker: a preview of the sound as it is now */
        cu_pick_begin();
}

static void cu_home_tap(void)
{
    if (cu.lock == L_EDIT)                        /* the engine picker: cancelled, the sound as it was */
        cu_pick_end(0);
    if (cu.lock && cu.page == PG_EDIT) {          /* a layer over the editor: back to the editor */
        cu_layer_close();
        return;
    }
    if (cu.page == PG_SAVE && !cu.lock) {         /* naming: cancelled, back to where it came from */
        cu_save_close();
        return;
    }
    if (cu.lock || cu.opt_open || cu.page)
        cu_close_all();
    else
        cs.view = (uint8_t)((cs.view + 1u) % V_N);
}

static void cu_calib_start(void);
static void cu_oct_tap(uint32_t b)
{
    if (cu.page == PG_SAVE && !cu_layer()) {      /* naming: OCT- deletes, OCT+ saves */
        if (ce.del_ask) {                         /* "delete?": OCT+ yes, OCT- no */
            if (b == B_OCTUP)
                cu_save_delete();
            else
                ce.del_ask = 0;
            return;
        }
        if (b == B_OCTUP)                         /* OCT+: save, OCT-: cancel (F#4 deletes a letter) */
            cu_save_commit();
        else
            cu_save_close();
        return;
    }
    if (b == B_OCTUP && cu.opt_open && cu.opt_sel == O_ERASE && cr_safe && !cu.lock && !cu.page) {
        if (!cu_erase_ask) {
            cu_erase_ask = 1;                     /* the first OCT+: "OCT+ again" */
            return;
        }
        cu_flash_erase();                         /* the second: erase the data objects, reboot (no return) */
        return;
    }
    if (b == B_OCTUP && cu.opt_open && cu.opt_sel == O_CALIB && !cu.lock && !cu.page) {
        cu_close_all();
        cu_calib_start();                         /* Options > Calibrate, OCT+: Felucca's HARDWARE CALIBRATION */
        return;
    }
    if (cr_snap.lcap == CRL_CAP_STEP && (!cu_layer() || cu_layer() == L_LOOP) && !cu.opt_open && cu.page == PG_NONE) {
        cr_post(CRE_LOOP, LP_STEP, 0, b == B_OCTUP ? CRL_STEP_REST : CRL_STEP_BACK);   /* step entry: a rest / back */
        cu_trace("loop: step %s\n", b == B_OCTUP ? "rest" : "back");
        return;
    }
    if (cu_layer()) {                             /* a layer: OCT+ does SAVE's action, else OK; OCT-: back */
        uint32_t l = cu_layer();
        if (l == L_EDIT) {                        /* the engine picker: OCT+ keeps the sound, OCT- cancels */
            cu_pick_end(b == B_OCTUP);
            cu_layer_close();
        } else if (b == B_OCTUP && l == L_SAVE)
            cu_save_act();
        else if (b == B_OCTUP && l == L_LOOP)
            ;                                     /* the PLAY menu: nothing (Pause = LOOP, Undo = REC held) */
        else if (cu.lock)
            cu_layer_close();
        return;
    }
    if (cu_picker_ctx()) {                        /* OCT+: OK (a picker's change is live already), OCT-: back */
        cu_close_all();
        return;
    }
    cu.octave = (int8_t)(cu.octave + (b == B_OCTUP ? 1 : -1));
    cu.octave = (int8_t)(cu.octave < -2 ? -2 : cu.octave > 2 ? 2 : cu.octave);
    cu_popup_num(cu.octave, 1, "", "octave", CR_COL_WHITE, -2, 2, 5);
}

/* ------------------------------------------------------------ the layers --- */
static int32_t cu_engine_rank(uint32_t e)          /* place of e among the melodic engines (the EDIT picker) */
{
    uint32_t r, n = 0;
    for (r = 0; r < NENG_SHOWN; r++) {
        uint32_t x = eng_vis(r);
        if (!cu_engine_melodic(x))
            continue;
        if (x == e)
            return (int32_t)n;
        n++;
    }
    return -1;
}
static uint32_t cu_engine_at(uint32_t i, uint32_t *n_out)
{
    uint32_t r, n = 0, e = 0, found = 0;
    for (r = 0; r < NENG_SHOWN; r++) {
        uint32_t x = eng_vis(r);
        if (!cu_engine_melodic(x))
            continue;
        if (n == i) {
            e = x;
            found = 1;
        }
        n++;
    }
    if (n_out)
        *n_out = n;
    return found ? e : eng_vis(0);
}
/* --------------------------------------------- the sound pages, saving --- */
/* EDIT tap: the chord sound's pages (BASS held + EDIT: the bass sound's), cr_pages.c; EDIT held: the engine
 * picker of the same part; SAVE: naming (cr_name.c) into a user slot (upreset.c) */
static track_t *cu_edit_trk(void) { return &trk[ce.part ? CR_PART_BASS : CR_PART_CHORD]; }

static void cu_edited(uint32_t part)
{
    psnd[part & 1u].edited = 1;
    trk[part ? CR_PART_BASS : CR_PART_CHORD].user = 0;   /* (no longer the slot's sound as stored) */
}

/* the part's engine becomes e: its own parameters from e's first preset, the envelope, filter, LFO, sends and
 * mix kept (mock-up 22) */
static void cu_engine_switch(uint32_t part, uint32_t e)
{
    track_t *t = &trk[part ? CR_PART_BASS : CR_PART_CHORD];
    const engine_t *en = ENGINES[e % NENGINES];
    uint32_t i, p0 = en->npresets ? cu_preset_orig(en, 0) : 0u;
    if (t->eng_req == e)
        return;
    fm1_irq_off();
    t->eng_req = (uint8_t)e;
    t->preset = (uint8_t)p0;
    t->trim = 0;
    for (i = 0; i < 8u; i++)
        t->p[P_E0 + i] = en->npresets ? (int16_t)en->presets[p0].e[i] : en->edit[i].def;
    panic_req |= (uint8_t)(1u << (uint32_t)(t - trk));
    fm1_irq_on();
    fm6_track_loaded(t);
    str_cpy(psnd[part].name, en->npresets ? en->presets[p0].name : en->name, sizeof psnd[0].name);
    cu_snd_set(part, pool_f0(e) > p0 || !en->npresets ? PK_INIT : PK_FACTORY, p0, PF_NONE);   /* (its first preset) */
    cu_edited(part);
    cu_pos_sync();
    cu_trace("engine: part %u -> %s\n", (unsigned)part, en->name);
}
static void cu_engine_pick(uint32_t i) { cu_engine_switch(ce.part, cu_engine_at(i, 0)); }   /* EDIT held: a root */

/* EDIT held + KNOB 1 (PRESETS in the editor): the next place in the pool of the part's engine, loaded whole (the
 * pool PRESETS turns: docs/PRESETS.md) */
static void cu_pool_step(int32_t s)
{
    uint32_t part = ce.part, e = cu_edit_trk()->eng_req % NENGINES, n = pool_count(e), pos;
    pos = (cu_part_pos(part) + (uint32_t)(s % (int32_t)n + (int32_t)n)) % n;
    cu_pool_load(part, e, pos);
    if (!part)
        cu_sends_to_fx();
    cu_trace("preset: part %u -> %s / %s (%u)\n", (unsigned)part, ENGINES[e]->name, psnd[part].name, (unsigned)pos);
}

/* EDIT held + KNOB 2 (or the ENGINE page's INIT): the init sound of the part's engine (its pool's position 0) */
static void cu_sound_init(uint32_t part)
{
    track_t *t = &trk[part ? CR_PART_BASS : CR_PART_CHORD];
    cu_pool_load(part, t->eng_req, 0);
    if (!part)
        cu_sends_to_fx();
    cu_message("init sound", CR_COL_WHITE);
    cu_trace("init: part %u %s\n", (unsigned)part, ENGINES[t->eng_req % NENGINES]->name);
}

/* the engine picker as a PREVIEW (EDIT held; PRESETS turned in the editor): the part's sound as it was at its opening
 * (every parameter, the engine, the deep patch, the flags and the user slot); OCT- / HOME put it back, OCT+ / EDIT
 * keep what was loaded (a fresh load: not edited). Main loop only */
static struct {
    uint8_t on, part, eng, preset, user, trim, changed, fx_on, fx_amt[CU_NFX], has_blob;
    int16_t p[P_COUNT];
    cu_snd_t snd;                                 /* the part's sound: its name, edited, its pool entry */
    uint8_t blob[ENG_BLOB_MAX];
} cpk __attribute__((section(".pool")));

/* the part's sound as a checksum (the traces: the sound before and after a preview) */
static uint32_t cu_snd_crc(const track_t *t)
{
    const eng_deep_t *d = cp_deep(t);
    uint8_t b[ENG_BLOB_MAX];
    uint32_t h = 2166136261u, i;
    h = (h ^ t->eng_req) * 16777619u;
    for (i = 0; i < P_COUNT; i++) {
        h = (h ^ (uint8_t)t->p[i]) * 16777619u;
        h = (h ^ (uint8_t)((uint16_t)t->p[i] >> 8)) * 16777619u;
    }
    if (d && d->blob_get && d->blob_size && d->blob_size <= sizeof b) {
        d->blob_get(t, b);
        for (i = 0; i < d->blob_size; i++)
            h = (h ^ b[i]) * 16777619u;
    }
    return h & 0xFFFFu;
}

static void cu_pick_begin(void)
{
    track_t *t = cu_edit_trk();
    const eng_deep_t *d = cp_deep(t);
    uint32_t i;
    cpk.on = 1;
    cpk.part = ce.part;
    cpk.eng = t->eng_req;
    cpk.preset = t->preset;
    cpk.user = t->user;
    cpk.trim = (uint8_t)t->trim;
    cpk.changed = 0;
    cpk.fx_on = cs.fx_on;
    for (i = 0; i < CU_NFX; i++)
        cpk.fx_amt[i] = cs.fx_amt[i];
    for (i = 0; i < P_COUNT; i++)
        cpk.p[i] = t->p[i];
    cpk.snd = psnd[ce.part];
    cpk.has_blob = d && d->blob_get && d->blob_set && d->blob_size && d->blob_size <= sizeof cpk.blob;
    if (cpk.has_blob)
        d->blob_get(t, cpk.blob);
    cu_trace("picker: open part %u %s crc %04x roots %s\n", (unsigned)cpk.part, cpk.snd.name, (unsigned)cu_snd_crc(t),
             cs.pick_roots ? "engines" : "play");
}

/* the preview ends: keep (OCT+, EDIT, another layer) or cancel (OCT-, HOME: the snapshot back) */
static void cu_pick_end(int keep)
{
    track_t *t = &trk[cpk.part ? CR_PART_BASS : CR_PART_CHORD];
    const eng_deep_t *d;
    uint32_t i;
    if (!cpk.on)
        return;
    cpk.on = 0;
    if (keep) {
        if (cpk.changed)
            psnd[cpk.part].edited = 0;            /* (a fresh load) */
        cu_trace("picker: keep part %u %s crc %04x\n", (unsigned)cpk.part, psnd[cpk.part].name, (unsigned)cu_snd_crc(t));
        return;
    }
    if (cpk.changed) {
        fm1_irq_off();
        t->eng_req = cpk.eng;
        t->preset = cpk.preset;
        t->user = cpk.user;
        t->trim = (int8_t)cpk.trim;
        for (i = 0; i < P_COUNT; i++)
            t->p[i] = cpk.p[i];
        panic_req |= (uint8_t)(1u << (uint32_t)(t - trk));
        fm1_irq_on();
        fm6_track_loaded(t);                      /* (the engine's own state for the sound) */
        d = cp_deep(t);
        if (cpk.has_blob && d && d->blob_set)
            d->blob_set(t, cpk.blob);             /* the deep patch as it was */
        if (!cpk.part) {
            cs.fx_on = cpk.fx_on;
            for (i = 0; i < CU_NFX; i++)
                cs.fx_amt[i] = cpk.fx_amt[i];
        }
        psnd[cpk.part] = cpk.snd;                 /* (its name, edited, its place in the pool) */
        cu_pos_sync();
    }
    cu_trace("picker: cancel part %u -> %s crc %04x\n", (unsigned)cpk.part, psnd[cpk.part].name, (unsigned)cu_snd_crc(t));
}
static void cu_pick_open(void)                     /* PRESETS turned in the editor: the picker over it */
{
    cu.opt_open = 0;
    cu.lock = L_EDIT;
    cu_trace("layer: open %u (locked)\n", (unsigned)L_EDIT);
    cu_pick_begin();
}

#include "cr_edit.c"                       /* the sound editor: its state, input and screen */
static void cu_edit_open(uint32_t part) { ce_open(part); }

/* SAVE (docs/PRESETS.md; mock-ups design/choralroot-fm1-preset-screens.png 3, 4, 6): a tap opens the dialog, two
 * choices: Overwrite (the current preset: saved at once, its name kept) and Save as new (the next free slot: the
 * pool's next place, then the naming page prefilled with the current name). The default: Overwrite on a user preset
 * or an edited sound, Save as new otherwise. OCT+ (or SAVE again) takes the choice, OCT- cancels. Overwrite on a
 * factory preset saves a record bound to it (into the first free slot), on a user preset rewrites its slot (its
 * binding and name kept), on INIT (which cannot be overwritten) is Save as new. Overwrite never renames: a new name
 * is a Save as new (and the old one deleted). SAVE held 1 s in the dialog: "reset to factory?" on an overwritten
 * factory preset, "delete?" on an added one; OCT+ does it. No free slot: "no free slot". Every user preset is one of
 * upreset.c's 32 records (cr_bank.c cb_store writes the binding) */
static void cu_save_open(uint32_t part)
{
    if (cr_safe) {                                 /* SAFE MODE: the user sounds are neither read nor written */
        cu_message("safe mode: no saving", CR_COL_RED);
        return;
    }
    ce.save_part = (uint8_t)part;
    ce.save_from_edit = cu.page == PG_EDIT;
    ce.save_step = 0;
    ce.save_choice = psnd[part].kind == PK_USER || psnd[part].edited ? 0u : 1u;
    ce.del_ask = 0;
    cu.opt_open = 0;
    cu.lock = L_NONE;
    cu.page = PG_SAVE;
    cu.pop.until = 0;                              /* (a PRESETS meter would hide the dialog) */
    cu_trace("save: dialog part %u %s%s (%s)\n", (unsigned)part, psnd[part].name, psnd[part].edited ? "*" : "",
             ce.save_choice ? "save as new" : "overwrite");
}
static void cu_save_close(void) { cu.page = ce.save_from_edit ? PG_EDIT : PG_NONE; }

/* "FM6 27": the engine and a place of its pool (b holds 24) */
static void cu_place(char *b, uint32_t e, uint32_t pos, const char *sep)
{
    char n[8];
    cu_cpy(b, ENGINES[e % NENGINES]->name, 24);
    cu_cat(b, sep, 24);
    cu_2d(n, pos, sizeof n);
    cu_cat(b, n, 24);
}
static void cu_save_done(uint32_t part, uint32_t k, int rc, const char *what)   /* after cb_store: the message */
{
    track_t *t = &trk[part ? CR_PART_BASS : CR_PART_CHORD];
    char b[24], l[24];
    if (rc != 0 && rc != 3) {
        cu_message(rc == 2 ? "save error" : "bad slot", CR_COL_RED);
        return;
    }
    t->user = (uint8_t)(k + 1u);
    up_name(k, psnd[part].name);
    cu_snd_set(part, PK_USER, cb_bound(k), k);
    cu_pos_sync();
    cb_pool_check();
    cu_place(l, t->eng_req, cu_part_pos(part), " ");
    cu_cpy(b, what, sizeof b);
    cu_cat(b, l, sizeof b);
    cu_message(b, CR_COL_GREEN);
    cu_save_close();
}

/* OCT+ (or SAVE) on the choice */
static void cu_save_take(void)
{
    uint32_t part = ce.save_part, k, f;
    track_t *t = &trk[part ? CR_PART_BASS : CR_PART_CHORD];
    uint32_t e = t->eng_req % NENGINES;
    int rc;
    char l[4], nm[13];
    if (ce.save_choice == 0u && psnd[part].kind != PK_INIT) {     /* Overwrite */
        cu_cpy(nm, psnd[part].name, sizeof nm);
        if (psnd[part].kind == PK_USER && up_used(psnd[part].slot)) {
            k = psnd[part].slot;
            f = cb_bind_raw(k);                    /* (its binding as it is) */
            memcpy(nm, up_rec(k)->name, 12);      /* (its name as stored, case and all) */
            nm[12] = 0;
        } else {
            f = psnd[part].kind == PK_FACTORY ? psnd[part].fk : PF_NONE;
            k = f != PF_NONE ? pool_bound_slot(e, f) : UP_SLOTS;
            if (k >= UP_SLOTS)
                k = cb_free_slot();
        }
        if (k >= UP_SLOTS) {
            cu_message("no free slot", CR_COL_RED);
            cu_trace("save: no free slot\n");
            return;
        }
        rc = cb_store(t, part, k, nm, f != PF_NONE ? f + 1u : 0u);
        up_slot_label(l, k);
        cu_trace("save: part %u overwrite slot %s %s %02u %s rc %d\n", (unsigned)part, l, ENGINES[e]->name,
                 (unsigned)(f != PF_NONE ? pool_pos_factory(e, f) : pool_pos_slot(e, k)), nm, rc);
        cu_save_done(part, k, rc, "saved ");
        return;
    }
    k = cb_free_slot();                            /* Save as new: the next free slot, the pool's next place */
    if (k >= UP_SLOTS) {
        cu_message("no free slot", CR_COL_RED);
        cu_trace("save: no free slot\n");
        return;
    }
    ce.slot = (uint8_t)k;
    ce.save_pos = (uint8_t)pool_pos_new(e, k);
    ce.save_step = 1;
    cn_open(psnd[part].name);
    up_slot_label(l, k);
    cu_trace("save: new part %u %s %02u (slot %s)\n", (unsigned)part, ENGINES[e]->name, (unsigned)ce.save_pos, l);
}

/* OCT+ (or SAVE) on the naming page: the part's sound -> slot ce.slot, added to the pool, named */
static void cu_save_commit(void)
{
    uint32_t part = ce.save_part, k = ce.slot;
    track_t *t = &trk[part ? CR_PART_BASS : CR_PART_CHORD];
    char name[16], l[4];
    int rc;
    if (ce.save_step == 0u) {
        cu_save_take();
        return;
    }
    cn_result(name);
    if (up_used(k)) {                              /* (taken meanwhile: a SysEx, the backup) */
        k = cb_free_slot();
        if (k >= UP_SLOTS) {
            cu_message("no free slot", CR_COL_RED);
            return;
        }
    }
    rc = cb_store(t, part, k, name, 0);
    up_slot_label(l, k);
    cu_trace("save: part %u slot %s name %s rc %d\n", (unsigned)part, l, name, rc);
    cu_save_done(part, k, rc, "saved ");
}

/* SAVE held 1 s in the dialog: the question about the current preset (none for INIT or a factory preset) */
static void cu_save_ask(void)
{
    uint32_t part = ce.save_part;
    if (psnd[part].kind != PK_USER || !up_used(psnd[part].slot)) {
        cu_message(psnd[part].kind == PK_INIT ? "INIT: nothing to delete" : "factory: nothing to reset", CR_COL_RED);
        cu_trace("save: nothing to delete\n");
        return;
    }
    ce.del_ask = cb_bound(psnd[part].slot) != PF_NONE ? 2u : 1u;
    cu_trace("save: %s %s?\n", ce.del_ask == 2u ? "reset" : "delete", psnd[part].name);
}

/* OCT+ on the question: the record deleted (upreset.c up_put(k, 0), as Felucca's ERASE). Reset: the factory preset
 * back at its place (loaded); delete: the pool closes up, the part loads what is now at its place (the one before at
 * the end). The other part, if it played that record: its sound stays, unsaved (edited, no place: INIT's) */
static void cu_save_delete(void)
{
    uint32_t part = ce.save_part, k = psnd[part].slot, other = part ^ 1u, f, pos, n;
    track_t *t = &trk[part ? CR_PART_BASS : CR_PART_CHORD];
    uint32_t e = t->eng_req % NENGINES;
    char l[4], b[24], pl[24];
    int rc, reset = ce.del_ask == 2u;
    ce.del_ask = 0;
    if (psnd[part].kind != PK_USER || k >= UP_SLOTS)
        return;
    f = cb_bound(k);
    pos = cu_part_pos(part);
    rc = up_put(k, 0);
    up_slot_label(l, k);
    cu_trace("save: %s slot %s rc %d\n", reset ? "reset" : "delete", l, rc);
    if (rc != 0 && rc != 3) {
        cu_message("delete error", CR_COL_RED);
        return;
    }
    if (psnd[other].kind == PK_USER && psnd[other].slot == k) {
        if (f != PF_NONE)
            psnd[other].kind = PK_FACTORY;
        else
            psnd[other].kind = PK_INIT;
        psnd[other].edited = 1;
    }
    n = pool_count(e);
    if (reset)
        pos = pool_pos_factory(e, f);
    else if (pos >= n)
        pos = n - 1u;
    cu_pool_load(part, e, pos);
    if (!part)
        cu_sends_to_fx();
    cb_pool_check();
    cu_place(pl, e, pos, " ");
    cu_cpy(b, reset ? "reset " : "deleted ", sizeof b);
    cu_cat(b, reset ? pl : l, sizeof b);
    cu_message(b, reset ? CR_COL_GREEN : CR_COL_RED);
    cu_trace("save: now part %u %s %s\n", (unsigned)part, pl, psnd[part].name);
    cu_save_close();
}

static void cu_layer_pick(uint32_t l, int32_t i)   /* a white root / SELECT: the picker's item i */
{
    uint32_t n;
    if (i < 0)
        return;
    switch (l) {
    case L_PERF:
        if (i < 7) {
            if (cs.perf_sel != (uint8_t)i)
                cu_hot.knob = 0;                  /* another mode: its row has no hot cell */
            cu_perf_pick((uint32_t)i);
            if (!cs.perform_on) {
                cs.perform_on = 1;
                cr_post(CRE_PERFORM, 0, 0, 1);
            }
        }
        break;
    case L_FX:
        if (i < (int32_t)CU_NFX) {
            if (cs.fx_sel != (uint8_t)i)
                cu_hot.knob = 0;                  /* another effect: its row has no hot cell */
            cs.fx_sel = (uint8_t)i;
            cu_trace("fx: effect %s\n", CU_FX[i].name);
        }
        break;
    case L_BASS:
        if (i < 4) {
            if (cs.bass_mode != (uint8_t)i)
                cu_hot.knob = 0;
            cs.bass_mode = (uint8_t)i;
            cr_post(CRE_BASS_MODE, 0, 0, i);
            cu_trace("bass: behaviour %s\n", CU_BASSMODE[i]);
        }
        break;
    case L_EDIT:
        cu_engine_at(0, &n);
        if (i < (int32_t)n) {
            cpk.changed |= cu_edit_trk()->eng_req != cu_engine_at((uint32_t)i, 0);
            cu_engine_pick((uint32_t)i);
        }
        break;
    case L_LOOP:                                   /* SELECT: the length (the next take's) */
        if (i < (int32_t)CRL_NSYNC) {
            cs.loop_len = (uint8_t)i;
            cr_post(CRE_LOOP, LP_CONF, LC_SYNC, i);
            cu_trace("loop: length %d\n", (int)i);
            if (cr_snap.lcap != CRL_CAP_NONE || cu_playing()) {   /* (the middle shows the timeline: say it) */
                char t[24];
                cu_cpy(t, "next take: ", sizeof t);
                cu_cat(t, CU_LOOPLEN[i], sizeof t);
                cu_message(t, CR_COL_RED);
            }
        }
        break;
    case L_SAVE:                                   /* SELECT / KNOB 1: save load delete */
        if (i < 3)
            cs.save_act = (uint8_t)i;
        break;
    case L_METRO:                                  /* the time signature */
        if (i < (int32_t)CRL_NSIG) {
            cs.metro_sig = (uint8_t)i;
            cr_post(CRE_LOOP, LP_CONF, LC_SIG, i);
        }
        break;
    default:
        break;
    }
}
static int32_t cu_layer_sel(uint32_t l)
{
    switch (l) {
    case L_PERF: return cs.perf_sel;
    case L_FX: return cs.fx_sel;
    case L_BASS: return cs.bass_mode;
    case L_EDIT: return cu_engine_rank(cu_edit_trk()->eng_req % NENGINES);
    case L_LOOP: return cs.loop_len;
    case L_SAVE: return cs.save_act;
    case L_METRO: return cs.metro_sig;
    default: return 0;
    }
}
static int32_t cu_layer_count(uint32_t l)
{
    uint32_t n;
    switch (l) {
    case L_PERF: return 7;
    case L_FX: return CU_NFX;
    case L_BASS: return 4;
    case L_EDIT: cu_engine_at(0, &n); return (int32_t)n;
    case L_LOOP: return (int32_t)CRL_NSYNC;
    case L_SAVE: return 3;
    case L_METRO: return CRL_NSIG;
    default: return 0;
    }
}

static void cu_layer_key(uint32_t l, uint32_t k)   /* a root key while layer l is open */
{
    int32_t wi = cu_white_idx(k);
    if (l == L_KEY) {                              /* the tonic; MIN held: minor */
        cs.tonic = (uint8_t)((53u + k) % 12u);
        cs.scale = (cu.kheld >> 3) & 1u ? CR_SCALE_MINOR : CR_SCALE_MAJOR;
        cs.key_on = 1;
        cu_post_key();
        return;
    }
    if (l == L_LOOP && k == 10u) {                 /* D#4: CLEAR, held 1 s (cu_clear_poll) */
        cu.clear_t0 = cu_now() | 1u;
        cu_message("hold to clear", CR_COL_RED);
        return;
    }
    if (l == L_LOOP && k == 13u) {                 /* F#4: UNDO */
        cr_post(CRE_LOOP, LP_UNDO, 0, 0);
        return;
    }
    if ((l == L_LOOP || l == L_SAVE) && wi >= 0) { /* the white roots: slots 1..10 */
        if (wi < (int32_t)CRL_SLOTS) {
            if (l == L_LOOP)
                cu_loop_load((uint32_t)wi, 0);
            else
                cs.loop_target = (uint8_t)wi;
        }
        return;
    }
    cu_layer_pick(l, wi);
}

static void cu_layer_knob(uint32_t l, uint32_t knob, int32_t s)   /* KNOB 1..4 (0..3) while layer l is open */
{
    int32_t v;
    switch (l) {
    case L_KEY:
        if (knob == 0) {                           /* TONIC */
            cs.tonic = (uint8_t)((cs.tonic + 12 + s % 12) % 12);
            cs.key_on = 1;
            cu_post_key();
            cu_trace("key: knob 1 tonic %s\n", CU_NOTE[cs.tonic]);
        } else if (knob == 1) {                    /* SCALE */
            cs.scale = s > 0 ? CR_SCALE_MINOR : CR_SCALE_MAJOR;
            cu_post_key();
            cu_trace("key: knob 2 scale %s\n", cs.scale ? "minor" : "major");
        } else if (knob == 2) {                    /* TRANSPOSE */
            v = cs.transpose + s;
            cs.transpose = (int8_t)(v < -24 ? -24 : v > 24 ? 24 : v);
            cr_post(CRE_TRANSPOSE, 0, 0, cs.transpose);
            cu_trace("key: knob 3 transpose %d\n", (int)cs.transpose);
        } else {                                   /* SINGLE NOTES: Full Octave / Split */
            cs.single = s > 0;
            cu_post_single();
            cu_trace("key: knob 4 single %s\n", cs.single ? "split" : "full");
        }
        cu_hot_set(L_KEY, knob);                  /* (no popup: the knob row is the readout, its cell turns hot) */
        break;
    case L_PERF:
        cu_param_turn(cu_perf_mode(), CU_PERF_KNOB[cu_perf_mode()][knob], s, 0);
        cu_hot_set(L_PERF, knob);                 /* (no popup: the cell turns hot) */
        break;
    case L_FX:
        if (knob == 3) {
            v = cs.fx_amt[cs.fx_sel] + s * 4;
            cs.fx_amt[cs.fx_sel] = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
            cs.fx_on = 1;
            cu_fx_apply();
            cu_hot_set(L_FX, 3);                    /* (no popup: the knob row is the readout, its cell turns hot) */
            cu_trace("fx: knob 4 %s amount %u\n", CU_FX[cs.fx_sel].name, (unsigned)cs.fx_amt[cs.fx_sel]);
        } else if (CU_FX[cs.fx_sel].g[knob] >= 0) {
            uint32_t g = (uint32_t)CU_FX[cs.fx_sel].g[knob];
            v = song.g[g] + s * (GP[g].max - GP[g].min > 20 ? 2 : 1);
            song.g[g] = (int16_t)(v < GP[g].min ? GP[g].min : v > GP[g].max ? GP[g].max : v);
            cu_hot_set(L_FX, knob);
            cu_trace("fx: knob %u %s %s %d\n", (unsigned)knob + 1u, CU_FX[cs.fx_sel].name, CU_FX[cs.fx_sel].gname[knob],
                     (int)song.g[g]);
        }
        break;
    case L_BASS:
        if (knob == 0) {                           /* BEHAVIOUR */
            v = cs.bass_mode + (s > 0 ? 1 : -1);
            cu_layer_pick(L_BASS, v < 0 ? 0 : v > 3 ? 3 : v);
        } else if (knob == 1) {                    /* REGISTER */
            cr_post(CRE_BASS_VOICING, 0, 0, s);
            cu_trace("bass: knob 2 register %+d\n", (int)s);
        } else if (knob == 2) {                    /* SOUND */
            v = (int32_t)cs.bass_sound + s;
            cu_bass_go((uint32_t)(v < 0 ? 0 : v));
            cu_trace("bass: knob 3 sound %u\n", (unsigned)cs.bass_sound);
        } else {                                   /* LEVEL */
            v = trk[CR_PART_BASS].p[P_LEVEL] + s * 2;
            trk[CR_PART_BASS].p[P_LEVEL] = (int16_t)(v < 0 ? 0 : v > 127 ? 127 : v);
            cu_trace("bass: knob 4 level %d\n", (int)trk[CR_PART_BASS].p[P_LEVEL]);
        }
        cu_hot_set(L_BASS, knob);                 /* (no popup: the knob row is the readout, its cell turns hot) */
        break;
    case L_EDIT:
        if (knob == 0) {                           /* the engine's pool, as PRESETS (a preview: the bar jumps) */
            cu_pool_step(s);
            cpk.changed = 1;
            cu_pool_popup(ce.part, cu_part_pos(ce.part));
            cu.pop.jump = 1;
        } else if (knob == 1) {                    /* INIT */
            cu_sound_init(ce.part);
            cpk.changed = 1;
        } else if (knob == 3) {                    /* the white roots: engines <-> play (Settings: pick_roots) */
            cs.pick_roots ^= 1u;
            cu_message(cs.pick_roots ? "roots: engines" : "roots: play", CR_COL_WHITE);
            cu_trace("picker: roots %s\n", cs.pick_roots ? "engines" : "play");
        }
        break;
    case L_LOOP:                                   /* the PLAY menu: QUANTIZE COUNT-IN LEVEL KEYS */
        if (knob == 0) {
            v = cs.loop_quant + (s > 0 ? 1 : -1);
            cs.loop_quant = (uint8_t)(v < 0 ? 0 : v >= (int32_t)CRL_NQUANT ? CRL_NQUANT - 1u : (uint32_t)v);
            cr_post(CRE_LOOP, LP_CONF, LC_QUANT, cs.loop_quant);
            cu_trace("loop: knob 1 quantize %s\n", CU_QUANT[cs.loop_quant]);
        } else if (knob == 1) {
            cs.loop_count_in = s > 0;
            cr_post(CRE_LOOP, LP_CONF, LC_COUNTIN, cs.loop_count_in);
            cu_trace("loop: knob 2 count-in %s\n", cs.loop_count_in ? "on" : "off");
        } else if (knob == 2) {
            v = cs.loop_level + s * 5;
            cs.loop_level = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v);
            cr_post(CRE_LOOP, LP_CONF, LC_LEVEL, cs.loop_level);
            cu_trace("loop: knob 3 level %u\n", (unsigned)cs.loop_level);
        } else {
            cu_loop_keys(s > 0);
        }
        cu_hot_set(L_LOOP, knob);                 /* (no popup: the cell turns hot) */
        break;
    case L_SAVE:
        if (knob == 0)
            cu_layer_pick(L_SAVE, (int32_t)(cs.save_act + 3u + (s > 0 ? 1u : 2u)) % 3);
        break;
    case L_METRO:                                  /* KNOB 1: the click level */
        if (knob == 0) {
            v = cs.metro_vol + s * 5;
            cs.metro_vol = (uint8_t)(v < 0 ? 0 : v > 100 ? 100 : v);
            cr_post(CRE_LOOP, LP_CONF, LC_VOL, cs.metro_vol);
            cu_trace("metro: knob 1 click %u\n", (unsigned)cs.metro_vol);
            cu_hot_set(L_METRO, 0);               /* (no popup: the cell turns hot) */
        }
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------- the scan --- */
static void cu_activity(void)
{
    if (cu.armed != NB) {                         /* something touched during a hold: a combo, the layer at once */
        cu.combo = 1;
        if (!cu.open) {
            cu.open = 1;
#if CR_EDIT_HOOKS
            if (cu.page == PG_EDIT && !cu_layer() && ce_owns(cu.armed))
                return;                           /* the editor's button: a combo, no layer */
#endif
            cu_opened(cu.armed);
        }
    }
}

/* B3 = LOCK (the chord block's latch mode). A UI-level virtual hold: a latched chord key stays posted down to the
 * engine (CRE_MOD down, no up), so every play style sees it held. LOCK on: the chord keys held are latched when
 * released; with no chord key held, a top-row key (DIM MIN MAJ SUS) resets the latch to that type alone and a
 * bottom-row key (6 m7 M7 9) toggles its extension. LOCK off and PANIC clear the latch. */
#define CU_MOD_TOP ((1u << CR_MOD_DIM) | (1u << CR_MOD_MIN) | (1u << CR_MOD_MAJ) | (1u << CR_MOD_SUS))
static uint32_t cu_mods_phys(void)                 /* bit cr_mod_t: the chord keys physically held */
{
    uint32_t k, m = 0;
    for (k = 0; k < CU_ROOT0; k++)
        if (CU_MOD_OF_KEY[k] != 0xFFu && ((cu.kheld >> k) & 1u))
            m |= 1u << CU_MOD_OF_KEY[k];
    return m;
}
static void cu_mods_trace(void)
{
    static const char *const NM[CR_NMOD] = {"DIM", "MIN", "MAJ", "SUS", "6", "m7", "M7", "9"};
    uint32_t m;
    cu_trace("lock: latched");
    for (m = 0; m < CR_NMOD; m++)
        if ((cu.mlatch >> m) & 1u)
            cu_trace(" %s", NM[m]);
    cu_trace("%s\n", cu.mlatch ? "" : " none");
    (void)NM;
}
static void cu_unlatch(void)                       /* the latched keys not held: up */
{
    uint32_t m, held = cu_mods_phys();
    for (m = 0; m < CR_NMOD; m++)
        if (((cu.mlatch >> m) & 1u) && !((held >> m) & 1u))
            cr_post(CRE_MOD, m, 0, 0);
    if (cu.mlatch) {
        cu.mlatch = 0;
        cu_mods_trace();
    }
    cu.mkill = 0;
}
static void cu_lock_tap(void)                      /* B3 */
{
    cu.clock ^= 1u;
    if (!cu.clock)
        cu_unlatch();
    cu_trace("lock: %s\n", cu.clock ? "on" : "off");
}
static void cu_mod_press(uint32_t k)               /* a chord key down (k < CU_ROOT0, not B3) */
{
    uint32_t m = CU_MOD_OF_KEY[k], bit = 1u << m, x;
    uint32_t others = cu_mods_phys() & ~bit;
    if (cu.clock && !others && cu.mlatch) {        /* nothing else held, a latch: reset / toggle */
        if (bit & CU_MOD_TOP) {
            for (x = 0; x < CR_NMOD; x++)
                if (x != m && ((cu.mlatch >> x) & 1u))
                    cr_post(CRE_MOD, x, 0, 0);
            cu.mlatch &= (uint8_t)bit;             /* (already down when it was latched) */
        } else if (cu.mlatch & bit) {
            cu.mlatch &= (uint8_t)~bit;
            cu.mkill |= (uint8_t)bit;
            cr_post(CRE_MOD, m, 0, 0);
            cu_mods_trace();
            return;
        }
    }
    cr_post(CRE_MOD, m, 0, 1);                     /* (the engine ignores a second down) */
}
static void cu_mod_release(uint32_t k)
{
    uint32_t m = CU_MOD_OF_KEY[k], bit = 1u << m;
    if (cu.mkill & bit) {                          /* toggled off by this press */
        cu.mkill &= (uint8_t)~bit;
        return;
    }
    if (cu.clock) {                                /* LOCK: stays down */
        cu.mlatch |= (uint8_t)bit;
        cu_mods_trace();
        return;
    }
    cr_post(CRE_MOD, m, 0, 0);
}

/* the root keys belong to layer l (its map), else they play: PERF and FX play the chord so a mode or an effect is
 * heard at once (SELECT picks there: docs/PRESETS.md "Also in this pass"), the engine picker with its roots off
 * plays the preview */
static int cu_layer_keys(uint32_t l)
{
    return l && l != L_PERF && l != L_FX && !(l == L_EDIT && !cs.pick_roots) && !(l == L_LOOP && !cs.loop_keys);
}   /* (the PLAY menu with Keys = Play: they play, docs/LOOPER-MODES.md) */

static void cu_key_press(uint32_t k)
{
    uint32_t l;
    cu.kheld |= 1u << k;
    cu.last_sound = cu_now();
    cu_activity();
    if (k < CU_ROOT0) {                            /* the chord block keeps its job in every layer */
        if (CU_MOD_OF_KEY[k] != 0xFFu)
            cu_mod_press(k);
        else
            cu_lock_tap();
        return;
    }
    l = cu_layer();
    if (cu_layer_keys(l)) {                        /* (PERF, FX, the picker with its roots off: they play) */
        cu_layer_key(l, k);
        cu.key_note[k] = 0xFFu;
        return;
    }
    if (cu.page == PG_SAVE && ce.save_step) {      /* naming: the roots type, D#4 a space, F#4 deletes (the
                                                    * choice before it: they play) */
        int32_t wi = cu_white_idx(k);
        if (wi >= 0)
            cn_white((uint32_t)wi, cu_now());
        else if (k == 10u)
            cn_space();
        else if (k == 13u)
            cn_delete();
        cu.key_note[k] = 0xFFu;
        return;
    }
    if (cu.opt_open) {                             /* Options: the white roots index the settings */
        int32_t wi = cu_white_idx(k);
        if (wi >= 0 && wi < (int32_t)cu_opt_n())
            cu.opt_sel = (uint8_t)wi;
        cu.key_note[k] = 0xFFu;
        return;
    }
    {
        int32_t n = 53 + (int32_t)k + 12 * cu.octave;
        n = n < 0 ? 0 : n > 127 ? 127 : n;
        cr_post(CRE_KEY, (uint32_t)n, cs.vel, 1);
        cu.key_note[k] = (uint8_t)(n + 1);
    }
}

static void cu_key_release(uint32_t k)
{
    cu.kheld &= ~(1u << k);
    if (k < CU_ROOT0) {
        if (CU_MOD_OF_KEY[k] != 0xFFu)
            cu_mod_release(k);
        return;
    }
    if (cu.key_note[k] && cu.key_note[k] != 0xFFu)
        cr_post(CRE_KEY, cu.key_note[k] - 1u, 0, 0);
    cu.key_note[k] = 0;
}

static void cu_btn_press(uint32_t b)
{
    uint32_t octs = CU_BIT(B_OCTDN) | CU_BIT(B_OCTUP);
    cu.bheld |= CU_BIT(b);
    if (b == B_OCTDN || b == B_OCTUP) {
        if (b == B_OCTDN)
            cu.oct_mod = 0;
        if ((cu.bheld & octs) == octs && !cu.oct_chord) {
            cu.oct_chord = 1;
            cu_panic();
        }
        return;
    }
    if (b == BT_HOME) {                           /* (its release: home; a held button's release: no tap) */
        if (cu.armed != NB)
            cu.combo = 1;
        return;
    }
    if (cu.armed == NB) {
        if (b == BT_EDIT && cu.page != PG_EDIT)   /* EDIT held: the engine picker of the chord sound */
            ce.part = 0;
        cu.armed = (uint8_t)b;
        cu.t0 = cu_now();
        cu.open = cu.combo = 0;
        return;
    }
    /* another button during a hold: a combo (its release does nothing) */
    cu.swallow |= CU_BIT(b);
#if CR_EDIT_HOOKS
    if (cu.page == PG_EDIT && cu.armed == BT_OPT && b == BT_EDIT) {   /* SHIFT + EDIT in the editor: chord / bass */
        cu.combo = 1;
        ce_button(b, CE_SHIFT);
        return;
    }
#endif
    cu_activity();
    if (cu.armed == BT_BASS && b == BT_EDIT) {     /* BASS held + EDIT / SAVE: the bass sound's editor, saving */
        cu.lock = L_NONE;
        cu_edit_open(1);
    }
    if (cu.armed == BT_BASS && b == BT_SAVE) {
        cu.lock = L_NONE;
        cu_save_open(1);
    }
}

static void cu_btn_release(uint32_t b)
{
    uint32_t octs = CU_BIT(B_OCTDN) | CU_BIT(B_OCTUP);
    cu.bheld &= ~CU_BIT(b);
    if (b == B_OCTDN || b == B_OCTUP) {
        if (!cu.oct_chord && !(b == B_OCTDN && cu.oct_mod))
            cu_oct_tap(b);
        if (b == B_OCTDN)
            cu.oct_mod = 0;
        if (!(cu.bheld & octs))
            cu.oct_chord = 0;
        return;
    }
    if (b == BT_HOME) {
        if (cu.swallow & CU_BIT(b))               /* (held through the calibration) */
            cu.swallow &= ~CU_BIT(b);
        else
            cu_home_tap();
        return;
    }
    if (cu.swallow & CU_BIT(b)) {
        cu.swallow &= ~CU_BIT(b);
        return;
    }
    if (b == cu.armed) {
        if (b == BT_OPT)
            cu_epk_commit();                       /* OPT + PRESETS: the engine picked */
        if (!cu.open && !cu.combo)
            cu_tap(b);
        cu.armed = NB;
        cu.open = cu.combo = 0;
    }
}

/* OPT held + PRESETS (docs/PRESETS.md; mock-up design/choralroot-fm1-preset-screens.png 2): the chord part's engine,
 * a horizontal picker shown while OPT is held (each detent an engine, wrapping); on OPT's release the part switches to
 * it and lands on the place it last had in that engine's pool (cs.pool_pos; the first preset before that). Not in the
 * editor (its EDIT-held picker) nor on the save dialog */
static void cu_epk_step(int32_t s)
{
    uint32_t n;
    int32_t r;
    cu_engine_at(0, &n);
    if (!n)
        return;
    if (!cu.epk_on) {
        r = cu_engine_rank(trk[CR_PART_CHORD].eng_req % NENGINES);
        cu.epk_on = 1;
        cu.epk_rank = (uint8_t)(r < 0 ? 0 : r);
        cu.pop.until = 0;
    }
    cu.epk_rank = (uint8_t)(((int32_t)cu.epk_rank + s % (int32_t)n + (int32_t)n) % (int32_t)n);
    cu_trace("engine pick: %s (%u presets)\n", ENGINES[cu_engine_at(cu.epk_rank, 0)]->name,
             (unsigned)pool_count(cu_engine_at(cu.epk_rank, 0)) - 1u);
}
static void cu_epk_commit(void)                   /* OPT released: the engine picked */
{
    uint32_t e, pos, n;
    if (!cu.epk_on)
        return;
    cu.epk_on = 0;
    e = cu_engine_at(cu.epk_rank, 0);
    if (e == trk[CR_PART_CHORD].eng_req % NENGINES) {
        cu_trace("sound: part 0 engine %s kept\n", ENGINES[e]->name);
        return;
    }
    cu_pos_sync();                                 /* (the place in the engine left: remembered) */
    n = pool_count(e);
    pos = cs.pool_pos[0][eng_rank(e)];
    if (pos >= n)
        pos = n > 1u ? 1u : 0u;
    cu_pool_load(0, e, pos);
    cu_sends_to_fx();
    cu_sound_popup(0);
    cu_trace("sound: part 0 engine %s pos %u %s\n", ENGINES[e]->name, (unsigned)cs.sound, psnd[0].name);
}
static void cu_epk_screen(cr_screen_t *s);

static void cu_knob(uint32_t role, int32_t s)
{
    uint32_t l;
    int32_t v;
    cu_activity();
    if (cu_shift() && cu.page == PG_EDIT && (role >= EN_K1 || role == EN_SELECT) && !cu_layer()) {
        if (role == EN_SELECT)                     /* the editor wins: SHIFT + SELECT steps as SELECT */
            ce_select(s);
        else                                       /* .. SHIFT + a knob: fine (one step) */
            ce_knob(role - EN_K1, s, 1);
        return;
    }
    if (cu_shift() && role == EN_PRESET && cu.page != PG_EDIT && cu.page != PG_SAVE && !cu_layer()) {
        cu_epk_step(s);                            /* OPT held + PRESETS: the chord part's engine (on OPT's release) */
        return;
    }
    if (cu_shift() && (role == EN_ALGO || role == EN_K1 || role == EN_SELECT)) {   /* OPT held: second functions
                                                                                    * (the other knobs: their own) */
        if (role == EN_ALGO) {
            v = trk[CR_PART_BASS].p[P_LEVEL] + s * 2;
            trk[CR_PART_BASS].p[P_LEVEL] = (int16_t)(v < 0 ? 0 : v > 127 ? 127 : v);
            cu_popup_num(trk[CR_PART_BASS].p[P_LEVEL], 0, "", "bass level", CR_COL_ORANGE, 0, 127, 12);
        } else if (role == EN_K1) {                /* the Single Notes split point */
            cs.split = (uint8_t)((cs.split + 12 + s % 12) % 12);
            cu_post_single();
            cu_popup(CU_NOTE[cs.split], cs.single ? "split" : "split off", "split point", CR_COL_BLUE, cs.split, 0, 11, 12);
        } else {                                   /* SELECT: the metronome level */
            cu_layer_knob(L_METRO, 0, s);
            if (cu_layer() != L_METRO)             /* (outside the layer: the popup is the readout) */
                cu_popup_num(cs.metro_vol, 0, "%", "click level", CR_COL_WHITE, 0, 100, 10);
        }
        return;
    }
    l = cu_layer();
#if CR_EDIT_HOOKS
    if (role == EN_PRESET && !l && cu.page == PG_EDIT) {   /* PRESETS in the editor: the picker, previewing */
        cu_pick_open();
        l = L_EDIT;
    }
#endif
    if (l == L_EDIT && role == EN_PRESET) {        /* .. in the picker: its presets, as KNOB 1 */
        cu_layer_knob(l, 0, s);
        return;
    }
    if (l && role >= EN_K1) {
        cu_layer_knob(l, role - EN_K1, s);
        return;
    }
    if (l == L_LOOP && role == EN_PRESET) {        /* the PLAY menu: PRESETS the slot, ALGORITHM the record mode */
        cu_loop_turn(s);
        return;
    }
    if (l == L_LOOP && role == EN_ALGO) {
        cu_loop_mode_turn(s);
        return;
    }
    if (l && role == EN_SELECT && l != L_KEY) {    /* a picker: SELECT moves */
        v = cu_layer_sel(l) + s;
        v = v < 0 ? 0 : v >= cu_layer_count(l) ? cu_layer_count(l) - 1 : v;
        cu_layer_pick(l, v);
        return;
    }
    if (cu.opt_open && (role == EN_SELECT || role == EN_K1)) {
        if (role == EN_SELECT) {
            v = cu.opt_sel + s;
            cu.opt_sel = (uint8_t)(v < 0 ? 0 : v >= (int32_t)cu_opt_n() ? (int32_t)cu_opt_n() - 1 : v);
        } else {
            opt_set(cu.opt_sel, opt_get(cu.opt_sel) + s * (cu.opt_sel == O_VEL ? 4 : 1));
        }
        return;
    }
    if (cu.page == PG_EDIT && (role == EN_SELECT || role >= EN_K1)) {   /* the sound editor */
        if (role == EN_SELECT)
            ce_select(s);
        else
            ce_knob(role - EN_K1, s, 0);
        return;
    }
    if (cu.page == PG_SAVE && !ce.save_step && (role == EN_K1 || role == EN_SELECT)) {   /* the choice */
        ce.save_choice = s > 0 ? 1u : 0u;
        ce.del_ask = 0;
        cu_trace("save: choice %s\n", ce.save_choice ? "save as new" : "overwrite");
        return;
    }
    if (cu.page == PG_SAVE && ce.save_step && role == EN_K2) {            /* naming: the last letter */
        cn_knob(s);
        return;
    }
    switch (role) {
    case EN_K1:
        cr_post(CRE_VOICING, 0, 0, s);
        cu.pop.until = cu_now() + CR_POPUP_MS;
        cu.pop.kind = PU_VOICING;
        break;
    case EN_K2:
        cr_post(CRE_BASS_VOICING, 0, 0, s);
        cu.pop.until = cu_now() + CR_POPUP_MS;
        cu.pop.kind = PU_BASS_VOICING;
        break;
    case EN_K3:
        cu_param_turn(cu_perf_mode(), CU_PERF_KNOB[cu_perf_mode()][0], s, 1);
        break;
    case EN_K4:
        v = cs.fx_amt[cs.fx_sel] + s * 4;
        cs.fx_amt[cs.fx_sel] = (uint8_t)(v < 0 ? 0 : v > 127 ? 127 : v);
        cs.fx_on = 1;
        cu_fx_apply();
        {
            char lb[24];
            cu_cpy(lb, CU_FX[cs.fx_sel].name, sizeof lb);
            lb[0] = (char)(lb[0] | 0x20);
            cu_popup_num(cs.fx_amt[cs.fx_sel] * 99 / 127, 0, "", lb, CR_COL_GREEN, 0, 99, 12);
        }
        break;
    case EN_PRESET:                                /* the chord part's engine's pool, wrapping */
        {
            uint32_t n = pool_count(trk[CR_PART_CHORD].eng_req % NENGINES);
            cu_sound_go((cs.sound + (uint32_t)(s % (int32_t)n + (int32_t)n)) % n);
            cu_sound_popup(0);
            cu_trace("sound: part 0 pos %u %s\n", (unsigned)cs.sound, psnd[0].name);
        }
        break;
    case EN_ALGO:                                  /* OFF, then the bass part's engine's pool, wrapping */
        {
            int32_t n = (int32_t)pool_count(trk[CR_PART_BASS].eng_req % NENGINES) + 1;
            v = ((int32_t)cs.bass_sound + s % n + n) % n;
        }
        cu_bass_go((uint32_t)v);
        cu_sound_popup(1);
        cu_trace("sound: part 1 pos %u %s\n", (unsigned)cs.bass_sound, cs.bass_sound ? psnd[1].name : "off");
        break;
    case EN_SELECT:
        cu_set_tempo((int32_t)cs.bpm + s);
        cu_popup_num(cs.bpm, 0, "", "bpm", CR_COL_WHITE, 20, 300, 14);
        break;
    default:
        break;
    }
}

/* one scan: buttons first (a layer armed this pass owns the keys pressed in it), then keys, then knobs */
/* ---------------------------------------------------------- calibration --- */
/* Felucca's HARDWARE CALIBRATION (ui_input.c panel_setup) on ChoralRoot's screens, run from the UI frame instead of
 * a blocking loop: press each printed button (raw matrix edges, the table is what is being taught), turn each knob
 * right, then OCT+ keeps the new table (panel.c `panel`, saved by settings_save in the settings record as Felucca
 * does) and OCT- (as just taught) puts the old one back. 30 s without input cancels (Felucca's SETUP_IDLE_MS).
 * Entry: OCT- + OCT+ held at power-on (main.c, as Felucca) or Options > Calibrate, OCT+. */
#define CU_CAL_IDLE_MS 30000u
#define CU_CAL_DONE (NB + NE)                     /* the step asking OCT+ keep / OCT- discard */
static struct {
    uint8_t on, step, wait_up, req;               /* wait_up: every button let go first; req: asked at power-on */
    uint32_t used, t0, settle;                    /* matrix ids taught; the step's start; an encoder's 300 ms */
    panel_t old;
} cc;

static void cu_calib_start(void)
{
    uint32_t e;
    cc.on = 1;
    cc.step = 0;
    cc.used = 0;
    cc.wait_up = 1;
    cc.settle = 0;
    cc.old = panel;
    cc.t0 = cu_now();
    for (e = 0; e < 7u; e++)
        fm1_enc_take(e);
    cu_trace("calib: start\n");
}

static void cu_calib_end(int keep)
{
    if (keep) {
        panel.magic = PANEL_MAGIC;
        panel_init();                             /* (validated: a broken table falls back to the default) */
#if CR_HAVE_SETTINGS
        cr_settings_save();                       /* (Felucca's settings_save: the panel table in the record) */
#endif
        cu_message("calibrated", CR_COL_GREEN);
    } else {
        panel = cc.old;
        cu_message("calibration cancelled", CR_COL_WHITE);
    }
    cu_trace("calib: %s\n", keep ? "saved" : "cancelled");
    cc.on = 0;
    {                                             /* what is still held (OCT+ that kept it): its release does nothing */
        uint32_t b, held = 0;
        for (b = 0; b < NB; b++)
            held |= ((fm1_in.buttons >> panel.btn[b]) & 1u) << b;
        cu.bheld = held;
        cu.swallow = held;
        cu.oct_chord = (uint8_t)((held & (CU_BIT(B_OCTDN) | CU_BIT(B_OCTUP))) != 0u);
    }
    cu.armed = NB;
    cu.open = cu.combo = 0;
    cu_activity();
}

static void cu_calib_input(uint32_t pe, uint32_t bm)
{
    uint32_t now = cu_now(), e, id;
    if (now - cc.t0 > CU_CAL_IDLE_MS) {
        cu_calib_end(0);
        return;
    }
    if (cc.wait_up) {                             /* (the gesture that started it: let go first) */
        if (!bm)
            cc.wait_up = 0;
        return;
    }
    if (cc.step < NB) {                           /* PRESS <label>: a matrix button not taught yet */
        pe &= ~cc.used & 0x3FFFu;
        if (!pe)
            return;
        for (id = 0; id < 14u; id++)
            if ((pe >> id) & 1u)
                break;
        panel.btn[cc.step] = (uint8_t)id;
        cc.used |= 1u << id;
        cu_trace("calib: %s = button %u\n", B_NAME[cc.step], (unsigned)id);
        if (++cc.step == NB) {
            cc.used = 0;
            for (e = 0; e < 7u; e++)
                fm1_enc_take(e);
        }
        cc.t0 = now;
        return;
    }
    if (cc.step < CU_CAL_DONE) {                  /* TURN RIGHT <knob>: an encoder not taught yet */
        int32_t st = 0;
        if (cc.settle) {                          /* Felucca: 300 ms, then what the knob still sent is dropped */
            if (now - cc.settle < 300u)
                return;
            cc.settle = 0;
            for (e = 0; e < 7u; e++)
                if ((cc.used >> e) & 1u)
                    fm1_enc_take(e);
            if (cc.step == CU_CAL_DONE)
                return;
        }
        for (e = 0; e < 7u; e++)
            if (!((cc.used >> e) & 1u) && (st = fm1_enc_take(e)) != 0)
                break;
        if (e == 7u)
            return;
        panel.enc[cc.step - NB] = (uint8_t)e;
        panel.dir[cc.step - NB] = (int8_t)(st > 0 ? 1 : -1);
        cu_trace("calib: %s = encoder %u dir %d\n", E_NAME[cc.step - NB], (unsigned)e, st > 0 ? 1 : -1);
        cc.used |= 1u << e;
        cc.step++;
        cc.settle = now ? now : 1u;
        cc.t0 = now;
        return;
    }
    for (e = 0; e < 7u; e++)                      /* the last step: the knobs do nothing */
        fm1_enc_take(e);
    if ((pe >> panel.btn[B_OCTUP]) & 1u)          /* the new table's OCT+: keep, OCT-: put the old one back */
        cu_calib_end(1);
    else if ((pe >> panel.btn[B_OCTDN]) & 1u)
        cu_calib_end(0);
}

/* one big thing: the label to press / turn, huge; the ring shows how far through the 21 steps */
static void cu_calib_screen(cr_screen_t *s)
{
    char b[12];
    s->kind = CR_K_BIG;
    s->size = 40;
    s->col = CR_COL_WHITE;
    s->header = 0;                                /* (the ring's band: no top line; the ring is the progress) */
    s->ring_on = 1;
    s->ring_col = CR_COL_YELLOW;
    s->ring = (uint16_t)(cc.step * 256u / CU_CAL_DONE);
    if (cc.wait_up) {
        cu_cpy(s->value, "let go", sizeof s->value);
        cu_cpy(s->label, "of every button", sizeof s->label);
    } else if (cc.step < NB) {
        cu_cpy(s->value, B_NAME[cc.step], sizeof s->value);
        cu_cpy(s->label, "press", sizeof s->label);
    } else if (cc.step < CU_CAL_DONE) {
        cu_cpy(s->value, E_NAME[cc.step - NB], sizeof s->value);
        cu_cpy(s->label, "turn right", sizeof s->label);
    } else {
        cu_cpy(s->value, "done", sizeof s->value);
        s->col = CR_COL_GREEN;
        cu_cpy(s->sub, "OCT+ keeps", sizeof s->sub);
        cu_cpy(s->label, "OCT- discards", sizeof s->label);
    }
    if (cc.step < CU_CAL_DONE) {
        char n[4];
        cu_int(b, cc.step + 1, 0, sizeof b);
        cu_cat(b, "/", sizeof b);
        cu_int(n, CU_CAL_DONE, 0, sizeof n);
        cu_cat(b, n, sizeof b);
        cu_cpy(s->sub, b, sizeof s->sub);
    }
}

static void fm6_service(void);                     /* fm6_store.c (after this file): DX7 SysEx */
#if FELUCCA_CZ
static void cz_service(void);                      /* cz_store.c (after this file): Casio CZ-1 SysEx */
#endif
static void cr_ui_input(void)
{
    uint32_t pe = fm1_input_edges(0), ne = fm1_input_note_edges(), bm = fm1_in.buttons, km = fm1_in.notes;
    uint32_t cur = 0, edg = 0, b, k, now = cu_now();
    int32_t s;
    fm6_poll();                                    /* FM6: a sound's PTCH -> its patch (as ui_input.c) */
    fm6_service();                                 /* FM6: a DX7 SysEx frame from USB-MIDI (fm6_store.c) */
#if FELUCCA_CZ
    cz_bank_poll();                                /* CZ-1: a sound's BANK / PTCH -> its tone (cz_bank.c) */
    cz_service();                                  /* CZ-1: a Casio SysEx frame from USB-MIDI (cz_store.c) */
#endif
    if (cc.req) {                                  /* OCT- + OCT+ held at power-on (main.c panel_setup) */
        cc.req = 0;
        cu_calib_start();
    }
    if (cc.on) {
        cu_calib_input(pe, bm);
        (void)ne;
        return;
    }
    for (b = 0; b < NB; b++) {
        cur |= ((bm >> panel.btn[b]) & 1u) << b;
        edg |= ((pe >> panel.btn[b]) & 1u) << b;
    }
    for (b = 0; b < NB; b++) {                     /* the releases first (one let go as another goes down) */
        uint32_t was = (cu.bheld >> b) & 1u, is = (cur >> b) & 1u, e = (edg >> b) & 1u;
        if (was && (e || !is))
            cu_btn_release(b);
    }
    for (b = 0; b < NB; b++) {                     /* then the presses; a tap shorter than a scan: down and up */
        uint32_t was = (cu.bheld >> b) & 1u, is = (cur >> b) & 1u, e = (edg >> b) & 1u;
        if (!was && (is || e)) {
            cu_btn_press(b);
            if (!is)
                cu_btn_release(b);
        }
    }
    if (cu.armed == BT_SAVE && cu.page == PG_SAVE && !cu.open && !cu.combo && ((cu.bheld >> BT_SAVE) & 1u) &&
        now - cu.t0 >= 1000u) {                    /* naming: SAVE held 1 s asks to delete the slot (no loop layer) */
        cu.combo = 1;                              /* (its release: no cancel) */
        cu_save_ask();
    }
    if (cu.armed != NB && !cu.open && ((cu.bheld >> cu.armed) & 1u) && !(cu.armed == BT_SAVE && cu.page == PG_SAVE) &&
        now - cu.t0 >= (uint32_t)HOLD_MS[settings_hold % 4u]) {
#if CR_EDIT_HOOKS
        if (cu.page == PG_EDIT && !cu_layer() && ce_owns(cu.armed)) {   /* the editor's: its hold, no layer */
            cu.open = 1;
            cu.combo = 1;
            ce_button(cu.armed, CE_HOLD);
        } else
#endif
        {
            cu.open = 1;
            cu_opened(cu.armed);
        }
    }
    for (k = 0; k < CU_NKEY; k++) {
        uint32_t was = (cu.kheld >> k) & 1u, is = (km >> k) & 1u, e = (ne >> k) & 1u;
        if (was && (e || !is))
            cu_key_release(k);
    }
    for (k = 0; k < CU_NKEY; k++) {
        uint32_t was = (cu.kheld >> k) & 1u, is = (km >> k) & 1u, e = (ne >> k) & 1u;
        if (!was && (is || e)) {
            cu_key_press(k);
            if (!is)
                cu_key_release(k);
        }
    }
    {                                              /* step entry: every key up (nothing latched) after a chord: the
                                                    * cursor advances (the engine: if a chord was written there) */
        static uint8_t down;
        uint8_t dn = (uint8_t)(cu.kheld || cu.mlatch);
        if (cr_snap.lcap == CRL_CAP_STEP && down && !dn) {
            cr_post(CRE_LOOP, LP_STEP, 0, CRL_STEP_KEYSUP);
            cu_trace("loop: step keys up\n");
        }
        down = dn;
    }
    for (b = 0; b < NE; b++)
        if ((s = panel_enc(b)) != 0)
            cu_knob(b, s);
    if (cu.clear_t0) {                             /* D#4 held 1 s in the loop layer: CLEAR */
        if (!((cu.kheld >> 10) & 1u) || cu_layer() != L_LOOP || !cs.loop_keys)
            cu.clear_t0 = 0;
        else if (now - cu.clear_t0 >= 1000u) {
            cu.clear_t0 = 0;
            cr_post(CRE_LOOP, LP_CLEAR, 0, 0);
        }
    }
}

/* ------------------------------------------------------------- the LEDs --- */
/* as ui_input.c ui_leds: the picture built off-line, the glow (fm1_led_dim) first, one byte per column */
static uint8_t cu_led_pos[41];                     /* (col << 3) | row bit, 0xFF = none (FM1_KEYMAP) */
#ifndef LED_PLAY_GREEN
#define LED_PLAY_GREEN ((8u << 3) | 1u)            /* PLAY's green LED (ui_input.c): column 8, row bit 1 */
#endif
static void cu_led_init(void)
{
    uint32_t id, p, r;
    for (id = 0; id < 41u; id++) {
        cu_led_pos[id] = 0xFF;
        for (p = 0; p < FM1_NCOL; p++)
            for (r = 1; r < 5u; r++)
                if (FM1_KEYMAP[r][p] == (int8_t)id)
                    cu_led_pos[id] = (uint8_t)((p << 3) | r);
    }
}
static void cu_led(uint8_t *nl, uint32_t id, int on)
{
    uint8_t q = cu_led_pos[id % 41u];
    if (q != 0xFF && on)
        nl[q >> 3] |= (uint8_t)(1u << (q & 7u));
}
static int32_t cu_key_of_note(uint32_t note)       /* the root key a note sounds on (OCT shifted), -1 none */
{
    int32_t k = (int32_t)note - 53 - 12 * cu.octave;
    return k >= (int32_t)CU_ROOT0 && k < (int32_t)CU_NKEY ? k : -1;
}

static void cr_leds(void)
{
    uint8_t nl[FM1_NCOL] = {0}, nd[FM1_NCOL] = {0}, own[FM1_NCOL] = {0}, ld[FM1_NCOL] = {0};
    uint32_t k, b, now = cu_now(), blink = (uint32_t)cu_blink(now), l = cu_layer();
    const cr_snap_t *sn = &cr_snap;
    if (cu.msg.big == 1u && (int32_t)(cu.msg.until - now) > 0) {   /* panic: everything flashes */
        for (k = 0; k < 41u; k++)
            cu_led(nl, k < NB ? panel.btn[k] : k, (int)blink);
        for (k = 0; k < FM1_NCOL; k++) {
            fm1_led_dim[k] = 0;
            fm1_led[k] = nl[k];
        }
        return;
    }
    /* buttons: on = active */
    cu_led(nl, panel.btn[BT_KEY], cs.key_on);
    cu_led(nl, panel.btn[BT_PERF], cs.perform_on);
    cu_led(nl, panel.btn[BT_FX], cs.fx_on);
    cu_led(nl, panel.btn[BT_BASS], cs.bass_on);
    cu_led(nl, panel.btn[BT_LATCH], cs.sticky || sn->latching);
    cu_led(nl, panel.btn[BT_OPT], cu.opt_open || cu_shift() || (l == L_LOOP && cs.loop_keys));   /* (the PLAY menu:
                                                                                            * Keys = Loops) */
    cu_led(nl, panel.btn[BT_EDIT], cu.page == PG_EDIT && blink);   /* blinks while the editor is open */
    cu_led(nl, panel.btn[BT_SAVE], cu.page == PG_SAVE);
    cu_led(nl, panel.btn[BT_METRO], cs.metro);
    {                                                    /* REC: blinks capturing, lit armed; LOOP: a loop */
        uint32_t cap = sn->lcap;
        cu_led(nl, panel.btn[BT_REC], cap == CRL_CAP_ARMED || cap == CRL_CAP_OD_ARMED || cap == CRL_CAP_STEP ||
                                          ((cap == CRL_CAP_COUNTIN || cap == CRL_CAP_REC || cap == CRL_CAP_OD) && blink));
        cu_led(nl, panel.btn[BT_LOOP], sn->lstate != CRL_EMPTY);
        if (sn->lstate == CRL_PLAYING)                   /* its green LED while playing */
            nl[LED_PLAY_GREEN >> 3] |= (uint8_t)(1u << (LED_PLAY_GREEN & 7u));
    }
    if (l) {                                             /* the open layer's button blinks */
        uint8_t q = cu_led_pos[panel.btn[L_BTN[l]]];
        if (q != 0xFF)
            nl[q >> 3] &= (uint8_t)~(1u << (q & 7u));
        cu_led(nl, panel.btn[L_BTN[l]], (int)blink);
    }
#if CR_EDIT_HOOKS
    if (cu.page == PG_EDIT && !l)                        /* the editor's buttons (EDIT blinking, the section lit) */
        ce_leds(nl, blink);
#endif
    if (sn->lcap == CRL_CAP_STEP && (!l || l == L_LOOP) && !cu.opt_open && cu.page == PG_NONE) {
        cu_led(nl, panel.btn[B_OCTDN], 1);               /* step entry: OCT- back, OCT+ a rest */
        cu_led(nl, panel.btn[B_OCTUP], 1);
    } else if (cu_picker_ctx() || l) {                   /* a picker: OCT- back (lit), OCT+ OK (blinking; the PLAY
                                                          * menu: none) */
        cu_led(nl, panel.btn[B_OCTDN], 1);
        cu_led(nl, panel.btn[B_OCTUP], l != L_LOOP && blink);
    } else {
        cu_led(nl, panel.btn[B_OCTDN], cu.octave < 0);
        cu_led(nl, panel.btn[B_OCTUP], cu.octave > 0);
    }
    for (b = 0; b < NB; b++)
        cu_led(nd, panel.btn[b], 1);
    /* the chord block: lit while held / latched (LOCK's latch: posted down, so the engine's held); B3 = LOCK, lit on */
    for (k = 0; k < CU_ROOT0; k++) {
        if (CU_MOD_OF_KEY[k] == 0xFFu) {
            cu_led(nl, 14u + k, cu.clock);
            cu_led(nd, 14u + k, 1);
            continue;
        }
        cu_led(nl, 14u + k, (int)((sn->mods_active >> CU_MOD_OF_KEY[k]) & 1u));
        cu_led(nd, 14u + k, 1);
    }
    /* the roots: a layer's map, or the voiced notes where they sound */
    if ((l == L_LOOP && cs.loop_keys) || l == L_SAVE) {   /* the slots: lit = a loop, blinking = the one selected (the
                                                          * PLAY menu with Keys = Play: the keys as on the view) */
        uint32_t sel = l == L_LOOP ? cu_lm_slot() : cs.loop_target;
        for (k = CU_ROOT0; k < CU_NKEY; k++) {
            int32_t wi = cu_white_idx(k);
            if (wi < 0 || wi >= (int32_t)CRL_SLOTS) {
                if (l == L_LOOP && (k == 10u || k == 13u))   /* CLEAR, UNDO */
                    cu_led(nl, 14u + k, 1);
                continue;
            }
            cu_led(nd, 14u + k, 1);
            if ((uint32_t)wi == sel ? blink : (cs.loop_used >> wi) & 1u)
                cu_led(own, 14u + k, 1);
        }
    } else if (cu_layer_keys(l) && l != L_KEY) {
        int32_t sel = cu_layer_sel(l), n = cu_layer_count(l);
        for (k = CU_ROOT0; k < CU_NKEY; k++) {
            int32_t wi = cu_white_idx(k);
            if (wi >= 0 && wi < n)
                cu_led(wi == sel ? own : nd, 14u + k, 1);
        }
    } else if (l == L_KEY) {
        for (k = CU_ROOT0; k < CU_NKEY; k++)
            cu_led(cs.key_on && (53u + k) % 12u == cs.tonic ? own : nd, 14u + k, 1);
    } else if (cu.page == PG_SAVE && ce.save_step) {     /* naming: the keys that type lit (the letters, D#4 space) */
        for (k = CU_ROOT0; k < CU_NKEY; k++)
            if (cu_white_idx(k) >= 0 || k == 10u || k == 13u)
                cu_led(own, 14u + k, 1);
    } else if (cu.opt_open) {
        for (k = CU_ROOT0; k < CU_NKEY; k++) {
            int32_t wi = cu_white_idx(k);
            if (wi >= 0 && wi < (int32_t)cu_opt_n())
                cu_led(wi == cu.opt_sel ? own : nd, 14u + k, 1);
        }
    } else {
        uint32_t i;
        int32_t pk = -1;
        if (sn->ci.sounding && !sn->ldisp)              /* the player's notes (the loop's glow below) */
            for (i = 0; i < sn->ci.nnotes; i++) {
                int32_t kk = cu_key_of_note(sn->ci.notes[i]);
                if (kk >= 0)
                    cu_led(nl, 14u + (uint32_t)kk, 1);
            }
        if (sn->perform_on && sn->perf_pos >= 0 && sn->ci.sounding)
            pk = cu_key_of_note(sn->perf_note);
        if (pk >= 0 && !blink) {                         /* the performed note blinks */
            uint8_t q = cu_led_pos[14u + (uint32_t)pk];
            if (q != 0xFF)
                nl[q >> 3] &= (uint8_t)~(1u << (q & 7u));
        }
        for (k = CU_ROOT0; k < CU_NKEY; k++) {           /* Key Mode: the black roots off the scale dark */
            uint32_t pc = (uint32_t)(53 + (int32_t)k + 12 * cu.octave + 120) % 12u;
            if (sn->scale_mask && cu_black(k) && !((sn->scale_mask >> pc) & 1u))
                continue;
            cu_led(nd, 14u + k, 1);
        }
        for (k = CU_ROOT0; k < CU_NKEY; k++) {           /* PLAN 6: the loop's notes glow dim (not the player's) */
            int32_t n = 53 + (int32_t)k + 12 * cu.octave;
            uint8_t q = cu_led_pos[14u + k];
            if (n >= 0 && n < 128 && ((sn->lnote[n >> 3] >> (n & 7)) & 1u) && q != 0xFF &&
                !((nl[q >> 3] >> (q & 7u)) & 1u))
                cu_led(ld, 14u + k, 1);
        }
    }
    for (k = 0; k < FM1_NCOL; k++) {
        if (cs.leds) {                                   /* STOCK: the idle ones lit, the active ones dark */
            nl[k] = (uint8_t)(nd[k] & ~nl[k]);
            nd[k] = 0;
        }
        nl[k] |= own[k];
        nd[k] |= ld[k];
        nl[k] &= (uint8_t)~ld[k];
    }
    for (k = 0; k < FM1_NCOL; k++)
        fm1_led_dim[k] = nd[k];
    for (k = 0; k < FM1_NCOL; k++)
        fm1_led[k] = nl[k];
}

/* ------------------------------------------------------- the view-model --- */
static const uint8_t CU_TRIAD[CR_Q_COUNT][3] = {     /* intervals of each quality's base (the rest: extensions) */
    {0, 0, 0}, {0, 3, 6}, {0, 3, 7}, {0, 4, 7}, {0, 5, 7}, {0, 4, 8}, {0, 7, 7}, {0, 3, 5}};

static void cu_name(cr_name_t *n, const cr_chord_info_t *ci)
{
    cu_cpy(n->root, ci->root, sizeof n->root);
    cu_cpy(n->quality, ci->qual, sizeof n->quality);
    cu_cpy(n->sup, ci->sup, sizeof n->sup);
    n->col_root = CR_COL_WHITE;
    n->col_quality = CR_COL_WHITE;
    n->col_sup = ci->secret ? CR_COL_RED : CR_COL_ORANGE;
}
static void cu_notes(cr_screen_t *s, const cr_chord_info_t *ci)
{
    uint32_t i, j;
    s->n_notes = 0;
    for (i = 0; i < ci->nnotes && i < CR_NOTES_MAX; i++) {
        char b[5];
        uint32_t iv = (uint32_t)(ci->notes[i] + 120u - ci->root_pc) % 12u, base = ci->quality == CR_Q_NONE;
        for (j = 0; j < 3u && !base; j++)
            base = iv == CU_TRIAD[ci->quality % CR_Q_COUNT][j];
        cr_note_name(ci->notes[i], b);
        cu_cpy(s->note[i].t, b, sizeof s->note[i].t);
        s->note[i].col = base ? CR_COL_WHITE : ci->secret ? CR_COL_RED : CR_COL_ORANGE;
        s->note[i].mark = (uint8_t)!base;
        s->n_notes++;
    }
}
static uint32_t cu_lit(const cr_chord_info_t *ci)  /* the keyboard strip: bit k = a voiced note at key k */
{
    uint32_t i, m = 0;
    for (i = 0; i < ci->nnotes; i++) {
        int32_t k = (int32_t)ci->notes[i] - 53 - 12 * cu.octave;
        while (k >= (int32_t)CU_NKEY)
            k -= 12;
        while (k < 0)
            k += 12;
        m |= 1u << k;
    }
    return m;
}

/* the battery as Felucca's header showed it (ui_draw.c batt_shown): 4 on USB power (charging), else 0..3 by the
 * smoothed ADC (main.c batt_raw, thresholds 531 / 561 / 591); shown on the Options page's top line only */
static uint32_t cu_batt(void)
{
    if (usb.config && !usb.suspended)
        return 4u;
    return song.batt_raw >= 591 ? 3u : song.batt_raw >= 561 ? 2u : song.batt_raw >= 531 ? 1u : 0u;
}

static void cu_header(cr_screen_t *s)
{
    const cr_snap_t *sn = &cr_snap;
    s->header = 1;
    s->icon = CR_ICON_NONE;
    s->batt = 255;                     /* the battery shows on the Options page only (the user's call) */
    s->mid_col = CR_COL_WHITE;
    s->right_col = CR_COL_WHITE;
    if (cs.key_on) {
        cu_cpy(s->mid, "Key: ", sizeof s->mid);
        cu_cat(s->mid, CU_NOTE[cs.tonic], sizeof s->mid);
        if (cs.scale == CR_SCALE_MINOR)
            cu_cat(s->mid, " minor", sizeof s->mid);
        s->mid_col = CR_COL_YELLOW;
    } else if (cr_safe) {                         /* SAFE MODE: on the top line all session (Options > Safe Mode) */
        cu_cpy(s->mid, "Safe mode", sizeof s->mid);
        s->mid_col = CR_COL_RED;
    }
    /* the status (top right), one of: "Bass Solo" (Bass Behaviour Solo with the bass on: the chord part is silent),
     * the perform mode ("Arp"), "Bass" (the bass on), "Oct +1", "Latch", "lock" */
    if (sn->bass_on && cs.bass_mode == CR_BASS_SOLO) {
        cu_cpy(s->right, "Bass Solo", sizeof s->right);
        s->right_col = CR_COL_ORANGE;
    } else if (sn->perform_on)
        cu_cpy(s->right, CU_PERF[cs.perf_sel].short_name, sizeof s->right);
    else if (sn->bass_on) {
        cu_cpy(s->right, "Bass", sizeof s->right);
        s->right_col = CR_COL_ORANGE;
    } else if (cu.octave) {
        cu_cpy(s->right, "Oct ", sizeof s->right);
        cu_int(s->right + 4, cu.octave, 1, sizeof s->right - 4u);
    } else if (cs.sticky)
        cu_cpy(s->right, "Latch", sizeof s->right);
    else if (cu.clock)                            /* LOCK on (mock-up 3): a small white "lock" */
        cu_cpy(s->right, "lock", sizeof s->right);
    /* the looper owns the top line while it runs: "Rec 2.3", "Rec" (the count-in), "Loop 1" */
    if (sn->lcap != CRL_CAP_NONE || sn->lstate == CRL_PLAYING) {
        char b[8];
        s->mid_col = s->right_col = CR_COL_RED;
        s->right[0] = 0;
        if (sn->lcap == CRL_CAP_COUNTIN) {
            cu_cpy(s->mid, "Rec", sizeof s->mid);   /* (the beats to go: the panel, cr_build_screen) */
        } else {                                   /* "Loop 3" and the state: "Rec \267 Overwrite" (armed: the mode),
                                                    * "Rec 2.4", "Dub 3.2", "Rep 3.2", "Rec \267 Step" */
            cu_cpy(s->mid, "Loop ", sizeof s->mid);
            cu_int(b, (int32_t)cu_lm_slot() + 1, 0, sizeof b);
            cu_cat(s->mid, b, sizeof s->mid);
            if (sn->lcap == CRL_CAP_ARMED || sn->lcap == CRL_CAP_STEP) {
                cu_cpy(s->right, "Rec \267 ", sizeof s->right);
                cu_cat(s->right, sn->lcap == CRL_CAP_STEP ? "Step" : CU_LOOP_MODE[cs.loop_mode % CRL_NMODE],
                       sizeof s->right);
            } else if (sn->lcap == CRL_CAP_REC)
                cu_cpy(s->right, "Rec ", sizeof s->right);
            else if (sn->lcap == CRL_CAP_OD || sn->lcap == CRL_CAP_OD_ARMED)   /* (armed too: "Dub 3.2") */
                cu_cpy(s->right, sn->lrep ? "Rep " : "Dub ", sizeof s->right);
        }
        if (sn->lcap == CRL_CAP_REC || sn->lcap == CRL_CAP_OD || sn->lcap == CRL_CAP_OD_ARMED) {
            cu_int(b, (int32_t)sn->lbar, 0, sizeof b);
            cu_cat(s->right, b, sizeof s->right);
            cu_cat(s->right, ".", sizeof s->right);
            cu_int(b, (int32_t)sn->lbeat, 0, sizeof b);
            cu_cat(s->right, b, sizeof s->right);
        }
    }
}
/* the corner dial's state (cr_screen.h CR_DIAL_*), -1 none: the loop playing (PLAY), REC armed with no loop yet or an
 * overdub armed over the playing loop (ARMED), the first take recording (REC), an overdub recording (OD); none
 * during the count-in (its own screen, with the ring) */
static int cu_dial_state(void)
{
    const cr_snap_t *sn = &cr_snap;
    switch (sn->lcap) {
    case CRL_CAP_NONE: return sn->lstate == CRL_PLAYING ? CR_DIAL_PLAY : -1;
    case CRL_CAP_ARMED:
    case CRL_CAP_STEP:                             /* (step entry: the REC dot steady) */
    case CRL_CAP_OD_ARMED: return CR_DIAL_ARMED;
    case CRL_CAP_REC: return CR_DIAL_REC;
    case CRL_CAP_OD: return CR_DIAL_OD;
    default: return -1;
    }
}
/* the loop's ring round the edge, red: only where the loop is the whole screen (the count-in, the undo screen) */
static void cu_ring_on(cr_screen_t *s)
{
    const cr_snap_t *sn = &cr_snap;
    if (sn->lcap == CRL_CAP_NONE && sn->lstate != CRL_PLAYING)
        return;
    s->ring_on = 1;
    s->ring_col = CR_COL_RED;
    s->ring = sn->lring;
    s->ring_rec = sn->lcap == CRL_CAP_COUNTIN || sn->lcap == CRL_CAP_REC || sn->lcap == CRL_CAP_OD;
}
/* the downbeat (the bar's first beat begins): its time, for the dial's pulse; tracked every frame while the dial
 * shows a progress (the loop playing, an overdub armed or recording, the first take recording) */
static struct { uint32_t t0; uint8_t beat, bar, on; } cu_db;
static void cu_downbeat(uint32_t now)
{
    uint8_t beat = (uint8_t)cr_snap.lbeat, bar = (uint8_t)cr_snap.lbar;
    if (cu_dial_state() < 0 || (cr_snap.lstate != CRL_PLAYING && cr_snap.lcap != CRL_CAP_REC)) {
        cu_db.on = 0;
        return;
    }
    if (beat == 1u && (!cu_db.on || cu_db.beat != 1u || cu_db.bar != bar))
        cu_db.t0 = now;
    cu_db.on = 1;
    cu_db.beat = beat;
    cu_db.bar = bar;
}
/* the corner dial (every loop state but the count-in): in the top line, where no ring is drawn and a header shows.
 * The arc is cr_snap.lring: the loop's cycle (playing, an overdub armed or recording); recording the first take, the
 * take's progress with a loop length set (Sync: rec_len), else (Free) the bar's, sweeping once a bar. The REC dot is
 * the REC LED's twin (cr_leds): lit while armed, blinking in its phase while recording or overdubbing */
static void cu_dial(cr_screen_t *s, uint32_t now)
{
    int st = cu_dial_state();
    if (st < 0 || s->ring_on || !s->header)
        return;
    s->dial_on = 1;
    s->dial_mode = (uint8_t)st;
    s->dial = cr_snap.lring;
    s->dial_pulse = (uint8_t)(cr_motion != CR_MOTION_OFF && cu_db.on && now - cu_db.t0 < 100u);
    s->dial_dot = (uint8_t)(st == CR_DIAL_ARMED || ((st == CR_DIAL_REC || st == CR_DIAL_OD) && cu_blink(now)));
}

static void cu_meter(cr_screen_t *s, const char *value, const char *sub, const char *label, uint32_t col, uint32_t pct,
                     uint32_t segs)
{
    s->kind = CR_K_METER;
    cu_cpy(s->value, value, sizeof s->value);
    cu_cpy(s->sub, sub, sizeof s->sub);
    cu_cpy(s->label, label, sizeof s->label);
    s->col = (uint8_t)col;
    s->pct = (uint16_t)(pct > 256u ? 256u : pct);
    s->segments = (uint8_t)segs;
    s->thick = 14;
}

static void cu_picker(cr_screen_t *s, const char *const *items, uint32_t n, uint32_t sel, uint32_t col,
                      const char *label, const char *footer)
{
    uint32_t i, i0 = sel > 3u ? sel - 3u : 0u;
    if (n > CR_PICK_MAX && i0 > n - CR_PICK_MAX)
        i0 = n - CR_PICK_MAX;
    if (n <= CR_PICK_MAX)
        i0 = 0;
    s->kind = CR_K_PICKER;
    s->n_items = (uint8_t)n;
    s->item0 = (uint8_t)i0;
    s->sel = (uint8_t)(sel < n ? sel : 0u);
    for (i = 0; i < CR_PICK_MAX && i0 + i < n; i++)
        cu_cpy(s->item[i], items[i0 + i], sizeof s->item[i]);
    s->col = (uint8_t)col;
    s->title_col = CR_COL_MID;
    cu_cpy(s->label, label, sizeof s->label);
    cu_cpy(s->footer, footer, sizeof s->footer);
}

/* the power-on splash is the idle stripes' first CR_SPLASH_MS: the bands slide in (CR_A_INTRO, unless main.c's
 * splash, cr_shim.c cr_splash, already played it) and the version sits under them */
#ifndef FELUCCA_VERSION
#define FELUCCA_VERSION "ChoralRoot 0.1"
#endif
#define CR_SPLASH_MS 1500u
static uint32_t cu_boot_ms;
static uint8_t cu_intro_played;

/* SAFE MODE (main.c, cr_bootguard.h: two boots in a row crashed within 30 s): main.c's splash (cr_shim.c cr_splash)
 * and the UI's first CR_SAFE_MS (or until a button or key is held) show this; the top line says "Safe mode" all
 * session, Options opens on Safe Mode and Flash Data. prev_stage: the breadcrumb the last crash left */
#define CR_SAFE_MS 5000u
static void cu_safe_screen(cr_screen_t *s)
{
    char b[8];
    s->kind = CR_K_BIG;
    cu_cpy(s->value, "SAFE MODE", sizeof s->value);
    s->size = 52;
    s->block = CR_COL_YELLOW;
    s->col = CR_COL_WHITE;
    cu_cpy(s->sub, "stage ", sizeof s->sub);
    cu_int(b, (int32_t)felucca_dbg.prev_stage, 0, sizeof b);
    cu_cat(s->sub, b, sizeof s->sub);
    cu_cpy(s->label, "flash data skipped", sizeof s->label);
    cu_cpy(s->footer, "crashed at: ", sizeof s->footer);
    cu_cat(s->footer, bootguard_stage_name(felucca_dbg.prev_stage), sizeof s->footer);
    cu_cat(s->footer, " \267 Options: erase", sizeof s->footer);
}

/* Options > Flash Data (SAFE MODE, OCT+ twice): erase every data object ChoralRoot keeps (not the firmware, not the
 * update area, not the free sample sectors 0xB4000..0xC7FFF) and reboot; the boot after it loads nothing (an
 * erased flash: factory sounds, default settings, calibration from RAM or the default). The regions: the stores and
 * FM6 bank copy A 0x97000..0x9FFFF (storage.c st_sector: VA store, FM6 stores, the project slots, 0x9F000), the loop
 * slots 0xC8000..0xDBFFF (CRL_FL_BASE), the user sound banks 0xDC000..0xDFFFF, the settings A / B and FM6 bank
 * copy B 0xFC000..0xFEFFF; FELUCCA_CZ: the CZ-1 tone store's second half and the eight CZ-1 banks 0xA0000..0xB1FFF;
 * FELUCCA_QUAD: the QUAD patch store 0xB2000..0xB3FFF */
#ifndef CR_REBOOT
#define CR_REBOOT() do { bootguard_settled(&bootguard); usb_detach(); fm1_delay_ms(30); fm1_reboot(); } while (0)
#endif
static void cu_flash_erase(void)
{
    static const uint32_t R[][2] = {{0x97000u, 9u}, {CRL_FL_BASE, 2u * CRL_SLOTS}, {0xDC000u, 4u}, {0xFC000u, 3u},
#if FELUCCA_CZ
                                    {0xA0000u, 18u},   /* (storage.c: OBJ_CZSTORE1, OBJ_CZBANK0..+7) */
#endif
#if FELUCCA_QUAD
                                    {0xB2000u, 2u},    /* (storage.c: OBJ_QUADSTORE) */
#endif
    };
    static cr_screen_t es;                         /* (static: off the stack) */
    uint32_t r, k, n = 0, total = 0, bad = 0;
    for (r = 0; r < NELEM(R); r++)
        total += R[r][1];
    for (r = 0; r < NELEM(R); r++)
        for (k = 0; k < R[r][1]; k++) {
            if (!(n % 4u)) {                       /* the progress, every 4 sectors (~180 ms) */
                cr_screen_clear(&es);
                es.kind = CR_K_BIG;
                cu_cpy(es.value, "ERASING", sizeof es.value);
                es.size = 52;
                es.block = CR_COL_RED;
                cu_int(es.sub, (int32_t)(n * 100u / total), 0, sizeof es.sub);
                cu_cat(es.sub, " %", sizeof es.sub);
                cu_cpy(es.label, "flash data: do not switch off", sizeof es.label);
                cr_draw_invalidate();
                cr_draw(&es, CR_ANIM_SETTLED);
            }
            fm1_wdt_feed();
#if FELUCCA_FLASH
            if (flash_ok && st_erase(R[r][0] + k * ST_SECTOR))
                bad++;
#endif
            n++;
        }
    cu_trace("safe: flash data erased, %u sectors, %u failed: reboot\n", (unsigned)n, (unsigned)bad);
    (void)bad;
    CR_REBOOT();
}

static void cu_stripes(cr_screen_t *s)
{
    s->kind = CR_K_STRIPES;
    s->bands[0] = CR_COL_RED;
    s->bands[1] = CR_COL_ORANGE;
    s->bands[2] = CR_COL_WHITE;
    s->n_bands = 3;
    s->band = 18;
    s->gap = 8;
    cu_cpy(s->title, "choralroot", sizeof s->title);
    s->title_px = 34;
    s->title_col = CR_COL_WHITE;
    s->period_ms = cr_anim_bar_ms(cs.bpm);
    if (cu_now() - cu_boot_ms < CR_SPLASH_MS) {
        cu_cpy(s->foot, FELUCCA_VERSION, sizeof s->foot);
        if (!cu_intro_played)
            s->anim = CR_A_INTRO;
    }
}

/* the trace (CR_TRACE): a knob row's labels and values when they change ("perf: cells Division=1/8 ...") */
static void cu_row_trace(const cr_screen_t *s, const char *who, char *seen, uint32_t n)
{
    char lb[96];
    uint32_t k;
    lb[0] = 0;
    for (k = 0; k < 4u; k++) {
        cu_cat(lb, k ? " " : "", sizeof lb);
        cu_cat(lb, s->cell[0][k].flags ? s->cell[0][k].label : "-", sizeof lb);
    }
    if (!cu_eq(lb, seen)) {
        cu_cpy(seen, lb, n);
        cu_trace("%s: cells %s\n", who, lb);
    }
    (void)who;
}

/* PERF's knob row: the mode's four parameters as KNOB 1..4 carry them (CU_PERF_KNOB; '-' for none), the labels the
 * popups' names capitalised, the glyph by the parameter's kind (pct / pct2 Q8 of 255):
 *   parameter          glyph    pct                             value
 *   rate (ms)          ECHOES   (v - 1) / 999, pct2 1.0         "120 ms"
 *   division           ECHOES   index / 11, pct2 1.0            "1/8"
 *   direction          ARROW    up 0, down 96, up-down and      "Up" "Down" "Up-down" "Down-up" "Played" "Random"
 *                               down-up 160, as played 0,
 *                               random 255 (the arrow's bands)
 *   range (octaves)    RANGE    (v - 1) / 3                     "2 oct"
 *   gate (%)           GATE     v / 200                         "70%"
 *   amount (slop %)    BAR      v / 100                         "40%"
 *   pattern            BAR      (v - 1) / (patterns - 1)        "03"
 *   swing (%)          text                                     "55%"
 *   hold, retrigger    text                                     "On" / "Off"
 *   rotate             text                                     "+3"                                               */
static void cu_perf_cells(cr_screen_t *s)
{
    static const uint8_t ARROW[6] = {0, 96, 160, 160, 0, 255};
    static const char *const DIR[6] = {"Up", "Down", "Up-down", "Down-up", "Played", "Random"};
    uint32_t m = cu_perf_mode(), k;
    for (k = 0; k < 4u; k++) {
        cr_cell_t *c = &s->cell[0][k];
        int32_t p = CU_PERF_KNOB[m][k], v;
        if (p < 0)
            continue;
        v = cs.par[m][p];
        c->flags = CR_CF_ON;
        cu_cpy(c->label, CU_PAR[p].name, sizeof c->label);
        c->label[0] = (char)(c->label[0] - 'a' + 'A');
        switch (p) {
        case CR_P_RATE:
            cu_int(c->value, v, 0, sizeof c->value);
            cu_cat(c->value, " ms", sizeof c->value);
            c->glyph = CR_G_ECHOES;
            c->pct = (uint8_t)((uint32_t)(v - 1) * 255u / 999u);
            c->pct2 = 255;
            break;
        case CR_P_DIV:
            cu_cpy(c->value, CU_DIV[v % CR_DIV_COUNT], sizeof c->value);
            c->glyph = CR_G_ECHOES;
            c->pct = (uint8_t)((uint32_t)v * 255u / (CR_DIV_COUNT - 1u));
            c->pct2 = 255;
            break;
        case CR_P_DIR:
            cu_cpy(c->value, DIR[v % 6], sizeof c->value);
            c->glyph = CR_G_ARROW;
            c->pct = ARROW[v % 6];
            break;
        case CR_P_RANGE:
            cu_int(c->value, v, 0, sizeof c->value);
            cu_cat(c->value, " oct", sizeof c->value);
            c->glyph = CR_G_RANGE;
            c->pct = (uint8_t)((uint32_t)(v - 1) * 255u / 3u);
            break;
        case CR_P_GATE: case CR_P_AMOUNT: case CR_P_SWING:
            cu_int(c->value, v, 0, sizeof c->value);
            cu_cat(c->value, "%", sizeof c->value);
            if (p == CR_P_GATE) {
                c->glyph = CR_G_GATE;
                c->pct = (uint8_t)((uint32_t)v * 255u / 200u);
            } else if (p == CR_P_AMOUNT) {
                c->glyph = CR_G_BAR;
                c->pct = (uint8_t)((uint32_t)v * 255u / 100u);
            }
            break;
        case CR_P_PATTERN:
            cu_2d(c->value, (uint32_t)v, sizeof c->value);
            c->glyph = CR_G_BAR;
            c->pct = (uint8_t)(CR_NPATTERN > 1 ? (uint32_t)(v - 1) * 255u / (CR_NPATTERN - 1u) : 0u);
            break;
        case CR_P_HOLD: case CR_P_RETRIG:
            cu_cpy(c->value, v ? "On" : "Off", sizeof c->value);
            break;
        default:
            cu_int(c->value, v, p == CR_P_ROTATE, sizeof c->value);
            break;
        }
    }
    cu_hot_row(s, L_PERF);
}

/* BASS's knob row: KNOB 1 Behaviour (text), 2 Register (shift: -2..4 octaves), 3 Sound (the popup's number and name,
 * "off" while BASS is off; text), 4 Level (bar; the real level even with BASS off: the top line says so) */
static void cu_bass_cells(cr_screen_t *s)
{
    static const char *const SHORT[4] = {"Chords", "Unison", "Single", "Solo"};
    static const char *const LB[4] = {"Behaviour", "Register", "Sound", "Level"};
    int32_t bv = cr_snap.bass_voicing, lv = trk[CR_PART_BASS].p[P_LEVEL];
    uint32_t k;
    for (k = 0; k < 4u; k++) {
        s->cell[0][k].flags = CR_CF_ON;
        cu_cpy(s->cell[0][k].label, LB[k], sizeof s->cell[0][k].label);
    }
    cu_cpy(s->cell[0][0].value, SHORT[cs.bass_mode & 3u], sizeof s->cell[0][0].value);
    cu_int(s->cell[0][1].value, bv, bv != 0, sizeof s->cell[0][1].value);
    s->cell[0][1].glyph = CR_G_SHIFT;
    s->cell[0][1].pct = (uint8_t)((uint32_t)(bv + 2 < 0 ? 0 : bv + 2) * 255u / 6u);
    if (!cs.bass_sound || !cs.bass_on) {
        cu_cpy(s->cell[0][2].value, "off", sizeof s->cell[0][2].value);
    } else {
        uint32_t pos = cs.bass_sound - 1u;
        char nm[13];
        cu_2d(s->cell[0][2].value, pos, sizeof s->cell[0][2].value);
        pool_name(trk[CR_PART_BASS].eng_req % NENGINES, pos, nm);
        cu_cat(s->cell[0][2].value, " ", sizeof s->cell[0][2].value);
        cu_cat(s->cell[0][2].value, nm, sizeof s->cell[0][2].value);
        {   /* a name cut short ends at a word: "01 SUB" for SUB BASS, not "01 SUB B" */
            char *v = s->cell[0][2].value;
            uint32_t i = 0, sp = 0, nl = 0;
            while (nm[nl]) nl++;
            while (v[i]) { if (v[i] == ' ') sp = i; i++; }
            if (i < 3u + nl && sp > 2u)
                v[sp] = 0;
        }
    }
    cu_int(s->cell[0][3].value, lv, 0, sizeof s->cell[0][3].value);
    s->cell[0][3].glyph = CR_G_BAR;
    s->cell[0][3].pct = (uint8_t)((uint32_t)lv * 255u / 127u);
    cu_hot_row(s, L_BASS);
}

/* KEY's knob row (layers sheet 1): Tonic (text), Scale (text), Transpose (shift: -24..24, centre 0), Single (text) */
static void cu_key_cells(cr_screen_t *s)
{
    static const char *const LB[4] = {"Tonic", "Scale", "Transpose", "Single"};
    uint32_t k;
    for (k = 0; k < 4u; k++) {
        s->cell[0][k].flags = CR_CF_ON;
        cu_cpy(s->cell[0][k].label, LB[k], sizeof s->cell[0][k].label);
    }
    cu_cpy(s->cell[0][0].value, cs.key_on ? CU_NOTE[cs.tonic % 12u] : "-", sizeof s->cell[0][0].value);
    cu_cpy(s->cell[0][1].value, cs.scale ? "Minor" : "Major", sizeof s->cell[0][1].value);
    cu_int(s->cell[0][2].value, cs.transpose, 1, sizeof s->cell[0][2].value);
    s->cell[0][2].glyph = CR_G_SHIFT;
    s->cell[0][2].pct = (uint8_t)((uint32_t)(cs.transpose + 24) * 255u / 48u);
    cu_cpy(s->cell[0][3].value, cs.single ? "Split" : "Full", sizeof s->cell[0][3].value);
    cu_hot_row(s, L_KEY);
}
/* the PLAY menu's knob row (docs/LOOPER-MODES.md): Quantize (echoes), Count-in (gate: full on, narrow off), Level
 * (bar), Keys (text: Play / Loops). The length is the menu's middle (SELECT) */
static void cu_loop_cells(cr_screen_t *s)
{
    static const char *const LB[4] = {"Quantize", "Count-in", "Level", "Keys"};
    uint32_t k, q = cs.loop_quant % CRL_NQUANT;
    for (k = 0; k < 4u; k++) {
        s->cell[0][k].flags = CR_CF_ON;
        cu_cpy(s->cell[0][k].label, LB[k], sizeof s->cell[0][k].label);
    }
    cu_cpy(s->cell[0][0].value, CU_QUANT[q], sizeof s->cell[0][0].value);
    s->cell[0][0].glyph = CR_G_ECHOES;              /* "none": no grid (pct 0, pct2 0: the baseline alone, cr_draw.c);
                                                     * 1/4 .. 1/32 the repeats, the spacing growing as before */
    s->cell[0][0].pct = (uint8_t)(q * 255u / (CRL_NQUANT - 1u));
    s->cell[0][0].pct2 = q ? 255u : 0u;
    cu_cpy(s->cell[0][1].value, cs.loop_count_in ? "On" : "Off", sizeof s->cell[0][1].value);
    s->cell[0][1].glyph = CR_G_GATE;
    s->cell[0][1].pct = cs.loop_count_in ? 255 : 26;
    cu_int(s->cell[0][2].value, cs.loop_level, 0, sizeof s->cell[0][2].value);
    s->cell[0][2].glyph = CR_G_BAR;
    s->cell[0][2].pct = (uint8_t)(cs.loop_level * 255u / 100u);
    cu_cpy(s->cell[0][3].value, cs.loop_keys ? "Loops" : "Play", sizeof s->cell[0][3].value);
    cu_hot_row(s, L_LOOP);
}

/* the PLAY menu's timeline / step grid marks (cr_screen.h lm_lane): read from the ISR's loop (IRQ off, <= 512 events)
 * only when its events (crl.gen) or the window changed */
static struct {
    uint32_t key, start;
    uint16_t gen;
    uint8_t ok, nlane, newlane, cur_ok, cur_root, cur_qx;
    uint64_t lane[4];
} cu_lmc;
static void cu_lm_marks(uint32_t start, uint32_t span, uint32_t mid)   /* window [start, start + span) ticks */
{
    const cr_snap_t *sn = &cr_snap;
    uint32_t key = start * 2654435761u ^ span * 40503u ^ mid ^ (uint32_t)sn->lcap << 24 ^ sn->lnlayers << 16, i, nl, tot,
             off, rec = sn->lcap == CRL_CAP_REC || sn->lcap == CRL_CAP_OD;
    if (cu_lmc.ok && cu_lmc.gen == sn->lgen && cu_lmc.key == key)
        return;
    for (i = 0; i < 4u; i++)
        cu_lmc.lane[i] = 0;
    cu_lmc.cur_ok = 0;
    nl = sn->lnlayers;
    tot = nl + (sn->lcap == CRL_CAP_OD || sn->lcap == CRL_CAP_REC ? 1u : 0u);   /* (the first take: nlayers 0) */
    off = tot > 4u ? tot - 4u : 0u;                /* more than 4 lanes: the older layers fold into lane 0 */
    fm1_irq_off();
    if (mid == CR_LM_STEPS) {
        uint32_t g = crl.step_g ? crl.step_g : 24u, cur = (uint32_t)crl.step * g;
        for (i = crl.layer_ev0; i < crl.d.nev; i++) {
            const crl_ev_t *e = &crl.d.ev[i];
            uint32_t st = e->t / g;
            if (st >= start && st < start + span)
                cu_lmc.lane[0] |= (uint64_t)1 << (st - start);
            if (e->t == cur) {                     /* (the last one written there names the cursor's step) */
                cu_lmc.cur_ok = 1;
                cu_lmc.cur_root = e->root;
                cu_lmc.cur_qx = e->qx;
            }
        }
    } else {
        for (i = 0; i < crl.d.nev; i++) {
            const crl_ev_t *e = &crl.d.ev[i];
            uint32_t l = CRL_LAYER(e), ln;
            if (l == 0xFFu || e->t < start || e->t >= start + span)
                continue;
            ln = l > off ? l - off : 0u;
            cu_lmc.lane[ln & 3u] |= (uint64_t)1 << ((e->t - start) * 64u / span);
        }
        if (rec)                                   /* the take's gestures still held: their marks where they began */
            for (i = 0; i < CRL_OPEN; i++) {
                uint32_t t0 = crl.open[i].t0, t;
                if (!crl.open[i].used)
                    continue;
                if (crl.cap == CRL_CAP_REC)
                    t = (int32_t)(t0 - crl.rec0) < 0 ? 0u : t0 - crl.rec0;
                else if ((int32_t)(t0 - crl.od0) < 0 || !crl.d.len)
                    continue;
                else
                    t = (t0 - crl.cyc0) % crl.d.len;
                if (t >= start && t < start + span)
                    cu_lmc.lane[(nl > off ? nl - off : 0u) & 3u] |= (uint64_t)1 << ((t - start) * 64u / span);
            }
    }
    fm1_irq_on();
    cu_lmc.nlane = (uint8_t)(tot > 4u ? 4u : tot);
    cu_lmc.newlane = (uint8_t)(rec && tot ? (nl > off ? nl - off : 0u) + 1u : 0u);
    cu_lmc.gen = sn->lgen;
    cu_lmc.key = key;
    cu_lmc.ok = 1;
}
/* the PLAY menu (LOOP held: docs/LOOPER-MODES.md, design/choralroot-fm1-looper-screens.png): the slot strip, the record
 * mode, the middle (the length; the record timeline while recording or playing; the step grid in step entry), the
 * knob row; the top line "Loop 3" and the state at its right */
static void cu_loopmenu(cr_screen_t *s, uint32_t now)
{
    static char seen[48];
    const cr_snap_t *sn = &cr_snap;
    uint32_t sig = sn->lsig % CRL_NSIG, bar = sig == CRL_SIG_44 ? 384u : 288u;
    uint32_t beats = sig == CRL_SIG_44 ? 4u : sig == CRL_SIG_34 ? 3u : 6u, cap = sn->lcap, slot = cu_lm_slot();
    uint32_t cur = 1u << cs.loop_slot;
    char b[12];
    cu_picker(s, CU_LOOP_MODE, CRL_NMODE, cs.loop_mode % CRL_NMODE, CR_COL_WHITE, "", "");
    s->kind = CR_K_LOOPMENU;
    s->orient = 1;
    cu_loop_cells(s);
    cu_row_trace(s, "loop", seen, sizeof seen);
    s->lm_used = (uint16_t)((cs.loop_used & ~cur) | (sn->lstate != CRL_EMPTY ? cur : 0u));   /* (the slot in RAM: what
                                                                                              * it holds now) */
    s->lm_slot = (uint8_t)slot;
    s->lm_jump = (uint8_t)((int32_t)(cu_lp.adv_until - now) > 0 ? cu_lp.adv_to : 0u);
    cu_cpy(s->mid, "Loop ", sizeof s->mid);
    cu_int(b, (int32_t)slot + 1, 0, sizeof b);
    cu_cat(s->mid, b, sizeof s->mid);
    s->mid_col = CR_COL_RED;
    if (cap == CRL_CAP_NONE && sn->lstate == CRL_PLAYING) {   /* playing: bar.beat at the right */
        cu_int(s->right, (int32_t)sn->lbar, 0, sizeof s->right);
        cu_cat(s->right, ".", sizeof s->right);
        cu_int(b, (int32_t)sn->lbeat, 0, sizeof b);
        cu_cat(s->right, b, sizeof s->right);
        s->right_col = CR_COL_WHITE;
    } else if (cap == CRL_CAP_NONE && !((s->lm_used >> slot) & 1u)) {
        cu_cpy(s->right, "empty", sizeof s->right);
        s->right_col = CR_COL_GREY;
    }
    if (cap == CRL_CAP_STEP) {                     /* the step grid: 16 a bar at 1/16 (the Quantize grid), 4 bars a page */
        uint32_t g = sn->lstep_g ? sn->lstep_g : 24u, per = bar / g, pb = 64u / per, nst, tb, first, shown;
        char r[4], q[5], sup[CR_SUP_MAX];
        pb = pb > 4u ? 4u : pb ? pb : 1u;
        nst = sn->lstep_len ? sn->lstep_len / g : (sn->lstep_end > sn->lstep + 1u ? sn->lstep_end : sn->lstep + 1u);
        tb = (nst + per - 1u) / per;
        first = sn->lstep / (per * pb) * per * pb;
        shown = tb - first / per;
        shown = shown < 1u ? 1u : shown > pb ? pb : shown;
        cu_lm_marks(first, per * shown, CR_LM_STEPS);
        s->lm_mid = CR_LM_STEPS;
        s->lm_bars = (uint8_t)shown;
        s->lm_beats = (uint8_t)per;
        s->lm_lane[0] = cu_lmc.lane[0];
        s->lm_pos = (uint16_t)((sn->lstep - first) | (cu_blink(now) ? 0u : CR_LM_CUR_OFF));
        if (sn->ci.sounding && !sn->ldisp) {       /* the chord being entered, else the one at the cursor */
            cu_cpy(s->sub, sn->ci.root, sizeof s->sub);
            cu_cat(s->sub, sn->ci.qual, sizeof s->sub);
            cu_cat(s->sub, sn->ci.sup, sizeof s->sub);
        } else if (cu_lmc.cur_ok) {
            cr_chord_name((uint8_t)(cu_lmc.cur_root % 12u), (uint8_t)(cu_lmc.cur_qx >> 4), (uint8_t)(cu_lmc.cur_qx & 15u),
                          r, q, sup);
            cu_cpy(s->sub, r, sizeof s->sub);
            cu_cat(s->sub, q, sizeof s->sub);
            cu_cat(s->sub, sup, sizeof s->sub);
        } else
            cu_cpy(s->sub, "-", sizeof s->sub);
        cu_cpy(s->value, "step ", sizeof s->value);
        cu_int(b, (int32_t)(sn->lstep % per) + 1, 0, sizeof b);
        cu_cat(s->value, b, sizeof s->value);
        cu_cat(s->value, " \267 bar ", sizeof s->value);
        cu_int(b, (int32_t)(sn->lstep / per) + 1, 0, sizeof b);
        cu_cat(s->value, b, sizeof s->value);
        cu_cpy(s->foot, "OCT-: back \267 OCT+: rest \267 REC: done", sizeof s->foot);
    } else if (cap == CRL_CAP_REC || cap == CRL_CAP_OD || cap == CRL_CAP_OD_ARMED || sn->lstate == CRL_PLAYING) {
        uint32_t pos = sn->lpos, start = 0, shown, len;
        s->lm_mid = CR_LM_TIMELINE;
        s->lm_beats = (uint8_t)beats;
        if (cap == CRL_CAP_REC && !sn->lrec_len) { /* Free, the first take: the bars as they pass, the current one right */
            uint32_t nb = pos / bar + 1u, ms = pos * 625u / ((cs.bpm ? cs.bpm : 120u) * 1u);   /* (60000 / 96 = 625) */
            start = (nb > 4u ? nb - 4u : 0u) * bar;
            shown = nb > 4u ? 4u : nb;
            s->lm_free = (uint8_t)(nb > 255u ? 255u : nb);
            ms /= 1000u;                           /* "0:07" */
            cu_int(s->sub, (int32_t)(ms / 60u), 0, sizeof s->sub);
            cu_cat(s->sub, ":", sizeof s->sub);
            cu_2d(b, ms % 60u, sizeof b);
            cu_cat(s->sub, b, sizeof s->sub);
        } else {                                   /* N bars: all of them (more than 8: the half the playhead is in) */
            len = cap == CRL_CAP_REC ? sn->lrec_len : sn->llen;
            shown = (len + bar - 1u) / bar;
            shown = shown ? shown : 1u;
            if (shown > 8u) {
                start = pos / (8u * bar) * 8u * bar;
                shown = shown - start / bar > 8u ? 8u : shown - start / bar;
            }
        }
        s->lm_bars = (uint8_t)shown;
        cu_lm_marks(start, shown * bar, CR_LM_TIMELINE);
        for (cur = 0; cur < 4u; cur++)
            s->lm_lane[cur] = cu_lmc.lane[cur];
        s->lm_nlane = cu_lmc.nlane;
        s->lm_newlane = cu_lmc.newlane;
        s->lm_pos = (uint16_t)(pos >= start ? (pos - start) * 256u / bar : 0u);
        if (sn->lrep_on) {                         /* Replace: the held span (from its start, or the window's edge) */
            uint32_t e0 = sn->lrep0 >= start && sn->lrep0 <= pos ? sn->lrep0 - start : 0u;
            s->lm_erase0 = (uint16_t)(e0 * 256u / bar);
            s->lm_erase1 = (uint16_t)(s->lm_pos > s->lm_erase0 ? s->lm_pos : s->lm_erase0 + 1u);
        }
    } else {                                       /* idle, armed: the length big ("4 bars", "Free") */
        s->lm_mid = CR_LM_TEXT;
        cu_cpy(s->title, CU_LOOPLEN[cs.loop_len % CRL_NSYNC], sizeof s->title);
        s->title_col = CR_COL_NONE;
        if (cap == CRL_CAP_ARMED && cu_lp.adv_armed)
            cu_cpy(s->sub, cu_lp.adv_txt, sizeof s->sub);
        else if (cap == CRL_CAP_ARMED)
            cu_cpy(s->sub, "the first chord starts the take", sizeof s->sub);
    }
}

static void cu_layer_screen(cr_screen_t *s, uint32_t l)
{
    static const char *const PERF_ITEMS[7] = {"Strum", "Strum 2 Octaves", "Slop", "Arpeggiate", "Arp 2 Octaves",
                                              "Pattern", "Harp"};
    static const char *const FX_ITEMS[CU_NFX] = {"Reverb", "Chorus", "Delay", "Drive"};
    static const char *const SLOTS[10] = {"Slot 1", "Slot 2", "Slot 3", "Slot 4", "Slot 5", "Slot 6", "Slot 7",
                                          "Slot 8", "Slot 9", "Slot 10"};
    const char *eng[NENGINES];
    uint32_t k, n;
    switch (l) {
    case L_KEY: {                                  /* the knob row with the keyboard as its band (layers sheet 1) */
        static char seen[48];
        s->kind = CR_K_KNOBROW;
        s->kr_band = 1;
        s->orient = 1;
        s->col = CR_COL_YELLOW;
        s->title_col = CR_COL_WHITE;
        cu_cpy(s->label, "key", sizeof s->label);
        cu_cpy(s->footer, "MIN: minor \267 OCT-: back \267 HOME: home", sizeof s->footer);
        for (k = CU_ROOT0; cs.key_on && k < CU_NKEY; k++)
            if ((53u + k) % 12u == cs.tonic) {
                s->lit |= 1u << k;
                s->lit_col[k] = CR_COL_YELLOW;
                cu_cpy(s->key_label[k], CU_NOTE[cs.tonic], sizeof s->key_label[k]);
            }
        cu_key_cells(s);
        cu_row_trace(s, "key", seen, sizeof seen);
        break;
    }
    case L_PERF: {
        static char seen[48];
        cu_picker(s, PERF_ITEMS, 7, cs.perf_sel, CR_COL_WHITE, "perform",
                  "SELECT: mode \267 OCT-: back \267 HOME: home");
        s->kind = CR_K_KNOBROW;
        s->orient = 1;
        cu_perf_cells(s);
        cu_row_trace(s, "perf", seen, sizeof seen);
        break;
    }
    case L_FX: {                                   /* the knob row: the effect over KNOB 1..4's cells */
        static char seen[48];                      /* (the trace: the row's labels when they change) */
        cu_picker(s, FX_ITEMS, CU_NFX, cs.fx_sel, CR_COL_GREEN, "fx", "SELECT: effect \267 OCT-: back \267 HOME: home");
        s->kind = CR_K_KNOBROW;
        s->orient = 1;
        cu_fx_cells(s);
        cu_row_trace(s, "fx", seen, sizeof seen);
        break;
    }
    case L_BASS: {
        static char seen[48];
        cu_picker(s, CU_BASSMODE, 4, cs.bass_mode, CR_COL_ORANGE, "bass", "a root: preview \267 OCT-: back \267 HOME");
        s->kind = CR_K_KNOBROW;
        s->orient = 1;
        cu_bass_cells(s);
        cu_row_trace(s, "bass", seen, sizeof seen);
        break;
    }
    case L_EDIT:
        cu_engine_at(0, &n);
        for (k = 0; k < n && k < NENGINES; k++)
            eng[k] = ENGINES[cu_engine_at(k, 0)]->name;
        cu_picker(s, eng, n, (uint32_t)cu_layer_sel(L_EDIT), ce.part ? CR_COL_ORANGE : CR_COL_WHITE,
                  ce.part ? "bass engine \267 KNOB 1 its presets" : "engine \267 KNOB 1 its presets",
                  cs.pick_roots ? "roots: engines \267 OCT-: cancel \267 OCT+: ok"
                                : "roots: play \267 OCT-: cancel \267 OCT+: ok");
        cu_cpy(s->value, psnd[ce.part].name, sizeof s->value);
        if (psnd[ce.part].edited)
            cu_cat(s->value, "*", sizeof s->value);
        break;
    case L_LOOP:                                   /* the PLAY menu (no ring: a loop running shows the dial) */
        cu_loopmenu(s, cu_now());
        break;
    case L_SAVE:                                   /* loops: save / load / delete on the root-chosen slot */
        cu_picker(s, CU_SAVE_ACT, 3, cs.save_act, CR_COL_RED,
                  (cs.loop_used >> cs.loop_target) & 1u ? "holds a loop" : "empty",
                  "");                       /* (a plain picker: no ring; a loop running: the dial) */
        s->orient = 1;
        cu_cpy(s->value, SLOTS[cs.loop_target % 10u], sizeof s->value);
        cu_cpy(s->mid, "Loops", sizeof s->mid);
        s->mid_col = CR_COL_RED;
        break;
    case L_METRO: {                                /* layers sheet 7: the time signature over Click (KNOB 1) */
        static char seen[48];
        cu_picker(s, CU_SIG, CRL_NSIG, cs.metro_sig, CR_COL_WHITE, "metronome", "OCT-: back \267 HOME: home");
        s->kind = CR_K_KNOBROW;
        s->orient = 1;
        s->cell[0][0].flags = CR_CF_ON;
        cu_cpy(s->cell[0][0].label, "Click", sizeof s->cell[0][0].label);
        cu_int(s->cell[0][0].value, cs.metro_vol, 0, sizeof s->cell[0][0].value);
        s->cell[0][0].glyph = CR_G_BAR;
        s->cell[0][0].pct = (uint8_t)(cs.metro_vol * 255u / 100u);
        cu_hot_row(s, L_METRO);
        cu_row_trace(s, "metro", seen, sizeof seen);
        break;
    }
    default:
        break;
    }
}

static void cu_options_screen(cr_screen_t *s)
{
    s->batt = (uint8_t)cu_batt();                  /* the one page with the battery level */
    if (cu.opt_sel != O_ERASE)
        cu_erase_ask = 0;                          /* (left the entry: the confirmation starts again) */
    cu_picker(s, O_NAME, cu_opt_n(), cu.opt_sel, CR_COL_WHITE, "options \267 KNOB 1 sets", "");
    opt_text(cu.opt_sel, s->value, sizeof s->value);
    if (cu.opt_sel == O_SAFE)                      /* "Safe mode: flash data skipped \267 OCT-+OCT+ 5 s: update mode" */
        cu_cpy(s->label, "OCT-+OCT+ 5 s: update mode", sizeof s->label);
    else if (cu.opt_sel == O_ERASE)                /* "Flash data: erase and reboot", OCT+ twice */
        cu_cpy(s->label, cu_erase_ask ? "sounds, loops, settings go" : "OCT+ twice: erase, reboot", sizeof s->label);
    else if (cu.opt_sel == O_USB_IN)               /* what each USB entry does, under its value */
        cu_cpy(s->label, "records master, chord, bass", sizeof s->label);
    else if (cu.opt_sel == O_USB_LEVEL)            /* (the values stay Master / Fixed: the line says what they do) */
        cu_cpy(s->label, cs.usb_fixed ? "record full, MASTER: speaker" : "record level: MASTER knob", sizeof s->label);
}

/* the sound editor (design/choralroot-fm1-sound-editor-mockups.json): cr_edit.c */
static void cu_edit_screen(cr_screen_t *s, uint32_t now) { ce_screen(s, now); }

/* OPT + PRESETS (preset sheet 2): the engines, the one picked big, its neighbours peeking, the pool's size under it */
static void cu_epk_screen(cr_screen_t *s)
{
    const char *eng[NENGINES];
    uint32_t k, n, e = cu_engine_at(cu.epk_rank, 0);
    cu_engine_at(0, &n);
    for (k = 0; k < n && k < NENGINES; k++)
        eng[k] = ENGINES[cu_engine_at(k, 0)]->name;
    cu_picker(s, eng, n, cu.epk_rank, CR_COL_WHITE, "engine \267 OPT + PRESETS", "");
    s->orient = 1;
    cu_int(s->value, (int32_t)pool_count(e) - 1, 0, sizeof s->value);
    cu_cat(s->value, " presets", sizeof s->value);
}

/* SAVE (preset sheet 3, 4, 6): the choice (Overwrite / Save as new, the current name under it, "*" when edited), the
 * naming page (the place it takes, "FM6 \267 27", the name being typed), the red question */
static void cu_save_screen(cr_screen_t *s, uint32_t now)
{
    static const char *const CHOICE[2] = {"Overwrite", "Save as new"};
    uint32_t part = ce.save_part, e = trk[part ? CR_PART_BASS : CR_PART_CHORD].eng_req % NENGINES, col;
    char b[24];
    col = part ? CR_COL_ORANGE : CR_COL_WHITE;
    if (ce.del_ask) {                              /* the place huge in red, the name (reset: the factory one's) */
        s->kind = CR_K_BIG;
        cu_2d(s->value, cu_part_pos(part), sizeof s->value);
        if (ce.del_ask == 2u && psnd[part].kind == PK_USER && cb_bound(psnd[part].slot) != PF_NONE)
            cu_cpy(s->sub, ENGINES[e]->presets[cb_bound(psnd[part].slot)].name, sizeof s->sub);
        else
            cu_cpy(s->sub, psnd[part].name, sizeof s->sub);
        cu_cpy(s->label, ce.del_ask == 2u ? "reset to factory?" : "delete?", sizeof s->label);
        cu_cpy(s->footer, ce.del_ask == 2u ? "OCT+: reset \267 OCT-: keep" : "OCT+: delete \267 OCT-: keep",
               sizeof s->footer);
        s->col = CR_COL_RED;
        s->size = 96;
        return;
    }
    if (!ce.save_step) {                           /* the choice */
        cu_cpy(b, part ? "save bass \267 " : "save \267 ", sizeof b);
        cu_cat(b, ENGINES[e]->name, sizeof b);
        cu_picker(s, CHOICE, 2, ce.save_choice, col, b, "OCT+: save \267 OCT-: cancel");
        cu_cpy(s->value, psnd[part].name, sizeof s->value);
        if (psnd[part].edited)
            cu_cat(s->value, " *", sizeof s->value);
        return;
    }
    s->kind = CR_K_TEXT;
    cu_cpy(s->title, part ? "save bass" : "save sound", sizeof s->title);
    s->title_col = part ? CR_COL_ORANGE : CR_COL_RED;
    cu_place(s->lines[0].t, e, ce.save_pos, " \267 ");
    s->lines[0].px = 15;
    s->lines[0].col = CR_COL_GREY;
    cn_line(s->lines[1].t, sizeof s->lines[1].t, now);
    s->lines[1].px = 36;
    s->lines[1].col = cn.pristine ? CR_COL_GREY : CR_COL_WHITE;
    s->lines[1].bold = 1;
    s->lines[2].px = 12;
    cu_cpy(s->lines[3].t, "keys: letters \267 F#4: delete", sizeof s->lines[3].t);
    s->lines[3].px = 12;
    s->lines[3].col = CR_COL_GREY;
    cu_cpy(s->lines[4].t, "SAVE: save \267 KNOB 2: letter", sizeof s->lines[4].t);
    s->lines[4].px = 12;
    s->lines[4].col = CR_COL_GREY;
    s->n_lines = 5;
    cu_cpy(s->footer, "OCT+: save \267 OCT-: cancel", sizeof s->footer);
}

/* the SCOPE view's trace: audio.c's ring of the master output (scope_buf, every 2nd sample, written in the audio
 * ISR), 240 samples (10.9 ms) from the steepest rising zero crossing of the first 272 (a held chord stands
 * still), auto-scaled as Felucca's graph_scope with a floor (silence: a flat line) */
static void cu_scope(cr_screen_t *s)
{
    static int16_t snap[SCOPE_N];
    uint32_t w = scope_w, i, trig = 0;
    int32_t peak = 2048, best = 0;
    for (i = 0; i < SCOPE_N; i++) {
        int32_t v = snap[i] = scope_buf[(w + i) & (SCOPE_N - 1u)];
        if (v < 0) v = -v;
        if (v > peak) peak = v;
    }
    for (i = 1; i < SCOPE_N - CR_WAVE_N; i++)
        if (snap[i - 1] < 0 && snap[i] >= 0 && snap[i] - snap[i - 1] > best) {
            best = snap[i] - snap[i - 1];
            trig = i;
        }
    for (i = 0; i < CR_WAVE_N; i++)
        s->wave[i] = (int8_t)(snap[trig + i] * 127 / peak);
}

static void cu_view_screen(cr_screen_t *s)
{
    const cr_snap_t *sn = &cr_snap;
    const cr_chord_info_t *ci = &sn->ci;
    uint32_t pm = sn->perform_mode;
    if (cs.view == V_CHORD && sn->perform_on && ci->sounding && pm != CR_PM_STRUM && pm != CR_PM_SLOP) {
        char v[8], sub[12];
        int32_t p = CU_PERF_KNOB[pm][0];
        s->kind = CR_K_ARP;                        /* perform in motion: the sounding note hops along */
        cu_name(&s->name, ci);
        cu_notes(s, ci);
        s->pos = (int8_t)(sn->perf_pos < (int)s->n_notes ? sn->perf_pos : -1);
        s->hop_col = CR_COL_WHITE;
        cu_param_text((uint32_t)p, cs.par[pm][p], v, sub);
        cu_cpy(s->line, CU_PERF[cs.perf_sel].short_name, sizeof s->line);
        s->line[0] = (char)(s->line[0] | 0x20);
        cu_cat(s->line, " ", sizeof s->line);
        cu_cat(s->line, p == CR_P_DIV || p == CR_P_PATTERN ? sub : v, sizeof s->line);
        s->line_col = CR_COL_MID;
        return;
    }
    switch (cs.view) {
    case V_SCOPE:                                 /* the sound itself: one bold line, the chord small on top */
        s->kind = CR_K_SCOPE;
        s->col = CR_COL_WHITE;
        if (!s->mid[0] && ci->sounding) {
            cu_cpy(s->mid, ci->root, sizeof s->mid);
            cu_cat(s->mid, ci->qual, sizeof s->mid);
            cu_cat(s->mid, ci->sup, sizeof s->mid);
        }
        cu_scope(s);
        break;
    case V_KEYBOARD:
        s->kind = CR_K_KEYBOARD;
        cu_name(&s->name, ci);
        s->lit = ci->sounding ? cu_lit(ci) : 0u;
        break;
    case V_NOTES: {
        uint32_t i;
        s->kind = CR_K_TEXT;
        cu_cpy(s->title, ci->root, sizeof s->title);
        cu_cat(s->title, ci->qual, sizeof s->title);
        cu_cat(s->title, ci->sup, sizeof s->title);
        s->title_col = CR_COL_WHITE;
        for (i = 0; i < ci->nnotes && i < CR_LINES_MAX; i++) {
            char b[5];
            cr_note_name(ci->notes[i], b);
            cu_cpy(s->lines[i].t, b, sizeof s->lines[i].t);
            s->lines[i].px = 20;
            s->lines[i].bold = 1;
            s->lines[i].center = 1;
            s->lines[i].col = CR_COL_WHITE;
        }
        s->n_lines = (uint8_t)i;
        break;
    }
    case V_GEEK: {
        static const char *const STYLE[3] = {"simple", "advanced", "free"};
        s->kind = CR_K_GEEK;
        cu_name(&s->name, ci);
        cu_notes(s, ci);
        s->lit = ci->sounding ? cu_lit(ci) : 0u;
        cu_cpy(s->lines[0].t, "voicing ", sizeof s->lines[0].t);
        cu_int(s->lines[0].t + 8, sn->voicing, 1, sizeof s->lines[0].t - 8u);
        cu_cat(s->lines[0].t, " \267 ", sizeof s->lines[0].t);
        cu_cat(s->lines[0].t, STYLE[sn->playstyle % 3u], sizeof s->lines[0].t);
        cu_cpy(s->lines[1].t, sn->bass_on ? "bass on \267 " : "bass off \267 ", sizeof s->lines[1].t);
        {
            char b[8];
            cu_int(b, cs.bpm, 0, sizeof b);
            cu_cat(s->lines[1].t, b, sizeof s->lines[1].t);
        }
        cu_cat(s->lines[1].t, " bpm", sizeof s->lines[1].t);
        if (bootguard_unclean(&bootguard)) {     /* the last boot crashed (or SAFE MODE): "boot wdt stage 13" */
            char b[12];
            cu_cpy(s->lines[2].t, "boot ", sizeof s->lines[2].t);
            cu_cat(s->lines[2].t, bootguard_class_name(bootguard.cls), sizeof s->lines[2].t);
            cu_cat(s->lines[2].t, " stage ", sizeof s->lines[2].t);
            cu_int(b, (int32_t)felucca_dbg.prev_stage, 0, sizeof b);
            cu_cat(s->lines[2].t, b, sizeof s->lines[2].t);
        } else {   /* the audio ISR, the last second (audio.c cpu_window): its longest half in us of 2902, halves late */
            char b[12];
            cu_cpy(s->lines[2].t, "isr ", sizeof s->lines[2].t);
            cu_int(b, (int32_t)cpu_last.max_all_us, 0, sizeof b);
            cu_cat(s->lines[2].t, b, sizeof s->lines[2].t);
            cu_cat(s->lines[2].t, "us \267 late ", sizeof s->lines[2].t);
            cu_int(b, (int32_t)felucca_dbg.late, 0, sizeof b);
            cu_cat(s->lines[2].t, b, sizeof s->lines[2].t);
        }
        s->n_lines = 3;
#if CR_CPU1
        {   /* the second core (cr_cpu1.h, docs/DUALCORE.md): "c1 ok 1234 \267 stk 828" / "c1 failed" / "c1 gave up";
             * then the ISR's stages, the last second's average in us per half: "t107 c443 b135 f730 m159 w102" (the tick,
             * the chord part, the bass part, the rest of the mix and the FX, the master and the click, the wait) */
            char b[12];
            uint32_t k;
            static const char *const SN[CRP_N] = {"t", "c", "b", "f", "m", "w"};   /* tick, chord, bass, fx, master, wait */
            cu_cpy(s->lines[3].t, "c1 ", sizeof s->lines[3].t);
            cu_cat(s->lines[3].t, cr_c1_name(), sizeof s->lines[3].t);
            if (cr_dbg.c1 == C1_OK) {
                cu_cat(s->lines[3].t, " ", sizeof s->lines[3].t);
                cu_int(b, (int32_t)cr_dbg.c1_jobs, 0, sizeof b);
                cu_cat(s->lines[3].t, b, sizeof s->lines[3].t);
                cu_cat(s->lines[3].t, " \267 stk ", sizeof s->lines[3].t);
                cu_int(b, (int32_t)cr_dbg.c1_stack, 0, sizeof b);
                cu_cat(s->lines[3].t, b, sizeof s->lines[3].t);
            }
            s->lines[4].t[0] = 0;
            for (k = 0; k < CRP_N; k++) {
                if (k == CRP_WAIT && cr_dbg.c1 != C1_OK)
                    break;
                cu_cat(s->lines[4].t, k ? " " : "", sizeof s->lines[4].t);
                cu_cat(s->lines[4].t, SN[k], sizeof s->lines[4].t);
                cu_int(b, (int32_t)cr_dbg.avg_us[k], 0, sizeof b);
                cu_cat(s->lines[4].t, b, sizeof s->lines[4].t);
            }
            s->n_lines = 5;
        }
#endif
        cu_cpy(s->right, "Trans ", sizeof s->right);
        if (cs.transpose)
            cu_int(s->right + 6, cs.transpose, 1, sizeof s->right - 6u);
        else
            cu_cat(s->right, "+0", sizeof s->right);
        break;
    }
    default:
        s->kind = CR_K_CHORD;
        cu_name(&s->name, ci);
        cu_notes(s, ci);
        s->line_col = CR_COL_MID;
        break;
    }
}

/* the screen of this frame, top down, the first that applies (INTEGRATION section 5) */
static void cr_build_screen(cr_screen_t *s, uint32_t now)
{
    const cr_snap_t *sn = &cr_snap;
    uint32_t l = cu_layer();
    cr_screen_clear(s);
    cu_header(s);
    cu_downbeat(now);
    if (cc.on) {                                  /* calibration: over everything */
        cu_calib_screen(s);
        return;
    }
    if (sn->ci.sounding || (cu.kheld >> CU_ROOT0))
        cu.last_sound = now;
    if (cr_safe && now - cu_boot_ms < CR_SAFE_MS && !cu.bheld && !cu.kheld) {   /* 0. SAFE MODE, after the boot */
        cu_safe_screen(s);
        return;
    }
    /* 1. a message: PANIC (the whole panel red), else a box over whatever is below */
    if (cu.msg.big == 1u && (int32_t)(cu.msg.until - now) > 0) {
        s->kind = CR_K_BIG;
        cu_cpy(s->value, cu.msg.text, sizeof s->value);
        cu_cpy(s->label, cu.msg.label, sizeof s->label);
        s->block = CR_COL_RED;
        s->col = CR_COL_WHITE;
        s->size = 64;
        return;
    }
    /* 1b. the count-in: the beats to go huge in red, the ring drawing itself in (over popups and layers) */
    if (sn->lcap == CRL_CAP_COUNTIN) {
        s->kind = CR_K_BIG;
        cu_int(s->value, (int32_t)(sn->lbeat ? sn->lbeat : 1u), 0, sizeof s->value);
        cu_cpy(s->label, "count-in", sizeof s->label);
        s->col = CR_COL_RED;
        s->size = 104;
        cu_ring_on(s);
        return;
    }
    /* 1c. undo: the layers left, huge in red; the ring stays */
    if (cu.msg.big == 2u && (int32_t)(cu.msg.until - now) > 0) {
        s->kind = CR_K_BIG;
        cu_cpy(s->value, cu.msg.text, sizeof s->value);
        cu_cpy(s->label, cu.msg.label, sizeof s->label);
        s->col = cu.msg.col;
        s->size = 104;
        cu_ring_on(s);
        return;
    }
    if (!cu.msg.big && (int32_t)(cu.msg.until - now) > 0) {
        cu_cpy(s->message, cu.msg.text, sizeof s->message);
        s->message_col = cu.msg.col;
    }
    /* 1d. OPT held + PRESETS: the engine picker (over popups and layers, while OPT is held) */
    if (cu.epk_on) {
        cu_epk_screen(s);
        cu_dial(s, now);
        return;
    }
    /* 2. a knob's meter (the voicing: the chord with its voicing line while one is shown) */
    if ((int32_t)(cu.pop.until - now) > 0) {
        char b[8];
        if (cu.pop.kind == PU_VOICING) {
            cu_int(b, sn->voicing, 1, sizeof b);
            if (!sn->voicing)
                cu_cpy(b, "0", sizeof b);
            if (sn->ci.valid) {
                s->kind = CR_K_CHORD;
                cu_name(&s->name, &sn->ci);
                cu_notes(s, &sn->ci);
                cu_cpy(s->line, "voicing ", sizeof s->line);
                cu_cat(s->line, b, sizeof s->line);
                s->line_col = CR_COL_BLUE;
            } else {
                cu_meter(s, b, "", "voicing", CR_COL_BLUE, (uint32_t)(sn->voicing + 12) * 256u / 24u, 12);
            }
        } else if (cu.pop.kind == PU_BASS_VOICING) {
            cu_int(b, sn->bass_voicing, 1, sizeof b);
            if (!sn->bass_voicing)
                cu_cpy(b, "0", sizeof b);
            cu_meter(s, b, "oct", "bass register", CR_COL_ORANGE, (uint32_t)(sn->bass_voicing + 2) * 256u / 6u, 6);
        } else {
            cu_meter(s, cu.pop.value, cu.pop.sub, cu.pop.label, cu.pop.col, cu.pop.pct, cu.pop.segs);
            s->sub_mark = cu.pop.mark;
        }
        cu_dial(s, now);
        return;
    }
    /* the loop: the corner dial in every state (playing, armed, recording, overdubbing; the ring only on the count-in
     * and undo screens above), not in the sound editor (no top line) nor on the Options pages: the REC and LOOP LEDs */
    /* 3. an open layer, a page; 4. Options */
    if (l) {
        cu_layer_screen(s, l);
        cu_dial(s, now);
        return;
    }
    if (cu.page == PG_EDIT) {
        cu_edit_screen(s, now);
        return;
    }
    if (cu.page == PG_SAVE) {
        cu_save_screen(s, now);
        cu_dial(s, now);
        return;
    }
    if (cu.opt_open) {
        cu_options_screen(s);
        return;
    }
    /* 6. idle: nothing played yet, or nothing sounding for CR_IDLE_MS */
    if (!sn->ci.valid || (!sn->ci.sounding && now - cu.last_sound >= CR_IDLE_MS && cs.view != V_SCOPE)) {
        cu_stripes(s);
        cu_dial(s, now);
        return;
    }
    /* 5. the View */
    cu_view_screen(s);
    cu_dial(s, now);
}

/* the animations' clock: restarted when what animates changed (cr_anim.c, cr_draw.c CR_A_*) */
static struct {
    uint8_t kind, sel, anim, sweeping;
    int8_t slide;
    uint16_t pct, pct_from;
    uint32_t sweep_t0;
    cr_name_t name, from;
    char label[32], value[24];
    uint8_t turn;                                 /* params: ce.turn last seen */
} ca;
static cr_anim_t cu_anim;

static int cu_name_eq(const cr_name_t *a, const cr_name_t *b)
{
    return cu_eq(a->root, b->root) && cu_eq(a->quality, b->quality) && cu_eq(a->sup, b->sup);
}

static void cu_animate(cr_screen_t *s, uint32_t now)
{
    int kind_changed = s->kind != ca.kind;
    static const cr_name_t NONAME;
    /* the first chord sweeps the idle stripes off, then lands */
    if ((s->kind == CR_K_CHORD || s->kind == CR_K_ARP) && ca.kind == CR_K_STRIPES && cr_motion != CR_MOTION_OFF) {
        if (!ca.sweeping) {
            ca.sweeping = 1;
            ca.sweep_t0 = now;
            cr_anim_mark(&cu_anim, now);
        }
        if (now - ca.sweep_t0 < (cr_motion == CR_MOTION_CALM ? 160u : 320u)) {
            cr_screen_clear(s);
            cu_header(s);
            cu_stripes(s);
            cu_dial(s, now);
            s->anim = CR_A_STRIPES | CR_A_SWEEP;
            return;
        }
    }
    ca.sweeping = 0;
    switch (s->kind) {
    case CR_K_CHORD:
        if (kind_changed) {
            ca.from = NONAME;
            ca.anim = CR_A_SQUEEZE;
            cr_anim_mark(&cu_anim, now);
        } else if (!cu_name_eq(&s->name, &ca.name)) {
            ca.from = ca.name;
            ca.anim = CR_A_SQUEEZE;
            cr_anim_mark(&cu_anim, now);
        }
        s->from = ca.from;
        s->anim = ca.anim;
        ca.name = s->name;
        break;
    case CR_K_PICKER:
    case CR_K_KNOBROW:                             /* (the knob row's band slides as a picker) */
        if (kind_changed || !cu_eq(s->label, ca.label)) {
            ca.anim = 0;
            ca.slide = 0;
            cr_anim_mark(&cu_anim, now);
        } else if (s->sel != ca.sel) {
            ca.slide = (int8_t)(s->sel > ca.sel ? 1 : -1);
            ca.anim = CR_A_SLIDE;
            cr_anim_mark(&cu_anim, now);
        }
        s->slide = ca.slide;
        s->anim = ca.anim;
        ca.sel = s->sel;
        break;
    case CR_K_METER:
        if (kind_changed || !cu_eq(s->label, ca.label)) {
            ca.pct_from = 0;
            ca.anim = CR_A_FILL;
            cr_anim_mark(&cu_anim, now);
        } else if (s->pct != ca.pct || !cu_eq(s->value, ca.value)) {
            ca.pct_from = ca.pct;
            ca.anim = CR_A_FILL;
            cr_anim_mark(&cu_anim, now);
        }
        if (cu.pop.jump && (int32_t)(cu.pop.until - now) > 0) {   /* the picker's preview: the bar jumps */
            ca.anim &= (uint8_t)~CR_A_FILL;
            ca.pct_from = s->pct;
        }
        s->pct_from = ca.pct_from;
        s->anim = ca.anim;
        s->size = (uint8_t)cr_spring(92, 104, cu_anim.t0, 240, now);   /* the number springs in */
        ca.pct = s->pct;
        break;
    case CR_K_BIG: {                               /* a new number springs in (as the meters') */
        int32_t to = s->size ? s->size : 118;
        if (kind_changed || !cu_eq(s->value, ca.value))
            cr_anim_mark(&cu_anim, now);
        s->size = (uint8_t)cr_spring(to * 3 / 4, to, cu_anim.t0, 240, now);
        break;
    }
    case CR_K_PARAMS:                              /* a page turned: its columns slide in from that side */
        if (kind_changed) {
            ca.anim = 0;
            ca.slide = 0;
            cr_anim_mark(&cu_anim, now);
        } else if (ce.turn != ca.turn && cr_motion != CR_MOTION_OFF) {
            ca.slide = ce.dir;
            ca.anim = CR_A_SLIDE;
            cr_anim_mark(&cu_anim, now);
        }
        ca.turn = ce.turn;
        s->slide = ca.slide;
        s->anim = ca.anim;
        break;
    case CR_K_STRIPES:
        if (kind_changed)
            cr_anim_mark(&cu_anim, now);
        s->anim = (uint8_t)((cr_motion == CR_MOTION_OFF ? 0u : CR_A_STRIPES) | (s->anim & CR_A_INTRO));
        break;
    default:
        if (kind_changed)
            cr_anim_mark(&cu_anim, now);
        break;
    }
    ca.kind = s->kind;
    if (s->kind != CR_K_CHORD)
        ca.name = s->kind == CR_K_ARP ? s->name : NONAME;
    cu_cpy(ca.label, s->label, sizeof ca.label);
    cu_cpy(ca.value, s->value, sizeof ca.value);
}

/* --------------------------------------------------------------- frames --- */
static cr_screen_t cu_scr __attribute__((section(".pool")));   /* (cleared every frame: cr_screen_clear) */

/* MIDI in (cr_out.c): the clock's tempo mirrored on screen, CC 7 / 91 / 93 / 94 and program changes applied as the
 * knobs apply them (with their meter) */
static void cu_midi_poll(void)
{
    static uint32_t bpm_n;
    crm_act_t a;
    if (bpm_n != cr_in_bpm_n) {
        bpm_n = cr_in_bpm_n;
        cs.bpm = cr_in_bpm;                       /* (the ISR set the engine's: no CRE_TEMPO back) */
        song.g[G_BPM] = (int16_t)(cs.bpm < 40u ? 40u : cs.bpm > 240u ? 240u : cs.bpm);
        cu_trace("bpm: %u (clock in)\n", (unsigned)cs.bpm);
    }
    while (cr_min_take(&a)) {
        uint32_t p = a.part ? CR_PART_BASS : CR_PART_CHORD, i;
        if (a.kind == CRM_PROGRAM) {
            if (!p) {                             /* the chord part's pool position (0 INIT) */
                if (a.v >= pool_count(trk[CR_PART_CHORD].eng_req % NENGINES))
                    continue;
                cu_sound_go(a.v);
                cu_sound_popup(0);
                cu_trace("sound: part 0 pos %u %s (program change)\n", (unsigned)cs.sound, psnd[0].name);
            } else {
                if (a.v > pool_count(trk[CR_PART_BASS].eng_req % NENGINES))   /* 0 OFF, then the pool */
                    continue;
                cu_bass_go(a.v);
                cu_sound_popup(1);
                cu_trace("sound: part 1 pos %u %s (program change)\n", (unsigned)cs.bass_sound,
                         cs.bass_sound ? psnd[1].name : "off");
            }
        } else if (a.kind == CRM_LEVEL) {
            fm1_irq_off();
            trk[p].p[P_LEVEL] = (int16_t)a.v;
            fm1_irq_on();
            cu_popup_num(a.v, 0, "", p ? "bass level" : "level", p ? CR_COL_ORANGE : CR_COL_WHITE, 0, 127, 12);
            cu_trace("level: part %u %u (cc 7)\n", (unsigned)p, (unsigned)a.v);
        } else {
            uint32_t id = a.kind == CRM_SEND_REV ? P_REV : a.kind == CRM_SEND_CHO ? P_CHOR : P_DLY;
            for (i = 0; i < CU_NFX; i++)
                if (CU_FX[i].send == id)
                    break;
            if (!p && i < CU_NFX) {               /* part 0's sends are the FX amounts */
                cs.fx_amt[i] = a.v;
                cs.fx_on = 1;
                cu_fx_apply();
            } else {
                fm1_irq_off();
                trk[p].p[id] = (int16_t)a.v;
                fm1_irq_on();
            }
            {
                char lb[24];
                cu_cpy(lb, i < CU_NFX ? CU_FX[i].name : "send", sizeof lb);
                lb[0] = (char)(lb[0] | 0x20);
                cu_popup_num(a.v * 99 / 127, 0, "", lb, p ? CR_COL_ORANGE : CR_COL_GREEN, 0, 99, 12);
            }
            cu_trace("send: part %u %s %u (cc)\n", (unsigned)p, i < CU_NFX ? CU_FX[i].name : "?", (unsigned)a.v);
        }
    }
}

static void cr_ui_frame(void)                      /* after the scan: the engine's state, the LEDs */
{
    cpu_window(fm1_ms);                            /* audio.c: the last second's ISR load (console `cpu`, GEEK OUT) */
#if CR_CPU1
    if (cr_dbg.c1 == C1_OK)
        cr_dbg.c1_stack = cr_c1_stack_used();      /* core 1's stack high-water mark (its C1MK fill) */
#endif
    if (cpk.on && cu.lock != L_EDIT)               /* the picker closed another way (OPT, SAVE, BASS + EDIT): kept */
        cu_pick_end(1);
    if (cu.epk_on && !cu_shift())                  /* OPT + PRESETS: OPT let go another way */
        cu_epk_commit();
    cu_midi_poll();
    cr_snapshot();
#ifdef CR_TRACE
    {                                              /* the emulator's log: the sounding chord as it changes */
        static char last[24];
        char nm[24];
        nm[0] = 0;
        if (cr_snap.ci.sounding) {
            cu_cpy(nm, cr_snap.ci.root, sizeof nm);
            cu_cat(nm, cr_snap.ci.qual, sizeof nm);
            cu_cat(nm, cr_snap.ci.sup, sizeof nm);
            cu_cat(nm, cr_snap.ci.nnotes == 1u ? " (1 note)" : "", sizeof nm);
        }
        if (!cu_eq(nm, last)) {
            cu_cpy(last, nm, sizeof last);
            cu_trace("chord: %s\n", nm[0] ? nm : "-");
        }
    }
#endif
    cu_loop_frame();
    cr_leds();
}

static void cr_ui_draw(void)
{
    uint32_t now = cu_now();
    cr_build_screen(&cu_scr, now);
    cu_animate(&cu_scr, now);
    cr_draw(&cu_scr, cr_anim_ms(&cu_anim, now));
}

/* the pool position of engine e's factory preset named so (none: 1, the first) */
static uint32_t cu_pool_find(uint32_t e, const char *name)
{
    const engine_t *en = ENGINES[e % NENGINES];
    uint32_t k;
    for (k = pool_f0(e); k < en->npresets; k++)
        if (cu_eq(en->presets[k].name, name))
            return pool_pos_factory(e, k);
    return 1u;
}

/* power-on: the engine, the two parts' sounds, the settings mirror (the engine's defaults), the LED map */
static void cr_ui_init(void)
{
    uint32_t m, p, k;
    CR_STAGE(BS_UI_INIT);
    settings_init();                              /* (panel.c: the palette, LOWCUT, HOLD) */
    panel_init();
    if (!cr_ring.state)
        cr_ring_build();                          /* the loop ring's table (cr_draw.c): now, not at the first count-in */
    palette_set(NPALETTES - 1u);                  /* MOD: ChoralRoot's (gfx.c palettes: the last) */
    fm6_init();                                   /* every part's FM6 patch: the init voice */
    for (k = 0; k < G_COUNT; k++)
        song.g[k] = GP[k].def;
    for (k = 0; k < NPART; k++)
        for (m = 0; m < P_E0; m++)
            trk[k].p[m] = TP[m].def;
    song.master_q12 = 2048;
    cr_out_init();
    cr_bank_boot();                               /* (the device: persist_boot did it, with the flash) */
    cb_init();
    cu_led_init();
    cs.playstyle = cr.playstyle;
    cs.extadd = cr.extadd;
    cs.secret = cr.secret;
    cs.key_on = cr.key_on;
    cs.tonic = cr.tonic;
    cs.scale = cr.scale;
    cs.transpose = cr.transpose;
    cs.bass_mode = cr.bass_mode;
    cs.perform_on = cr.perform_on;
    cs.sticky = cr.sticky;
    cs.vel = 100;
    for (m = 0; m < CR_PM_COUNT; m++)
        for (p = 0; p < CR_P_COUNT; p++)
            cs.par[m][p] = (int16_t)cr_get_param(&cr, (cr_pmode_t)m, (cr_eparam_t)p);
    for (k = 0; k < 7u; k++)
        if (CU_PERF[k].mode == cr.perform_mode) {
            cs.perf_sel = (uint8_t)k;
            break;
        }
    cs.ch[CR_STREAM_MAIN] = 1;
    cs.ch[CR_STREAM_BASS] = 2;
    cs.ch[CR_STREAM_RAW] = 0;                     /* (Orchid: raw chord off; Options turns it on, channel 3) */
    cs.fx_on = 1;
    cs.view = V_CHORD;
    cu.armed = NB;
    cu.opt_sel = O_STYLE;
    /* the parts' default levels (LEVEL steps 0.5 dB; Felucca's 104 is -4 dB): the chord -6 dB, the bass -6 dB, so a
     * 6-note chord with the bass stays under the master limiter (INTEGRATION Performance, Defaults); a sound's load
     * keeps the part's level (cu_kept), the bank's loud sounds carry a trim after it (cr_bank.c), MASTER makes up the
     * loudness */
    trk[CR_PART_CHORD].p[P_LEVEL] = (int16_t)(TP[P_LEVEL].def - 12);
    trk[CR_PART_BASS].p[P_LEVEL] = (int16_t)(TP[P_LEVEL].def - 12);
    for (k = 2; k < NPART; k++)                   /* parts 3 and 4: unused, silent */
        trk[k].p[P_MUTE] = 1;
    for (k = 0; k < (uint32_t)NENG_SHOWN; k++)      /* each engine's pool: its first preset until played */
        cs.pool_pos[0][k] = cs.pool_pos[1][k] = 1;
    cu_pool_load(0, ENGI_FM6, cu_pool_find(ENGI_FM6, "TINE EP"));   /* the power-on sounds: FM6 TINE EP, the bass VA
                                                                    * DEEP SUB (ALGORITHM at OFF: BASS tap brings it;
                                                                    * ANALOG SUB BASS until 0.14) */
    cu_sends_to_fx();
    cu_pool_load(1, ENGI_VA, cu_pool_find(ENGI_VA, "DEEP SUB"));
    ce.page = CP_ENV;
    ce.col = -1;
    for (k = 0; k < 2u; k++)
        trk[k].engine = trk[k].eng_req;           /* (power-on: no fade) */
    cs.bass_sound = 0;                            /* ALGORITHM at OFF: the bass off */
    cs.bass_on = 0;
    cr.bass_on = 0;
    cs.split = cr.split_pc;
    cs.loop_count_in = 1;
    cs.loop_level = 100;
    cs.metro_vol = 70;
    cs.pick_roots = 1;
    cs.usb_in = 1;                                /* (the record's, cr_settings_load; usb.c got it at boot) */
    cu_route();
    cu_set_tempo(cr.bpm ? cr.bpm : 120);
#if CR_HAVE_SETTINGS
    CR_STAGE(BS_UI_SETTINGS);
    cr_settings_load();                           /* the stored settings, at power-on on both builds */
#endif
    CR_STAGE(BS_LOOPS);
    cu_loop_conf();
    cu_loop_scan();                               /* (SAFE MODE: no slot read, core.h ST_BLOCKED) */                               /* the slots in flash; the last one used back in RAM (stopped) */
    if ((cs.loop_used >> cs.loop_slot) & 1u)
        cu_loop_load(cs.loop_slot, 1);            /* (a load only: nothing in RAM to keep yet) */
    cu.msg.until = 0;
#if FELUCCA_SEQ
    song.grid = 2;                                /* seq.c's keyboard never plays (keyboard_block: every key silent) */
#endif
    if (cr_safe)
        cu.opt_sel = O_SAFE;                      /* Options opens on Safe Mode (then Flash Data) */
    cu_boot_ms = cu_now();                        /* the splash: the idle stripes' first CR_SPLASH_MS */
    cr_anim_mark(&cu_anim, cu_boot_ms);
    cr_draw_invalidate();
}
